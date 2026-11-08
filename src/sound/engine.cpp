#include "crpp/sound/engine.hpp"

#include "crpp/sound/synth.hpp"

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

Engine::Engine(AudioDevice& device, std::filesystem::path sounds_dir, std::span<const std::string> expected)
    : device_(device), sounds_dir_(std::move(sounds_dir)), expected_(expected.begin(), expected.end()), random_(std::random_device{}()) {
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

void Engine::set_expected(std::vector<std::string> expected) {
  {
    std::lock_guard lock(mutex_);
    new_expected_ = std::move(expected);
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
        return stopping_ || device_changed_ || pending_count_ > 0 || new_settings_ || new_members_ || new_expected_ ||
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
      if (new_expected_) {
        expected_ = std::move(*std::exchange(new_expected_, std::nullopt));
        catalog_ = {};  // read again at the next sound
      }
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
  mixer_.set_reverb(settings_.reverb);
  if (rate_ == 0) return;
  const SpatialConfig config{settings_.spatial, settings_.balance, settings_.reverb};
  for (std::size_t desk = 0; desk < desk_count; ++desk) desk_mixes_[desk] = desk_mix(desk, rate_, config);
}

void Engine::place_members() {
  std::erase_if(desks_, [&](const auto& desk) {
    return std::ranges::none_of(members_, [&](const Member& m) { return m.slot == desk.first; });
  });
  for (const Member& member : members_) {
    if (std::ranges::any_of(desks_, [&](const auto& desk) { return desk.first == member.slot; })) continue;
    // Everyone sits alone while a Desk is free, spread out left to right.
    std::array<bool, desk_count> taken{};
    for (const auto& [slot, desk] : desks_) taken[desk] = true;
    std::array<std::size_t, desk_count> choices{};
    const std::size_t count = farthest_desks(taken, choices);
    desks_.emplace_back(member.slot, choices[random_.below(count)]);
  }
}

std::size_t Engine::desk_of(std::uint32_t slot) const {
  for (const auto& [member, desk] : desks_) {
    if (member == slot) return desk;
  }
  return slot % desk_count;  // the member list has not arrived yet
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

// Kept with the recordings: voices point into them, and they cost nothing until needed.
std::span<const float> Engine::generated(wire::Kind kind, std::string_view pack) {
  const auto variant = static_cast<std::uint32_t>(random_.below(synth_variants));
  const std::string key = "generated:" + std::string(pack) + ":" + std::to_string(static_cast<int>(kind)) + ":" + std::to_string(variant);
  if (const auto found = recordings_.find(key); found != recordings_.end()) return found->second;
  return recordings_.emplace(key, synthesize(kind, pack, rate_, variant)).first->second;
}

std::span<const float> Engine::generated_cue(const std::string& name) {
  const std::string key = "generated:" + name;
  if (const auto found = recordings_.find(key); found != recordings_.end()) return found->second;
  return recordings_.emplace(key, synthesize_cue(name, rate_)).first->second;
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

void Engine::start_voice(std::span<const float> samples, const DeskMix& mix, float gain) {
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
  if (catalog_.empty()) catalog_ = Catalog::scan(sounds_dir_, expected_);
  const std::string_view pack = pack_for(event, device);
  const auto file = catalog_.pick(event.kind, pack, random_);
  if (!file || !ensure_open()) return;
  const DeskMix mix = event.own ? own_mix() : desk_mixes_[desk_of(event.slot)];
  std::span<const float> samples = recording(*file);
  if (samples.empty()) samples = generated(event.kind, pack);
  start_voice(samples, mix, gain_of(device_volume) * gain_of(member_volume));
}

std::span<const float> Engine::generated_chime(const std::string& name) {
  const std::string key = "chime:" + std::to_string(static_cast<int>(settings_.chimes)) + ":" + name;
  if (const auto found = recordings_.find(key); found != recordings_.end()) return found->second;
  return recordings_.emplace(key, synthesize_chime(name, rate_, settings_.chimes)).first->second;
}

void Engine::play_cue(const Cue& cue) {
  if (settings_.volume <= 0) return;
  if (cue.kind == CueKind::Notification &&
      (settings_.idle_muted || (settings_.muted && settings_.mute_notifications))) {
    return;
  }
  if (settings_.chimes != ChimeStyle::None && cue.kind != CueKind::System && can_chime(cue.name)) {
    if (ensure_open()) start_voice(generated_chime(cue.name), own_mix(), 1.0f);
    return;
  }
  const auto file = cue.kind == CueKind::System ? std::optional(sounds_dir_ / "system" / (cue.name + ".wav"))
                                                : notification_file(sounds_dir_, cue.name);
  if ((!file && !can_synthesize_cue(cue.name)) || !ensure_open()) return;
  std::span<const float> samples;
  if (file) samples = recording(*file);
  if (samples.empty()) samples = generated_cue(cue.name);
  start_voice(samples, own_mix(), 1.0f);
}

}  // namespace crpp::sound
