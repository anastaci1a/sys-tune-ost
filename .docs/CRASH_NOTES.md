# Crash evidence and unresolved investigation

The handoff contains three unique original 1,104-byte reports in `evidence/crash-reports/`. Two duplicated uploads were byte-identical and were deduplicated; the provenance inventory records the original names and hashes. No report from the later qs10 Ultrahand/User Select/B incident was supplied.

## What the stored reports show

The first fields of each AFE2 file were inspected as little-endian values: magic at offset 0, error description at offset 4, program ID at offset 8, and the register area beginning at offset 16. The values below agree with the earlier reported register interpretation. File-name order is not calendar chronology across reboots.

| Report | Program ID | Error | x0 / first register value | Conclusion supported here |
| --- | --- | --- | --- | --- |
| `report_000000000b503613.bin` | `010000000000BD00` | `0xFFE` | `0x0` | Different program from both sys-tune and ams.mitm; no sys-tune attribution follows. |
| `report_00000002a59a1084.bin` | `010041544D530000` | `0xFFE` | `0xE401` | ams.mitm abort; the earlier discussion considered it the closest candidate for the generic remembered error screen, without proving that match. |
| `report_000000398b51c450.bin` | `010041544D530000` | `0xFFE` | `0x202` | ams.mitm abort with a path-not-found value relevant to the earlier filesystem hypothesis; root cause remains unproved. |

Atmosphère's fatal-display code identifies `010041544D530000` as `ProgramIdAmsMitm` and maps `0xFFE` to an abort. The sys-tune NPDM in this repository uses `4200000000000000`. [Atmosphère fatal display source](https://github.com/Atmosphere-NX/Atmosphere/blob/master/fusee/program/source/fusee_display.cpp)

**Correction to the earlier development narrative:** the highest-numbered report was previously described too confidently as an automatic-backup/archive-bit assertion. Upstream's diagnostic considers the ams.mitm abort and path-not-found value, then also searches the stack for an `automatic_backups/X` prefix. That byte sequence is absent from all three recovered reports. The old interpretation is a hypothesis, not an established archive-bit diagnosis. This also does not establish that sys-tune caused or could not have contributed to an ams.mitm failure. [Atmosphère diagnostic conditions](https://github.com/Atmosphere-NX/Atmosphere/blob/master/fusee/program/source/fusee_display.cpp)

## Latest reported reproduction (after qs10)

The user had Ultrahand open as a launched game finished the stock loading screen and displayed User Select. Pressing B to close the overlay also reached User Select, which began exiting. The overlay then appeared frozen and accepted no input for a moment before the Atmosphère error screen appeared:

```text
Program ID: 010041544d530000
Error Desc: std::abort() called (0xffe)
```

No matching new `.bin` was available. The earlier qs8 hard freeze requiring a forced shutdown is another observation; no report establishes its root cause either. Keep the two incidents and report provenance distinct.

## What qs9/qs11 changed and what that means

qs9 increased the detector's stack reserve from 4 to 12 KiB and used best-effort optional input/service setup. The stack change addresses a plausible risk; it did not establish a stack-overflow cause.

qs11 removed `hidsysInitialize()` and the explicit `hidsysActivateHomeButton()` call. Libnx says AM normally owns activation of that global feed. The current code observes HOME samples passively, while Npad/touchscreen retain best-effort setup. This is a targeted reduction of an interaction risk, not a demonstrated crash fix. [libnx HOME activation documentation](https://switchbrew.github.io/libnx/hidsys_8h.html)

The next investigation needs an exact matching report, the component/firmware versions, and the event sequence. Compare the report's module/address context with appropriate build symbols before concluding whether the problem lies in sys-tune, another overlay component, Atmosphère, the SD/filesystem path, or their interaction. The supplied reports are preserved for that work; the handoff does not claim a full symbolic crash analysis.
