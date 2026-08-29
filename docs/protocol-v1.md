# Protocol V1 wire contract

- 狀態：Protocol V1 codec boundary 完成
- 預設 codec：`QSanProtocol::ProtocolV1Codec`
- 相容 facade：`QSanProtocol::Packet`
- wire／replay 版本：維持既有 Protocol V1，沒有版本提升

## Framing

Protocol V1 使用 newline-delimited JSON。每一行是一個 JSON packet；codec
只編碼或解碼單一 packet，不加入或移除換行。`NativeClientSocket` 負責在送出時
補上 `\n`，並以 `canReadLine()` 取得完整輸入行。

## Envelope

```text
[globalSerial, localSerial, description, command, optionalBody]
```

| 索引 | 欄位 | V1 契約 |
|---:|---|---|
| 0 | `globalSerial` | 無號整數；建構 `Packet` 時維持 `0`，只有明確呼叫 `createGlobalSerial()` 才遞增 |
| 1 | `localSerial` | 無號整數；client reply 用它回指 server request 的 `globalSerial` |
| 2 | `description` | `PacketDescription` bit mask 的原始數值 |
| 3 | `command` | `CommandType` 的原始數值；本邊界不重新編號 |
| 4 | `optionalBody` | legacy `QVariant`／JSON representation；`QVariant::isNull()` 時不輸出第五欄 |

四欄與五欄 envelope 都是合法 V1。為保持完整 compatibility，重用同一個
`Packet` 解析四欄 envelope 時，仍保留該物件先前的 `messageBody`；新程式不應
依賴此 legacy 行為，Protocol V2 設計亦不應沿用。

## Packet-description bits

| 分類 | Mask | 既有值 |
|---|---:|---|
| Type | `S_TYPE_MASK` (`0x00f`) | request=`0x001`、reply=`0x002`、notification=`0x004` |
| Source | `S_SRC_MASK` (`0x0f0`) | room=`0x010`、lobby=`0x020`、client=`0x040` |
| Destination | `S_DEST_MASK` (`0xf00`) | room=`0x100`、lobby=`0x200`、client=`0x400` |

V1 decoder 只要求 header 可依現有 `JsonUtils::isNumberArray()` 規則轉為數值。
未知 command number 或 description bits 仍會被接受並原值保存；codec 不自行
建立新規則或重寫舊值。

## Request／reply correlation

| 階段 | 行為 |
|---|---|
| Server request | `RequestCoordinator` 保存 request packet 的 `globalSerial` 與預期 reply command |
| Client reply | `Client::replyToServer()` 把最後一個 server request serial 寫入 reply 的 `localSerial` |
| Server validation | `RequestCoordinator::processResponse()` 同時驗證 expected command 與 `localSerial` |

某些 request／reply command 不同，例如 `S_COMMAND_PLAY_CARD` 對應
`S_COMMAND_RESPONSE_CARD`；配對仍由 `RequestCoordinator` 管理，不屬 codec。

## Encoding and decoding

`Packet::parse()`、`Packet::toJson()` 與 `Packet::toString()` 是現有 public API，
實作統一委派給無狀態 (stateless) `ProtocolV1Codec`。合法 packet 的 compact JSON
bytes、欄位順序與 `QVariant` body 表示不變。

最大 packet 大小為 65535 bytes：

- encode 結果超過限制時回傳空 `QByteArray`；diagnostic overload 同時提供錯誤文字。
- decode 輸入超過限制時拒絕，且不修改 output packet。
- codec 不丟出未受控例外；null output pointer、空 input 與 malformed input 都有分類結果。

| `ProtocolDecodeError` | 條件 |
|---|---|
| `NullOutput` | output `Packet *` 為 null |
| `EmptyInput` | input 為空 |
| `PacketTooLarge` | input 超過 65535 bytes |
| `InvalidJson` | JSON parser 拒絕 input |
| `InvalidEnvelope` | root 不是 array，或欄位數不是四／五 |
| `InvalidHeader` | 前四欄不符合既有數值轉換規則 |

`UnsupportedVersion` 保留給未來版本選擇；Protocol V1 envelope 本身沒有版本欄位，
本階段不會產生該錯誤。

## Replay compatibility

`Recorder` 仍保存 server socket 收到的原始 V1 packet line，外加既有 elapsed-time
前綴。`Replayer`、`ReplayIndex` 與 client playback 仍把保存的 command 交給
`Packet::parse()`。舊 `.txt` 與 `.png` replay 的 framing、壓縮內容及 packet bytes
沒有改變，本階段沒有 replay version bump 或 migration。

## Validation

`qsanguosha_protocol_v1_contract` 以 hard-coded golden bytes 覆蓋 request、reply、
notification、serial、description bits、command ID、Unicode、nested body、四／五欄
decode、round trip、malformed input、大小限制、facade delegation 與 diagnostic result。

## Versioning boundary

| 項目 | 狀態 |
|---|---|
| Protocol V1 codec boundary | Complete |
| Protocol V2 codec／payload | Not Started |
| Capability negotiation／handshake | Complete；preferred 可為 V2，active 固定 V1 |
| Runtime codec switching | Not Started |
| Replay version bump／migration | Not Started |

Capability wire、fallback 與 per-connection state contract 見
[`protocol-capability-negotiation.md`](protocol-capability-negotiation.md)。未來
Protocol V2 不應依賴隱含 `QVariant` schema；下一個 slice 必須先提供實際 codec 與
switch contract，才能改變 active version 或 gameplay wire。
