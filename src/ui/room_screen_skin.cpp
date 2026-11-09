// A Room drawn from a Skin Pack. State, hit ids and callbacks stay the screen's own.
#include <algorithm>
#include <string>

#include "crpp/skin/draw.hpp"
#include "crpp/ui/emoji_picker.hpp"
#include "crpp/ui/home_logic.hpp"
#include "crpp/ui/room_screen.hpp"
#include "crpp/ui/skin_canvas.hpp"

namespace crpp::ui {

namespace {

enum TextId : int { Contacts, MuteLabel, Status, Code, VolumeText, MemberName };
enum NumberId : int {
  Muted, OptionsOpen, MutePopupOpen, ControlsShown, HasChat, Public, PublicOff, ChatOff, PinPublic, PinChat, Volume,
  Members, MemberSlot, MemberLevel, MemberMuted, MemberOffline, MutedAll, MembersScroll, MembersOffset, MembersOverflow,
  Scale, StageX, StageY, MembersFadeTop, MembersFadeBottom, MemberNew, MemberKey, Sleepy, Cake, Stars, Weather
};
enum PictureId : int { MemberPicture, MutedPicture };
enum ReportId : int { MembersView, MembersContent };
enum NoteId : int { MembersArea, FabLeave, FabMute, FabMore, CodeNote, PublicNote, ChatNote, VolumeNote, MemberNote, MemberDisc, MembersTrack };

constexpr skin::Name text_names[] = {{"contacts", Contacts}, {"mute_label", MuteLabel}, {"status", Status}, {"code", Code}, {"volume_text", VolumeText}, {"member_name", MemberName}};
constexpr skin::Name number_names[] = {{"muted", Muted}, {"options_open", OptionsOpen}, {"mute_popup_open", MutePopupOpen}, {"controls_shown", ControlsShown}, {"chat", HasChat}, {"public", Public},
                                       {"public_off", PublicOff}, {"chat_off", ChatOff}, {"pin_public", PinPublic}, {"pin_chat", PinChat}, {"volume", Volume}, {"members", Members},
                                       {"member_slot", MemberSlot}, {"member_level", MemberLevel}, {"member_muted", MemberMuted}, {"member_offline", MemberOffline}, {"muted_all", MutedAll},
                                       {"members_scroll", MembersScroll}, {"members_offset", MembersOffset}, {"members_overflow", MembersOverflow}, {"scale", Scale}, {"stage_x", StageX}, {"stage_y", StageY},
                                       {"members_fade_top", MembersFadeTop}, {"members_fade_bottom", MembersFadeBottom}, {"member_new", MemberNew}, {"member_key", MemberKey},
                                       {"sleepy", Sleepy}, {"cake", Cake}, {"stars", Stars},
                                       {"weather", Weather}};
constexpr skin::Name picture_names[] = {{"member", MemberPicture}, {"muted", MutedPicture}};
constexpr skin::Name slot_names[] = {{"chat", 0}};
constexpr skin::Name report_names[] = {{"members_view", MembersView}, {"members_content", MembersContent}};
constexpr skin::Name note_names[] = {{"members_area", MembersArea}, {"fab_leave", FabLeave}, {"fab_mute", FabMute}, {"fab_more", FabMore}, {"code", CodeNote}, {"public", PublicNote}, {"chat", ChatNote},
                                     {"volume", VolumeNote}, {"member", MemberNote}, {"member_disc", MemberDisc}, {"members_track", MembersTrack}};
constexpr skin::Name hit_names[] = {{"leave", RoomScreen::Leave}, {"mute", RoomScreen::Mute}, {"more", RoomScreen::More}, {"opt_code", RoomScreen::OptCode}, {"opt_public", RoomScreen::OptPublic}, {"opt_chat", RoomScreen::OptChat}, {"opt_mini", RoomScreen::OptMini},
                                    {"volume", RoomScreen::VolumeSlider}, {"popup", RoomScreen::Popup}, {"members_up", RoomScreen::MembersUp}, {"members_down", RoomScreen::MembersDown}, {"members_track", RoomScreen::MembersTrack}, {"members_thumb", RoomScreen::MembersThumb}, {"member", RoomScreen::MemberBase}};

constexpr const char* muted_emoji = "1f507";

}  // namespace

skin::ScreenSchema RoomScreen::schema() {
  return {.screen = "room", .texts = text_names, .numbers = number_names, .pictures = picture_names, .hits = hit_names, .slots = slot_names, .reports = report_names, .notes = note_names};
}

void RoomScreen::paint_pack(Painter& p, const skin::Screen& screen) {
  needs_full_ = false;
  ended_area_ = {};
  member_rects_.clear();
  member_discs_.clear();
  p.set_base(D2D1::Matrix3x2F::Identity());

  class Shown final : public skin::Source {
   public:
    Shown(RoomScreen& room, Painter& painter) : s_(room), p_(painter), muted_all_(room.ui_.prefs->get(pref::mute_others)) {
      std::string names;
      for (const MemberView& m : s_.members_) {
        if (m.slot == s_.me_ && s_.members_.size() > 1) continue;
        if (!names.empty()) names += ", ";
        names += m.nick.empty() ? std::string(tr().guest) : m.nick;
      }
      contacts_ = locale::fill(tr().room_contacts, {names});
      status_ = std::string(tr().room_code) + ": " + s_.room_code_;
      volume_text_ = std::to_string(s_.volume_) + "%";
    }

    std::string_view text(int id, int index) override {
      switch (id) {
        case Contacts: return contacts_;
        case MuteLabel: return muted_all_ ? tr().unmute : tr().mute;
        case Status: return status_;
        case Code: return s_.room_code_;
        case VolumeText: return volume_text_;
        case MemberName: {
          const MemberView& m = member(index);
          return m.nick.empty() ? std::string_view(tr().guest) : std::string_view(m.nick);
        }
        default: return {};
      }
    }

    float number(int id, int index) override {
      const auto yes = [](bool b) { return b ? 1.0f : 0.0f; };
      const auto muted_here = [&](const MemberView& m) { return m.slot != s_.me_ && s_.member_volume(m.cid) == 0; };
      switch (id) {
        case Muted: return yes(muted_all_);
        case OptionsOpen: return yes(s_.options_open_);
        case MutePopupOpen: return yes(s_.mute_popup_open_);
        case ControlsShown: return yes(s_.controls_shown_);
        case HasChat: return yes(s_.chat_);
        case Public: return yes(s_.public_);
        case PublicOff: return yes(!s_.is_host());
        case ChatOff: return yes(!s_.is_host() || s_.chat_locked());
        case PinPublic: return yes(s_.ui_.prefs->get(pref::pin_public));
        case PinChat: return yes(s_.ui_.prefs->get(pref::pin_chat));
        case Volume: return static_cast<float>(s_.volume_);
        case Members: return static_cast<float>(s_.members_.size());
        case MemberSlot: return static_cast<float>(member(index).slot);
        case MemberLevel: {
          const MemberView& m = member(index);
          return !muted_here(m) && !muted_all_ ? s_.speaking_level(m.slot) : 0;
        }
        case MemberMuted: return yes(muted_here(member(index)));
        case MemberOffline: return yes(member(index).offline);
        case MutedAll: return yes(muted_all_);
        case MembersScroll: return s_.members_scroll_.drawn();
        case MembersOffset: return s_.members_scroll_.offset;
        case MembersOverflow: return yes(s_.members_scroll_.max_offset() > 0);
        case Scale: return s_.ui_.scale;
        case StageX: return s_.ui_.ox;
        case StageY: return s_.ui_.oy;
        case MembersFadeTop: return s_.members_scroll_.fade_top();
        case MembersFadeBottom: return s_.members_scroll_.fade_bottom();
        case MemberNew:
        case MemberKey: {
          const auto came = std::ranges::find(s_.came_later_, member(index).slot, [](const auto& c) { return c.first; });
          if (id == MemberNew) return yes(came != s_.came_later_.end());
          return came != s_.came_later_.end() ? static_cast<float>(came->second) : 0;
        }
        case Sleepy: return yes((s_.code_words_ & home::WordZzz) != 0);
        case Cake: return yes((s_.code_words_ & home::WordCake) != 0);
        case Stars: return yes((s_.code_words_ & home::WordStar) != 0);
        case Weather: return static_cast<float>(s_.weather_.step());  // 0 clear, 1 cloudy, 2 rain, 3 storm
        default: return 0;
      }
    }

    void picture(int id, int index, skin::Box box, float opacity, float gray) override {
      if (id != MutedPicture && !has(index)) return;
      draw_svg(p_, s_.ui_.emoji_image(id == MutedPicture ? muted_emoji : member(index).emoji), rect_of(box), opacity, gray);
    }

    void slot(int, int, skin::Box box, const skin::SlotValues&) override { s_.chat_panel_.paint(p_, rect_of(box)); }

    void report(int id, float value) override {
      if (id == MembersView) {
        s_.members_scroll_.view_h = value;
      } else {
        s_.members_scroll_.content_h = value;
        s_.members_scroll_.clamp();
        s_.members_scroll_.advance(s_.ui_);
      }
    }

    void note(int id, int index, skin::Box box) override {
      const Rect r = rect_of(box);
      switch (id) {
        case MembersArea: s_.members_area_ = r; break;
        case FabLeave: s_.fab_rects_[0] = r; break;
        case FabMute: s_.fab_rects_[1] = r; break;
        case FabMore: s_.fab_rects_[2] = r; break;
        case CodeNote: s_.code_rect_ = r; break;
        case PublicNote: s_.public_rect_ = r; break;
        case ChatNote: s_.chat_rect_ = r; break;
        case VolumeNote: s_.volume_track_ = r; break;
        case MemberNote:
          if (has(index)) s_.member_rects_.emplace_back(member(index).slot, r);
          break;
        case MemberDisc:
          if (has(index)) s_.member_discs_.emplace_back(member(index).slot, r);
          break;
        case MembersTrack: s_.members_track_ = r; break;
        default: break;
      }
    }

   private:
    // A round of the pack's repeat may count past the members: it reads as nobody.
    [[nodiscard]] bool has(int index) const { return index >= 0 && static_cast<std::size_t>(index) < s_.members_.size(); }
    [[nodiscard]] const MemberView& member(int index) const {
      static const MemberView nobody;
      return has(index) ? s_.members_[static_cast<std::size_t>(index)] : nobody;
    }

    RoomScreen& s_;
    Painter& p_;
    bool muted_all_;
    std::string contacts_, status_, volume_text_;
  } shown(*this, p);

  SkinCanvas canvas(ui_, p, this);
  canvas.in_window_coordinates();
  const Rect c = ui_.content;
  skin::draw(*ui_.pack, screen, ui_.palette, {c.x, c.y, c.w, c.h}, canvas, shown);
  paint_tips(p);
}

}  // namespace crpp::ui
