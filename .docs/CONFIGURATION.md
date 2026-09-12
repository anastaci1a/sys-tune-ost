# Configuration and persistence



Authoritative configuration is `common/config/config.cpp`; on the console it is `/config/sys-tune/config.ini`.

| Setting | Factory default | Limits/location |
| --- | --- | --- |
| System UI OST | Off | Master switch; legacy `[applet_bgm] enabled` key |
| Global OST volume | 100% | 0–150%; main playback page |
| State soundtrack volume | 100% | 0–150%; each playlist editor |
| Album video multiplier | 100% | 0–150%; Album playlist configuration |
| Quick Settings Lower OST | On | Additional Options → Quick Settings |
| Quick Settings multiplier | 50% | 0–100%; same submenu |
| HOME Hold Time | 400 ms | Absolute 100–1500 ms; same submenu |
| Loading Start Delay | 500 ms | Off–5000 ms; Loading Screen editor → Start / End Delay |
| Loading End Delay | Off | Off–5000 ms; same submenu |
| Track-boundary Fade In | 150 ms | Off–5000 ms |
| Track-boundary Fade Out | Off | Off–5000 ms |
| Mid-Song Fade In | 300 ms | Off–5000 ms |
| Mid-Song Fade Out | 500 ms | Off–5000 ms |
| Include Wake from Sleep | Off | Startup Sound editor |
| Use Separate Wake Playlist | Off | Visible only when wake inclusion is enabled |
| Normal playlist repeat | All | Off / One / All; Startup is one-shot |
| Normal playlist shuffle | Off | Saved per playlist |

Duration sliders use finer short-duration steps and coarser long-duration steps. `Hold Time: 400 ms` is the complete threshold, not an adjustment to a hidden one-second base. Values above a second are displayed as seconds. The gain path multiplies global OST volume × state volume × Quick Settings gain × applicable Album-video gain; inactive contextual gains are 1. Gains above unity can clip loud input. Fades are sequential fades, not overlapping crossfades.

The `[ost_manager]` keys include `quick_settings_enabled`, `quick_settings_volume`, `quick_settings_hold_ms`, `loading_start_delay_ms`, `loading_end_delay_ms`, `fade_in_ms`, `fade_out_ms`, `mid_song_fade_in_ms`, `mid_song_fade_out_ms`, `startup_on_wake`, `separate_wake_playlist`, and `album_video_volume`. Playlist sections use `ost_%016lX`, `count`, `item_000` etc., `shuffle`, and `repeat`; per-state gains use `[ost_volume]`.

Migration imports old single-file applet assignments and a valid old startup file without deleting legacy keys. Home retains qlaunch's real ID, so the formerly combined Home/Settings/Lock playlist becomes Home. When introducing mid-song fades to an older configuration, explicit earlier fade values are inherited rather than overwritten by new defaults.


## State identifiers and limits

`common/applet_bgm.hpp` defines Home as `0100000000001000`, Album as
`010000000000100D`, and User Select as `0100000000001007`. Synthetic states use
`UINT64_MAX` (Startup), minus 1 (Settings), minus 2 (Lock), minus 3 (Wake), and
minus 4 (Loading). Zero means silence. These are configuration/runtime IDs,
not additional installed programs. Playlist paths must fit 256 bytes including
the terminator; each state has at most 64 entries.

`migration_version` in `[ost_manager]` currently reaches 2. The inherited
generic player keys remain in the config API for compatibility; their presence
does not imply a product requirement to play OST during gameplay.
