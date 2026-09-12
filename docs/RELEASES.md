# Release inventory

All listed source commits are present in `../.git`. Twelve original install ZIPs are included. Six earlier original ZIPs were not recovered; they were not recreated or represented as historical binaries. The hashes below identify the preserved bytes.

**Latest original archive in the handoff inventory:** qs11 `79f2e94`, API 18. **qs8 is a regression build; qs9 has the reported indefinite Loading bug.** qs7 is the recovery lineage baseline, but also has known detection issues. No version is labeled comprehensively hardware-verified by this handoff.

| Version | Commit | API from source | Install archive |
| --- | --- | ---: | --- |
| 2.1.0-applet-bgm.1 | `ea29c19` | 5 | **Not recovered** |
| 2.2.0-ui-ost.1 | `bfdd084` | 6 | **Not recovered** |
| 2.2.1-ui-ost.1 | `e656b39` | 7 | **Not recovered** |
| 2.2.2-ui-ost.1 | `8ce0d0d` | 7 | **Not recovered** |
| 2.2.3-ui-ost.1 | `191c171` | 8 | **Not recovered** |
| 2.3.0-ui-ost.1 | `c0e5dfc` | 8 | **Not recovered** |
| 2.3.1-ui-ost.1 | `a7da4a4` | 8 | [ZIP](../_dist/sys-tune-2.3.1-ui-ost.1-a7da4a4.zip) |
| 2.4.0-exp.qs1 | `ee5fc7b` | 9 | [ZIP](../_dist/sys-tune-2.4.0-exp.qs1-ee5fc7b.zip) |
| 2.4.0-exp.qs2 | `afb4388` | 10 | [ZIP](../_dist/sys-tune-2.4.0-exp.qs2-afb4388.zip) |
| 2.4.0-exp.qs3 | `0408b3a` | 11 | [ZIP](../_dist/sys-tune-2.4.0-exp.qs3-0408b3a.zip) |
| 2.4.0-exp.qs4 | `2320cea` | 12 | [ZIP](../_dist/sys-tune-2.4.0-exp.qs4-2320cea.zip) |
| 2.4.0-exp.qs5 | `f4daa02` | 13 | [ZIP](../_dist/sys-tune-2.4.0-exp.qs5-f4daa02.zip) |
| 2.4.0-exp.qs6 | `fd716e6` | 14 | [ZIP](../_dist/sys-tune-2.4.0-exp.qs6-fd716e6.zip) |
| 2.4.0-exp.qs7 | `7960f42` | 15 | [ZIP](../_dist/sys-tune-2.4.0-exp.qs7-7960f42.zip) |
| 2.4.0-exp.qs8 | `787197a` | 15 | [ZIP](../_dist/sys-tune-2.4.0-exp.qs8-787197a.zip) |
| 2.4.0-exp.qs9 | `1c695f3` | 16 | [ZIP](../_dist/sys-tune-2.4.0-exp.qs9-1c695f3.zip) |
| 2.4.0-exp.qs10 | `9637066` | 17 | [ZIP](../_dist/sys-tune-2.4.0-exp.qs10-9637066.zip) |
| 2.4.0-exp.qs11 | `79f2e94` | 18 | [ZIP](../_dist/sys-tune-2.4.0-exp.qs11-79f2e94.zip) |

## SHA-256 of included ZIPs

**sys-tune-2.3.1-ui-ost.1-a7da4a4.zip**

`9beb51fb2191ad7306fc724486da9145a5bb451887b26e1421f07544108dafa5`

**sys-tune-2.4.0-exp.qs1-ee5fc7b.zip**

`7aaccdf92c5418351b75444df517524a53775d2999db3f417fd5b54b4862e68e`

**sys-tune-2.4.0-exp.qs2-afb4388.zip**

`cea9d4b8de6701c38ee2bd6f66b8d972fcb585d2d1772bb73c4c5c3dd9b5ee25`

**sys-tune-2.4.0-exp.qs3-0408b3a.zip**

`820701dc27c08f81880ed080d0b5024fd4f6cc153038c555b86930ecd9f3a4bd`

**sys-tune-2.4.0-exp.qs4-2320cea.zip**

`893b75a51fada665bc7a48ddf5f81c08f5bf5aa3a9285277b7cb8d3d21f18d4c`

**sys-tune-2.4.0-exp.qs5-f4daa02.zip**

`183b6f092fd2b3e30a67782ba3016c9fd10f44247bfe1c6710162e25cc0fd2a6`

**sys-tune-2.4.0-exp.qs6-fd716e6.zip**

`5c21f8c4fa3e83e172b509946e0ae5b1990f9675aa974aacd5a3e8af3331ce3c`

**sys-tune-2.4.0-exp.qs7-7960f42.zip**

`6be080a0e9dcd375aa0767473e844016289037f5283c0f0459133c511b3534e7`

**sys-tune-2.4.0-exp.qs8-787197a.zip**

`be85352d322f1ee691d6b32b7cddc7b20a31408ded2b230f6e48a26bd87658bc`

**sys-tune-2.4.0-exp.qs9-1c695f3.zip**

`90230e21d5aa646956621198e458dfb73f06ed861a29828b612455f409ae9534`

**sys-tune-2.4.0-exp.qs10-9637066.zip**

`3443a231dddc35fa234bef1f499f227f9b33280489e1d12869e5b3d2af9a17ed`

**sys-tune-2.4.0-exp.qs11-79f2e94.zip**

`41cff15e5bbb58cb5fe4f914c781de7f08408cbc12a61a3b05963fafba4f2e95`

## Verification and coverage

Each included ZIP passed CRC validation, has the expected four-file install payload, and contains a PFS0 ExeFS with `main` and `main.npdm`. Every overlay contains its expected release version. Eleven also contain their filename commit; qs4 contains only `v2.4.0-exp.qs4`, with no commit suffix. The sysmodule Makefile embeds its API but does not define VERSION/GITHASH, so a literal module version/commit stamp is not expected. Original bytes are retained and these limits are recorded rather than treated as proof of a wrong build.

NSO segments were decompressed; enabled segment hashes were checked. qs7/qs9/qs10/qs11 also match the hashes in the supplied release messages. Module identity is tied to the preserved paired artifact and payload hash, not a nonexistent version string.

[ARTIFACT_MANIFEST.json](evidence/ARTIFACT_MANIFEST.json) records byte sizes, full source commits, per-payload hashes, ancestry and the source-bundle provenance. API numbers in that manifest are read from each source Makefile; this handoff does not independently disassemble every historical IPC handler. It does not claim a rebuild or new hardware validation.

The six missing ZIPs correspond to the initial Applet BGM release through 2.3.0. Their exact source remains in history. Rebuilding them later would produce newly built artifacts that should be identified as such, with their toolchain recorded.

A complete original chat export, original toolchain image digest/debug binaries, and the later qs10 crash report are not included because they were not recoverable. Development narrative and known feedback have been reconstructed with those limits stated.

## Later publication

The recorded 2.4.1 GitHub release is described in [its release notes](releases/2.4.1.md). It is not one of the twelve original handoff ZIPs. The later manual CI artifact and hash are in [RELEASE_AUTOMATION.md](RELEASE_AUTOMATION.md). Remote publication state has not been re-queried for this documentation-only migration.
