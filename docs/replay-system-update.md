# Replay system update

The replay subsystem is specified by the Replay V2 contract in
[`replay-v2.md`](replay-v2.md). This document records the integration boundary
for branch-and-play takeover; it is not a runtime acceptance report.

| Area | Contract | Boundary |
|---|---|---|
| Recording and loading | Replay V2 JSONL with strict typed Protocol V2 validation | Replay V1, headerless files, Protocol V1 arrays, and partial fallback are rejected |
| Snapshot format | One current snapshot schema with complete validation | Older or unknown snapshot schemas are not read |
| Takeover model | Create a new local game from a validated snapshot | The historical server is never resumed or mutated |
| Eligible boundary | Top-level normal `actionNormal()` before the first real `TurnStart` | Nested extra-turn execution has no takeover node |
| Game mode | Ordinary fixed-player mode; retain the snapshot's original `gameMode` | Scenario, mini-scene, hegemony and special turn controllers are rejected; takeover is session metadata |
| Seat policy | Viewer may select an alive seat | Dead seats are not selectable; all other seats use fresh `SmartAI` instances |
| Restored state | Players, cards and stable zone order, wrapped cards, skill instances, history, tags, dynamic attributes, and pending extra turns | Transient table/WuGu/unknown card locations are ineligible; historical SmartAI decision state is not restored |
| Randomness | Gameplay RNG and AI RNG restore `algorithm`, `seed`, and `drawCount` | RNG state is restored after initialization so setup draws do not consume the saved progress |
| Lua state | Versioned takeover providers serialize JSON-safe state | Unregistered VM/provider private state makes a node ineligible |
| Integrity | Manifest binds session identity, source replay hash, and each snapshot hash | Any hash, schema, catalog, package, card, mode, or provider mismatch rejects takeover |
| Branch output | `Recorder(..., true)` writes a new Replay V2 file with `takeover:true` | The source recording is never overwritten |
| PNG | Playback-only transport container | PNG takeover is permanently disabled |
| Startup failure | Close the local session and reopen the source replay at its saved position, perspective, and paused state | Return home only if the source replay cannot be reopened |
| TUI | No Replay playback or takeover implementation | TUI protocol cases are not Replay evidence |

## Operational sequence

1. Validate the manifest, source hash, current snapshot schema, game mode,
   catalog, package/card catalog, and Lua provider versions.
2. Save the source replay context, safely stop its replayer, and create the
   local server/client session.
3. Create the original seat count, map the selected alive seat, assign the
   recorded generals/roles without re-running selection, and create fresh
   `SmartAI` instances for the other seats.
4. Restore wrapped cards and every zone in recorded order, then restore player
   fields, skill instances, history, tags, dynamic attributes, pending extra
   turns, Lua provider state, and both RNG states.
5. Complete `GameReady` restoration and validation before allowing the first
   real top-level `TurnStart`.
6. On any failure, tear down the local session and perform the documented
   replay rollback.

The source and branch files remain independently loadable under the same strict
Replay V2 reader. This document intentionally does not claim that GUI,
cross-platform, or long-running gameplay acceptance has been completed.
