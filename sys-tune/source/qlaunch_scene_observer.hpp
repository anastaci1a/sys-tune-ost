#pragma once

#include "tune.h"

namespace tune::qlaunch_scene {

// Starts a best-effort Atmosphere erpt:c observer. Failure is deliberately
// non-fatal so the audio module and HOME Menu can continue to boot normally.
Result Initialize();
void Exit();

TuneQlaunchSceneObserverInfo GetInfo();
bool TryGetCurrentScene(u8* out_scene);
void ResetHistory();

}
