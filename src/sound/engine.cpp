#include "crpp/sound/engine.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <random>

namespace crpp::sound {

namespace {

constexpr auto idle_check = std::chrono::seconds(1);
constexpr auto open_retry = std::chrono::seconds(5);

// Volume sliders are perceptually even at about the square of the amplitude.
float gain_of(int percent) noexcept {
  const float v = static_cast<float>(std::clamp(percent, 0, 100)) / 100.0f;
  return v * v;
}

std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

std::vector<float> load_recording(const std::filesystem::path& path, std::uint32_t rate) {
  const auto pcm = parse_wav(read_bytes(path));
  if (!pcm) return {};
  return Resampler(pcm->rate, rate).run(pcm->samples);
}

Engine::Engine(AudioDevice& device, std::filesystem::path sounds_dir)
    : device_(device), sounds_dir_(std::move(sounds_dir)), random_(std::random_device{}()) {
  device_.set_on_changed([this] {
    {
      std::lock_guard lock(mutex_);
      device_changed_ = true;
    }
    wake_.notify_one();
  });
}

Engine::~Engine() {
  stop();
  device_.set_on_changed(nullptr);
}

void Engine::start() {
  if (worker_.joinable()) return;
  {
    std::lock_guard lock(mutex_);
    stopping_ = false;
  }
  worker_ = std::thread([this] { run(); });
}

void Engine::stop() {
  if (!worker_.joinable()) return;
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
  }
  wake_.notify_one();
  worker_.join();
}

void Engine::set_settings(Settings settings) {
  {
    std::lock_guard lock(mutex_);
    new_settings_ = std::move(settings);
  }
  wake_.notify_one();
}

void Engine::set_members(std::vector<Member> members) {
  {
    std::lock_guard lock(mutex_);
    new_members_ = std::move(members);
  }
  wake_.notify_one();
}

void Engine::set_member_volume(std::string cid, int percent) {
  if (cid.empty()) return;
  {
    std::lock_guard lock(mutex_);
    new_volumes_.push_back({std::move(cid), std::clamp(percent, 0, 100)});
  }
  wake_.notify_one();
}

void Engine::clear_member_volumes() {
  {
    std::lock_guard lock(mutex_);
    new_volumes_.push_back({});
  }
  wake_.notify_one();
}

void Engine::push_cue(CueKind kind, std::string_view name) {
  {
    std::lock_guard lock(mutex_);
    new_cues_.push_back({kind, std::string(name)});
  }
  wake_.notify_one();
}

void Engine::play_notification(std::string_view name) { push_cue(CueKind::Notification, name); }
void Engine::preview_notification(std::string_view name) { push_cue(CueKind::Preview, name); }
void Engine::play_system(std::string_view name) { push_cue(CueKind::System, name); }

void Engine::check_device() {
  {
    std::lock_guard lock(mutex_);
    check_device_ = true;
  }
  wake_.notify_one();
}

void Engine::set_on_open_failed(std::function<void()> failed) {
  std::lock_guard lock(mutex_);
  on_open_failed_ = std::move(failed);
}

void Engine::push(const Event& event) noexcept {
  {
    std::lock_guard lock(mutex_);
    if (pending_count_ == pending_.size()) return;  // the worker is far behind
    pending_[pending_count_++] = event;
  }
  wake_.notify_one();
}

void Engine::play_own(wire::Kind kind) noexcept { push({0, kind, true}); }
void Engine::play_peer(std::uint32_t slot, wire::Kind kind) noexcept { push({slot, kind, false}); }

void Engine::run() {
  std::array<Event, max_pending> batch;
  for (;;) {
    std::size_t count = 0;
    bool changed = false;
    std::optional<Settings> settings;
    std::optional<std::vector<Member>> members;
    std::vector<VolumeChange> volumes;
    std::vector<Cue> cues;
    std::function<void()> check_failed;
    bool check = false;
    {
      std::unique_lock lock(mutex_);
      const auto ready = [&] {
        return stopping_ || device_changed_ || pending_count_ > 0 || new_settings_ || new_members_ ||
               !new_volumes_.empty() || !new_cues_.empty() || check_device_;
      };
      // Asleep with no timer unless a stream is running and must be stopped later.
      if (running_) wake_.wait_for(lock, idle_check, ready);
      else wake_.wait(lock, ready);
      if (stopping_) break;
      std::copy_n(pending_.begin(), pending_count_, batch.begin());
      count = pending_count_;
      pending_count_ = 0;
      changed = std::exchange(device_changed_, false);
      settings = std::exchange(new_settings_, std::nullopt);
      members = std::exchange(new_members_, std::nullopt);
      volumes.swap(new_volumes_);
      cues.swap(new_cues_);
      check = std::exchange(check_device_, false);
      if (check) check_failed = on_open_failed_;
    }
    if (changed) close_device();
    if (settings) {
      const bool moved = settings->device != settings_.device;
      settings_ = std::move(*settings);
      if (moved) {
        close_device();
        device_.select(settings_.device);
        retry_open_at_ = {};
      }
      apply_settings();
    }
    if (check && !ensure_open() && check_failed) check_failed();
    for (VolumeChange& change : volumes) {
      if (change.cid.empty()) {
        member_volumes_.clear();
        continue;
      }
      std::erase_if(member_volumes_, [&](const auto& known) { return known.first == change.cid; });
      if (change.percent != 100) member_volumes_.emplace_back(std::move(change.cid), change.percent);
    }
    if (members) {
      members_ = std::move(*members);
      place_members();
    }
    for (std::size_t i = 0; i < count; ++i) play(batch[i]);
    for (const Cue& cue : cues) play_cue(cue);
    if (running_ && count == 0 && mixer_.idle()) {
      device_.stop();
      running_ = false;
    }
  }
  close_device();
}

bool Engine::ensure_open() {
  if (open_) return true;
  const auto now = std::chrono::steady_clock::now();
  if (now < retry_open_at_) return false;
  const auto rate = device_.open(*this);
  if (!rate) {
    retry_open_at_ = now + open_retry;
    return false;
  }
  if (*rate != rate_) {
    recordings_.clear();
    resamplers_.clear();
    rate_ = *rate;
  }
  mixer_.prepare(rate_);
  open_ = true;
  apply_settings();
  return true;
}

void Engine::close_device() {
  if (running_) device_.stop();
  running_ = false;
  if (open_) device_.close();
  open_ = false;
}

void Engine::apply_settings() {
  mixer_.set_master(gain_of(settings_.volume));
  if (rate_ == 0) return;
  const SpatialConfig config{settings_.spatial, settings_.balance};
  for (std::size_t seat = 0; seat < seat_count; ++seat) seat_mixes_[seat] = seat_mix(seat, rate_, config);
}

void Engine::place_members() {
  std::erase_if(seats_, [&](const auto& seat) {
    return std::ranges::none_of(members_, [&](const Member& m) { return m.slot == seat.first; });
  });
  for (const Member& member : members_) {
    if (std::ranges::any_of(seats_, [&](const auto& seat) { return seat.first == member.slot; })) continue;
    // Everyone sits somewhere random, and alone while a seat is free.
    std::array<std::size_t, seat_count> free_seats{};
    std::size_t free_count = 0;
    for (std::size_t seat = 0; seat < seat_count; ++seat) {
      if (std::ranges::none_of(seats_, [&](const auto& taken) { return taken.second == seat; })) free_seats[free_count++] = seat;
    }
    const std::size_t seat = free_count > 0 ? free_seats[random_.below(free_count)] : random_.below(seat_count);
    seats_.emplace_back(member.slot, seat);
  }
}

std::size_t Engine::seat_of(std::uint32_t slot) const {
  for (const auto& [member, seat] : seats_) {
    if (member == slot) return seat;
  }
  return slot % seat_count;  // the member list has not arrived yet
}

std::string_view Engine::pack_for(const Event& event, Device device) const {
  const auto choice = [&](const std::string& keyboard, const std::string& mouse, const std::string& pen) -> std::string_view {
    switch (device) {
      case Device::Keyboard: return keyboard;
      case Device::Mouse: return mouse;
      case Device::Pen: return pen;
    }
    return {};
  };
  if (!event.own && !settings_.others_use_my_sounds) {
    for (const Member& member : members_) {
      if (member.slot == event.slot) return choice(member.keyboard_pack, member.mouse_pack, member.pen_pack);
    }
    return {};
  }
  return choice(settings_.keyboard_pack, settings_.mouse_pack, settings_.pen_pack);
}

std::span<const float> Engine::recording(const std::filesystem::path& path) {
  const std::string key = path.string();
  if (const auto found = recordings_.find(key); found != recordings_.end()) return found->second;
  std::vector<float> converted;
  if (const auto pcm = parse_wav(read_bytes(path))) {
    auto resampler = resamplers_.find(pcm->rate);
    if (resampler == resamplers_.end()) {
      resampler = resamplers_.try_emplace(pcm->rate, pcm->rate, rate_).first;
    }
    converted = resampler->second.run(pcm->samples);
  }
  return recordings_.emplace(key, std::move(converted)).first->second;
}

int Engine::volume_of(std::uint32_t slot) const {
  for (const Member& member : members_) {
    if (member.slot != slot) continue;
    for (const auto& [cid, percent] : member_volumes_) {
      if (cid == member.cid) return percent;
    }
    break;
  }
  return 100;
}

void Engine::start_voice(std::span<const float> samples, const SeatMix& mix, float gain) {
  if (samples.empty() || !mixer_.trigger(samples, mix, gain)) return;
  if (!running_) running_ = device_.start();
  if (!running_) close_device();
}

void Engine::play(const Event& event) {
  if (settings_.idle_muted) return;
  if (event.own ? !settings_.listen_self : settings_.muted) return;
  if (settings_.volume <= 0) return;
  const Device device = device_of(event.kind);
  const int device_volume = device == Device::Keyboard ? settings_.keyboard_volume
                            : device == Device::Mouse  ? settings_.mouse_volume
                                                       : settings_.pen_volume;
  if (device_volume <= 0) return;
  const int member_volume = event.own ? 100 : volume_of(event.slot);
  if (member_volume <= 0) return;
  if (catalog_.empty()) catalog_ = Catalog::scan(sounds_dir_);
  const auto file = catalog_.pick(event.kind, pack_for(event, device), random_);
  if (!file || !ensure_open()) return;
  const SeatMix mix = event.own ? own_mix() : seat_mixes_[seat_of(event.slot)];
  start_voice(recording(*file), mix, gain_of(device_volume) * gain_of(member_volume));
}

void Engine::play_cue(const Cue& cue) {
  if (settings_.volume <= 0) return;
  if (cue.kind == CueKind::Notification &&
      (settings_.idle_muted || (settings_.muted && settings_.mute_notifications))) {
    return;
  }
  const auto file = cue.kind == CueKind::System ? std::optional(sounds_dir_ / "system" / (cue.name + ".wav"))
                                                : notification_file(sounds_dir_, cue.name);
  if (!file || !ensure_open()) return;
  start_voice(recording(*file), own_mix(), 1.0f);
}

}  // namespace crpp::sound
