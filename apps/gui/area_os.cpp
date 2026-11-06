// The tray icon, closing to it, what the window gives up while nobody sees it, and how
// chat and members reach the user while they look elsewhere.
#include <windows.h>

#include <shellapi.h>
#include <windowsx.h>

#include <climits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "areas.hpp"
#include "autostart.hpp"
#include "autostart_windows.hpp"
#include "os_policy.hpp"
#include "shell.hpp"
#include "tray.hpp"

using namespace crpp::ui;

namespace crpp::gui::area::os {

namespace {

constexpr const char* hint_shown_key = "roomStaysConnectedShown";  // not upstream's

constexpr std::string_view chat_sound_key = "cloud_chat", chat_sound_file = "01_cloud_chat";
constexpr std::string_view enter_sound_key = "cloud_enter", enter_sound_file = "02_cloud_enter";

struct State {
  TrayIcon tray;
  UINT taskbar_created = 0;
  UINT show_window_requested = 0;
  std::unique_ptr<RunKey> run_key;
  bool reverting_autostart = false;
  bool flashing = false;
  bool notified = false;        // chat notifications may still be on screen
  int balloons = 0;             // shown or queued, for an icon that exists only for them
  bool announce_entering = false;  // the next member list is the Room I just entered
  std::vector<std::string> known_members;
};
State state;  // one window per process

[[nodiscard]] bool tray_wanted(const Shell& s) { return s.prefs->get(pref::minimize_to_tray); }

// With the tray setting off the icon exists only while a notification needs it, since
// Windows shows notifications only from an icon.
void balloon_done(Shell& s) {
  if (state.balloons > 0) --state.balloons;
  if (state.balloons == 0 && !tray_wanted(s)) state.tray.remove();
}

bool show_notification(Shell& s, std::wstring_view title, std::wstring_view body, HICON icon, DWORD flags) {
  if (!state.tray.add()) return false;
  if (!state.tray.notify(title, body, icon, flags)) {
    if (state.balloons == 0 && !tray_wanted(s)) state.tray.remove();
    return false;
  }
  ++state.balloons;
  return true;
}

void play(Shell& s, std::string_view key, std::string_view file) {
  if (preferences::notification_enabled(s, key)) sound::play_notification(s, file);
}

// The sender's emoji beside their message, as upstream. The sound is our own, and has
// its own setting.
void notify_chat(Shell& s, const ChatEntry& entry) {
  const int size = GetSystemMetrics(SM_CXICON);
  const auto& image = s.app.ui().emoji_image(entry.emoji);
  const auto pixels = render_pixels(s.app.factory(), size, [&](ID2D1RenderTarget* rt) {
    Painter painter(s.app.ui().gfx, rt);
    draw_svg(painter, image, {0, 0, static_cast<float>(size), static_cast<float>(size)});
  });
  HICON icon = icon_from_pixels(pixels, size);
  const std::wstring title = widen(entry.nick.empty() ? std::string_view("게스트") : std::string_view(entry.nick));
  if (show_notification(s, title, widen(entry.text), icon, NIIF_NOSOUND | NIIF_RESPECT_QUIET_TIME)) state.notified = true;
  if (icon) DestroyIcon(icon);
}

void flash_taskbar(Shell& s, DWORD flags) {
  FLASHWINFO info{};
  info.cbSize = sizeof info;
  info.hwnd = s.hwnd;
  info.dwFlags = flags;
  info.uCount = flags == FLASHW_STOP ? 0 : UINT_MAX;
  FlashWindowEx(&info);
}

void on_chat(Shell& s, const ChatEntry& entry) {
  if (entry.text.empty()) return;
  const auto reaction = os_policy::chat_reaction({
      .mine = entry.slot == s.bridge->my_slot(),
      .muted = sound::member_muted(s, entry.cid),
      .hidden = s.hidden,
      .focused = GetForegroundWindow() == s.hwnd,
      .tray_notify = s.prefs->get(pref::chat_tray_notify),
      .taskbar_flash = s.prefs->get(pref::chat_taskbar_flash),
      .flashing = state.flashing,
  });
  if (reaction.sound) play(s, chat_sound_key, chat_sound_file);
  if (reaction.notify) notify_chat(s, entry);
  if (reaction.flash) {
    flash_taskbar(s, FLASHW_TRAY | FLASHW_TIMERNOFG);
    state.flashing = true;
  }
}

void on_members(Shell& s, const std::vector<MemberView>& members) {
  std::vector<os_policy::MemberFacts> next;
  next.reserve(members.size());
  for (const auto& m : members) {
    next.push_back({os_policy::member_id(m.cid, m.slot), m.slot, m.reconnected, sound::member_muted(s, m.cid)});
  }
  if (state.announce_entering || os_policy::someone_entered(state.known_members, next, s.bridge->my_slot())) {
    play(s, enter_sound_key, enter_sound_file);
  }
  state.announce_entering = false;
  state.known_members.clear();
  for (auto& m : next) state.known_members.push_back(std::move(m.id));
}

// The window is in front again: what asked for attention has it.
void on_seen(Shell& s) {
  if (state.flashing) flash_taskbar(s, FLASHW_STOP);
  state.flashing = false;
  if (state.notified) state.tray.clear_notification();
  state.notified = false;
  if (!tray_wanted(s)) {  // deleting the icon takes its notifications along
    state.tray.remove();
    state.balloons = 0;
  }
}

void show_window(Shell& s) {
  ShowWindow(s.hwnd, SW_SHOW);
  if (IsIconic(s.hwnd)) ShowWindow(s.hwnd, SW_RESTORE);
  SetForegroundWindow(s.hwnd);
}

// Input timing keeps being shared with the Room while the window is away.
void hint_room_stays_connected(Shell& s) {
  if (!os_policy::hide_needs_hint(s.app.view() == View::Session, s.store->bool_or(hint_shown_key, false))) return;
  if (show_notification(s, L"Cloudroom++", L"창을 숨겨도 구름에 계속 연결되어 있어요. 키보드와 마우스 입력 시점도 계속 공유돼요.",
                        nullptr, NIIF_NOSOUND)) {
    s.store->set(hint_shown_key, store::Json(true));
  }
}

void close_to_tray(Shell& s) {
  ShowWindow(s.hwnd, SW_HIDE);
  hint_room_stays_connected(s);
}

void show_menu(Shell& s, int x, int y) {
  HMENU menu = CreatePopupMenu();
  if (!menu) return;
  constexpr UINT_PTR exit_item = 1;
  AppendMenuW(menu, MF_STRING, exit_item, L"Cloudroom++ 종료");
  // Without both of these the menu does not close on a click elsewhere, or closes
  // at once the second time it opens (TrackPopupMenu's documentation).
  SetForegroundWindow(s.hwnd);
  const BOOL chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, x, y, 0, s.hwnd, nullptr);
  PostMessageW(s.hwnd, WM_NULL, 0, 0);
  DestroyMenu(menu);
  if (static_cast<UINT_PTR>(chosen) == exit_item) DestroyWindow(s.hwnd);
}

void on_tray_event(Shell& s, WPARAM wp, LPARAM lp) {
  switch (LOWORD(lp)) {
    case NIN_SELECT:
    case NIN_KEYSELECT:
      show_window(s);
      break;
    case WM_CONTEXTMENU:
      show_menu(s, GET_X_LPARAM(wp), GET_Y_LPARAM(wp));
      break;
    case NIN_BALLOONUSERCLICK:
      show_window(s);
      balloon_done(s);
      break;
    case NIN_BALLOONTIMEOUT:
    case NIN_BALLOONHIDE:
      balloon_done(s);
      break;
    default:
      break;
  }
}

// Only when the setting changes: Windows asks programs run from this key not to write
// to it during their run, so a logon does not refresh it as upstream's does.
void apply_start_with_system(Shell& s) {
  wchar_t exe[MAX_PATH];
  const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
  const bool enabled = s.prefs->get(pref::start_with_system);
  if (n > 0 && n < MAX_PATH && apply_autostart(*state.run_key, enabled, std::wstring_view(exe, n))) return;
  state.reverting_autostart = true;
  s.prefs->set(pref::start_with_system, !enabled);
  state.reverting_autostart = false;
  s.app.toasts().toast("시스템과 함께 자동 실행 설정을 바꾸지 못했어요.");
}

// A hidden window draws nothing. What it drew with comes back on the next WM_PAINT.
void sync_hidden(Shell& s) {
  const bool hidden = os_policy::window_hidden(IsWindowVisible(s.hwnd) != FALSE, IsIconic(s.hwnd) != FALSE);
  if (hidden == s.hidden) return;
  s.hidden = hidden;
  if (hidden) {
    s.target.reset();
    if (s.app.ui().emoji) s.app.ui().emoji.emplace(s.assets_dir);
    // Freed memory stays in the working set until asked for. What is still in use comes
    // back on the next touch, at the price of one soft fault per page.
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
  } else {
    InvalidateRect(s.hwnd, nullptr, FALSE);
  }
  sync_frames(s);
}

}  // namespace

void wire(Shell& s) {
  state.tray.attach(s.hwnd, s.app.ui().gfx);
  state.taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
  state.show_window_requested = RegisterWindowMessageW(show_window_message);
  if (s.throwaway_data) state.run_key = std::make_unique<MemoryRunKey>();
  else state.run_key = std::make_unique<WindowsRunKey>();
  if (tray_wanted(s)) state.tray.add();

  // These chain: another area may listen to the same events.
  Shell* sp = &s;
  s.bridge->on_chat = [sp, previous = std::move(s.bridge->on_chat)](const ChatEntry& entry) {
    if (previous) previous(entry);
    on_chat(*sp, entry);
  };
  s.bridge->on_members = [sp, previous = std::move(s.bridge->on_members)](const std::vector<MemberView>& members) {
    if (previous) previous(members);
    on_members(*sp, members);
  };
  s.bridge->on_room_changed = [sp, previous = std::move(s.bridge->on_room_changed)](bool in_room) {
    if (previous) previous(in_room);
    state.known_members.clear();
    // Upstream is silent when it returns to the Room of the last run; a resume is what
    // the store still holds in that case, and a new Room has none yet.
    state.announce_entering = in_room && !sp->store->resume();
    // A Room can start while nobody sees the window: started hidden, it resumes one.
    if (in_room && sp->hidden) hint_room_stays_connected(*sp);
  };
}

bool scene(Shell&, std::string_view) { return false; }

void pref_changed(Shell& s, std::string_view key) {
  if (key == pref::minimize_to_tray.name) {
    if (tray_wanted(s)) state.tray.add();
    else if (state.balloons == 0) state.tray.remove();
  }
  if (key == pref::start_with_system.name && !state.reverting_autostart) apply_start_with_system(s);
}

void first_show(Shell& s, bool minimized) {
  if (!minimized) {
    ShowWindow(s.hwnd, SW_SHOWDEFAULT);
    return;
  }
  if (!os_policy::start_hidden(tray_wanted(s), state.tray.present())) ShowWindow(s.hwnd, SW_SHOWMINNOACTIVE);
}

bool on_message(Shell& s, UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) {
  if (msg == wm_tray) {
    if (HIWORD(lp) == tray_icon_id) on_tray_event(s, wp, lp);
    result = 0;
    return true;
  }
  if (msg != 0 && msg == state.show_window_requested) {
    show_window(s);
    result = 0;
    return true;
  }
  if (msg != 0 && msg == state.taskbar_created) {
    state.tray.forget();
    state.balloons = 0;
    if (tray_wanted(s)) state.tray.add();
    result = 0;
    return true;
  }
  switch (msg) {
    case WM_CLOSE:
      // Tried again here: the taskbar may have been busy when it first was.
      if (tray_wanted(s)) state.tray.add();
      if (!os_policy::close_hides(tray_wanted(s), state.tray.present())) return false;
      close_to_tray(s);
      result = 0;
      return true;
    case WM_WINDOWPOSCHANGED:
      sync_hidden(s);
      return false;
    case WM_ACTIVATE:
      if (LOWORD(wp) != WA_INACTIVE) {
        on_seen(s);
      } else if (s.app.room().member_menu().is_open()) {
        s.app.room().member_menu().close();  // window.addEventListener("blur", closeMemberMenu)
        InvalidateRect(s.hwnd, nullptr, FALSE);
      }
      return false;
    case WM_DESTROY:
      state.tray.remove();
      return false;
    default:
      return false;
  }
}

}  // namespace crpp::gui::area::os
