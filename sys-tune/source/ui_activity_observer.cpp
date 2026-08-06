#include "ui_activity_observer.hpp"

#include "applet_bgm.hpp"

#include <algorithm>
#include <array>

namespace tune::ui_activity {

namespace {

constexpr u64 OverlayDispProgramId = 0x010000000000100CULL;
constexpr s32 EventBatchSize = 16;

Mutex g_info_mutex{};
TuneUiActivityObserverInfo g_info{};
bool g_available{};
s32 g_last_end_entry_index{-1};
u64 g_tracked_application_pid{};
u64 g_tracked_application_program_id{};
bool g_has_pending_application_event{};
u64 g_pending_application_program_id{};
u8 g_pending_application_event_type{};
bool g_pending_application_loading{};
bool g_application_session_started{};
bool g_application_backgrounded{};
bool g_application_internal_out_of_focus{};
bool g_application_slot_baselined{};
u32 g_update_count{};

bool ProgramIdsMatch(u64 event_program_id, u64 application_program_id) {
    return event_program_id == application_program_id ||
           event_program_id == (application_program_id & ~0xFFFULL);
}

u64 GetEventProgramId(const PdmPlayEvent& event) {
    union {
        u32 parts[2];
        u64 full;
    } program_id{};
    program_id.parts[0] = event.event_data.applet.program_id[1];
    program_id.parts[1] = event.event_data.applet.program_id[0];
    return program_id.full;
}

bool IsOutOfFocusEvent(u8 event_type) {
    return event_type == PdmAppletEventType_OutOfFocus ||
           event_type == PdmAppletEventType_OutOfFocus4;
}

bool IsExitEvent(u8 event_type) {
    return event_type == PdmAppletEventType_Exit ||
           event_type == PdmAppletEventType_Exit5 ||
           event_type == PdmAppletEventType_Exit6;
}

void SetLoadingLocked(bool active) {
    if (g_info.loading_active == active) {
        return;
    }
    g_info.loading_active = active;
    if (active) {
        ++g_info.loading_start_count;
    } else {
        ++g_info.loading_end_count;
    }
    ++g_update_count;
}

void SetQuickSettingsLocked(bool open) {
    if (g_info.quick_settings_open == open) {
        return;
    }
    g_info.quick_settings_open = open;
    if (open) {
        ++g_info.quick_settings_open_count;
    } else {
        ++g_info.quick_settings_close_count;
    }
    ++g_update_count;
}

void ApplyEventLocked(
    const PdmPlayEvent& event, u32 entry_index, bool allow_overlay_events) {
    if (event.play_event_type != PdmPlayEventType_Applet) {
        return;
    }

    const auto applet_id = event.event_data.applet.applet_id;
    const auto event_type = event.event_data.applet.event_type;
    const auto program_id = GetEventProgramId(event);

    ++g_info.event_count;
    g_info.last_event_type = event_type;
    g_info.last_applet_id = applet_id;
    g_info.last_event_index = entry_index;
    g_info.last_program_id = program_id;
    g_info.has_last_event = true;

    if (allow_overlay_events && applet_id == AppletId_OverlayApplet &&
        program_id == OverlayDispProgramId) {
        ++g_info.overlay_event_count;
        g_info.has_overlay_signal = true;
        if (event_type == PdmAppletEventType_InFocus) {
            SetQuickSettingsLocked(true);
        } else if (IsOutOfFocusEvent(event_type) || IsExitEvent(event_type)) {
            SetQuickSettingsLocked(false);
        }
    }

    if (applet_id != AppletId_application) {
        return;
    }

    ++g_info.application_event_count;
    g_info.has_application_signal = true;
    if (event_type == PdmAppletEventType_Launch) {
        ++g_info.application_launch_count;
    } else if (event_type == PdmAppletEventType_InFocus) {
        ++g_info.application_in_focus_count;
    } else if (event_type == PdmAppletEventType_OutOfFocus) {
        ++g_info.application_out_of_focus_count;
    } else if (event_type == PdmAppletEventType_OutOfFocus4) {
        ++g_info.application_background_count;
    } else if (IsExitEvent(event_type)) {
        ++g_info.application_exit_count;
    }

    const bool awaiting_application_process =
        g_tracked_application_pid == 0;
    const bool matches_current =
        !awaiting_application_process &&
        ProgramIdsMatch(program_id, g_tracked_application_program_id);
    const bool matches_pending =
        g_has_pending_application_event &&
        ProgramIdsMatch(program_id, g_pending_application_program_id);
    const bool begins_future_application =
        event_type == PdmAppletEventType_Launch && !matches_current;
    if (!awaiting_application_process && !matches_current &&
        !matches_pending && !begins_future_application) {
        return;
    }

    const bool launch_should_load =
        g_info.loading_active ||
        !g_application_session_started ||
        g_application_backgrounded;
    if (!matches_current) {
        // PDM can publish Launch/InFocus before pmdmnt exposes the new
        // application PID, including between programs in a multi-program
        // title. Retain both the lifecycle edge and whether it represents a
        // system launch or an in-game hand-off.
        g_has_pending_application_event = true;
        g_pending_application_program_id = program_id;
        g_pending_application_event_type = event_type;
        g_pending_application_loading =
            event_type == PdmAppletEventType_Launch && launch_should_load;
        g_info.application_program_id = program_id;
    }

    const bool another_application_is_pending =
        matches_current && g_has_pending_application_event &&
        !ProgramIdsMatch(
            g_pending_application_program_id,
            g_tracked_application_program_id);
    if (event_type == PdmAppletEventType_Launch) {
        g_application_session_started = true;
        g_application_backgrounded = false;
        g_application_internal_out_of_focus = false;
        g_info.application_out_of_focus = false;
        SetLoadingLocked(launch_should_load);
    } else if (event_type == PdmAppletEventType_InFocus) {
        if (!matches_current) {
            g_pending_application_loading = false;
        }
        if (!another_application_is_pending) {
            g_application_session_started = true;
            g_application_backgrounded = false;
            g_application_internal_out_of_focus = false;
            g_info.application_out_of_focus = false;
            SetLoadingLocked(false);
        }
    } else if (event_type == PdmAppletEventType_OutOfFocus) {
        if (!matches_current) {
            g_pending_application_loading = false;
        }
        if (!another_application_is_pending) {
            // FocusState::OutOfFocus is used for an applet or internal
            // hand-off. Known applet processes are routed separately; with no
            // applet target, keep game-native loading intervals silent.
            g_application_internal_out_of_focus = true;
            g_info.application_out_of_focus = false;
            SetLoadingLocked(false);
        }
    } else if (event_type == PdmAppletEventType_OutOfFocus4) {
        if (!matches_current) {
            g_pending_application_loading = false;
        }
        if (!another_application_is_pending) {
            // FocusState::Background is the real HOME/sleep path.
            g_application_backgrounded = true;
            g_application_internal_out_of_focus = false;
            g_info.application_out_of_focus = true;
            SetLoadingLocked(false);
        }
    } else if (IsExitEvent(event_type)) {
        if (!matches_current) {
            g_pending_application_loading = false;
        }
        if (!another_application_is_pending) {
            if (!g_application_internal_out_of_focus) {
                g_application_backgrounded = true;
            }
            g_info.application_out_of_focus = false;
            SetLoadingLocked(false);
        }
    }
}

Result ReadEvents(
    s32 first_entry_index, s32 end_entry_index,
    bool allow_overlay_events) {
    std::array<PdmPlayEvent, EventBatchSize> events{};
    auto next_entry_index = first_entry_index;
    while (next_entry_index <= end_entry_index) {
        const auto remaining = end_entry_index - next_entry_index + 1;
        const auto requested = std::min<s32>(EventBatchSize, remaining);
        s32 count = 0;
        const auto result = pdmqryQueryPlayEvent(
            next_entry_index, events.data(), requested, &count);
        if (R_FAILED(result)) {
            return result;
        }
        if (count <= 0) {
            break;
        }

        mutexLock(&g_info_mutex);
        ++g_info.query_count;
        for (s32 index = 0; index < count; ++index) {
            ApplyEventLocked(
                events[index], static_cast<u32>(next_entry_index + index),
                allow_overlay_events);
        }
        mutexUnlock(&g_info_mutex);
        next_entry_index += count;
    }
    return 0;
}

void SetStatus(TuneUiActivityObserverStatus status, Result result) {
    mutexLock(&g_info_mutex);
    g_info.status = status;
    g_info.last_result = result;
    mutexUnlock(&g_info_mutex);
}

}

void Initialize(bool pdm_available) {
    mutexLock(&g_info_mutex);
    g_info = {};
    g_info.status = pdm_available
        ? TuneUiActivityObserverStatus_Active
        : TuneUiActivityObserverStatus_Unavailable;
    mutexUnlock(&g_info_mutex);
    g_available = pdm_available;
    g_last_end_entry_index = -1;
    g_tracked_application_pid = 0;
    g_tracked_application_program_id = 0;
    g_has_pending_application_event = false;
    g_pending_application_program_id = 0;
    g_pending_application_event_type = 0;
    g_pending_application_loading = false;
    g_application_session_started = false;
    g_application_backgrounded = false;
    g_application_internal_out_of_focus = false;
    g_application_slot_baselined = false;
    g_update_count = 0;
}

void Poll(u64 application_process_id, u64 application_program_id) {
    if (!g_available) {
        return;
    }

    mutexLock(&g_info_mutex);
    const bool first_application_sample = !g_application_slot_baselined;
    g_application_slot_baselined = true;
    const bool application_changed =
        application_process_id != g_tracked_application_pid ||
        application_program_id != g_tracked_application_program_id;
    if (application_changed) {
        g_tracked_application_pid = application_process_id;
        g_tracked_application_program_id = application_program_id;
        g_info.application_process_id = application_process_id;
        const bool has_application =
            application_process_id != 0 &&
            application_program_id != 0 &&
            application_program_id != applet_bgm::QlaunchTitleId;
        g_info.application_program_id =
            has_application ? application_program_id : 0;
        // Encountering an already-running application when this observer is
        // initialized is only a baseline, not proof of a launch. After that
        // first sample, an application appearing from a known Home/background
        // state may provisionally enter Loading while its PDM Launch event is
        // still being committed.
        bool loading = has_application && !first_application_sample &&
            (!g_application_session_started || g_application_backgrounded);
        bool out_of_focus = false;
        if (has_application && g_has_pending_application_event &&
            ProgramIdsMatch(
                g_pending_application_program_id,
                application_program_id)) {
            g_info.has_application_signal = true;
            loading = g_pending_application_loading;
            out_of_focus =
                g_pending_application_event_type ==
                    PdmAppletEventType_OutOfFocus4;
        }
        if (has_application) {
            g_application_session_started = true;
            // A populated application slot ends any zero-PID hand-off hold.
            // A subsequent real OutOfFocus (3) event can assert it again.
            g_application_internal_out_of_focus = false;
            if (loading) {
                g_application_backgrounded = false;
            }
        }
        g_info.application_out_of_focus = out_of_focus;
        SetLoadingLocked(loading);
        if (application_process_id != 0 || !has_application) {
            g_has_pending_application_event = false;
            g_pending_application_program_id = 0;
            g_pending_application_event_type = 0;
            g_pending_application_loading = false;
        }
    }
    mutexUnlock(&g_info_mutex);

    s32 total_entries = 0;
    s32 start_entry_index = 0;
    s32 end_entry_index = 0;
    auto result = pdmqryGetAvailablePlayEventRange(
        &total_entries, &start_entry_index, &end_entry_index);
    mutexLock(&g_info_mutex);
    ++g_info.query_count;
    mutexUnlock(&g_info_mutex);
    if (R_FAILED(result)) {
        SetStatus(TuneUiActivityObserverStatus_Failed, result);
        return;
    }

    if (g_last_end_entry_index < 0) {
        // Do not revive a historical overlayDisp focus event as an open Quick
        // Settings panel. Recent Application events are safe and necessary
        // when this sysmodule is started while a game is already running.
        const auto recent_start = std::max(
            start_entry_index, end_entry_index - (EventBatchSize - 1));
        if (total_entries > 0 && application_process_id != 0) {
            result = ReadEvents(recent_start, end_entry_index, false);
        }
        g_last_end_entry_index = end_entry_index;
    } else if (end_entry_index < g_last_end_entry_index) {
        // The database was cleared or wrapped. Re-baseline without replaying
        // historical overlay events.
        g_last_end_entry_index = end_entry_index;
    } else if (end_entry_index > g_last_end_entry_index) {
        const auto first = std::max(
            start_entry_index, g_last_end_entry_index + 1);
        result = ReadEvents(first, end_entry_index, true);
        if (R_SUCCEEDED(result)) {
            g_last_end_entry_index = end_entry_index;
        }
    }

    SetStatus(
        R_SUCCEEDED(result) ? TuneUiActivityObserverStatus_Active
                            : TuneUiActivityObserverStatus_Failed,
        result);
}

TuneUiActivityObserverInfo GetInfo() {
    mutexLock(&g_info_mutex);
    g_info.application_handoff_active =
        g_application_session_started &&
        !g_application_backgrounded &&
        g_tracked_application_pid == 0 &&
        !g_info.loading_active;
    const auto info = g_info;
    mutexUnlock(&g_info_mutex);
    return info;
}

Snapshot GetSnapshot() {
    mutexLock(&g_info_mutex);
    const Snapshot snapshot{
        .availability = g_info.status == TuneUiActivityObserverStatus_Active
            ? Availability::Active : Availability::Unavailable,
        .quick_settings_open = g_info.quick_settings_open != 0,
        .application_loading = g_info.loading_active != 0,
        .application_out_of_focus =
            g_info.application_out_of_focus != 0,
        .application_handoff_active =
            g_application_session_started &&
            !g_application_backgrounded &&
            g_tracked_application_pid == 0 &&
            !g_info.loading_active,
        .update_count = g_update_count,
    };
    mutexUnlock(&g_info_mutex);
    return snapshot;
}

void ResetHistory() {
    mutexLock(&g_info_mutex);
    g_info.query_count = 0;
    g_info.event_count = 0;
    g_info.overlay_event_count = 0;
    g_info.application_event_count = 0;
    g_info.application_launch_count = 0;
    g_info.application_in_focus_count = 0;
    g_info.application_out_of_focus_count = 0;
    g_info.application_background_count = 0;
    g_info.application_exit_count = 0;
    g_info.quick_settings_open_count = 0;
    g_info.quick_settings_close_count = 0;
    g_info.loading_start_count = 0;
    g_info.loading_end_count = 0;
    g_info.last_event_type = 0;
    g_info.last_applet_id = 0;
    g_info.last_event_index = 0;
    g_info.last_program_id = 0;
    g_info.has_overlay_signal = false;
    g_info.has_application_signal = false;
    g_info.has_last_event = false;
    g_update_count = 0;
    mutexUnlock(&g_info_mutex);
}

}
