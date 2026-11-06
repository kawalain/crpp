// crpp: the native Cloudroom window.
//   crpp                          run the app
//   crpp --minimized              run it hidden in the tray, as a logon does
//   crpp --shot out.png [WxH] [hour] [scene] [notice]
//                                 render a scene offscreen and exit; the area_*.cpp
//                                 files name the scenes
//   crpp --smoke out.png          create a room, save a screenshot and exit
//   ... --dpi <n>                 (with --shot) render at that DPI; WxH stay in
//                                 DIPs, so the image grows with it
//   ... --asset-url <url>         fetch assets from <url>, not the published
//                                 ones; the manifest still decides what is kept
//   ... --data <dir>              (last) keep data in <dir>, not the user's
//                                 folders; other instances are then allowed
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <expected>

#include "areas.hpp"
#include "crpp/assets/built_in.hpp"
#include "crpp/assets/windows_platform.hpp"
#include "shell.hpp"

using namespace crpp;
using namespace crpp::gui;

namespace {

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
  return audio;
}

// Only one window per user session; a second start brings the first forward.
bool another_instance_running() {
  CreateMutexW(nullptr, FALSE, L"Local\\crpp.single-instance");  // released at exit
  if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
  if (HWND existing = FindWindowW(L"CrppWindow", nullptr)) {
    if (IsIconic(existing)) ShowWindow(existing, SW_RESTORE);
    SetForegroundWindow(existing);
  }
  return true;
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
  shell.isolated = !data_dir.empty();
  const assets::Manifest manifest = assets::built_in_manifest();
  shell.asset_url = assets::published_url(manifest);
  if (argc >= 3 && std::string(argv[argc - 2]) == "--asset-url") {
    shell.asset_url = argv[argc - 1];
    argc -= 2;
  }
  float shot_dpi = 96;
  if (argc >= 3 && std::string(argv[argc - 2]) == "--dpi") {
    shot_dpi = static_cast<float>(std::atof(argv[argc - 1]));
    argc -= 2;
  }
  auto locations = data_dir.empty() ? store::default_locations()
                                    : std::expected<store::Locations, store::Error>(
                                          store::Locations{data_dir / "crpp", data_dir / "Cloudroom"});
  if (!locations) locations = store::Locations{exe_dir() / "data" / "crpp", exe_dir() / "data" / "Cloudroom"};
  const bool development = std::filesystem::exists(exe_dir() / "assets" / "fonts");
  shell.assets_dir = development ? exe_dir() / "assets" : locations->protected_dir / "assets";
  const bool needs_install = !development && !assets::missing(manifest, shell.assets_dir).empty();
  const auto& assets = shell.assets_dir;
  // Nothing is opened from a folder about to change: Windows would refuse the replacement.
  if (!shell.app.init(needs_install ? std::filesystem::path{} : assets)) {
    std::fprintf(stderr, "failed to initialise the UI\n");
    return 1;
  }
  std::time_t now = std::time(nullptr);
  std::tm local{};
  localtime_s(&local, &now);
  shell.app.home().set_hour(local.tm_hour);

  if (argc >= 3 && std::string(argv[1]) == "--shot") return run_shot(shell, argc, argv, shot_dpi);
  if (data_dir.empty() && another_instance_running()) return 0;

  shell.store.emplace(store::Store::open(*locations, shell.platform, unix_ms()));
  store::Store& data = *shell.store;
  shell.prefs.emplace(data);
  shell.app.set_prefs(*shell.prefs);

  shell.audio = load_audio_settings(data);
  shell.app.room().set_volume(shell.audio.volume);
  shell.engine = std::make_unique<sound::Engine>(shell.audio_device, assets / "sounds");
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
  Shell* sp = &shell;
  shell.app.setup().on_retry = [sp] { start_install(*sp); };
  if (needs_install) start_install(shell);
  else enter_app(shell);

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  if (shell.assets_worker.joinable()) {
    // A blocked download may take long to give up, and files are only ever
    // replaced whole, so there is nothing to finish.
    (void)data.save();
    ExitProcess(0);
  }
  shell.input->stop();
  if (shell.client) shell.client->stop();
  shell.tasks.run_all();  // what the client reported last must still reach the store
  shell.engine->stop();
  (void)data.save();
  return 0;
}
