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

The branch is recorded with `Recorder(..., true)` and a Replay V2 header with
`takeover:true`. It is always written to a new file; the source replay is never
overwritten.

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
