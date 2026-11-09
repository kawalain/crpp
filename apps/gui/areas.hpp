#pragma once
// The shell's side of each feature area. One area_*.cpp per area, so features under
// work at the same time do not touch the same file.
//
// wire():  connects the area's surfaces and Bridge hooks; runs once the window exists.
// scene(): sets up a named --shot scene; false if the name is not this area's.
#include <windows.h>

#include <expected>
#include <functional>
#include <string>
#include <string_view>

#include "crpp/assets/assets.hpp"
#include "crpp/ui/app.hpp"

namespace crpp::gui {
struct Shell;
struct AddedRepository;
struct Candidate;
}  // namespace crpp::gui

namespace crpp::gui::area {

namespace connection {  // failures, reconnecting, the offline popup
void wire(Shell& s);
[[nodiscard]] bool scene(Shell& s, std::string_view name);
// Runs `then` once the server answers; until then the offline popup says it does not.
void check_server(Shell& s, std::function<void()> then);
// An HTTP API call failed on the network: asks, and shows the popup if the server is gone.
void server_unreachable(Shell& s);
// Asks /health how many public Rooms are open, for Home to say. One question at a time; none
// where startup's question is skipped too.
void count_public_rooms(Shell& s);
}  // namespace connection

namespace home {  // create, join, find a public Room, return to a Seat
void wire(Shell& s);
// The greeting, special days and season follow the local clock: read it again when Home shows or
// the app is come back to, as a run in the tray can last for days.
void read_clock(Shell& s);
[[nodiscard]] bool scene(Shell& s, std::string_view name);
}  // namespace home

namespace room {  // the Room screen, its dialogs, profile card and member menu
void wire(Shell& s);
// The Nickname or handle changed: chat that names them stands out by the new ones.
void names_changed(Shell& s);
[[nodiscard]] bool scene(Shell& s, std::string_view name);
// A scene that acts on what the first frame drew, as a right-click does: true if it did.
[[nodiscard]] bool after_frame(Shell& s, std::string_view name);
}  // namespace room

namespace sound {  // the sound tab and everything it changes in the engine
// Its packs are named in the language they were listed in.
void language_changed(Shell& s);
// The Sound packs of the added Repositories changed: installed, removed, offered or coming.
void packs_changed(Shell& s);
// A Sound pack picked in the sound tab has come, or could not.
void pack_fetched(Shell& s, const std::string& id, const std::expected<void, assets::Error>& result);
// The name of an added Repository's Sound pack, from its catalog.
[[nodiscard]] std::string pack_name(const Shell& s, std::string_view id);
// A Skin may have notification sounds of its own.
void theme_changed(Shell& s);
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
// Whether this member's volume is 0, which also silences their notifications.
[[nodiscard]] bool member_muted(Shell& s, std::string_view cid);
// A notification sound by its file name stem ("cloud_chat"). Honors nothing but
// the mute settings: whether this sound is enabled is the caller's question.
void play_notification(Shell& s, std::string_view name);
// The notification tab's "미리 듣기": plays even while muted.
void preview_notification(Shell& s, std::string_view name);
// The user pressed something, which ends being away.
void own_input(Shell& s);
// The mute shortcut was pressed, wherever the keyboard was.
void hotkey_pressed(Shell& s);
}  // namespace sound

namespace preferences {  // display, notification and system tabs
// Makes the saved language, or the OS's, what the program speaks.
void apply_language(Shell& s);
// Packs arrived in the Skin folder: offers them and draws what the settings choose.
void skins_installed(Shell& s);
// Logs the packs the last search could not read. With `say`, one the settings choose is also told
// of, as the app opens with another.
void note_skipped(Shell& s, bool say);
// What the latest catalog offers that is not installed, for the display tab to list.
void offer_packs(Shell& s);
// A pack picked in the display tab, on its way and when it has come, failed or was cancelled.
void pack_progress(Shell& s, const std::string& skin, int percent);
void pack_fetched(Shell& s, const std::string& skin, bool cancelled, const std::expected<void, assets::Error>& result);
void wire(Shell& s);
[[nodiscard]] bool scene(Shell& s, std::string_view name);
void pref_changed(Shell& s, std::string_view key);
// Whether the notification tab has this sound on ("notificationSounds").
[[nodiscard]] bool notification_enabled(Shell& s, std::string_view name);
// WM_SETTINGCHANGE: the OS's light or dark setting may have changed.
void system_settings_changed(Shell& s);
}  // namespace preferences

namespace repositories {  // where Skins come from: the official Repository and those the user added
// Reads those added from their folders. Before the packs are looked for.
void load(Shell& s);
// Where the packs are: the official ones, then each added Repository's.
[[nodiscard]] std::vector<ui::App::SkinFolder> skin_folders(const Shell& s);
// The packs installed from a Repository, as the system tab lists them, after the packs changed.
void show_packs(Shell& s);
// The Repository that offers this pack id, or null for the official one.
[[nodiscard]] AddedRepository* of(Shell& s, std::string_view skin);
// The Repositories as a chain of packs sees them: the official one first, with its latest catalog,
// then those added, in order.
[[nodiscard]] std::vector<assets::Shelf> shelves(const Shell& s);
// A pack's id in settings and the display tab, from its shelf and its id there.
[[nodiscard]] std::string full_id(const Shell& s, std::size_t shelf, std::string_view pack);
// The shelf and the id there of a pack's id; nothing for one of a Repository not added.
[[nodiscard]] std::optional<std::pair<std::size_t, std::string>> place(const Shell& s, std::string_view skin);
[[nodiscard]] assets::Chain chain_of(const Shell& s, std::span<const assets::Shelf> shelves, std::string_view skin);
// Whether files of that shelf are installed by this program: not the official packs beside a
// development exe.
[[nodiscard]] bool installs_into(const Shell& s, std::size_t shelf);
// Where a shelf's files are installed, and where they come from.
[[nodiscard]] std::filesystem::path install_dir(const Shell& s, std::size_t shelf);
[[nodiscard]] std::string base_of(const Shell& s, std::size_t shelf);
// The groups of a shelf the installed packs and the default one need.
[[nodiscard]] std::vector<std::string> needed_groups(const Shell& s, std::span<const assets::Shelf> shelves, std::size_t shelf);
// On a worker thread: the Repositories the packs of these catalogs name by a fingerprint not known,
// read from the address the pack gives, and what they name in turn, within a chain's depth. Those
// that could not be read go to `unreadable` by their address, or their fingerprint's first digits.
[[nodiscard]] std::vector<Candidate> read_wanted(std::vector<const assets::Catalog*> from, std::vector<std::string> known,
                                                 std::vector<std::string>* unreadable);
// Read behind the app: Repositories a pack names, which are added as that pack asks, and added ones
// whose address now signs with another key, which the user is asked about.
void found(Shell& s, std::vector<Candidate> wanted, std::vector<Candidate> rekeyed);
// The digest of a parent the user went ahead with for this pack, though its pin did not hold; empty
// for none. Asked again when the parent changes again.
[[nodiscard]] std::string accepted(const Shell& s, std::string_view child);
void accept(Shell& s, std::string_view child, const std::string& digest);
// Where the pack a pack in `from` extends is: beside it or in the official Repository without a
// fingerprint, else in the Repository of that key.
[[nodiscard]] std::optional<std::filesystem::path> parent_dir(const Shell& s, std::string_view id, std::string_view fingerprint,
                                                              const std::filesystem::path& from);
void wire(Shell& s);
[[nodiscard]] bool scene(Shell& s, std::string_view name);
}  // namespace repositories

namespace os {  // tray, chat notifications, taskbar flash, autostart, reopening
// The registered message another start of the app posts to this window.
inline constexpr const wchar_t* show_window_message = L"crpp.show-window";
void wire(Shell& s);
[[nodiscard]] bool scene(Shell& s, std::string_view name);
void pref_changed(Shell& s, std::string_view key);
// A window message before the shell handles it; true means it was consumed.
[[nodiscard]] bool on_message(Shell& s, UINT msg, WPARAM wp, LPARAM lp, LRESULT& result);
// Shows the window for the first time, or (--minimized) puts it where it can be reopened.
void first_show(Shell& s, bool minimized);
// A frame was drawn and nothing moves: the clocks to giving memory back start again.
void painted(Shell& s);
// WM_ACTIVATE of any of the app's windows: the chat learns whether the app is in front.
void activation(Shell& s, WPARAM wp, LPARAM lp);
// An Achievement was reached: the notification sound, as for a message.
void achievement_sound(Shell& s);
}  // namespace os

namespace achievements {  // the record of Achievements, its tab and its toggle
void wire(Shell& s);
[[nodiscard]] bool scene(Shell& s, std::string_view name);
void pref_changed(Shell& s, std::string_view key);
// Puts what was counted since into the store, which writes it with the settings.
void flush(Shell& s);
}  // namespace achievements

namespace account {  // HTTP API, first-run flow, the account tab
void wire(Shell& s);
[[nodiscard]] bool scene(Shell& s, std::string_view name);
// Startup, once assets are in place: true if the first-run flow took over the
// screen, and the Home screen must not be shown yet.
[[nodiscard]] bool begin_first_run(Shell& s);
// The server refused the saved token: forgets the account and asks to sign in again.
void token_rejected(Shell& s);
}  // namespace account

namespace update {  // version check, update popup, changelog viewer
void wire(Shell& s);
[[nodiscard]] bool scene(Shell& s, std::string_view name);
// In a new exe started by the old one, once it has come up: tells the old one, which
// then exits, and waits for that. Comes before the store and the single-instance check.
void took_over(unsigned long old_pid);
// Last thing before the process ends. A check or download still running would take
// long to give up, and files are only replaced whole, so this may end it here.
void shutdown(Shell& s);
}  // namespace update

}  // namespace crpp::gui::area
