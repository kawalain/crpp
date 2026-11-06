#include "crpp/ui/app.hpp"

#include <windows.h>

namespace crpp::ui {

App::App() {
  ui_.prefs = &memory_prefs_;
  ui_.dialogs = &dialogs_;
  ui_.toasts = &toasts_;
  ui_.emoji_picker = &emoji_picker_;
}

bool App::init(const std::filesystem::path& assets_dir, bool load_font) {
  if (!ui_.gfx.init()) return false;
  if (!assets_dir.empty() && load_font) reload_assets(assets_dir);
  else ui_.emoji.emplace(assets_dir);
  ui_.cloud = ui_.gfx.parse_path(cloud_path_data);
  return ui_.cloud && room_.init() && settings_.init();
}

void App::reload_assets(const std::filesystem::path& assets_dir) {
  ui_.gfx.load_font(assets_dir / "fonts" / "PretendardGOVVariable.ttf");
  ui_.emoji.emplace(assets_dir);
}

void App::view_left(View from) {
  if (!ui_.animations || from == view_) return;
  left_view_ = from;
  left_at_ = clock_ms();
}

void App::finish_leaving_room() {
  if (room_leaving_) room_.reset();
  room_leaving_ = false;
}

void App::show_setup(int percent) {
  const View from = view_;
  view_ = View::Setup;
  setup_.set(percent);
  view_left(from);
}

void App::show_home() {
  const View from = view_;
  if (view_ == View::Prompt) {
    prompt_return_ = View::Home;
  } else {
    view_ = View::Home;
    ui_.set_focus(nullptr, nullptr);
  }
  view_left(from);
  // The Room stays as it was while it fades out.
  if (from == View::Session && ui_.animations) room_leaving_ = true;
  else room_.reset();
}

void App::show_session(std::string room_code) {
  finish_leaving_room();
  home_.left_home();
  const View from = view_;
  if (view_ == View::Prompt) {
    prompt_return_ = View::Session;
  } else {
    view_ = View::Session;
    ui_.set_focus(nullptr, nullptr);
  }
  view_left(from);
  room_.enter_room(std::move(room_code));
}

void App::show_prompt(Prompt prompt) {
  if (view_ != View::Prompt) {
    const View from = view_;
    prompt_return_ = view_ == View::Setup ? View::Home : view_;
    view_ = View::Prompt;
    settings_.close();
    view_left(from);
  }
  prompt_.show(std::move(prompt));
}

void App::close_prompt() {
  if (view_ != View::Prompt) return;
  view_ = prompt_return_;
  ui_.set_focus(nullptr, nullptr);
  view_left(View::Prompt);
}

std::vector<Surface*> App::layers() {
  if (view_ == View::Setup) return {&changelog_, &update_popup_, &setup_};
  if (view_ == View::Prompt) return {&dialogs_, &toasts_, &notice_, &prompt_};
  Surface* screen = view_ == View::Home ? static_cast<Surface*>(&home_) : &room_;
  return {&dialogs_, &changelog_, &update_popup_, &toasts_, &emoji_picker_, &notice_, &settings_, &offline_, screen};
}

void App::paint(ID2D1RenderTarget* rt, float width, float height) {
  ui_.begin_frame(width, height, clock_ms());
  ui_.more_frames = false;
  Painter p(ui_.gfx, rt);
  p.set_base(D2D1::Matrix3x2F::Scale(ui_.scale, ui_.scale) * D2D1::Matrix3x2F::Translation(ui_.ox, ui_.oy));
  p.clear(ui_.theme.sky);
  room_.set_controls_shown(!settings_.is_open());
  paint_screens(p);
  if (view_ == View::Setup) {
    update_popup_.paint(p);
    changelog_.paint(p);
    return;
  }
  if (view_ == View::Prompt) {  // no droplet here, so no way into the settings
    notice_.paint(p);
    toasts_.paint(p);
    dialogs_.paint(p);
    return;
  }
  offline_.set_covered(settings_.is_open());
  offline_.paint(p);  // under the settings droplet, the one thing usable above it
  settings_.paint(p);
  if (view_ == View::Session) room_.paint_overlay(p);
  notice_.paint(p);
  emoji_picker_.paint(p);
  if (view_ == View::Home) home_.paint_return(p);
  toasts_.paint(p);
  update_popup_.paint(p);
  changelog_.paint(p);
  dialogs_.paint(p);
}

void App::paint_screen(Painter& p, View view) {
  p.set_base(D2D1::Matrix3x2F::Scale(ui_.scale, ui_.scale) * D2D1::Matrix3x2F::Translation(ui_.ox, ui_.oy));
  switch (view) {
    case View::Setup: setup_.paint(p); break;
    case View::Prompt: prompt_.paint(p); break;
    case View::Home: home_.paint(p); break;
    case View::Session: room_.paint(p); break;
  }
}

void App::paint_screens(Painter& p) {
  constexpr float leaving_ms = 180, rise_px = 8;
  const float t = left_at_ != 0 ? transition(ui_, left_at_, leaving_ms) : 1;
  if (t >= 1) {
    left_at_ = 0;
    finish_leaving_room();
    paint_screen(p, view_);
    return;
  }
  ui_.more_frames = true;
  ui_.hits_enabled = false;
  p.push_opacity(1 - t);
  paint_screen(p, left_view_);
  p.pop_opacity();
  ui_.hits_enabled = true;
  p.push_opacity(t);
  p.set_moved(0, rise_px * (1 - t));
  paint_screen(p, view_);
  p.set_moved(0, 0);
  p.pop_opacity();
}

bool App::mouse_move(float x, float y) {
  if (ui_.capture) {
    ui_.capture->drag(x, y);
    return true;
  }
  const Ui::Hit* hit = ui_.hit_at(x, y);
  Surface* owner = hit ? hit->owner : nullptr;
  const int id = hit ? hit->id : 0;
  if (owner == ui_.hover_owner && id == ui_.hover_id) return false;
  ui_.hover_owner = owner;
  ui_.hover_id = id;
  return true;
}

bool App::mouse_leave() {
  if (!ui_.hover_owner) return false;
  ui_.hover_owner = nullptr;
  ui_.hover_id = 0;
  return true;
}

bool App::mouse_up() {
  if (!ui_.capture) return false;
  ui_.capture->release();
  ui_.capture = nullptr;
  return true;
}

bool App::right_click(float x, float y) {
  const Ui::Hit* hit = ui_.hit_at(x, y);
  // Popups close on any button, as for a left click.
  for (Surface* s : layers()) s->before_click(hit ? hit->owner : nullptr, hit ? hit->id : 0);
  if (hit) hit->owner->right_click(hit->id, x, y);
  return true;
}

bool App::mouse_down(float x, float y, bool shift) {
  const Ui::Hit* hit = ui_.hit_at(x, y);
  Surface* owner = hit ? hit->owner : nullptr;
  const int id = hit ? hit->id : 0;
  ui_.set_focus(nullptr, nullptr);
  ui_.caret_on = true;
  for (Surface* s : layers()) s->before_click(owner, id);
  if (owner) owner->click(id, x, y, shift);
  return true;
}

bool App::wheel(float x, float y, float delta) {
  for (Surface* s : layers()) {
    if (s->wheel(x, y, delta)) return true;
  }
  return false;
}

bool App::char_input(wchar_t c, bool shift) {
  if (!ui_.focus) return false;
  if (c == L'\r') ui_.focus_owner->enter(shift);
  else ui_.focus->insert(std::wstring_view(&c, 1));
  ui_.caret_on = true;
  return true;
}

bool App::key_down(unsigned vk, bool ctrl, bool shift) {
  if (vk == VK_ESCAPE) {
    for (Surface* s : layers()) {
      if (s->escape()) return true;
    }
  }
  if (!ui_.focus) return false;
  ui_.caret_on = true;
  return ui_.focus_owner->key(vk, ctrl, shift);
}

bool App::ime_composition(std::wstring_view composition, std::wstring_view result) {
  if (!ui_.focus) return false;
  if (!result.empty()) ui_.focus->insert(result);
  ui_.focus->set_composition(composition);
  ui_.caret_on = true;
  return true;
}

bool App::blink() {
  if (!ui_.focus) return false;
  ui_.caret_on = !ui_.caret_on;
  return true;
}

bool App::animating() {
  if (ui_.more_frames) return true;
  for (Surface* s : layers()) {
    if (s->animating()) return true;
  }
  return false;
}

bool App::tick(unsigned long long now_ms) {
  ui_.now_ms = now_ms;
  bool changed = false;
  for (Surface* s : layers()) changed = s->tick(now_ms) || changed;
  return changed;
}

unsigned long long App::wake_at() {
  unsigned long long at = 0;
  for (Surface* s : layers()) {
    const auto wake = s->wake_at();
    if (wake != 0 && (at == 0 || wake < at)) at = wake;
  }
  return at;
}

Cursor App::cursor() const {
  if (ui_.capture) return Cursor::Hand;
  if (!ui_.hover_owner) return Cursor::Arrow;
  return ui_.hover_owner->cursor(ui_.hover_id);
}

}  // namespace crpp::ui
