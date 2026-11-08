#include "crpp/ui/app.hpp"

#include <windows.h>

#include <fstream>
#include <utility>

#include "crpp/ui/luna.hpp"
#include "crpp/ui/skin_canvas.hpp"
#include "crpp/ui/themes.hpp"

namespace crpp::ui {

App::App() {
  ui_.prefs = &memory_prefs_;
  ui_.dialogs = &dialogs_;
  ui_.toasts = &toasts_;
  ui_.emoji_picker = &emoji_picker_;
  frame_.on_leave_room = [this] { room_.click(RoomScreen::Leave, 0, 0, false); };
  frame_.on_options = [this] {
    if (!settings_.is_open()) settings_.open(settings_.page());
    raised_pane_ = Pane::Options;
  };
  frame_.on_changelog = [this] {
    if (settings_.on_version) settings_.on_version();
    raised_pane_ = Pane::Changelog;
  };
  pane_active_.fill(true);
  pane_frame_.set_menu_shown(false);
  pane_frame_.on_close = [this] { close_pane(pane_); };
}

bool App::init(const std::filesystem::path& assets_dir, bool load_font) {
  if (!ui_.gfx.init()) return false;
  if (!assets_dir.empty() && load_font) reload_assets(assets_dir);
  else ui_.emoji.emplace(assets_dir);
  return ui_.cloud() != nullptr;
}

void App::release_graphics() {
  ui_.forget_render_target();
  if (ui_.emoji) {
    auto dir = ui_.emoji->dir();  // what emplace is about to destroy
    ui_.emoji.emplace(std::move(dir));
  }
  emoji_picker_.forget_graphics();
  ui_.gfx.release_factory();
}

void App::reload_assets(const std::filesystem::path& assets_dir) {
  ui_.gfx.load_font(assets_dir / "fonts" / "PretendardGOVVariable.ttf");
  ui_.emoji.emplace(assets_dir);
}

std::span<const skin::ScreenSchema> screen_schemas() {
  static const skin::ScreenSchema schemas[] = {HomeScreen::schema(), HomeScreen::return_schema(), PromptScreen::schema(), WindowFrame::schema(), WindowFrame::menu_schema(), RoomScreen::schema(), ChatPanel::schema(),
                                               SettingsPanel::schema(), SettingsPanel::droplet_schema(), SoundTab::schema(), DisplayTab::schema(), NotifyTab::schema(), SystemTab::schema(), AccountTab::schema(),
                                               SetupScreen::schema(), Dialogs::schema(), OfflinePopup::schema(), NoticeCard::schema(), UpdatePopup::schema(), Toasts::schema(),
                                               Toasts::bubble_schema(), HoverTip::schema(), MemberMenu::schema(), ProfileCard::schema(), EmojiPicker::schema(), Dropdown::schema(), SoundTab::tip_schema(), ChangelogView::schema()};
  return schemas;
}

namespace {

// A folder holds a pack when it has its pack.cskin.
bool holds_pack(const std::filesystem::path& dir) {
  std::error_code error;
  return std::filesystem::is_regular_file(dir / "pack.cskin", error);
}

skin::ReadFile files_of(std::filesystem::path dir) {
  return [dir = std::move(dir)](std::string_view path) -> std::optional<std::string> {
    std::error_code error;
    const std::filesystem::path file = dir / std::filesystem::path(path);
    const auto size = std::filesystem::file_size(file, error);
    if (error) return std::nullopt;
    if (size > skin::max_file_bytes) return std::string(skin::max_file_bytes + 1, ' ');  // the loader says it is too large
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    std::string text(size, '\0');
    if (!in.read(text.data(), static_cast<std::streamsize>(size))) return std::nullopt;
    return text;
  };
}

}  // namespace

skin::LoadOptions App::load_options(std::string overlay, const std::filesystem::path& folder) const {
  skin::LoadOptions options;
  options.overlay = std::move(overlay);
  options.where = narrow(folder.wstring());
  options.parent = [finder = parent_finder_](const skin::ParentQuery& query) -> std::optional<skin::FoundPack> {
    const std::filesystem::path from = widen(query.from);
    std::optional<std::filesystem::path> dir;
    if (finder) dir = finder(query.id, query.fingerprint, from);
    else if (query.fingerprint.empty()) dir = from.parent_path() / std::filesystem::path(widen(query.id));  // a folder beside it
    if (!dir || !holds_pack(*dir)) return std::nullopt;
    return skin::FoundPack{files_of(*dir), narrow(dir->wstring())};
  };
  return options;
}

std::expected<void, std::string> App::read_pack(const std::string& overlay) {
  auto pack = skin::load_pack(files_of(pack_dir_), screen_schemas(), load_options(overlay, pack_dir_));
  if (!pack) return std::unexpected(std::move(pack.error()));
  if (const auto missing = skin::missing_constant(*pack)) return std::unexpected("pack: no constant \"" + std::string(*missing) + "\"");
  ui_.forget_render_target();  // brushes are kept by the address of their stops
  previous_pack_ = std::move(pack_);  // a hit of the last frame can still point into it
  pack_ = std::move(*pack);
  ui_.pack = &*pack_;
  pack_skin_ = make_pack_skin(ui_);
  return {};
}

int App::notification_style() const {
  if (ui_.theme.from_pack) return static_cast<int>(pack_->constant("notification_sounds"));
  return ui_.theme.skin == SkinId::Luna ? 1 : 0;
}

void App::point_at_palette() {
  ui_.palette = pack_->palette_of(theme_id_).value_or(-1);
  ui_.theme.from_pack = ui_.palette >= 0 ? pack_skin_.get() : nullptr;
  ui_.motion = {};
  if (ui_.palette < 0) return;
  if (pack_->constant("moves") != 0) {
    const skin::Pack& k = *pack_;
    ui_.motion = {k.constant("scroll_settle_ms"), k.constant("scroll_return_ms"), k.constant("scroll_stretch"), k.constant("edge_fade"),
                  k.constant("screen_fade_ms"), k.constant("screen_rise"), k.constant("speaking_hold_ms"), k.constant("speaking_fade_ms")};
  }
  ui_.motion.hold_wait_ms = pack_->constant("hold_wait_ms");
  ui_.motion.hold_repeat_ms = pack_->constant("hold_repeat_ms");
  ui_.theme.dark = pack_->palette_is_dark(ui_.palette);
  if (const skin::Paint* sky = pack_->paint_named(ui_.palette, "sky")) ui_.theme.sky = sky->color;
}

bool App::find_skins(std::span<const SkinFolder> where) {
  ui_.skins.clear();
  skipped_.clear();
  for (const SkinFolder& place : where) {
    std::error_code error;
    std::vector<std::filesystem::path> folders;
    for (const auto& entry : std::filesystem::directory_iterator(place.dir, error)) {
      if (entry.is_directory(error) && holds_pack(entry.path())) folders.push_back(entry.path());
    }
    std::ranges::sort(folders);
    for (const std::filesystem::path& folder : folders) {
      // One that cannot be read is not offered, and the others still are.
      const auto pack = skin::load_pack(files_of(folder), screen_schemas(), load_options({}, folder));
      const auto lacking = pack ? skin::missing_constant(*pack) : std::nullopt;
      if (!pack || lacking) {
        const std::string name = narrow(folder.filename().wstring());
        skipped_.push_back({place.prefix.empty() ? name : place.prefix + "/" + name, pack ? "no constant " + std::string(*lacking) : pack.error()});
        continue;
      }
      SkinInfo info = skin_info(*pack, narrow(folder.wstring()));
      if (!place.prefix.empty()) info.id = place.prefix + "/" + info.id;
      ui_.skins.push_back(std::move(info));
    }
  }
  return !ui_.skins.empty();
}

void App::apply_skin_choice(bool system_dark) {
  const SkinChoice choice = choice_of(*ui_.prefs, ui_.skins);
  if (!choice.skin || !choice.theme) return;
  const std::string palette(choice.palette(system_dark));
  const std::filesystem::path dir = widen(choice.skin->dir);
  if (!pack_ || pack_dir_ != dir || pack_->overlay != choice.theme->overlay) {
    const std::filesystem::path before = pack_dir_;
    pack_dir_ = dir;
    if (!read_pack(choice.theme->overlay)) {  // changed under the app since it was found: it stays as it is
      pack_dir_ = before;
      return;
    }
  }
  set_theme(palette);
}

std::expected<void, std::string> App::load_skin_dir(const std::filesystem::path& dir) {
  std::filesystem::path whole = std::filesystem::absolute(dir).lexically_normal();
  if (!whole.has_filename()) whole = whole.parent_path();
  const std::filesystem::path before = pack_dir_;
  pack_dir_ = whole;
  auto read = read_pack({});
  // Which overlay a Theme needs is known once the pack has been read.
  if (read) {
    const skin::PackTheme* theme = pack_->theme_of_palette(theme_id_);
    if (theme && !theme->overlay.empty()) read = read_pack(theme->overlay);
  }
  if (!read) {
    pack_dir_ = before;
    return read;
  }
  point_at_palette();
  ui_.skins = {skin_info(*pack_, narrow(whole.wstring()))};
  skin_changed();
  return {};
}

void App::skin_changed() {
  Gfx::ClassicFonts fonts;
  if (ui_.theme.from_pack && pack_->fonts) {
    const auto wide = [](const std::vector<std::string>& names) {
      std::vector<std::wstring> list;
      for (const std::string& name : names) list.push_back(widen(name));
      return list;
    };
    fonts = {wide(pack_->fonts->korean), wide(pack_->fonts->other), wide(pack_->fonts->caption), pack_->fonts->korean_size, pack_->fonts->other_size};
  }
  ui_.gfx.set_classic_fonts(std::move(fonts));
  ui_.gfx.set_classic(!ui_.skin().scales_stage());
  set_animations(animations_wanted_);
  room_.chat().locale_changed();  // its rows hold their text, laid out in the other font
}

void App::set_theme(std::string_view id) {
  ui_.forget_render_target();  // brushes are kept by the address of the colours they show
  ui_.theme = theme_named(id).theme;
  theme_id_ = theme_named(id).id;
  if (pack_ && theme_named(id).id != id) {
    if (const skin::PackTheme* known = pack_->theme_of_palette(id)) {
      // Only the pack knows it. What the program still reads of a Theme comes from the table until its layouts go.
      theme_id_ = std::string(id);
      ui_.theme = theme_named(known->dark == id ? "luna_blue_dark" : "luna_blue").theme;
    }
  }
  if (pack_) {
    // Read earlier and found good, so a failure now is a pack changed under the app: it draws as if it had no such Theme.
    const skin::PackTheme* theme = pack_->theme_of_palette(theme_id_);
    if (theme && theme->overlay != pack_->overlay) (void)read_pack(theme->overlay);
    point_at_palette();
  }
  skin_changed();
}

bool App::theme_cursors() const { return ui_.theme.from_pack && pack_->cursors && ui_.prefs && ui_.prefs->get(pref::theme_cursor); }

FrameMetrics App::frame_metrics() const {
  if (!ui_.theme.from_pack || !own_frame()) return {};
  const skin::Pack& k = *pack_;
  return {k.constant("caption_h"), k.constant("frame_w"), k.constant("menu_h"), k.constant("window_radius"),
          k.constant("frame_edge"), k.constant("frame_corner"), k.constant("frame_button"), k.constant("frame_button_gap"), k.constant("frame_button_top"), k.constant("frame_button_right")};
}

float App::window_size(std::string_view name, std::string_view side, float code) const {
  if (!ui_.theme.from_pack) return code;
  return pack_->constant("window_" + std::string(name) + "_" + std::string(side));
}

void App::set_animations(bool wanted) {
  animations_wanted_ = wanted;
  ui_.animations = wanted && ui_.skin().moves();
}

ScreenSize App::screen_size() const {
  if (!own_frame()) return {};
  const auto sized = [&](std::string_view key, std::string_view name, float w, float h, float min_w, float min_h) {
    return ScreenSize{key, window_size(name, "w", w), window_size(name, "h", h), window_size(name, "min_w", min_w), window_size(name, "min_h", min_h)};
  };
  switch (view_) {
    case View::Home: return sized("Home", "home", 300, 560, 260, 400);
    case View::Session: return sized("Room", "room", 640, 500, 480, 360);
    default: return sized("Prompt", "prompt", 360, 420, 320, 360);
  }
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

void App::show_setup(const SetupScreen::Status& status) {
  const View from = view_;
  view_ = View::Setup;
  setup_.set(status);
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

std::vector<Pane> App::open_panes() const {
  std::vector<Pane> list;
  if (!own_frame()) return list;
  const bool screen = view_ != View::Setup && view_ != View::Prompt;
  if (screen && settings_.is_open()) list.push_back(Pane::Options);
  if (view_ != View::Prompt && changelog_.is_open()) list.push_back(Pane::Changelog);
  if (view_ == View::Session && room_.profile_card().is_open()) list.push_back(Pane::Profile);
  if (screen && emoji_picker_.is_open()) list.push_back(Pane::Emoji);
  if (screen && offline_.is_shown()) list.push_back(Pane::Offline);
  if (view_ != View::Setup && dialogs_.is_open()) list.push_back(Pane::Dialog);
  return list;
}

PaneInfo App::pane_info(Pane pane) const {
  const FrameMetrics frame = frame_metrics();
  const auto around = [&](D2D1_SIZE_F content) {
    return std::pair(content.width + 2 * frame.frame_w, content.height + frame.caption_h + frame.frame_w);
  };
  PaneInfo info;
  const auto fixed = [&](D2D1_SIZE_F content) {
    const auto [w, h] = around(content);
    info.w = info.min_w = w;
    info.h = info.min_h = h;
  };
  info.title = "Cloudroom++";
  switch (pane) {
    case Pane::Options:
      info.key = "Options";
      info.title = tr().options_title;
      info.w = window_size("options", "w", 580), info.h = window_size("options", "h", 480);
      info.min_w = window_size("options", "min_w", 540), info.min_h = window_size("options", "min_h", 440);
      info.resizable = true;
      break;
    case Pane::Changelog:
      info.key = "Changelog";
      info.title = tr().menu_changelog;
      info.w = window_size("changelog", "w", 560), info.h = window_size("changelog", "h", 560);
      info.min_w = window_size("changelog", "min_w", 400), info.min_h = window_size("changelog", "min_h", 300);
      info.resizable = true;
      break;
    case Pane::Profile:
      info.title = tr().member_profile;
      fixed(room_.profile_card().content_size());
      break;
    case Pane::Emoji:
      info.title = tr().profile_change_icon;
      fixed(emoji_picker_.content_size());
      break;
    case Pane::Offline:
      info.close = false;  // it goes when the server is back
      fixed(offline_.content_size());
      break;
    case Pane::Dialog:
      info.close = dialogs_.has_cancel();
      fixed(dialogs_.content_size());
      break;
    case Pane::Main:
      break;
  }
  return info;
}

PaneState App::pane_state() const {
  PaneState state;
  for (const Pane pane : open_panes()) state.open |= 1u << static_cast<unsigned>(pane);
  state.revision = dialogs_.revision() * 2 + (room_.profile_card().profile().created ? 1 : 0);
  state.raising = raised_pane_.has_value();
  return state;
}

std::optional<Pane> App::take_raised_pane() { return std::exchange(raised_pane_, std::nullopt); }

void App::use_pane(Pane pane) {
  if (pane == pane_) return;
  ui_.swap_window(windows_[static_cast<std::size_t>(pane_)]);
  ui_.swap_window(windows_[static_cast<std::size_t>(pane)]);
  pane_ = pane;
  ui_.window = static_cast<int>(pane);
}

void App::close_pane(Pane pane) {
  switch (pane) {
    case Pane::Options: settings_.close(); break;
    case Pane::Changelog: changelog_.close(); break;
    case Pane::Profile: room_.profile_card().close(); break;
    case Pane::Emoji: emoji_picker_.close(); break;
    case Pane::Dialog: (void)dialogs_.escape(); break;
    case Pane::Main:
    case Pane::Offline: break;
  }
}

void App::set_pane_active(Pane pane, bool active) { pane_active_[static_cast<std::size_t>(pane)] = active; }

bool App::window_deactivated() {
  if (pane_ == Pane::Emoji) {
    emoji_picker_.close();  // it closes on a click elsewhere
    return true;
  }
  for (Surface* s : layers()) s->before_click(nullptr, 0);
  return true;
}

void App::forget_pane(Pane pane) {
  if (pane == pane_) {
    static_cast<WindowState&>(ui_) = WindowState{};
    ui_.window = static_cast<int>(pane);
  } else {
    windows_[static_cast<std::size_t>(pane)] = WindowState{};
  }
}

std::vector<Surface*> App::all_layers() {
  if (!own_frame()) return layers();
  std::vector<Surface*> list{&dialogs_, &changelog_, &emoji_picker_, &settings_};
  const PaneScope main(*this, Pane::Main);
  const std::vector<Surface*> on_main = layers();
  list.insert(list.end(), on_main.begin(), on_main.end());
  return list;
}

std::vector<Surface*> App::layers() {
  std::vector<Surface*> list;
  if (own_frame()) {
    switch (pane_) {
      case Pane::Options: return {&pane_frame_, &toasts_, &settings_};
      case Pane::Changelog: return {&pane_frame_, &changelog_};
      case Pane::Profile: return {&pane_frame_, &room_.profile_card()};
      case Pane::Emoji: return {&pane_frame_, &emoji_picker_};
      case Pane::Offline: return {&pane_frame_, &offline_};
      case Pane::Dialog: return {&pane_frame_, &dialogs_};
      case Pane::Main: break;
    }
    list.push_back(&frame_);
    if (view_ == View::Setup) {
      list.insert(list.end(), {&update_popup_, &setup_});
    } else if (view_ == View::Prompt) {
      list.insert(list.end(), {&toasts_, &notice_, &prompt_});
    } else {
      Surface* screen = view_ == View::Home ? static_cast<Surface*>(&home_) : &room_;
      list.insert(list.end(), {&update_popup_, &toasts_, &notice_, &offline_, screen});
    }
    return list;
  }
  if (view_ == View::Setup) {
    list.insert(list.end(), {&changelog_, &update_popup_, &setup_});
  } else if (view_ == View::Prompt) {
    list.insert(list.end(), {&dialogs_, &toasts_, &notice_, &prompt_});
  } else {
    Surface* screen = view_ == View::Home ? static_cast<Surface*>(&home_) : &room_;
    list.insert(list.end(), {&dialogs_, &changelog_, &update_popup_, &toasts_, &emoji_picker_, &notice_, &settings_, &offline_, screen});
  }
  return list;
}

void App::paint_pane(Painter& p, float width, float height) {
  p.set_base(D2D1::Matrix3x2F::Identity());
  p.clear(ui_.theme.sky);
  const PaneInfo info = pane_info(pane_);
  pane_frame_.set_title(info.title);
  pane_frame_.set_parts({.resizable = info.resizable, .minimize = false, .close = info.close});
  pane_frame_.set_active(pane_active_[static_cast<std::size_t>(pane_)]);
  ui_.content = ui_.modal_area = WindowFrame::content(width, height, false, frame_metrics());
  switch (pane_) {
    case Pane::Options:
      settings_.paint(p);
      if (toasts_.shown_in() == ui_.window) toasts_.paint(p);  // the main window may be behind this one
      break;
    case Pane::Changelog: changelog_.paint(p); break;
    case Pane::Profile: room_.profile_card().paint(p); break;
    case Pane::Emoji: emoji_picker_.paint(p); break;
    case Pane::Offline: offline_.paint(p); break;
    case Pane::Dialog: dialogs_.paint(p); break;
    case Pane::Main: break;
  }
  p.set_base(D2D1::Matrix3x2F::Identity());
  pane_frame_.paint(p);
}

void App::paint(ID2D1RenderTarget* rt, float width, float height) {
  FLOAT dpi_x = 96, dpi_y = 96;
  rt->GetDpi(&dpi_x, &dpi_y);
  // Classic text is fitted to the pixels; a window may be on another monitor than the last.
  if (dpi_x / 96 != pixels_per_dip_) {
    pixels_per_dip_ = dpi_x / 96;
    ui_.gfx.set_pixels_per_dip(pixels_per_dip_);
  }
  if (pane_ == Pane::Main && pixels_per_dip_ != main_pixels_per_dip_) {
    main_pixels_per_dip_ = pixels_per_dip_;
    room_.chat().locale_changed();
  }
  ui_.begin_frame(width, height, clock_ms());
  ui_.more_frames = false;
  Painter p(ui_.gfx, rt);
  if (pane_ != Pane::Main) {
    paint_pane(p, width, height);
    return;
  }
  full_frame_ = false;
  p.set_base(D2D1::Matrix3x2F::Scale(ui_.scale, ui_.scale) * D2D1::Matrix3x2F::Translation(ui_.ox, ui_.oy));
  p.clear(ui_.theme.sky);
  if (controls_sheet_) {
    paint_controls_sheet(p);
    return;
  }
  if (cursor_sheet_) {
    paint_cursor_sheet(p);
    return;
  }
  const bool own = own_frame();
  room_.set_controls_shown(own || !settings_.is_open());
  if (own) {
    const bool menus = view_ == View::Home || view_ == View::Session;
    frame_.set_menu_shown(menus);
    frame_.set_in_room(view_ == View::Session);
    const FrameMetrics frame = frame_metrics();
    ui_.content = WindowFrame::content(width, height, menus, frame);
    ui_.modal_area = WindowFrame::below_caption(width, height, frame);
  }
  paint_screens(p);
  if (own) frame_.paint(p);
  paint_layers(p);
  if (own) frame_.paint_menu(p);
}

void App::paint_layers(Painter& p) {
  if (own_frame()) {  // the panels have windows of their own
    const bool toast_here = toasts_.shown_in() != static_cast<int>(Pane::Options) || !settings_.is_open();
    if (view_ == View::Setup) {
      update_popup_.paint(p);
    } else if (view_ == View::Prompt) {
      notice_.paint(p);
      if (toast_here) toasts_.paint(p);
    } else {
      // The screen is out of use until the server is back; the menus are not.
      if (offline_.is_shown()) ui_.add_hit_window(&offline_, ui_.content, OfflinePopup::Backdrop);
      if (view_ == View::Session) room_.paint_overlay(p);
      notice_.paint(p);
      if (toast_here) toasts_.paint(p);
      update_popup_.paint(p);
    }
    return;
  }
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
  const float t = left_at_ != 0 ? transition(ui_, left_at_, ui_.motion.screen_fade_ms) : 1;
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
  p.set_moved(0, ui_.motion.screen_rise * (1 - t));
  paint_screen(p, view_);
  p.set_moved(0, 0);
  p.pop_opacity();
}

bool App::mouse_move(float x, float y) {
  ui_.pressed_x = x;
  ui_.pressed_y = y;
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

unsigned App::hold_wait_ms() const { return ui_.pressed_repeats ? static_cast<unsigned>(ui_.motion.hold_wait_ms) : 0; }

unsigned App::hold_repeat_ms() const { return ui_.pressed_repeats ? static_cast<unsigned>(ui_.motion.hold_repeat_ms) : 0; }

bool App::hold_step() {
  if (!ui_.pressed_repeats) return false;
  // Asked anew each time, as the thumb comes to the pointer on a track and stops it there.
  const Ui::Hit* hit = ui_.hit_at(ui_.pressed_x, ui_.pressed_y);
  if (!hit || hit->owner != ui_.pressed_owner || hit->id != ui_.pressed_id) return false;
  hit->owner->click(hit->id, ui_.pressed_x, ui_.pressed_y, false);
  return true;
}

bool App::mouse_leave() {
  if (!ui_.hover_owner) return false;
  ui_.hover_owner = nullptr;
  ui_.hover_id = 0;
  return true;
}

bool App::mouse_up(float x, float y) {
  Surface* const owner = ui_.pressed_owner;
  const int id = ui_.pressed_id;
  const bool waiting = ui_.pressed_waits;
  const bool changed = mouse_cancel();
  if (!owner || !waiting) return changed;
  const Ui::Hit* hit = ui_.hit_at(x, y);
  // Only a field wants shift, and a field acts on the press.
  if (hit && hit->owner == owner && hit->id == id) owner->click(id, x, y, false);
  return true;
}

bool App::mouse_cancel() {
  const bool was_pressed = ui_.pressed_owner != nullptr;
  ui_.pressed_owner = nullptr;
  ui_.pressed_waits = ui_.pressed_repeats = false;
  if (!ui_.capture) return was_pressed;
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
  // What is under the button is what the look and the release go by, even if the last move saw something else.
  ui_.hover_owner = ui_.pressed_owner = owner;
  ui_.hover_id = ui_.pressed_id = id;
  ui_.pressed_waits = owner && !owner->acts_on_press(id);
  ui_.pressed_repeats = owner && owner->repeats(id);
  ui_.pressed_x = x;
  ui_.pressed_y = y;
  if (owner && !ui_.pressed_waits) owner->click(id, x, y, shift);
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
  if (vk == VK_RETURN && pane_ == Pane::Dialog) {  // as any window that asks a question
    dialogs_.click(Dialogs::Confirm, 0, 0, false);
    return true;
  }
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
  for (Surface* s : all_layers()) {
    if (s->animating()) return true;
  }
  return false;
}

bool App::wants_full_frame() const { return full_frame_ || room_.needs_full(); }

std::optional<Rect> App::only_moving_area() {
  if (ui_.more_frames || view_ != View::Session || wants_full_frame()) return std::nullopt;
  for (Surface* s : layers()) {
    if (s != &room_ && s->animating()) return std::nullopt;
  }
  return room_.only_moving_area();
}

bool App::tick(unsigned long long now_ms) {
  ui_.now_ms = now_ms;
  bool changed = false;
  for (Surface* s : all_layers()) {
    const bool it_changed = s->tick(now_ms);
    if (it_changed && s != &room_) full_frame_ = true;
    changed = it_changed || changed;
  }
  return changed;
}

unsigned long long App::wake_at() {
  unsigned long long at = 0;
  for (Surface* s : all_layers()) {
    const auto wake = s->wake_at();
    if (wake != 0 && (at == 0 || wake < at)) at = wake;
  }
  return at;
}

Cursor App::cursor() const {
  // A field keeps its own while dragging; a slider may be dragged far off itself.
  const bool hand_on_links_only = !ui_.skin().moves();
  if (ui_.capture && ui_.capture != ui_.hover_owner) return hand_on_links_only ? Cursor::Arrow : Cursor::Hand;
  if (!ui_.hover_owner) return Cursor::Arrow;
  const Cursor wanted = ui_.hover_owner->cursor(ui_.hover_id);
  return hand_on_links_only && wanted == Cursor::Hand && !ui_.hover_owner->is_link(ui_.hover_id) ? Cursor::Arrow : wanted;
}

}  // namespace crpp::ui
