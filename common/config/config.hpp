#pragma once

#include <switch.h>

namespace config {

// tune shuffle
auto get_shuffle() -> bool;
void set_shuffle(bool value);

// tune repeat
auto get_repeat() -> int;
void set_repeat(int value);

// tune volume
auto get_volume() -> float;
void set_volume(float value);

// per title tune enable
auto has_title_enabled(u64 tid) -> bool;
auto get_title_enabled(u64 tid) -> bool;
void set_title_enabled(u64 tid, bool value);

// default for tune for every title
auto get_title_enabled_default() -> bool;
void set_title_enabled_default(bool value);

// per title volume
auto has_title_volume(u64 tid) -> bool;
auto get_title_volume(u64 tid) -> float;
void set_title_volume(u64 tid, float value);

// default volume for every title
auto get_default_title_volume() -> float;
void set_default_title_volume(float value);

// returns the length of the string
auto get_load_path(char* out, int max_len) -> int;
void set_load_path(const char* path);

// System UI OST master switch. Regular applications are always silent.
auto get_applet_bgm_enabled() -> bool;
void set_applet_bgm_enabled(bool value);

// Legacy single-song Applet BGM config, retained for one-time migration.
auto get_applet_bgm_path(u64 tid, char* out, int max_len) -> int;
void set_applet_bgm_path(u64 tid, const char* path);

// Persistent playlist and playback settings for a UI state. StartupTitleId is
// the special random, one-shot boot/wake sound pool.
auto get_ost_playlist_size(u64 tid) -> u32;
auto get_ost_playlist_item(u64 tid, u32 index, char* out, int max_len) -> int;
auto append_ost_playlist_item(u64 tid, const char* path) -> bool;
auto remove_ost_playlist_item(u64 tid, u32 index) -> bool;
auto move_ost_playlist_item(u64 tid, u32 src, u32 dst) -> bool;
void clear_ost_playlist(u64 tid);

auto get_ost_shuffle(u64 tid) -> bool;
void set_ost_shuffle(u64 tid, bool value);
auto get_ost_repeat(u64 tid) -> int;
void set_ost_repeat(u64 tid, int value);

auto get_fade_in_ms() -> u32;
void set_fade_in_ms(u32 value);
auto get_fade_out_ms() -> u32;
void set_fade_out_ms(u32 value);

// When enabled, the Startup Sound pool also plays after system sleep.
auto get_startup_on_wake() -> bool;
void set_startup_on_wake(bool value);

// Imports settings created by the first Applet BGM build exactly once.
void migrate_ost_config();

}
