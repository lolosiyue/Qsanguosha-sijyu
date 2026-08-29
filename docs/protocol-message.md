# Codec-neutral ProtocolMessage

- 狀態：C++ canonical codec boundary 完成
- active protocol：Protocol V1
- Protocol V2 wire schema：未定義

## Boundary

```text
Application / protocol routing
            |
     ProtocolMessage
       /           \
ProtocolV1Codec   future ProtocolV2Codec
       |                  |
V1 JSON array       V2 wire object (not defined)
```

`IProtocolCodec` 的 encode／decode 只接受 `ProtocolMessage`。`Packet` 保留為 V1
compatibility facade，不是 future codec 的 required API。production client、server、
socket framing 與 replay call site 本階段仍可繼續使用 `Packet`。

## Canonical fields

| Field | Meaning |
|---|---|
| `version` | message 所屬 protocol version；不會自行切換 connection |
| `type` | request／reply／notification，與 source／destination 分離 |
| `source`／`destination` | room／lobby／client endpoint |
| `messageId` | message identity；V1 adapter 對應 `globalSerial` |
| `replyTo` | reply correlation；V1 adapter 對應 `localSerial` |
| `command` | 既有 `S_COMMAND_*` numeric identity；本階段不重編號 |
| `payload` | QtCore transitional bridge，不是 V2 wire type registry |
| `hasPayload` | 區分 absent 與 explicitly present null |

enum 的 C++ underlying value 只供 V1 adapter 保存未知 numeric component，不是 V2
wire contract。V1 combined `PacketDescription` 只存在 adapter 內；canonical message
沒有 combined bitmask。

## V1 adapter

`protocol-v1-message-adapter` 是 production mapping 的唯一真相：

| Protocol V1 | ProtocolMessage |
|---|---|
| `globalSerial` | `messageId` |
| `localSerial` | `replyTo` |
| `S_TYPE_*` | `type` |
| `S_SRC_*` | `source` |
| `S_DEST_*` | `destination` |
| `CommandType` | `int command` |
| fifth field present | `hasPayload = true` |

未知 description component 與 command number 仍可由 V1 decode／encode 原值 round trip。
四欄 decode 重用舊 `Packet` 時保留既有 body 的行為只在 facade adapter 實作，不進入
`ProtocolMessage` 或 future codec lifecycle。

## Structural validation

`validateProtocolMessage()` 只檢查 version、message type 與 endpoint 是否為已知值。
它不驗證 gameplay payload、卡牌合法性、目標選擇或 request/reply command pairing。
Protocol V1 codec 為保存 legacy unknown numeric values，不會強制套用此 validation。

## Payload contract

`QVariant` 只作現階段 C++ bridge。future V2 codec 必須：

- 明列允許的 JSON-domain scalar／array／object／null 型別。
- 不把 Qt-specific QVariant type name 放上 wire。
- 另行定義正式 schema、malformed policy 與 golden bytes。

以下僅為 future field inventory，屬 **DRAFT / NOT WIRE CONTRACT**：

```text
version, type, source, destination, message_id, reply_to, command, payload
```

## Non-goals

- 不新增 `ProtocolV2Codec`。
- 不新增 switch／ack 或令 `activeVersion` 變成 V2。
- 不修改 newline framing、replay、gameplay semantics 或 command registry。
