# Development process and iteration history

## Scope and how to read this history

This record covers the custom sys-tune fork from its first Applet BGM commit through qs11. It preserves what the project became, the reasoning behind detector changes, regressions discovered by the user, and the remaining uncertainty. Full upstream and custom Git history is in `../.git`; readable logs and the important regression diffs are in `evidence/`.

Evidence has three levels:

- **Source:** behavior or changes directly visible in a recovered commit.
- **User observation:** hardware behavior reported in the supplied development conversation or available project summary.
- **Prior analysis:** an earlier assistant's explanation, build report, or interpretation; useful context that is not automatically verified runtime behavior.

The handoff independently checks the repository and stored archives. It does not rerun past builds or console tests. The earlier conversation is incomplete before qs7, so early entries are reconstructed from commits rather than presented as a verbatim chat history. Commit order and parentage are authoritative; the recorded timestamps use multiple time zones.

## The initial design

The custom work began from upstream `v2.1.0-beta.1` (`2a6c31c`). The inherited project already provided resident audio playback, an overlay, MP3/FLAC/WAV decoders, audio output/resampling, and playlist controls. The user's goal was to make the Switch's system UI feel like it has its own soundtrack, with different music for Home and individual applets, while leaving gameplay silent.

This led to three durable design decisions: playback ownership follows the visible UI state; Home is a resumable session; other applet sessions restart when entered. Configured playlists, order, volume, repeat, and shuffle must survive reboot. Startup is a special random one-shot pool, not another endlessly looping normal playlist.

## Iteration ledger

Release ZIP availability and exact hashes are in `RELEASES.md`. Every commit below is present, including the qs8 side branch.

| Version | Commit | API | Change and its reason | Limitation or subsequent finding |
| --- | --- | ---: | --- | --- |
| 2.1.0-applet-bgm.1 | `ea29c19` | 5 | First per-applet BGM mode, persistent single-file assignments, process targeting, overlay browser/IPC controls. | Home, Settings, and Lock share qlaunch and cannot be split by PID alone. Original ZIP not recovered. |
| 2.2.0-ui-ost.1 | `bfdd084` | 6 | Refocused the application into a UI OST manager with per-state playlists, Startup pool, active-state controls, persistence and migrations. | This large foundational change preceded later playback and detection refinements. Original ZIP not recovered. |
| 2.2.1-ui-ost.1 | `e656b39` | 7 | Optional Startup replay after waking from sleep. | Initially keyed to system sleep; display-only transitions were addressed later. Original ZIP not recovered. |
| 2.2.2-ui-ost.1 | `8ce0d0d` | 7 | Reduced transition latency through batched config loading, retained Home decoder, and MP3 startup improvements. | Headerless MP3 duration/seek information is unavailable in exchange for faster starts. Original ZIP not recovered. |
| 2.2.3-ui-ost.1 | `191c171` | 8 | Added Home resume timing diagnostics to investigate latency and retained-decoder behavior. | Temporary diagnostic UI was later removed. Original ZIP not recovered. |
| 2.3.0-ui-ost.1 | `c0e5dfc` | 8 | Consolidated the UI OST release, removed temporary diagnostics, refined sliders/options. | This was called stable in its release lineage, before the experimental scene split. Original ZIP not recovered. |
| 2.3.1-ui-ost.1 | `a7da4a4` | 8 | Polished playlist editing, row updates, cursor behavior, footer/help layout, and deferred reload notifications. | Deferring reload reduced repeated interruption but did not yet preserve playback across the final applied edit. |
| 2.4.0-exp.qs1 | `ee5fc7b` | 9 | Introduced experimental qlaunch `SystemAppletScene` observation through an `erpt:c` MITM, with raw history diagnostics. | Initial experiment collected evidence; raw values were not yet assigned independent playlists. |
| qs2 | `afb4388` | 10 | Mapped repeatedly observed Home `0x00`, Lock `0x0A`, Settings `0x32` to independent persistent playlists. | Boot/wake precedence, stale processes and repeat behavior still required corrections. |
| qs3 | `0408b3a` | 11 | Corrected power/repeat handling, split track-boundary and mid-song fades, covered display on/off, and kept waiting-for-first-scene boot time silent. | Broader transition/lock priority and delayed power observations still needed work. |
| qs4 | `2320cea` | 12 | Prioritized Lock/Settings over stale processes, added per-state 0–150% gain and moved global OST gain to the main page. | Home confirmation delays reduced leaks but added latency; Settings priority was later found too broad. |
| qs5 | `f4daa02` | 13 | Added Album video-context gain/diagnostics and optional separate Wake playlist. | New observer behavior remained hardware-sensitive; later UI work hid disabled wake-dependent controls. |
| qs6 | `fd716e6` | 14 | Replaced ordinary Home confirmation delay with coordinated PSC power detection, PDM fallback, and queued-audio discard on power transitions. | Became the baseline for launch/Quick Settings work; game handoffs still exposed Home fallback. |
| qs7 | `7960f42` | 15 | Added Quick Settings and initial-launch signals, Loading playlist, path-based playlist reconciliation, requested fade defaults, concise help and diagnostics. | Quick Settings did not lower volume on real opens. Loading started early/ended early in some titles. User Select could stick after HOME. |
| qs8 | `787197a` | 15 | Added Loading Start Delay and overlayDisp-based lifetime handling, plus a qlaunch-based applet/Home override. | User reported cold-boot muting, Home sticking on re-entry, continued failed Quick Settings detection, freezes/fatals. Kept only as an audit branch. |
| qs9 | `1c695f3` | 16 | Rebuilt directly from qs7; separated Quick Settings HOME input from Loading; used applet focus plus bounded HOME fallback; expanded diagnostics and detector stack. | One-second HOME threshold was too long. OverlayDisp loading-end latch could stay active indefinitely. |
| qs10 | `9637066` | 17 | Made Quick Settings optional/configurable at an absolute 400 ms default; restored finite launch-focus Loading endpoint; added Start/End Delay submenu and User Select warning. | User reported Home pause persistence, Settings submenu masking, and another fatal during a simultaneous overlay/User Select B transition. |
| qs11 | `79f2e94` | 18 | Home auto-resume, conditional wake rows, Settings yielding to foreground applets, and removal of redundant HOME-feed activation. | Latest preserved build. Console confirmation and the fatal/freeze root cause remain open. |

## Phase 1: playlist/session design and responsiveness

The single-file Applet BGM mode was expanded into a complete playlist manager rather than layered with unrelated player UI. The top playback panel follows the active state, so pausing one applet should not pause the next. Non-Home applets restart/reshuffle on activation; Home retains playback position and eventually its live decoder to avoid reopening and seeking through a long file.

Latency work addressed real costs rather than only adding timeouts: loading a playlist in a single config pass, avoiding unnecessary decoder recreation, and avoiding a full duration scan of headerless MP3s. Temporary Home timing diagnostics were introduced in 2.2.3, then removed in 2.3.0 after serving that investigation. Their commit remains available if a future regression requires comparison.

Editor improvements in 2.3.1 updated rows in place and batched notifications until the editor/overlay was left. qs7 later solved the remaining issue the user described: adding or moving songs still restarted the playlist when the changes were applied. Path reconciliation now keeps the active decoder if the current file survives. In ordered mode, the file adopts its new location/neighbors; in shuffle mode, it is pinned while the other entries are reshuffled. Removing the current file is intentionally different and triggers a transition.

## Phase 2: distinguish qlaunch scenes using evidence

Process detection was inadequate for Home, retail Settings and Lock because all are qlaunch views. qs1 added a best-effort `erpt:c` observer and exposed raw scene history, including legacy and 21.0.0+ ERPT layouts, domain requests, and cloned SDK sessions. The intent was to observe a repeatable state signal while forwarding normal requests, without a persistent `mitm.lst` or a version-specific qlaunch memory patch.

Repeated hardware observations established the three current mappings, enabling qs2. Unknown scenes were kept silent. Later revisions distinguished an observer that is unavailable from one that is merely waiting for its first report: treating both as Home produced boot/wake leakage.

The same technique did **not** work for native Quick Settings. The user explicitly opened and closed the hold-HOME panel and found no raw scene change. That negative result matters: re-running the same scene-mapping idea without a new signal would repeat an already-tested dead end.

## Phase 3: fades, power and gain composition

Earlier user reports included repeat One/All not behaving, Home briefly playing while waking or powering the screen off, Lock being masked by a still-live applet, and wake audio being interrupted by Lock → Home. qs3/qs4 introduced changes to repeat/session routing, silence while awaiting real scene ownership, display/sleep event handling, and the distinction between natural track-boundary fades and mid-song transitions.

qs4 temporarily used a 600 ms ordinary Home confirmation window and a 1.5 s boot/wake fallback to allow a late power event to win. The benefit was fewer transient leaks; the cost was a perceptible delay on legitimate returns. qs6 replaced the ordinary Home delay with PSC preparation before suspension/display-off and discarded queued old audio. The lock-disabled 1.5 s boot/wake fallback remains a separate special case.

Per-state 0–150% volume was added in qs4, multiplied by global OST volume. qs5 added Album-video context gain via `caps:a`, an optional separate Wake pool, and its own diagnostics. qs7 added native Quick Settings gain, capped at 100% and defaulting to 50%. These factors compose rather than override one another. The user's requested factory fade values were 150 ms track Fade In, track Fade Out off, 300 ms Mid-Song Fade In, and 500 ms Mid-Song Fade Out. Existing saved values were deliberately preserved.

## Phase 4: qs7 and the first launch/Quick Settings model

The user and assistant clarified that Quick Settings means Nintendo's native hold-HOME panel, not the Ultrahand/Tesla overlay. With no distinct qlaunch scene, qs7 tried overlayDisp (`010000000000100C`) focus lifecycle records. Mere process existence was recognized as insufficient because this process persists.

Application Launch → InFocus records were used for a new Loading playlist directly below Home. The user then observed Home music during 3D All-Stars' game-owned Galaxy loading. Plausible causes included focus transitions and a multi-program handoff that temporarily vacates the application slot. The user explicitly requested that in-game loading be excluded, so qs7 distinguished initial application launch from later internal handoffs, baselined an already-running game at sysmodule startup, and added resettable raw application-event counters.

The supplied release report says qs7 was clean-built and checked as an API-15 pair. Hardware feedback then contradicted part of the design: Quick Settings never attenuated, even at a configured 0%; Loading began before Nintendo's visuals and ended too early for Octopath Traveler, while 3D All-Stars' stock loading end lined up; User Select could remain selected after HOME until another applet was opened.

## Phase 5: qs8 regression and the decision to rebuild from qs7

qs8 was developed in another chat. It was a small source diff (109 additions across six files in the earlier audit), but its state changes had wide consequences. The original qs8 source bundle was eventually recovered under a broader UI-state-fixes name, allowing a direct source comparison rather than an inference from binaries.

The user's observations were specific:

- Cold boot played Startup, then all normal soundtracks were silent until launching a game.
- The game launch produced Loading and User Select music; HOME correctly reached Home, but returning to the game left Home music playing.
- Loading delay/end behavior appeared improved.
- Quick Settings still did not attenuate.
- Several crashes/freezes occurred, including one requiring a forced shutdown and an `ams.mitm` abort screen.

The source audit identified two concrete routing problems. OverlayDisp's one-time boot Launch was treated as an open Quick Settings panel; the user's 0% test multiplier could therefore mute all normal states. A broad lingering-applet → Home override keyed to qlaunch update count could outlive the transition and overwrite a legitimate User Select target. These findings explain the reported behavior without proving that the same diff caused the separate fatal.

The branch relationship is important: **qs9's parent is qs7, not qs8**. The qs8 commit remains at `refs/remotes/qs8-audit/power-state-coordinator`; it is not an ancestor of HEAD. Useful Loading timing work was reimplemented while the problematic routing was discarded. The user returned to the original development chat because its continuity had worked better, motivating this handoff's explicit history and evidence.

## Phase 6: qs9's replacement signals and its new limitations

qs9 separated three responsibilities: a measured one-second HOME hold for Quick Settings, applet focus events with a short-HOME fallback for Home/User Select, and overlayDisp lifetime only for Nintendo's launch animation. Optional input setup returned errors rather than adding an abort requirement. The detector stack grew from 4 KiB to 12 KiB to add headroom; this was defensive hardening, not a demonstrated stack-overflow diagnosis.

The user confirmed that sufficiently long holds could trigger attenuation, but the panel actually appeared after roughly 0.4 s in repeated phone timing tests. A one-second threshold therefore missed normal short opens. The user preferred a real visibility signal and accepted a heuristic only if necessary, with a disable switch and a submenu containing its settings.

The other regression was more severe: Loading music never stopped until another sound/state transition. The overlayDisp close edge was not reliably published, leaving the lifetime latch true. qs9 demonstrates why an apparently sensible OS event boundary cannot be accepted without hardware proof of both its start and end signals.

## Phase 7: qs10 makes timing explicit and restores a finite endpoint

qs10 removed the overlayDisp Loading hold instead of adding a timeout around it. It restored qs7's finite application-focus end and applied a new End Delay afterwards. The user explicitly requested both delays inside a submenu within the normal Loading playlist editor. Start defaults to 500 ms; End defaults to off.

Quick Settings received Additional Options → Quick Settings, with Lower OST, the multiplier, and an absolute Hold Time slider defaulting to 400 ms. The user specifically clarified that the slider must show the full threshold, not `+100 ms` or another hidden-base delta. Release-on-threshold handling catches qualifying holds between routing polls. Turning attenuation off restores normal gain while preserving HOME observation for applet routing.

The review considered short launches, a new launch during End Delay, and another applet taking priority. These were source/build checks, not a complete console test campaign. User Select also received the requested warning about possible overlap with the game's own music.

## Phase 8: qs11 responds to the latest feedback

After qs10, the user requested four corrections:

1. Home must not retain manual pause after leaving and returning. qs11 clears its paused flag only on re-entry, retaining track/queue/decoder state and resuming through Mid-Song Fade In.
2. Turning Include Wake from Sleep off must hide both Use Separate Wake Playlist and Wake Playlist. qs11 inserts/removes those rows without discarding their saved configuration.
3. Settings → Internet/Data Management/Amiibo/Mii transitions were misdetected; Mii worked when entered from User Page. The resolver let stale Settings `0x32` override all detected applets. qs11 lets foreground applets supersede Settings while retaining Lock priority.
4. Another fatal happened after the user pressed B with Ultrahand open while a newly launched game's User Select was also active. Both UIs responded to B, input then froze briefly, and the `010041544D530000 / 0xffe` screen appeared. The exact report was unavailable. qs11 removed the redundant AM-owned HOME activation as a plausible interaction risk, without claiming a demonstrated root cause.

The prior qs11 final build encountered a zero-byte `gui_qlaunch_scene_diagnostics.o` during a parallel clean pass. An initial source-enumeration diagnosis was retracted: a fully serial rebuild generated the object and linked both components. The actual cause was not established. Future releases should use a serial final build and reject empty objects rather than describing this as a repaired Makefile bug.

The supplied final message reports a clean API-18 pair at `79f2e94`, payload/permissions checks, and no `hidsysActivateHomeButton` reference. This handoff confirms the preserved archive's exact hash and the source change. No subsequent qs11 hardware result was supplied.

## What the failed approaches taught us

| Approach | Benefit | Observed cost / boundary |
| --- | --- | --- |
| Process targeting | Lightweight and useful for mapped applets. | A live process is not proof of foreground ownership; qlaunch contains several views. |
| Qlaunch scene observation | Separates three repeatedly observed views without a per-firmware address patch. | Does not distinguish native Quick Settings; scenes can linger underneath another applet. |
| OverlayDisp focus/lifetime | Seemed to offer a direct system-UI boundary. | Real panel focus did not arrive reliably; boot events caused false attenuation; missing Loading close edge caused indefinite playback. |
| Configurable HOME heuristic | Actually detected longer panel opens; independent of overlayDisp latches. | Timing, B/touch exits and panel geometry remain heuristics, with no authoritative visibility guarantee. |
| Application Launch/InFocus | Provides a finite Loading interval and excludes ordinary gameplay. | Does not always align with the end of Nintendo's visible animation. |
| Fixed Home delay | Allowed late power events to suppress brief leakage. | Added latency to real Home returns; replaced by coordinated power preparation. |
| PSC plus PDM fallback | Can silence/discard before power transitions while retaining a fallback. | Must handle service failure, queued buffers and stale events coherently. |

The unresolved detector questions concern reproducible observations and the exact owning state/signal. A clean build establishes compilation; console evidence establishes whether Horizon publishes the assumed signals.

## Publication, CI and documentation consolidation

On 2026-09-12 the user authorized rewriting the 18 custom Codex-authored commits
to Ana Jahnel; 227 upstream commits retained their identities. qs11 became
`ffbb9d45437abdd71af42846ff00493fad14a63b` from original
`79f2e9429be41e27d1ceb75d13e07776051abbaf`, without changing its source tree.
The 245-entry [mapping](evidence/author-rewrite-map.csv), original refs/tree,
and independent `evidence/pre-author-rewrite.bundle` preserve recovery data.
Historical ZIP names and embedded stamps still use original hashes.

The fork was created as `anastaci1a/sys-tune`, renamed to `sys-tune-ost`, and
published with `main` as default. The publication record reported GitHub fork
ancestry under HookedBehemoth/sys-tune and owner admin access, verified at that
time. Publication checkpoint `6c5c3172df596b216719e9cc089f389cdf5baabe` added
documentation over qs11; it is not the latest development checkpoint.

Later history includes upstream PR merge `b4ddfd2`, release automation
`a792807`, compiler-path correction `ac126d1`, and environment initialization
`98f0389`. The recorded manual CI run at the latter passed. See
[release automation](RELEASE_AUTOMATION.md). The 2.4.1 publication record and
description are distinct from the Makefile's default `2.4.0-exp.qs11` version.

The documentation consolidation replaces the nested `project/` handoff with a
single root repository, `releases/` with ignored `_dist/`, and `context/` with
this documentation and its preserved evidence. Original snapshot manifests
remain historical rather than falsely describing the modified working tree.
Agent procedures from the outer instructions and `DEVELOPMENT.md` now live in
root `AGENTS.md`; project facts live here. No soundtrack implementation changes
or new console results accompany this reorganization.

The completed move verified all 453 repository files, including Git data,
against SHA-256 snapshots taken immediately before moving them. The active
read-only verifier passed for all 20 preserved evidence files and twelve
original ZIPs, including payload hashes, CRCs, embedded overlay identities and
ExeFS structure. Superproject and libtesla object integrity checks passed;
the dependency HEAD still matches the tracked gitlink at `f766e9b` and its
worktree is clean. The host's `git submodule` shell helper could not run because
its shell utilities were unavailable, so those dependency checks used direct
Git commands instead. Runtime source, Makefiles and the release workflow are
unchanged. No new toolchain build or console tests were run.
