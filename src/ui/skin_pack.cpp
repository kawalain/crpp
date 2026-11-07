// What surfaces ask of a Skin, answered from the parts and constants of the pack in use.
#include <initializer_list>
#include <utility>

#include "crpp/skin/pack.hpp"
#include "crpp/ui/skin_canvas.hpp"

namespace crpp::ui {

namespace {

using Args = std::initializer_list<std::pair<std::string_view, float>>;

class PackSkin final : public Skin {
 public:
  explicit PackSkin(Ui& ui) : ui_(ui), pack_(*ui.pack) {}

  [[nodiscard]] bool scales_stage() const override { return false; }
  [[nodiscard]] bool moves() const override { return false; }

  void panel(Painter& p, const Theme&, Rect r, PanelKind kind, float) const override {
    switch (kind) {
      case PanelKind::Modal: part(p, "panel_modal", r); break;
      case PanelKind::List: part(p, "panel_list", r); break;
      case PanelKind::Menu: part(p, "panel_menu", r); break;
      case PanelKind::Popup:
      case PanelKind::Picker: part(p, "panel_popup", r); break;
      case PanelKind::Update:
      case PanelKind::Notice:
      case PanelKind::Toast: part(p, "panel_info", r); break;
    }
  }

  void backdrop(Painter&, const Theme&, Rect, float) const override {}

  void button(Painter& p, const Theme&, Rect r, std::string_view label, const ButtonLook& b) const override {
    part(p, "button", r, label, b.hot, b.pressed, {{"primary", b.primary ? 1.0f : 0.0f}});
  }

  void switch_at(Painter& p, const Theme&, Rect r, bool on, float, bool disabled, bool hot) const override {
    part(p, "switch", r, {}, hot, false, {{"on", on ? 1.0f : 0.0f}, {"off", disabled ? 1.0f : 0.0f}});
  }

  void slider(Painter& p, const Theme&, Rect r, float at) const override { part(p, "slider", r, {}, false, false, {{"at", at}}); }

  void dropdown_button(Painter& p, const Theme&, Rect r, std::string_view text, bool open, bool hot, bool disabled) const override {
    part(p, "dropdown", r, text, hot, false, {{"open", open ? 1.0f : 0.0f}, {"off", disabled ? 1.0f : 0.0f}});
  }

  [[nodiscard]] ListMetrics list_metrics() const override {
    return {number("list_pad"), number("list_item_h"), number("list_gap"), number("list_max_h"), number("list_margin")};
  }

  void list_item(Painter& p, const Theme&, Rect r, std::string_view text, bool selected, bool hot) const override {
    part(p, "list_item", r, text, hot, false, {{"selected", selected ? 1.0f : 0.0f}});
  }

  void scrollbar(Painter& p, const Theme&, Rect thumb, float, float) const override { part(p, "plain_thumb", thumb); }

  [[nodiscard]] Color bubble(Painter& p, const Theme&, Rect r, float, Color, Color, float) const override {
    part(p, "bubble", r);
    const skin::Paint* ink = pack_.paint_named(ui_.palette, "tooltip_ink");
    return ink ? ink->color : Color{};
  }

  void cell(Painter& p, const Theme&, Rect r, bool selected, bool hot) const override {
    part(p, "cell", r, {}, hot, false, {{"selected", selected ? 1.0f : 0.0f}});
  }

  void menu_item(Painter& p, const Theme&, Rect r, std::string_view text, bool active, bool hot, bool disabled) const override {
    part(p, "menu_item", r, text, hot, false, {{"active", active ? 1.0f : 0.0f}, {"off", disabled ? 1.0f : 0.0f}});
  }

  void avatar(Painter& p, const Theme&, Rect r, float, bool lit, float) const override {
    part(p, "picture_frame", r, {}, false, false, {{"glow", lit ? 1.0f : 0.0f}});
  }

  void progress(Painter& p, const Theme&, Rect track, int percent) const override {
    part(p, "progress", track, {}, false, false, {{"percent", static_cast<float>(percent)}});
  }

  void close_button(Painter& p, const Theme&, Rect r, bool hot, float) const override {
    part(p, "caption_button", r, {}, hot, false, {{"close", 1.0f}});
  }

  [[nodiscard]] DialogMetrics dialog_metrics() const override {
    return {number("dialog_pad_top"),   number("dialog_pad_x"),     number("dialog_pad_bottom"),
            number("dialog_gap"),       number("dialog_button_h"),  number("dialog_button_w"),
            number("dialog_button_gap"), number("dialog_width"),    number("dialog_wide_width"),
            number("dialog_margin"),    number("dialog_text_size"), number("dialog_text_line"),
            static_cast<Align>(static_cast<int>(number("dialog_align")))};
  }

 private:
  [[nodiscard]] float number(std::string_view name) const { return pack_.constant(name); }

  void part(Painter& p, std::string_view name, Rect r, std::string_view label = {}, bool hot = false, bool pressed = false, Args args = {}) const {
    const skin::Part* const found = pack_.part(name);
    if (!found) return;
    SkinCanvas canvas(ui_, p, nullptr);
    skin::Source nothing;
    const std::span<const std::pair<std::string_view, float>> given(args.begin(), args.size());
    skin::draw_part(pack_, *found, ui_.palette, {r.x, r.y, r.w, r.h}, canvas, nothing, {.label = label, .hot = hot, .pressed = pressed, .args = given});
  }

  Ui& ui_;
  const skin::Pack& pack_;
};

}  // namespace

std::unique_ptr<Skin> make_pack_skin(Ui& ui) { return std::make_unique<PackSkin>(ui); }

}  // namespace crpp::ui
