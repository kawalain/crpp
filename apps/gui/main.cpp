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
//   ... --data <dir>              (last) keep data in <dir>, not the user's
//                                 folders; other instances are then allowed
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

std::filesystem::path exe_dir() {
  wchar_t buf[MAX_PATH];
  const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  return std::filesystem::path(std::wstring(buf, n)).parent_path();
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

// The manifest's sounds, relative to the sounds folder.
std::vector<std::string> expected_sounds(const assets::Manifest& manifest) {
  constexpr std::string_view folder = "sounds/";
  std::vector<std::string> paths;
  for (const assets::Entry& entry : manifest.entries) {
    if (entry.path.starts_with(folder)) paths.emplace_back(entry.path.substr(folder.size()));
  }
  return paths;
}

void wire_areas(Shell& s) {
  area::connection::wire(s);
  area::home::wire(s);
  area::room::wire(s);
  area::sound::wire(s);
  area::preferences::wire(s);
  area::os::wire(s);
  area::account::wire(s);
  area::update::wire(s);
  Shell* sp = &s;
  s.prefs->on_changed = [sp](std::string_view key) {
    area::sound::pref_changed(*sp, key);
    area::preferences::pref_changed(*sp, key);
    area::os::pref_changed(*sp, key);
  };
}

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path data_dir;
  if (argc >= 3 && std::string(argv[argc - 2]) == "--data") {
    data_dir = ui::widen(argv[argc - 1]);
    argc -= 2;
  }
  Shell shell;
  shell.throwaway_data = !data_dir.empty();
  shell.shares_the_machine = os_policy::shares_the_machine(update::official_build(), !data_dir.empty());
  shell.data_override = data_dir.wstring();
  unsigned long updated_from = 0;
  if (argc >= 3 && std::string(argv[argc - 2]) == "--update-url") {
    shell.update_url = argv[argc - 1];
    argc -= 2;
  }
  if (argc >= 3 && std::string(argv[argc - 2]) == "--updated-from") {
    updated_from = std::strtoul(argv[argc - 1], nullptr, 10);
    argc -= 2;
  }
  shell.asset_manifest = assets::built_in_manifest();
  shell.asset_url = assets::published_url();
  if (argc >= 3 && std::string(argv[argc - 2]) == "--asset-url") {
    shell.asset_url = argv[argc - 1];
    shell.asset_manifest.sources = {};  // everything under its own path there
    argc -= 2;
  }
  std::filesystem::path skin_dir;
  if (argc >= 3 && std::string(argv[argc - 2]) == "--skin-dir") {
    skin_dir = ui::widen(argv[argc - 1]);
    argc -= 2;
  }
  const locale::Locale* shot_locale = locale::find_locale("ko");
  if (argc >= 3 && std::string(argv[argc - 2]) == "--locale") {
    if (const locale::Locale* named = locale::find_locale(argv[argc - 1])) shot_locale = named;
    argc -= 2;
  }
  std::string shot_theme;
  if (argc >= 3 && std::string(argv[argc - 2]) == "--theme") {
    shot_theme = argv[argc - 1];
    argc -= 2;
  }
  float shot_dpi = 96;
  if (argc >= 3 && std::string(argv[argc - 2]) == "--dpi") {
    shot_dpi = static_cast<float>(std::atof(argv[argc - 1]));
    argc -= 2;
  }
  const store::Locations beside_exe{exe_dir() / "data" / "crpp", exe_dir() / "data" / "Cloudroom"};
  auto locations = !data_dir.empty()
                       ? std::expected<store::Locations, store::Error>(store::Locations{data_dir / "crpp", data_dir / "Cloudroom"})
                   : os_policy::data_beside_exe(update::official_build(), false)
                       ? std::expected<store::Locations, store::Error>(beside_exe)
                       : store::default_locations();
  if (!locations) locations = beside_exe;
  const bool assets_beside_exe = std::filesystem::exists(exe_dir() / "assets" / "fonts");
  shell.assets_dir = assets_beside_exe ? exe_dir() / "assets" : locations->protected_dir / "assets";
  const auto absent = assets_beside_exe ? std::vector<const assets::Entry*>{} : assets::missing(shell.asset_manifest, shell.assets_dir);
  // Only a first start waits for the files: a network that keeps them out would otherwise
  // hold up every start.
  const bool first_install = !assets_beside_exe && !assets::tried(shell.assets_dir);
  const auto& assets = shell.assets_dir;
  // Windows refuses to replace an open font file, so one about to change stays closed.
  const bool font_pending = std::ranges::any_of(absent, [](const assets::Entry* entry) { return entry->path.ends_with(".ttf"); });
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
  if (!shell.app.init(first_install ? std::filesystem::path{} : assets, !font_pending)) return failed_to_start(locale::tr().start_failed);
  if (!skin_dir.empty()) {
    if (const auto loaded = shell.app.load_skin_dir(skin_dir); !loaded) return failed_to_start(loaded.error());
  } else if (std::string_view(argc >= 2 ? argv[1] : "") != "--shot") {  // a shot without --skin-dir is the C++ layouts, to compare with
    (void)shell.app.find_skins(exe_dir() / "skins");
  }
  for (const std::string_view id : assets::picker_emoji()) shell.app.ui().emoji_ids.emplace_back(id);
  std::time_t now = std::time(nullptr);
  std::tm local{};
  localtime_s(&local, &now);
  shell.app.home().set_hour(local.tm_hour);

  if (argc >= 3 && std::string(argv[1]) == "--shot") {
    locale::set_current(*shot_locale);
    shell.app.ui().gfx.set_language(ui::widen(shot_locale->tag));
    if (!shot_theme.empty()) shell.app.set_theme(shot_theme);
    return run_shot(shell, argc, argv, shot_dpi);
  }
  if (updated_from != 0) area::update::took_over(updated_from);
  if (shell.shares_the_machine && another_instance_running()) return 0;

  shell.data_dir = locations->protected_dir;
  shell.store.emplace(store::Store::open(*locations, shell.platform, unix_ms()));
  store::Store& data = *shell.store;
  shell.prefs.emplace(data);
  shell.app.set_prefs(*shell.prefs);
  area::preferences::apply_language(shell);
  shell.log.set_path(locations->protected_dir / "logs" / "crpp.log");

  shell.audio = load_audio_settings(data);
  shell.app.room().set_volume(shell.audio.volume);
  shell.expected_sounds = expected_sounds(shell.asset_manifest);
  shell.engine = std::make_unique<sound::Engine>(shell.audio_device, assets / "sounds", shell.expected_sounds);
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
    if (!absent.empty()) start_install(shell, false);
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
  (void)data.save();
  area::update::shutdown(shell);
  // A blocked download may take long to give up, and files are only ever replaced whole,
  // so there is nothing to finish.
  if (shell.assets_worker.joinable()) ExitProcess(0);
  return 0;
}
