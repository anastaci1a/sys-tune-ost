#pragma once

#include "tune_types.hpp"

namespace tune::power_state {

enum class Availability {
    Unavailable,
    Active,
};

enum class Transition : u8 {
    None = TunePowerTransition_None,
    Sleep = TunePowerTransition_Sleep,
    Wake = TunePowerTransition_Wake,
};

struct Snapshot {
    Availability availability{Availability::Unavailable};
    Transition last_transition{Transition::None};
    u32 transition_count{};
};

Result Initialize();
void Exit();

TunePowerStateObserverInfo GetInfo();
Snapshot GetSnapshot();

}
