// What happens between the window opening and the Home screen.
#include <cstdio>
#include <random>
#include <vector>

#include "areas.hpp"
#include "crpp/assets/built_in.hpp"
#include "crpp/assets/windows_platform.hpp"
#include "shell.hpp"

using namespace crpp::ui;

namespace crpp::gui {

namespace {

// --- moving away from the Shared store ---

void show_cleanup_offer(Shell& s) {
  s.app.notice().set(Notice{"안전한 저장소로 옮겼어요. 기존 Cloudroom 파일에 남은 로그인 정보와 채팅 기록도 지울까요? "
                            "지우면 기존 Cloudroom 클라이언트에서 다시 로그인해야 해요.",
                            "지우기", "남겨 두기"});
  s.app.notice().on_action = [&s] {
    s.app.notice().set(Notice{s.store->erase_shared_secrets() ? "기존 파일의 로그인 정보와 채팅 기록을 지웠어요."
                                                              : "기존 파일을 고치지 못했어요. 파일이 그대로 남아 있어요.",
                              "", "닫기"});
  };
}

void show_store_notice(Shell& s) {
  if (s.store->mode() == store::Mode::Shared) {
    s.app.notice().set(Notice{"기존 Cloudroom의 설정 파일을 함께 쓰고 있어요. 이 파일에는 로그인 정보가 암호화되지 않은 채 "
                              "저장돼요.",
                              "안전한 저장소로 옮기기", "닫기"});
    s.app.notice().on_action = [&s] {
      if (s.store->convert_to_protected()) {
        show_cleanup_offer(s);
      } else {
        s.app.notice().set(Notice{"옮기지 못했어요. 지금까지처럼 기존 파일을 계속 써요.", "", "닫기"});
      }
    };
  } else if (s.store->secrets_lost()) {
    s.app.notice().set(Notice{"저장된 로그인 정보와 채팅 기록을 이 PC에서 읽지 못해 새로 시작해요.", "", "닫기"});
  } else if (s.store->data_corrupt()) {
    s.app.notice().set(Notice{"설정 파일이 손상되어 기본값으로 시작해요. 손상된 파일은 .bad 이름으로 남겨 두었어요.", "", "닫기"});
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
    for (std::size_t i = 0; (i = text.find('\'', i)) != std::string::npos;) {
      const auto end = text.find('\'', i + 1);
      if (end == std::string::npos) break;
      list.push_back(text.substr(i + 1, end - i - 1));
      i = end + 1;
    }
    return list;
  };
  const auto adjectives = load("adjectives.txt"), animals = load("animals.txt");
  if (adjectives.empty() || animals.empty()) return "게스트";
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
    nickname = random_nickname(s.assets_dir / "words");
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
    s.smoke_start = GetTickCount64();
    s.client->create(false, false);
    SetTimer(s.hwnd, smoke_timer, 200, nullptr);
  } else {
    restore_seat(s);
  }
  (void)data.save();
}

void start_install(Shell& s) {
  s.app.show_setup(0);
  s.assets_worker = std::thread([&s] {
    assets::WindowsPlatform platform(s.asset_url);
    int shown = 0;
    const auto result = assets::install(assets::built_in_manifest(), s.assets_dir, platform,
                                        [&](std::uint64_t done, std::uint64_t total) {
                                          const int percent = static_cast<int>(done * 100 / total);
                                          if (percent == shown) return;
                                          shown = percent;
                                          s.tasks.post([&s, percent] { s.app.show_setup(percent); });
                                        });
    s.tasks.post([&s, result] {
      s.assets_worker.join();
      if (result) {
        s.app.reload_assets(s.assets_dir);
        enter_app(s);
      } else if (result.error() == assets::Error::Disk) {
        s.app.show_setup(0, "파일을 저장하지 못했어요. 저장 공간을 확인해 주세요.");
      } else if (result.error() == assets::Error::Network) {
        s.app.show_setup(0, "파일을 받지 못했어요. 인터넷 연결을 확인해 주세요.");
      } else {
        s.app.show_setup(0, "필요한 파일을 제대로 받지 못했어요. 잠시 후 다시 시도해 주세요.");
      }
    });
  });
}

}  // namespace crpp::gui
