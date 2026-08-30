# Protocol V2 wire contract

## Status

```text
Wire envelope: defined
Codec: implemented
Production activation: enabled after explicit V1 barrier
Replay: V1 logical normalization
Framing: byte-oriented newline transport, maximum 65535 encoded bytes
Typed gameplay payloads: 7/29 interaction commands
```

`QSanProtocol::ProtocolV2Codec` can independently encode and decode a
`ProtocolMessage`. A mutually capable production connection starts on V1 and
changes its per-connection codec only after the OFFER/ACK/COMMIT barrier defined
in [protocol-runtime-switch.md](protocol-runtime-switch.md).

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

## Typed gameplay payload inventory

`ProtocolGameplayPayloadRegistry` is the single wire-boundary registry. Gameplay,
server decision code, `Client`, and ClientCore continue to exchange the existing
logical payload. The router applies the following rules:

| Active version／command／direction | Wire transformation |
|---|---|
| V1／any command | Identity |
| V2／non-migrated command | Identity |
| V2／`S_COMMAND_MULTIPLE_CHOICE` Room → Client request | Legacy four-string array → typed request object |
| V2／`S_COMMAND_MULTIPLE_CHOICE` Client → Room reply | Legacy scalar string → typed reply object |
| V2／`S_COMMAND_CHOOSE_GENERAL` Room → Client request | Legacy string array → `{schema_version, candidates}` |
| V2／`S_COMMAND_CHOOSE_GENERAL` Client → Room reply | Legacy string → `{schema_version, general}` |
| V2／`S_COMMAND_CHOOSE_SUIT` Room → Client request | Missing legacy payload → `{schema_version}` |
| V2／`S_COMMAND_CHOOSE_SUIT` Client → Room reply | Legacy string → `{schema_version, suit}` |
| V2／`S_COMMAND_CHOOSE_KINGDOM` Room → Client request | Legacy joined-string array → `{schema_version, kingdoms}` |
| V2／`S_COMMAND_CHOOSE_KINGDOM` Client → Room reply | Legacy string → `{schema_version, kingdom}` |
| V2／`S_COMMAND_CHOOSE_ORDER` Room → Client request | Legacy numeric reason → `{schema_version, reason}` |
| V2／`S_COMMAND_CHOOSE_ORDER` Client → Room reply | Legacy numeric camp → `{schema_version, camp}` |
| V2／`S_COMMAND_INVOKE_SKILL` Room → Client request | Legacy two-string array → `{schema_version, skill_name, data}` |
| V2／`S_COMMAND_INVOKE_SKILL` Client → Room reply | Legacy boolean → `{schema_version, invoke}` |
| V2／`S_COMMAND_SURRENDER` Room → Client vote request | Legacy initiator string → `{schema_version, initiator_general}` |
| V2／`S_COMMAND_SURRENDER` Client → Room vote reply | Legacy boolean → `{schema_version, surrender}` |
| V2／migrated command, any other flow | Identity |

The migrated command inventory is exactly **7 of 29** gameplay interactions:
`MULTIPLE_CHOICE`, `CHOOSE_GENERAL`, `CHOOSE_SUIT`, `CHOOSE_KINGDOM`,
`CHOOSE_ORDER`, `INVOKE_SKILL`, and `SURRENDER`. No new command ID is introduced.
Registry classification uses the complete message type/source/destination/command
key rather than command identity alone.

### `S_COMMAND_MULTIPLE_CHOICE` request

```json
{
  "schema_version": 1,
  "skill_name": "tuxi",
  "options": ["left", "right"],
  "disabled_options": ["left"],
  "tip": "choose a side"
}
```

| Field | Type | Required | Contract |
|---|---|---|---|
| `schema_version` | integer | yes | Exactly `1` |
| `skill_name` | string | yes | Legacy skill/reason string |
| `options` | array of strings | yes | Order and duplicates are preserved |
| `disabled_options` | array of strings | yes | Structural data; subset membership is not enforced here |
| `tip` | string | yes | Existing display tip |

The V1 request remains exactly `[skill_name, "a+b", "disabled", tip]`.
Empty option tokens retain the legacy `QString::split('+')` behavior; empty
disabled tokens are omitted when normalizing to the legacy logical payload.

### `S_COMMAND_MULTIPLE_CHOICE` reply

```json
{
  "schema_version": 1,
  "choice": "cancel"
}
```

Both fields are required and strictly typed. `choice` is structural at this
boundary; membership, enabled state, and gameplay legality remain server
authority. `"cancel"` is an ordinary valid string. The V1 reply remains the
scalar choice string.

Unknown additional object members are ignored for compatible schema evolution.
Missing required members, wrong types, a schema other than `1`, and legacy
array/scalar shapes sent inside a migrated V2 envelope fail with
`InvalidPayload`; there is no V1 fallback after V2 activation. Parsing and
registry transforms are transactional.

### Simple-choice schemas

Every object below requires integral `schema_version: 1`. The listed fields are
also required and strictly typed; unknown additional members are ignored.

| Interaction | V2 Room → Client request payload | V2 Client → Room reply payload | Normalized V1 logical payload |
|---|---|---|---|
| `CHOOSE_GENERAL` | `{"schema_version":1,"candidates":["caocao","liubei"]}` | `{"schema_version":1,"general":"caocao"}` | request string array; reply string |
| `CHOOSE_SUIT` | `{"schema_version":1}` | `{"schema_version":1,"suit":"spade"}` | request has no payload; reply string |
| `CHOOSE_KINGDOM` | `{"schema_version":1,"kingdoms":["wei","shu"]}` | `{"schema_version":1,"kingdom":"wei"}` | request `["wei+shu"]`; reply string |
| `CHOOSE_ORDER` | `{"schema_version":1,"reason":"turn"}` | `{"schema_version":1,"camp":"warm"}` | request/reply numeric enums |
| `INVOKE_SKILL` | `{"schema_version":1,"skill_name":"test_skill","data":"playerdata:sgs1"}` | `{"schema_version":1,"invoke":true}` | request two-string array; reply boolean |
| `SURRENDER` vote | `{"schema_version":1,"initiator_general":"caocao"}` | `{"schema_version":1,"surrender":false}` | request string; reply boolean |

The general, suit, and kingdom values are structurally validated strings.
Candidate membership, known suits, known kingdoms, and gameplay legality remain
server authority. This preserves `FreeChoose`, mini-scenario, and custom-scenario
general selection. Kingdom order and empty legacy token behavior are preserved.

`CHOOSE_ORDER` uses explicit mappings only:

| Legacy enum | V2 string |
|---|---|
| `S_REASON_CHOOSE_ORDER_TURN` | `turn` |
| `S_REASON_CHOOSE_ORDER_SELECT` | `select` |
| `S_CAMP_WARM` | `warm` |
| `S_CAMP_COOL` | `cool` |

Unknown strings and numeric values inside V2 typed objects fail with
`InvalidPayload`. `INVOKE_SKILL.data` remains an opaque legacy semantic string;
formats such as `playerdata:<name>` are not parsed by the wire boundary.

### Flow-aware exceptions

| Flow | V2 behavior |
|---|---|
| Room → Client Notification `INVOKE_SKILL` | Identity; legacy notification array remains inside the V2 envelope |
| Client → Room Request `SURRENDER` | Identity; surrender initiation remains payload-free |
| `LUCK_CARD` request/reply | Identity; excluded until aliased reply commands are normalized |
| `CHOOSE_DIRECTION` request/reply | Identity; excluded until aliased reply commands are normalized |

`RequestCoordinator` currently maps expected replies from `LUCK_CARD` to
`INVOKE_SKILL` and from `CHOOSE_DIRECTION` to `MULTIPLE_CHOICE`. Their request
command, expected reply command, and Client reply encoder identity need a
dedicated alias-normalization change before typed migration.

## Compatibility

```text
V1 codec does not parse V2
V2 codec does not parse V1
Negotiation still begins on V1
preferredVersion may be V2
activeVersion remains V1 until COMMIT
Production may become V2 per connection
Replay remains V1-compatible logical packets
Seven migrated interaction request commands use typed V2 objects
All non-migrated flows retain their current wire shape
```

The V2 codec has no dependency on legacy `Packet` or `PacketDescription`.
After V2 decode, the registry restores the legacy-compatible logical payload
before Client/server dispatch and before `encodeReplayV1()`. Replay therefore
never receives the typed V2 object. A future replay version and the remaining
22 gameplay payload migrations remain later work. The next protocol batch should
normalize the aliased `CHOOSE_DIRECTION` and `LUCK_CARD` reply-command identities
before migrating their payloads.

## Transport

Newline framing remains external to the codec. `encode()` returns only the JSON
object bytes and does not append `\n`.

The socket transport accumulates raw bytes dynamically. It accepts an encoded
frame of at most 65535 bytes plus its external newline delimiter and disconnects
on over-limit data, including an unterminated over-limit frame. It does not
round-trip protocol bytes through `QString` or Latin-1.
