# Replay V2 contract

Replay V2 is the only supported recording and playback format. The format is
UTF-8 JSON Lines: one header object followed by zero or more timeline-event
objects.

## Format

Header:

```json
{"format":"qsanguosha-replay","schema_version":1,"format_version":2,"protocol_version":2,"game_version":"2026.08","mod_name":"official","takeover":false}
```

Event:

```json
{"schema_version":1,"elapsed_ms":"17","message":{"v":2,"type":"notification","source":"room","destination":"client","message_id":"1","command":64,"payload":{"schema_version":1}}}
```

`elapsed_ms` is a canonical non-negative decimal string and must be monotonic.
Embedded messages use the same typed Protocol V2 registry and full-range
`quint64` identities as TCP. Duplicate or missing message IDs, invalid reply
correlation, unregistered flows, and replay-policy violations reject the whole
load transaction.

Replay V1, headerless recordings, Protocol V1 arrays, and partial fallback are
not supported. There is no converter. Snapshot loading is also a breaking
cutover: a snapshot must use the current schema and older or unknown snapshot
schemas are rejected.

## Result statistics

The live recorder feeds validated typed messages into `RecAnalysis` as they
arrive. Result dialogs copy this player-sized statistics snapshot and calculate
designations, instead of rescanning the game history on the UI thread. An open
replayer supplies its already validated events to the same statistics reducer.
The final-message check is retained so later events do not make an earlier
`GAME_OVER` count as the terminal result. Analysis of other text or PNG files
still uses the complete strict Replay V2 reader.

The desktop result dialog prepares its hidden table widgets and polishes their
shared style at game start. At game end it sets the outcome banner, fills the
two tables from one statistics snapshot, and measures their columns in one pass.
While showing it, an event guard pauses updates of the frozen room viewports
until the result's first Paint event, then schedules their updates again. If
closed before painting, destruction of the guard restores the viewports too.
The prepared dialog is consumed once and disposed after closing, including when
a replay reaches another ending.

For a bounded manual or net-runner diagnosis, `QSAN_WINNER_DIALOG_TRACE=1`
logs client preparation, dialog stages, the first dialog paint event, and the
displayed result rows. It is disabled by default and does not change game rules.

## PNG container

The PNG container transports compressed Replay V2 JSONL bytes in lossless RGBA
pixels. It is not a screenshot and does not alter the replay timeline. The
embedded container has a fixed magic marker, container version, big-endian
compressed size, and SHA-256 digest. Ordinary PNGs, unknown container versions,
truncated data, and corrupted payloads are rejected by the same strict reader.

PNG recordings never expose branch-and-play takeover. A PNG replay is therefore
playback-only, even when its embedded header identifies a takeover branch.

## Playback and branch-and-play takeover

Normal playback provides timeline seeking, perspective switching, and indexed
state reconstruction. Takeover creates a new local game from a validated
snapshot; it does not resume or mutate the historical server.

Seeking reconstructs the recorded prefix in a fresh native `Client` and scene.
It preserves paused state and playback speed; old player objects, hand cards,
marks and dynamic seats are discarded before the new prefix is applied.
The perspective menu is populated only by a valid sibling headless game index.
Each choice opens that seat's own recipient stream at the current elapsed time,
so private hands come from the messages actually sent to that seat. Historical
control-context messages still select the operating player. A dynamically added
seat becomes selectable from its birth time; dead seats remain selectable.

## Headless recording and inspection

Network servers can opt into the same per-seat recorder with `--record-dir
<new-directory>`. The ordinary TCP signup and gameplay paths remain active;
SETUP is copied into the recorder without sending a second SETUP to the client.
Each room has its own game index and finalizes before its players are destroyed.
The index's round count follows recorded ADD_ROUND / STATE_SYNC notifications,
so it survives room-tag cleanup and includes a late seat's bootstrap round.

Native `--headless` games automatically record Replay V2, independently of the
GUI recorder settings. Override the batch directory with `--headless-record-dir`
or opt out with `--no-headless-record`. The default is a unique timestamp/PID
directory below the runtime record directory's `headless/` folder. An existing
batch index or game directory is never overwritten.

```powershell
.\QSanguosha.exe --headless --games 1 --game-mode 05p --seed 20261112 --headless-record-dir L:/finaldebug/QSanguosha-v2/record/headless-example --headless-log L:/finaldebug/QSanguosha-v2/record/headless-example.log
```

Each batch has `index.json`; each `game-000001/` contains its own `index.json`
and `seat-001.txt`, `seat-002.txt`, etc. The indexes record mode, gameplay seed,
status, winner, player/general/role metadata, event counts and, on finalization,
file sizes and SHA-256 hashes. Open any seat TXT through the usual Replay entry;
keep the sibling index and other seat files together to retain the menu.
These recordings contain private hands and identities for every recorded seat.

All streams share one monotonic clock. Initial signup is recorded before the
game starts. A new mid-game seat records a native marshal bootstrap at its birth
coordinate, using the original card catalogue without advancing gameplay RNG.
The record buffer appends complete JSON lines and flushes every 128 events or
one second of event time, then finalizes after room workers stop. A hard process
termination leaves a `running` index and may lose the unflushed tail; such a file
is a partial recording, not a completed game, and strict parsing still applies.

The product QA inspector loads real recordings with the ordinary
`MainWindow`/`Client` playback lifecycle; it does not create a game or synthetic
fixture. Run it from the deployed application's working directory:

```powershell
$env:QT_QPA_PLATFORM = 'offscreen'
.\QSanguosha.exe --inspect-replay L:/finaldebug/QSanguosha-v2/record/headless-example/game-000001/seat-001.txt --replay-report L:/finaldebug/QSanguosha-v2/record/headless-example/inspection.json
```

The inspector defaults to a 60-second deadline. After explicitly authorizing a
longer inspection, pass `--replay-timeout-ms <milliseconds>` (at most 2700000);
application startup and shutdown must also fit the process's authorized limit.
Use `--replay-perspectives sgs1,sgs2` to focus native inspection on selected
recorded seats. Native loading validates sibling catalogue identities and paths;
`tools/autotest/replay_recording_audit.py` separately checks every stream's
recorded byte count, event count and SHA-256 against the final index.
Streams without both gain/loss card-movement notifications fail the hand-coverage
check before an empty hand can become a comparison baseline.
Its JSON report includes each seat's
middle/end state, native hand IDs and counts, ClientCore state, backward-seek
comparison against a fresh prefix, perspective switches, pause/speed retention,
and the final `GAME_OVER` state, including native cleanup notifications that
follow the result event. It closes only identified native result dialogs during
inspection. A passing report covers the events present in that recording;
control transfers or mid-game additions require a recording containing them.
No computer-use automation is needed.

For a real network QA game, `--network-ui-smoke-script <file.lua>` submits a
server script once at the first ordinary play-phase request, using the existing
cheat-script API. Enable cheats only in that game's isolated server settings.
This can invoke control transfer and `requestSummonBetween` without fabricated
hands or an injected result; the native responder continues normal play.
Movement notifications resolve the complete recipient/controller set once,
then render each recipient's visibility and deliver one body directly.

Replay reconstruction retires the previous Client and its engine runtime
registration together. This also applies when leaving a paused middle position
before `GAME_OVER`; the next Client must never inherit the retired context.
Independent hand audits must include `TAKE_AG` acquisitions with `move_cards`,
in addition to the ordinary gain/loss movement notifications.

## Takeover eligibility

A takeover node is eligible only when all of the following are true:

- it is in an ordinary fixed-player mode such as `02p`, not a scenario,
  mini-scene, hegemony, 1v1, 3v3, XMode, Hulao, boss, or defense controller;
- it is at a top-level normal `actionNormal()` boundary before the first real
  `TurnStart` of that turn;
- no nested extra turn is executing at that boundary;
- every physical card is in a stable, ordered zone; transient table, WuGu, or
  unknown locations make that node ineligible;
- gameplay Lua and SmartAI are enabled in the recorded configuration;
- all required snapshot data and registered Lua provider state are present and
  valid.

The selected seat must be alive. The original `gameMode` is retained; takeover
is session metadata, not a mode change. The selected seat is controlled by the
viewer, while every other seat is created as a fresh `SmartAI`. Historical AI
decision state is not restored.

Snapshot reconstruction suppresses nested gameplay-event dispatch while Room
APIs rebuild marks, skills, and card zones. This prevents historical restore
operations from firing live skills before the branch's first `TurnStart`.

The snapshot captures and validates, as one transaction:

- player identity, role/general assignment, core properties, marks, flags,
  piles, history, tags, and JSON-safe dynamic attributes;
- every card object and every card-zone order, including wrapped-card state;
- skill instances, including parent/child relationships, amounts, and
  registered private state;
- the pending extra-turn queue;
- gameplay RNG and AI RNG, each with `algorithm`, `seed`, and `drawCount`;
- versioned Lua takeover-provider state, limited to JSON-safe values.

Lua state that belongs to an unregistered VM or provider, including opaque
private state without a registered serializer, makes the node ineligible. A
snapshot is not written as eligible unless catalog, package, card, mode,
schema, and provider validation succeeds.

Each replay session has a manifest binding the session identity, source replay
SHA-256, and every snapshot SHA-256. The manifest and all referenced snapshots
must verify before a takeover game is created.

On 32-bit builds, the server releases each successfully persisted snapshot
payload and retains only its file path, SHA-256 and turn/player metadata.
Retrieval loads a snapshot on demand, verifies the actual parsed bytes and
metadata, and caches at most one payload. Returned shared pointers keep a
snapshot alive for its caller even after cache replacement. The 64-bit default
continues to retain snapshots in memory.

32-bit saves stream schema-3 JSON through a bounded output buffer, emitting
individual history records, players and cards instead of materializing a full
history QVariant list plus JSON document. Manifest file hashing and snapshot
copying use 64 KiB chunks; copies verify the bytes before atomic publication.
Snapshots still contain the complete history, and failed writes do not replace
the previous committed file. This reduces retained/temporary snapshot memory;
it does not cap the live history, total process memory or disk usage. Loading
one selected snapshot still uses the existing whole-document JSON parser.

The branch is recorded with `Recorder(..., true)` and a Replay V2 header with
`takeover:true`. It is always written to a new file; the source replay is never
overwritten.

### Headless takeover (Lua edit loop)

```
QSanguosha --headless --takeover <replay>.snapshots/turn_NNN_turn.json [--takeover-seat SGSn] --games 1 --headless-log out.txt
```

Each headless game restores the turn snapshot in a new Room, whose Lua VMs
re-read `extensions/` and `lua/ai/` from disk. Editing a skill and running the
command again therefore replays the same position under the new code: mode and
gameplay seed come from the snapshot and the restore reinstates the gameplay
and AI RNG, every seat is SmartAI (the selected seat stays a robot), so two
runs of unchanged code are meant to produce logs that can be diffed directly. `--takeover-seat` defaults to the snapshot's
current player; it only satisfies the seat binding. `--game-mode` is ignored.

The ordinary takeover limits apply: turn boundaries only, ordinary modes only,
and every skill and general named in the snapshot must still exist. Lua source
is not fingerprinted, but a changed takeover-state provider name or version
rejects the restore. A rejected snapshot or a failed restore logs `ERROR` and
exits with code 1. The command reads the turn snapshot JSON directly; it is not
the diagnostic-bundle loader described below.

## Bug diagnostic bundle

The Replay control bar can export a currently open text replay with a verified
takeover manifest as a `*.qsgbug.zip` diagnostic bundle. This is a thin export
layer, not another snapshot or restore format. The ZIP32 archive uses stored
entries and contains these fixed paths:

```text
bundle.json
replay.txt
replay.snapshots/manifest.json
replay.snapshots/turn_*.json
state-now.json
diagnostics.json
```

`replay.txt`, the takeover manifest, and every snapshot accepted when the
`Replayer` was constructed are copied byte-for-byte. Export does not repeat the
takeover schema or SHA-256 validation. A later external modification can
therefore produce a bundle that a future loader rejects; unreadable core files
instead fail the atomic export without leaving a partial archive.

`bundle.json` identifies the source by basename only and records the size and
SHA-256 of every packaged payload. It also records optional-file omission
reasons. Existing snapshots retain their original `replayPath` metadata, which
can contain a local absolute path; the GUI warns about this when export
finishes. Replay payloads and `state-now.json` can also contain player names,
chat text, room metadata, or connection metadata. Only `diagnostics.json` is
intentionally restricted to the sanitized fields listed below.

`state-now.json` is diagnostic and explicitly has `restorable:false`. During
playback, `Replayer` stops immediately before the next event and queues a
barrier to the same `Client` receiver as preceding replay events. The client
captures `ClientCore::toJson()` only after those events have been applied, and
records the exact `lastAppliedPairIndex` and `elapsedMs`. Playback remains
quiesced until the atomic ZIP write finishes, then returns to its previous
playing or paused state. If the barrier cannot be reached within two seconds,
the file is omitted rather than replaced with an approximate state. Timeline
seeks and background playback share a serialized dispatch boundary, so an old
worker cursor cannot be delivered after the seek history.

`diagnostics.json` contains the game version, build mode, compiler, Qt compile
and runtime versions, OS/CPU architecture, and replay counts. It does not
contain environment variables, hostname, IP address, or an absolute executable
path. The game build is identified by `QSanVersion::Number`; no executable hash
or Git metadata is generated.

There is no `--load-repro` command. A future loader must validate the extracted
takeover manifest and delegate restoration to the existing
`GameSessionConfig` and `TakeoverScenario` path.

## Startup failure and rollback

Starting a takeover is transactional. Before destroying the replay client, the
implementation saves the source filename, playback position, perspective, and
paused state. The replay client is stopped safely, then the local server and
client are started and the snapshot is restored before the first real
`TurnStart`.

If listening, connecting, robot completion, catalog/provider validation, or
state restoration fails, the local takeover session is closed and the original
replay is reopened at its saved position, perspective, and paused state. The
failure reason is shown to the user. Returning to the home screen is required
only when the original replay itself cannot be reopened.

## TUI boundary

TUI has no Replay playback, PNG-container, timeline, perspective-switch, or
branch-and-play takeover implementation. JSON/TUI protocol cases are not Replay
evidence.
