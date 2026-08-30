# Replay V2 格式

## Replay 版本

| 格式 | 識別方式 | 事件編碼 | 目前政策 |
|---|---|---|---|
| Legacy V1 | 第一個非空白行沒有 `QSAN_REPLAY ` header | Protocol V1 array | 唯讀相容 (read-only compatibility) |
| Replay V2 | 明確的 `QSAN_REPLAY ` header | Protocol V2 envelope | 現行讀寫格式 |

新錄影一律寫成 Replay V2。現行 reader 仍能載入歷史 headerless V1
`.txt` 與 `.png`；舊版客戶端能否讀取 Replay V2 不作保證。

## Header

Replay V2 的第一個非空白行固定為：

```text
QSAN_REPLAY {"format_version":2,"protocol_version":2}
```

`format_version` 與 `protocol_version` 都是必要整數。未知的額外 header
欄位會被忽略，header 最長 4 KiB。格式版本與訊息協定版本是兩個獨立
契約：未來可以升級其中之一而不混淆另一個。

## Event

每個事件使用一行 UTF-8：

```text
<elapsed_ms> <compact Protocol V2 JSON>
```

Golden example：

```text
0 {"command":64,"destination":"client","message_id":"7","payload":["p1","hello"],"source":"room","type":"notification","v":2}
```

`elapsed_ms` 是非負、不可遞減的 `qint64` 十進位整數。事件 JSON 由
`ProtocolGameplayPayloadRegistry` 與 `ProtocolV2Codec` 編解碼，最大值沿用
`ProtocolV2Codec::MaxPacketSize`（65,535 bytes）。Replay V2 不在事件層自動
偵測 V1；V1 array 出現在 V2 檔案時會整份拒絕。

## 訊息識別

- 已有正數 `message_id`／`reply_to` 會完整保存，包括大於 `UINT32_MAX`
  及 `UINT64_MAX` 的值。
- `message_id == 0` 時，writer 使用每份 replay 獨立、由 1 開始的
  `ProtocolMessageIdGenerator` 補值；不修改傳入的 `ProtocolMessage`。
- Replay ID 不與 live TCP ID 共用狀態。
- `OFFER`／`ACK`／`COMMIT` 所用的 `S_COMMAND_PROTOCOL_SWITCH` 不寫入 replay；
  檔案 header 已決定整份事件的 codec。

## 讀取與錯誤

Reader 先完成全檔驗證，再交付事件，錯誤時不會留下部分載入狀態。

| 類別 | 例子 |
|---|---|
| `FileOpenFailure` | `.txt` 不存在或不可讀 |
| `UnsupportedContainer` | 副檔名不是 `.txt`／`.png` |
| `EmptyInput` | 空檔或只有空白 |
| `InvalidHeader` | 缺欄位、錯誤型別或非 JSON object |
| `UnsupportedFormatVersion` | `format_version` 不是 2 |
| `UnsupportedProtocolVersion` | Replay V2 的 `protocol_version` 不是 2 |
| `InvalidTimelineEntry` | 缺 elapsed 或 message |
| `InvalidElapsedTime` | 負數、非整數、溢位或時間倒退 |
| `ProtocolDecodeFailure` | message 不符合指定 codec／typed payload 契約 |
| `PacketTooLarge` | header 或事件超過格式上限 |

`Replayer::isValid()`、`loadError()` 與 `errorString()` 將錯誤交給 UI；UI
顯示診斷後停止建立 RoomScene，不會靜默播放空檔。

## 相容矩陣

| Reader | Replay V1 | Replay V2 | 未來／不支援版本 |
|---|---:|---:|---:|
| current | read | read/write | explicit reject |
| legacy | historical | not guaranteed | no |

`.png` 仍是既有 `TXT2PNG`／`PNG2TXT` 容器；解出後的 bytes 與 `.txt` 使用
同一個 ReplayReader。快照目錄命名、timeline、seek、ReplayIndex 與 takeover
流程不因格式版本改變。

## 架構邊界

`src/core/replay/replay-codec.*` 僅依賴 Qt Core 與既有 protocol core。
Reader 將 Legacy V1 或 Replay V2 bytes 正規化為 `ReplayEvent`；之後的
`Replayer`、`ReplayIndex`、`ReplayGameState` 與 Client replay dispatch 只傳遞
logical `ProtocolMessage`，不再 stringify 或透過 `Packet::parse()` 重解 wire。
