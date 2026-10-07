# Local battle statistics (first implementation)

This is a C++ projection of authoritative `ResolutionHistoryService` values,
shared by the native server and Qt viewer. There is no Lua statistics collector,
Python runtime, skill-by-skill instrumentation, or remote API. Open **本機實戰統計**
from the general overview. The selected general initializes the filter; clearing
the filter shows all recorded generals. Reading and writing run off the game/UI
threads, except for tiny atomic timeline-journal writes and a nonwaiting new-root
existence check at arming. Global restore never waits for a SQLite write lock.

The integrated restricted rewind executor now calls the production Room hook
after native/Lua publication. Room and WorldStore share one GameTimeline; the
statistics Match is only a projection receipt. Automatic pre-turn retry and the
local debug `retry stats` command acknowledge the same generation without
performing a second game restore. The imported GUI, TS catalog and synthetic UI
fixture documentation remain byte-identical to the statistics handoff. Normal
GUI/network rollback remains unsupported. See `game-state-contract.md` for the
restricted console and combined writer/restore validation.

## Storage and capture

`BattleStatistics::defaultDatabasePath()` resolves to
`QStandardPaths::AppLocalDataLocation/battle-statistics.sqlite`. The dialog shows
the actual path. GUI-hosted local rooms use the GUI application's database;
standalone dedicated servers use their own application data location. Remote
clients do not download private server histories or aggregate them locally.

The schema stores each root match, its generation/branch, metadata and pure-value
history, participant projections, and per-player turn rows. Indexes partition
dataset/environment/general pairs. A single background writer serializes jobs;
SQLite transactions atomically replace the complete match contribution. No
`GameSnapshot` or card-effect QVariant serialization is involved. The history-only
snapshot retains move and numeric-state facts for future reanalysis as well as
the facts used by version 1. This is an explicit SQLite runtime dependency:
distributions must ship Qt Sql and the QSQLITE driver (`libqt6sql6-sqlite` for
the Docker image). A missing driver/write failure is logged, never reported as
a successful save.

Capture starts before GameReady. Terminal intent is marked before GameOver
callbacks. The RoomThread worker's final guard freezes history after nested
resolution guards unwind and before worker/Lua cleanup. Thus the decisive damage,
including separately committed armor/HP components, survives terminal exceptions.
The terminal turn remains marked truncated. An unfinished/cancelled room is not
submitted as a completed match. Empty/dot winners and abnormal terminal causes
are isolated.

## Isolation and interpretation

* `human`: human seats from completed naturally controlled games.
* `ai_self_play`: robot seats only when the entire roster is robots.
* `human_ai_opponent`: robots in games containing humans; visible but not classified.
* `mixed`: any trust/offline/control transition, external controller, or changed
  general/role isolates the whole match. Original identities, subsequent identity
  values and controller-state provenance remain stored. Version 1 does not assign
  statistics to individual changed-general segments.
* `excluded`: scenario/mini/custom/Work/replay-takeover and other excluded terminal
  results. Ordinary replay playback has no server capture hook.

Environment groups include mode, player count, participant role, full C++/Lua
rules identity, analysis version and whether extra turns occurred; ordered
general/second-general identities are additional grouping keys. Datasets never
share samples. The display's short environment fingerprint is only a label;
grouping uses the full identity.

Damage is committed HP loss plus committed armor loss, not requested damage.
Recovery uses committed HP increases. Skill cards are excluded from card-use
frequency; Slash/AOE use the recorded card class ancestry. Per-turn statistics
follow each player's own turn. Extra and truncated turns remain in the raw totals,
but their numerators and denominators are both excluded from classification.
Statistics never delete damage merely because a skill later restores HP/state.

Version 1 evaluates 菜刀, 爆发, AOE, 辅助, 发育 with multiple labels permitted:

* 菜刀: Slash share of normal-turn card uses >= .45, uses per completed normal
  own turn >= .8, and Slash share of normal-turn damage >= .5.
* 爆发: >= 20% of completed normal own turns deal >= 3 damage and the empirical
  75th percentile (sorted index `floor(.75 * (N - 1))`) is >= 2. A single maximum
  is insufficient.
* AOE: >= .4 of normal-turn damage and >= .3 uses per completed normal own turn.
* 辅助: >= .5 actual recovery to other players per completed normal own turn.
* 发育: reconstruct hand count and maximum HP from the initial `player_state`
  baseline and subsequent committed before/after values, requiring continuity.
  Sample at both `turn_hp_snapshot` boundaries of each completed normal own turn
  with the participant alive. At least three such turns per match are needed for
  ordinary least-squares slopes of end-of-turn hand count, maximum HP and own-turn
  damage. Require at least five distinct matches, >=60% of those matches showing
  growth, and the aggregate slopes also showing growth. "Growth" means hand slope
  >= .25 or maximum-HP slope >= .2 with damage slope >= 0, or damage slope >= .5
  with both resource slopes >= 0. These are separately reported slopes, not a
  fabricated combined resource score. Duplicate same-root seats average first.
  Slopes use original normal-own-turn ordinals, preserving gaps from dead or
  truncated turns instead of compressing time after filtering observations.

The development projection reports its eligible matches/total matches, eligible
turns, slopes, consistency fraction and `survivor_only=true`. Missing baseline,
resource continuity or boundary evidence makes that projection unknown. It samples
only turns observed while alive, so it explicitly carries survival bias and never
claims a survival advantage. It does not infer equipment/private piles or output
outside the player's own turn, and does not extrapolate unobserved late turns.

These are versioned initial observational thresholds, not calibrated population
percentiles or claims about a general's intrinsic skills. At least five distinct
root matches, ten completed normal own turns, and complete required event coverage
are necessary. Lower samples are **provisional**, missing coverage **unknown**;
neither becomes zero ability or a "balanced" label. Control/tank/HP-trade/card-flow/
quick-finish/long-game labels remain explicitly unsupported/unknown.
Survival advantages and game-length causation are not claimed in this version.

Cross-game damage mean, sample variance, maximum and percentile are recomputed
from the currently effective records within each environment. Duplicate same-general
seats in the same root/environment first average their game damage, so they do not
pretend to be independent games. Variance is unknown below two distinct matches.

## Contract for the separate global-rewind implementation

The statistics change does **not** implement global restore, select an anchor, or
modify `GameSnapshotService`, `TakeoverScenario`, or `ResolutionHistoryService::restore`.
After a successful full-room restore, on the room worker thread, call:

```cpp
// The world has already committed; read its authority, do not increment again.
const quint64 next = room->gameTimeline().generation();
const bool persisted = room->commitStatisticsTimelineRestore(
    next, branchId, anchorKind, anchorId);
```

`statisticsRootMatchId()` stays fixed for the room lifetime. `anchorKind` must be
`previous_player_turn` or `full_round`; `anchorId` is the positive checkpoint
anchor ID from GameTimeline (not a reused local event ID). `branchId` comes
from that same authority. Both anchor types replace
the entire effective timeline's contribution. Local skill state restoration must
not call this API. Reused local event IDs are never globally deduplicated.

The call records the generation/anchor and synchronously persists an independent
generation journal, immediately withdrawing the previous contribution from the
analytical view. Physical SQLite invalidation is queued in the background.
A subsequent terminal
freeze submits the complete restored effective history for that same generation.
Older delayed saves cannot overwrite it; duplicate terminal submissions are
idempotent; a delayed equal-generation invalidation cannot erase its terminal.
Maxima/percentiles/labels update because summaries are recomputed, not incrementally
subtracted. The match still counts once.

`true` means the independent journal is durable; it does **not** mean SQLite has
caught up. `false` means invalid arguments or the journal could not be persisted.
Storage errors are logged and statistics remain uncertain. The caller must surface
the failure, not undo a successful game restore. Game generation changes immediately
regardless of SQLite availability. These Room identity/generation accessors are
the adapter for the shared room timeline contract; the separate rewind owner must
consume them rather than create competing root/generation fields.

For a failed fence, repeating the same generation/branch/anchor is supported.
`pendingTimelineInvalidations()` lets the Qt viewer show **統計更新中** and
withdraw previously displayed summaries. Conservatively recorded control/identity
changes are not undone by rewind.

### Crash/restart fence

Each root has a small independent sidecar under `<database>.timeline/`, containing
the root identity, decimal generation, owner token, publication token, active lifecycle
and dirty state. QSaveFile replaces the document atomically; a nonwaiting QLockFile prevents
concurrent mutation. This is a statistics validity receipt, not a second game
snapshot or an independent authoritative timeline.

Before play, `armMatch` persists an active marker for the new root. If arming fails,
that root cannot publish statistics in the process. The marker stays active even
after a terminal save while that Room can still restore/play. `closeMatch` is queued
at Room destruction, after previous saves, and may seal the marker only once the
database is caught up with the current generation and no dirty state remains.
The generation receipt is retained after clean closure.

Restarted readers do not trust a foreign active marker. Missing, malformed or
unreadable sidecars for existing database contributions are also unknown, never
implicitly fresh. Thus even if a later journal update fails, an earlier successfully
armed marker still forces conservative quarantine after an abrupt process exit.
Dirty generation receipts continue suppressing old rows when SQLite is read-only;
recovery must supply authoritative current facts before publishing the replacement.
The synchronous `save` API is an import/test seam; live Rooms use the armed lifecycle.

`recoverMatch` is a synchronous worker/import reconciliation seam. Its caller must
supply the complete current terminal Match from the authoritative timeline owner,
with generation at least the journal's floor. It can take ownership of a foreign
active marker, replace the entire contribution and seal only after success. It
never reads old `battle_matches.history` as a substitute for authoritative facts,
does not perform game restore, and refuses missing/corrupt journals. No automatic
repair UI or general snapshot recovery is introduced here. Each authoritative recovery
rotates a publication token also stored in the database metadata; readers compare it
before publishing results, so an older SQLite snapshot cannot pass as a newly
reconciled same-generation contribution.

No code can guarantee a new durable write when all storage fails. These guarantees
depend on the filesystem retaining previously acknowledged writes. If even arming
cannot be saved, publication is disabled; if the journal is missing or unreadable,
the UI reports uncertainty. Silent rollback of both database and journal to an
older mutually consistent backup, and physical loss of all storage, are outside
this local journal's guarantees.

## Focused verification

Configure with `-DQSAN_BUILD_GUI=OFF -DQSAN_BUILD_SERVER=OFF
-DQSAN_BUILD_TUI=OFF -DQSAN_TEST_BATTLE_STATISTICS=ON`, build
`qsanguosha_battle_statistics_tests`, and run
`ctest --test-dir <build> -R battle_statistics_contract --output-on-failure`.
The target compiles the production collector/projection and history service
directly, with Qt Core/Sql only, and does not stage game assets or run long games.
Production server compilation separately checks Room/lifecycle integration.

Full Qt 6.11 GUI/platform packaging and the external rewind task's eventual call
site require integration QA. The first version does not auto-migrate old schema
formats, recover abruptly killed games, calibrate population-relative thresholds,
or classify unsupported tags. The SQLite schema and analysis version are explicit
so those changes can be implemented without silently mixing meanings.

### Verified workspace result (2026-10-07)

Base: merged PR52 `debug` commit `77926a851cb008399d54f44ef3aa3a9226a0980e`,
local branch `feature/local-battle-statistics-durable` in
`/workspace/Qsanguosha-statistics-durable`. PR52 scenario lifecycle/TV changes
remain intact. No commit, push or PR was created.

* Production `qsanguosha_server` configured and built successfully with GCC 14,
  Qt 6.8.2 and Qt Sql. Existing deprecated Qt API and Lua `tmpnam` warnings remain.
* `battle_statistics_contract`: 1/1 CTest passed in 0.22 seconds. Covers dataset
  isolation/exclusions, exact HP/armor/recovery, duplicate frames/seats, stale
  generation replacement, maximum shrinkage, incomplete/null and provisional
  data, variance, nested terminal unwinding, development trends/survival samples,
  and a real read-only SQLite failure followed by visibility fencing and retry.
  New subprocess cases arm a root, make SQLite read-only, notify a restore, call
  `std::_Exit`, and check a fresh reader while the database remains read-only.
  After write access returns, authoritative reconciliation replaces the one root;
  another fresh reader sees only its current metrics. Other cases cover both
  storage paths unavailable, corrupt/missing markers, failed arming, a held SQLite
  write lock, and stale publication tokens after same-generation reconciliation.
* The new dialog compiled and passed an offscreen populated-sample smoke check
  (eight columns, multiple labels, development warning, general filter, and usable
  verified samples alongside a quarantined contribution).
  The modified general-overview translation unit plus MOC/UIC also compiled
  independently with Qt 6.8.2 and QML disabled. This is not a full Qt 6.11 GUI build.
* `git diff --check` and reverse-apply validation of the complete handoff patch
  passed. MainWindow, input routing, translation files and rule-restore code
  have no changes. No game assets were downloaded, and no long games were run.

Build logs remain at `/tmp/statistics-durable-build.log`,
`/tmp/statistics-durable-test.log`, `/tmp/statistics-durable-ui-build.log`, and
`/tmp/statistics-durable-ui-run.log` in this workspace. The full Qt 6.11 GUI and
end-to-end integration with the independent rewind implementation remain untested.
