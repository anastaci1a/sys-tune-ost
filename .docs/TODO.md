# Project TODO checklist

Implementation baseline: qs11 (original `79f2e94`, rewritten `ffbb9d4`), API 18. Documentation review: 2026-09-12, source at `98f0389`. Publication and CI do not close console checks.

`[x]` means the implementation or stated handoff task exists in the preserved source/artifacts. It does **not** mean all hardware behavior has been confirmed. `[ ]` marks an unresolved issue, required validation, or an explicitly labeled follow-up suggestion. Features and bugfixes are both tracked. Record a commit/build and concrete observation when checking off an open hardware item.

## Highest-priority open bugs and validation

- [ ] **BUG-01 — Resolve the fatal/freeze during Ultrahand + User Select exit.** Latest reported sequence: game finishes initial loading → User Select appears while Ultrahand is open → B dismisses both → short input freeze → `010041544D530000`, `std::abort()`, `0xffe`. The exact later report is missing. If it recurs naturally, retain the matching report and actual firmware/Atmosphère/Ultrahand/sys-tune versions. Correlate process/module/stack evidence before blaming sys-tune. qs11's removed HOME activation is hardening only. See `CRASH_NOTES.md`.
- [ ] **BUG-02 — Validate qs11 Home manual-pause re-entry.** Pause Home mid-track, enter an applet and return. It should resume the retained track/time/queue with Mid-Song Fade In. Pausing while staying on Home must still work.
- [ ] **BUG-03 — Validate Settings submenu ownership in qs11.** Test Settings → Network/Internet, Data Management, Amiibo, Mii Editor → Settings → Home. Compare Mii from User Page. Foreground submenu OST must win over stale Settings; closing it must restore Settings. Lock must still supersede an underlying applet/game.
- [ ] **BUG-04 — Validate qs11 Quick Settings after removing HOME activation.** Use 0% multiplier to make detection obvious. Normal approximately 400 ms opens should attenuate; HOME/B/outside-touch exits should restore. Test cold boot, Home, Settings, other applets, connected/handheld controllers, and opening Ultrahand by itself. Check diagnostics if HOME samples are absent.
- [ ] **BUG-05 — Improve/validate Loading end alignment across games.** qs7 ended too early for Octopath Traveler but matched 3D All-Stars' stock animation. qs10/qs11 deliberately restore that finite focus endpoint plus an End Delay. Establish actual event/visual timing before attempting a new direct endpoint. Do not reinstate the missing-close overlayDisp latch.
- [ ] **BUG-06 — Recheck User Select → HOME → User Select round trip.** User Select must not stick after HOME, and Home must not stick on return to the still-active selection UI. Repeat while a game is launching and after reopening a new applet instance, to expose stale focus data.
- [ ] **BUG-07 — Recheck cold-boot silence/muting regression.** With no Startup track and Quick Settings multiplier 0%, normal Home/Lock/Settings must function without first launching a game. A boot overlayDisp event must never activate attenuation.
- [ ] **BUG-08 — Recheck power transitions.** Sleep/display-off from Home, Settings, Album, a game, and an applet should silence promptly. Wake must not leak old queued Home audio, select an underneath applet instead of Lock, or restart/cancel the wake one-shot incorrectly. Include lock-disabled behavior and empty Startup/Wake playlists.

## Implemented core features

- [x] **FEAT-01 — Per-applet BGM mode** with persistent single-file assignments and browser selection. `ea29c19`.
- [x] **FEAT-02 — System UI OST manager** with independent playlists, active-state controls, a master switch, and silent gameplay. `bfdd084` and later routing refinements.
- [x] **FEAT-03 — Persistent playlist ordering, shuffle, repeat Off/One/All, and configuration migration.** `bfdd084`, `0408b3a`.
- [x] **FEAT-04 — Independent Home, Lock and Settings playlists** using observed scenes `0x00`, `0x0A`, `0x32`. `ee5fc7b`, `afb4388`.
- [x] **FEAT-05 — Home retains session/decoder/time** across interruptions; other normal states restart/reshuffle on activation. `bfdd084`, `8ce0d0d`.
- [x] **FEAT-06 — Random one-shot Startup Sound** with optional replay after sleep/display wake. `bfdd084`, `e656b39`, `0408b3a`.
- [x] **FEAT-07 — Optional separate Wake playlist**, preserved when disabled. `f4daa02`.
- [x] **FEAT-08 — Track-boundary and mid-song fade controls**, nonlinear Off–5 s sliders, sequential fades. `0408b3a`.
- [x] **FEAT-09 — Requested factory fades:** track In 150 ms, track Out off, mid-song In 300 ms, mid-song Out 500 ms; saved settings still win. `7960f42`.
- [x] **FEAT-10 — Global and per-state 0–150% gain**, with global OST volume on the main page. `2320cea`.
- [x] **FEAT-11 — Album-video context multiplier and diagnostics.** `f4daa02`.
- [x] **FEAT-12 — Quick Settings multiplier**, default 50%, range 0–100%, compounded with global/state/Album gains and eased with mid-song fades. `7960f42`; detector replaced subsequently.
- [x] **FEAT-13 — Quick Settings submenu** with Lower OST switch, multiplier and absolute Hold Time; default 400 ms, range 100–1500 ms. `9637066`.
- [x] **FEAT-14 — Loading playlist directly below Home**, limited to initial application launch. `7960f42`.
- [x] **FEAT-15 — Loading Start/End Delay submenu inside its playlist editor**, nonlinear Off–5 s controls, defaults 500 ms/off. `1c695f3`, `9637066`.
- [x] **FEAT-16 — User Select warning** that chosen OST can overlap game music. `9637066`.
- [x] **FEAT-17 — Concise help, usable playlist footer spacing, in-place rows/cursor preservation, and batched edit notifications.** `a7da4a4`, `7960f42`.
- [x] **FEAT-18 — Raw scene, Album video, power, and UI-activity diagnostics**, including event counters/history reset. qs1, qs5, qs6, qs7–qs10.
- [x] **FEAT-19 — Fixed soundtrack menu order:** Startup, Lock, Home, Loading, Settings, then mapped applets. Current `gui_applet_bgm.cpp`. User-configurable reordering of the soundtrack categories is not implemented or recorded here as an agreed requirement.

## Implemented bugfixes and hardening

- [x] **FIX-01 — Reduce transition latency** by retaining Home's decoder, batching playlist config reads, and avoiding headerless-MP3 duration scans. `8ce0d0d`.
- [x] **FIX-02 — Repair repeat/session handling** for repeat One/All and track completion. `0408b3a`; retain separate console validation below.
- [x] **FIX-03 — Separate waiting-for-first-scene from unavailable observer** so boot does not default prematurely to Home. `0408b3a`.
- [x] **FIX-04 — Improve Lock priority over stale applet/game processes**, silence/discard across display/sleep changes, and retain Startup/Wake across Lock → Home. `0408b3a`, `2320cea`, `fd716e6`.
- [x] **FIX-05 — Replace ordinary fixed Home return delay with PSC preparation and PDM fallback.** `fd716e6`.
- [x] **FIX-06 — Preserve active playback across playlist additions/reordering**, pin current track during shuffle rebuild; interrupt only when the current file is removed. `7960f42`.
- [x] **FIX-07 — Baseline an already-running game on sysmodule start**, and suppress Home/Loading during later internal game handoffs. `7960f42`.
- [x] **FIX-08 — Discard qs8 boot overlayDisp→Quick Settings classification and sticky qlaunch update override** by rebuilding on qs7. `1c695f3`.
- [x] **FIX-09 — Add applet focus ownership with a bounded short-HOME fallback** and freshness checks for reopened applets. `1c695f3`.
- [x] **FIX-10 — Increase detector stack from 4 KiB to 12 KiB**, with best-effort optional input setup. `1c695f3`. This does not prove the earlier freeze was a stack overflow.
- [x] **FIX-11 — Remove nonterminating overlayDisp Loading latch** and return to the finite first-focus endpoint. `9637066`.
- [x] **FIX-12 — Classify HOME holds that cross the threshold between polling cycles**, including release-edge classification. `9637066`.
- [x] **FIX-13 — Home clears manual pause on re-entry without rebuilding its retained session.** `79f2e94`; BUG-02 remains open for hardware.
- [x] **FIX-14 — Hide both dependent Wake controls when wake inclusion is disabled**, restore saved options when enabled. `79f2e94`.
- [x] **FIX-15 — Settings yields to a foreground detected applet**, with Lock priority preserved. `79f2e94`; BUG-03 remains open for hardware.
- [x] **FIX-16 — Remove redundant AM-owned HOME-feed activation**, retain passive HOME observation. `79f2e94`; this is not a closed crash diagnosis.
- [x] **FIX-17 — Produce qs11 with a serial clean build after a parallel pass left a zero-byte object.** Reported in the supplied development session; no source/build-system root-cause repair was established.

## Additional open regression checks

- [ ] **TEST-01 — Playlist reconciliation:** ordered additions/moves, shuffled additions/moves, remove current, remove another, empty playlist, edits while manually paused, edits to Home while another state owns audio. Confirm current position and intended next track; unknown duplicate-path semantics should be recorded if encountered.
- [ ] **TEST-02 — Repeat Off/One/All:** actual EOF, next/previous interaction, a single-track list and multi-track shuffle. No comprehensive post-qs3 confirmation was preserved.
- [ ] **TEST-03 — Wake menu dynamics:** disabling inclusion hides both controls immediately; re-enable restores saved separate-pool choice and content without a focus/crash issue.
- [ ] **TEST-04 — Loading edge cases:** short launch under Start Delay, launch canceled by B/HOME, new launch during End Delay, User Select or another applet taking priority, launching with empty Loading playlist, restarting sysmodule during gameplay.
- [ ] **TEST-05 — 3D All-Stars → Galaxy internal handoff:** no Home/Loading OST during game-owned visuals. Distinguish this from the initial stock Switch launch animation.
- [ ] **TEST-06 — Gain composition:** distinct global/state values, Album video context, Quick Settings open at the same time, and disabling attenuation while open. Check fade restoration and avoid mistaking a configured multiplier for a detector failure.
- [ ] **TEST-07 — Album video observation:** actual clip playback start/stop, photos, exiting Album during a clip, and retained Home behavior. Source and diagnostics exist; broad hardware coverage is not recorded.
- [ ] **TEST-08 — Observer fallback:** where practical, verify unavailable/no-sample states remain nonfatal and diagnostics explain the missing signal. Do not introduce a failure by changing system ownership just to manufacture a test.

## Ongoing engineering follow-ups (suggestions, not additional user feature promises)

- [x] **ENG-01 — Consolidate and correct project documentation.** Root README now indexes `.docs`; usage, API 18, HOME heuristic/submenu, Loading delays, Settings priority and serial build guidance are documented there. Agent procedures are in root `AGENTS.md`. No runtime change or console validation is implied.
- [x] **ENG-02 — Pin a working devkitPro image digest and record compiler/libnx versions.** Completed in the release workflow; the 2026-09-12 manual run passed with devkitA64 GCC 15.2.0 and libnx 4.12.0-1. See `RELEASE_AUTOMATION.md` for the image digest and full validation record. This does not reproduce the historical release binaries or close hardware checks.
- [ ] **ENG-03 — Investigate the observed zero-byte parallel-build object only if relevant to restoring parallel builds.** Preserve serial final builds meanwhile. Check top-level `nxExt`/module ordering and actual failed logs; do not claim a proved cause from a hypothesis.
- [ ] **ENG-04 — Investigate an authoritative Quick Settings visibility signal if new evidence appears.** Existing scene/process/focus approaches failed on this console. Keep the configurable/disable-able heuristic until a better signal is validated.
- [ ] **ENG-05 — Measure heap/stack high-water usage before further observer growth.** Source still contains inherited heap-sizing TODOs in `main.cpp`; existing allocation sizes are not measured guarantees.
- [ ] **ENG-06 — Preserve debug ELF/map/toolchain metadata for future crash correlation.** The install ZIP and Git tree alone are insufficient for every later address-level diagnosis.

## Historical handoff milestones (superseded layout)

- [x] **HANDOFF-01 — Restore qs11 as a normal nested Git repository** with all preserved branches/tags, qs8 audit ref, and populated pinned libtesla dependency.
- [x] **HANDOFF-02 — Include twelve original release ZIPs and hashes**, document six earlier missing ZIPs without passing off rebuilds as historical originals.
- [x] **HANDOFF-03 — Write this completed/open feature-and-bugfix checklist, iteration history, current context, and build/release guide.**
- [x] **HANDOFF-04 — Recover three unique earlier fatal reports**, deduplicate identical copies, preserve bytes and distinguish a hypothesis from a proven crash cause.

## Next update record

Append new entries with: date, exact release/commit/API, console/firmware/Atmosphère/Ultrahand versions, steps, expected/actual behavior, relevant diagnostic counters or report filename, and which checklist item the evidence resolves. Preserve old observations instead of replacing them with a generic “fixed.”
