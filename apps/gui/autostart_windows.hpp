#pragma once
// HKCU\Software\Microsoft\Windows\CurrentVersion\Run, value "Cloudroom".
#include "autostart.hpp"

namespace crpp::gui {

class WindowsRunKey final : public RunKey {
 public:
  [[nodiscard]] std::expected<std::optional<std::wstring>, AutostartError> read() override;
  [[nodiscard]] std::expected<void, AutostartError> write(std::wstring_view command) override;
  [[nodiscard]] std::expected<void, AutostartError> remove() override;
};

}  // namespace crpp::gui
