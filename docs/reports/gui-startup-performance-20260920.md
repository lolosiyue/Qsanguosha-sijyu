# GUI startup comparison — 2026-09-20

The observations below correspond to the source and binary snapshots in the evidence directory. [Timing boundaries and measurement procedure](../gui-startup-performance.md).

Local measurement evidence is under `builds/startup-profile-20260920/`.
The per-skill log run (`media.*`) is diagnostic only and must not be used for a
performance comparison because its logging overhead is substantial. Compare
`aggregate-before.*` and `aggregate-after.*`, which use the same aggregated
instrumentation.

## Local Debug comparison

Same Windows Qt 6.11.1 runtime, L asset root, existing user configuration and
`--ui-startup-smoke` arguments; one low-overhead observation per revision:

| Stage | Before (ms) | After (ms) |
| --- | ---: | ---: |
| Common setup before main window | 27,828.55 | 12,504.79 |
| Engine, inclusive | 27,552.72 | 12,305.49 |
| Native packages | 6,786.36 | 2,195.22 |
| Lua/extensions | 19,917.57 | 9,501.29 |
| Audio source discovery, nested | 16,374.80 | 3,857.95 |
| Settings | 25.56 | 22.42 |
| UI fonts | 35.62 | 25.01 |

Both observations discovered audio sources 11,726 times and reported 4,083
generals. Common setup fell by 55.06% in this pair; this is not a statistical
benchmark or a Release/Android/XP performance claim. Earlier instrument-only
runs varied, so filesystem caching and background load remain sources of noise.

GUI and package catalog targets compiled. The existing package catalog
executable plus the new root/asset regression passed directly (under one
second), and both startup runs reported ready and exited with code 0. No local
CTest, full game, manual GUI/audio acceptance or cross-platform gate was run.

After preserving the relative-root compatibility path, the final GUI and catalog
targets were rebuilt. The catalog fixture, including a Windows drive-relative
root, passed in 0.791 seconds. A final startup run with profiling disabled also
reported ready, zero Qt critical messages, exit code 0, and no timing output
(`final.*`; process wall time 17.077 seconds). This is a final startup check,
not another per-phase performance sample.
