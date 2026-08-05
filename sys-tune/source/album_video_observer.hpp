#pragma once

#include "tune.h"

namespace tune::album_video {

enum class Availability {
    Unavailable,
    Waiting,
    Ready,
};

struct Snapshot {
    Availability availability{Availability::Unavailable};
    bool active{};
    u32 update_count{};
};

// Starts a best-effort caps:a observer for PhotoViewer. Failure is non-fatal.
Result Initialize();
void Exit();

TuneAlbumVideoObserverInfo GetInfo();
Snapshot GetSnapshot();

}
