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

// Persistent playlist and playback settings for a UI state. The startup IDs
// are special random, one-shot sound pools for boot and wake.
auto get_ost_playlist_size(u64 tid) -> u32;
auto get_ost_playlist_item(u64 tid, u32 index, char* out, int max_len) -> int;
struct OstPlaylistConfig {
    u32 count{};
    bool shuffle{};
    int repeat{2};
};
// Loads every configured item with a single config-file pass. Each destination
// begins at out + (index * item_stride) and must hold PathSizeMax bytes.
auto load_ost_playlist(u64 tid, char* out, size_t item_stride,
                       u32 max_items) -> OstPlaylistConfig;
auto append_ost_playlist_item(u64 tid, const char* path) -> bool;
auto remove_ost_playlist_item(u64 tid, u32 index) -> bool;
auto move_ost_playlist_item(u64 tid, u32 src, u32 dst) -> bool;
void clear_ost_playlist(u64 tid);

auto get_ost_shuffle(u64 tid) -> bool;
void set_ost_shuffle(u64 tid, bool value);
auto get_ost_repeat(u64 tid) -> int;
void set_ost_repeat(u64 tid, int value);
auto get_ost_volume(u64 tid) -> float;
void set_ost_volume(u64 tid, float value);

// Multiplier applied to Album OST playback while a movie stream is active.
auto get_album_video_volume() -> float;
void set_album_video_volume(float value);

// Multiplier applied while Nintendo's native hold-HOME panel is in focus.
auto get_quick_settings_volume() -> float;
void set_quick_settings_volume(float value);

// Time between detecting an application launch and starting Loading OST.
auto get_loading_start_delay_ms() -> u32;
void set_loading_start_delay_ms(u32 value);

auto get_fade_in_ms() -> u32;
void set_fade_in_ms(u32 value);
auto get_fade_out_ms() -> u32;
void set_fade_out_ms(u32 value);
auto get_mid_song_fade_in_ms() -> u32;
void set_mid_song_fade_in_ms(u32 value);
auto get_mid_song_fade_out_ms() -> u32;
void set_mid_song_fade_out_ms(u32 value);

// When enabled, the Startup Sound pool also plays after system sleep.
auto get_startup_on_wake() -> bool;
void set_startup_on_wake(bool value);

// Selects WakeStartupTitleId for wake cues without modifying either playlist.
auto get_separate_wake_playlist() -> bool;
void set_separate_wake_playlist(bool value);

// Imports settings created by the first Applet BGM build exactly once.
void migrate_ost_config();

}
