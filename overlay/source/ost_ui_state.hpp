#pragma once

#include <switch.h>

namespace ost_ui_state {

// Records an edit without interrupting playback. The returned revision lets
// the editor acknowledge its own in-memory update without rereading the SD.
u32 markPlaylistChanged(u64 title_id);

// Cheap, in-memory revisions used by menus to refresh only changed rows.
u32 getGlobalRevision();
u32 getPlaylistRevision(u64 title_id);

// Playback changes are committed when the editor is left or the overlay hides.
void flushPlaylistReload(u64 title_id);
void flushAllPlaylistReloads();

}
