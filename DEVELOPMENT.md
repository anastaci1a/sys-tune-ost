# Fork structure and development

This repository is Ana Jahnel's (`anastaci1a`) fork of
[HookedBehemoth/sys-tune](https://github.com/HookedBehemoth/sys-tune).

## Branches

- `main` is this fork's primary development branch. It starts from the qs11
  implementation, version `2.4.0-exp.qs11`, API 18. Experimental features still
  require console validation; being on `main` does not make this a stable release.
- There is no separate `dev` branch. Future changes can use short-lived feature
  branches based on `main`. If a pull request is useful, its target is
  `anastaci1a/sys-tune-ost:main`, within this fork.
- The handoff's `power-state-coordinator` was the latest development branch.
  `applet-bgm`, `qlaunch-scene-experiment`, and `album-video-wake-playlist`
  are earlier checkpoints along the same development history, not parallel
  maintained release lines. Their commits are already included in `main`.
- The local `qs8-audit/power-state-coordinator` reference preserves a discarded
  experiment. It is not the development baseline.
- Any `master` branch initially supplied by GitHub's fork operation is an
  upstream snapshot, not this fork's primary development branch.

The publication setup does not create pull requests to the original repository.

## Remotes

In the maintainer's local checkout, `origin` is
`https://github.com/anastaci1a/sys-tune-ost.git`, the publishing destination.
`upstream` is `https://github.com/HookedBehemoth/sys-tune.git`, for inspecting
upstream history. Ordinary pushes default to `origin`.

## Files and the local handoff

The GitHub repository root corresponds to `project/` in the local handoff:

- `sys-tune/`: resident Switch sysmodule.
- `overlay/`: Tesla/Ultrahand interface, including the pinned `overlay/lib`
  libtesla submodule.
- `common/`: shared configuration and supporting code.
- `ipc/`: interface between the overlay and sysmodule.
- `Makefile`: version, API, builds, and package assembly.

The handoff's sibling `context/`, `releases/`, and `evidence/` directories are
local supporting material, outside this Git repository. They are not uploaded
when this repository is pushed. Original release ZIPs retain their original
commit stamps; rewriting Git authorship did not rebuild those binaries.

## Build context

The source is authoritative where older README descriptions differ. In qs11,
the API is 18, native Quick Settings attenuation uses a HOME-hold heuristic,
and Settings yields to a foreground library applet.

For a fresh clone, initialize the pinned dependency using public HTTPS:

```sh
git config submodule.overlay/lib.url https://github.com/WerWolv/libtesla.git
git submodule update --init --recursive
```

With a configured devkitPro toolchain, use a clean serial final build:

```sh
make -j1 clean
make -j1 dist
```

Always install the overlay and sysmodule from the same build. Compilation and
packaging do not replace console validation. The handoff's detailed bug and
hardware-validation checklist remains in the local `context/TODO.md`.
