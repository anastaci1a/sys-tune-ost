# sys-tune-ost project documentation

sys-tune-ost assigns music to Nintendo Switch system UI states using a resident
sysmodule and Tesla/Ultrahand overlay. Gameplay is intended to remain silent.
The current implementation is qs11, API 18. The unresolved freeze/fatal and
console regression checks remain open despite the successful later CI build.

| Document | Contents |
| --- | --- |
| [Functionality](FUNCTIONALITY.md) | Product intent, playlist/session behavior, formats and limits |
| [User guide](USAGE.md) | Installation, controls, diagnostics and limitations |
| [Architecture](ARCHITECTURE.md) | Complete folder/source map, runtime threads, IPC and ownership |
| [Configuration](CONFIGURATION.md) | Defaults, units, limits, state IDs and migration |
| [Detection](DETECTION.md) | Qlaunch, applets, Quick Settings, Loading, power and observer boundaries |
| [TODO](TODO.md) | Stable bug/feature/fix/test IDs and implementation versus hardware status |
| [Crash evidence](CRASH_NOTES.md) | Report findings, unresolved reproduction and attribution limits |
| [Build and release](BUILD_AND_RELEASE.md) | Toolchain, build outputs, package checks and console validation |
| [Release automation](RELEASE_AUTOMATION.md) | CI triggers, naming, pinned environment and recorded run |
| [Repository](REPOSITORY.md) | Branches, remotes, source identity and distribution layout |
| [Development history](DEVELOPMENT_HISTORY.md) | All eighteen iterations, failed approaches, rewrite and publication |
| [Releases](RELEASES.md) | Historical archive inventory, missing releases and checksums |
| [2.4.1 notes](releases/2.4.1.md) | Published release description preserved from context |
| [Evidence](EVIDENCE.md) | Supporting files, historical snapshots and verification limits |

Agent behavior and the per-commit maintenance procedure are defined only in
[AGENTS.md](../AGENTS.md). This directory documents the project itself.
Current facts above supersede old snapshot claims about a clean original qs11
handoff, no remotes, a nested repository, API 15, and overlayDisp panel focus.
Historical observations and hashes remain explicitly historical.
