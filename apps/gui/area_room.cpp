#include <algorithm>
#include <ctime>
#include <optional>

#include "areas.hpp"
#include "shell.hpp"

#include <shellapi.h>  // after windows.h, which shell.hpp brings

using namespace crpp::ui;

namespace crpp::gui::area::room {

namespace {

bool copy_to_clipboard(HWND hwnd, const std::string& text) {
  const std::wstring w = widen(text);
  if (!OpenClipboard(hwnd)) return false;
  bool copied = false;
  if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t))) {
    auto* dst = static_cast<wchar_t*>(GlobalLock(mem));
    std::copy(w.begin(), w.end(), dst);
    dst[w.size()] = L'\0';
    GlobalUnlock(mem);
    EmptyClipboard();
    copied = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
    if (!copied) GlobalFree(mem);
  }
  CloseClipboard();
  return copied;
}

// Saves the new emoji and tells the server, as start_client does at startup.
void change_emoji(Shell& s, const std::string& id) {
  s.store->set("emoji", store::Json(id));
  (void)s.store->save();
  session::event::SetSounds sounds;
  sounds.key = s.audio.keyboard_pack;
  sounds.mouse = s.audio.mouse_pack;
  sounds.pen = s.audio.pen_pack;
  sounds.emoji = id;
  s.client->set_sounds(sounds);
}

void demo_session(App& app, bool chat) {
  app.show_session("ABC234");
  if (chat) {
    const long long now = static_cast<long long>(std::time(nullptr)) * 1000;
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
  s.app.room().on_copy = [sp](std::string text) { return copy_to_clipboard(sp->hwnd, text); };
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
    s.app.set_theme(ThemeId::DarkCloud);
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
  if (name == "room-leave") {
    demo_session(s.app, false);
    s.app.room().click(RoomScreen::Leave, 0, 0, false);
    return true;
  }
  if (name == "room-profile" || name == "room-profile-handle") {
    demo_session(s.app, false);
    RoomScreen& room = s.app.room();
    room.profile_card().request(0);
    const bool handle = name == "room-profile-handle";
    room.show_profile({0, "크롬이", "1f600", handle ? std::optional<std::string>("chrome") : std::nullopt,
                       handle ? std::optional<std::int64_t>(1700000000) : std::nullopt, 3900, 12345});
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
    chat.add({1, "c1", "하늘", "1f431", "자료는 https://example.com/docs?page=1 에 있어요.", static_cast<long long>(std::time(nullptr)) * 1000});
    chat.blocked(5000);
    return true;
  }
  if (name == "room-emoji") {
    demo_session(s.app, false);
    s.app.emoji_picker().show("1f600", nullptr);
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

}  // namespace crpp::gui::area::room
