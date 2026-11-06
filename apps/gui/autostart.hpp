#pragma once
// Starting with the system: one value under the user's Run key. RunKey is the seam;
// tests and --data runs use MemoryRunKey and never touch the registry.
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace crpp::gui {

enum class AutostartError : std::uint8_t { TooLong, Registry };

class RunKey {
 public:
  virtual ~RunKey() = default;
  // Empty if the value is not there.
  [[nodiscard]] virtual std::expected<std::optional<std::wstring>, AutostartError> read() = 0;
  [[nodiscard]] virtual std::expected<void, AutostartError> write(std::wstring_view command) = 0;
  // Succeeds if the value is not there.
  [[nodiscard]] virtual std::expected<void, AutostartError> remove() = 0;
};

class MemoryRunKey final : public RunKey {
 public:
  std::optional<std::wstring> value;
  int writes = 0;

  [[nodiscard]] std::expected<std::optional<std::wstring>, AutostartError> read() override { return value; }
  [[nodiscard]] std::expected<void, AutostartError> write(std::wstring_view command) override {
    value = std::wstring(command);
    ++writes;
    return {};
  }
  [[nodiscard]] std::expected<void, AutostartError> remove() override {
    value.reset();
    return {};
  }
};

// The command line a logon runs.
[[nodiscard]] inline std::wstring autostart_command(std::wstring_view exe) {
  return L"\"" + std::wstring(exe) + L"\" --minimized";
}

// Makes the key say what the setting says. Windows documents a limit of 260 characters
// for the command line, and a longer one would not run.
[[nodiscard]] inline std::expected<void, AutostartError> apply_autostart(RunKey& key, bool enabled, std::wstring_view exe) {
  if (!enabled) return key.remove();
  const std::wstring command = autostart_command(exe);
  if (command.size() > 260) return std::unexpected(AutostartError::TooLong);
  const auto current = key.read();
  if (!current) return std::unexpected(current.error());
  if (*current == command) return {};
  return key.write(command);
}

}  // namespace crpp::gui
