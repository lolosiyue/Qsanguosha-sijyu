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
  -> ProtocolInteractionRequestBuilder / InteractionDescriptorRegistry
  -> canonical InteractionRequest
  -> DesktopInteractionView or TuiInteractionView
  -> canonical InteractionResponse
  -> ClientCore validation / deadline / exactly-once
  -> InteractionReplyEncoder
  -> Protocol V2 typed reply with full quint64 reply_to
```

GUI 與 TUI 共用同一份 registry、canonical model、validator、deadline、correlation
與 reply encoder。Public submission 只有 typed `submitInteractionResponse()`；不為
每個 interaction 建立 `respondTo*` API。Invalid、stale、duplicate、expired、disabled
或 correlation mismatch 都不會發出 wire reply。

## Shared live state boundary

```text
NativeClientSocket -> ClientLiveSession -> typed ProtocolMessage
  -> ClientGameStateReducer -> ClientGameState
  -> GUI Client signal adapter / TUI renderer
```

Production GUI 不再自行擁有第二套 socket decoder 或 gameplay reducer。GUI-only
presentation 仍留在既有 `Client` facade；TUI-only rendering 留在 `src/tui`。Reconnect
通知在 `STATE_SYNC begin/end` 之間 reduce 到 staging state，完成後才原子替換，且
舊 generation 的 pending response 不會重送。

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
drift. TUI 對所有 Room→Client production flow 的 reducer／presentation／interaction／
session 分類另見
[`artifacts/tui-flow-coverage.json`](../artifacts/tui-flow-coverage.json)。Local CTest
不屬本次本機 gate；remote cross-platform、production GUI 及完整 live TCP game 仍是
分開的驗收證據。
