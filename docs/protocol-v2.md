# Protocol V2 production contract

## Status

Protocol V2 is the only production protocol. The first TCP frame and every
subsequent frame use the V2 JSON-object envelope. There is no capability
negotiation, runtime codec switch, V1 fallback, `Packet` facade, or mixed-version
connection state.

The production inventory contains exactly 145 registered flows. Every flow has
a schema-versioned typed-object payload, a parser, an encoder, replay policy,
correlation policy, and producer/consumer evidence. The generated source of truth
is [`artifacts/protocol-v2-flow-matrix.json`](../artifacts/protocol-v2-flow-matrix.json).

## Envelope

```json
{
  "v": 2,
  "type": "request",
  "source": "room",
  "destination": "client",
  "message_id": "42",
  "reply_to": "21",
  "command": 37,
  "payload": {
    "schema_version": 1
  }
}
```

| Field | Wire type | Contract |
|---|---|---|
| `v` | integer | Exactly `2` |
| `type` | string enum | `request`, `reply`, or `notification` |
| `source` / `destination` | string enum | `room`, `lobby`, or `client` |
| `message_id` | decimal string | Canonical positive full-range `quint64` |
| `reply_to` | decimal string | Required only for replies; exact request correlation |
| `command` | integer | Registered `S_COMMAND_*` identity |
| `payload` | object | Required; contains a positive integral `schema_version` |

The codec accepts a positive payload schema version; the production registry
then enforces the exact version and fields for the selected flow. Unknown flows,
wrong directions, missing fields, wrong scalar types, unknown enum values,
positional arrays, scalar payloads, and malformed nested objects are rejected.
There is no restoration of a legacy wire shape after decode.

## Typed payload boundary

Wire payloads never use:

- positional application arrays;
- delimiter-packed lists such as `a+b`;
- Base64 wrappers for ordinary text;
- generic `data` or compatibility-wrapper fields;
- sentinel values such as `-1` for cancellation.

Existing gameplay APIs may still construct domain values internally. The room
boundary converts them once into their registered named object before Replay or
TCP delivery. The client keeps the validated typed object during dispatch.
`S_COMMAND_SHOW_ALL_CARDS` domain lists are still `[player_name, false, card_ids]`
(Gongxin-shaped); the encoder drops the unused boolean so `card_ids` stays an
integer list.

All 29 production interactions, including `S_COMMAND_QML_INTERACT`, use direct
typed request and reply schemas. Cancellation is represented by named boolean
discriminators such as `cancelled` or `has_value`; QML is not a legacy adapter.
The generated interaction matrix is
[`artifacts/client-core-interaction-matrix.json`](../artifacts/client-core-interaction-matrix.json).
The production TUI/GUI shared-state coverage of every Room-to-Client flow is
[`artifacts/tui-flow-coverage.json`](../artifacts/tui-flow-coverage.json); its
contract rejects unclassified flows, silent drops, or an interaction without a
registered presenter.

## Framing and errors

Newline framing is external to the codec and applies only to TCP.
A TCP frame is at most 65,535 encoded bytes; over-limit or unterminated
over-limit input closes the connection. CRLF is accepted at the TCP
framing boundary.

The dedicated server and GUI embedded server also listen for WebSocket
clients on a separate port (default 9528), except the Windows XP Qt 5.6.3
legacy build, which is TCP-only. Each WebSocket text frame
carries one encoded Protocol V2 JSON object; the gateway does not wrap
the object in a newline. Binary frames, empty frames, frames larger than
65,535 UTF-8 bytes, and frames that contain CR or LF are rejected. JSON
payload nesting is capped at 128.
Non-finite numbers, unsupported Qt metatypes, and integers outside the JSON-safe
payload range are rejected.

`SignupRequestPayload` is schema 2. Native GUI and TUI omit `room_id` and join
`current` as before. Schema 1 without `room_id` remains accepted. A present
`room_id` must be a non-negative integer and selects that waiting room; unknown,
finished, started, or full rooms are rejected and do not create a new seat.
`SignupReplyPayload` is schema 2. An accepted reply includes the assigned
`room_id` (including `0`). Schema 1 accepted replies without `room_id` remain
valid. Native GUI and TUI ignore the field.

Decode is transactional: a failed decode does not mutate the output message.
An encode failure returns an empty byte array and a diagnostic.

## Replay

New recordings contain the same validated Protocol V2 objects in Replay V2
JSON Lines. Replay V1 and headerless recordings are rejected. Replay playback,
timeline, perspective switching, and player takeover remain supported. TUI has
no Replay implementation and is intentionally outside this contract. See
[`replay-v2.md`](replay-v2.md).

```text
Replay in qsanguosha_tui: Permanently Unsupported
Protocol V1: Unsupported
GUI dependencies in qsanguosha_tui: Forbidden
```
