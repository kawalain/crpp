#pragma once
// The diagnostic log behind "로그 기록". It takes only events from this list, with no
// text of any kind, so nothing typed, no key, no chat and no Room code can reach it.
// Written only while enabled; turning it off deletes the file, as upstream does. All
// of it runs on the UI thread, on rare events, so each line opens the file itself.
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>

namespace crpp::gui {

enum class DiagEvent : std::uint8_t {
  LogStarted,
  ConnectionIdle,
  Connecting,
  Joining,
  InRoom,
  Leaving,
  WaitingToReconnect,
  FailedUnauthorized,
  FailedToConnect,
  FailedRoomGone,
  FailedServerError,
  FailedInvalidArgument,
  AssetsNetwork,
  AssetsUnavailable,
  AssetsBadContent,
  AssetsDisk,
  AssetsBadSignature,
  Count,
};

[[nodiscard]] constexpr std::string_view diag_name(DiagEvent event) {
  constexpr std::array<std::string_view, static_cast<std::size_t>(DiagEvent::Count)> names{
      "log started",      "connection idle",    "connecting",        "joining",
      "in room",          "leaving",            "waiting to reconnect", "failed: unauthorized",
      "failed: connect",  "failed: room gone",  "failed: server error", "failed: invalid argument",
      "assets: no answer", "assets: not found", "assets: bad content", "assets: disk",
      "assets: bad signature",
  };
  return names[static_cast<std::size_t>(event)];
}

class DiagLog {
 public:
  static constexpr std::uintmax_t limit_bytes = 256 * 1024;  // upstream's LOG_LIMIT_BYTES

  void set_path(std::filesystem::path file) { file_ = std::move(file); }
  [[nodiscard]] const std::filesystem::path& file() const { return file_; }
  [[nodiscard]] bool enabled() const { return enabled_; }

  void set_enabled(bool on) {
    if (on == enabled_) return;
    enabled_ = on;
    if (on) {
      note(DiagEvent::LogStarted);
    } else if (!file_.empty()) {
      std::error_code ec;
      std::filesystem::remove(file_, ec);
    }
  }

  void note(DiagEvent event) {
    if (!enabled_ || file_.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(file_.parent_path(), ec);
    std::string line = timestamp();
    line += ' ';
    line += diag_name(event);
    line += '\n';
    const auto size = std::filesystem::file_size(file_, ec);
    if (!ec && size + line.size() > limit_bytes) drop_older_half();
    std::ofstream out(file_, std::ios::binary | std::ios::app);
    out << line;
  }

 private:
  static std::string timestamp() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char text[32];
    std::strftime(text, sizeof text, "%Y-%m-%d %H:%M:%S", &local);
    return text;
  }

  void drop_older_half() {
    std::string bytes;
    {
      std::ifstream in(file_, std::ios::binary);
      bytes.assign(std::istreambuf_iterator<char>(in), {});
    }
    const std::size_t from = bytes.size() / 2;
    const std::size_t line = bytes.find('\n', from);
    std::ofstream out(file_, std::ios::binary | std::ios::trunc);
    if (line != std::string::npos) out.write(bytes.data() + line + 1, static_cast<std::streamsize>(bytes.size() - line - 1));
  }

  std::filesystem::path file_;
  bool enabled_ = false;
};

}  // namespace crpp::gui
