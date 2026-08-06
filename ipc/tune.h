#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <switch.h>

typedef enum {
    TuneShuffleMode_Off,
    TuneShuffleMode_On,

    TuneShuffleMode_Count,
} TuneShuffleMode;

typedef enum {
    TuneRepeatMode_Off,
    TuneRepeatMode_One,
    TuneRepeatMode_All,

    TuneRepeatMode_Count,
} TuneRepeatMode;

typedef enum {
    TuneEnqueueType_Front,
    TuneEnqueueType_Back,

    TuneEnqueueType_Count,
} TuneEnqueueType;

typedef struct {
    u32 sample_rate;
    u32 current_frame;
    u32 total_frames;
} TuneCurrentStats;

enum {
    TuneQlaunchSceneHistorySize = 8,
};

typedef enum {
    TuneQlaunchObserverStatus_Unavailable,
    TuneQlaunchObserverStatus_Starting,
    TuneQlaunchObserverStatus_Installed,
    TuneQlaunchObserverStatus_QlaunchConnected,
    TuneQlaunchObserverStatus_ReceivingScenes,
    TuneQlaunchObserverStatus_Failed,
} TuneQlaunchObserverStatus;

typedef struct {
    u64 tick;
    u8 scene;
    u8 reserved[7];
} TuneQlaunchSceneEvent;

typedef struct {
    u32 status;
    u32 last_result;
    u32 query_count;
    u32 request_count;
    u32 context_command_count;
    u32 scene_update_count;
    u32 last_command_id;
    u32 reserved0;
    u64 qlaunch_process_id;
    u8 current_scene;
    u8 has_scene;
    u8 history_count;
    u8 has_last_command;
    u8 reserved[4];
    TuneQlaunchSceneEvent history[TuneQlaunchSceneHistorySize];
} TuneQlaunchSceneObserverInfo;

typedef enum {
    TuneAlbumVideoObserverStatus_Unavailable,
    TuneAlbumVideoObserverStatus_Starting,
    TuneAlbumVideoObserverStatus_Installed,
    TuneAlbumVideoObserverStatus_AlbumConnected,
    TuneAlbumVideoObserverStatus_ReceivingMovieData,
    TuneAlbumVideoObserverStatus_Failed,
} TuneAlbumVideoObserverStatus;

typedef struct {
    u32 status;
    u32 last_result;
    u32 query_count;
    u32 request_count;
    u32 movie_open_count;
    u32 movie_read_count;
    u32 movie_close_count;
    u32 state_update_count;
    u32 last_command_id;
    u32 active_stream_count;
    u64 album_process_id;
    u8 video_active;
    u8 has_signal;
    u8 has_last_command;
    u8 reserved[5];
} TuneAlbumVideoObserverInfo;

typedef enum {
    TunePowerObserverStatus_Unavailable,
    TunePowerObserverStatus_Starting,
    TunePowerObserverStatus_Active,
    TunePowerObserverStatus_Failed,
} TunePowerObserverStatus;

typedef enum {
    TunePowerTransition_None,
    TunePowerTransition_Sleep,
    TunePowerTransition_Wake,
} TunePowerTransition;

typedef struct {
    u32 status;
    u32 last_result;
    u32 module_id;
    u32 current_state;
    u32 current_flags;
    u32 request_count;
    u32 transition_count;
    u32 sleep_count;
    u32 wake_count;
    u32 audio_quiesce_timeout_count;
    u64 last_state_tick;
    u8 has_state;
    u8 last_transition;
    u8 lock_screen_enabled;
    u8 audio_hold_active;
    u8 reserved[4];
} TunePowerStateObserverInfo;

typedef enum {
    TuneUiActivityObserverStatus_Unavailable,
    TuneUiActivityObserverStatus_Active,
    TuneUiActivityObserverStatus_Failed,
} TuneUiActivityObserverStatus;

typedef struct {
    u32 status;
    u32 last_result;
    u32 query_count;
    u32 event_count;
    u32 overlay_event_count;
    u32 application_event_count;
    u32 application_launch_count;
    u32 application_in_focus_count;
    u32 application_out_of_focus_count;
    u32 application_background_count;
    u32 application_exit_count;
    u32 quick_settings_open_count;
    u32 quick_settings_close_count;
    u32 loading_start_count;
    u32 loading_end_count;
    u32 last_event_type;
    u32 last_applet_id;
    u32 last_event_index;
    u32 input_status;
    u32 input_last_result;
    u32 home_sample_count;
    u32 home_press_count;
    u32 home_short_press_count;
    u32 home_long_press_count;
    u32 quick_settings_home_close_count;
    u32 quick_settings_b_close_count;
    u32 quick_settings_touch_close_count;
    u32 library_applet_event_count;
    u32 library_applet_in_focus_count;
    u32 library_applet_out_of_focus_count;
    u32 library_applet_focus_update_count;
    u32 last_library_applet_event_type;
    u64 last_program_id;
    u64 application_process_id;
    u64 application_program_id;
    u64 last_home_short_press_tick;
    u64 last_library_applet_program_id;
    u64 last_library_applet_event_tick;
    u64 foreground_library_applet_program_id;
    u8 quick_settings_open;
    u8 loading_active;
    u8 application_out_of_focus;
    u8 application_handoff_active;
    u8 has_overlay_signal;
    u8 has_application_signal;
    u8 has_last_event;
    u8 input_available;
    u8 home_button_held;
    u8 has_home_button_signal;
    u8 has_npad_signal;
    u8 has_touch_signal;
    u8 has_library_applet_signal;
    u8 library_applet_foreground;
    u8 loading_overlay_active;
    u8 reserved[1];
} TuneUiActivityObserverInfo;

Result tuneInitialize();

void tuneExit();

/**
 * @brief Get the current status of playback.
 * @param[out] status \ref AudioOutState
 */
Result tuneGetStatus(bool *status);

Result tunePlay();
Result tunePause();
Result tuneNext();
Result tunePrev();

/**
 * @brief Get the current playback volume.
 * @note On FW lower than [6.0.0] this will set the decode volume.
 * @param[out] out volume value (linear factor).
 */
Result tuneGetVolume(float *out);

/**
 * @brief Set the playback volume.
 * @note On FW lower than [6.0.0] this will return the decode volume.
 * @param[in] volume volume value (linear factor).
 */
Result tuneSetVolume(float volume);

/**
 * @brief Get the volume of the current title
 * @param[out] out volume value (linear factor).
 */
Result tuneGetTitleVolume(float *out);

/**
 * @brief Set the volume of the current title
 * @param[in] volume volume value (linear factor).
 */
Result tuneSetTitleVolume(float volume);

/**
 * @brief Get the default volume of all titles
 * @param[out] out volume value (linear factor).
 */
Result tuneGetDefaultTitleVolume(float *out);

/**
 * @brief Set the default volume of all titles
 * @param[in] volume volume value (linear factor).
 */
Result tuneSetDefaultTitleVolume(float volume);

/**
 * @brief Get the current loop status.
 * @param[out] state \ref TuneRepeatMode
 */
Result tuneGetRepeatMode(TuneRepeatMode *state);

/**
 * @brief Set repeat mode.
 * @param[in] state \ref TuneRepeatMode
 */
Result tuneSetRepeatMode(TuneRepeatMode state);

Result tuneGetShuffleMode(TuneShuffleMode *state);
Result tuneSetShuffleMode(TuneShuffleMode state);

/**
 * @brief Get the current queue size.
 * @param[out] count remaining tracks after current.
 */
Result tuneGetPlaylistSize(u32 *count);

/**
 * @brief Read queue.
 * @param[out] read Amount written to buffer.
 * @param[out] out_path Path array FS_MAX_PATH * n
 * @param[in] out_path_length Size of the supplied path array.
 */
Result tuneGetPlaylistItem(u32 index, char *out_path, size_t out_path_length);

/**
 * @brief Get current song.
 * @param[out] out_path Path to current playing song.
 * @param[in] out_path_length Size of the out_path buffer. Path of the current track needs to fit.
 * @param[out] out \ref MusicCurrentTune
 */
Result tuneGetCurrentQueueItem(char *out_path, size_t out_path_length, TuneCurrentStats *out);

/**
 * @brief Clear queue.
 */
Result tuneClearQueue();
Result tuneMoveQueueItem(u32 src, u32 dst);
Result tuneSelect(u32 index);
Result tuneSeek(u32 position);

/**
 * @brief Add track to queue.
 * @note Must not include leading mount name.
 * @note Must match ^(sdmc:/.*.mp3)$
 * @param[in] path Path to file on sdcard.
 */
Result tuneEnqueue(const char *path, TuneEnqueueType type);

Result tuneRemove(u32 index);

/**
 * @brief Reload the System UI OST master setting after config.ini changes.
 */
Result tuneReloadAppletBgm();
Result tuneReloadOstState(u64 title_id);
Result tuneReloadOstMisc();
Result tuneGetActiveOstState(u64* title_id);

/**
 * @brief Get experimental qlaunch SystemAppletScene observer diagnostics.
 */
Result tuneGetQlaunchSceneObserver(TuneQlaunchSceneObserverInfo* out);

/**
 * @brief Clear the diagnostic transition history without changing playback.
 */
Result tuneResetQlaunchSceneHistory();

/**
 * @brief Get Album movie-stream observer diagnostics.
 */
Result tuneGetAlbumVideoObserver(TuneAlbumVideoObserverInfo* out);

/**
 * @brief Get PSC power-transition observer diagnostics.
 */
Result tuneGetPowerStateObserver(TunePowerStateObserverInfo* out);

/**
 * @brief Get native Quick Settings and application-loading diagnostics.
 */
Result tuneGetUiActivityObserver(TuneUiActivityObserverInfo* out);

/**
 * @brief Clear UI-activity counters without changing live routing state.
 */
Result tuneResetUiActivityHistory(void);

Result tuneQuit();

Result tuneGetApiVersion(u32 *version);

#ifdef __cplusplus
}
#endif
