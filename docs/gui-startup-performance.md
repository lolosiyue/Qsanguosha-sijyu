# GUI startup timing

[`QSanStartupTiming`](../src/util/startup-timing.h) reads `QSAN_STARTUP_PROFILE=1` to enable opt-in `STARTUP_TIMING` JSON lines on stderr.
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
| `engine.lua_extensions` | [`lua/sanguosha.lua`](../lua/sanguosha.lua), including extension creation, registration and translations |
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
share the catalog's lifetime: [`installCatalog()` and `clearCatalog()`](../src/core/package-catalog.cpp) discard
their cached canonical paths. Root replacement, including directory/symlink
changes, must use the same catalog reload boundary as package replacement.
Relative roots retain uncached `QFileInfo` current-directory/drive semantics;
absolute roots preserve `..` until filesystem canonicalization.

This cache stores only root metadata. It does not cache asset success/failure,
skip mapped-file containment/existence checks, or permit loose-file fallback
when a declared package asset is missing. Skill audio discovery and playback
ordering remain eager and unchanged.

## Measurement records

The 2026-09-20 Debug comparison recorded the before/after phases, instrument-only runs, compatibility regression and final unprofiled startup check.
