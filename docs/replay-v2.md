# Replay V2 contract

## Format

Replay V2 is the only supported recording and playback format. It is UTF-8 JSON
Lines: one header object followed by zero or more timeline-event objects.

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
correlation, unregistered flows, or replay-policy violations fail the whole
load transaction.

Replay V1, headerless recordings, Protocol V1 arrays, and partial fallback are
not supported. No converter is provided.

## PNG Replay container

The PNG container stores the compressed Replay V2 JSONL bytes inside lossless
RGBA pixels. Its purpose is transport: a recording can be shared through
systems that accept PNG images more reliably than arbitrary replay files. It is
not a screenshot and does not alter the replay timeline.

The embedded container has a fixed magic marker, container version, big-endian
compressed size, and SHA-256 digest. Ordinary PNGs, unknown container versions,
truncated data, and corrupted payloads are rejected. The decompressed bytes are
then parsed by the same strict Replay V2 reader.

## Playback and takeover

The existing replay features remain available:

- timeline and indexed seeking;
- perspective switching;
- snapshots and reconstructed game state;
- watching-time player takeover.

Takeover does not resume the historical server. `ReplayTakeoverManager` pauses
playback at an eligible recorded request, lets the viewer or AI provide a typed
reply, records that reply with the exact `reply_to`, and continues on a new
takeover Replay V2 branch. Passive replays reject client-to-room replies;
takeover recordings admit them only under the registered takeover policy.

Saving a takeover branch never overwrites the source recording.

## TUI boundary

TUI has no Replay playback, PNG-container, timeline, perspective-switch, or
takeover implementation. This is intentional and permanent for the current
scope; JSON/TUI protocol cases are not Replay evidence.
