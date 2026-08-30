# ProtocolMessage boundary

`QSanProtocol::ProtocolMessage` is the only C++ message model used by production
TCP, Replay, client dispatch, and server request correlation.

```text
Application / typed payload registry
                 |
          ProtocolMessage
                 |
        Protocol V2 codec
                 |
       newline-framed TCP or Replay V2
```

## Canonical fields

| Field | Meaning |
|---|---|
| `version` | Always `ProtocolVersion::V2` |
| `type` | Request, reply, or notification |
| `source` / `destination` | Room, lobby, or client endpoint |
| `messageId` | Positive full-range `quint64` identity |
| `replyTo` | Positive full-range request identity on replies |
| `command` | Numeric `S_COMMAND_*` identity |
| `payload` | Registered schema-versioned `QVariantMap` |
| `hasPayload` | Always true for an encoded production V2 frame |

`ProtocolMessageIdGenerator` is connection-local and preserves the full
`quint64` range. `RequestCoordinator` requires exact `replyTo` and command
matching; historical reply aliases are not accepted.

## Validation order

1. `ProtocolV2Codec` validates the envelope, positive message IDs, reply
   correlation shape, JSON domain, and positive payload schema version.
2. `ProtocolPayloadRegistry` resolves the complete
   type/source/destination/command identity and validates the exact production
   payload schema.
3. `ProtocolGameplayPayloadRegistry` validates the 29 interaction-specific
   request/reply variants while keeping the typed V2 object for dispatch.

Unregistered flows fail closed. No decoded frame is converted to a V1 array,
scalar, delimiter string, `Packet`, or compatibility facade.

## Non-goals

- Protocol negotiation or runtime switching;
- Protocol V1 encode/decode;
- implicit payload passthrough;
- Replay V1 conversion;
- TUI Replay support.
