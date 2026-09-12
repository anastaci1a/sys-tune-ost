# Product intent and playback behavior



This is a Nintendo Switch **system UI soundtrack manager**, implemented as a resident sysmodule plus a Tesla/Ultrahand overlay. It assigns independent playlists to Home, Lock, Settings, Startup/Wake, Loading, and mapped Nintendo applets. Regular applications and gameplay should be silent with respect to OST playback. The old generic music-player UI was refocused around the currently active UI soundtrack.

Distribution consists of complete install ZIPs with paired overlay/sysmodule binaries and identifiable source history. Hardware feedback drives detector development. The intended interface uses concise configuration, related controls in submenus and clear absolute units.

## Playback sessions

| State or operation | Intended/current implementation |
| --- | --- |
| Home interrupted by another state | Retain the live decoder, queue, shuffle order, current track, and playback position. |
| Home manually paused, then left and re-entered | Clear only the manual paused flag; resume through Mid-Song Fade In. Added in qs11; console confirmation pending. |
| Other normal applets, Settings, Lock | Restart the state playlist on activation; reshuffle when configured. |
| Startup Sound | Choose one random entry for a one-shot startup session. Optional replay on waking. Allow continuation across Lock → Home; another applet/game can cancel it through the normal transition path. |
| Separate Wake playlist | Optional, saved independently. When Include Wake from Sleep is off, hide both dependent controls without deleting their saved values or songs. |
| Add/reorder an active playlist | Reconcile by path, preserve the current decoder/time when its file survives. Ordered mode adopts the new neighbors; shuffle pins the current file and reshuffles the rest. |
| Remove the playing file | Interrupt and begin the rebuilt queue. Empty playlists should stop cleanly. |
| Manual controls | Apply to the active state. Repeat/shuffle persist for that state; one-shot Startup hides those controls. |
| Native Quick Settings | Compound an optional attenuation multiplier with other gains. Ultrahand/Tesla opening itself is not the trigger. |
| Loading Screen | Initial system application launch only. Later in-game loading and multi-program handoffs remain silent. |
| User Select | Has a soundtrack, with a warning that it may overlap music already played by the game. |

MP3, FLAC, WAV/WAVE are supported. Current limits are 64 entries per UI-state playlist and paths shorter than 256 bytes. Headerless MP3 playback avoids the costly upfront full-file duration scan; unknown duration is shown as `--:--` and seeking is disabled for that session. M4A/AAC is not implemented; it was excluded from the small resident decoder design, rather than left as an agreed next feature.
