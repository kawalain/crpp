// crpp: the native Cloudroom window.
//   crpp                          run the app
//   crpp --shot out.png [WxH] [hour] [home|session]
//                                 render a screen offscreen and exit
#include <windows.h>
#include <windowsx.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <random>
#include <string>

#include "crpp/client/client.hpp"
#include "crpp/transport/winhttp_transport.hpp"
#include "crpp/ui/app.hpp"
#include "crpp/ui/png.hpp"

using namespace crpp;
using namespace crpp::ui;

namespace {

constexpr UINT_PTR blink_timer = 1;
constexpr UINT_PTR frame_timer = 2;
constexpr UINT_PTR smoke_timer = 3;
constexpr UINT wm_ui_task = WM_APP + 1;

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

// Turns client events into UI changes.
class Bridge final : public client::Observer {
 public:
  Bridge(App& app, UiTasks& tasks) : app_(app), tasks_(tasks) {}

  void on_status(session::State s) override {
    if (s == session::State::Idle) tasks_.post([this] { app_.show_home(); });
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
      tasks_.post([this, entry = std::move(entry)]() mutable { app_.add_chat(std::move(entry)); });
      return;
    }
    if (!room) return;
    std::vector<MemberView> members;
    for (const auto& info : room->members) {
      members.push_back({info.slot, std::string(info.nick), info.emoji ? std::string(*info.emoji) : std::string{},
                         info.offline});
    }
    const auto host = room->host;
    const bool is_public = room->is_public, chat = room->chat;
    tasks_.post([=, this, members = std::move(members), code = std::move(code)]() mutable {
      me_ = me;
      if (!code.empty()) app_.show_session(code);
      app_.set_room(std::move(members), me, host, is_public, chat);
    });
  }
  void on_peer_input(std::span<const wire::ServerEvent> events) override {
    std::vector<std::uint32_t> slots;
    for (const auto& e : events) slots.push_back(e.slot);
    tasks_.post([this, slots = std::move(slots)] {
      for (const auto s : slots) app_.pulse(s);
    });
  }
  void on_failed(session::Failure, std::string_view) override { tasks_.post([this] { app_.show_home(); }); }
  void on_persist(const session::Persistent&) override {}

 private:
  App& app_;
  UiTasks& tasks_;
  std::uint32_t me_ = 0;
};

struct Shell {
  App app;
  UiTasks tasks;
  ComPtr<ID2D1HwndRenderTarget> target;
  bool tracking = false;
  bool frame_timer_on = false;
  std::wstring smoke_path;  // --smoke: create a room, save a screenshot, leave, exit
  ULONGLONG smoke_start = 0;
  bool smoke_left = false;
  std::unique_ptr<transport::WinHttpTransport> transport;
  std::unique_ptr<Bridge> bridge;
  std::unique_ptr<client::Client> client;
};

void ensure_target(Shell& s, HWND hwnd) {
  if (s.target) return;
  RECT rc;
  GetClientRect(hwnd, &rc);
  const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(), 96, 96);
  const auto hwnd_props = D2D1::HwndRenderTargetProperties(
      hwnd, D2D1::SizeU(static_cast<UINT32>(rc.right - rc.left), static_cast<UINT32>(rc.bottom - rc.top)));
  s.app.factory()->CreateHwndRenderTarget(props, hwnd_props, s.target.put());
}

void place_ime(Shell& s, HWND hwnd) {
  HIMC imc = ImmGetContext(hwnd);
  if (!imc) return;
  const Rect c = s.app.caret_rect();
  COMPOSITIONFORM cf{};
  cf.dwStyle = CFS_POINT;
  cf.ptCurrentPos = {static_cast<LONG>(c.x), static_cast<LONG>(c.y)};
  ImmSetCompositionWindow(imc, &cf);
  CANDIDATEFORM cand{};
  cand.dwStyle = CFS_CANDIDATEPOS;
  cand.ptCurrentPos = {static_cast<LONG>(c.x), static_cast<LONG>(c.bottom())};
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
  switch (msg) {
    case WM_CREATE:
      SetTimer(hwnd, blink_timer, 530, nullptr);
      return 0;
    case WM_DESTROY:
      KillTimer(hwnd, blink_timer);
      KillTimer(hwnd, frame_timer);
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
        RECT rc;
        GetClientRect(hwnd, &rc);
        s->target->BeginDraw();
        s->app.paint(s->target.get(), static_cast<float>(rc.right), static_cast<float>(rc.bottom));
        if (s->target->EndDraw() == D2DERR_RECREATE_TARGET) s->target.reset();
      }
      EndPaint(hwnd, &ps);
      place_ime(*s, hwnd);
      return 0;
    }
    case wm_ui_task:
      s->tasks.run_all();
      InvalidateRect(hwnd, nullptr, FALSE);
      // Run the frame timer only while an effect is animating.
      if (s->app.animating() && !s->frame_timer_on) {
        SetTimer(hwnd, frame_timer, 16, nullptr);
        s->frame_timer_on = true;
      }
      return 0;
    case WM_MOUSEMOVE:
      if (!s->tracking) {
        TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&t);
        s->tracking = true;
      }
      repaint(s->app.mouse_move(static_cast<float>(GET_X_LPARAM(lp)), static_cast<float>(GET_Y_LPARAM(lp))));
      return 0;
    case WM_MOUSELEAVE:
      s->tracking = false;
      repaint(s->app.mouse_leave());
      return 0;
    case WM_LBUTTONDOWN:
      SetFocus(hwnd);
      SetCapture(hwnd);
      repaint(s->app.mouse_down(static_cast<float>(GET_X_LPARAM(lp)), static_cast<float>(GET_Y_LPARAM(lp)),
                                (wp & MK_SHIFT) != 0));
      return 0;
    case WM_LBUTTONUP:
      ReleaseCapture();
      repaint(s->app.mouse_up());
      return 0;
    case WM_MOUSEWHEEL: {
      POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(hwnd, &pt);
      repaint(s->app.wheel(static_cast<float>(pt.x), static_cast<float>(pt.y),
                           static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA));
      return 0;
    }
    case WM_RBUTTONDOWN:
      repaint(s->app.right_click(static_cast<float>(GET_X_LPARAM(lp)), static_cast<float>(GET_Y_LPARAM(lp))));
      return 0;
    case WM_SETCURSOR:
      if (LOWORD(lp) == HTCLIENT) {
        const Cursor c = s->app.cursor();
        SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(c == Cursor::Hand ? 32649 : c == Cursor::IBeam ? 32513 : 32512)));
        return TRUE;
      }
      break;
    case WM_CHAR:
      repaint(s->app.char_input(static_cast<wchar_t>(wp)));
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
        if (!s->smoke_left && s->app.view() == View::Session && s->app.member_count() > 0) {
          RECT rc;
          GetClientRect(hwnd, &rc);
          const bool saved = save_png(s->app, s->smoke_path, static_cast<unsigned>(rc.right), static_cast<unsigned>(rc.bottom));
          std::printf("smoke: in room, screenshot %s\n", saved ? "saved" : "FAILED");
          s->smoke_left = true;
          s->client->leave();
          ExitProcess(saved ? 0 : 1);
        } else if (elapsed > 20000) {
          std::printf("smoke: timed out before entering a room\n");
          ExitProcess(1);
        }
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
    app.add_chat({0, "c0", "크롬이", "1f600", "안녕하세요! 처음 뵙겠습니다.", now - 400000});
    app.add_chat({0, "c0", "크롬이", "1f600", "같은 사람이 5분 안에 보낸 메시지는 묶여요.", now - 390000});
    app.add_chat({1, "c1", "하늘", "1f431", "반가워요 :)\n두 줄짜리 메시지도 잘 보이나요?", now - 120000});
  }
  app.set_room({{0, "크롬이", "1f600", false},
                {1, "하늘", "1f431", false},
                {2, "구름빵", "1f436", false},
                {3, "졸린 곰", "1f43b", true}},
               0, 0, false, chat);
}

}  // namespace

int main(int argc, char** argv) {
  SetProcessDPIAware();
  const auto assets = exe_dir() / "assets";
  Shell shell;
  if (!shell.app.init(assets)) {
    std::fprintf(stderr, "failed to initialise the UI\n");
    return 1;
  }
  const std::string nickname = random_nickname(assets / "words");
  shell.app.set_nickname(nickname);
  std::time_t now = std::time(nullptr);
  std::tm local{};
  localtime_s(&local, &now);
  shell.app.set_hour(local.tm_hour);

  if (argc >= 3 && std::string(argv[1]) == "--shot") {
    unsigned w = 951, h = 692;
    if (argc >= 4) std::sscanf(argv[3], "%ux%u", &w, &h);
    if (argc >= 5) shell.app.set_hour(std::atoi(argv[4]));
    shell.app.set_nickname("크롬이");
    if (argc >= 6 && std::string(argv[5]) == "session") demo_session(shell.app, false);
    if (argc >= 6 && std::string(argv[5]) == "chat") demo_session(shell.app, true);
    return save_png(shell.app, widen(argv[2]), w, h) ? 0 : 1;
  }

  shell.bridge = std::make_unique<Bridge>(shell.app, shell.tasks);
  shell.transport = std::make_unique<transport::WinHttpTransport>();
  client::Config config;
  config.server_url = "https://cr.nanliburusu.com/";
  config.nickname = nickname;
  shell.client = std::make_unique<client::Client>(*shell.transport, config, *shell.bridge);

  Shell* sp = &shell;
  shell.app.on_create = [sp] { sp->client->create(false, false); };
  shell.app.on_join = [sp](std::string code) { sp->client->join(code); };
  shell.app.on_leave = [sp] { sp->client->leave(); };
  shell.app.on_set_public = [sp](bool v) { sp->client->set_visibility(v); };
  shell.app.on_set_chat = [sp](bool v) { sp->client->set_chat_enabled(v); };
  shell.app.on_send_chat = [sp](std::string text) { sp->client->send_chat(text); };

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof wc;
  wc.lpfnWndProc = wnd_proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  wc.lpszClassName = L"CrppWindow";
  RegisterClassExW(&wc);

  RECT rc{0, 0, 951, 692};
  AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
  HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Cloudroom", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT,
                              CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, wc.hInstance,
                              &shell);
  if (!hwnd) return 1;
  shell.tasks.attach(hwnd);
  shell.app.on_copy = [hwnd](std::string text) { copy_to_clipboard(hwnd, text); };
  shell.client->start();
  if (argc >= 3 && std::string(argv[1]) == "--smoke") {
    shell.smoke_path = widen(argv[2]);
    shell.smoke_start = GetTickCount64();
    shell.client->create(false, false);
    SetTimer(hwnd, smoke_timer, 200, nullptr);
  }

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  shell.client->stop();
  return 0;
}
