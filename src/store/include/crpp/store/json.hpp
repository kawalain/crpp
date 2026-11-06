#pragma once
// A JSON document that survives a round trip through this client: keys keep
// their order, numbers keep their exact text, and values this client does not
// understand stay in place. Meant for small files read and written rarely, so
// unlike the wire parser it allocates freely.
#include <concepts>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace crpp::store {

enum class JsonError : std::uint8_t { Syntax, TooDeep, BadUtf8 };

class Json {
 public:
  struct Member;
  using Array = std::vector<Json>;
  using Object = std::vector<Member>;

  Json() = default;
  template <std::integral T>
  explicit Json(T value) {
    if constexpr (std::is_same_v<T, bool>) value_ = value;
    else value_ = Number{std::to_string(value)};
  }
  explicit Json(std::string value) : value_(std::move(value)) {}
  explicit Json(std::string_view value) : value_(std::string(value)) {}
  explicit Json(const char* value) : value_(std::string(value)) {}
  explicit Json(Array value);
  explicit Json(Object value);
  [[nodiscard]] static Json object();
  [[nodiscard]] static Json array();

  [[nodiscard]] static std::expected<Json, JsonError> parse(std::string_view text);
  [[nodiscard]] std::string dump(bool pretty = false) const;

  [[nodiscard]] bool is_null() const noexcept { return std::holds_alternative<std::monostate>(value_); }
  [[nodiscard]] bool is_object() const noexcept { return std::holds_alternative<Object>(value_); }

  // Each is empty when the value has another type. as_i64 is also empty for a
  // number with a fraction or outside the range.
  [[nodiscard]] std::optional<bool> as_bool() const noexcept;
  [[nodiscard]] std::optional<std::int64_t> as_i64() const noexcept;
  [[nodiscard]] std::optional<std::string_view> as_string() const noexcept;
  [[nodiscard]] const Array* as_array() const noexcept;
  [[nodiscard]] Array* as_array() noexcept;
  [[nodiscard]] const Object* as_object() const noexcept;
  // The exact source text of a number; empty for every other type.
  [[nodiscard]] std::string_view number_text() const noexcept {
    const auto* v = std::get_if<Number>(&value_);
    return v ? std::string_view(v->text) : std::string_view{};
  }

  // --- object members; a non-object has none, and set() turns it into one ---
  [[nodiscard]] const Json* find(std::string_view key) const noexcept;
  [[nodiscard]] Json* find(std::string_view key) noexcept;
  void set(std::string_view key, Json value);
  bool erase(std::string_view key);
  [[nodiscard]] std::optional<Json> take(std::string_view key);

  friend bool operator==(const Json&, const Json&);

 private:
  struct Number {
    std::string text;
    bool operator==(const Number&) const = default;
  };
  friend class JsonParser;
  std::variant<std::monostate, bool, Number, std::string, Array, Object> value_;
};

struct Json::Member {
  std::string key;
  Json value;
  bool operator==(const Member&) const = default;
};

}  // namespace crpp::store
