#include "autostart_windows.hpp"

#include <windows.h>

namespace crpp::gui {

namespace {

constexpr const wchar_t* run_key = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* value_name = L"Cloudroom";

}  // namespace

std::expected<std::optional<std::wstring>, AutostartError> WindowsRunKey::read() {
  DWORD bytes = 0;
  LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, run_key, value_name, RRF_RT_REG_SZ, nullptr, nullptr, &bytes);
  if (status == ERROR_FILE_NOT_FOUND) return std::optional<std::wstring>{};
  if (status != ERROR_SUCCESS) return std::unexpected(AutostartError::Registry);
  std::wstring text(bytes / sizeof(wchar_t), L'\0');
  status = RegGetValueW(HKEY_CURRENT_USER, run_key, value_name, RRF_RT_REG_SZ, nullptr, text.data(), &bytes);
  if (status != ERROR_SUCCESS) return std::unexpected(AutostartError::Registry);
  text.resize(bytes / sizeof(wchar_t));
  while (!text.empty() && text.back() == L'\0') text.pop_back();
  return std::optional<std::wstring>(std::move(text));
}

std::expected<void, AutostartError> WindowsRunKey::write(std::wstring_view command) {
  const std::wstring text(command);
  const auto bytes = static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t));
  if (RegSetKeyValueW(HKEY_CURRENT_USER, run_key, value_name, REG_SZ, text.c_str(), bytes) != ERROR_SUCCESS) {
    return std::unexpected(AutostartError::Registry);
  }
  return {};
}

std::expected<void, AutostartError> WindowsRunKey::remove() {
  const LSTATUS status = RegDeleteKeyValueW(HKEY_CURRENT_USER, run_key, value_name);
  if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) return std::unexpected(AutostartError::Registry);
  return {};
}

}  // namespace crpp::gui
