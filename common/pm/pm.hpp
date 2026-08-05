#pragma once

#include <switch.h>

namespace pm {

auto Initialize() -> Result;
void Exit();
void getCurrentPidTid(u64* pid_out, u64* tid_out);
auto PollCurrentPidTid(u64* pid_out, u64* tid_out) -> bool;

// Returns the active System UI OST target. Known library applets take priority;
// otherwise a running application silences BGM, and qlaunch is selected only
// when the application slot is empty.
void getAppletBgmTarget(u64* pid_out, u64* tid_out, bool application_out_of_focus);

}
