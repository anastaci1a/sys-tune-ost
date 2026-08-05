#include "music_player.hpp"

#include "applet_bgm.hpp"
#include "../album_video_observer.hpp"
#include "../qlaunch_scene_observer.hpp"
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
constexpr u64 RETURN_HOME_STABLE_NS = 1'200'000'000ULL;

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
    ReturnToHome,
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
    u32 position{};
    u32 generation{};

    void Clear() {
        source.reset();
        path[0] = '\0';
        position = 0;
        generation = 0;
    }

    bool Take(const char* requested_path, u32 requested_position,
              u32 requested_generation, u32 requested_frame,
              std::unique_ptr<Source>& out) {
        const bool matches = source &&
            position == requested_position &&
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
        position = 0;
        generation = 0;
        return true;
    }

    void Store(const char* cached_path, u32 cached_position,
               u32 cached_generation, std::unique_ptr<Source> cached_source) {
        Clear();
        std::snprintf(path, sizeof(path), "%s", cached_path);
        position = cached_position;
        generation = cached_generation;
        source = std::move(cached_source);
    }
};

LockableMutex g_mutex;
LockableMutex g_startup_mutex;

OstSession g_home_session;
OstSession g_applet_session;
OstSession g_startup_session;
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

std::atomic<u64> g_detected_state = applet_bgm::SilentTitleId;
std::atomic<u64> g_requested_state = applet_bgm::SilentTitleId;
std::atomic<u32> g_transition_serial = 1;

std::atomic_bool g_force_reload_pending = false;
std::atomic<u64> g_force_reload_state = applet_bgm::SilentTitleId;

std::atomic<u32> g_fade_in_ms = 500;
std::atomic<u32> g_fade_out_ms = 500;
std::atomic<u32> g_mid_song_fade_in_ms = 500;
std::atomic<u32> g_mid_song_fade_out_ms = 500;

std::atomic<float> g_global_volume = 1.f;
std::atomic<float> g_state_volume = 1.f;
std::atomic<float> g_album_video_volume = 1.f;
float g_default_title_volume = 1.f;

AudioOutBuffer g_audout_buffer[AUDIO_BUFFER_COUNT];
alignas(0x1000) s16 AudioMemoryPool[AUDIO_BUFFER_COUNT]
    [(AUDIO_BUFFER_SIZE + 0xFFF) & ~0xFFF];
static_assert((sizeof(AudioMemoryPool[0]) % 0x2000) == 0,
              "Audio Memory pool needs to be page aligned!");

bool g_pdmqry_available = false;

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
    // qlaunch can visibly cover a still-running game or library applet. Its
    // verified Lock/Settings scenes therefore take precedence over process
    // presence; Home does not, because the underlying applet may be in front.
    if (snapshot.availability == qlaunch_scene::SceneAvailability::Ready) {
        if (snapshot.scene == applet_bgm::QlaunchSceneLock) {
            return applet_bgm::LockStateId;
        }
        if (snapshot.scene == applet_bgm::QlaunchSceneSettings) {
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

void LoadSession(OstSession& session, u64 state, bool startup) {
    session = {};
    session.state = state;
    session.generation = ++g_session_generation;
    if (session.generation == 0) {
        session.generation = ++g_session_generation;
    }

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
        const auto chosen = static_cast<u32>(randomGet64() % session.count);
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
        if (!g_home_initialized || force_reload) {
            LoadSession(g_home_session, state, false);
            g_home_initialized = true;
        } else {
            // Policy can be edited while HOME is paused behind another applet.
            // Refresh repeat without rebuilding its retained queue/position.
            g_home_session.repeat =
                static_cast<RepeatMode>(config::get_ost_repeat(state));
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

Result IsApplicationOutOfFocus(u64 title_id, bool* out_of_focus) {
    static s32 last_total_entries = -1;
    static s32 last_end_entry_index = -1;
    static u64 last_title_id = 0;
    static bool last_out_of_focus = false;
    static Result last_result = 1;

    s32 total_entries = 0;
    s32 start_entry_index = 0;
    s32 end_entry_index = 0;
    Result rc = pdmqryGetAvailablePlayEventRange(
        &total_entries, &start_entry_index, &end_entry_index);
    if (R_FAILED(rc)) {
        return rc;
    }

    if (total_entries == last_total_entries &&
        end_entry_index == last_end_entry_index &&
        title_id == last_title_id) {
        if (R_SUCCEEDED(last_result)) {
            *out_of_focus = last_out_of_focus;
        }
        return last_result;
    }

    const bool had_cached_state = title_id == last_title_id && R_SUCCEEDED(last_result);
    const bool cached_out_of_focus = last_out_of_focus;

    last_total_entries = total_entries;
    last_end_entry_index = end_entry_index;
    last_title_id = title_id;

    constexpr s32 EventCount = 16;
    PdmPlayEvent events[EventCount]{};
    s32 count = 0;
    const s32 start = std::max(start_entry_index, end_entry_index - (EventCount - 1));

    rc = pdmqryQueryPlayEvent(start, events, EventCount, &count);
    if (R_FAILED(rc) || count == 0) {
        last_result = R_FAILED(rc) ? rc : 1;
        return last_result;
    }

    for (s32 i = count - 1; i >= 0; i--) {
        const auto& event = events[i];
        if (event.play_event_type != PdmPlayEventType_Applet ||
            event.event_data.applet.applet_id != AppletId_application) {
            continue;
        }

        union {
            u32 parts[2];
            u64 full;
        } event_title_id{};
        event_title_id.parts[0] = event.event_data.applet.program_id[1];
        event_title_id.parts[1] = event.event_data.applet.program_id[0];

        if (event_title_id.full != title_id &&
            event_title_id.full != (title_id & ~0xFFFULL)) {
            continue;
        }

        const auto event_type = event.event_data.applet.event_type;
        last_out_of_focus = event_type == PdmAppletEventType_OutOfFocus ||
                            event_type == PdmAppletEventType_OutOfFocus4;
        *out_of_focus = last_out_of_focus;
        last_result = 0;
        return 0;
    }

    if (had_cached_state) {
        last_out_of_focus = cached_out_of_focus;
        *out_of_focus = cached_out_of_focus;
        last_result = 0;
    } else {
        last_result = 1;
    }
    return last_result;
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
                         u32 playing_position, u32 playing_generation,
                         u64 playing_state,
                         PlaybackSourceCache* resume_cache) {
    std::unique_ptr<Source> source;
    const bool reused_source = resume_cache && resume_cache->Take(
        path, playing_position, playing_generation, resume_frame, source);
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
        const auto playback_gain =
            g_global_volume.load(std::memory_order_acquire) *
            g_state_volume.load(std::memory_order_acquire);
        ApplyGain(
            static_cast<s16*>(buffer->buffer), decoded_bytes,
            std::min(fade_in_start, fade_out_start),
            std::min(fade_in_end, fade_out_end),
            playback_gain * video_gain_start,
            playback_gain * video_gain_end);

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
        resume_cache->Store(path, playing_position, playing_generation,
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

    return 0;
}

void Exit() {
    g_should_run = false;
    SignalTransition();
}

void Finalize() {
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
        }

        u32 play_serial = 0;
        do {
            play_serial = g_transition_serial.load(std::memory_order_acquire);
            ApplyPendingState();
        } while (g_transition_serial.load(std::memory_order_acquire) != play_serial);

        char play_path[applet_bgm::PathSizeMax]{};
        u32 resume_frame = 0;
        u64 playing_state = applet_bgm::SilentTitleId;
        u32 playing_position = 0;
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
                    playing_position = g_active_session->position;
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
            play_path, resume_frame, play_serial, playing_position,
            playing_generation, playing_state,
            playing_state == applet_bgm::QlaunchTitleId
                ? &home_source_cache : nullptr);
        bool startup_finished = false;
        {
            std::scoped_lock lk(g_mutex);
            g_playing_path[0] = '\0';
            g_status = PlayerStatus::FetchNext;

            if (g_active_session && g_active_state == playing_state &&
                g_active_session->position == playing_position) {
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
    // The initial power-on event belongs to cold boot, whose Startup Sound is
    // already claimed in Initialize(). A real sleep event unlatches wake.
    bool wake_latched = true;
    HomeSceneGate home_scene_gate = HomeSceneGate::BootOrWake;
    bool gated_home_seen = false;
    u64 gated_home_since_tick = 0;
    bool has_previous_qlaunch_scene = false;
    u8 previous_qlaunch_scene = 0;
    u64 previous_process_target = UINT64_MAX;

    while (g_should_run) {
        const bool reload = g_applet_bgm_reload.exchange(false);
        if (reload) {
            enabled = config::get_applet_bgm_enabled();
            g_master_enabled = enabled;
            current_target = UINT64_MAX;
            if (!enabled) {
                CancelStartupAndRequest(applet_bgm::SilentTitleId);
            }
        }

        u64 application_pid = 0;
        u64 application_tid = 0;
        pm::getCurrentPidTid(&application_pid, &application_tid);

        bool application_out_of_focus = false;
        if (g_pdmqry_available && application_tid != 0 &&
            application_tid != applet_bgm::QlaunchTitleId) {
            bool out_of_focus = false;
            if (R_SUCCEEDED(IsApplicationOutOfFocus(application_tid, &out_of_focus))) {
                application_out_of_focus = out_of_focus;
            }
        }

        u64 target_pid = 0;
        u64 process_target = applet_bgm::SilentTitleId;
        pm::getAppletBgmTarget(
            &target_pid, &process_target, application_out_of_focus);
        const auto scene_snapshot = qlaunch_scene::GetSceneSnapshot();

        PowerTransitions power_transitions{};
        if (g_pdmqry_available) {
            ConsumePowerTransitions(&power_transitions);
        }

        if (power_transitions.last == PowerTransition::Sleep) {
            sleeping = true;
            wake_latched = false;
            home_scene_gate = HomeSceneGate::None;
            gated_home_seen = false;
            current_target = applet_bgm::SilentTitleId;
            // Display-off and system-sleep are explicit silent states. Flush
            // queued buffers as well as changing ownership, otherwise a stale
            // Home buffer can play immediately after resume.
            CancelStartupAndRequest(applet_bgm::SilentTitleId, true);
        } else if (power_transitions.last == PowerTransition::Wake) {
            const bool new_wake = !wake_latched || power_transitions.saw_sleep;
            sleeping = false;
            if (new_wake) {
                wake_latched = true;
                home_scene_gate = HomeSceneGate::BootOrWake;
                gated_home_seen = false;
                current_target = applet_bgm::SilentTitleId;

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
        }

        u64 target = sleeping
            ? applet_bgm::SilentTitleId
            : ResolveQlaunchState(process_target, scene_snapshot);

        if (!sleeping) {
            const bool scene_ready =
                scene_snapshot.availability ==
                qlaunch_scene::SceneAvailability::Ready;
            const bool scene_is_home = scene_ready &&
                scene_snapshot.scene == applet_bgm::QlaunchSceneHome;
            const bool scene_is_lock = scene_ready &&
                scene_snapshot.scene == applet_bgm::QlaunchSceneLock;
            const bool scene_is_settings = scene_ready &&
                scene_snapshot.scene == applet_bgm::QlaunchSceneSettings;
            const bool qlaunch_view_to_home = scene_is_home &&
                has_previous_qlaunch_scene &&
                previous_qlaunch_scene != applet_bgm::QlaunchSceneHome;
            const bool process_return_to_home =
                target == applet_bgm::QlaunchTitleId &&
                process_target == applet_bgm::QlaunchTitleId &&
                previous_process_target != UINT64_MAX &&
                previous_process_target != applet_bgm::QlaunchTitleId;

            if ((qlaunch_view_to_home || process_return_to_home) &&
                home_scene_gate == HomeSceneGate::None) {
                // A normal return to Home and the beginning of display-off
                // look identical at first: qlaunch reports Home, and an
                // applet/game may lose foreground ownership before the power
                // event is visible. Let a pending power event veto playback.
                home_scene_gate = HomeSceneGate::ReturnToHome;
                gated_home_seen = false;
            }

            if (scene_is_lock || scene_is_settings) {
                // A concrete non-Home scene proves that boot/wake has reached
                // qlaunch. It also overrides an applet process that remains
                // alive behind the visible Lock or Settings scene.
                home_scene_gate = HomeSceneGate::None;
                gated_home_seen = false;
            } else if (home_scene_gate != HomeSceneGate::None) {
                // At boot/wake the foreground process may still be Album, a
                // game, or another applet while qlaunch first reports Home and
                // then Lock. Gate every process target until Home is genuinely
                // stable, not only targets already identified as qlaunch.
                const bool can_measure_stability = scene_is_home ||
                    scene_snapshot.availability ==
                        qlaunch_scene::SceneAvailability::Unavailable;
                if (can_measure_stability) {
                    const auto now = armGetSystemTick();
                    if (!gated_home_seen) {
                        gated_home_seen = true;
                        gated_home_since_tick = now;
                    }

                    const auto required_stability =
                        home_scene_gate == HomeSceneGate::BootOrWake
                        ? BOOT_WAKE_HOME_STABLE_NS
                        : RETURN_HOME_STABLE_NS;
                    if (armTicksToNs(now - gated_home_since_tick) <
                        required_stability) {
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

            if (scene_ready) {
                previous_qlaunch_scene = scene_snapshot.scene;
                has_previous_qlaunch_scene = true;
            }
        }
        previous_process_target = process_target;
        g_detected_state = target;

        if (enabled) {
            if (sleeping) {
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
    {
        std::scoped_lock lk(g_mutex);
        if (title_id == applet_bgm::QlaunchTitleId) {
            g_home_initialized = false;
        }
        active = g_active_state == title_id;
    }

    if (active) {
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
    g_startup_on_wake = config::get_startup_on_wake();
    g_separate_wake_playlist = config::get_separate_wake_playlist();
    g_album_video_volume = config::get_album_video_volume();
    std::scoped_lock lk(g_mutex);
    g_state_volume.store(
        g_active_state == applet_bgm::SilentTitleId
            ? 1.f : config::get_ost_volume(g_active_state),
        std::memory_order_release);
}

} // namespace tune::impl
