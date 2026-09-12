# Repository lineage and distribution

Local inspection for the consolidation began at `98f0389a4851ecd6272c92e4b6b04d608e3960cb`
on clean `main`, tracking `origin/main`. The Makefile defaults to
`2.4.0-exp.qs11`, API 18. Tagged CI builds override the version; the recorded
2.4.1 release does not imply that the Makefile was changed to 2.4.1.

`origin` is `https://github.com/anastaci1a/sys-tune-ost.git`; `upstream` is
`https://github.com/HookedBehemoth/sys-tune.git`. Local push defaults were
configured to origin/simple. These are recorded repository settings, not a
fresh verification of server permissions or remote release status.

`main` is the active development line; there is no maintained `dev` line.
`power-state-coordinator`, `applet-bgm`, `qlaunch-scene-experiment`, and
`album-video-wake-playlist` are historical checkpoints. The separate
`qs8-audit/power-state-coordinator` reference preserves the discarded regression.
An inherited `master` is an upstream snapshot, not the fork's development line.
The original custom source began at upstream `v2.1.0-beta.1` (`2a6c31c`);
the preserved `origin/master` at `24132cf` in the original evidence was a later
upstream reference, not the first custom commit's parent.

The root now opens directly in Git tools. The repository tree includes `.docs`
and its evidence, including the pre-author-rewrite recovery bundle. `_dist/`
is local, ignored storage for historical install ZIPs; `dist/` is disposable
build output. Neither is required in a source clone. No historical ZIP is
rebuilt or renamed internally by this layout change.

The [development history](DEVELOPMENT_HISTORY.md) and
[identity mapping](evidence/author-rewrite-map.csv) explain old commit IDs.
The pre-rewrite bundle also retains the old authorship as recovery evidence;
it does not change current Git branch authorship.
