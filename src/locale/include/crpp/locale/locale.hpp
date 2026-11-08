#pragma once
// The words of the program, one table per Locale, compiled in (docs/adr/0006). To add a
// Locale, copy en.cpp and add it to all_locales() in locale.cpp; a test finds what is missing.
//
// A text may hold {0}, {1} and so on, filled by fill(). A Locale decides its own line breaks.
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace crpp::locale {

struct Strings {
  // Shared words.
  const char* guest;
  const char* ok;
  const char* cancel;
  const char* mute;
  const char* unmute;
  const char* room_word_chat;  // the Room's chat, as a heading and a switch

  // Times and counts. clock_12h fills {0} with am or pm, {1} the hour, {2} the minutes.
  const char* clock_am;
  const char* clock_pm;
  const char* clock_12h;
  const char* chat_yesterday;   // {0} the time
  const char* chat_two_days_ago;
  const char* chat_earlier;     // {0} month, {1} day, {2} the time
  const char* release_date;     // {0} year, {1} month, {2} day
  const char* stay_year;        // one, then many: {0} the number
  const char* stay_years;
  const char* stay_month;
  const char* stay_months;
  const char* stay_hour;
  const char* stay_hours;
  const char* stay_minute;
  const char* stay_minutes;
  const char* stay_under_a_minute;
  const char* times_one;        // {0} the number, with separators
  const char* times_many;

  // Chat.
  const char* chat_calm;
  const char* chat_send_failed;
  const char* chat_link_tip;
  const char* chat_placeholder;
  const char* chat_link_failed;

  // Joining and losing a Room.
  const char* join_not_found;
  const char* join_room_full;
  const char* join_already_in_room;
  const char* join_server_full;
  const char* join_no_public;
  const char* join_failed;
  const char* lost_token_in_room;
  const char* lost_token;
  const char* connect_closed;
  const char* connect_no_reply;
  const char* connect_failed;
  const char* lost_room_closed;
  const char* lost_server;
  const char* offline_message;

  // Home.
  const char* home_create;
  const char* home_morning;
  const char* home_afternoon;
  const char* home_evening;
  const char* home_night;
  const char* home_hello;  // {0} the Nickname
  const char* home_find;
  const char* home_code_placeholder;
  const char* home_find_public;
  const char* home_return_question;
  const char* home_return;
  const char* home_random;
  const char* home_code_length;

  // Members and profiles.
  const char* member_profile;
  const char* member_volume;
  const char* profile_joined;  // {0} a date
  const char* profile_stayed;  // {0} a length of time
  const char* profile_inputs;  // {0} a count
  const char* profile_change_icon;
  const char* profile_load_failed;

  // Room.
  const char* room_code;
  const char* room_public;
  const char* room_tip_leave;
  const char* room_tip_more;
  const char* room_tip_copy;
  const char* room_tip_host_only;
  const char* room_tip_unpin;
  const char* room_tip_pin;
  const char* room_tip_pin_blocked;
  const char* room_tip_chat_locked;
  const char* room_tip_host;
  const char* room_tip_offline;
  const char* room_copied;
  const char* room_copy_failed;
  const char* copy_failed;
  // The context menu's items, with & before the access key, as Windows writes them.
  const char* menu_cut;
  const char* menu_copy;
  const char* menu_paste;
  const char* menu_delete;
  const char* menu_select_all;
  const char* menu_copy_link;
  const char* room_leave_question;
  const char* room_leave_delegates;
  const char* room_leave;
  const char* room_pin_notice;
  const char* room_public_notice;
  const char* room_public_confirm;
  const char* room_chat_notice;
  const char* room_chat_confirm;

  // The prompt and accounts.
  const char* prompt_back;
  const char* account_id;
  const char* account_id_placeholder;
  const char* account_id_hint;
  const char* account_id_link;
  const char* account_create;
  const char* account_password;
  const char* account_password_placeholder;
  const char* account_password_hint;
  const char* account_password_link;
  const char* account_confirm;
  const char* account_confirm_placeholder;
  const char* account_confirm_hint;
  const char* account_confirm_link;
  const char* account_login;
  const char* account_login_hint;
  const char* account_handle_heading;
  const char* account_handle;
  const char* account_handle_placeholder;
  const char* account_handle_hint;
  const char* account_nickname_heading;
  const char* account_nickname;
  const char* account_nickname_placeholder;
  const char* account_nickname_hint;
  const char* account_nickname_random;
  const char* account_use_handle;
  const char* account_nickname_too_long;
  const char* account_wrong_password;  // {0} attempts, {1} the most allowed
  const char* account_username_taken;
  const char* account_invalid_username;
  const char* account_invalid_password;
  const char* account_weak_password;
  const char* account_handle_taken;
  const char* account_invalid_handle;
  const char* account_invalid_credentials;
  const char* account_rate_limited;
  const char* account_expired;
  const char* account_failed;
  const char* account_lock_hour;
  const char* account_lock_minutes;  // {0} minutes
  const char* account_locked;        // {0} one of the two above
  const char* account_bad_password_char;
  const char* account_password_mismatch;
  const char* account_skip_question;
  const char* account_logout_in_room;
  const char* account_logout_question;
  const char* account_logout;

  // Settings.
  const char* tab_sound;
  const char* tab_display;
  const char* tab_notify;
  const char* tab_system;
  const char* tab_account;
  const char* tip_changelog;
  const char* tip_settings;
  const char* account_heading;
  const char* account_change_nickname;
  const char* account_change_handle;

  const char* menu_file;
  const char* menu_leave;
  const char* menu_exit;
  const char* menu_tools;
  const char* menu_options;
  const char* menu_help;
  const char* menu_changelog;
  const char* options_title;
  const char* options_close;
  const char* home_enter;
  const char* home_seats;
  const char* home_seats_empty;
  const char* home_tasks;
  const char* room_contacts;
  const char* room_options;
  const char* chat_says;
  const char* chat_send;
  const char* display_theme;
  const char* display_theme_cursor;
  const char* display_skin;
  const char* display_dark_mode;
  const char* display_dark_off;
  const char* display_dark_on;
  const char* display_dark_system;
  const char* display_theme_fetch;
  const char* display_theme_needs;
  const char* display_needs_repository;
  const char* pin_question_digest;
  const char* pin_question_version;
  const char* pin_question_unversioned;
  const char* pin_anyway;
  const char* display_fetching;
  const char* display_fetch_failed;
  const char* display_fetch_retry;
  const char* fetch_reason_network;
  const char* fetch_reason_unavailable;
  const char* fetch_reason_bad;
  const char* fetch_reason_disk;
  const char* fetch_reason_unreadable;
  const char* skin_not_loaded;
  const char* system_repositories;
  const char* system_repositories_hint;
  const char* system_repository_fingerprint;
  const char* system_repository_placeholder;
  const char* system_repository_add;
  const char* system_repository_remove;
  const char* system_repository_checking;
  const char* system_repository_failed;
  const char* repository_reason_address;
  const char* repository_reason_none;
  const char* repository_reason_bad;
  const char* repository_reason_known;
  const char* repository_add_question;
  const char* repository_add_pack;
  const char* repository_add_sound;
  const char* repository_add_nothing;
  const char* repository_add_also;
  const char* repository_add_unreadable;
  const char* repository_key_changed;
  const char* repository_remove_question;
  const char* repository_in_use;
  const char* repository_needed;
  const char* repository_sound_in_use;
  const char* system_packs;
  const char* system_pack_detail;
  const char* system_pack_official;
  const char* system_pack_remove;
  const char* system_pack_in_use;
  const char* system_pack_default;
  const char* system_pack_needed;
  const char* system_pack_sound;
  const char* system_pack_held;
  const char* system_pack_update;
  const char* pin_update_question;
  const char* pack_remove_question;
  const char* sound_remove_question;
  const char* sound_fetch_failed;
  const char* display_chat_size;
  const char* display_chat_sample;

  const char* notify_heading;
  const char* notify_on_message;
  const char* notify_tray;
  const char* notify_flash;
  const char* notify_sounds;
  const char* notify_preview;
  const char* notify_sound_chat;
  const char* notify_sound_enter;

  const char* sound_speaker;
  const char* sound_devices;
  const char* sound_effects;
  const char* sound_speaker_volume;
  const char* sound_keyboard_volume;
  const char* sound_mouse_volume;
  const char* sound_pen_volume;
  const char* sound_listen_self;
  const char* sound_others_use_mine;
  const char* sound_mute_notifications;
  const char* sound_mute_on_idle;
  const char* sound_spatial;
  const char* sound_balance_enabled;
  const char* sound_balance_weak;
  const char* sound_balance_medium;
  const char* sound_balance_strong;
  const char* sound_reverb;
  const char* sound_reverb_off;
  const char* sound_reverb_office;
  const char* sound_reverb_large;
  const char* sound_default_output;
  const char* sound_current_output;
  const char* sound_me;
  const char* sound_balance;
  const char* sound_balance_off;
  const char* sound_balance_hint;
  const char* sound_output_failed;

  const char* system_heading;
  const char* system_autostart;
  const char* system_tray;
  const char* system_detail;
  const char* system_detail_hint;
  const char* system_time;
  const char* system_24h;
  const char* system_idle;
  const char* system_idle_15;
  const char* system_idle_30;
  const char* system_idle_60;
  const char* system_language;
  const char* system_language_row;
  const char* system_language_auto;
  const char* system_advanced;
  const char* system_release;
  const char* system_release_now;
  const char* system_release_10s;
  const char* system_release_1m;
  const char* system_release_off;
  const char* system_logging;
  const char* system_open_logs;
  const char* system_reset;
  const char* system_reset_question;
  const char* system_reset_confirm;
  const char* system_logs_failed;
  const char* system_autostart_failed;

  // Starting up.
  const char* setup_message;
  const char* setup_detail;  // {0} files of {1}, {2} of {3} MB
  const char* setup_failed;
  const char* setup_retry;
  const char* setup_quit;
  const char* notice_moved;
  const char* notice_erase;
  const char* notice_keep;
  const char* notice_erased;
  const char* notice_erase_failed;
  const char* notice_shared;
  const char* notice_move;
  const char* notice_close;
  const char* notice_move_failed;
  const char* notice_unreadable;
  const char* notice_corrupt;
  const char* notice_fetch_failed;
  const char* start_failed;

  // Updates.
  const char* update_installing;
  const char* update_downloading;  // {0} percent
  const char* update_failed_short;
  const char* update_retry;
  const char* update_available;
  const char* update_button;
  const char* update_failed;  // {0} the reason's name
  const char* update_latest;
  const char* changelog_title;
  const char* changelog_failed;
  const char* changelog_newer_hint;
  const char* changelog_older_hint;

  // The tray.
  const char* tray_still_connected;
  const char* tray_exit;
};

struct Locale {
  std::string_view id;    // "ko", also the key in a sound pack's names
  std::string_view name;  // in its own language, for the list of Locales
  std::string_view tag;   // for picking glyph variants, "ko-kr"
  std::string_view words_folder;  // of the random Nickname word lists, under the assets
  const Strings* strings;
};

[[nodiscard]] std::span<const Locale> all_locales() noexcept;

// The Locale with this id; null when there is none.
[[nodiscard]] const Locale* find_locale(std::string_view id) noexcept;

// The Locale for a saved choice: the saved one, or for an empty or unknown one the
// one matching the OS language ("ko-KR"), or else English.
[[nodiscard]] const Locale& choose_locale(std::string_view saved, std::string_view os_language) noexcept;

// What the program speaks now. Korean until set_current: set from the UI thread.
[[nodiscard]] const Locale& current() noexcept;
void set_current(const Locale& locale) noexcept;

[[nodiscard]] inline const Strings& tr() noexcept { return *current().strings; }

// `pattern` with {0}, {1}... replaced by `values`. An unknown placeholder stays as it is.
[[nodiscard]] std::string fill(std::string_view pattern, std::initializer_list<std::string_view> values);

// A menu's wording marks its access key with & before it, as Windows writes it ("&File", "파일(&F)"),
// and && is a plain &. `label` is the wording as shown, `access_key` that key in upper case, or 0.
[[nodiscard]] std::string label(std::string_view wording);
[[nodiscard]] char access_key(std::string_view wording) noexcept;

// A field of Strings by its name, for what refers to wording from outside the code. The index
// counts the fields in their order.
[[nodiscard]] std::optional<std::size_t> key_index(std::string_view name) noexcept;
[[nodiscard]] std::string_view key_name(std::size_t index) noexcept;
[[nodiscard]] std::string_view key_text(std::size_t index) noexcept;

// Every text of a table, for the tests.
inline constexpr std::size_t strings_count = sizeof(Strings) / sizeof(const char*);
[[nodiscard]] constexpr std::array<const char*, strings_count> strings_as_array(const Strings& strings) noexcept {
  return std::bit_cast<std::array<const char*, strings_count>>(strings);
}

// "1시간 5분", "1 hour 5 minutes"; a shorter message under a minute.
[[nodiscard]] std::string format_stay(std::int64_t seconds);
// "12,345회", "12,345 times".
[[nodiscard]] std::string format_times(std::uint64_t count);
// The time on a clock.
[[nodiscard]] std::string format_clock(int hour, int minute, bool use_24h);
// A date from "2026-11-04..." as the Locale writes it; empty if it is not one.
[[nodiscard]] std::string format_release_date(std::string_view published_at);

}  // namespace crpp::locale
