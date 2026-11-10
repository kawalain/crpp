// The Crash report of the run before, offered at the next start.
#include <windows.h>

#include <shellapi.h>

#include "areas.hpp"
#include "crpp/crash/handler.hpp"
#include "crpp/crash/report.hpp"
#include "shell.hpp"

using crpp::locale::tr;

namespace crpp::gui::area::crash_report {

namespace {

// Where a report becomes an issue, from the crash form.
constexpr std::string_view repository_page = "https://github.com/kawalain/crpp";

// The report is the user's to post: on the clipboard, and in a new issue the browser opens filled,
// which they read before they send it. Nothing is sent from here.
void post(Shell& s, const std::string& report) {
  (void)s.app.ui().copy(ui::widen(report));
  const std::wstring url = ui::widen(crash::issue_url(repository_page, report));
  // ShellExecute's result above 32 is success.
  const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(s.hwnd, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
  s.app.toasts().toast(result > 32 ? tr().crash_copied : tr().crash_open_failed);
}

void ask(Shell& s, std::string report) {
  Shell* sp = &s;
  s.app.dialogs().show({tr().crash_question, tr().crash_send, tr().crash_close, true, [sp, report = std::move(report)] { post(*sp, report); }, nullptr});
}

}  // namespace

void offer(Shell& s) {
  const std::filesystem::path dir = s.data_dir / "crash";
  crash::collect_unreported(dir);
  if (auto report = crash::take_new_report(dir)) ask(s, std::move(*report));
}

bool scene(Shell& s, std::string_view name) {
  if (name != "crash-report") return false;
  ask(s, "crpp crash report\ncause: access violation (0xc0000005)\n");
  return true;
}

}  // namespace crpp::gui::area::crash_report
