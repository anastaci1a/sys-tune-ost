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
    u32 update_count{};
};

// Uses the already-open pdm:qry session owned by the music player. The native
// hold-HOME panel is overlayDisp; its focus events delimit Quick Settings.
void Initialize(bool pdm_available);
void Poll(u64 application_process_id, u64 application_program_id);

TuneUiActivityObserverInfo GetInfo();
Snapshot GetSnapshot();
void ResetHistory();

}
