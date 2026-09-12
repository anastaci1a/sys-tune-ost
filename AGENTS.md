# Agent working procedure

## Start and keep context current

Work from this repository root. Read [.docs/README.md](.docs/README.md),
[.docs/FUNCTIONALITY.md](.docs/FUNCTIONALITY.md),
[.docs/ARCHITECTURE.md](.docs/ARCHITECTURE.md), and
[.docs/TODO.md](.docs/TODO.md) before changing the project. Keep `.docs` as a
constant reference throughout planning, implementation, review and commits;
consult the relevant configuration, detection, build and history documents
before making a decision, not just at session startup. Project facts and
intent belong in `.docs`; this file contains agent procedures only.

Inspect Git status, branch, recent history and applicable local instructions.
Preserve user changes. Use the actual current `main` lineage, not a historical
handoff hash. Consult `.docs/REPOSITORY.md` for checkpoint/audit branch context.
For source uncertainty, inspect code and record conflicts in the relevant doc;
do not silently reinterpret intended behavior to match an accidental regression.

## Make changes consistently

- Follow the requested scope. A context document is not authorization to
  implement every open feature or publish a release.
- Preserve the session, state ownership, persistence and UI contracts in
  `.docs/FUNCTIONALITY.md`, `.docs/CONFIGURATION.md` and `.docs/DETECTION.md`.
- Before changing detectors, read `.docs/DEVELOPMENT_HISTORY.md` and the relevant
  evidence. Do not reintroduce discarded overlayDisp visibility/loading latches,
  qs8's sticky qlaunch override, or HOME-feed activation without new evidence
  and an explicit rationale addressing their recorded failures.
- Use recorded scene tests and user feedback before asking the user to repeat
  already-known observations or explain the qs8 regression again.
- Keep optional observer initialization best-effort and respect MITM forwarding,
  descriptor bounds, service permissions, synchronization and teardown order.
- Update both sides of IPC together; increase API when interface changes require
  it. Keep component builds paired. Use a clean serial final build and the
  checks in `.docs/BUILD_AND_RELEASE.md` when producing binaries.
- Distinguish source review, build/package validation and console confirmation.
  Never close a crash/detector bug merely because compilation succeeds. Record
  user feedback with its build and concrete observation under the relevant
  stable TODO ID; preserve contradictory or missing evidence explicitly.
- Preserve original reports, ZIPs, hashes and historical provenance. Keep local
  release archives in ignored `_dist/`, outside `dist/` cleanup. Do not rewrite
  released history or overwrite historical binaries. Retired scripts under
  `.docs/evidence/*.txt` are records, not runnable maintenance instructions.
- Default any requested push to the configured fork, never upstream. Ordinary
  feature branches are based on `main`; optional PRs target the fork's `main`.

## Every commit: documentation inspection and revision

Before committing, thoroughly inspect the complete `.docs` set against the
proposed diff, current source, project intent and newly obtained evidence.
Review every indexed document for applicability, including structure,
functionality, configuration, detectors, bugs/TODOs, build/release behavior,
history and evidence references. Revise every affected document in the same
commit; verify unaffected documents rather than adding empty cosmetic edits.
Keep historical records labeled and immutable evidence bytes intact. Add a new
document to the index whenever needed. Put behavior/procedure changes for
agents here, never into the project-fact documents as agent instructions.

Run appropriate checks and review the staged diff for completeness, accidental
artifacts, broken links and unsupported claims. Run
`python3 .github/scripts/verify-project.py` for documentation/evidence changes;
add `--archives` when validating the optional local historical packages.

At the end of **every commit**, inspect `git show --stat`, the committed diff,
Git status, and the entire documentation index again. Confirm the committed
`.docs` accounts accurately cover the final change and that all links, paths,
status distinctions and outstanding TODOs remain consistent. If that inspection
finds a gap, revise the affected docs immediately and amend the just-created
unpublished commit when safe, or make an explicit corrective documentation
commit. Repeat this end-of-commit inspection for the correction. Never defer
documentation synchronization to an unspecified future task.

Finish by reporting what changed, what was checked, and what remains untested
or unresolved. Do not claim a build, console result, publication, or commit
that did not occur.
