#include <algorithm>
#include <ctime>

#include "areas.hpp"
#include "shell.hpp"

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
  s.app.room().chat().on_send = [sp](std::string text) { sp->client->send_chat(text); };
  s.app.room().on_copy = [sp](std::string text) { return copy_to_clipboard(sp->hwnd, text); };
}

bool scene(Shell& s, std::string_view name) {
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
  if (name == "room-options") {
    demo_session(s.app, true);
    s.app.ui().prefs->set(pref::pin_chat, true);
    s.app.room().click(RoomScreen::More, 0, 0, false);
    return true;
  }
  return false;
}

}  // namespace crpp::gui::area::room
