# Protocol V2 wire contract

## Status

```text
Wire envelope: defined
Codec: implemented
Production activation: enabled after explicit V1 barrier
Replay: V1 logical normalization
Framing: byte-oriented newline transport, maximum 65535 encoded bytes
Typed gameplay payloads: 29/29 production interaction commands
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
| V2／registered Room → Client interaction request | Legacy logical payload → schema-versioned typed object |
| V2／registered Client → Room interaction reply | Legacy logical reply → schema-versioned typed object |
| V2／non-interaction or flow-aware exception | Identity |

The migrated command inventory is exactly **29 of 29** production gameplay
interactions. No new command ID is introduced.
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

### Remaining production interaction schemas

Every payload below is also an object with integral `schema_version: 1`.
Array suffixes mean homogeneous JSON arrays. A `cancelled` or `has_value`
discriminator preserves legacy missing-payload cancellation without using JSON
`null`.

| Request command | Required Room → Client fields | Client reply command | Required reply fields |
|---|---|---|---|
| `CHOOSE_ROLE` | none | `CHOOSE_ROLE` | `cancelled`; when false: `players[]`, `roles[]` |
| `CHOOSE_DIRECTION` | none | `CHOOSE_DIRECTION` | `direction` |
| `EXCHANGE_CARD` | `max_cards`, `min_cards`, `include_equip`, `prompt`, `optional`, `pattern` | `DISCARD_CARD` | `cancelled`; when false: `card_ids[]` |
| `ASK_PEACH` | `dying_player`, `peach_count` | `RESPONSE_CARD` | response-card reply below |
| `SKILL_GUANXING` | `card_ids[]`; optional `mode` (`up_only`, `both_sides`, `down_only`) | `SKILL_GUANXING` | `top_card_ids[]`, `bottom_card_ids[]` |
| `SKILL_GONGXIN` | `player`, `enable_heart`, `card_ids[]`, `enabled_card_ids[]` | `SKILL_GONGXIN` | `cancelled`; when false: `card_id` |
| `SKILL_YIJI` | `card_ids[]`, `optional`, `max_cards`, `players[]`, `prompt` | `SKILL_YIJI` | `cancelled`; when false: `card_ids[]`, `target_player` |
| `PLAY_CARD` | `player` | `RESPONSE_CARD` | response-card reply below |
| `RESPONSE_CARD` | `pattern`, `prompt`; optional contiguous `handling_method`, `notice_index` | `RESPONSE_CARD` | `cancelled`; when false: `card_text`, `targets[]`, `activation_skill_name`, `activation_skill_instance_id` |
| `DISCARD_CARD` | `max_cards`, `min_cards`, `optional`, `include_equip`, `prompt`, `pattern` | `DISCARD_CARD` | `cancelled`; when false: `card_ids[]` |
| `CHOOSE_PLAYER` | `players[]`, `skill_name`, `prompt`, `max_players`, `min_players` | `CHOOSE_PLAYER` | `cancelled`; when false: `players[]` |
| `TRIGGER_ORDER` | `options[]` of objects, `optional` | `TRIGGER_ORDER` | `trigger` |
| `NULLIFICATION` | `trick_name`, `source_player`, `target_player` | `RESPONSE_CARD` | response-card reply above |
| `SHOW_CARD` | `requestor` | `RESPONSE_CARD` | response-card reply above |
| `AMAZING_GRACE` | `refusable`, `reason`, `prompt` | `AMAZING_GRACE` | `card_id` (`-1` retains legacy refusal) |
| `PINDIAN` | `requestor`, `player` | `RESPONSE_CARD` | response-card reply above |
| `CHOOSE_CARD` | `player`, `zone_flags`, `reason`, `hand_cards_visible`, `handling_method`, `disabled_card_ids[]`, `can_cancel` | `CHOOSE_CARD` | `cancelled`; when false: `card_id` |
| `CHOOSE_ROLE_3V3` | `scheme`, `roles[]` | `CHOOSE_ROLE_3V3` | `role` |
| `LUCK_CARD` | none | `LUCK_CARD` | `use_luck_card` |
| `ASK_GENERAL` | none | `ASK_GENERAL` | `general` |
| `ARRANGE_GENERAL` | optional `generals[]` | `ARRANGE_GENERAL` | `cancelled`; when false: `generals[]` |
| `QML_INTERACT` | `kind`; either `legacy_qml` + `qml_path` + `parameters`, or `structured` + `interaction` | `QML_INTERACT` | `has_value`; when true: arbitrary JSON-domain `value` |

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

`RequestCoordinator` retains the dedicated request command as the expected
identity, while accepting historical `LUCK_CARD → INVOKE_SKILL` and
`CHOOSE_DIRECTION → MULTIPLE_CHOICE` replies only for those two requests. Both
current and historical clients therefore remain compatible without weakening
ordinary `INVOKE_SKILL` or `MULTIPLE_CHOICE` reply matching. Current replies use
the dedicated typed schema; historical shared-command replies use the already
typed `INVOKE_SKILL` or `MULTIPLE_CHOICE` schema.

## Compatibility

```text
V1 codec does not parse V2
V2 codec does not parse V1
Negotiation still begins on V1
preferredVersion may be V2
activeVersion remains V1 until COMMIT
Production may become V2 per connection
Replay remains V1-compatible logical packets
All 29 production interaction request commands use typed V2 objects
Non-interaction flows and the two explicit flow exceptions retain their shape
```

The V2 codec has no dependency on legacy `Packet` or `PacketDescription`.
After V2 decode, the registry restores the legacy-compatible logical payload
before Client/server dispatch and before `encodeReplayV1()`. Replay therefore
never receives the typed V2 object. A future replay version remains separate
work; the production gameplay interaction inventory is fully migrated.

## Transport

Newline framing remains external to the codec. `encode()` returns only the JSON
object bytes and does not append `\n`.

The socket transport accumulates raw bytes dynamically. It accepts an encoded
frame of at most 65535 bytes plus its external newline delimiter and disconnects
on over-limit data, including an unterminated over-limit frame. It does not
round-trip protocol bytes through `QString` or Latin-1.
