#include "crpp/wire/client_message.hpp"

#include <algorithm>

#include "utf8.hpp"

namespace crpp::wire {

namespace {

class Writer {
 public:
  explicit Writer(std::span<char> out) noexcept : out_(out) {}

  void begin(std::string_view type) noexcept {
    raw("{\"t\":");
    str(type);
  }
  void end() noexcept { raw("}"); }

  void field(std::string_view name, std::string_view value) noexcept {
    key(name);
    str(value);
  }
  void field(std::string_view name, std::optional<std::string_view> value) noexcept {
    if (value) field(name, *value);
  }
  void field(std::string_view name, bool value) noexcept {
    key(name);
    raw(value ? "true" : "false");
  }
  void field(std::string_view name, std::uint32_t value) noexcept {
    key(name);
    char digits[10];
    std::size_t n = 0;
    do {
      digits[n++] = static_cast<char>('0' + value % 10);
      value /= 10;
    } while (value != 0);
    while (n > 0) raw(std::string_view(&digits[--n], 1));
  }

  std::expected<std::size_t, EncodeError> finish() const noexcept {
    if (failed_) return std::unexpected(error_);
    if (size_ > max_text_frame_bytes) return std::unexpected(EncodeError::TooLong);
    return size_;
  }

 private:
  void fail(EncodeError e) noexcept {
    if (!failed_) { failed_ = true; error_ = e; }
  }

  void raw(std::string_view s) noexcept {
    if (failed_) return;
    if (size_ + s.size() > out_.size()) { fail(EncodeError::BufferTooSmall); return; }
    std::copy(s.begin(), s.end(), out_.begin() + static_cast<std::ptrdiff_t>(size_));
    size_ += s.size();
  }

  void key(std::string_view name) noexcept {
    raw(",");
    str(name);
    raw(":");
  }

  void str(std::string_view s) noexcept {
    raw("\"");
    std::size_t pos = 0;
    while (pos < s.size() && !failed_) {
      const char c = s[pos];
      if (c == '"') { raw("\\\""); ++pos; }
      else if (c == '\\') { raw("\\\\"); ++pos; }
      else if (c == '\n') { raw("\\n"); ++pos; }
      else if (c == '\r') { raw("\\r"); ++pos; }
      else if (c == '\t') { raw("\\t"); ++pos; }
      else if (static_cast<unsigned char>(c) < 0x20) {
        static constexpr char hex[] = "0123456789abcdef";
        const char esc[6] = {'\\', 'u', '0', '0', hex[(c >> 4) & 0xF], hex[c & 0xF]};
        raw(std::string_view(esc, 6));
        ++pos;
      } else {
        const std::size_t n = detail::utf8_length(s, pos);
        if (n == 0) { fail(EncodeError::InvalidUtf8); return; }
        raw(s.substr(pos, n));
        pos += n;
      }
    }
    raw("\"");
  }

  std::span<char> out_;
  std::size_t size_ = 0;
  bool failed_ = false;
  EncodeError error_ = EncodeError::BufferTooSmall;
};

void write(Writer& w, const cmd::Create& m) noexcept {
  w.begin("create");
  w.field("public", m.is_public);
  w.field("chat", m.chat);
  w.field("gid", m.gid);
  w.end();
}
void write(Writer& w, const cmd::Join& m) noexcept {
  w.begin("join");
  w.field("code", m.code);
  w.field("gid", m.gid);
  w.end();
}
void write(Writer& w, const cmd::JoinRandom& m) noexcept {
  w.begin("join_random");
  w.field("gid", m.gid);
  w.end();
}
void write(Writer& w, const cmd::Resume& m) noexcept {
  w.begin("resume");
  w.field("code", m.code);
  w.field("key", m.key);
  w.field("ticket", m.ticket);
  w.field("gid", m.gid);
  w.end();
}
void write(Writer& w, const cmd::Visibility& m) noexcept {
  w.begin("visibility");
  w.field("public", m.is_public);
  w.end();
}
void write(Writer& w, const cmd::ChatEnable&) noexcept { w.begin("chat_enable"); w.end(); }
void write(Writer& w, const cmd::ChatDisable&) noexcept { w.begin("chat_disable"); w.end(); }
void write(Writer& w, const cmd::Chat& m) noexcept {
  w.begin("chat");
  w.field("text", m.text);
  w.end();
}
void write(Writer& w, const cmd::Profile& m) noexcept {
  w.begin("profile");
  w.field("slot", m.slot);
  w.end();
}
void write(Writer& w, const cmd::Nick& m) noexcept {
  w.begin("nick");
  w.field("nick", m.nick);
  w.end();
}
void write(Writer& w, const cmd::Sounds& m) noexcept {
  w.begin("sounds");
  w.field("key", m.key);
  w.field("mouse", m.mouse);
  w.field("pen", m.pen);
  w.field("emoji", m.emoji);
  w.end();
}
void write(Writer& w, const cmd::Leave&) noexcept { w.begin("leave"); w.end(); }

}  // namespace

std::expected<std::size_t, EncodeError> encode_client_message(const ClientMessage& message,
                                                              std::span<char> out) noexcept {
  Writer w(out);
  std::visit([&w](const auto& m) noexcept { write(w, m); }, message);
  return w.finish();
}

}  // namespace crpp::wire
