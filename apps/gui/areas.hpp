#pragma once
// The shell's side of each feature area. One area_*.cpp per area, so features under
// work at the same time do not touch the same file.
//
// wire():  connects the area's surfaces and Bridge hooks; runs once the window exists.
// scene(): sets up a named --shot scene; false if the name is not this area's.
#include <windows.h>

#include <string_view>

namespace crpp::gui {
struct Shell;
}  // namespace crpp::gui

namespace crpp::gui::area {

namespace connection {  // failures, reconnecting, the offline popup
void wire(Shell& s);
[[nodiscard]] bool scene(Shell& s, std::string_view name);
}  // namespace connection

namespace home {  // create, join, find a public Room, return to a Seat
void wire(Shell& s);
[[nodiscard]] bool scene(Shell& s, std::string_view name);
}  // namespace home

namespace room {  // the Room screen, its dialogs, profile card and member menu
void wire(Shell& s);
[[nodiscard]] bool scene(Shell& s, std::string_view name);
}  // namespace room

namespace sound {  // the sound tab and everything it changes in the engine
struct State;
void wire(Shell& s);
[[nodiscard]] bool scene(Shell& s, std::string_view name);
// A setting under this area's keys changed.
void pref_changed(Shell& s, std::string_view key);
// One member's volume and mute, from the member menu. `cid` identifies them across
// reconnects.
void set_member_volume(Shell& s, std::string_view cid, int percent);
void set_member_muted(Shell& s, std::string_view cid, bool muted);
// What the member menu shows; muted is 0.
[[nodiscard]] int member_volume(Shell& s, std::string_view cid);
// A notification sound by its file name stem ("01_cloud_chat"). Honors nothing but
// the mute settings: whether this sound is enabled is the caller's question.
void play_notification(Shell& s, std::string_view name);
// The notification tab's "미리 듣기": plays even while muted.
void preview_notification(Shell& s, std::string_view name);
// The user pressed something, which ends being away.
void own_input(Shell& s);
}  // namespace sound

namespace preferences {  // display, notification and system tabs
void wire(Shell& s);
[[nodiscard]] bool scene(Shell& s, std::string_view name);
void pref_changed(Shell& s, std::string_view key);
// Whether the notification tab has this sound on ("notificationSounds").
[[nodiscard]] bool notification_enabled(Shell& s, std::string_view name);
}  // namespace preferences

namespace os {  // tray, chat notifications, taskbar flash, autostart, reopening
void wire(Shell& s);
[[nodiscard]] bool scene(Shell& s, std::string_view name);
void pref_changed(Shell& s, std::string_view key);
// A window message before the shell handles it; true means it was consumed.
[[nodiscard]] bool on_message(Shell& s, UINT msg, WPARAM wp, LPARAM lp, LRESULT& result);
}  // namespace os

namespace account {  // HTTP API, first-run flow, the account tab
void wire(Shell& s);
[[nodiscard]] bool scene(Shell& s, std::string_view name);
// Startup, once assets are in place: true if the first-run flow took over the
// screen, and the Home screen must not be shown yet.
[[nodiscard]] bool begin_first_run(Shell& s);
}  // namespace account

namespace update {  // version check, update popup, changelog viewer
void wire(Shell& s);
[[nodiscard]] bool scene(Shell& s, std::string_view name);
}  // namespace update

}  // namespace crpp::gui::area
