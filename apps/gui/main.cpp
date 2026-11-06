// crpp: the native Cloudroom window.
//   crpp                          run the app
//   crpp --shot out.png [WxH] [hour] [home|session|chat|setup|setup-failed] [notice]
//                                 render a screen offscreen and exit
//   crpp --smoke out.png          create a room, save a screenshot and exit
//   ... --dpi <n>                 (with --shot) render at that DPI; WxH stay in
//                                 DIPs, so the image grows with it
//   ... --asset-url <url>         fetch assets from <url>, not the published
//                                 ones; the manifest still decides what is kept
//   ... --data <dir>              (last) keep data in <dir>, not the user's
//                                 folders; other instances are then allowed
#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <expected>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <thread>

#include "crpp/assets/built_in.hpp"
#include "crpp/assets/windows_platform.hpp"
#include "crpp/client/client.hpp"
#include "crpp/input/raw_input_source.hpp"
#include "crpp/sound/engine.hpp"
#include "crpp/sound/wasapi_device.hpp"
#include "crpp/store/store.hpp"
#include "crpp/store/windows_platform.hpp"
#include "crpp/transport/winhttp_transport.hpp"
#include "crpp/ui/app.hpp"
#include "crpp/ui/png.hpp"

using namespace crpp;
using namespace crpp::ui;

namespace {

constexpr UINT_PTR blink_timer = 1;
constexpr UINT_PTR frame_timer = 2;
constexpr UINT_PTR smoke_timer = 3;
constexpr UINT_PTR save_timer = 4;
constexpr UINT save_delay_ms = 2000;  // chat arrives in bursts; write once per burst
constexpr UINT wm_ui_task = WM_APP + 1;
constexpr UINT wm_own_input = WM_APP + 2;

std::int64_t unix_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::filesystem::path exe_dir() {
  wchar_t buf[MAX_PATH];
  const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  return std::filesystem::path(std::wstring(buf, n)).parent_path();
}

// A guest nickname like the upstream one: "<adjective> <animal>".
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

// Runs closures on the UI thread; the client's callbacks arrive on another.
class UiTasks {
 public:
  void attach(HWND hwnd) { hwnd_ = hwnd; }
  void post(std::function<void()> f) {
    {
      std::lock_guard lock(m_);
      q_.push_back(std::move(f));
    }
    if (hwnd_) PostMessageW(hwnd_, wm_ui_task, 0, 0);
  }
  void run_all() {
    std::deque<std::function<void()>> batch;
    {
      std::lock_guard lock(m_);
      batch.swap(q_);
    }
    for (auto& f : batch) f();
  }

 private:
  HWND hwnd_ = nullptr;
  std::mutex m_;
  std::deque<std::function<void()>> q_;
};

// Turns client events into UI and store changes. The store and the UI are only
// touched on the UI thread, inside posted tasks and the methods marked so.
class Bridge final : public client::Observer {
 public:
  Bridge(App& app, UiTasks& tasks, store::Store& store, sound::Engine& engine)
      : app_(app), tasks_(tasks), store_(store), engine_(engine) {}

  // Called when the token is rejected; the client cannot go on with it.
  std::function<void()> on_unauthorized;
  // UI thread: input is observed only while in a Room.
  std::function<void(bool in_room)> on_room_changed;

  // UI thread: remember which held Seat a resume is for, to forget it if refused.
  void expect_resume(std::string code) { resume_code_ = std::move(code); }

  // UI thread: the user leaves. The Seat stays held for a while.
  void leaving() {
    const auto now = unix_ms();
    if (const auto resume = store_.resume()) store_.remember_seat(resume->code, resume->key, is_public_, true, now);
    store_.clear_resume();
    store_.mark_left(room_code_, now);
    room_code_.clear();
    if (on_room_changed) on_room_changed(false);
    (void)store_.save();
  }

  void on_status(session::State s) override {
    if (s != session::State::Idle) return;
    tasks_.post([this] {
      const auto now = unix_ms();
      if (keep_seat_) {
        if (const auto resume = store_.resume()) store_.remember_seat(resume->code, resume->key, is_public_, false, now);
      }
      // Still set here means the Room ended without the user leaving.
      store_.mark_left(room_code_, now);
      room_code_.clear();
      store_.clear_resume();
      keep_seat_ = false;
      resume_code_.clear();
      if (on_room_changed) on_room_changed(false);
      app_.show_home();
      (void)store_.save();
    });
  }
  void on_deliver(const wire::ServerMessage& m) override {
    const wire::RoomState* room = nullptr;
    std::string code;
    std::uint32_t me = me_;
    if (const auto* j = std::get_if<wire::msg::Joined>(&m)) {
      room = &j->room;
      code = std::string(j->code);
      me = j->me;
    } else if (const auto* u = std::get_if<wire::msg::Members>(&m)) {
      room = &u->room;
    }
    if (const auto* c = std::get_if<wire::msg::Chat>(&m)) {
      ChatEntry entry{c->slot, std::string(c->cid), std::string(c->nick), c->emoji ? std::string(*c->emoji) : std::string{},
                      std::string(c->text), c->time};
      tasks_.post([this, entry = std::move(entry), slot = c->slot]() mutable {
        store_.record_chat(room_code_, {slot, entry.cid, entry.nick, entry.emoji, entry.text, entry.time_ms});
        app_.room().chat().add(std::move(entry));
      });
      return;
    }
    if (!room) return;
    my_slot_.store(me, std::memory_order_relaxed);
    std::vector<MemberView> members;
    std::vector<sound::Member> sounds;
    for (const auto& info : room->members) {
      members.push_back({info.slot, std::string(info.nick), info.emoji ? std::string(*info.emoji) : std::string{},
                         info.offline});
      sounds.push_back({info.slot, std::string(info.key.value_or("")), std::string(info.mouse.value_or("")),
                        std::string(info.pen.value_or(""))});
    }
    engine_.set_members(std::move(sounds));
    const auto host = room->host;
    const bool is_public = room->is_public, chat = room->chat;
    tasks_.post([=, this, members = std::move(members), code = std::move(code)]() mutable {
      me_ = me;
      is_public_ = is_public;
      if (!code.empty()) enter(code);
      app_.room().set_room(std::move(members), me, host, is_public, chat);
    });
  }
  void on_peer_input(std::span<const wire::ServerEvent> events) override {
    std::vector<std::uint32_t> slots;
    const std::uint32_t me = my_slot_.load(std::memory_order_relaxed);
    for (const auto& e : events) {
      slots.push_back(e.slot);
      if (e.slot != me) engine_.play_peer(e.slot, e.kind);
    }
    tasks_.post([this, slots = std::move(slots)] {
      for (const auto s : slots) app_.room().pulse(s);
    });
  }
  void on_failed(session::Failure reason, std::string_view) override {
    tasks_.post([this, reason] {
      switch (reason) {
        case session::Failure::Unauthorized:
          if (on_unauthorized) on_unauthorized();
          [[fallthrough]];
        case session::Failure::ConnectFailed:
          keep_seat_ = true;  // the server may still hold it
          break;
        default:
          if (!resume_code_.empty()) store_.forget_seat(resume_code_);
          break;
      }
      app_.show_home();
    });
  }
  void on_persist(const session::Persistent& p) override {
    tasks_.post([this, code = std::string(p.code), key = std::string(p.key), ticket = std::string(p.ticket),
                 gid = std::string(p.gid)] {
      if (!gid.empty()) store_.set("gid", store::Json(gid));
      // Ignored outside the entered Room, so a late message cannot undo leaving.
      if (!key.empty() && code == room_code_) store_.set_resume({code, key, ticket, std::nullopt});
      (void)store_.save();
    });
  }

 private:
  void enter(const std::string& code) {
    const bool same_room = code == room_code_;  // a reconnect keeps what is on screen
    room_code_ = code;
    resume_code_.clear();
    keep_seat_ = false;
    store_.forget_seat(code);
    app_.show_session(code);
    if (same_room) return;
    if (on_room_changed) on_room_changed(true);
    app_.room().chat().clear();
    for (auto& r : store_.enter_room(code)) {
      app_.room().chat().add({r.slot.value_or(0), std::move(r.cid), std::move(r.nick), std::move(r.emoji), std::move(r.text), r.time_ms});
    }
  }

  App& app_;
  UiTasks& tasks_;
  store::Store& store_;
  sound::Engine& engine_;
  std::atomic<std::uint32_t> my_slot_{0xFFFFFFFF};
  std::uint32_t me_ = 0;
  bool is_public_ = false;
  bool keep_seat_ = false;
  std::string room_code_;
  std::string resume_code_;
};

// Every input goes to the server and, if the user wants, to their own speakers.
class InputSink final : public input::Sink {
 public:
  InputSink(std::unique_ptr<client::Client>& client, std::unique_ptr<sound::Engine>& engine)
      : client_(client), engine_(engine) {}
  void attach(HWND hwnd) noexcept { hwnd_ = hwnd; }
  void on_input(wire::Kind kind) noexcept override {
    client_->input(kind);
    engine_->play_own(kind);
    // The server never sends a member's input back, so their ring is lit from
    // here. One message at a time, and no heap on this path.
    if (!pulse_posted_.exchange(true, std::memory_order_relaxed)) PostMessageW(hwnd_, wm_own_input, 0, 0);
  }
  // UI thread: the next input may post again.
  void pulse_taken() noexcept { pulse_posted_.store(false, std::memory_order_relaxed); }

 private:
  std::unique_ptr<client::Client>& client_;
  std::unique_ptr<sound::Engine>& engine_;
  HWND hwnd_ = nullptr;
  std::atomic<bool> pulse_posted_{false};
};

struct Shell {
  App app;
  UiTasks tasks;
  ComPtr<ID2D1HwndRenderTarget> target;
  bool tracking = false;
  bool frame_timer_on = false;
  bool blink_timer_on = false;
  UINT dpi = USER_DEFAULT_SCREEN_DPI;
  std::filesystem::path assets_dir;
  std::string asset_url;
  std::thread assets_worker;
  std::wstring smoke_path;  // --smoke: create a room, save a screenshot, leave, exit
  ULONGLONG smoke_start = 0;
  bool smoke_left = false;
  bool save_timer_on = false;
  store::WindowsPlatform platform;
  std::optional<store::Store> store;
  client::Config config;
  std::unique_ptr<transport::WinHttpTransport> transport;
  std::unique_ptr<Bridge> bridge;
  std::unique_ptr<client::Client> client;
  sound::WasapiDevice audio_device;
  sound::Settings audio;
  std::unique_ptr<sound::Engine> engine;
  InputSink input_sink{client, engine};
  std::unique_ptr<input::RawInputSource> input;
};

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

void save_window_size(Shell& s, HWND hwnd) {
  RECT rc;
  if (IsIconic(hwnd) || !GetClientRect(hwnd, &rc) || rc.right <= 0 || rc.bottom <= 0) return;
  // Without the scale, so the window keeps its size on any monitor.
  s.store->set("windowWidth", store::Json(static_cast<std::int64_t>(MulDiv(rc.right, 96, static_cast<int>(s.dpi)))));
  s.store->set("windowHeight", store::Json(static_cast<std::int64_t>(MulDiv(rc.bottom, 96, static_cast<int>(s.dpi)))));
}

void ensure_target(Shell& s, HWND hwnd) {
  if (s.target) return;
  RECT rc;
  GetClientRect(hwnd, &rc);
  const auto dpi = static_cast<float>(s.dpi);
  const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(), dpi, dpi);
  const auto hwnd_props = D2D1::HwndRenderTargetProperties(
      hwnd, D2D1::SizeU(static_cast<UINT32>(rc.right - rc.left), static_cast<UINT32>(rc.bottom - rc.top)));
  s.app.factory()->CreateHwndRenderTarget(props, hwnd_props, s.target.put());
}

void place_ime(Shell& s, HWND hwnd) {
  HIMC imc = ImmGetContext(hwnd);
  if (!imc) return;
  const Rect c = s.app.caret_rect();
  const auto pixels = [&](float dips) { return static_cast<LONG>(dips * static_cast<float>(s.dpi) / 96); };
  COMPOSITIONFORM cf{};
  cf.dwStyle = CFS_POINT;
  cf.ptCurrentPos = {pixels(c.x), pixels(c.y)};
  ImmSetCompositionWindow(imc, &cf);
  CANDIDATEFORM cand{};
  cand.dwStyle = CFS_CANDIDATEPOS;
  cand.ptCurrentPos = {pixels(c.x), pixels(c.bottom())};
  ImmSetCandidateWindow(imc, &cand);
  ImmReleaseContext(hwnd, imc);
}

std::wstring ime_string(HIMC imc, DWORD kind) {
  const LONG bytes = ImmGetCompositionStringW(imc, kind, nullptr, 0);
  if (bytes <= 0) return {};
  std::wstring s(static_cast<std::size_t>(bytes) / sizeof(wchar_t), L'\0');
  ImmGetCompositionStringW(imc, kind, s.data(), static_cast<DWORD>(bytes));
  return s;
}

void copy_to_clipboard(HWND hwnd, const std::string& text) {
  const std::wstring w = widen(text);
  if (!OpenClipboard(hwnd)) return;
  if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t))) {
    auto* dst = static_cast<wchar_t*>(GlobalLock(mem));
    std::copy(w.begin(), w.end(), dst);
    dst[w.size()] = L'\0';
    GlobalUnlock(mem);
    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
  }
  CloseClipboard();
}

// The caret timer runs only while a text field can actually take typing.
void sync_blink(Shell& s, HWND hwnd) {
  const bool wanted = s.app.wants_blink() && GetFocus() == hwnd;
  if (wanted == s.blink_timer_on) return;
  if (wanted) SetTimer(hwnd, blink_timer, 530, nullptr);
  else KillTimer(hwnd, blink_timer);
  s.blink_timer_on = wanted;
}

// The frame timer runs only while an effect is animating.
void sync_frames(Shell& s, HWND hwnd) {
  if (s.app.animating() && !s.frame_timer_on) {
    SetTimer(hwnd, frame_timer, 16, nullptr);
    s.frame_timer_on = true;
  }
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  auto* s = reinterpret_cast<Shell*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE) {
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    return DefWindowProcW(hwnd, msg, wp, lp);
  }
  if (!s) return DefWindowProcW(hwnd, msg, wp, lp);
  const auto repaint = [&](bool needed) {
    if (needed) InvalidateRect(hwnd, nullptr, FALSE);
  };
  const auto dips = [&](int pixels) { return static_cast<float>(pixels) * 96 / static_cast<float>(s->dpi); };
  switch (msg) {
    case WM_DPICHANGED: {
      s->dpi = HIWORD(wp);
      if (s->target) s->target->SetDpi(static_cast<float>(s->dpi), static_cast<float>(s->dpi));
      const auto* suggested = reinterpret_cast<const RECT*>(lp);
      SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                   suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
      sync_blink(*s, hwnd);
      return 0;
    case WM_DESTROY:
      KillTimer(hwnd, blink_timer);
      KillTimer(hwnd, frame_timer);
      KillTimer(hwnd, save_timer);
      if (s->store) save_window_size(*s, hwnd);
      PostQuitMessage(0);
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_SIZE:
      if (s->target) s->target->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      BeginPaint(hwnd, &ps);
      ensure_target(*s, hwnd);
      if (s->target) {
        const D2D1_SIZE_F size = s->target->GetSize();  // in DIPs
        s->target->BeginDraw();
        s->app.paint(s->target.get(), size.width, size.height);
        if (s->target->EndDraw() == D2DERR_RECREATE_TARGET) s->target.reset();
      }
      EndPaint(hwnd, &ps);
      place_ime(*s, hwnd);
      return 0;
    }
    case wm_ui_task:
      s->tasks.run_all();
      InvalidateRect(hwnd, nullptr, FALSE);
      sync_blink(*s, hwnd);
      sync_frames(*s, hwnd);
      if (s->store && s->store->has_unsaved() && !s->save_timer_on) {
        SetTimer(hwnd, save_timer, save_delay_ms, nullptr);
        s->save_timer_on = true;
      }
      return 0;
    case wm_own_input:
      s->input_sink.pulse_taken();
      s->app.room().pulse_me();
      InvalidateRect(hwnd, nullptr, FALSE);
      sync_frames(*s, hwnd);
      return 0;
    case WM_MOUSEMOVE:
      if (!s->tracking) {
        TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&t);
        s->tracking = true;
      }
      repaint(s->app.mouse_move(dips(GET_X_LPARAM(lp)), dips(GET_Y_LPARAM(lp))));
      return 0;
    case WM_MOUSELEAVE:
      s->tracking = false;
      repaint(s->app.mouse_leave());
      return 0;
    case WM_LBUTTONDOWN:
      SetFocus(hwnd);
      SetCapture(hwnd);
      repaint(s->app.mouse_down(dips(GET_X_LPARAM(lp)), dips(GET_Y_LPARAM(lp)), (wp & MK_SHIFT) != 0));
      sync_blink(*s, hwnd);
      return 0;
    case WM_LBUTTONUP:
      ReleaseCapture();
      repaint(s->app.mouse_up());
      return 0;
    case WM_MOUSEWHEEL: {
      POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(hwnd, &pt);
      repaint(s->app.wheel(dips(pt.x), dips(pt.y), static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA));
      return 0;
    }
    case WM_RBUTTONDOWN:
      repaint(s->app.right_click(dips(GET_X_LPARAM(lp)), dips(GET_Y_LPARAM(lp))));
      return 0;
    case WM_SETCURSOR:
      if (LOWORD(lp) == HTCLIENT) {
        const Cursor c = s->app.cursor();
        SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(c == Cursor::Hand ? 32649 : c == Cursor::IBeam ? 32513 : 32512)));
        return TRUE;
      }
      break;
    case WM_CHAR:
      repaint(s->app.char_input(static_cast<wchar_t>(wp), (GetKeyState(VK_SHIFT) & 0x8000) != 0));
      return 0;
    case WM_KEYDOWN:
      repaint(s->app.key_down(static_cast<unsigned>(wp), (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                              (GetKeyState(VK_SHIFT) & 0x8000) != 0));
      return 0;
    case WM_IME_STARTCOMPOSITION:
      place_ime(*s, hwnd);
      return 0;  // our own drawing replaces the IME's composition window
    case WM_IME_COMPOSITION: {
      HIMC imc = ImmGetContext(hwnd);
      std::wstring result, comp;
      if (imc) {
        if (lp & GCS_RESULTSTR) result = ime_string(imc, GCS_RESULTSTR);
        if (lp & GCS_COMPSTR) comp = ime_string(imc, GCS_COMPSTR);
        ImmReleaseContext(hwnd, imc);
      }
      repaint(s->app.ime_composition(comp, result));
      return 0;
    }
    case WM_IME_ENDCOMPOSITION:
      repaint(s->app.ime_composition({}, {}));
      return 0;
    case WM_TIMER:
      if (wp == smoke_timer) {
        const ULONGLONG elapsed = GetTickCount64() - s->smoke_start;
        if (!s->smoke_left && s->app.view() == View::Session && s->app.room().member_count() > 0) {
          RECT rc;
          GetClientRect(hwnd, &rc);
          const bool saved = save_png(s->app, s->smoke_path, static_cast<unsigned>(dips(rc.right)),
                                      static_cast<unsigned>(dips(rc.bottom)), static_cast<float>(s->dpi));
          std::printf("smoke: in room, screenshot %s\n", saved ? "saved" : "FAILED");
          s->smoke_left = true;
          s->client->leave();
          ExitProcess(saved ? 0 : 1);
        } else if (elapsed > 20000) {
          std::printf("smoke: timed out before entering a room\n");
          ExitProcess(1);
        }
      } else if (wp == save_timer) {
        KillTimer(hwnd, save_timer);
        s->save_timer_on = false;
        (void)s->store->save();
      } else if (wp == blink_timer) {
        repaint(s->app.blink());
      } else if (wp == frame_timer) {
        const bool changed = s->app.tick(GetTickCount64());
        repaint(changed);
        if (!s->app.animating()) {
          KillTimer(hwnd, frame_timer);
          s->frame_timer_on = false;
        }
      }
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

void demo_session(App& app, bool chat) {
  app.show_session("ABC234");
  if (chat) {
    const long long now = static_cast<long long>(std::time(nullptr)) * 1000;
    app.room().chat().add({0, "c0", "크롬이", "1f600", "안녕하세요! 처음 뵙겠습니다.", now - 400000});
    app.room().chat().add({0, "c0", "크롬이", "1f600", "같은 사람이 5분 안에 보낸 메시지는 묶여요.", now - 390000});
    app.room().chat().add({1, "c1", "하늘", "1f431", "반가워요 :)\n두 줄짜리 메시지도 잘 보이나요?", now - 120000});
  }
  app.room().set_room({{0, "크롬이", "1f600", false},
                       {1, "하늘", "1f431", false},
                       {2, "구름빵", "1f436", false},
                       {3, "졸린 곰", "1f43b", true}},
                      0, 0, false, chat);
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

// The part of startup that needs the assets in place.
void enter_app(Shell& s, HWND hwnd) {
  store::Store& data = *s.store;
  std::string nickname = data.string_or("nickname", "");
  if (nickname.empty()) {
    nickname = random_nickname(s.assets_dir / "words");
    data.set("nickname", store::Json(nickname));
  }
  s.app.home().set_nickname(nickname);
  s.config.server_url = data.string_or("serverUrl", store::default_server_url);
  s.config.nickname = nickname;
  s.config.token = data.string_or("token", "");
  show_store_notice(s);
  s.app.show_home();
  start_client(s);
  if (!s.smoke_path.empty()) {
    s.smoke_start = GetTickCount64();
    s.client->create(false, false);
    SetTimer(hwnd, smoke_timer, 200, nullptr);
  } else {
    restore_seat(s);
  }
  (void)data.save();
}

void start_install(Shell& s, HWND hwnd) {
  s.app.show_setup(0);
  s.assets_worker = std::thread([&s, hwnd] {
    assets::WindowsPlatform platform(s.asset_url);
    int shown = 0;
    const auto result = assets::install(assets::built_in_manifest(), s.assets_dir, platform,
                                        [&](std::uint64_t done, std::uint64_t total) {
                                          const int percent = static_cast<int>(done * 100 / total);
                                          if (percent == shown) return;
                                          shown = percent;
                                          s.tasks.post([&s, percent] { s.app.show_setup(percent); });
                                        });
    s.tasks.post([&s, hwnd, result] {
      s.assets_worker.join();
      if (result) {
        s.app.reload_assets(s.assets_dir);
        enter_app(s, hwnd);
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

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path data_dir;
  if (argc >= 3 && std::string(argv[argc - 2]) == "--data") {
    data_dir = widen(argv[argc - 1]);
    argc -= 2;
  }
  Shell shell;
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

  if (argc >= 3 && std::string(argv[1]) == "--shot") {
    unsigned w = 951, h = 692;
    if (argc >= 4) std::sscanf(argv[3], "%ux%u", &w, &h);
    if (argc >= 5) shell.app.home().set_hour(std::atoi(argv[4]));
    shell.app.home().set_nickname("크롬이");
    if (argc >= 6 && std::string(argv[5]) == "session") demo_session(shell.app, false);
    if (argc >= 6 && std::string(argv[5]) == "chat") demo_session(shell.app, true);
    if (argc >= 6 && std::string(argv[5]) == "setup") shell.app.show_setup(42);
    if (argc >= 6 && std::string(argv[5]) == "setup-failed") {
      shell.app.show_setup(0, "파일을 받지 못했어요. 인터넷 연결을 확인해 주세요.");
    }
    if (std::string(argv[argc - 1]) == "notice") {
      shell.app.notice().set(Notice{"기존 Cloudroom의 설정 파일을 함께 쓰고 있어요. 이 파일에는 로그인 정보가 암호화되지 않은 채 "
                                    "저장돼요.",
                                    "안전한 저장소로 옮기기", "닫기"});
    }
    return save_png(shell.app, widen(argv[2]), w, h, shot_dpi) ? 0 : 1;
  }
  if (data_dir.empty() && another_instance_running()) return 0;

  shell.store.emplace(store::Store::open(*locations, shell.platform, unix_ms()));
  store::Store& data = *shell.store;

  shell.audio = load_audio_settings(data);
  shell.app.room().set_volume(shell.audio.volume);
  shell.engine = std::make_unique<sound::Engine>(shell.audio_device, assets / "sounds");
  shell.engine->set_settings(shell.audio);
  shell.engine->start();
  shell.input = std::make_unique<input::RawInputSource>(shell.input_sink);
  shell.input->set_privacy(data.bool_or("inputDetail", false) ? input::Privacy::Detail : input::Privacy::Presence);

  shell.bridge = std::make_unique<Bridge>(shell.app, shell.tasks, data, *shell.engine);

  Shell* sp = &shell;
  shell.bridge->on_room_changed = [sp](bool in_room) {
    if (in_room) (void)sp->input->start();
    else sp->input->stop();
  };
  shell.app.room().on_mute = [sp](bool muted) {
    sp->audio.muted = muted;
    sp->engine->set_settings(sp->audio);
  };
  shell.app.room().on_volume = [sp](int percent) {
    sp->audio.volume = percent;
    sp->engine->set_settings(sp->audio);
    sp->store->set("volume", store::Json(percent));
  };
  // There is no sign-in screen yet, so continue as a guest; the token stays saved.
  shell.bridge->on_unauthorized = [sp] {
    if (sp->config.token.empty()) return;
    sp->config.token.clear();
    sp->input->stop();  // it calls into the client being replaced
    sp->client->stop();
    start_client(*sp);
    sp->app.notice().on_action = nullptr;
    sp->app.notice().set(Notice{"로그인이 만료되어 게스트로 접속해요. 다시 시도해 주세요.", "", "닫기"});
    if (!sp->smoke_path.empty()) sp->client->create(false, false);
  };
  shell.app.home().on_create = [sp] { sp->client->create(false, false); };
  shell.app.home().on_join = [sp](std::string code) {
    if (const auto seat = sp->store->find_seat(code, unix_ms())) {
      sp->bridge->expect_resume(code);
      sp->client->resume(code, seat->key, std::nullopt, true);
    } else {
      sp->client->join(code);
    }
  };
  shell.app.room().on_leave = [sp] {
    sp->bridge->leaving();
    sp->client->leave();
  };
  shell.app.room().on_set_public = [sp](bool v) { sp->client->set_visibility(v); };
  shell.app.room().on_set_chat = [sp](bool v) { sp->client->set_chat_enabled(v); };
  shell.app.room().chat().on_send = [sp](std::string text) { sp->client->send_chat(text); };

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof wc;
  wc.lpfnWndProc = wnd_proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  wc.lpszClassName = L"CrppWindow";
  RegisterClassExW(&wc);

  // The monitor's DPI is known only once the window exists, so it is sized while
  // still hidden.
  HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Cloudroom", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, wc.hInstance, &shell);
  if (!hwnd) return 1;
  shell.dpi = GetDpiForWindow(hwnd);
  const auto scaled = [&](std::int64_t dips) { return MulDiv(static_cast<int>(dips), static_cast<int>(shell.dpi), 96); };
  RECT rc{0, 0, scaled(std::clamp<std::int64_t>(data.i64_or("windowWidth", 951), 480, 16384)),
          scaled(std::clamp<std::int64_t>(data.i64_or("windowHeight", 692), 360, 16384))};
  AdjustWindowRectExForDpi(&rc, WS_OVERLAPPEDWINDOW, FALSE, 0, shell.dpi);
  SetWindowPos(hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  ShowWindow(hwnd, SW_SHOWDEFAULT);
  shell.tasks.attach(hwnd);
  shell.input_sink.attach(hwnd);
  shell.app.room().on_copy = [hwnd](std::string text) { copy_to_clipboard(hwnd, text); };
  if (argc >= 3 && std::string(argv[1]) == "--smoke") shell.smoke_path = widen(argv[2]);
  shell.app.setup().on_retry = [sp, hwnd] { start_install(*sp, hwnd); };
  if (needs_install) start_install(shell, hwnd);
  else enter_app(shell, hwnd);

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
