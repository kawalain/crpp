// What happens between the window opening and the Home screen.
#include <cstdio>
#include <random>
#include <vector>

#include "areas.hpp"
#include "crpp/assets/windows_platform.hpp"
#include "shell.hpp"

using namespace crpp::ui;

namespace crpp::gui {

namespace {

// --- moving away from the Shared store ---

void show_cleanup_offer(Shell& s) {
  s.app.notice().set(Notice{tr().notice_moved, tr().notice_erase, tr().notice_keep});
  s.app.notice().on_action = [&s] {
    s.app.notice().set(Notice{s.store->erase_shared_secrets() ? tr().notice_erased
                                                              : tr().notice_erase_failed,
                              "", tr().notice_close});
  };
}

void show_store_notice(Shell& s) {
  if (s.store->mode() == store::Mode::Shared) {
    s.app.notice().set(Notice{tr().notice_shared, tr().notice_move, tr().notice_close});
    s.app.notice().on_action = [&s] {
      if (s.store->convert_to_protected()) {
        show_cleanup_offer(s);
      } else {
        s.app.notice().set(Notice{tr().notice_move_failed, "", tr().notice_close});
      }
    };
  } else if (s.store->secrets_lost()) {
    s.app.notice().set(Notice{tr().notice_unreadable, "", tr().notice_close});
  } else if (s.store->data_corrupt()) {
    s.app.notice().set(Notice{tr().notice_corrupt, "", tr().notice_close});
  }
}

// What the last run left: a Room it was still in, or a recently left Seat.
void restore_seat(Shell& s) {
  const auto resume = s.store->resume();
  if (!resume) return;
  if (!resume->left_at) {
    s.bridge->expect_resume(resume->code);
    s.client->resume(resume->code, resume->key,
                     resume->ticket.empty() ? std::nullopt : std::optional<std::string_view>(resume->ticket), false);
    return;
  }
  const auto now = unix_ms();
  if (now - *resume->left_at < store::left_grace_ms) {
    s.store->remember_seat(resume->code, resume->key, false, true, *resume->left_at);
  }
  s.store->clear_resume();
}

}  // namespace

std::string random_nickname(const std::filesystem::path& words) {
  const auto load = [&](const char* name) {
    std::vector<std::string> list;
    FILE* f = std::fopen((words / name).string().c_str(), "rb");
    if (!f) return list;
    std::string text;
    char buf[512];
    for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
    std::fclose(f);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    for (std::size_t start = 0, comma = 0; comma != std::string::npos; start = comma + 1) {
      comma = text.find(',', start);
      if (const auto word = text.substr(start, comma - start); !word.empty()) list.push_back(word);
    }
    return list;
  };
  const auto adjectives = load("adjectives.txt"), animals = load("animals.txt");
  if (adjectives.empty() || animals.empty()) return tr().guest;
  std::random_device rd;
  return adjectives[rd() % adjectives.size()] + " " + animals[rd() % animals.size()];
}

void start_client(Shell& s) {
  s.client.reset();  // before the transport it uses
  s.transport = std::make_unique<transport::WinHttpTransport>();
  s.client = std::make_unique<client::Client>(*s.transport, s.config, *s.bridge);
  s.client->start();
  const std::string gid = s.store->string_or("gid", "");
  if (!gid.empty()) s.client->restore_gid(gid);
  const std::string emoji = s.store->string_or("emoji", "");
  session::event::SetSounds sounds;
  sounds.key = s.audio.keyboard_pack;
  sounds.mouse = s.audio.mouse_pack;
  sounds.pen = s.audio.pen_pack;
  if (!emoji.empty()) sounds.emoji = emoji;
  s.client->set_sounds(sounds);
}

// The part of startup that needs the assets in place.
void enter_app(Shell& s) {
  store::Store& data = *s.store;
  // First: it decides on what is stored, before the default below is.
  const bool first_run = area::account::begin_first_run(s);
  std::string nickname = data.string_or("nickname", "");
  if (const std::string handle = data.string_or("handle", ""); nickname.empty() && !handle.empty()) {
    nickname = "@" + handle;  // an account can go by its handle alone
  } else if (nickname.empty()) {
    nickname = random_nickname(s.assets_dir / locale::current().words_folder);
    if (!first_run) data.set("nickname", store::Json(nickname));
  }
  s.app.home().set_nickname(nickname);
  s.config.server_url = data.string_or("serverUrl", store::default_server_url);
  s.config.nickname = nickname;
  s.config.token = data.string_or("token", "");
  show_store_notice(s);
  if (!first_run) s.app.show_home();
  start_client(s);
  if (!s.smoke_path.empty()) {
    s.smoke_start = clock_ms();
    s.client->create(false, false);
    SetTimer(s.hwnd, smoke_timer, 200, nullptr);
  } else {
    // As upstream: nothing is resumed until the server is known to be there.
    area::connection::check_server(s, [&s] {
      restore_seat(s);
      (void)s.store->save();
    });
  }
  (void)data.save();
}

namespace {

DiagEvent diag_of(assets::Error error) {
  switch (error) {
    case assets::Error::Network: return DiagEvent::AssetsNetwork;
    case assets::Error::Unavailable: return DiagEvent::AssetsUnavailable;
    case assets::Error::BadContent: return DiagEvent::AssetsBadContent;
    case assets::Error::Disk: return DiagEvent::AssetsDisk;
  }
  return DiagEvent::AssetsNetwork;
}

// Said once when the app falls back on the system's font and emoji, not at every start of
// a network that keeps the files out.
void report_assets(Shell& s, const std::expected<void, assets::Error>& result) {
  constexpr const char* key = "assetsFailed";
  if (result) {
    s.store->set(key, store::Json(false));
    return;
  }
  s.log.note(diag_of(result.error()));
  if (!s.store->bool_or(key, false)) {
    s.app.ui().toasts->toast(tr().notice_fetch_failed);
    s.store->set(key, store::Json(true));
  }
}

}  // namespace

void start_install(Shell& s, bool wait) {
  if (wait) s.app.show_setup(0);
  s.assets_worker = std::thread([&s, wait] {
    assets::WindowsPlatform platform(s.asset_url);
    int shown = 0;
    const auto result = assets::install(s.asset_manifest, s.assets_dir, platform,
                                        [&](std::uint64_t done, std::uint64_t total) {
                                          const int percent = static_cast<int>(done * 100 / total);
                                          if (!wait || percent == shown) return;
                                          shown = percent;
                                          s.tasks.post([&s, percent] { s.app.show_setup(percent); });
                                        });
    s.tasks.post([&s, wait, result] {
      s.assets_worker.join();
      if (wait) {
        s.app.reload_assets(s.assets_dir);
        enter_app(s);
      }
      report_assets(s, result);
    });
  });
}

}  // namespace crpp::gui
