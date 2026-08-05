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
- Startup Sound chooses one random entry once when the sysmodule starts at boot,
  then yields when it finishes or when anything other than Home opens; and
- global 0–5 second fade-in and fade-out settings smooth track, applet, pause,
  Startup, and stop transitions. These are sequential fades, not overlapping
  crossfades.

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

## Detectable states

The overlay includes Album / Photos, Controllers, User Page, Mii Editor,
Amiibo, Nintendo eShop, User Select, network and web applets, Software
Keyboard, and several less common system applets.

Home, retail System Settings, and the "press the same button three times"
Entrance/Lock Screen are all internal views of
[`qlaunch` (`0100000000001000`)](https://layoutdocs.themezer.net/guide/2-firmware-files/).
They therefore share **Home / Settings / Lock**. The public libnx interface only
exposes the user's [lock-screen enabled setting](https://github.com/switchbrew/libnx/blob/master/nx/include/switch/services/set.h),
not which internal qlaunch view is currently visible. Separating those views
reliably would require a firmware-specific qlaunch hook.

HOME focus detection uses the bounded `pdm:qry` play-event approach described
by masagrator in [upstream issue #55](https://github.com/HookedBehemoth/sys-tune/issues/55).
If that service cannot be opened, mapped library applets still work, but Home
resumes only after the foreground application closes.

## Configuration and migration

Configuration is stored in `/config/sys-tune/config.ini`. Playlist contents,
order, shuffle, repeat, fades, volume, and the master switch survive reboot.

On first launch this build automatically imports:

- every single-song applet assignment made by the earlier Applet BGM build; and
- the old startup path into the new Startup Sound pool when it points to a file.

The migration is one-time and does not delete the legacy keys.

## Installation

1. Back up `/config/sys-tune/config.ini` if desired.
2. Remove the previous matching components so an old overlay and new sysmodule
   cannot be mixed:
   - `/atmosphere/contents/4200000000000000/`
   - `/switch/.overlays/sys-tune-overlay.ovl`
3. Copy the contents of `dist/` to the root of the SD card.
4. Put audio files under `/music/` (subfolders are supported).
5. Fully reboot the console.
6. Open Ultrahand/Tesla, choose **sys-tune → Manage soundtracks**, build each
   playlist, then enable **System UI OST** on the main page.

Use **A** on “Add songs” to open the browser and **X** there to add every
supported file in the current folder. In a playlist, use **Y** to remove and
**ZL/ZR** to move an entry. Fade and output-volume controls live under
**Misc Options**.

The overlay and sysmodule use API version 6. Install both from the same build;
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
