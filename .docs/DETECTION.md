# UI detection and ownership



## Qlaunch, applets, and Home ownership

Home, Lock, and retail Settings all live in qlaunch, ID `0100000000001000`. Process presence alone cannot distinguish them. Hardware observations led to these mappings:

| Raw scene | Playlist |
| --- | --- |
| `0x00` | Home Menu |
| `0x0A` | Lock Screen |
| `0x32` | System Settings |

Unknown reported scenes stay silent. Waiting for a first scene is different from observer unavailability: the former stays silent during boot; only genuine unavailability permits compatibility fallback. Lock has priority over stale underlying process presence. **As of qs11, Settings yields to a detected foreground library applet**, because its `0x32` scene can linger while a submenu such as Mii Editor is in front.

`PmdmntThreadFunc` polls at roughly 50 ms, obtains the process target, applies library-applet focus/HOME handling, applies Loading ownership, then coordinates power, qlaunch, Startup, and audio requests. The actual function defines precedence. It is not a single universal visibility signal.

User Select → Home → User Select uses real applet focus updates first. Short HOME is a fallback if a matching focus signal does not arrive; the code waits 200 ms before applying the fallback and correlates nearby focus/button events. Old applet events are checked against current instance/freshness so they cannot automatically mask a new instance. This bounded mechanism is different from qs8's unconditional qlaunch-update latch.

## Quick Settings

The hardware tests did **not** expose a reliable open/close scene change for native Quick Settings. Process existence also failed because overlayDisp remains alive. The focus-based approach failed on the user's console. The current fallback observes HOME duration using the already-active shared-memory feed, plus B/HOME/outside-touch closing heuristics. It uses a fixed panel touch boundary (`QuickSettingsPanelLeft = 760`) and therefore still needs hardware checks, including touch behavior and different controller modes.

400 ms is based on the user's repeated timing estimate, not a universal OS visibility guarantee. Release-on-threshold handling was added because the press can qualify between polling cycles. Turning attenuation off restores its gain to 1 through normal mid-song fade behavior; it does not disable HOME observation used by User Select routing.

qs11 removes explicit `hidsysInitialize()` / `hidsysActivateHomeButton()` use. HOME is passively observed; Npad and touchscreen still have best-effort setup calls, so the entire HID initialization is not literally read-only. Libnx documents AM's ownership of HOME activation. [libnx HOME activation](https://switchbrew.github.io/libnx/hidsys_8h.html)

## Loading

qs10/qs11 use the qs7-style finite application Launch → first InFocus interval. PDM event 3/internal out-of-focus versus event 4/background and multi-program handoffs were distinguished to suppress Home/Loading music during game-owned loading, notably 3D All-Stars → Galaxy. That mapping is a hardware-facing assumption to validate, not a promise that all firmware publishes identical sequences.

The qs9 overlayDisp close-edge latch is removed. Start Delay suppresses Home while waiting. A launch shorter than Start Delay should never start Loading music; End Delay should only extend a launch whose music actually began. A fresh launch inside an active end window can remain continuous. Another visible applet takes priority over Loading delay. Starting the sysmodule while a game already exists baselines it rather than classifying it as a fresh launch.

The focus endpoint did end too early for Octopath Traveler in qs7, despite matching 3D All-Stars well. End Delay is a configurable compromise; consistent matching to Nintendo's actual loading animation remains an open investigation. The discarded missing-close-event latch caused indefinite Loading.

## Power, Album video, and MITM

PSC replaced a fixed ordinary Home return delay so playback can stop before suspension/display-off. PDM power records are a fallback. Audio already queued before sleep is discarded during the ownership transition, rather than allowed to leak briefly on wake. Lock-enabled consoles wait for a concrete Lock scene; lock-disabled consoles retain a 1.5 s boot/wake fallback. That fallback is not an ordinary applet-to-Home delay.

Qlaunch `erpt:c` and Album `caps:a` observers forward original IPC requests; they must preserve domain/cloned-session handling, descriptor bounds, and teardown order. Initialization uses an early runtime future-MITM declaration without a persistent `mitm.lst`. NPDM currently hosts `tune`, `erpt:c`, `caps:a`. These permissions are part of the current paired build.
