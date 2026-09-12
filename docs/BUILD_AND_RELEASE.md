# Building, installation and validation

Commands run from the repository root. The development line is `main`; qs11/API 18 is the implementation baseline, not a required historical HEAD.

```sh
git status --short --branch
git rev-parse HEAD
git submodule status --recursive
git config submodule.overlay/lib.url https://github.com/WerWolv/libtesla.git
git submodule update --init --recursive
```

Pinned libtesla: `f766e9b607a05e9756843cbd62b3bfb98be1646c`.

## Toolchain and clean build

The existing build uses GNU Make, devkitA64/libnx, C++23 support, devkitPro Switch packaging tools, and ZIP. `nxExt` is tracked in the repository and built locally. Decoder flags for MP3, FLAC, and WAV are enabled by the top-level Makefile.

Previous releases used `devkitpro/devkita64:latest`, sometimes through an isolated unpacked root filesystem when Docker was unavailable. The exact historical image digest, compiler manifest, unpacking scripts, and debug object outputs were not preserved in the recoverable artifacts. The current CI image is pinned and validated as recorded in [RELEASE_AUTOMATION.md](RELEASE_AUTOMATION.md); it is not evidence of byte-identical historical reproduction.

From the repository root, with Docker available:

```sh
docker run --rm -v "$PWD:/src" -w /src devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282 \
  bash -lc 'set -e; export DEVKITA64="$DEVKITPRO/devkitA64"; export PATH="$DEVKITA64/bin:$PATH"; make -j1 clean; make -j1 dist'
```

Or, from a configured devkitPro shell:

```sh
make -j1 clean
make -j1 dist
```

The supported final build is **serial**. The top-level order is overlay, nxExt, module, then dist. During the earlier qs11 session a parallel clean pass left `gui_qlaunch_scene_diagnostics.o` at zero bytes and linking failed; a serial pass succeeded. The initial diagnosis of a source-enumeration bug was withdrawn. No compiler or Makefile root cause was established. A temporary toolchain extraction ownership problem was also encountered earlier; that was environment setup, not evidence of damaged source.

`make clean` removes `dist/` and normal build outputs; the historical ZIPs in `_dist/` are outside that cleanup. Unsaved debug outputs in the build directories are not preserved by cleanup.

## Source identity and API

The top-level Makefile exports `VERSION`, `GITHASH`, and `API_VERSION`. Both components use the same API and build inputs. The default version is `2.4.0-exp.qs11`, API 18; `GITHASH` comes from the actual checkout. The historical qs11 ZIP was built at original `79f2e94`, before the identity rewrite. Overlay versions also use `git describe --dirty --always` when `RELEASE` is not set.

For a new release, review the source, choose the next version/API as appropriate, commit the intended change, then run the final clean build so compiled identity matches that commit. Keep the source snapshot and install ZIP associated with each other. An incremental build is useful while fixing compile errors; it is insufficient evidence for the final release identity.

The qs8 audit reference is intentionally outside the main ancestry:

```sh
git diff 7960f42 787197a
git diff 7960f42 1c695f3
git diff 9637066 79f2e94
```

The first diff explains the discarded regression, the second the qs7-based recovery, and the third the qs11 changes. These commands use original IDs, which may exist only in the preserved recovery bundle in a fresh clone. Rewritten IDs are in [the mapping](evidence/author-rewrite-map.csv); the original patches are also saved in `docs/evidence/`.

## Required artifact checks for a new release

1. Both component builds and linking succeed from clean objects. Preserve build output; distinguish harmless toolchain ABI notes or optional NPDM-field notices from actual failures.
2. Reject any zero-length `.o` files and empty binaries. The zero-byte `boot2.flag` is intentional and should not fail this check.
3. Confirm the overlay's embedded version/commit identity and no accidental dirty suffix. Confirm the actual sysmodule IPC API handler and overlay bindings agree. The current sysmodule build embeds the API but does not define a VERSION/GITHASH string; use its final-build payload hash and build provenance, not an assumed literal commit stamp. A filename or README string alone does not prove the API.
4. Confirm ExeFS contains `main` and `main.npdm`, and that NPDM identity is `4200000000000000` with hosted services `tune`, `erpt:c`, `caps:a` and the required syscall set. Preserve the existing best-effort observer initialization design.
5. ZIP integrity must pass. Compare the packaged `.ovl` and `exefs.nsp` bytes against the final outputs. The archive must contain the four files below; the boot flag is deliberately empty.
6. Hash the ZIP, retain the exact source/history and submodule revision, and record what was tested on a console separately from compilation/package checks. Preserve ELF/map/toolchain details for future fatal analysis when available.

| Install path inside the ZIP | Source |
| --- | --- |
| `switch/.overlays/sys-tune-overlay.ovl` | `overlay/sys-tune-overlay.ovl` |
| `atmosphere/contents/4200000000000000/exefs.nsp` | `sys-tune/sys-tune.nsp` |
| `atmosphere/contents/4200000000000000/toolbox.json` | `sys-tune/toolbox.json` |
| `atmosphere/contents/4200000000000000/flags/boot2.flag` | Empty boot flag |

The top-level `dist` rule creates the install ZIP in `dist/`. Store the reviewed release outside the build directory with its version and commit in the name. A Git bundle can be produced with `git bundle create <destination> --all`, but a superproject bundle alone does not contain submodule objects. This handoff includes the populated submodule repository specifically to close that gap.

## Installation and rollback

Back up `/config/sys-tune/config.ini` before experimental hardware testing. Install the matching sysmodule and overlay from the **same ZIP** at the SD root, replacing old components at the paths above. Fully reboot so the resident sysmodule and early MITM observers are actually reloaded. Audio belongs under `/music/`; build/edit the playlists and enable System UI OST, which is off by default on a fresh configuration.

The installable payload is the content of a selected install ZIP, not the repository folder. Locally preserved historical ZIPs are in `_dist/`. Rolling back uses another complete paired ZIP and a full reboot. qs8 and qs9 have specifically reported regressions, so they are evidence packages rather than general recommendations.

## Focused next hardware pass

Record the actual system firmware, Atmosphère, loader/Ultrahand, and sys-tune versions; no exact current combination was recovered for qs11. Work through `TODO.md` using the user-observed cases rather than a generic smoke test:

- Cold boot with empty Startup and Quick Settings multiplier 0%.
- Home pause → applet → Home, and Settings → each affected submenu → Settings.
- User Select → HOME → User Select, including a newly opened instance.
- Native Quick Settings open/close, roughly 400 ms holds, disabling attenuation, B and outside touch, and opening Ultrahand alone.
- Octopath Traveler versus 3D All-Stars stock launch timing, then the separate Galaxy in-game handoff.
- Short/canceled launches and both Loading delays with another applet taking priority.
- Sleep/display-off/wake from several starting states, with and without Lock and separate Wake audio.
- Playlist reconciliation, repeat behavior, and compounded Album/Quick Settings gains.

If the reported fatal occurs again during ordinary use, retain its actual `.bin` report and the immediate steps. Do not label the old fatal resolved based only on a successful build, absent `hidsysActivateHomeButton` symbol, or a single uneventful run.

## Read-only repository checks

`python3 .github/scripts/verify-project.py` checks documentation links and preserved evidence. `--archives` additionally verifies the optional local historical ZIPs in `_dist/` against the original manifest (payload hashes, CRC, identities and ExeFS structure). Neither mode requires an unchanged historical HEAD or mutates refs. `git fsck --full --no-dangling` and `git submodule status --recursive` check repository integrity separately.
