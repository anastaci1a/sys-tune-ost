# sys-tune: Applet BGM fork

Background audio player for Nintendo Switch with a Tesla/Ultrahand overlay.
This local fork is based on
[`v2.1.0-beta.1`](https://github.com/HookedBehemoth/sys-tune/releases/tag/v2.1.0-beta.1)
and adds one-song-per-applet background music.

## Applet BGM

Applet BGM is disabled by default. When enabled:

- each mapped Home Menu or Nintendo applet plays one looping MP3, FLAC, WAV,
  or WAVE file;
- changing applets stops the old song and starts the new applet's song;
- an unmapped applet is silent;
- opening a regular application or game stops Applet BGM; and
- pressing HOME while a game is suspended resumes the Home Menu song.

The overlay includes mappings for Home Menu / Settings, Album / Photos,
Controllers, User Page, Mii Editor, Amiibo, Nintendo eShop, User Select,
network and web applets, Software Keyboard, and several less common system
applets.

System Settings is part of the retail `qlaunch` process, not a separate
applet. It therefore shares the **Home Menu / Settings** song. Separating the
two would require a firmware-specific qlaunch hook, which this fork
intentionally avoids.

HOME focus detection uses the bounded `pdm:qry` play-event approach described
by masagrator in [upstream issue #55](https://github.com/HookedBehemoth/sys-tune/issues/55).
If that service cannot be opened, the mode degrades safely: mapped library
applets still work, but the Home Menu song resumes only after the game closes.

## Installation

1. Back up `/config/sys-tune/config.ini` if desired.
2. Remove the previous matching components so an old overlay and new sysmodule
   cannot be mixed:
   - `/atmosphere/contents/4200000000000000/`
   - `/switch/.overlays/sys-tune-overlay.ovl`
3. Copy the contents of `dist/` to the root of the SD card.
4. Put music files under `/music/` on the SD card. Full paths must stay below
   256 bytes.
5. Fully reboot the console.
6. Open Ultrahand/Tesla, choose **sys-tune → Applet BGM**, assign songs with
   **A**, clear an assignment with **Y**, then turn **Applet BGM** on.

Applet BGM temporarily overrides playback but does not erase the normal
playlist. Turning it off returns sys-tune to its original playlist and
per-title behavior.

The overlay and sysmodule use API version 5 in this fork. Install both from the
same build; an older overlay will correctly report the sysmodule as
unsupported, and vice versa.

## Building

The official devkitPro image is the reproducible build environment:

```sh
git submodule update --init --recursive
docker run --rm -v "$PWD:/src" -w /src devkitpro/devkita64:latest \
  bash -lc 'make -j$(nproc) dist'
```

Installable files and a release ZIP are written under `dist/`.

## Screenshots
![Main](/sample/libtesla_1586882452.jpg)
![Main](/sample/libtesla_1586882672.jpg)
![Main](/sample/libtesla_1586882735.jpg)
(Alpha values are wrong in these screenshots. The overlay will be less transparent.)

## Special thanks to:
- [mackron](http://mackron.github.io/) who made the awesome [audio decoders used here.](https://github.com/mackron/dr_libs/)
- [WerWolv](https://werwolv.net/) for making libtesla, the UI library used for the control overlay.
- [TotalJustice](https://github.com/ITotalJustice) for bug fixes, adding some features and bad code.
- [masagrator](https://github.com/masagrator) for documenting the bounded
  `pdm:qry` application-focus technique.
- [ppkantorski](https://github.com/ppkantorski/sys-tune) for a working
  open-source reference implementation of HOME-aware focus behavior.

## Info for developers
I implemented an IPC interface accessible via service wrappers [here](/ipc/).

My [Tesla overlay](/overlay/source/) uses these bindings.
