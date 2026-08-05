#pragma once

#include "tune.h"

namespace tune::qlaunch_scene {

enum class SceneAvailability {
    Unavailable,
    Waiting,
    Ready,
};

struct SceneSnapshot {
    SceneAvailability availability{SceneAvailability::Unavailable};
    u8 scene{};
    u32 update_count{};
};

// Starts a best-effort Atmosphere erpt:c observer. Failure is deliberately
// non-fatal so the audio module and HOME Menu can continue to boot normally.
Result Initialize();
void Exit();

TuneQlaunchSceneObserverInfo GetInfo();
SceneSnapshot GetSceneSnapshot();
void ResetHistory();

}
