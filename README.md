# sys-tune: System UI OST Manager fork

Background soundtrack manager for Nintendo Switch system UI, controlled from
a Tesla/Ultrahand overlay. This local fork is based on
[`v2.1.0-beta.1`](https://github.com/HookedBehemoth/sys-tune/releases/tag/v2.1.0-beta.1).

## System UI OST mode

The master **System UI OST** switch is disabled by default. When enabled:

- Home and each mapped Nintendo applet have independent persistent playlists;
- regular applications and games remain silent;
- non-Home applets restart (and reshuffle, when enabled) on each activation;
- Home retains its queue, track, and playback position while interrupted, then
  fades back in and resumes;
- each normal playlist has persistent shuffle and repeat Off / One / All modes;
- Startup Sound chooses one random entry once when the sysmodule starts at cold
  boot, with an optional **Include Wake from Sleep** toggle to choose a fresh
  random entry after every sleep-wake or display-on event; it may continue
  through the Lock Screen, then yields when it finishes or when Settings,
  another applet, or a game opens;
- separate 0–5 second track-boundary and mid-song fade controls: the original
  Fade In/Out settings now apply only to track starts and natural endings,
  while Mid-Song Fade In/Out applies to interruptions, manual pause/resume,
  early Startup cancellation, and Home's retained pause/resume. These are
  sequential fades, not overlapping crossfades;
- persistent 0–150% volume for every UI-state soundtrack, multiplied by the
  global 0–150% OST volume. Gain above 100% is applied in software and can clip
  already-loud source files;
- a 0–100% native Quick Settings multiplier (50% by default), eased with the
  Mid-Song fades and multiplied with the global, state, and Album-video gains.
  Tesla and Ultrahand overlays do not activate it; and
- a Loading Screen playlist for the interval between an application's launch
  and its first in-focus event. Later in-game loading screens remain silent.

Sleep/off detection uses Horizon's power-state coordinator, which asks the
sysmodule to prepare before suspension. Entering either display-off or system
sleep immediately holds playback silent; wake discards any queued pre-sleep
audio before routing Startup/Lock/Home. Recorded
[`PlayEvent` power-state changes](https://switchbrew.org/wiki/Shared_Database_services#PlayEvent)
remain a fallback if the coordinator is unavailable.

The overlay's top playback panel always controls the currently active UI-state
playlist. Previous/next, seek, play/pause, repeat, and shuffle change ownership
as the active applet changes, so manually pausing one state does not pause the
next. Repeat and shuffle edits are saved directly to that applet's
configuration. The panel hides repeat/shuffle during the special one-shot
Startup state.

Supported audio files are MP3, FLAC, WAV, and WAVE. Paths must be shorter than
256 bytes, and each state is limited to 64 entries. M4A/AAC is not included:
it requires an additional MP4 demuxer and AAC decoder, and the available
licensing/memory tradeoffs are not suitable for this small resident sysmodule.

State detection is polled every 50 ms. The next soundtrack begins after the
configured sequential Mid-Song Fade Out; playlist loading and decoder setup do
not add an intentional wait. HOME's live decoder is retained while another
applet is in front, so returning HOME does not reopen and seek through the track.
Headerless MP3s also start without an up-front whole-file duration scan. Such a
file shows an unknown total duration (`--:--`) and disables seeking for that
playback, but otherwise plays, loops, and advances normally.

## Detectable states

The overlay includes Album / Photos, Controllers, User Page, Mii Editor,
Amiibo, Nintendo eShop, User Select, network and web applets, Software
Keyboard, and several less common system applets.

Home, retail System Settings, and the "press the same button three times"
Entrance/Lock Screen are all internal views of
[`qlaunch` (`0100000000001000`)](https://layoutdocs.themezer.net/guide/2-firmware-files/).
Process-ID detection alone therefore cannot distinguish them. This experimental
branch additionally observes qlaunch's `SystemAppletScene` report and maps the
three values verified repeatedly on hardware:

- `0x00`: Home Menu
- `0x0A`: Entrance / Lock Screen
- `0x32`: System Settings

Each now has an independent persistent playlist. Home keeps the old qlaunch
playlist and its pause/resume behavior; Settings and Lock restart like other
non-Home UI states. An unrecognized reported scene stays silent instead of
being mislabeled. While the observer is waiting for its first scene—such as
during the boot logo—qlaunch also stays silent, allowing a configured Startup
Sound to own that period. Only an observer that is genuinely unavailable falls
back to Home for compatibility.

A verified Lock or Settings scene takes precedence even when a game or applet
process remains alive underneath qlaunch. On a lock-enabled console, boot and
wake remain silent until the concrete Lock scene appears. Returns from Album,
another applet, or a game to Home have no fixed confirmation delay. Consoles
with the Lock Screen disabled retain a 1.5-second boot/wake fallback because
they never produce a concrete Lock scene.

The observer touches only qlaunch's `erpt:c` connection through Atmosphere's
MITM extension, records raw scene transitions, and forwards every original
request unchanged. It supports the legacy and 21.0.0+ ERPT context layouts,
CMIF domain requests, and cloned SDK sessions.

HOME focus detection uses the bounded `pdm:qry` play-event approach described
by masagrator in [upstream issue #55](https://github.com/HookedBehemoth/sys-tune/issues/55).
The same event stream supplies two additional signals: focus of Nintendo's
`overlayDisp` applet identifies the native hold-HOME Quick Settings panel, and
an application's Launch-to-InFocus interval identifies the Loading Screen
state. Later in-game program hand-offs stay silent. These signals and their raw
application-event counters can be inspected and reset under **Additional
Options -> UI Activity Diagnostics**. If the service cannot be opened, these
three event-based behaviors are unavailable, while mapped library applets and
qlaunch scene detection continue to work.

## Configuration and migration

Configuration is stored in `/config/sys-tune/config.ini`. Playlist contents,
order, shuffle, repeat, Startup wake mode, all four fades, volume, and the master
switch survive reboot. **Include Wake from Sleep** and **Use Separate Wake
Playlist** are off by default. Disabling the separate Wake playlist leaves its
tracks intact and makes wakes use the main Startup playlist again. Factory fade
defaults are 150 ms Fade In, Fade Out off, 300 ms Mid-Song Fade In, and 500 ms
Mid-Song Fade Out. Existing explicit values are not overwritten; older builds
that predate the mid-song controls retain their prior fade values during
migration.

On first launch this build automatically imports:

- every single-song applet assignment made by the earlier Applet BGM build; and
- the old startup path into the new Startup Sound pool when it points to a file.

The migration is one-time and does not delete the legacy keys.
An existing **Home / Settings / Lock** playlist remains under qlaunch's real
program ID and therefore becomes the **Home Menu** playlist automatically;
the new Settings and Lock playlists begin empty.

## Installation

1. Back up `/config/sys-tune/config.ini` if desired.
2. Remove the previous matching components so an old overlay and new sysmodule
   cannot be mixed:
   - `/atmosphere/contents/4200000000000000/`
   - `/switch/.overlays/sys-tune-overlay.ovl`
3. Copy the contents of `dist/` to the root of the SD card.
4. Put audio files under `/music/` (subfolders are supported).
5. Fully reboot the console.
6. Open Ultrahand/Tesla, choose **sys-tune → Manage Soundtracks**, build each
   playlist, then enable **System UI OST** on the main page.

Use **A** on “Add Songs” to open the browser and **X** there to add every
supported file in the current folder. In a playlist, use **Y** to remove and
**ZL/ZR** to move an entry. Every playlist editor has its own **Soundtrack
Volume** slider. Global **OST Volume** is directly below the player controls on
the main page. All four fade controls live under **Additional Options** and use
tapered sliders from off through 5 seconds, with finer steps for short fades and
progressively wider steps for long fades.

Playlist edits are reconciled by path. Adding or moving tracks does not restart
the current file. Ordered playlists immediately adopt its new position and
neighbors; shuffled playlists keep the current file and reshuffle every other
entry as the new upcoming queue. Removing the playing file is the one edit that
interrupts it and begins the rebuilt queue.

### Experimental Qlaunch Scene Diagnostics

After a full reboot, open **Additional Options → Qlaunch Scene Diagnostics**. The
observer should progress from **Installed — Waiting** to **Qlaunch Connected**
and then **Receiving Scenes**. The history remains available to identify more
qlaunch views in a later build. Any value not listed above is intentionally not
assigned a soundtrack yet.

The early hook uses only a runtime future-MITM declaration; it does not install
a persistent `mitm.lst` file. If this experimental build ever disrupts boot,
power off and restore the stable sysmodule and overlay from the previous ZIP.

**Additional Options → UI Activity Diagnostics** shows whether native Quick
Settings focus and application Launch/InFocus signals are arriving. This first
hardware-facing build exposes both counters so unsupported firmware behavior
can be distinguished from an empty Loading playlist.

Playlist moves, removals, and additions update rows in place without rebuilding
the menu or resetting its cursor. Queue reconciliation is deferred until leaving
the playlist editor, hiding the overlay, or closing it, so a sequence of edits
is applied as one update while the retained current file keeps playing.

The overlay and sysmodule use API version 15. Install both from the same build;
an older overlay will correctly report the sysmodule as unsupported, and vice
versa.

## Building

The official devkitPro image is the reproducible build environment:

```sh
git submodule update --init --recursive
docker run --rm -v "$PWD:/src" -w /src devkitpro/devkita64:latest \
  bash -lc 'make -j$(nproc) dist'
```

Installable files and a release ZIP are written under `dist/`.

## Special thanks

- [mackron](http://mackron.github.io/) for the audio decoders.
- [WerWolv](https://werwolv.net/) for libtesla.
- [TotalJustice](https://github.com/ITotalJustice) for sys-tune improvements.
- [masagrator](https://github.com/masagrator) for documenting the bounded
  `pdm:qry` application-focus technique.
- [ppkantorski](https://github.com/ppkantorski/sys-tune) for an open-source
  reference implementation of HOME-aware focus behavior.

## Developer notes

The IPC interface is in [`ipc/`](/ipc/), and the overlay uses those bindings
from [`overlay/source/`](/overlay/source/).
