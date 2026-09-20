# GUI startup timing

`QSAN_STARTUP_PROFILE=1` enables opt-in `STARTUP_TIMING` JSON lines on stderr.
The normal setting performs no timing or logging. Use the same Debug executable,
asset root, user configuration, Qt runtime and startup arguments for comparisons.

| Phase | Boundary |
| --- | --- |
| `main.before_window` | `main()` entry through common GUI setup, before dispatch to the main window/startup controller; excludes DLL loading and static initializers |
| `main.engine` | `EngineBootstrap::initialize()` including all Engine child phases |
| `main.settings` | `Config.init()` |
| `main.ui_fonts` | `UiConfig.init()`, including font registration and selection |
| `main.theme_font_apply` | Theme/palette and application font application |
| `engine.native_packages` | Native package factories and registration |
| `engine.lua_extensions` | `lua/sanguosha.lua`, including extension creation, registration and translations |
| `engine.*snapshot` | Rule content snapshot/identity preparation |
| `package.*`, `skills.register`, `skill.media_sources` | Aggregated hot-function durations on the startup thread |

Top-level phases emit `begin`/`end`; hot functions emit one `aggregate` record
with `calls` and summed `elapsed_ms` after common GUI setup. Nested durations are
inclusive: do not add `skill.media_sources` to its enclosing package/Lua phase.
Aggregate records avoid thousands of synchronous log writes distorting the
measurement. A timeout or `exit()` may leave only a `begin` marker; that phase is
incomplete, and aggregate totals may be unavailable. Other entry points such as
server/game scenarios are not covered by the `main.before_window` boundary.

For a bounded Windows measurement, launch the existing `--ui-startup-smoke`
entry with `--asset-root <runtime-root> --ui-startup-timeout-ms 55000
--ui-startup-report <absolute-report.json>`, redirect stdout/stderr to separate
files, and enforce an external 60-second process deadline. Prepend the matching
Qt Debug `bin` to PATH. Do not use CTest or start a game for this measurement.
The report establishes automatic startup readiness; visible UI acceptance and
complete-game/teardown validation remain separate gates.

## Root metadata reuse

Skill construction eagerly discovers available audio sources. Each file probe
resolves the runtime root and package catalog root. Those directory paths now
share the catalog's lifetime: `installCatalog()` and `clearCatalog()` discard
their cached canonical paths. Root replacement, including directory/symlink
changes, must use the same catalog reload boundary as package replacement.
Relative roots retain uncached `QFileInfo` current-directory/drive semantics;
absolute roots preserve `..` until filesystem canonicalization.

This cache stores only root metadata. It does not cache asset success/failure,
skip mapped-file containment/existence checks, or permit loose-file fallback
when a declared package asset is missing. Skill audio discovery and playback
ordering remain eager and unchanged.

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
