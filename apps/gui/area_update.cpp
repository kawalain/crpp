// New versions and what changed in them. Network and disk work runs on one short-lived
// thread at a time; between checks there is one wake-up and nothing else.
#include <algorithm>
#include <memory>
#include <optional>

#include "areas.hpp"
#include "crpp/update/built_in.hpp"
#include "crpp/update/windows_platform.hpp"
#include "shell.hpp"

using namespace crpp::ui;

namespace crpp::gui::area::update {

namespace up = crpp::update;

namespace {

constexpr unsigned long long first_check_delay_ms = 5ull * 1000;  // out of startup's way
constexpr unsigned long long check_interval_ms = 6ull * 60 * 60 * 1000;

// Everything here is touched on the UI thread only.
struct State {
  std::thread worker;
  std::optional<up::Available> available;
  bool update_requested = false;  // pressed while a check was still running
  bool open_current_after_check = false;  // first start of this version, notes not cached yet
};

State& state() {
  static State instance;
  return instance;
}

// The running version; only a build that can verify anything ever goes online.
struct Target {
  up::Source source;
  up::Version current;
  std::optional<up::PublicKey> key;
  bool enabled = false;
};

Target target_for(const Shell& s) {
  Target t;
  t.current = up::parse_version(up::built_version()).value_or(up::Version{});
  t.key = up::built_in_key();
  if (up::official_build()) {
    t.source = up::published_source();
    t.enabled = t.key.has_value();
  } else if (!s.update_url.empty()) {
    t.source = {s.update_url + "releases", s.update_url + "download/"};
    t.enabled = t.key.has_value();
  }
  return t;
}

std::filesystem::path cache_path(const Shell& s) { return s.data_dir / "releases.json"; }

std::wstring relaunch_args(const Shell& s) {
  std::wstring args;
  if (!s.update_url.empty()) args += L"--update-url " + ui::widen(s.update_url) + L" ";
  if (!s.data_override.empty()) args += L"--data \"" + s.data_override + L"\"";
  return args;
}

std::vector<ChangelogEntry> to_entries(const std::vector<up::Release>& releases) {
  std::vector<ChangelogEntry> entries;
  entries.reserve(releases.size());
  for (const up::Release& release : releases) {
    // A release named after its tag has no tagline of its own.
    const bool named = !release.name.empty() && release.name != release.tag();
    entries.push_back({named ? release.name : std::string{}, locale::format_release_date(release.published_at),
                       up::to_string(release.version), up::parse_notes(release.body)});
  }
  return entries;
}

std::vector<up::Release> read_cache(const Shell& s) {
  up::WindowsPlatform platform;
  const auto bytes = platform.read_file(cache_path(s), 8 * 1024 * 1024);
  if (!bytes) return {};
  auto releases = up::parse_releases({reinterpret_cast<const char*>(bytes->data()), bytes->size()});
  return releases ? std::move(*releases) : std::vector<up::Release>{};
}

// Off the UI thread. A flushed temp file and a rename, and no write if nothing changed.
void write_cache(const std::filesystem::path& path, const std::vector<up::Release>& releases, up::Platform& platform) {
  const std::string text = up::dump_releases(releases);
  const std::span bytes(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
  if (const auto old = platform.read_file(path, 8 * 1024 * 1024); old && std::ranges::equal(*old, bytes)) return;
  std::error_code ignored;
  std::filesystem::create_directories(path.parent_path(), ignored);
  auto temp = path;
  temp += ".tmp";
  if (platform.write_file(temp, bytes)) (void)platform.move_file(temp, path);
  else (void)platform.remove_file(temp);
}

void start_update(Shell& s);

void open_changelog_at(Shell& s, std::vector<up::Release> releases, std::string_view version) {
  std::size_t index = 0;
  for (std::size_t i = 0; i < releases.size(); ++i) {
    if (up::to_string(releases[i].version) == version) index = i;
  }
  s.app.changelog().open(to_entries(releases), index);
}

constexpr const char* notes_shown_key = "crppNotesShownVersion";

void show_notes_of_available(Shell& s, const std::vector<up::Release>& releases, const up::Version& version) {
  const std::string name = up::to_string(version);
  const View view = s.app.view();
  // Nothing can be read over the first-run prompt or the setup screen; the next check tries again.
  if (view != View::Home && view != View::Session) return;
  if (!up::notes_due_when_noticed(s.store->string_or(notes_shown_key, ""), name)) return;
  s.store->set(notes_shown_key, store::Json(name));
  if (!s.app.changelog().is_open()) open_changelog_at(s, releases, name);
}

// Runs one thing on the worker, then `done` on the UI thread once it has been joined.
template <typename Work>
void run_worker(Shell& s, Work work) {
  State& st = state();
  st.worker = std::thread([&s, work = std::move(work)]() mutable {
    auto done = work();
    s.tasks.post([&s, done = std::move(done)]() mutable {
      state().worker.join();
      done();
    });
  });
}

void start_check(Shell& s);

void check_finished(Shell& s, std::expected<up::CheckResult, up::Error> result) {
  State& st = state();
  if (result) {
    if (s.app.changelog().is_open()) s.app.changelog().set_entries(to_entries(result->releases));
    if (st.open_current_after_check) {
      st.open_current_after_check = false;
      const std::string_view current = up::built_version();
      if (std::ranges::any_of(result->releases, [&](const up::Release& r) { return up::to_string(r.version) == current; })) {
        open_changelog_at(s, result->releases, current);
      }
    }
    // A running download or a failure on screen is not interrupted by a later check.
    if (result->update && s.app.update_popup().state() != UpdatePopup::State::Busy) {
      const bool same = st.available && st.available->release.version == result->update->release.version &&
                        st.available->manifest.sha256 == result->update->manifest.sha256;
      st.available = std::move(result->update);
      if (!same || s.app.update_popup().state() == UpdatePopup::State::Hidden) s.app.update_popup().show_available();
      show_notes_of_available(s, result->releases, st.available->release.version);
    }
  }
  s.app.update_popup().schedule_check(clock_ms() + check_interval_ms);
  sync_frames(s);
  if (st.update_requested) {
    st.update_requested = false;
    start_update(s);
  }
}

void start_check(Shell& s) {
  State& st = state();
  Target target = target_for(s);
  if (!target.enabled) return;
  if (st.worker.joinable()) {  // a download is running; the check can wait its turn
    s.app.update_popup().schedule_check(clock_ms() + check_interval_ms);
    return;
  }
  run_worker(s, [&s, target = std::move(target), cache = cache_path(s)] {
    up::WindowsPlatform platform;
    auto result = up::check(target.source, target.current, target.key, platform);
    if (result) write_cache(cache, result->releases, platform);
    return [&s, result = std::move(result)]() mutable { check_finished(s, std::move(result)); };
  });
}

void update_failed(Shell& s, up::Error error) {
  s.app.update_popup().show_failed();
  s.app.toasts().toast(locale::fill(tr().update_failed, {up::error_name(error)}));
}

void start_update(Shell& s) {
  State& st = state();
  if (!st.available) return;
  s.app.update_popup().show_progress(0);
  if (st.worker.joinable()) {
    st.update_requested = true;  // a check is running; it picks this up when it ends
    return;
  }
  Target target = target_for(s);
  run_worker(s, [&s, target = std::move(target), args = relaunch_args(s)] {
    using Done = std::function<void()>;
    up::WindowsPlatform platform(args);
    // The release may have changed since the popup appeared: always go by the latest.
    const auto latest = up::check(target.source, target.current, target.key, platform);
    if (latest && !latest->update && !latest->problem) {
      return Done([&s] {
        state().available.reset();
        s.app.update_popup().hide();
        s.app.toasts().toast(tr().update_latest);
      });
    }
    if (!latest || !latest->update) {
      const up::Error error = latest ? *latest->problem : latest.error();
      return Done([&s, error] { update_failed(s, error); });
    }
    const up::Available available = *latest->update;
    std::shared_ptr<int> shown = std::make_shared<int>(0);
    const auto bytes = up::download(target.source, available, platform, [&s, shown, total = available.manifest.size](std::uint64_t received) {
      const int percent = static_cast<int>(received * 100 / total);
      if (percent == *shown) return;
      *shown = percent;
      s.tasks.post([&s, percent] { s.app.update_popup().show_progress(percent); });
    });
    if (!bytes) {
      const up::Error error = bytes.error();
      return Done([&s, error] { update_failed(s, error); });
    }
    s.tasks.post([&s] { s.app.update_popup().show_progress(100); });
    const auto replaced = up::self_replace(up::running_exe(), *bytes, available.manifest.sha256, platform);
    if (!replaced) {
      const up::Error error = replaced.error();
      return Done([&s, error] { update_failed(s, error); });
    }
    // The new process is up and waiting for this one to go. A Room stays held: the
    // saved resume brings it back, as after any restart.
    return Done([&s] { DestroyWindow(s.hwnd); });
  });
}

void on_version(Shell& s) {
  std::vector<up::Release> releases = read_cache(s);
  s.app.changelog().open(to_entries(releases), 0);
  // Never seen, so nothing to show offline: try now. The view fills in if it works.
  if (releases.empty()) start_check(s);
}

// Upstream opens the changelog on the first start of a version. A first run ever has
// nothing to compare with, and so no version it was updated from.
void show_notes_of_a_new_version(Shell& s) {
  constexpr const char* key = "crppLastSeenVersion";
  const std::string current(up::built_version());
  const std::string seen = s.store->string_or(key, "");
  if (seen == current) return;
  s.store->set(key, store::Json(current));
  if (!up::notes_due_at_start(seen, s.store->string_or(notes_shown_key, ""), current)) return;
  auto releases = read_cache(s);
  if (std::ranges::any_of(releases, [&](const up::Release& r) { return up::to_string(r.version) == current; })) {
    open_changelog_at(s, std::move(releases), current);
  } else {
    state().open_current_after_check = true;
  }
}

std::vector<ChangelogEntry> sample_entries() {
  std::vector<ChangelogEntry> entries;
  entries.push_back({"", "2026년 11월 04일", "0.1.10", up::parse_notes("## 변경 사항\n- 음소거 시 알림 소리도 끄기 기능이 추가되었습니다.\n")});
  entries.push_back({"새로운 구름", "2026년 11월 03일", "0.1.9",
                     up::parse_notes("<!-- generated -->\n## 변경 사항\n"
                                     "- 음소거 버튼이 추가되었습니다.\n"
                                     "- 자리비움 상태일 때 음소거 전환 기능이 추가되었습니다.\n"
                                     "- **채팅 메시지 영역**이 창 크기에 따라 조절되며, 채팅 메시지 크기 조절 옵션이 추가되었습니다.\n"
                                     "  - [링크](https://example.com)는 글자만 보여 줍니다.\n\n"
                                     "## 버그 수정\n"
                                     "- 클라이언트를 재시작하고 구름에 복귀할 때 일부 메시지가 손실되는 문제를 해결했습니다.\n\n"
                                     "그 밖의 문단도 그대로 보여 줍니다.\n")});
  return entries;
}

}  // namespace

void wire(Shell& s) {
  up::WindowsPlatform platform;
  up::remove_leftovers(up::running_exe(), platform);

  s.app.settings().set_version("v" + std::string(up::built_version()) + " (" + std::string(up::built_hash()) + ")");
  s.app.settings().on_version = [&s] { on_version(s); };
  s.app.update_popup().on_update = [&s] { start_update(s); };
  s.app.update_popup().on_check_due = [&s] { start_check(s); };

  const Target target = target_for(s);
  if (!target.enabled) return;  // a development build, or no real key yet: no network
  show_notes_of_a_new_version(s);
  s.app.update_popup().schedule_check(clock_ms() + first_check_delay_ms);
  sync_frames(s);
}

void took_over(unsigned long old_pid) {
  up::report_started(old_pid);
  up::wait_for_exit(old_pid);
}

void shutdown(Shell&) {
  if (state().worker.joinable()) ExitProcess(0);
}

bool scene(Shell& s, std::string_view name) {
  const bool dark = name.ends_with("-dark");
  if (dark) {
    s.app.set_theme("darkcloud");
    name.remove_suffix(5);
  }
  if (name.starts_with("changelog=")) {  // a release's notes, to look at before publishing
    up::WindowsPlatform platform;
    const auto bytes = platform.read_file(std::filesystem::path(ui::widen(name.substr(10))), 8 * 1024 * 1024);
    if (!bytes) return false;
    std::string_view notes(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    std::string title;
    if (notes.starts_with("# ")) {
      const auto end = notes.find('\n');
      title = notes.substr(2, end - 2);
      notes.remove_prefix(end == std::string_view::npos ? notes.size() : end + 1);
    }
    s.app.changelog().open({{std::move(title), "2026년 11월 07일", "0.1.0", up::parse_notes(notes)}}, 0);
  } else if (name == "update-popup") {
    s.app.update_popup().show_available();
  } else if (name == "update-progress") {
    s.app.update_popup().show_progress(42);
  } else if (name == "update-failed") {
    s.app.update_popup().show_failed();
    s.app.toasts().toast(locale::fill(tr().update_failed, {"network"}));
  } else if (name == "changelog") {
    s.app.changelog().open(sample_entries(), 1);
  } else {
    if (dark) s.app.set_theme("cumulus");
    return false;
  }
  return true;
}

}  // namespace crpp::gui::area::update
