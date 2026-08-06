#pragma once

#include "tune.h"

namespace tune::ui_activity {

enum class Availability {
    Unavailable,
    Active,
};

struct Snapshot {
    Availability availability{Availability::Unavailable};
    bool quick_settings_open{};
    bool application_loading{};
    bool application_out_of_focus{};
    bool application_handoff_active{};
    bool input_available{};
    bool has_home_button_signal{};
    u32 home_short_press_count{};
    u64 last_home_short_press_tick{};
    bool has_library_applet_signal{};
    bool library_applet_foreground{};
    u64 library_applet_program_id{};
    u32 library_applet_focus_update_count{};
    u64 last_library_applet_event_tick{};
    u32 update_count{};
};

// Uses the already-open pdm:qry session for launch/applet focus and a
// best-effort HID observer for the native hold-HOME panel.
void Initialize(bool pdm_available);
void Exit();
void Poll(
    u64 application_process_id, u64 application_program_id,
    u32 quick_settings_hold_ms);
void CloseQuickSettings();

TuneUiActivityObserverInfo GetInfo();
Snapshot GetSnapshot();
void ResetHistory();

}
