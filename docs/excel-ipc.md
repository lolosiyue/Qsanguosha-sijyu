# Excel bridge IPC v1

Implementation contract for the worksheet/VBA frontend. This document describes
the new local adapter, not a replacement for Protocol V2. Product acceptance is
pending until the separate gates in `excel-client.md` pass.

## Ownership and framing

One workbook owns one bridge session. The bridge listens only on IPv4 loopback,
on port zero, and publishes the chosen port in a private bootstrap JSON file.
Every HTTP request requires `Authorization: Bearer <token>` and
`X-QSan-Session: <session>`. Tokens never occur in URLs or ordinary logs.
HTTP bodies are UTF-8 JSON objects. Requests are bounded to 1 MiB, headers to
16 KiB; responses are bounded to 4 MiB. Chunked request bodies and redirects are
not used. Each connection closes after one response.

`api_version` is the integer 1. `session`, `id`, `generation`, `revision`,
`request_id`, and event `sequence` are strings. Integer identifiers must never
pass through a VBA Double or a JSON numeric conversion. Card IDs, counts and
skill instance IDs remain bounded integers. Unknown commands fail explicitly.

## Bootstrap

CLI: `--bootstrap <absolute file> --nonce <uuid> --parent-pid <pid>
--asset-root <directory>`. The parent must be a live Excel process. The bridge
retains its handle and creation time; idle/busy Excel is not parent death.
The bootstrap parent directory must belong to the current user. The bridge
applies a current-user-only ACL before writing credentials atomically.

The ready file contains `api_version`, `nonce`, `session`, `token`, `port`,
`pid`, `parent_pid`, `parent_created`, `runtime_tier` (`modern` or `legacy`),
`max_players` (legacy: 10), and `status` (`ready`). Initialization failure writes
`status: error` and a safe `error` code, without credentials. VBA retains the
bootstrap path for explicit stop/recovery, never as a cell value.

## Commands

`POST /v1/commands` accepts:

```json
{"api_version":1,"session":"...","id":"1","generation":"0",
 "revision":"0","name":"connect","args":{"host":"127.0.0.1",
 "port":9527,"name":"Excel","avatar":"caocao"}}
```

Response: `api_version`, `session`, `id`, `ok`, optional `error`, and `result`.
The same ID and identical payload returns its cached result; conflicting reuse
fails. A bounded duplicate window must not allow an older ID to execute again.

Commands:

| Name | args / result |
|---|---|
| `catalog` | returns `modes`, `packages`, `settings`, `generals`, `runtime_tier`, `max_players` |
| `connect` | `host`, `port`, `name`, `avatar`; starts async connection |
| `host` | `private` boolean, `name`, `avatar`, `settings` object, `robots` count; starts owned helper |
| `ready` | `ready` boolean |
| `chat` | `text` |
| `add_robot` | `count` (positive integer, or 0 for all remaining seats) |
| `trust` | `enabled` boolean |
| `surrender` | no args |
| `reconnect` | no args |
| `select` | `request_id`, `draft`; returns `selection` preflight view |
| `submit` | `request_id`, `draft`; validates the current draft again before submitting |
| `cancel` | `request_id`; uses the schema-specific cancellation |
| `details` | `kind` (`card`, `general`, `skill`, `player`, `pile`), `key` |
| `disconnect` | graceful disconnection; returns to home by shutting down this bridge |

`select`, `submit`, and `cancel` require current generation and interaction
revision. Card selection drafts use `cards` (integer array), `targets` (ordered
string array, repetitions preserved), `skill_name`, `skill_instance_id`, and
`declaration`. Other presenters use `option`, `enabled`, `top`, `bottom`,
`order`, `assignments`, `value`, and `hidden_hand` as appropriate to the typed
interaction. The bridge builds the typed response; VBA never constructs wire
card text or a Protocol V2 packet. Unknown custom interaction types fail closed.

The user explicitly excludes `qml_interact` / `qsanguosha.qml` from both Excel
runtime tiers. Requests remain explicitly rejected without a fabricated reply;
the missing QML presenter is not an Excel release acceptance blocker. Other
in-scope custom interaction presenters remain required.

`declaration` maps to the native rules evaluator's `user_string`. Conflicting
values are rejected. Native `interaction.ui` and preflight `selection.ui` supply
the current option/card/player/skill/declaration rows with labels, availability
and optional image paths; these are presentation data, never another game state.

## Updates and presentation

`GET /v1/updates?after=<decimal sequence>` returns `api_version`, `session`,
`sequence`, `resync`, `events`, and `snapshot`. Each event has `sequence`,
`kind`, and `data`. Snapshots contain:

* `generation`, `revision`, `connection`, `request_id`;
* `state`: the authorized client state projection;
* `interaction`: canonical InteractionRequest JSON, or an empty object;
* `view`: `prompt`, `players`, `hand`, `skills`, `logs`, `status`, `game_over`;
* `selection`: the last current preflight result, or an empty object.

Presentation rows have stable `id`, `label`, `enabled`, optional `image`, and
`detail`. Player rows additionally have `hp`, `max_hp`, `hand_count`, `seat`,
`alive`, `equip`, and `marks`. Skill rows include `name` and `instance_id`.
Images are absolute paths beneath the packaged image root, never arbitrary
paths supplied by a remote peer. Only visible/authorized card faces are emitted.

A selection result has `can_confirm`, `reason`, `cards`, `targets`, and
`draft`. It must be discarded when request/generation/revision changes.
Snapshot revision changes with state/request changes, not polling itself.
The view never includes raw protocol history or unredacted diagnostic payloads.
When event history is unavailable, `resync=true` and the current snapshot is
authoritative. Old audio events are not replayed after resync.

## Shutdown

`POST /v1/shutdown` is authenticated and acknowledges before asynchronous
cleanup. It never stops an external server. Close cancels the exact OnTime
schedule and aborts outstanding WinHTTP requests. Parent death and forced helper
termination are recorded as emergency cleanup, not graceful lifetime success.

The native `--stop-session <bootstrap>` entry stops exactly one authenticated
session. With `--wait-ready`, it creates a private `cancel.request` marker before
waiting for late bootstrap publication. Normal startup checks this marker before
and after Engine initialization, so cancelling slow startup cannot silently leave
a newly started bridge behind. Excel inactivity is never used as a death signal.

## VBA transport

Use late-bound WinHTTP 5.1, async Open, direct proxy mode and redirects disabled.
Keep one updates request and one command request in flight. OnTime ticks harvest
with WaitForResponse(0), process bounded work, then schedule exactly one next
tick. False means not finished, not a transport failure. Abort only on a separate
deadline. Clicks change local selection immediately; pending preflight drafts
can coalesce, but submitted actions cannot be reordered or silently dropped.

## Admission compatibility

Excel may send the optional V2 `SignupRequestPayload.max_players` field when
its packaged client supports only a smaller room. The value is `0` or omitted
for no frontend cap; otherwise it is an integral value from 2 through 1000.
The server rejects a signup with `frontend_player_limit` before assigning a
room seat when the selected room's configured capacity exceeds that cap,
including reconnect before reattaching its existing seat. Existing
GUI/TUI clients omit the field and retain their current wire shape and room
rules. This is a per-room frontend constraint and does not impose a server-wide
admitted-player limit.
