# Project structure and runtime architecture



| File/module | Responsibility |
| --- | --- |
| `Makefile` | Release version, API, decoder flags, component builds, install-tree packaging |
| `common/applet_bgm.hpp` | UI-state IDs, scene constants, supported applets, detection priority, limits |
| `common/pm/pm.cpp` | Process/title enumeration and initial applet target |
| `common/config/` | Persistent values, clamping, playlist storage, migration |
| `sys-tune/source/impl/music_player.cpp` | `PlaylistSession`, active/Home/Startup sessions, path reconciliation, gain/fades, audio loop and routing loop |
| `sys-tune/source/impl/source.cpp` | Decoders, seek/duration behavior, MP3 startup optimization |
| `sys-tune/source/qlaunch_scene_observer.*` | Best-effort qlaunch `erpt:c` MITM, scene snapshots and raw history |
| `sys-tune/source/album_video_observer.*` | Album `caps:a` observation and video playback diagnostics |
| `sys-tune/source/power_state_observer.*` | PSC sleep/display transition preparation and wake notification |
| `sys-tune/source/ui_activity_observer.*` | Bounded PDM events, application launch/focus/handoffs, applet focus, HOME/B/touch observation |
| `sys-tune/source/main.cpp` | Service initialization, thread stacks, observer startup |
| `sys-tune/source/tune_service.*`, `ipc/` | Sysmodule IPC handlers and matching overlay bindings/diagnostic structs |
| `sys-tune/sys-tune.json` | NPDM program identity, services, syscall permissions, thread metadata |
| `overlay/source/gui_applet_bgm.*` | Manage Soundtracks and its fixed display order |
| `overlay/source/gui_ost_playlist.*` | Playlist editor, conditional Startup controls, Loading link, User Select warning |
| `overlay/source/ost_ui_state.*` | Batched editor change notifications and deferred reloads |
| `overlay/source/gui_quick_settings_settings.*`, `gui_loading_settings.*` | Requested configuration submenus |
| `overlay/source/duration_slider.hpp`, `ost_volume_slider.hpp` | Shared nonlinear duration and gain controls |
| `overlay/source/gui_*_diagnostics.*` | Qlaunch, Album video, power, and UI-activity evidence |
| `sys-tune/nxExt/` | Tracked IPC helper library, built before the module |

The sysmodule uses a 250 KiB static heap. In `main.cpp`, the GPIO thread reserves 4 KiB, detector thread 12 KiB (increased from 4 KiB in qs9), and audio thread 24 KiB. These sizes and the small resident budget make unbounded scans/allocations or deeper IPC paths significant. This is a code inventory, not proof of adequate high-water stack/heap margins.


## Repository layout

All paths above are relative to the repository root, `sys-tune-ost/`.

| Path | Role |
| --- | --- |
| `AGENTS.md` | Agent procedures and documentation maintenance contract |
| `docs/README.md` | Project documentation index |
| `docs/evidence/` | Optional ignored local diffs, logs, crash reports, manifests and identity-rewrite recovery data |
| `docs/releases/` | Historical release descriptions |
| `.github/workflows/build.yml` | Published-release/manual build workflow |
| `.github/scripts/package-release.py` | Four-payload ZIP validation and release naming |
| `.github/scripts/verify-project.py` | Read-only documentation, evidence and optional local archive checks |
| `common/minIni/` | Inherited INI reader/writer |
| `common/sdmc/` | SD filesystem access shared by the components |
| `sys-tune/source/impl/dr_*.h` | Embedded audio decoders |
| `sys-tune/source/impl/aud*`, `resamplers/` | Audio output and resampling |
| `overlay/lib/` | Pinned libtesla Git submodule, not locally maintained application code |
| `sample/` | Three inherited overlay screenshots; not current UI acceptance references |
| `.vscode/` | Editor configuration; local settings ignored |
| `LICENSE` | Inherited project license |
| `dist/` | Disposable Makefile output and staging; ignored |
| `_dist/` | Preserved local distribution archives; ignored, outside `make clean` targets |

The root README uses `sample/libtesla_1586882452.jpg` (playback) and
`sample/libtesla_1586882672.jpg` (playlist) as labeled historical screenshots.
These existing files can be replaced without changing the README image links.
The third inherited screenshot remains available in `sample/`.

The former outer handoff and nested repository are consolidated here. There is
one `.git/` at this root. The populated submodule's relative Git pointer and
worktree relationship remain valid when the entire repository moves together.
Local `_dist` archives are optional and are not downloaded by cloning Git.

## Execution and ownership

The main thread initializes essential services, starts GPIO, routing and audio
threads, then serves `tune` IPC. Essential service initialization can abort;
the optional observer paths use best-effort initialization. Early qlaunch and
Album MITM declarations precede ordinary initialization; the initial main-thread
priority is subsequently restored to `0x30`.

`PmdmntThreadFunc` computes ownership about every 50 ms. `TuneThreadFunc` produces
audio from the chosen session. `GpioThreadFunc` watches the headphone jack.
The playback implementation has shared player and Startup synchronization
(`g_mutex`, `g_startup_mutex`), retained Home and Startup sessions, and state
requests between routing and playback. The source functions, not a diagram,
define the detailed lock and teardown order.

The overlay writes persistent configuration through `common/config`, batches
playlist edits through `ost_ui_state`, and requests reloads over `ipc/tune.c`.
The resident `tune_service.cpp` exposes playback controls, state reloads and
observer diagnostics. `ipc/ipc_cmd.h` assigns controls 0–55 (with gaps) and API
query 5000. `ipc/tune.h` carries the shared client and diagnostic structures.
The top-level Makefile currently exports API 18 for both components.

The scene resolver, power coordinator, application events and retained sessions
are coupled: a change in a low-level observer can alter audio ownership even
without changing the playlist editor. [Detection](DETECTION.md) describes those
boundaries; [TODO](TODO.md) records the regression coverage still missing.
