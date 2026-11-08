#pragma once
// The clipboard, plain text only. The system's in the app; a test gives its own.
#include <optional>
#include <string>
#include <string_view>

namespace crpp::ui {

class Clipboard {
 public:
  virtual ~Clipboard() = default;
  // False when the clipboard could not be opened or written, as when another program holds it.
  virtual bool set_text(std::wstring_view text) = 0;
  // Nothing when it holds no text or cannot be opened.
  [[nodiscard]] virtual std::optional<std::wstring> text() = 0;
  // Whether there is text to paste, without reading it.
  [[nodiscard]] virtual bool has_text() = 0;
};

// The system's clipboard as CF_UNICODETEXT.
class SystemClipboard final : public Clipboard {
 public:
  bool set_text(std::wstring_view text) override;
  [[nodiscard]] std::optional<std::wstring> text() override;
  [[nodiscard]] bool has_text() override;
};

}  // namespace crpp::ui
