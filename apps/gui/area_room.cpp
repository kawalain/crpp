#include <algorithm>
#include <ctime>
#include <optional>

#include "areas.hpp"
#include "shell.hpp"

#include <shellapi.h>  // after windows.h, which shell.hpp brings

using namespace crpp::ui;

namespace crpp::gui::area::room {

namespace {

// Saves the new emoji and tells the server, as start_client does at startup.
void change_emoji(Shell& s, const std::string& id) {
  s.store->set("emoji", store::Json(id));
  (void)s.store->save();
  sound::tell_server_my_sounds(s);
}

void demo_session(App& app, bool chat) {
  app.show_session("ABC234");
  if (chat) {
    const long long now = wall_clock_seconds() * 1000;
    app.room().chat().add({0, "c0", "크롬이", "1f600", "안녕하세요! 처음 뵙겠습니다.", now - 400000});
    app.room().chat().add({0, "c0", "크롬이", "1f600", "같은 사람이 5분 안에 보낸 메시지는 묶여요.", now - 390000});
    app.room().chat().add({1, "c1", "하늘", "1f431", "반가워요 :)\n두 줄짜리 메시지도 잘 보이나요?", now - 120000});
  }
  app.room().set_room({{0, "크롬이", "1f600", false, "c0"},
                       {1, "하늘", "1f431", false, "c1"},
                       {2, "구름빵", "1f436", false, "c2"},
                       {3, "졸린 곰", "1f43b", true, "c3"}},
                      0, 0, false, chat);
}

}  // namespace

void names_changed(Shell& s) {
  std::vector<std::string> names{s.config.nickname};
  if (const std::string handle = s.store->string_or("handle", ""); !handle.empty()) names.push_back("@" + handle);
  s.app.room().chat().set_names(std::move(names));
}

void wire(Shell& s) {
  Shell* sp = &s;
  s.app.room().on_leave = [sp] {
    sp->bridge->leaving();
    sp->client->leave();
  };
  s.app.room().on_set_public = [sp](bool v) { sp->client->set_visibility(v); };
  s.app.room().on_set_chat = [sp](bool v) { sp->client->set_chat_enabled(v); };
  s.app.room().chat().on_send = [sp](std::string text) {
    // The session drops what it cannot send, so this is where that shows.
    if (!sp->bridge->can_send()) return false;
    sp->client->send_chat(text);
    return true;
  };
  s.app.room().chat().on_open_link = [](const std::string& url) {
    // ShellExecute's result above 32 means it worked.
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
  };
  s.bridge->on_chat_blocked = [sp](std::uint64_t ms) { sp->app.room().chat().blocked(ms); };
  // The Room says itself whether it worked, so not Ui::copy, which would say so too.
  s.app.room().on_copy = [sp](std::string text) { return sp->app.ui().clipboard->set_text(widen(text)); };
  s.app.room().on_request_profile = [sp](std::uint32_t slot) { sp->client->request_profile(slot); };
  s.app.room().on_member_volume = [sp](const std::string& cid, int percent) { sound::set_member_volume(*sp, cid, percent); };
  s.app.room().on_member_muted = [sp](const std::string& cid, bool muted) { sound::set_member_muted(*sp, cid, muted); };
  s.app.room().on_pick_emoji = [sp](const std::string& id) { change_emoji(*sp, id); };
  s.bridge->on_profile = [sp](const ProfileInfo& info) {
    sp->app.room().show_profile({info.slot, info.nick, info.emoji, info.handle, info.created, info.stayed, info.events});
  };
}

bool scene(Shell& s, std::string_view name) {
  // Any of these in the dark theme: "room-menu-dark".
  if (name.starts_with("room-") && name.ends_with("-dark")) {
    s.app.set_theme("darkcloud");
    name.remove_suffix(5);
  }
  if (name == "session") {
    demo_session(s.app, false);
    return true;
  }
  if (name == "chat") {
    demo_session(s.app, true);
    return true;
  }
  if (name == "room-muted") {
    s.app.ui().prefs->set(pref::mute_others, true);
    demo_session(s.app, false);
    return true;
  }
  if (name == "room-leave") {
    demo_session(s.app, false);
    s.app.room().click(RoomScreen::Leave, 0, 0, false);
    return true;
  }
  if (name == "room-profile" || name == "room-profile-handle" || name == "room-profile-anniversary") {
    demo_session(s.app, false);
    RoomScreen& room = s.app.room();
    room.profile_card().request(0);
    const bool handle = name != "room-profile";
    // The shot's clock is 1700000000; 730 days before it is the same day two years earlier.
    const std::int64_t made = name == "room-profile-anniversary" ? 1700000000 - 730 * 86400 : 1700000000;
    room.show_profile({0, "크롬이", "1f600", handle ? std::optional<std::string>("chrome") : std::nullopt,
                       handle ? std::optional<std::int64_t>(made) : std::nullopt, 3900, 12345});
    return true;
  }
  if (name == "room-menu") {
    demo_session(s.app, false);
    s.app.room().member_menu().open(1, false, 70, 300, 320);
    return true;
  }
  if (name == "room-calm") {
    demo_session(s.app, true);
    ChatPanel& chat = s.app.room().chat();
    chat.add({1, "c1", "하늘", "1f431", "자료는 https://example.com/docs?page=1 에 있어요.", wall_clock_seconds() * 1000});
    chat.blocked(5000);
    return true;
  }
  if (name == "room-emoji") {
    demo_session(s.app, false);
    s.app.emoji_picker().show("1f600", nullptr);
    return true;
  }
  if (name == "room-text-menu") {  // right-clicked on a link, after the first frame
    demo_session(s.app, true);
    s.app.room().chat().add({1, "c1", "하늘", "1f431", "자료는 https://example.com/docs?page=1 에 있어요.", wall_clock_seconds() * 1000});
    return true;
  }
  if (name == "room-field-menu") {  // the menu key on a selected draft, after the first frame
    demo_session(s.app, true);
    ChatPanel& chat = s.app.room().chat();
    s.app.ui().set_focus(&chat, &chat.input());
    chat.input().set_text(L"보낼까 말까");
    chat.input().select_all();
    return true;
  }
  if (name == "room-calm-firm") {  // the third block in a row
    demo_session(s.app, true);
    ChatPanel& chat = s.app.room().chat();
    for (int i = 0; i < 3; ++i) {
      chat.blocked(5000);
      if (i < 2) (void)chat.tick(clock_ms() + 10000);  // as if it had run out, without the wait
    }
    return true;
  }
  if (name == "room-word-zzz" || name == "room-word-cake" || name == "room-word-star") {  // a word in the code
    demo_session(s.app, false);
    s.app.room().enter_room(name == "room-word-zzz" ? "ZZZ7KQ" : name == "room-word-cake" ? "CAKE42" : "X9STAR");
    return true;
  }
  if (name == "room-weather-cloudy" || name == "room-weather-rain" || name == "room-weather-storm") {
    demo_session(s.app, name == "room-weather-rain");
    // A minute of input as busy as each step, held long enough for the sky to follow.
    const unsigned per_member = name == "room-weather-cloudy" ? 40 : name == "room-weather-rain" ? 120 : 300;
    RoomWeather& weather = s.app.room().weather();
    const auto now = clock_ms();
    weather.note(now, per_member * 4);
    (void)weather.update(now, 4);
    (void)weather.update(now + RoomWeather::hold_ms, 4);
    return true;
  }
  if (name == "room-mini" || name == "room-mini-many") {  // the Mini window, two members making a sound
    demo_session(s.app, false);
    if (name == "room-mini-many") {
      s.app.room().set_room({{0, "크롬이", "1f600", false, "c0"},
                             {1, "하늘", "1f431", false, "c1"},
                             {2, "구름빵", "1f436", false, "c2"},
                             {3, "졸린 곰", "1f43b", true, "c3"},
                             {4, "여우", "1f98a", false, "c4"},
                             {5, "판다", "1f43c", false, "c5"},
                             {6, "토끼", "1f430", false, "c6"}},
                            0, 0, false, false);
    }
    s.app.room().pulse(0);
    s.app.room().pulse(2);
    s.app.toggle_mini();
    return true;
  }
  if (name == "room-mention") {  // someone names this member
    demo_session(s.app, true);
    ChatPanel& chat = s.app.room().chat();
    chat.set_names({"크롬이"});
    chat.add({2, "c2", "구름빵", "1f436", "크롬이님, 오늘 회의 몇 시였죠?", wall_clock_seconds() * 1000});
    return true;
  }
  if (name == "room-new-line") {  // two messages came while the window was behind another
    demo_session(s.app, true);
    ChatPanel& chat = s.app.room().chat();
    const long long now = wall_clock_seconds() * 1000;
    chat.set_away(true);
    chat.add({2, "c2", "구름빵", "1f436", "다녀오셨어요? 회의 자료 올려 뒀어요.", now - 60000});
    chat.add({1, "c1", "하늘", "1f431", "저도 확인했어요!", now - 30000});
    chat.set_away(false);
    return true;
  }
  if (name == "room-chat-left") {  // a draft near the server's limit, with emoji counted as one
    demo_session(s.app, true);
    ChatPanel& chat = s.app.room().chat();
    s.app.ui().set_focus(&chat, &chat.input());
    std::wstring draft;
    while (draft.size() < 172) draft += L"오늘 할 일을 정리했어요 ";
    draft.resize(172);
    chat.input().set_text(draft + L"📝");  // 173 characters, 27 left
    return true;
  }
  if (name == "room-options") {
    demo_session(s.app, true);
    s.app.ui().prefs->set(pref::pin_chat, true);
    s.app.room().click(RoomScreen::More, 0, 0, false);
    return true;
  }
  return false;
}

bool after_frame(Shell& s, std::string_view name) {
  if (name.starts_with("room-") && name.ends_with("-dark")) name.remove_suffix(5);
  if (name == "room-text-menu") {
    const ChatPanel& chat = s.app.room().chat();
    const Ui::Hit* link = s.app.ui().find_hit(&chat, ChatPanel::LinkBase + 3 * ChatPanel::links_per_entry);
    if (!link) return false;
    s.app.right_click(link->rect.x + link->rect.w / 2, link->rect.y + link->rect.h / 2);
    return true;
  }
  if (name == "room-field-menu") return s.app.context_key();
  return false;
}

}  // namespace crpp::gui::area::room
