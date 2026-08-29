# Protocol V2 wire contract

## Status

```text
Wire envelope: defined
Codec: implemented
Production activation: disabled
Replay: V1
Framing: legacy newline transport
```

`QSanProtocol::ProtocolV2Codec` can independently encode and decode a
`ProtocolMessage`. Client and server production packet paths still use
`ProtocolV1Codec`; merely constructing a V2 codec does not change a session's
`activeVersion`.

## Envelope

Protocol V2 is a compact UTF-8 JSON object. Object member order has no semantic
meaning; the encoder nevertheless produces deterministic bytes.

Request:

```json
{
  "v": 2,
  "type": "request",
  "source": "room",
  "destination": "client",
  "message_id": "42",
  "command": 37,
  "payload": {}
}
```

Reply:

```json
{
  "v": 2,
  "type": "reply",
  "source": "client",
  "destination": "room",
  "message_id": "81",
  "reply_to": "42",
  "command": 37,
  "payload": {}
}
```

Notification:

```json
{
  "v": 2,
  "type": "notification",
  "source": "room",
  "destination": "client",
  "message_id": "43",
  "command": 90
}
```

## Field contract

| Field | Type | Required | Meaning |
|---|---|---|---|
| `v` | integer | yes | Exactly `2` |
| `type` | string enum | yes | `request`／`reply`／`notification` |
| `source` | string enum | yes | `room`／`lobby`／`client` |
| `destination` | string enum | yes | `room`／`lobby`／`client` |
| `message_id` | decimal string | yes | Positive message identity |
| `reply_to` | decimal string | reply only | Correlated request identity |
| `command` | integer | yes | Existing numeric `S_COMMAND_*` identity |
| `payload` | JSON value | no | Command payload |

`message_id` and `reply_to` contain canonical ASCII decimal digits. They have no
sign or whitespace, are greater than zero, reject leading zeroes, and may use the
full `quint64` range. Decimal strings avoid the IEEE-754 53-bit integer limit in
future Web, WASM, and JavaScript clients.

Unknown numeric commands are accepted. Command support belongs to routing and
capability handling rather than envelope parsing. Unknown additional top-level
fields are ignored to permit compatible V2 evolution; required fields and their
types remain strict.

## Presence

Missing payload and explicit JSON null are distinct:

```text
payload missing -> hasPayload = false
payload null    -> hasPayload = true, payload.isNull() = true
```

A reply must contain a positive `reply_to`. Requests and notifications must not
contain `reply_to`; the encoder never emits a zero placeholder.

## Payload domain

The wire payload accepts only JSON null, boolean, finite number, string, array,
and object values. The C++ bridge represents arrays with `QVariantList` and
objects with `QVariantMap`. Integer QVariant values outside
`-9007199254740991..9007199254740991` are rejected instead of being silently
rounded through a JSON double. Non-finite floating-point values and Qt-specific
types such as `QByteArray`, `QDateTime`, `QObject *`, or custom metatypes are
rejected with a diagnostic. Container nesting deeper than 128 levels is also
rejected to keep recursive validation bounded.

## Decode errors

| Error | Condition |
|---|---|
| `NullOutput` | Output `ProtocolMessage *` is null |
| `EmptyInput` | Input is empty |
| `PacketTooLarge` | Input exceeds 65535 bytes |
| `InvalidJson` | JSON parser rejects the input |
| `InvalidEnvelope` | Root is not an object |
| `InvalidHeader` | A required field is missing, mistyped, or violates its field contract |
| `InvalidPayload` | Payload violates the supported JSON-domain representation |
| `UnsupportedVersion` | `v` is not the integral number `2` |

Decode is transactional: failure leaves the output message unchanged. Encode
failure returns an empty `QByteArray` and, when supplied, fills `QString *error`.

## Compatibility

```text
V1 codec does not parse V2
V2 codec does not parse V1
Negotiation still begins on V1
preferredVersion may be V2
activeVersion remains V1
Production and replay remain V1
```

The V2 codec has no dependency on legacy `Packet` or `PacketDescription`.
Runtime codec selection, switch acknowledgement, gameplay migration, and replay
V2 belong to later work.

## Transport

Newline framing remains external to the codec. `encode()` returns only the JSON
object bytes and does not append `\n`.

The legacy transport still contains an approximately 16 KB read buffer while
the codec-level packet limit is 65535 bytes. This is a blocker for unrestricted
Protocol V2 production activation and must be resolved or reflected in the
active V2 frame limit before runtime switching is enabled.
