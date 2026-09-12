# Release automation — 2026-09-12

Published workflow changes on main through 98f0389a4851ecd6272c92e4b6b04d608e3960cb.

- `.github/workflows/build.yml` now runs on `release: published` and manual `workflow_dispatch`, not ordinary push or pull_request events.
- Release tags such as `2.4.1` or `v2.4.1` produce `sys-tune-ost-2.4.1.zip`; prerelease/build metadata suffixes are retained. The derived version is passed to both component builds.
- The workflow checks out the event's commit, clean-builds serially, rejects empty object files, validates all four install payloads and the ZIP CRC, and verifies the archive payloads match the actual build files.
- The ZIP uploads as a direct workflow artifact without an extra ZIP wrapper. Release events also attach it to their own release tag using `gh release upload`, without overwriting existing assets. Manual builds create artifacts only.
- Future release tags must include the updated workflow and packaging script. The already-published 2.4.1 release retains its existing install asset.
- 2.4.1's release description was updated to the high-level changelog in [2.4.1 notes](releases/2.4.1.md). It covers changes from upstream v2.1.0-beta.1 and explicitly retains the unresolved hardware-validation limitations.

## Validation

- actionlint 1.7.12 passed (download checksum verified).
- Eight isolated ZIP tests passed: valid package; missing, duplicate, extra, empty and mismatched payloads; nonempty boot flag; preservation of an existing destination.
- Seven tag/version tests passed, covering optional v prefix, prereleases/build metadata, and invalid filename/shell syntax rejection.
- Successful manual GitHub build: https://github.com/anastaci1a/sys-tune-ost/actions/runs/34712445272
- Run commit: 98f0389a4851ecd6272c92e4b6b04d608e3960cb.
- Compiler: aarch64-none-elf-g++ (devkitA64) 15.2.0. libnx: 4.12.0-1.
- Pinned devkitPro image: devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282.
- Artifact: sys-tune-ost-2.4.0-exp.qs11-manual.98f0389.zip, 438164 bytes, SHA-256 a990dfc86fdb89bb0052ac1a2c0050de0836e5ddc320459e940f0ddfac71b140.
- Build log: [CI_BUILD_98f0389.log](evidence/CI_BUILD_98f0389.log).

The real manual run verified compilation, packaging, and artifact upload. Release attachment is configured and linted but was intentionally skipped during the manual run; no test release was created and no existing release ZIP was replaced. These checks are not console/hardware validation.
