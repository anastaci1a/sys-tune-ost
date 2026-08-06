#include "music_player.hpp"

#include "applet_bgm.hpp"
#include "../album_video_observer.hpp"
#include "../power_state_observer.hpp"
#include "../qlaunch_scene_observer.hpp"
#include "../ui_activity_observer.hpp"
#include "../tune_result.hpp"
#include "sdmc/sdmc.hpp"
#include "pm/pm.hpp"
#include "config/config.hpp"
#include "source.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <utility>
#include <nxExt.h>

namespace tune::impl {

namespace {

constexpr float VOLUME_MAX = applet_bgm::VolumeMax;
constexpr auto AUDIO_FREQ = 48000;
constexpr auto AUDIO_CHANNEL_COUNT = 2;
constexpr auto AUDIO_BUFFER_COUNT = 2;
constexpr auto AUDIO_LATENCY_MS = 42;
constexpr auto AUDIO_BUFFER_SIZE =
    AUDIO_FREQ / 1000 * AUDIO_LATENCY_MS * AUDIO_CHANNEL_COUNT;
constexpr u8 PDM_POWER_STATE_TURNED_ON = 0;
constexpr u8 PDM_POWER_STATE_TURNED_OFF = 1;
constexpr u8 PDM_POWER_STATE_SLEEP_MODE_ON = 2;
constexpr u8 PDM_POWER_STATE_SLEEP_MODE_OFF = 3;
constexpr s32 PDM_EVENT_BATCH_SIZE = 16;
constexpr u64 BOOT_WAKE_HOME_STABLE_NS = 1'500'000'000ULL;
constexpr u64 LIBRARY_FOCUS_SIGNAL_FRESH_NS = 2'000'000'000ULL;

struct OstTrack {
    char path[applet_bgm::PathSizeMax]{};
    bool valid{};
};

struct OstSession {
    u64 state{applet_bgm::SilentTitleId};
    std::array<OstTrack, applet_bgm::PlaylistMax> tracks{};
    std::array<u8, applet_bgm::PlaylistMax> order{};
    u32 count{};
    u32 position{};
    u32 resume_frame{};
    RepeatMode repeat{RepeatMode::All};
    ShuffleMode shuffle{ShuffleMode::Off};
    bool paused{};
    bool finished{};
    u32 generation{};

    const OstTrack* Current() const {
        if (finished || count == 0 || position >= count) {
            return nullptr;
        }
        const auto index = order[position];
        if (index >= tracks.size() || !tracks[index].valid) {
            return nullptr;
        }
        return &tracks[index];
    }

    OstTrack* Current() {
        return const_cast<OstTrack*>(std::as_const(*this).Current());
    }

    void Shuffle() {
        if (count < 2) {
            return;
        }

        for (u32 i = count - 1; i > 0; i--) {
            const auto j = static_cast<u32>(randomGet64() % (i + 1));
            std::swap(order[i], order[j]);
        }
    }

    bool Advance(bool manual = false) {
        resume_frame = 0;
        if (count == 0) {
            finished = true;
            return false;
        }

        if (!manual && repeat == RepeatMode::One && Current() != nullptr) {
            finished = false;
            return true;
        }

        for (u32 checked = 0; checked < count; checked++) {
            if (position + 1 < count) {
                position++;
            } else if (manual || repeat == RepeatMode::All) {
                position = 0;
            } else {
                finished = true;
                return false;
            }

            const auto index = order[position];
            if (index < tracks.size() && tracks[index].valid) {
                finished = false;
                return true;
            }
        }

        finished = true;
        return false;
    }

    bool Previous() {
        resume_frame = 0;
        if (count == 0) {
            finished = true;
            return false;
        }

        for (u32 checked = 0; checked < count; checked++) {
            position = position > 0 ? position - 1 : count - 1;
            const auto index = order[position];
            if (index < tracks.size() && tracks[index].valid) {
                finished = false;
                return true;
            }
        }

        finished = true;
        return false;
    }
};

enum class PlaybackEnd {
    Interrupted,
    Natural,
    Error,
};

enum class PowerTransition {
    None,
    Sleep,
    Wake,
};

enum class HomeSceneGate {
    None,
    BootOrWake,
};

struct PowerTransitions {
    PowerTransition last{PowerTransition::None};
    bool saw_sleep{};
    bool saw_wake{};
};

struct PlaybackResult {
    Result result{};
    PlaybackEnd end{PlaybackEnd::Error};
    u32 current_frame{};
};

struct PlaybackSourceCache {
    std::unique_ptr<Source> source{};
    char path[applet_bgm::PathSizeMax]{};
    u32 generation{};

    void Clear() {
        source.reset();
        path[0] = '\0';
        generation = 0;
    }

    bool Take(const char* requested_path, u32 requested_generation,
              u32 requested_frame,
              std::unique_ptr<Source>& out) {
        const bool matches = source &&
            generation == requested_generation &&
            std::strcmp(path, requested_path) == 0 &&
            source->Tell().first == requested_frame;
        if (!matches) {
            Clear();
            return false;
        }

        // Another decoder may have used the shared compressed-data cache while
        // HOME was in the background. Its decoder/resampler state is retained,
        // but the shared file cache must be repopulated from this file.
        source->ResetIoBuffer();
        out = std::move(source);
        path[0] = '\0';
        generation = 0;
        return true;
    }

    void Store(const char* cached_path, u32 cached_generation,
               std::unique_ptr<Source> cached_source) {
        Clear();
        std::snprintf(path, sizeof(path), "%s", cached_path);
        generation = cached_generation;
        source = std::move(cached_source);
    }
};

LockableMutex g_mutex;
LockableMutex g_startup_mutex;

OstSession g_home_session;
OstSession g_applet_session;
OstSession g_startup_session;
OstSession g_reconcile_session;
OstSession* g_active_session{};
u64 g_active_state{applet_bgm::SilentTitleId};
bool g_home_initialized{};
u32 g_session_generation{};

std::atomic<PlayerStatus> g_status = PlayerStatus::FetchNext;
Source* g_source{};
char g_playing_path[applet_bgm::PathSizeMax]{};

// Headphone/output pause remains global. Manual play/pause lives on each
// session so the top controls affect only the soundtrack currently shown.
std::atomic_bool g_output_paused = false;
std::atomic_bool g_should_run = true;
std::atomic_bool g_master_enabled = false;
std::atomic_bool g_startup_active = false;
std::atomic_bool g_startup_on_wake = false;
std::atomic_bool g_separate_wake_playlist = false;
std::atomic<u32> g_startup_generation = 0;
std::atomic_bool g_applet_bgm_reload = true;
std::atomic_bool g_discard_audio_buffers = false;
std::atomic_bool g_power_audio_hold = false;
std::atomic_bool g_lock_screen_enabled = true;
std::atomic<u32> g_audio_quiesce_request = 0;
std::atomic<u32> g_audio_quiesce_completed = 0;

std::atomic<u64> g_detected_state = applet_bgm::SilentTitleId;
std::atomic<u64> g_requested_state = applet_bgm::SilentTitleId;
std::atomic<u32> g_transition_serial = 1;

std::atomic_bool g_force_reload_pending = false;
std::atomic<u64> g_force_reload_state = applet_bgm::SilentTitleId;

std::atomic<u32> g_fade_in_ms = 150;
std::atomic<u32> g_fade_out_ms = 0;
std::atomic<u32> g_mid_song_fade_in_ms = 300;
std::atomic<u32> g_mid_song_fade_out_ms = 500;
std::atomic<u32> g_loading_start_delay_ms = 500;
std::atomic<u32> g_loading_end_delay_ms = 0;
std::atomic<u32> g_quick_settings_hold_ms = 400;

std::atomic<float> g_global_volume = 1.f;
std::atomic<float> g_state_volume = 1.f;
std::atomic<float> g_album_video_volume = 1.f;
std::atomic<float> g_quick_settings_volume = 0.5f;
std::atomic_bool g_quick_settings_enabled = true;
float g_default_title_volume = 1.f;

AudioOutBuffer g_audout_buffer[AUDIO_BUFFER_COUNT];
alignas(0x1000) s16 AudioMemoryPool[AUDIO_BUFFER_COUNT]
    [(AUDIO_BUFFER_SIZE + 0xFFF) & ~0xFFF];
static_assert((sizeof(AudioMemoryPool[0]) % 0x2000) == 0,
              "Audio Memory pool needs to be page aligned!");

bool g_pdmqry_available = false;

void RefreshLockScreenSetting() {
    bool enabled = true;
    if (R_SUCCEEDED(setsysInitialize())) {
        if (R_FAILED(setsysGetLockScreenFlag(&enabled))) {
            // Failing closed avoids leaking Home audio into a Lock screen.
            enabled = true;
        }
        setsysExit();
    }
    g_lock_screen_enabled.store(enabled, std::memory_order_release);
}

void SignalTransition() {
    g_transition_serial.fetch_add(1, std::memory_order_release);
}

void RequestState(u64 state) {
    if (g_requested_state.exchange(state, std::memory_order_acq_rel) != state) {
        SignalTransition();
    }
}

void RequestStateAndDiscardAudio(u64 state) {
    g_discard_audio_buffers.store(true, std::memory_order_release);
    g_requested_state.store(state, std::memory_order_release);
    SignalTransition();
}

u64 ResolveQlaunchState(
    u64 process_state, const qlaunch_scene::SceneSnapshot& snapshot) {
    // Lock can visibly cover a still-running game or library applet and must
    // always win. Settings is different: qlaunch can retain its Settings scene
    // while a submenu applet (Mii Editor, Network Connection, Amiibo, etc.) is
    // actually in front, so a detected applet process must get first refusal.
    if (snapshot.availability == qlaunch_scene::SceneAvailability::Ready) {
        if (snapshot.scene == applet_bgm::QlaunchSceneLock) {
            return applet_bgm::LockStateId;
        }
        if (snapshot.scene == applet_bgm::QlaunchSceneSettings &&
            !applet_bgm::IsDetectedAppletTitleId(process_state)) {
            return applet_bgm::SettingsStateId;
        }
    }

    if (process_state != applet_bgm::QlaunchTitleId) {
        return process_state;
    }

    if (snapshot.availability ==
        qlaunch_scene::SceneAvailability::Unavailable) {
        // Preserve stable-build behavior when the best-effort observer cannot
        // run. Waiting for the first report is different: silence avoids
        // falsely playing Home during the boot logo or a wake transition.
        return applet_bgm::QlaunchTitleId;
    }
    if (snapshot.availability != qlaunch_scene::SceneAvailability::Ready) {
        return applet_bgm::SilentTitleId;
    }

    switch (snapshot.scene) {
        case applet_bgm::QlaunchSceneHome:
            return applet_bgm::QlaunchTitleId;
        case applet_bgm::QlaunchSceneSettings:
            return applet_bgm::SettingsStateId;
        case applet_bgm::QlaunchSceneLock:
            return applet_bgm::LockStateId;
        default:
            // Never label an unverified qlaunch scene as Home, Settings, or
            // Lock. Diagnostics remain available so it can be mapped later.
            return applet_bgm::SilentTitleId;
    }
}

bool StartupMayContinue(u64 process_state, u64 resolved_state) {
    // Lock is part of the boot/wake path, so it must not immediately cancel a
    // Startup Sound. Unknown qlaunch scenes also remain eligible so a new or
    // transient firmware value cannot suppress the boot sound. Settings and
    // every explicitly opened applet do cancel it.
    return resolved_state == applet_bgm::LockStateId ||
           (process_state == applet_bgm::QlaunchTitleId &&
            resolved_state != applet_bgm::SettingsStateId);
}

bool IsPlayablePath(const char* path) {
    return path && path[0] != '\0' && sdmc::FileExists(path) &&
           GetSourceType(path) != SourceType::NONE;
}

u32 NextSessionGenerationLocked() {
    auto generation = ++g_session_generation;
    if (generation == 0) {
        generation = ++g_session_generation;
    }
    return generation;
}

void LoadSession(OstSession& session, u64 state, bool startup,
                 const char* preferred_startup_path = nullptr) {
    session = {};
    session.state = state;
    session.generation = NextSessionGenerationLocked();

    // Read the whole playlist in one INI pass. File existence is deliberately
    // checked only when a track is opened; probing every path on every applet
    // activation can be surprisingly expensive on an SD card.
    const auto loaded = config::load_ost_playlist(
        state, session.tracks[0].path, sizeof(session.tracks[0]),
        session.tracks.size());
    session.repeat = startup
        ? RepeatMode::Off
        : static_cast<RepeatMode>(loaded.repeat);
    session.shuffle = startup || loaded.shuffle
        ? ShuffleMode::On
        : ShuffleMode::Off;

    for (u32 i = 0; i < loaded.count; i++) {
        auto& track = session.tracks[i];
        if (track.path[0] == '\0' || GetSourceType(track.path) == SourceType::NONE) {
            continue;
        }

        track.valid = true;
        session.order[session.count] = i;
        session.count++;
    }

    if (session.count == 0) {
        session.finished = true;
        return;
    }

    if (startup) {
        auto chosen = static_cast<u32>(randomGet64() % session.count);
        if (preferred_startup_path && preferred_startup_path[0] != '\0') {
            for (u32 position = 0; position < session.count; ++position) {
                const auto track_index = session.order[position];
                if (track_index < session.tracks.size() &&
                    std::strcmp(
                        session.tracks[track_index].path,
                        preferred_startup_path) == 0) {
                    chosen = position;
                    break;
                }
            }
        }
        const auto chosen_track = session.order[chosen];
        if (chosen_track != 0) {
            session.tracks[0] = session.tracks[chosen_track];
        }
        session.order[0] = 0;
        session.count = 1;
    } else if (session.shuffle == ShuffleMode::On) {
        session.Shuffle();
    }
}

bool ReconcileSessionLocked(OstSession& session, u64 state, bool startup) {
    const auto* current = session.Current();
    char current_path[applet_bgm::PathSizeMax]{};
    if (current) {
        std::snprintf(current_path, sizeof(current_path), "%s", current->path);
    }
    const auto old_resume_frame = session.resume_frame;
    const auto old_generation = session.generation;
    const auto old_paused = session.paused;

    LoadSession(
        g_reconcile_session, state, startup,
        startup ? current_path : nullptr);

    u32 matching_position = g_reconcile_session.count;
    if (current_path[0] != '\0') {
        for (u32 position = 0; position < g_reconcile_session.count;
             ++position) {
            const auto track_index = g_reconcile_session.order[position];
            if (track_index < g_reconcile_session.tracks.size() &&
                std::strcmp(
                    g_reconcile_session.tracks[track_index].path,
                    current_path) == 0) {
                matching_position = position;
                break;
            }
        }
    }

    const bool kept_current =
        matching_position < g_reconcile_session.count;
    if (kept_current) {
        if (!startup &&
            g_reconcile_session.shuffle == ShuffleMode::On) {
            // Keep the playing song fixed and reshuffle every other entry as
            // one fresh upcoming queue.
            std::swap(
                g_reconcile_session.order[0],
                g_reconcile_session.order[matching_position]);
            g_reconcile_session.position = 0;
        } else {
            // Ordered playlists immediately adopt the current file's edited
            // position and its new neighbors.
            g_reconcile_session.position = matching_position;
        }
        g_reconcile_session.resume_frame = old_resume_frame;
        g_reconcile_session.generation = old_generation;
        g_reconcile_session.paused = old_paused;
        g_reconcile_session.finished = false;
    }

    session = g_reconcile_session;
    return kept_current;
}

void ActivateStateLocked(u64 state, bool force_reload) {
    if (state == applet_bgm::SilentTitleId || !g_master_enabled) {
        g_active_session = nullptr;
        g_active_state = applet_bgm::SilentTitleId;
        g_state_volume.store(1.f, std::memory_order_release);
        return;
    }

    if (applet_bgm::IsStartupState(state)) {
        LoadSession(g_startup_session, state, true);
        g_active_session = &g_startup_session;
    } else if (state == applet_bgm::QlaunchTitleId) {
        const bool returning_to_home =
            g_active_state != applet_bgm::QlaunchTitleId;
        if (!g_home_initialized || force_reload) {
            LoadSession(g_home_session, state, false);
            g_home_initialized = true;
        } else {
            // Policy can be edited while HOME is paused behind another applet.
            // Refresh repeat without rebuilding its retained queue/position.
            g_home_session.repeat =
                static_cast<RepeatMode>(config::get_ost_repeat(state));
        }
        // Pause is a local Home-session control, not a state that survives a
        // trip through another applet. Preserve the queue, track, and decoder
        // position, but let the normal mid-song fade resume it on re-entry.
        if (returning_to_home) {
            g_home_session.paused = false;
        }
        g_active_session = &g_home_session;
    } else {
        // Every non-HOME applet activation starts from a freshly loaded and,
        // if requested, freshly shuffled playlist.
        LoadSession(g_applet_session, state, false);
        g_active_session = &g_applet_session;
    }

    g_active_state = state;
    g_state_volume.store(
        config::get_ost_volume(state), std::memory_order_release);
}

void ApplyPendingState() {
    const auto requested = g_requested_state.load(std::memory_order_acquire);
    bool force_reload = false;
    if (g_force_reload_pending.exchange(false, std::memory_order_acq_rel)) {
        force_reload = g_force_reload_state.load(std::memory_order_acquire) == requested;
    }

    std::scoped_lock lk(g_mutex);
    if (requested != g_active_state || force_reload) {
        ActivateStateLocked(requested, force_reload);
    }
}

void CompleteStartup(u32 generation) {
    std::scoped_lock lk(g_startup_mutex);
    if (g_startup_generation.load(std::memory_order_acquire) != generation ||
        !g_startup_active.exchange(false)) {
        return;
    }

    RequestState(g_master_enabled
        ? g_detected_state.load(std::memory_order_acquire)
        : applet_bgm::SilentTitleId);
}

void BeginStartup(u64 startup_state, bool discard_audio = false) {
    if (!applet_bgm::IsStartupState(startup_state)) {
        return;
    }
    std::scoped_lock lk(g_startup_mutex);
    g_startup_generation.fetch_add(1, std::memory_order_acq_rel);
    g_startup_active = true;

    if (discard_audio) {
        g_discard_audio_buffers.store(true, std::memory_order_release);
    }

    // A wake always chooses a fresh random entry, including when the previous
    // Startup sound was still active as the console entered sleep.
    g_force_reload_state.store(
        startup_state, std::memory_order_release);
    g_force_reload_pending.store(true, std::memory_order_release);
    g_requested_state.store(
        startup_state, std::memory_order_release);
    SignalTransition();
}

void CancelStartupAndRequest(u64 state, bool discard_audio = false) {
    std::scoped_lock lk(g_startup_mutex);
    if (g_startup_active.exchange(false)) {
        // Invalidate completion from any playback that was already in flight.
        g_startup_generation.fetch_add(1, std::memory_order_acq_rel);
    }
    if (discard_audio) {
        RequestStateAndDiscardAudio(state);
    } else {
        RequestState(state);
    }
}

Result ConsumePowerTransitions(PowerTransitions* transitions) {
    static s32 last_end_entry_index = -1;
    *transitions = {};

    s32 total_entries = 0;
    s32 start_entry_index = 0;
    s32 end_entry_index = 0;
    Result rc = pdmqryGetAvailablePlayEventRange(
        &total_entries, &start_entry_index, &end_entry_index);
    if (R_FAILED(rc)) {
        return rc;
    }

    // Ignore historical wake events when the sysmodule first opens pdm:qry.
    if (last_end_entry_index < 0 || total_entries <= 0) {
        last_end_entry_index = end_entry_index;
        return 0;
    }

    if (end_entry_index < last_end_entry_index) {
        // The log was cleared or its index wrapped. Re-baseline instead of
        // replaying an old wake entry as though it had just happened.
        last_end_entry_index = end_entry_index;
        return 0;
    }
    if (end_entry_index == last_end_entry_index) {
        return 0;
    }

    s32 next_entry_index = std::max(
        start_entry_index, last_end_entry_index + 1);
    if (next_entry_index > end_entry_index) {
        last_end_entry_index = end_entry_index;
        return 0;
    }

    while (next_entry_index <= end_entry_index) {
        PdmPlayEvent events[PDM_EVENT_BATCH_SIZE]{};
        const auto remaining = end_entry_index - next_entry_index + 1;
        const auto requested = std::min(PDM_EVENT_BATCH_SIZE, remaining);
        s32 count = 0;
        rc = pdmqryQueryPlayEvent(
            next_entry_index, events, requested, &count);
        if (R_FAILED(rc)) {
            return rc;
        }
        if (count <= 0) {
            return 0;
        }

        for (s32 i = 0; i < count; i++) {
            const auto& event = events[i];
            if (event.play_event_type != PdmPlayEventType_PowerStateChange) {
                continue;
            }

            const auto state = event.event_data.power_state_change.value;
            if (state == PDM_POWER_STATE_TURNED_OFF ||
                state == PDM_POWER_STATE_SLEEP_MODE_ON) {
                transitions->last = PowerTransition::Sleep;
                transitions->saw_sleep = true;
            } else if (state == PDM_POWER_STATE_TURNED_ON ||
                       state == PDM_POWER_STATE_SLEEP_MODE_OFF) {
                transitions->last = PowerTransition::Wake;
                transitions->saw_wake = true;
            }
        }

        next_entry_index += count;
        last_end_entry_index = next_entry_index - 1;
    }

    return 0;
}

struct GainEnvelope {
    float current{1.f};
    float start{1.f};
    float target{1.f};
    u64 elapsed_frames{};
    u64 total_frames{};

    void SetImmediate(float value) {
        current = value;
        start = value;
        target = value;
        elapsed_frames = 0;
        total_frames = 0;
    }

    void Retarget(float value, u32 duration_ms) {
        if (value == target) {
            return;
        }
        start = current;
        target = value;
        elapsed_frames = 0;
        total_frames =
            static_cast<u64>(AUDIO_FREQ) * duration_ms / 1000;
        if (total_frames == 0) {
            current = target;
        }
    }

    std::pair<float, float> Advance(u64 frame_count) {
        const float before = current;
        if (total_frames == 0 || elapsed_frames >= total_frames) {
            current = target;
            return {before, current};
        }

        elapsed_frames = std::min(total_frames, elapsed_frames + frame_count);
        const auto progress = static_cast<float>(elapsed_frames) /
            static_cast<float>(total_frames);
        current = start + (target - start) * progress;
        if (elapsed_frames == total_frames) {
            current = target;
        }
        return {before, current};
    }
};

float AlbumVideoGainTarget(u64 playing_state) {
    if (playing_state != applet_bgm::AlbumTitleId) {
        return 1.f;
    }
    const auto snapshot = album_video::GetSnapshot();
    return snapshot.active
        ? g_album_video_volume.load(std::memory_order_acquire)
        : 1.f;
}

float QuickSettingsGainTarget() {
    if (!g_quick_settings_enabled.load(std::memory_order_acquire)) {
        return 1.f;
    }
    const auto snapshot = ui_activity::GetSnapshot();
    return snapshot.input_available && snapshot.quick_settings_open
        ? g_quick_settings_volume.load(std::memory_order_acquire)
        : 1.f;
}

void ApplyGain(s16* samples, size_t byte_count, float start_gain,
               float end_gain, float playback_gain_start,
               float playback_gain_end) {
    const auto frame_count = byte_count / (sizeof(s16) * AUDIO_CHANNEL_COUNT);
    if (frame_count == 0) {
        return;
    }
    if (start_gain >= 0.9999f && end_gain >= 0.9999f &&
        playback_gain_start >= 0.9999f &&
        playback_gain_start <= 1.0001f &&
        playback_gain_end >= 0.9999f &&
        playback_gain_end <= 1.0001f) {
        return;
    }

    for (size_t frame = 0; frame < frame_count; frame++) {
        const float progress = frame_count > 1
            ? static_cast<float>(frame) / static_cast<float>(frame_count - 1)
            : 1.f;
        const float fade_gain = std::clamp(
            start_gain + (end_gain - start_gain) * progress, 0.f, 1.f);
        const float playback_gain = playback_gain_start +
            (playback_gain_end - playback_gain_start) * progress;
        const float gain = fade_gain * playback_gain;
        for (size_t channel = 0; channel < AUDIO_CHANNEL_COUNT; channel++) {
            const auto index = frame * AUDIO_CHANNEL_COUNT + channel;
            const auto amplified = static_cast<float>(samples[index]) * gain;
            samples[index] = static_cast<s16>(
                std::clamp(amplified, -32768.f, 32767.f));
        }
    }
}

PlaybackResult PlayTrack(const char* path, u32 resume_frame, u32 play_serial,
                         u32 playing_generation,
                         u64 playing_state,
                         PlaybackSourceCache* resume_cache) {
    std::unique_ptr<Source> source;
    const bool reused_source = resume_cache && resume_cache->Take(
        path, playing_generation, resume_frame, source);
    if (!reused_source) {
        source = OpenFile(path);
        if (!source || !source->IsOpen()) {
            return {tune::FileOpenFailure, PlaybackEnd::Error, 0};
        }
        if (!source->SetupResampler(audoutGetChannelCount(), audoutGetSampleRate())) {
            return {tune::VoiceInitFailure, PlaybackEnd::Error, 0};
        }
        if (resume_frame != 0 && !source->Seek(resume_frame)) {
            resume_frame = 0;
        }
    }
    const bool resuming_mid_song = reused_source || resume_frame != 0;

    AudioOutState state;
    Result rc = audoutGetAudioOutState(&state);
    if (R_FAILED(rc)) {
        return {rc, PlaybackEnd::Error, resume_frame};
    }
    if (state == AudioOutState_Stopped) {
        rc = audoutStartAudioOut();
        if (R_FAILED(rc)) {
            return {rc, PlaybackEnd::Error, resume_frame};
        }
    }

    {
        std::scoped_lock lk(g_mutex);
        g_source = source.get();
    }
    int first = 1;
    u64 segment_output_frames = 0;
    bool transition_fade = false;
    u64 transition_total_frames = 0;
    u64 transition_remaining_frames = 0;
    u32 interrupted_resume_frame = resume_frame;
    PlaybackResult outcome{0, PlaybackEnd::Error, resume_frame};
    GainEnvelope album_video_gain;
    album_video_gain.SetImmediate(AlbumVideoGainTarget(playing_state));
    GainEnvelope quick_settings_gain;
    quick_settings_gain.SetImmediate(QuickSettingsGainTarget());

    while (g_should_run) {
        const bool interrupted = g_output_paused ||
            g_transition_serial.load(std::memory_order_acquire) != play_serial;
        if (interrupted && !transition_fade) {
            interrupted_resume_frame = source->Tell().first;
            // Sleep/wake ownership changes must discard already queued audio;
            // waiting for a fade here can let stale Home audio escape on wake.
            if (g_discard_audio_buffers.load(std::memory_order_acquire)) {
                outcome = {0, PlaybackEnd::Interrupted, interrupted_resume_frame};
                break;
            }

            transition_fade = true;
            transition_total_frames =
                static_cast<u64>(AUDIO_FREQ) *
                g_mid_song_fade_out_ms.load() / 1000;
            transition_remaining_frames = transition_total_frames;
            if (transition_total_frames == 0) {
                outcome = {0, PlaybackEnd::Interrupted, interrupted_resume_frame};
                break;
            }
        }

        AudioOutBuffer* buffer = nullptr;
        for (int i = 0; i < AUDIO_BUFFER_COUNT; i++) {
            bool has_buffer = false;
            rc = audoutContainsAudioOutBuffer(&g_audout_buffer[i], &has_buffer);
            if (R_FAILED(rc)) {
                outcome = {rc, PlaybackEnd::Error, source->Tell().first};
                break;
            }
            if (!has_buffer) {
                buffer = &g_audout_buffer[i];
                break;
            }
        }
        if (R_FAILED(outcome.result)) {
            break;
        }

        if (!buffer) {
            u32 released_count = 0;
            rc = audoutWaitPlayFinish(&buffer, &released_count, UINT64_MAX);
            if (R_FAILED(rc)) {
                outcome = {rc, PlaybackEnd::Error, source->Tell().first};
                break;
            }
        }

        auto buffer_size = AUDIO_BUFFER_SIZE * sizeof(s16);
        if (first) {
            first--;
            buffer_size = std::min<size_t>(512 * sizeof(s16), buffer_size);
        }

        const auto before = source->Tell();
        const auto decoded_bytes = source->Resample(
            static_cast<u8*>(buffer->buffer), buffer_size);
        const auto after = source->Tell();
        if (decoded_bytes < 0) {
            outcome = {tune::Generic, PlaybackEnd::Error, after.first};
            break;
        }
        if (decoded_bytes == 0) {
            // Decoder exhaustion is a normal track boundary. Some decoders do
            // not update Done() until this final read, so consulting it here
            // incorrectly invalidated every track after its first playback.
            outcome = {0, PlaybackEnd::Natural, after.first};
            break;
        }

        const auto output_frames = static_cast<u64>(decoded_bytes) /
            (sizeof(s16) * AUDIO_CHANNEL_COUNT);
        const auto fade_in_ms = resuming_mid_song
            ? g_mid_song_fade_in_ms.load()
            : g_fade_in_ms.load();
        const auto fade_in_frames =
            static_cast<u64>(AUDIO_FREQ) * fade_in_ms / 1000;
        const float fade_in_start = fade_in_frames == 0
            ? 1.f
            : std::min(1.f, static_cast<float>(segment_output_frames) /
                              static_cast<float>(fade_in_frames));
        const float fade_in_end = fade_in_frames == 0
            ? 1.f
            : std::min(1.f, static_cast<float>(segment_output_frames + output_frames) /
                              static_cast<float>(fade_in_frames));

        float fade_out_start = 1.f;
        float fade_out_end = 1.f;
        const auto fade_out_ms = g_fade_out_ms.load();
        if (fade_out_ms != 0 && before.second != 0) {
            const auto native_fade_frames =
                static_cast<u64>(source->GetSampleRate()) * fade_out_ms / 1000;
            if (native_fade_frames != 0) {
                const auto before_remaining = before.second > before.first
                    ? before.second - before.first : 0;
                const auto after_remaining = after.second > after.first
                    ? after.second - after.first : 0;
                fade_out_start = std::min(1.f,
                    static_cast<float>(before_remaining) /
                    static_cast<float>(native_fade_frames));
                fade_out_end = std::min(1.f,
                    static_cast<float>(after_remaining) /
                    static_cast<float>(native_fade_frames));
            }
        }

        if (transition_fade) {
            const float transition_start = static_cast<float>(transition_remaining_frames) /
                                           static_cast<float>(transition_total_frames);
            const auto remaining_after = transition_remaining_frames > output_frames
                ? transition_remaining_frames - output_frames : 0;
            const float transition_end = static_cast<float>(remaining_after) /
                                         static_cast<float>(transition_total_frames);
            fade_out_start = std::min(fade_out_start, transition_start);
            fade_out_end = std::min(fade_out_end, transition_end);
            transition_remaining_frames = remaining_after;
        }

        const auto album_video_target = AlbumVideoGainTarget(playing_state);
        if (album_video_target != album_video_gain.target) {
            const auto duration_ms =
                album_video_target < album_video_gain.current
                ? g_mid_song_fade_out_ms.load(std::memory_order_acquire)
                : g_mid_song_fade_in_ms.load(std::memory_order_acquire);
            album_video_gain.Retarget(album_video_target, duration_ms);
        }
        const auto [video_gain_start, video_gain_end] =
            album_video_gain.Advance(output_frames);
        const auto quick_settings_target = QuickSettingsGainTarget();
        if (quick_settings_target != quick_settings_gain.target) {
            const auto duration_ms =
                quick_settings_target < quick_settings_gain.current
                ? g_mid_song_fade_out_ms.load(std::memory_order_acquire)
                : g_mid_song_fade_in_ms.load(std::memory_order_acquire);
            quick_settings_gain.Retarget(quick_settings_target, duration_ms);
        }
        const auto [quick_gain_start, quick_gain_end] =
            quick_settings_gain.Advance(output_frames);
        const auto playback_gain =
            g_global_volume.load(std::memory_order_acquire) *
            g_state_volume.load(std::memory_order_acquire);
        ApplyGain(
            static_cast<s16*>(buffer->buffer), decoded_bytes,
            std::min(fade_in_start, fade_out_start),
            std::min(fade_in_end, fade_out_end),
            playback_gain * video_gain_start * quick_gain_start,
            playback_gain * video_gain_end * quick_gain_end);

        buffer->data_size = decoded_bytes;
        rc = audoutAppendAudioOutBuffer(buffer);
        if (R_FAILED(rc)) {
            outcome = {rc, PlaybackEnd::Error, after.first};
            break;
        }
        segment_output_frames += output_frames;

        if (transition_fade && transition_remaining_frames == 0) {
            outcome = {0, PlaybackEnd::Interrupted, interrupted_resume_frame};
            break;
        }
        if (source->Done()) {
            outcome = {0, PlaybackEnd::Natural, after.first};
            break;
        }
    }

    if (!g_should_run && outcome.end == PlaybackEnd::Error && R_SUCCEEDED(outcome.result)) {
        outcome = {0, PlaybackEnd::Interrupted, source->Tell().first};
    }
    const bool retain_source = g_should_run && resume_cache &&
        outcome.end == PlaybackEnd::Interrupted && R_SUCCEEDED(outcome.result);
    if (retain_source) {
        // The fade consumes decoded audio. Retaining the decoder at its actual
        // post-fade position avoids a linear MP3 seek when HOME resumes.
        outcome.current_frame = source->Tell().first;
    }
    {
        std::scoped_lock lk(g_mutex);
        g_source = nullptr;
    }
    if (retain_source) {
        resume_cache->Store(path, playing_generation,
                            std::move(source));
    }
    return outcome;
}

u64 ActiveEditableStateLocked() {
    if (!g_active_session || g_active_state == applet_bgm::SilentTitleId ||
        applet_bgm::IsStartupState(g_active_state)) {
        return applet_bgm::SilentTitleId;
    }
    return g_active_state;
}

} // namespace

Result Initialize() {
    for (int i = 0; i < AUDIO_BUFFER_COUNT; i++) {
        g_audout_buffer[i].buffer = AudioMemoryPool[i];
        g_audout_buffer[i].buffer_size = sizeof(AudioMemoryPool[i]);
    }

    R_TRY(audoutInitialize());
    audoutSetAudioOutVolume(1.f);
    SetVolume(config::get_volume());
    SetDefaultTitleVolume(config::get_default_title_volume());
    config::migrate_ost_config();
    ReloadOstMisc();
    RefreshLockScreenSetting();

    g_power_audio_hold.store(false, std::memory_order_release);
    g_audio_quiesce_request.store(0, std::memory_order_release);
    g_audio_quiesce_completed.store(0, std::memory_order_release);

    g_master_enabled = config::get_applet_bgm_enabled();
    if (g_master_enabled &&
        config::get_ost_playlist_size(applet_bgm::StartupTitleId) != 0) {
        // Claim playback before the process/scene detector starts. Falling
        // back to Home here leaked Home audio into the boot-logo sequence.
        BeginStartup(applet_bgm::StartupTitleId);
    }

    // Best-effort HOME focus detection. pdm:qry has very few sessions;
    // replace libnx's initial session with a clone so the original slot is
    // released while retaining a working query handle.
    if (R_SUCCEEDED(pdmqryInitialize())) {
        Service* service = pdmqryGetServiceSession();
        Service clone{};
        if (R_SUCCEEDED(serviceClone(service, &clone))) {
            serviceClose(service);
            std::memcpy(service, &clone, sizeof(Service));
            g_pdmqry_available = true;
        } else {
            pdmqryExit();
        }
    }
    ui_activity::Initialize(g_pdmqry_available);

    // Best-effort early power-state detection. PDM remains available as a
    // fallback if PSC rejects the experimental observer module.
    power_state::Initialize();

    return 0;
}

void Exit() {
    power_state::Exit();
    g_should_run = false;
    SignalTransition();
}

void Finalize() {
    ui_activity::Exit();
    if (g_pdmqry_available) {
        pdmqryExit();
        g_pdmqry_available = false;
    }
}

void TuneThreadFunc(void*) {
    PlaybackSourceCache home_source_cache;

    while (g_should_run) {
        if (g_discard_audio_buffers.exchange(false, std::memory_order_acq_rel)) {
            bool flushed = false;
            // Best effort: this is supported on every firmware targeted by
            // this fork and removes buffers queued by the pre-sleep owner.
            audoutFlushAudioOutBuffers(&flushed);
            g_audio_quiesce_completed.store(
                g_audio_quiesce_request.load(std::memory_order_acquire),
                std::memory_order_release);
        }

        u32 play_serial = 0;
        do {
            play_serial = g_transition_serial.load(std::memory_order_acquire);
            ApplyPendingState();
        } while (g_transition_serial.load(std::memory_order_acquire) != play_serial);

        char play_path[applet_bgm::PathSizeMax]{};
        u32 resume_frame = 0;
        u64 playing_state = applet_bgm::SilentTitleId;
        u32 playing_generation = 0;
        u32 playing_startup_generation = 0;
        {
            std::scoped_lock lk(g_mutex);
            if (!g_output_paused && g_active_session && !g_active_session->paused) {
                const auto* track = g_active_session->Current();
                if (track) {
                    std::snprintf(play_path, sizeof(play_path), "%s", track->path);
                    resume_frame = g_active_session->resume_frame;
                    playing_state = g_active_state;
                    playing_generation = g_active_session->generation;
                    if (applet_bgm::IsStartupState(playing_state)) {
                        playing_startup_generation =
                            g_startup_generation.load(std::memory_order_acquire);
                    }
                    std::snprintf(g_playing_path, sizeof(g_playing_path), "%s", play_path);
                    g_status = PlayerStatus::Playing;
                }
            }
        }

        if (play_path[0] == '\0') {
            if (!g_master_enabled) {
                home_source_cache.Clear();
            }
            g_status = PlayerStatus::FetchNext;
            bool startup_exhausted = false;
            u32 startup_generation = 0;
            {
                std::scoped_lock lk(g_mutex);
                startup_exhausted =
                    applet_bgm::IsStartupState(g_active_state) &&
                    (!g_active_session || g_active_session->Current() == nullptr);
                if (startup_exhausted) {
                    startup_generation =
                        g_startup_generation.load(std::memory_order_acquire);
                }
            }
            if (startup_exhausted) {
                CompleteStartup(startup_generation);
            }
            svcSleepThread(50'000'000ULL);
            continue;
        }

        const auto playback = PlayTrack(
            play_path, resume_frame, play_serial, playing_generation,
            playing_state,
            playing_state == applet_bgm::QlaunchTitleId
                ? &home_source_cache : nullptr);
        bool startup_finished = false;
        {
            std::scoped_lock lk(g_mutex);
            g_playing_path[0] = '\0';
            g_status = PlayerStatus::FetchNext;

            if (g_active_session && g_active_state == playing_state &&
                g_active_session->generation == playing_generation) {
                const auto* current = g_active_session->Current();
                if (current && std::strcmp(current->path, play_path) == 0) {
                    if (playback.end == PlaybackEnd::Interrupted) {
                        g_active_session->resume_frame = playback.current_frame;
                    } else if (applet_bgm::IsStartupState(playing_state)) {
                        g_active_session->finished = true;
                        startup_finished = true;
                    } else if (playback.end == PlaybackEnd::Natural) {
                        g_active_session->Advance();
                    } else {
                        auto* failed = g_active_session->Current();
                        if (failed) {
                            failed->valid = false;
                        }
                        g_active_session->Advance(true);
                    }
                }
            }
        }

        if (startup_finished) {
            CompleteStartup(playing_startup_generation);
        }
    }

    audoutStopAudioOut();
    audoutExit();
}

void GpioThreadFunc(void* ptr) {
    auto* session = static_cast<GpioPadSession*>(ptr);
    bool pre_unplug_pause = false;
    GpioValue old_value = GpioValue_High;

    while (g_should_run) {
        GpioValue value;
        if (R_SUCCEEDED(gpioPadGetValue(session, &value))) {
            if (old_value == GpioValue_Low && value == GpioValue_High) {
                pre_unplug_pause = g_output_paused;
                if (!pre_unplug_pause) {
                    g_output_paused = true;
                    SignalTransition();
                }
            } else if (old_value == GpioValue_High && value == GpioValue_Low) {
                if (!pre_unplug_pause) {
                    g_output_paused = false;
                    SignalTransition();
                }
            }
            old_value = value;
        }
        svcSleepThread(10'000'000);
    }
}

void PmdmntThreadFunc(void*) {
    bool enabled = config::get_applet_bgm_enabled();
    u64 current_target = UINT64_MAX;
    bool sleeping = false;
    bool previous_loading_active = false;
    bool loading_start_tracking = false;
    bool loading_music_started = false;
    bool loading_end_holding = false;
    u64 loading_started_tick = 0;
    u64 loading_ended_tick = 0;
    u64 tracked_library_applet = applet_bgm::SilentTitleId;
    u64 tracked_library_applet_pid = 0;
    bool library_applet_home_override = false;
    bool pending_home_fallback = false;
    bool pending_home_fallback_value = false;
    u64 pending_home_fallback_tick = 0;
    u32 last_home_short_press_count = 0;
    u32 last_library_focus_update_count = 0;
    // The initial power-on event belongs to cold boot, whose Startup Sound is
    // already claimed in Initialize(). A real sleep event unlatches wake.
    bool wake_latched = true;
    HomeSceneGate home_scene_gate = HomeSceneGate::BootOrWake;
    bool gated_home_seen = false;
    u64 gated_home_since_tick = 0;
    u32 last_coordinated_transition = 0;
    const auto reset_loading_timing = [&]() {
        previous_loading_active = false;
        loading_start_tracking = false;
        loading_music_started = false;
        loading_end_holding = false;
        loading_started_tick = 0;
        loading_ended_tick = 0;
    };

    while (g_should_run) {
        const bool reload = g_applet_bgm_reload.exchange(false);
        if (reload) {
            enabled = config::get_applet_bgm_enabled();
            g_master_enabled = enabled;
            current_target = UINT64_MAX;
            reset_loading_timing();
            if (!enabled) {
                CancelStartupAndRequest(applet_bgm::SilentTitleId);
            }
        }

        u64 application_pid = 0;
        u64 application_tid = 0;
        pm::getCurrentPidTid(&application_pid, &application_tid);

        ui_activity::Poll(
            application_pid, application_tid,
            g_quick_settings_hold_ms.load(std::memory_order_acquire));
        const auto ui_activity_snapshot = ui_activity::GetSnapshot();
        const bool loading_active =
            ui_activity_snapshot.availability ==
                ui_activity::Availability::Active &&
            ui_activity_snapshot.application_loading;
        const auto routing_tick = armGetSystemTick();
        const auto loading_start_delay_ns = static_cast<u64>(
            g_loading_start_delay_ms.load(std::memory_order_acquire)) *
            1'000'000ULL;
        const auto loading_end_delay_ns = static_cast<u64>(
            g_loading_end_delay_ms.load(std::memory_order_acquire)) *
            1'000'000ULL;

        if (loading_active) {
            if (!previous_loading_active) {
                const bool continues_end_hold = loading_end_holding &&
                    loading_end_delay_ns != 0 &&
                    armTicksToNs(routing_tick - loading_ended_tick) <
                        loading_end_delay_ns;
                if (!continues_end_hold) {
                    loading_start_tracking = true;
                    loading_music_started = false;
                    loading_started_tick = routing_tick;
                }
                loading_end_holding = false;
                loading_ended_tick = 0;
            }
            if (loading_start_tracking && !loading_music_started &&
                armTicksToNs(routing_tick - loading_started_tick) >=
                    loading_start_delay_ns) {
                loading_start_tracking = false;
                loading_music_started = true;
            }
        } else if (previous_loading_active) {
            loading_start_tracking = false;
            if (loading_music_started && loading_end_delay_ns != 0) {
                loading_end_holding = true;
                loading_ended_tick = routing_tick;
            } else {
                loading_music_started = false;
                loading_end_holding = false;
                loading_started_tick = 0;
                loading_ended_tick = 0;
            }
        } else if (loading_end_holding &&
            (loading_end_delay_ns == 0 ||
             armTicksToNs(routing_tick - loading_ended_tick) >=
                loading_end_delay_ns)) {
            loading_music_started = false;
            loading_end_holding = false;
            loading_started_tick = 0;
            loading_ended_tick = 0;
        }
        const bool loading_music_active = loading_music_started &&
            (loading_active || loading_end_holding);
        const bool loading_start_pending =
            loading_active && !loading_music_started;
        previous_loading_active = loading_active;
        const bool application_out_of_focus =
            ui_activity_snapshot.availability ==
                ui_activity::Availability::Active &&
            ui_activity_snapshot.application_out_of_focus;

        u64 target_pid = 0;
        u64 process_target = applet_bgm::SilentTitleId;
        pm::getAppletBgmTarget(
            &target_pid, &process_target, application_out_of_focus);

        const auto detected_process_target = process_target;
        if (applet_bgm::IsDetectedAppletTitleId(
                detected_process_target)) {
            if (tracked_library_applet != detected_process_target ||
                tracked_library_applet_pid != target_pid) {
                tracked_library_applet = detected_process_target;
                tracked_library_applet_pid = target_pid;
                pending_home_fallback = false;
                last_home_short_press_count =
                    ui_activity_snapshot.home_short_press_count;
                last_library_focus_update_count =
                    ui_activity_snapshot.library_applet_focus_update_count;
                const bool matching_focus_signal =
                    ui_activity_snapshot.has_library_applet_signal &&
                    ui_activity_snapshot.library_applet_program_id ==
                        detected_process_target;
                const bool focus_signal_is_fresh = matching_focus_signal &&
                    ui_activity_snapshot.last_library_applet_event_tick != 0 &&
                    armTicksToNs(
                        routing_tick -
                        ui_activity_snapshot.last_library_applet_event_tick) <=
                        LIBRARY_FOCUS_SIGNAL_FRESH_NS;
                // A PDM focus record has no process ID. Do not let an old
                // OutOfFocus record from a previous instance make a newly
                // opened applet look like Home until its new InFocus record
                // arrives.
                library_applet_home_override = focus_signal_is_fresh &&
                    !ui_activity_snapshot.library_applet_foreground;
            } else {
                const bool focus_updated =
                    ui_activity_snapshot.library_applet_focus_update_count !=
                        last_library_focus_update_count;
                if (focus_updated) {
                    last_library_focus_update_count =
                        ui_activity_snapshot.library_applet_focus_update_count;
                    if (ui_activity_snapshot.library_applet_program_id ==
                        detected_process_target) {
                        library_applet_home_override =
                            !ui_activity_snapshot.library_applet_foreground;
                        pending_home_fallback = false;
                    }
                }

                if (ui_activity_snapshot.home_short_press_count !=
                    last_home_short_press_count) {
                    last_home_short_press_count =
                        ui_activity_snapshot.home_short_press_count;
                    bool focus_event_matches_press = false;
                    if (ui_activity_snapshot.has_library_applet_signal &&
                        ui_activity_snapshot.library_applet_program_id ==
                            detected_process_target &&
                        ui_activity_snapshot.last_home_short_press_tick != 0 &&
                        ui_activity_snapshot.last_library_applet_event_tick !=
                            0) {
                        const auto first_tick = std::min(
                            ui_activity_snapshot.last_home_short_press_tick,
                            ui_activity_snapshot.last_library_applet_event_tick);
                        const auto last_tick = std::max(
                            ui_activity_snapshot.last_home_short_press_tick,
                            ui_activity_snapshot.last_library_applet_event_tick);
                        focus_event_matches_press =
                            armTicksToNs(last_tick - first_tick) <=
                                500'000'000ULL;
                    }
                    if (!focus_event_matches_press) {
                        pending_home_fallback = true;
                        pending_home_fallback_value =
                            !library_applet_home_override;
                        pending_home_fallback_tick = routing_tick;
                    }
                }

                if (pending_home_fallback &&
                    armTicksToNs(
                        routing_tick - pending_home_fallback_tick) >=
                        200'000'000ULL) {
                    library_applet_home_override =
                        pending_home_fallback_value;
                    pending_home_fallback = false;
                }
            }

            if (library_applet_home_override) {
                process_target = applet_bgm::QlaunchTitleId;
            }
        } else {
            tracked_library_applet = applet_bgm::SilentTitleId;
            tracked_library_applet_pid = 0;
            library_applet_home_override = false;
            pending_home_fallback = false;
            last_home_short_press_count =
                ui_activity_snapshot.home_short_press_count;
            last_library_focus_update_count =
                ui_activity_snapshot.library_applet_focus_update_count;
        }

        const bool loading_can_own =
            process_target == applet_bgm::QlaunchTitleId ||
            (process_target == applet_bgm::SilentTitleId &&
             target_pid != 0 && target_pid == application_pid);
        if (loading_can_own && loading_music_active) {
            process_target = applet_bgm::LoadingStateId;
        } else if (loading_can_own && loading_start_pending) {
            // Do not let Home OST fill the intentional pre-loading delay.
            process_target = applet_bgm::SilentTitleId;
        } else if (process_target == applet_bgm::QlaunchTitleId &&
            ui_activity_snapshot.availability ==
                ui_activity::Availability::Active &&
            ui_activity_snapshot.application_handoff_active) {
            // Keep multi-program and other in-game hand-offs silent. They can
            // briefly vacate the application slot without displaying HOME.
            process_target = applet_bgm::SilentTitleId;
        }
        const auto scene_snapshot = qlaunch_scene::GetSceneSnapshot();

        // Keep the late PDM cursor current even while PSC is healthy. If the
        // coordinated observer ever fails mid-cycle, the fallback can then
        // consume the next real event instead of re-baselining past it and
        // leaving the pre-sleep audio hold latched.
        PowerTransitions pdm_power_transitions{};
        if (g_pdmqry_available) {
            ConsumePowerTransitions(&pdm_power_transitions);
        }

        PowerTransitions power_transitions{};
        bool coordinated_power_transition = false;
        const auto power_snapshot = power_state::GetSnapshot();
        if (power_snapshot.transition_count !=
            last_coordinated_transition) {
            // Consume the last signal even if PSC failed immediately after
            // publishing it; otherwise an acknowledged wake could leave the
            // safety hold latched while the PDM fallback re-baselines.
            last_coordinated_transition =
                power_snapshot.transition_count;
            coordinated_power_transition = true;
            if (power_snapshot.last_transition ==
                power_state::Transition::Sleep) {
                power_transitions.last = PowerTransition::Sleep;
                power_transitions.saw_sleep = true;
            } else if (power_snapshot.last_transition ==
                       power_state::Transition::Wake) {
                power_transitions.last = PowerTransition::Wake;
                power_transitions.saw_wake = true;
            }
        } else if (power_snapshot.availability !=
                   power_state::Availability::Active) {
            // The play-event database is deliberately only a fallback. Its
            // entries arrive after qlaunch begins its transition, which is too
            // late to prevent a short false Home selection on its own.
            power_transitions = pdm_power_transitions;
        }

        if (power_transitions.last == PowerTransition::Sleep) {
            ui_activity::CloseQuickSettings();
            reset_loading_timing();
            sleeping = true;
            wake_latched = false;
            home_scene_gate = HomeSceneGate::None;
            gated_home_seen = false;
            current_target = applet_bgm::SilentTitleId;
            // Display-off and system-sleep are explicit silent states. Flush
            // queued buffers as well as changing ownership, otherwise a stale
            // Home buffer can play immediately after resume.
            if (!coordinated_power_transition) {
                CancelStartupAndRequest(applet_bgm::SilentTitleId, true);
            }
        } else if (power_transitions.last == PowerTransition::Wake) {
            ui_activity::CloseQuickSettings();
            reset_loading_timing();
            process_target = applet_bgm::SilentTitleId;
            const bool new_wake = coordinated_power_transition ||
                !wake_latched || power_transitions.saw_sleep;
            sleeping = false;
            if (new_wake) {
                wake_latched = true;
                home_scene_gate = HomeSceneGate::BootOrWake;
                gated_home_seen = false;
                current_target = applet_bgm::SilentTitleId;
                RefreshLockScreenSetting();

                const auto wake_startup_state =
                    g_separate_wake_playlist.load(std::memory_order_acquire)
                    ? applet_bgm::WakeStartupTitleId
                    : applet_bgm::StartupTitleId;
                if (enabled && g_startup_on_wake &&
                    config::get_ost_playlist_size(wake_startup_state) != 0) {
                    BeginStartup(wake_startup_state, true);
                } else {
                    CancelStartupAndRequest(
                        applet_bgm::SilentTitleId, true);
                }
            }
            // PSC keeps all routing silent until this thread has installed the
            // wake/startup owner and the boot/wake scene guard.
            if (IsPowerAudioHoldActive()) {
                ReleasePowerAudioHold();
            }
        }

        const bool power_audio_hold = IsPowerAudioHoldActive();
        u64 target = sleeping || power_audio_hold
            ? applet_bgm::SilentTitleId
            : ResolveQlaunchState(process_target, scene_snapshot);

        if (!sleeping && !power_audio_hold) {
            const bool scene_ready =
                scene_snapshot.availability ==
                qlaunch_scene::SceneAvailability::Ready;
            const bool scene_is_home = scene_ready &&
                scene_snapshot.scene == applet_bgm::QlaunchSceneHome;
            const bool scene_is_lock = scene_ready &&
                scene_snapshot.scene == applet_bgm::QlaunchSceneLock;
            const bool scene_is_settings = scene_ready &&
                scene_snapshot.scene == applet_bgm::QlaunchSceneSettings;

            if (scene_is_lock || scene_is_settings) {
                // A concrete non-Home scene proves that boot/wake has reached
                // qlaunch. It also overrides an applet process that remains
                // alive behind the visible Lock or Settings scene.
                home_scene_gate = HomeSceneGate::None;
                gated_home_seen = false;
            } else if (home_scene_gate == HomeSceneGate::BootOrWake &&
                       g_lock_screen_enabled.load(std::memory_order_acquire) &&
                       scene_snapshot.availability !=
                           qlaunch_scene::SceneAvailability::Unavailable) {
                // When Horizon says a Lock screen is enabled, a boot/wake Home
                // report is known to be an intermediate scene. Wait for the
                // concrete 0x0a Lock report instead of guessing by duration.
                gated_home_seen = false;
                target = applet_bgm::SilentTitleId;
            } else if (home_scene_gate == HomeSceneGate::BootOrWake) {
                // Consoles with the Lock screen disabled have no concrete
                // qlaunch scene between boot/wake and Home, so retain a narrow
                // fallback only for that configuration.
                const bool can_measure_stability = scene_is_home ||
                    scene_snapshot.availability ==
                        qlaunch_scene::SceneAvailability::Unavailable;
                if (can_measure_stability) {
                    const auto now = armGetSystemTick();
                    if (!gated_home_seen) {
                        gated_home_seen = true;
                        gated_home_since_tick = now;
                    }

                    if (armTicksToNs(now - gated_home_since_tick) <
                        BOOT_WAKE_HOME_STABLE_NS) {
                        target = applet_bgm::SilentTitleId;
                    } else {
                        home_scene_gate = HomeSceneGate::None;
                        gated_home_seen = false;
                    }
                } else {
                    // Waiting and unknown transient scenes are not proof that
                    // Home is visible. Keep playback silent and restart the
                    // stability clock when a usable scene arrives.
                    gated_home_seen = false;
                    target = applet_bgm::SilentTitleId;
                }
            }
        }
        g_detected_state = target;

        if (enabled) {
            if (sleeping || power_audio_hold) {
                current_target = applet_bgm::SilentTitleId;
            } else if (g_startup_active) {
                // Startup can span the boot/wake Lock Screen, but opening
                // Settings, another applet, or a game ends it immediately.
                const bool awaiting_boot_or_wake_scene =
                    home_scene_gate == HomeSceneGate::BootOrWake;
                if (!awaiting_boot_or_wake_scene &&
                    !StartupMayContinue(process_target, target)) {
                    CancelStartupAndRequest(target);
                }
            } else if (target != current_target) {
                RequestState(target);
            }
            current_target = target;
        }

        svcSleepThread(enabled ? 50'000'000 : 100'000'000);
    }
}

u32 PrepareForPowerSleep() {
    g_power_audio_hold.store(true, std::memory_order_release);
    auto request = g_audio_quiesce_request.fetch_add(
        1, std::memory_order_acq_rel) + 1;
    if (request == 0) {
        request = g_audio_quiesce_request.fetch_add(
            1, std::memory_order_acq_rel) + 1;
    }
    CancelStartupAndRequest(applet_bgm::SilentTitleId, true);
    return request;
}

bool WaitForPowerAudioQuiesced(u32 request, u64 timeout_ns) {
    const auto start = armGetSystemTick();
    while (g_should_run.load(std::memory_order_acquire)) {
        if (g_audio_quiesce_completed.load(std::memory_order_acquire) ==
            request) {
            return true;
        }
        if (armTicksToNs(armGetSystemTick() - start) >= timeout_ns) {
            break;
        }
        svcSleepThread(1'000'000ULL);
    }
    return g_audio_quiesce_completed.load(std::memory_order_acquire) ==
        request;
}

void NotifyPowerWake() {
    // The Pmdmnt thread releases this only after it has armed Startup/Lock
    // ownership. Keeping the hold across resume prevents stale state routing.
    g_power_audio_hold.store(true, std::memory_order_release);
}

void ReleasePowerAudioHold() {
    g_power_audio_hold.store(false, std::memory_order_release);
}

bool IsPowerAudioHoldActive() {
    return g_power_audio_hold.load(std::memory_order_acquire);
}

bool IsLockScreenEnabled() {
    return g_lock_screen_enabled.load(std::memory_order_acquire);
}

bool GetStatus() {
    if (g_output_paused || !g_master_enabled) {
        return false;
    }
    std::scoped_lock lk(g_mutex);
    return g_active_session && !g_active_session->paused &&
           g_active_session->Current();
}

void Play() {
    bool changed = false;
    {
        std::scoped_lock lk(g_mutex);
        if (g_active_session) {
            changed = g_active_session->paused;
            g_active_session->paused = false;
            if (g_active_session->finished &&
                !applet_bgm::IsStartupState(g_active_state) &&
                g_active_session->count != 0) {
                g_active_session->position = 0;
                g_active_session->resume_frame = 0;
                g_active_session->finished = false;
                changed = true;
            }
        }
    }
    if (changed) {
        SignalTransition();
    }
}

void Pause() {
    bool changed = false;
    {
        std::scoped_lock lk(g_mutex);
        if (g_active_session && !g_active_session->paused) {
            g_active_session->paused = true;
            changed = true;
        }
    }
    if (changed) {
        SignalTransition();
    }
}

void Next() {
    bool changed = false;
    {
        std::scoped_lock lk(g_mutex);
        if (g_active_session &&
            !applet_bgm::IsStartupState(g_active_state)) {
            changed = g_active_session->Advance(true);
            if (changed) {
                g_active_session->generation = NextSessionGenerationLocked();
                g_active_session->paused = false;
            }
        }
    }
    if (changed) {
        SignalTransition();
    }
}

void Prev() {
    bool changed = false;
    {
        std::scoped_lock lk(g_mutex);
        if (g_active_session &&
            !applet_bgm::IsStartupState(g_active_state)) {
            changed = g_active_session->Previous();
            if (changed) {
                g_active_session->generation = NextSessionGenerationLocked();
                g_active_session->paused = false;
            }
        }
    }
    if (changed) {
        SignalTransition();
    }
}

float GetVolume() {
    return g_global_volume.load(std::memory_order_acquire);
}

void SetVolume(float volume) {
    volume = std::clamp(volume, 0.f, VOLUME_MAX);
    g_global_volume.store(volume, std::memory_order_release);
    config::set_volume(volume);
}

float GetTitleVolume() {
    return g_state_volume.load(std::memory_order_acquire);
}

void SetTitleVolume(float volume) {
    volume = std::clamp(volume, 0.f, VOLUME_MAX);
    u64 state = applet_bgm::SilentTitleId;
    {
        std::scoped_lock lk(g_mutex);
        state = g_active_state;
    }
    if (state != applet_bgm::SilentTitleId) {
        config::set_ost_volume(state, volume);
        g_state_volume.store(volume, std::memory_order_release);
    }
}

float GetDefaultTitleVolume() {
    return g_default_title_volume;
}

void SetDefaultTitleVolume(float volume) {
    g_default_title_volume = std::clamp(volume, 0.f, VOLUME_MAX);
    config::set_default_title_volume(g_default_title_volume);
}

void TitlePlay() {
    Play();
}

void TitlePause() {
    Pause();
}

void DefaultTitlePlay() {
    Play();
}

void DefaultTitlePause() {
    Pause();
}

RepeatMode GetRepeatMode() {
    std::scoped_lock lk(g_mutex);
    return g_active_session ? g_active_session->repeat : RepeatMode::Off;
}

void SetRepeatMode(RepeatMode mode) {
    mode = static_cast<RepeatMode>(std::clamp(
        static_cast<int>(mode), static_cast<int>(RepeatMode::Off),
        static_cast<int>(RepeatMode::All)));
    u64 state = applet_bgm::SilentTitleId;
    bool restart = false;
    {
        std::scoped_lock lk(g_mutex);
        state = ActiveEditableStateLocked();
        if (state != applet_bgm::SilentTitleId) {
            g_active_session->repeat = mode;
            if (mode != RepeatMode::Off && g_active_session->finished &&
                g_active_session->count != 0) {
                g_active_session->position = 0;
                g_active_session->resume_frame = 0;
                g_active_session->finished = false;
                restart = true;
            }
        }
    }
    if (state != applet_bgm::SilentTitleId) {
        config::set_ost_repeat(state, static_cast<int>(mode));
    }
    if (restart) {
        SignalTransition();
    }
}

ShuffleMode GetShuffleMode() {
    std::scoped_lock lk(g_mutex);
    return g_active_session ? g_active_session->shuffle : ShuffleMode::Off;
}

u64 GetActiveOstState() {
    std::scoped_lock lk(g_mutex);
    return g_active_state;
}

void SetShuffleMode(ShuffleMode mode) {
    u64 state = applet_bgm::SilentTitleId;
    bool changed = false;
    {
        std::scoped_lock lk(g_mutex);
        state = ActiveEditableStateLocked();
        changed = state != applet_bgm::SilentTitleId &&
                  g_active_session->shuffle != mode;
    }
    if (changed) {
        config::set_ost_shuffle(state, mode == ShuffleMode::On);
        ReloadOstState(state);
    }
}

u32 GetPlaylistSize() {
    std::scoped_lock lk(g_mutex);
    return g_active_session ? g_active_session->count : 0;
}

Result GetPlaylistItem(u32 index, char* buffer, size_t buffer_size) {
    std::scoped_lock lk(g_mutex);
    R_UNLESS(g_active_session && index < g_active_session->count, tune::OutOfRange);
    const auto track_index = g_active_session->order[index];
    R_UNLESS(track_index < g_active_session->tracks.size(), tune::OutOfRange);
    std::snprintf(buffer, buffer_size, "%s", g_active_session->tracks[track_index].path);
    return 0;
}

Result GetCurrentQueueItem(CurrentStats* out, char* buffer, size_t buffer_size) {
    std::scoped_lock lk(g_mutex);
    R_UNLESS(g_source != nullptr && g_source->IsOpen(), tune::NotPlaying);
    R_UNLESS(g_playing_path[0] != '\0', tune::NotPlaying);
    std::snprintf(buffer, buffer_size, "%s", g_playing_path);

    const auto [current, total] = g_source->Tell();
    out->sample_rate = g_source->GetSampleRate();
    out->current_frame = current;
    out->total_frames = total;
    return 0;
}

void ClearQueue() {
    u64 state;
    {
        std::scoped_lock lk(g_mutex);
        state = ActiveEditableStateLocked();
    }
    if (state != applet_bgm::SilentTitleId) {
        config::clear_ost_playlist(state);
        ReloadOstState(state);
    }
}

void MoveQueueItem(u32 src, u32 dst) {
    u64 state;
    {
        std::scoped_lock lk(g_mutex);
        state = ActiveEditableStateLocked();
    }
    if (state != applet_bgm::SilentTitleId &&
        config::move_ost_playlist_item(state, src, dst)) {
        ReloadOstState(state);
    }
}

void Select(u32 index) {
    bool changed = false;
    {
        std::scoped_lock lk(g_mutex);
        if (g_active_session && index < g_active_session->count &&
            !applet_bgm::IsStartupState(g_active_state)) {
            g_active_session->position = index;
            g_active_session->resume_frame = 0;
            g_active_session->generation = NextSessionGenerationLocked();
            g_active_session->paused = false;
            g_active_session->finished = false;
            changed = true;
        }
    }
    if (changed) {
        SignalTransition();
    }
}

void Seek(u32 position) {
    std::scoped_lock lk(g_mutex);
    if (g_source && g_source->IsOpen()) {
        g_source->Seek(position);
    }
}

Result Enqueue(const char* buffer, size_t, EnqueueType) {
    u64 state;
    {
        std::scoped_lock lk(g_mutex);
        state = ActiveEditableStateLocked();
    }
    R_UNLESS(state != applet_bgm::SilentTitleId, tune::InvalidPath);
    R_UNLESS(IsPlayablePath(buffer), tune::InvalidPath);
    R_UNLESS(config::append_ost_playlist_item(state, buffer), tune::OutOfMemory);
    ReloadOstState(state);
    return 0;
}

Result Remove(u32 index) {
    u64 state;
    {
        std::scoped_lock lk(g_mutex);
        state = ActiveEditableStateLocked();
    }
    R_UNLESS(state != applet_bgm::SilentTitleId, tune::QueueEmpty);
    R_UNLESS(config::remove_ost_playlist_item(state, index), tune::OutOfRange);
    ReloadOstState(state);
    return 0;
}

void ReloadAppletBgm() {
    g_applet_bgm_reload = true;
}

void ReloadOstState(u64 title_id) {
    bool active = false;
    bool kept_current = false;
    bool reconciled = false;
    {
        std::scoped_lock lk(g_mutex);
        active = g_active_state == title_id;

        // Rebuild an edited playlist around its current file. This can
        // update the queue without stopping the decoder that is already
        // feeding audio. HOME is also reconciled while retained behind another
        // state so its cached decoder/position survives playlist edits.
        if (title_id == applet_bgm::QlaunchTitleId &&
            g_home_initialized) {
            kept_current = ReconcileSessionLocked(
                g_home_session, title_id, false);
            reconciled = true;
        } else if (active && g_active_session) {
            kept_current = ReconcileSessionLocked(
                *g_active_session, title_id,
                applet_bgm::IsStartupState(title_id));
            reconciled = true;
        }

        if (title_id == applet_bgm::QlaunchTitleId && !reconciled) {
            g_home_initialized = false;
        }
    }

    if (active && reconciled) {
        // If the current file disappeared, interrupt it and begin the newly
        // loaded queue. Otherwise the existing PlayTrack call remains valid.
        if (!kept_current) {
            SignalTransition();
        }
    } else if (active) {
        g_force_reload_state = title_id;
        g_force_reload_pending = true;
        SignalTransition();
    }
}

void ReloadOstMisc() {
    g_fade_in_ms = config::get_fade_in_ms();
    g_fade_out_ms = config::get_fade_out_ms();
    g_mid_song_fade_in_ms = config::get_mid_song_fade_in_ms();
    g_mid_song_fade_out_ms = config::get_mid_song_fade_out_ms();
    g_loading_start_delay_ms = config::get_loading_start_delay_ms();
    g_loading_end_delay_ms = config::get_loading_end_delay_ms();
    g_quick_settings_hold_ms = config::get_quick_settings_hold_ms();
    g_startup_on_wake = config::get_startup_on_wake();
    g_separate_wake_playlist = config::get_separate_wake_playlist();
    g_album_video_volume = config::get_album_video_volume();
    g_quick_settings_volume = config::get_quick_settings_volume();
    g_quick_settings_enabled = config::get_quick_settings_enabled();
    std::scoped_lock lk(g_mutex);
    g_state_volume.store(
        g_active_state == applet_bgm::SilentTitleId
            ? 1.f : config::get_ost_volume(g_active_state),
        std::memory_order_release);
}

} // namespace tune::impl
