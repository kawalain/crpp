#pragma once
// The settings the UI reads and changes, kept in the store.
#include <functional>
#include <string>
#include <string_view>

#include "crpp/store/store.hpp"
#include "crpp/ui/prefs.hpp"

namespace crpp::gui {

class StorePrefs final : public ui::Prefs {
 public:
  explicit StorePrefs(store::Store& store) : store_(store) {}

  // After every change, on the UI thread.
  std::function<void(std::string_view key)> on_changed;

  [[nodiscard]] bool get(ui::pref::Bool key) const override { return store_.bool_or(key.name, key.fallback); }
  [[nodiscard]] int get(ui::pref::Int key) const override {
    return static_cast<int>(store_.i64_or(key.name, key.fallback));
  }
  [[nodiscard]] std::string get(ui::pref::Text key) const override { return store_.string_or(key.name, key.fallback); }
  void set(ui::pref::Bool key, bool value) override { change(key.name, store::Json(value)); }
  void set(ui::pref::Int key, int value) override { change(key.name, store::Json(static_cast<std::int64_t>(value))); }
  void set(ui::pref::Text key, std::string_view value) override { change(key.name, store::Json(std::string(value))); }

 private:
  void change(const char* name, store::Json value) {
    store_.set(name, std::move(value));
    if (on_changed) on_changed(name);
  }
  store::Store& store_;
};

}  // namespace crpp::gui
