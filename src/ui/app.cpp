#include "crpp/ui/app.hpp"

#include <windows.h>

#include <cwctype>
#include <fstream>
#include <utility>

#include "crpp/ui/achievement_words.hpp"
#include "crpp/ui/skin_canvas.hpp"

namespace crpp::ui {

App::App() {
  ui_.prefs = &memory_prefs_;
  ui_.clipboard = &system_clipboard_;
  ui_.dialogs = &dialogs_;
  ui_.toasts = &toasts_;
  ui_.emoji_picker = &emoji_picker_;
  ui_.achievements = &achievements_;
  achievements_.on_reached = [this](AchievementId id) {
    toasts_.toast(locale::fill(tr().ach_toast, {achievement_name(id)}));
    if (on_achievement) on_achievement(id);
  };
  frame_.on_leave_room = [this] { room_.click(RoomScreen::Leave, 0, 0, false); };
  frame_.on_options = [this] {
    if (!settings_.is_open()) settings_.open(settings_.page());
    raised_pane_ = Pane::Options;
  };
  frame_.on_mini = [this] { toggle_mini(); };
  room_.on_mini = [this] { toggle_mini(); };
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
  return true;
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
                                               SettingsPanel::schema(), SettingsPanel::droplet_schema(), SoundTab::schema(), DisplayTab::schema(), NotifyTab::schema(), SystemTab::schema(), AccountTab::schema(), AchievementsTab::schema(), MiniWindow::schema(),
                                               SetupScreen::schema(), Dialogs::schema(), OfflinePopup::schema(), NoticeCard::schema(), UpdatePopup::schema(), Toasts::schema(),
                                               Toasts::bubble_schema(), HoverTip::schema(), MemberMenu::schema(), ProfileCard::schema(), EmojiPicker::schema(), Dropdown::schema(), SoundTab::tip_schema(), ChangelogView::schema(), ContextMenu::schema()};
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
  // And hits hold views of their shapes in the pack that goes: no window keeps one until it is drawn again.
  ui_.forget_hits();
  for (WindowState& window : windows_) window.forget_hits();
  pack_ = std::move(*pack);
  ui_.pack = &*pack_;
  return {};
}

int App::notification_style() const { return ui_.pack ? static_cast<int>(pack_->constant("notification_sounds")) : 0; }

void App::point_at_palette() {
  // A Theme the pack does not have is drawn as its default one.
  ui_.palette = pack_->palette_of(theme_id_).or_else([&] { return pack_->palette_of(pack_->default_theme); }).value_or(0);
  ui_.motion = {};
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
  std::error_code error;
  std::filesystem::path whole = std::filesystem::absolute(dir, error).lexically_normal();
  if (error) return std::unexpected("pack: no folder " + narrow(dir.wstring()));
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
  if (pack_ && pack_->fonts) {
    const auto wide = [](const std::vector<std::string>& names) {
      std::vector<std::wstring> list;
      for (const std::string& name : names) list.push_back(widen(name));
      return list;
    };
    fonts = {wide(pack_->fonts->korean), wide(pack_->fonts->other), wide(pack_->fonts->caption), pack_->fonts->korean_size, pack_->fonts->other_size};
  }
  ui_.gfx.set_classic_fonts(std::move(fonts));
  ui_.gfx.set_classic(!ui_.scales_stage());
  set_animations(animations_wanted_);
  room_.chat().locale_changed();  // its rows hold their text, laid out in the other font
}

void App::set_theme(std::string_view id) {
  ui_.forget_render_target();  // brushes are kept by the address of the colours they show
  theme_id_ = std::string(id);
  if (pack_) {
    // Read earlier and found good, so a failure now is a pack changed under the app: it draws as if it had no such Theme.
    const skin::PackTheme* theme = pack_->theme_of_palette(theme_id_);
    if (theme && theme->overlay != pack_->overlay) (void)read_pack(theme->overlay);
    point_at_palette();
  }
  skin_changed();
}

bool App::theme_cursors() const { return pack_ && pack_->cursors && ui_.prefs && ui_.prefs->get(pref::theme_cursor); }

FrameMetrics App::frame_metrics() const {
  if (!pack_ || !own_frame()) return {};
  const skin::Pack& k = *pack_;
  return {k.constant("caption_h"), k.constant("frame_w"), k.constant("menu_h"), k.constant("window_radius"),
          k.constant("frame_edge"), k.constant("frame_corner"), k.constant("frame_button"), k.constant("frame_button_gap"), k.constant("frame_button_top"), k.constant("frame_button_right")};
}

float App::window_size(std::string_view name, std::string_view side, float code) const {
  if (!pack_) return code;
  return pack_->constant("window_" + std::string(name) + "_" + std::string(side));
}

void App::set_animations(bool wanted) {
  animations_wanted_ = wanted;
  ui_.animations = wanted && ui_.moves();
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
  // A Room can end under a prompt, as when a refused token asks to sign in; it is left all the same.
  const bool left_room = from == View::Session || (from == View::Prompt && prompt_return_ == View::Session);
  if (view_ == View::Prompt) {
    prompt_return_ = View::Home;
  } else {
    view_ = View::Home;
    ui_.set_focus(nullptr, nullptr);
    if (from != View::Home && home_.on_shown) home_.on_shown();
    if (home_.special_today()) ui_.achieve(AchievementId::SpecialDay);
  }
  if (left_room) {
    mini_.close();  // it belongs to the Room
    achievements_.leave_room(clock_ms(), local_day());
    if (on_left_room) on_left_room();
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
  if (from != View::Session) achievements_.enter_room(clock_ms());
}

void App::note_own_input(std::uint32_t events, std::uint32_t presses, std::uint32_t clicks) {
  room_.note_input(events);
  const auto today = local_day();
  achievements_.add(Tally::Presses, presses, today);
  achievements_.add(Tally::Clicks, clicks, today);
  achievements_.in_room(clock_ms(), today);
  if (presses + clicks > 0 && achievements_.reached_on(AchievementId::NightOwl) == 0 && local_hour() == 3) {
    ui_.achieve(AchievementId::NightOwl);
  }
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

void App::toggle_mini() {
  if (view_ != View::Session) return;
  mini_.toggle();
  if (mini_.is_open()) raised_pane_ = Pane::Mini;
}

bool App::mini_drags_at(float x, float y) const { return pane_ == Pane::Mini && ui_.hit_at(x, y) == nullptr; }

std::vector<Pane> App::open_panes() const {
  std::vector<Pane> list;
  if (view_ == View::Session && mini_.is_open()) list.push_back(Pane::Mini);
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
      info.title = tr().changelog_title;
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
    case Pane::Mini: {
      const MiniWindow::Shape shape = mini_.shape();
      info.title = tr().mini_title;
      info.w = info.min_w = shape.width;
      info.h = info.min_h = shape.height;
      info.radius = shape.radius;
      info.floating = true;
      break;
    }
    case Pane::Main:
      break;
  }
  // A pack measured some of these, and a window is made of them.
  for (float* side : {&info.w, &info.h, &info.min_w, &info.min_h}) {
    *side = !(*side >= 1) ? 1 : std::min(*side, skin::max_window_side);
  }
  return info;
}

PaneState App::pane_state() const {
  PaneState state;
  for (const Pane pane : open_panes()) state.open |= 1u << static_cast<unsigned>(pane);
  state.revision = dialogs_.revision() * 2 + (room_.profile_card().profile().created ? 1 : 0);
  state.raising = raised_pane_.has_value();
  if (mini_.is_open()) state.members = room_.member_count();
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
    case Pane::Mini: mini_.close(); break;
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
  alone_ = 0;
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
  std::vector<Surface*> list = layers_below_menu();
  list.insert(list.begin(), &context_menu_);  // over everything in the window it is in
  return list;
}

std::vector<Surface*> App::layers_below_menu() {
  std::vector<Surface*> list;
  if (pane_ == Pane::Mini) return {&mini_};
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
  if (pane_ == Pane::Mini) {  // its screen draws the whole window, frame and all
    ui_.content = ui_.modal_area = {0, 0, width, height};
    mini_.set_active(pane_active_[static_cast<std::size_t>(pane_)]);
    mini_.paint(p);
    return;
  }
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
    case Pane::Mini:
    case Pane::Main: break;
  }
  p.set_base(D2D1::Matrix3x2F::Identity());
  pane_frame_.paint(p);
  settle_stop();
  paint_focus(p);
  context_menu_.paint(p);
}

void App::paint_focus(Painter& p) {
  if (!ui_.stop_owner || !(ui_.cues || ui_.always_cues) || !ui_.pack) return;
  const skin::Part* part = ui_.pack->part("focus_ring");
  if (!part) return;
  for (const Ui::Hit* stop : ui_.tab_stops()) {
    if (stop != ui_.current_stop()) continue;
    // A shape's box is not its edge: a pack draws that itself, with @focused.
    if (!stop->shown || !stop->shape.empty()) return;
    // On the stage, so that it grows with what it is around.
    p.set_base(D2D1::Matrix3x2F::Scale(ui_.scale, ui_.scale) * D2D1::Matrix3x2F::Translation(ui_.ox, ui_.oy));
    const Rect r = stop->rect;
    SkinCanvas canvas(ui_, p, nullptr);
    skin::Source nothing;
    skin::draw_part(*ui_.pack, *part, ui_.palette, {ui_.to_stage_x(r.x), ui_.to_stage_y(r.y), r.w / ui_.scale, r.h / ui_.scale}, canvas, nothing,
                    {.focused = true});
    p.set_base(D2D1::Matrix3x2F::Identity());
    return;
  }
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
  settle_stop();
  paint_focus(p);
  context_menu_.paint(p);
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
  if (!hit || hit->owner->right_click(hit->id, x, y)) return true;
  // Text and fields offer what can be done with them.
  if (ui_.selection_owner && ui_.selection_owner != hit->owner) {
    ui_.selection_owner->drop_selection();
    ui_.selection_owner = nullptr;
  }
  std::vector<MenuItem> items = hit->owner->context_menu(hit->id, x, y);
  if (!items.empty()) context_menu_.open(hit->owner, std::move(items), x, y, false);
  return true;
}

bool App::context_key() {
  const Ui::Hit* const stop = ui_.focus ? nullptr : ui_.current_stop();
  // On a control the keys do what a right-click on it does, as a member's menu or a switch's pin.
  if (stop && stop->owner->right_click(stop->id, stop->whole.x + stop->whole.w / 2, stop->whole.y + stop->whole.h / 2)) return true;
  Surface* const target = ui_.focus ? ui_.focus_owner : ui_.selection_owner;
  if (!target) return false;
  // Under the caret of a field, under the keyboard's stop, or where the selection was last pressed.
  float x = last_press_x_, y = last_press_y_;
  if (ui_.focus) {
    x = ui_.caret.x;
    y = ui_.caret.bottom();
  } else if (stop && stop->owner == target) {
    x = stop->whole.x + 8;
    y = stop->whole.y + 8;
  }
  std::vector<MenuItem> items = target->context_menu(-1, x, y);
  if (items.empty()) return false;
  context_menu_.open(target, std::move(items), x, y, true);
  return true;
}

bool App::sys_key(unsigned vk, bool shift) {
  const bool menu_key = vk == VK_MENU || vk == VK_F10;
  alone_ = vk == VK_MENU || (vk == VK_F10 && !shift) ? vk : 0;
  taken_sys_char_ = 0;
  if (context_menu_.is_open()) {  // Alt or F10 leaves a menu
    context_menu_.close();
    alone_ = 0;
    return true;
  }
  if (in_frame_menus()) {
    alone_ = 0;
    if (menu_key) {
      frame_.leave_menus();
      return true;
    }
    // With Alt still down a letter is the menu's, and what it has no use for, as Alt+F4, leaves it.
    if (frame_.menu_key(vk)) {
      taken_sys_char_ = static_cast<wchar_t>(vk);
      return true;
    }
    frame_.leave_menus();
  }
  if (vk == VK_F10 && shift) return context_key();
  if (own_frame() && pane_ == Pane::Main && vk >= 'A' && vk <= 'Z' && frame_.open_by_key(static_cast<char>(vk))) {
    taken_sys_char_ = static_cast<wchar_t>(vk);
    ui_.cues = ui_.access_cues = true;
    return true;
  }
  if ((vk == VK_DOWN || vk == VK_UP) && !ui_.focus && stop_key(vk, false, shift, true)) return true;
  // Alt shows where the keyboard is, and the underlines of access keys.
  if (vk == VK_MENU && !(ui_.cues && ui_.access_cues)) {
    ui_.cues = ui_.access_cues = true;
    return true;
  }
  return false;
}

bool App::sys_key_up(unsigned vk) {
  const unsigned alone = std::exchange(alone_, 0u);
  if ((vk != VK_MENU && vk != VK_F10) || !own_frame() || pane_ != Pane::Main) return false;
  if (alone == vk) {
    frame_.enter_menus();
    ui_.cues = ui_.access_cues = true;
  }
  return true;
}

bool App::take_sys_char(wchar_t c) {
  const wchar_t taken = std::exchange(taken_sys_char_, wchar_t{0});
  return taken != 0 && static_cast<wchar_t>(std::towupper(c)) == taken;
}

bool App::move_stop(int step) {
  const std::vector<const Ui::Hit*> stops = ui_.tab_stops();
  if (stops.empty()) return false;
  auto at = std::ranges::find(stops, ui_.current_stop());
  // A field that took the keys without the keyboard, as one does when its screen opens, is where Tab goes on from.
  if (at == stops.end() && ui_.focus_owner) {
    at = std::ranges::find_if(stops, [&](const Ui::Hit* h) { return h->owner == ui_.focus_owner && h->owner->role(h->id) == Role::Field; });
  }
  const auto n = static_cast<std::ptrdiff_t>(stops.size());
  const std::ptrdiff_t from = at - stops.begin();
  const std::ptrdiff_t next = at == stops.end() ? (step > 0 ? 0 : n - 1) : ((from + step) % n + n) % n;
  go_to_stop(stops[static_cast<std::size_t>(next)]->owner, stops[static_cast<std::size_t>(next)]->id);
  ui_.cues = true;
  return true;
}

void App::go_to_stop(Surface* owner, int id) {
  ui_.set_stop(owner, id);
  ui_.set_focus(nullptr, nullptr);
  ui_.caret_on = true;
  if (!owner) return;
  owner->focus(id);
  // What a clip cut, in part or whole, is brought fully into view.
  const Ui::Hit* hit = ui_.find_stop(owner, id);
  if (!hit || (hit->shown && hit->rect.h >= hit->whole.h - 0.5f && hit->rect.w >= hit->whole.w - 0.5f)) return;
  for (Surface* s : layers()) {
    if (s->reveal(hit->whole)) return;
  }
}

void App::settle_stop() {
  const Ui::Hit* const holding = ui_.holding_hit();
  const Surface* const holder = holding ? holding->owner : nullptr;
  if (holder == ui_.keys_holder) return;
  const auto below = std::ranges::find_if(ui_.held_stops, [&](const Ui::HeldStop& h) { return h.holder == holder; });
  if (below != ui_.held_stops.end()) {
    // The layer over it closed: the keyboard goes back to the control that opened it.
    Surface* const owner = below->owner;
    const int id = below->id;
    ui_.held_stops.erase(below, ui_.held_stops.end());
    ui_.keys_holder = holder;
    ui_.set_stop(owner, id);
    if (owner && !ui_.focus && owner->role(id) == Role::Field) owner->focus(id);
    return;
  }
  // A layer opened over what had the keyboard, which waits for it to close.
  ui_.held_stops.push_back({ui_.keys_holder, ui_.stop_owner, ui_.stop_id});
  ui_.keys_holder = holder;
  const std::vector<const Ui::Hit*> stops = ui_.tab_stops();
  const std::optional<int> opening = holder ? holder->opening_stop() : std::nullopt;
  const auto named = std::ranges::find_if(stops, [&](const Ui::Hit* s) { return opening && s->owner == holder && s->id == *opening; });
  const Ui::Hit* const first = named != stops.end() ? *named : stops.empty() ? nullptr : stops.front();
  if (!first) {
    ui_.set_stop(nullptr, 0);
    return;
  }
  // A field in it takes the keys at once, as a dialog's first edit box does; anything else only the keyboard.
  ui_.set_stop(first->owner, first->id);
  if (first->owner->role(first->id) == Role::Field) first->owner->focus(first->id);
}

bool App::stop_key(unsigned vk, bool ctrl, bool shift, bool alt) {
  const Ui::Hit* stop = ui_.current_stop();
  if (!stop) return false;
  Surface* const owner = stop->owner;
  if (!owner->stop_key(stop->id, vk, ctrl, shift, alt)) return false;
  // An arrow shows where the keyboard is, as Tab does.
  ui_.cues = true;
  return true;
}

bool App::act_on_stop(unsigned vk) {
  const Ui::Hit* stop = ui_.current_stop();
  if (!stop) return false;  // gone, or under what has opened since
  const Ui::Hit& hit = *stop;
  const Role role = *hit.owner->role(hit.id);
  const bool acts = vk == VK_RETURN ? role == Role::Button || role == Role::Link || role == Role::Tab
                                    : role == Role::Button || role == Role::Toggle || role == Role::Tab || role == Role::Dropdown;
  if (!acts) return false;
  Surface* const owner = hit.owner;
  const int id = hit.id;
  const float x = hit.rect.x + hit.rect.w / 2, y = hit.rect.y + hit.rect.h / 2;
  ui_.clicks = 1;
  ui_.cues = true;
  for (Surface* s : layers()) s->before_click(owner, id);
  owner->click(id, x, y, false);
  return true;
}

bool App::mouse_down(float x, float y, bool shift) {
  const Ui::Hit* hit = ui_.hit_at(x, y);
  Surface* owner = hit ? hit->owner : nullptr;
  const int id = hit ? hit->id : 0;
  // Two presses close in time and place on the same thing are a double click, three a triple.
  const auto now = clock_ms();
  const bool again = owner == last_press_owner_ && id == last_press_id_ && now - last_press_ms_ <= ui_.double_click_ms &&
                     std::abs(x - last_press_x_) <= ui_.double_click_size && std::abs(y - last_press_y_) <= ui_.double_click_size;
  ui_.clicks = again && ui_.clicks < 3 ? ui_.clicks + 1 : 1;
  last_press_ms_ = now;
  last_press_x_ = x;
  last_press_y_ = y;
  last_press_owner_ = owner;
  last_press_id_ = id;
  alone_ = 0;
  ui_.set_focus(nullptr, nullptr);
  // The keyboard goes on from what was clicked, without showing it; a click on nothing it can stop at clears it.
  if (owner && owner->role(id)) ui_.set_stop(owner, id);
  else ui_.set_stop(nullptr, 0);
  ui_.cues = ui_.access_cues = false;
  if (ui_.selection_owner && ui_.selection_owner != owner) {
    ui_.selection_owner->drop_selection();
    ui_.selection_owner = nullptr;
  }
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
  if (context_menu_.is_open()) return true;  // an access key, not typing
  // The character of a key a menu, a question or a stop took is theirs too, whatever has the keys now.
  const wchar_t taken = std::exchange(taken_char_, wchar_t{0});
  if (taken == any_char || (taken != 0 && taken == c)) return true;
  if (!ui_.focus) return false;
  if (c == L'\r') ui_.focus_owner->enter(shift);
  else ui_.focus->insert(std::wstring_view(&c, 1));
  ui_.caret_on = true;
  return true;
}

void App::found_hidden_themes() {
  ui_.achieve(AchievementId::Konami);
  if (ui_.prefs->get(pref::hidden_themes)) return;
  ui_.prefs->set(pref::hidden_themes, true);
  toasts_.toast(tr().hidden_themes_found);
}

bool App::key_down(unsigned vk, bool ctrl, bool shift) {
  taken_char_ = 0;
  alone_ = 0;
  // Only watched: every key still goes where it would.
  if (!ctrl && konami_.key(vk)) found_hidden_themes();
  if (context_menu_.is_open()) {
    taken_char_ = any_char;
    return context_menu_.key(vk, ctrl, shift);
  }
  if (in_frame_menus()) {  // every key, as the menus of Windows take them
    taken_char_ = any_char;
    frame_.menu_key(vk);
    return true;
  }
  if (vk == VK_APPS) return context_key();
  if (vk == VK_TAB && !ctrl) {
    taken_char_ = L'\t';
    return move_stop(shift ? -1 : 1);
  }
  for (Surface* s : layers()) {
    if (s->layer_key(vk, ctrl, shift)) return true;
  }
  if (!ui_.focus && stop_key(vk, ctrl, shift, false)) {
    if (vk == VK_RETURN || vk == VK_SPACE) taken_char_ = vk == VK_RETURN ? L'\r' : L' ';
    return true;
  }
  if ((vk == VK_RETURN || vk == VK_SPACE) && !ctrl && !ui_.focus && act_on_stop(vk)) {
    taken_char_ = vk == VK_RETURN ? L'\r' : L' ';
    return true;
  }
  if (vk == VK_RETURN && dialogs_.is_open()) {  // as any window that asks a question
    taken_char_ = L'\r';
    dialogs_.click(Dialogs::Confirm, 0, 0, false);
    return true;
  }
  if (vk == VK_ESCAPE) {
    for (Surface* s : layers()) {
      if (s->escape()) return true;
    }
  }
  // As in a Windows message box, Ctrl+C copies the whole question.
  if (dialogs_.is_open() && !ui_.focus && ctrl && !shift && (vk == 'C' || vk == VK_INSERT)) {
    ui_.copy(widen(dialogs_.text()));
    return true;
  }
  if (!ui_.focus) return ui_.selection_owner && ui_.selection_owner->key(vk, ctrl, shift);
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
  const bool hand_on_links_only = !ui_.moves();
  if (ui_.capture && ui_.capture != ui_.hover_owner) return hand_on_links_only ? Cursor::Arrow : Cursor::Hand;
  if (!ui_.hover_owner) return Cursor::Arrow;
  const Cursor wanted = ui_.hover_owner->cursor(ui_.hover_id);
  return hand_on_links_only && wanted == Cursor::Hand && !ui_.hover_owner->is_link(ui_.hover_id) ? Cursor::Arrow : wanted;
}

}  // namespace crpp::ui
