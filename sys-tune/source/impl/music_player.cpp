#include "music_player.hpp"

#include "applet_bgm.hpp"
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

constexpr float VOLUME_MAX = 1.f;
constexpr auto AUDIO_FREQ = 48000;
constexpr auto AUDIO_CHANNEL_COUNT = 2;
constexpr auto AUDIO_BUFFER_COUNT = 2;
constexpr auto AUDIO_LATENCY_MS = 42;
constexpr auto AUDIO_BUFFER_SIZE =
    AUDIO_FREQ / 1000 * AUDIO_LATENCY_MS * AUDIO_CHANNEL_COUNT;
constexpr u8 PDM_POWER_STATE_SLEEP_MODE_OFF = 3;
constexpr s32 PDM_EVENT_BATCH_SIZE = 16;

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
std::atomic<u32> g_startup_generation = 0;
std::atomic_bool g_applet_bgm_reload = true;

std::atomic<u64> g_detected_state = applet_bgm::QlaunchTitleId;
std::atomic<u64> g_requested_state = applet_bgm::SilentTitleId;
std::atomic<u32> g_transition_serial = 1;

std::atomic_bool g_force_reload_pending = false;
std::atomic<u64> g_force_reload_state = applet_bgm::SilentTitleId;

std::atomic<u32> g_fade_in_ms = 500;
std::atomic<u32> g_fade_out_ms = 500;

float g_title_volume = 1.f;
float g_default_title_volume = 1.f;
bool g_use_title_volume = true;

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
        return;
    }

    if (state == applet_bgm::StartupTitleId) {
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

void BeginStartup() {
    std::scoped_lock lk(g_startup_mutex);
    g_startup_generation.fetch_add(1, std::memory_order_acq_rel);
    g_startup_active = true;

    // A wake always chooses a fresh random entry, including when the previous
    // Startup sound was still active as the console entered sleep.
    g_force_reload_state.store(
        applet_bgm::StartupTitleId, std::memory_order_release);
    g_force_reload_pending.store(true, std::memory_order_release);
    g_requested_state.store(
        applet_bgm::StartupTitleId, std::memory_order_release);
    SignalTransition();
}

void CancelStartupAndRequest(u64 state) {
    std::scoped_lock lk(g_startup_mutex);
    if (g_startup_active.exchange(false)) {
        // Invalidate completion from any playback that was already in flight.
        g_startup_generation.fetch_add(1, std::memory_order_acq_rel);
    }
    RequestState(state);
}

Result ConsumeSleepWakeEvent(bool* woke) {
    static s32 last_end_entry_index = -1;
    *woke = false;

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
            if (event.play_event_type == PdmPlayEventType_PowerStateChange &&
                event.event_data.power_state_change.value ==
                    PDM_POWER_STATE_SLEEP_MODE_OFF) {
                *woke = true;
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

void ApplyGain(s16* samples, size_t byte_count, float start_gain, float end_gain) {
    const auto frame_count = byte_count / (sizeof(s16) * AUDIO_CHANNEL_COUNT);
    if (frame_count == 0 || (start_gain >= 0.9999f && end_gain >= 0.9999f)) {
        return;
    }

    for (size_t frame = 0; frame < frame_count; frame++) {
        const float progress = frame_count > 1
            ? static_cast<float>(frame) / static_cast<float>(frame_count - 1)
            : 1.f;
        const float gain = std::clamp(
            start_gain + (end_gain - start_gain) * progress, 0.f, 1.f);
        for (size_t channel = 0; channel < AUDIO_CHANNEL_COUNT; channel++) {
            const auto index = frame * AUDIO_CHANNEL_COUNT + channel;
            samples[index] = static_cast<s16>(static_cast<float>(samples[index]) * gain);
        }
    }
}

PlaybackResult PlayTrack(const char* path, u32 resume_frame, u32 play_serial,
                         u32 playing_position, u32 playing_generation,
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

    while (g_should_run) {
        const bool interrupted = g_output_paused ||
            g_transition_serial.load(std::memory_order_acquire) != play_serial;
        if (interrupted && !transition_fade) {
            transition_fade = true;
            interrupted_resume_frame = source->Tell().first;
            transition_total_frames =
                static_cast<u64>(AUDIO_FREQ) * g_fade_out_ms.load() / 1000;
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
        if (decoded_bytes <= 0) {
            outcome = source->Done()
                ? PlaybackResult{0, PlaybackEnd::Natural, after.first}
                : PlaybackResult{tune::Generic, PlaybackEnd::Error, after.first};
            break;
        }

        const auto output_frames = static_cast<u64>(decoded_bytes) /
            (sizeof(s16) * AUDIO_CHANNEL_COUNT);
        const auto fade_in_frames =
            static_cast<u64>(AUDIO_FREQ) * g_fade_in_ms.load() / 1000;
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

        ApplyGain(static_cast<s16*>(buffer->buffer), decoded_bytes,
                  std::min(fade_in_start, fade_out_start),
                  std::min(fade_in_end, fade_out_end));

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
        g_active_state == applet_bgm::StartupTitleId) {
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
    SetVolume(config::get_volume());
    SetDefaultTitleVolume(config::get_default_title_volume());
    config::migrate_ost_config();
    ReloadOstMisc();

    g_master_enabled = config::get_applet_bgm_enabled();
    if (g_master_enabled &&
        config::get_ost_playlist_size(applet_bgm::StartupTitleId) != 0) {
        g_startup_active = true;
        g_requested_state = applet_bgm::StartupTitleId;
    } else if (g_master_enabled) {
        g_requested_state = applet_bgm::QlaunchTitleId;
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
                    if (playing_state == applet_bgm::StartupTitleId) {
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
                    g_active_state == applet_bgm::StartupTitleId &&
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
            playing_generation,
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
                    } else if (playing_state == applet_bgm::StartupTitleId) {
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
        u64 target = applet_bgm::SilentTitleId;
        pm::getAppletBgmTarget(&target_pid, &target, application_out_of_focus);
        g_detected_state = target;

        bool woke_from_sleep = false;
        if (g_pdmqry_available) {
            ConsumeSleepWakeEvent(&woke_from_sleep);
        }
        if (enabled && woke_from_sleep && g_startup_on_wake &&
            config::get_ost_playlist_size(applet_bgm::StartupTitleId) != 0) {
            BeginStartup();
        }

        if (enabled) {
            if (g_startup_active) {
                // Startup is allowed to finish only while HOME/qlaunch remains
                // in front, even when the same game or applet spans a wake.
                if (target != applet_bgm::QlaunchTitleId) {
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
                g_active_state != applet_bgm::StartupTitleId &&
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
        if (g_active_session && g_active_state != applet_bgm::StartupTitleId) {
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
        if (g_active_session && g_active_state != applet_bgm::StartupTitleId) {
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
    float volume = 1.f;
    audoutGetAudioOutVolume(&volume);
    return volume;
}

void SetVolume(float volume) {
    volume = std::clamp(volume, 0.f, VOLUME_MAX);
    audoutSetAudioOutVolume(volume);
    config::set_volume(volume);
}

float GetTitleVolume() {
    return g_title_volume;
}

void SetTitleVolume(float volume) {
    g_title_volume = std::clamp(volume, 0.f, VOLUME_MAX);
    g_use_title_volume = true;
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
            g_active_state != applet_bgm::StartupTitleId) {
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
    g_startup_on_wake = config::get_startup_on_wake();
}

} // namespace tune::impl
