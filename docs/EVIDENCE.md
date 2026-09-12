# Evidence inventory and interpretation

The evidence directory preserves the original non-Markdown context files.
Their original bytes are covered by [preservation.json](evidence/preservation.json).
Source history, user observations and prior assistant interpretations are
separate evidence classes; compilation and successful packaging are not console
confirmation. The historical narrative uses original commit IDs, translated by
the rewrite map where needed.

| Evidence | Meaning and limits |
| --- | --- |
| `evidence/qs*.patch` | Four original qs7→qs8, qs7→qs9, qs9→qs10, qs10→qs11 source comparisons |
| `evidence/git-*.txt` | Original handoff refs and readable history; snapshot, not current refs |
| `evidence/crash-reports/` | Three unique unmodified 1,104-byte AFE2 reports |
| `evidence/crash-report-inventory.json` | Five received names, including byte-identical duplicates; original paths retained |
| `evidence/ARTIFACT_MANIFEST.json` | Original eighteen-release source/ZIP provenance, twelve available ZIPs, payload hashes and source API numbers |
| `evidence/FILE_MANIFEST.json` | Historical handoff inventory, already predating some later edits; not a current working-tree manifest |
| `evidence/author-rewrite-map.csv` | 245 mappings; eighteen custom identity rewrites, 227 unchanged upstream commits |
| `evidence/author-rewrite-refs-before.txt`, `author-rewrite-tree-before.txt` | Pre-rewrite ref/tree records |
| `evidence/pre-author-rewrite.bundle` | Independent original Git recovery archive; does not include all submodule objects |
| `evidence/CI_BUILD_98f0389.log` | Recorded successful manual build and upload log |
| `evidence/verify_handoff.py.txt` | Retired unchanged-handoff verifier, preserved as text; incompatible with ongoing development |
| `evidence/verify-author-rewrite.ps1.txt` | Retired rewrite-time checker, preserved as text; includes ref deletion and obsolete absolute paths |

Historical paths in these original records intentionally retain `context/`,
`project/`, and `releases/`. Their current equivalents are `docs/evidence/`
for evidence, the repository root for source, and `_dist/` for archives.
These snapshots are not instructions to restore an old branch or folder layout.
The active read-only check is `.github/scripts/verify-project.py`.

Six original pre-2.3.1 ZIPs, a complete original chat export, the historical
toolchain digest/debug binaries and the later qs10 fatal report were not
recoverable. The twelve preserved ZIPs and three unique reports are evidence,
not proof of a hardware-stable release. The exact later abort remains unpaired
with a report. [CRASH_NOTES.md](CRASH_NOTES.md) records the corrected attribution.
