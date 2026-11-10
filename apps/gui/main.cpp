// crpp: the Cloudroom++ window.
//   crpp                          run the app
//   crpp --minimized              run it hidden in the tray, as a logon does
//   crpp --shot out.png [WxH] [hour] [scene] [notice]
//                                 render a scene offscreen and exit; the area_*.cpp
//                                 files name the scenes
//   crpp --smoke out.png          create a room, save a screenshot and exit with 0
//   crpp --shot out.png WxH hour motion-<name> --at <ms>
//                                 a moment of a motion, <ms> after it starts; shot.cpp
//                                 names them
//   ... --locale <id>             (with --shot) in this Locale; otherwise Korean
//   ... --dpi <n>                 (with --shot) render at that DPI; WxH stay in
//                                 DIPs, so the image grows with it
//   ... --skin-dir <dir>          draw from the unsigned Skin Pack in <dir>, for developing one
//   ... --asset-url <url>         fetch assets from <url>, not the published
//                                 ones; the manifest still decides what is kept
//   ... --update-url <url>        (not in official builds) look for updates at <url>,
//                                 which must be signed with the key built in
//   ... --updated-from <pid>      started by the update of process <pid>
//   ... --data <dir>              keep data in <dir>, not the user's
//                                 folders; other instances are then allowed
// An option with a value may stand anywhere; a word with dashes that is no option ends the
// start with 3 and no window (options.hpp).
// A development build always allows other instances, and without --data keeps its data in
// a data folder beside the exe.
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <expected>
#include <fstream>

#include "areas.hpp"
#include "crpp/assets/built_in.hpp"
#include "crpp/assets/windows_platform.hpp"
#include "crpp/update/built_in.hpp"
#include "crpp/update/windows_platform.hpp"
#include "options.hpp"
#include "os_policy.hpp"
#include "shell.hpp"
#include "sound_prefs.hpp"

using namespace crpp;
using namespace crpp::gui;

namespace {

// The exit code when a start failed where nobody could be told.
constexpr int scripted_start_failure = 3;

bool on_a_desktop_someone_sees() {
  wchar_t name[64]{};
  return GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME, name, sizeof name, nullptr) && std::wstring_view(name) == L"Default";
}

std::filesystem::path exe_dir() { return update::running_exe().parent_path(); }

bool is_file(const std::filesystem::path& path) {
  std::error_code ec;
  return std::filesystem::is_regular_file(path, ec);
}

sound::Settings load_audio_settings(const store::Store& data) {
  sound::Settings audio;
  const auto percent = [&](const char* key, int fallback) { return static_cast<int>(data.i64_or(key, fallback)); };
  audio.volume = percent("volume", audio.volume);
  audio.keyboard_volume = percent("keyboardVolume", audio.keyboard_volume);
  audio.mouse_volume = percent("mouseVolume", audio.mouse_volume);
  audio.pen_volume = percent("tabletVolume", audio.pen_volume);
  audio.keyboard_pack = data.string_or("keyboardSound", "plastic");
  audio.mouse_pack = data.string_or("mouseSound", "plastic");
  audio.pen_pack = data.string_or("tabletSound", "paper");
  audio.listen_self = data.bool_or("listenSelf", audio.listen_self);
  audio.others_use_my_sounds = data.bool_or("othersUseMySounds", audio.others_use_my_sounds);
  audio.spatial = data.bool_or("spatialAudio", audio.spatial);
  audio.balance = data.bool_or("channelBalanceEnabled", true) ? percent("channelBalance", audio.balance) : 0;
  audio.reverb = reverb_named(data.string_or("reverb", "office"));
  return audio;
}

// Only one window per user session; a second start brings the first forward, also from
// the tray. That window does it itself, and this process lends it the right to come to
// the foreground.
bool another_instance_running() {
  CreateMutexW(nullptr, FALSE, L"Local\\crpp.single-instance");  // released at exit
  if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
  if (HWND existing = FindWindowW(os_policy::window_class(true), nullptr)) {
    DWORD owner = 0;
    GetWindowThreadProcessId(existing, &owner);
    AllowSetForegroundWindow(owner);
    PostMessageW(existing, RegisterWindowMessageW(area::os::show_window_message), 0, 0);
  }
  return true;
}

void wire_areas(Shell& s) {
  area::connection::wire(s);
  area::home::wire(s);
  area::room::wire(s);
  area::sound::wire(s);
  area::preferences::wire(s);
  area::repositories::wire(s);
  area::os::wire(s);
  area::account::wire(s);
  area::update::wire(s);
  area::achievements::wire(s);
  Shell* sp = &s;
  s.prefs->on_changed = [sp](std::string_view key) {
    area::sound::pref_changed(*sp, key);
    area::preferences::pref_changed(*sp, key);
    area::os::pref_changed(*sp, key);
    area::achievements::pref_changed(*sp, key);
  };
}

}  // namespace

int main(int argc, char** argv) {
  // Options are read wherever they stand; what is left says what to do, in its order.
  const auto options = read_options(std::span<const char* const>(argv, static_cast<std::size_t>(argc)));
  if (!options) {
    // Never the real app in place of what was asked: a scripted run that misspelt an option stops here,
    // with no window, and says why where it can be found.
    std::error_code ec;
    for (int i = 1; i + 1 < argc; ++i) {
      const std::string_view word = argv[i];
      if (word == "--shot") std::ofstream(ui::widen(std::string(argv[i + 1]) + ".error"), std::ios::binary) << options.error();
      if (word == "--data" && std::filesystem::is_directory(ui::widen(argv[i + 1]), ec)) {
        std::ofstream(std::filesystem::path(ui::widen(argv[i + 1])) / "start-failure.txt", std::ios::binary) << options.error();
      }
    }
    return scripted_start_failure;
  }
  // From here on argc and argv are what says what to do, without the options.
  std::vector<std::string> words = options->words;
  std::vector<char*> kept;
  for (std::string& word : words) kept.push_back(word.data());
  argc = static_cast<int>(kept.size());
  argv = kept.data();

  const std::filesystem::path data_dir = ui::widen(options->value("data"));
  Shell shell;
  shell.throwaway_data = !data_dir.empty();
  shell.shares_the_machine = os_policy::shares_the_machine(update::official_build(), !data_dir.empty());
  shell.data_override = data_dir.wstring();
  shell.update_url = std::string(options->value("update-url"));
  const unsigned long updated_from = std::strtoul(std::string(options->value("updated-from")).c_str(), nullptr, 10);
  shell.repository = assets::official_repository();
  if (std::string url(options->value("asset-url")); !url.empty()) {
    // A Repository signed with this build's key, as a test key makes possible.
    if (!url.ends_with('/')) url += '/';
    if (const auto key = update::built_in_key()) shell.repository = assets::Repository{std::move(url), "", *key, std::string(assets::official_catalog_name())};
  }
  const std::filesystem::path skin_dir = ui::widen(options->value("skin-dir"));
  const locale::Locale* shot_locale = locale::find_locale("ko");
  if (const locale::Locale* named = locale::find_locale(options->value("locale"))) shot_locale = named;
  const std::string shot_theme(options->value("theme"));
  // Whole numbers only: from_chars of a float would bring 100 KB of tables into the exe.
  const float shot_dpi = static_cast<float>(number_or(options->value("dpi"), 96u));
  const store::Locations beside_exe{exe_dir() / "data" / "crpp", exe_dir() / "data" / "Cloudroom"};
  auto locations = !data_dir.empty()
                       ? std::expected<store::Locations, store::Error>(store::Locations{data_dir / "crpp", data_dir / "Cloudroom"})
                   : os_policy::data_beside_exe(update::official_build(), false)
                       ? std::expected<store::Locations, store::Error>(beside_exe)
                       : store::default_locations();
  if (!locations) locations = beside_exe;
  // A development build runs from the folders its build put beside it, and installs nothing. An official
  // one never does: those files are signed by nobody, and anyone could leave them beside a downloaded exe.
  const bool assets_beside_exe = !update::official_build() && is_file(exe_dir() / "assets" / assets::catalog_name);
  shell.assets_dir = assets_beside_exe ? exe_dir() / "assets" : locations->protected_dir / "assets";
  shell.skins_dir = assets_beside_exe ? exe_dir() / "skins" : shell.assets_dir / "skins";
  shell.repositories_dir = locations->protected_dir / "repositories";
  shell.files_dir = locations->protected_dir / "files";
  shell.assets_beside_exe = assets_beside_exe;
  // Every Repository's folder, whose lists say what the store holds.
  std::vector<std::filesystem::path> repository_dirs{shell.assets_dir};
  if (!assets_beside_exe) {
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(shell.repositories_dir, ec)) {
      if (entry.is_directory(ec)) repository_dirs.push_back(entry.path());
    }
    // Before the font is opened from it: what an older exe installed at its path goes into the store.
    assets::WindowsPlatform platform;
    for (const std::filesystem::path& dir : repository_dirs) assets::move_into_store(dir, shell.files_dir, platform);
  }
  if (assets_beside_exe) {
    shell.catalog = assets::load_local_catalog(shell.assets_dir);
  } else if (shell.repository) {
    assets::WindowsPlatform platform;
    shell.catalog = assets::load_catalog(shell.assets_dir, shell.repository->key, platform);
  }
  shell.offered = shell.catalog;  // until the latest is read
  // Only a start without its Skin Pack waits for files: a network that keeps the others out
  // would otherwise hold up every start.
  const bool first_install = !assets_beside_exe && shell.repository && !skin_installed(shell);
  const auto failed_to_start = [&](const std::string& reason) {
    if (on_a_desktop_someone_sees()) {
      // There is no console to report to, and the app has no way to draw yet.
      MessageBoxW(nullptr, ui::widen(reason).c_str(), L"Cloudroom++", MB_OK | MB_ICONERROR);
      return 1;
    }
    // Nobody could press a button there, so a dialog would hold a scripted run until it is killed.
    std::filesystem::path report;
    if (argc >= 3 && std::string(argv[1]) == "--shot") report = ui::widen(std::string(argv[2]) + ".error");
    else if (!data_dir.empty()) report = data_dir / "start-failure.txt";
    if (!report.empty()) std::ofstream(report, std::ios::binary) << reason;
    return scripted_start_failure;
  };
  // A font that changes comes as another file, so the one in use is never replaced under it.
  if (!shell.app.init(first_install ? files::Tree{} : official_files(shell))) return failed_to_start(locale::tr().start_failed);
  if (!skin_dir.empty()) {
    if (const auto loaded = shell.app.load_skin_dir(skin_dir); !loaded) return failed_to_start(loaded.error());
  } else if (std::string_view(argc >= 2 ? argv[1] : "") == "--shot") {
    // The packs beside the exe, as a run has them: the one with the Theme asked for, or the default.
    // A first run has none, so its scenes load none.
    const bool before_any_pack = argc >= 6 && std::string_view(argv[5]).starts_with("first-run");
    const ui::App::SkinFolder beside[] = {{before_any_pack ? files::Tree{} : official_files(shell).under("skins/"), "", shell.skins_dir}};
    if (!shell.app.find_skins(beside) && !before_any_pack) return failed_to_start("no Skin Pack in " + ui::narrow(shell.skins_dir.wstring()));
    const auto& skins = shell.app.ui().skins;
    if (!before_any_pack) {
      std::string dir = ui::choice_of(*shell.app.ui().prefs, skins).skin->dir;
      for (const ui::SkinInfo& info : skins) {
        if (!shot_theme.empty() && std::ranges::any_of(info.themes, [&](const skin::PackTheme& t) { return t.palette == shot_theme || t.dark == shot_theme; })) dir = info.dir;
      }
      if (const auto loaded = shell.app.load_found_skin(dir); !loaded) return failed_to_start(loaded.error());
    }
  } else {
    area::repositories::load(shell);
    shell.app.set_parent_finder([&shell](std::string_view id, std::string_view fingerprint, const std::filesystem::path& from) {
      return area::repositories::parent_dir(shell, id, fingerprint, from);
    });
    (void)shell.app.find_skins(area::repositories::skin_folders(shell));
  }
  if (shell.catalog) shell.app.ui().emoji_ids = assets::picker_emoji(*shell.catalog);
  area::home::read_clock(shell);

  if (argc >= 3 && std::string(argv[1]) == "--shot") {
    locale::set_current(*shot_locale);
    shell.app.ui().gfx.set_language(ui::widen(shot_locale->tag));
    if (!shot_theme.empty()) shell.app.set_theme(shot_theme);
    return run_shot(shell, argc, argv, shot_dpi);
  }
  if (updated_from != 0) area::update::took_over(updated_from);
  if (shell.shares_the_machine && another_instance_running()) return 0;
  // What nothing lists leaves the store, once no other start can be installing into it. Of the
  // store only the font is open yet, and it is listed.
  if (!assets_beside_exe) assets::collect_garbage(shell.files_dir, repository_dirs);

  shell.data_dir = locations->protected_dir;
  shell.store.emplace(store::Store::open(*locations, shell.platform, unix_ms()));
  store::Store& data = *shell.store;
  shell.prefs.emplace(data);
  shell.app.set_prefs(*shell.prefs);
  area::preferences::apply_language(shell);
  shell.log.set_path(locations->protected_dir / "logs" / "crpp.log");

  shell.audio = load_audio_settings(data);
  shell.app.room().set_volume(shell.audio.volume);
  if (shell.catalog) shell.expected_sounds = assets::sound_files(*shell.catalog);
  shell.engine = std::make_unique<sound::Engine>(shell.audio_device, official_sounds(shell));
  shell.engine->set_settings(shell.audio);
  shell.engine->start();
  shell.input = std::make_unique<input::RawInputSource>(shell.input_sink);
  shell.input->set_privacy(data.bool_or("inputDetail", false) ? input::Privacy::Detail : input::Privacy::Presence);
  shell.bridge = std::make_unique<Bridge>(shell.app, shell.tasks, data, *shell.engine);

  HWND hwnd = create_window(shell);
  if (!hwnd) return 1;
  shell.hwnd = hwnd;
  shell.tasks.attach(hwnd);
  shell.input_sink.attach(hwnd);
  wire_areas(shell);
  area::os::first_show(shell, argc >= 2 && std::string(argv[1]) == "--minimized");
  if (argc >= 3 && std::string(argv[1]) == "--smoke") shell.smoke_path = ui::widen(argv[2]);
  if (first_install) {
    start_install(shell, true);
  } else {
    enter_app(shell);
    // Behind the app: the latest catalog, and what is missing. A development build has its own.
    if (!assets_beside_exe && shell.repository) start_install(shell, false);
  }

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  shell.input->stop();
  if (shell.client) shell.client->stop();
  shell.tasks.run_all();  // what the client reported last must still reach the store
  shell.engine->stop();
  // Quitting in a Room is leaving it: the minutes since the last key or click count too.
  if (shell.app.base_view() == ui::View::Session) shell.app.achievements().in_room(ui::clock_ms(), ui::local_day());
  area::achievements::flush(shell);
  (void)data.save();
  area::update::shutdown(shell);
  // A blocked download may take long to give up, and files are only ever replaced whole,
  // so there is nothing to finish.
  if (shell.assets_worker.joinable() || shell.repository_worker.joinable()) ExitProcess(0);
  return 0;
}
