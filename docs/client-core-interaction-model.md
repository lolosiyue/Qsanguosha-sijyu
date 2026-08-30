# ClientCore interaction model

## Production contract

The production registry contains 29 interactions. All 29 use the same direct
typed path; `S_COMMAND_QML_INTERACT` is no longer a legacy adapter.

| Classification | Count |
|---|---:|
| Direct typed | 29 |
| Legacy adapter | 0 |
| Implicit passthrough | 0 |

```text
Protocol V2 typed request
  -> ProtocolGameplayPayloadRegistry validation
  -> InteractionDescriptorRegistry builder
  -> canonical InteractionRequest
  -> ClientCore validation / deadline / exactly-once
  -> typed desktop presenter
  -> canonical InteractionResponse
  -> InteractionReplyEncoder
  -> Protocol V2 typed reply with full quint64 reply_to
```

The desktop presenter, validator, and reply encoder are selected by the
production descriptor. Invalid, stale, duplicate, expired, or mismatched
responses do not emit a wire reply.

## QML interaction

QML requests use a structured custom-interaction object containing a type,
schema version, title, payload, and response schema. A reply is:

```json
{
  "schema_version": 1,
  "has_value": true,
  "value": {}
}
```

Cancellation uses `has_value: false`. There is no positional
`[qml_path, parameters]` wire payload and no `LegacyV1InteractionReplyAdapter`.

## Cancellation

Cancellation is explicit and schema-specific. Examples include
`cancelled: true` and `has_value: false`. Card identifiers never use `-1` as a
wire cancellation sentinel.

## Inventory and gates

The production GUI writes the registry-derived artifact:

```powershell
debug\QSanguosha.exe --interaction-inventory artifacts\client-core-interaction-matrix.json
```

The artifact contract is schema version 3, total 29, direct typed 29, and
implicit passthrough 0. Focused executables cover registry completeness,
presenter dispatch, response validation, typed reply encoding, and artifact
drift. Local CTest is not required for this migration; remote cross-platform and
live TCP gates remain separate evidence.
