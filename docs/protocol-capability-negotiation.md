# Protocol capability negotiation

- 狀態：V1-compatible capability negotiation 完成
- transport／framing／codec：仍為 Protocol V1
- preferred version：雙方均廣告 V2 時為 V2
- active version：固定為 V1，直到另案提供 runtime switch contract
- Protocol V2 codec：contract 已完成，但不參與 production negotiation transport

## Server advertisement

Server 仍以 V1 `S_COMMAND_CHECK_VERSION` notification 廣告能力：

```text
<gameVersion>:<modName>:protocol=1,2:<cardCount>
```

第一欄、第二欄與最後一欄保留 legacy client 的既有語義。新 client 只在中間
extension 欄位搜尋 `protocol=`；`foo=bar` 等未知 extension 會被忽略。缺少 token
視為 legacy `{V1}`，malformed token 記錄 diagnostic 後 fallback `{V1}`。

## Client advertisement

Client 仍以 V1 `S_COMMAND_SIGNUP` notification 傳送前三個 legacy 欄位，並在第四欄
附加 capability object：

```json
[
  false,
  "<base64-user-name>",
  "<avatar>",
  {
    "schema_version": 1,
    "protocol_versions": [1, 2]
  }
]
```

舊 server 只讀 index 0、1、2；新 server 對三欄 signup 視為 `{V1}`。第四欄若不是
object、schema 不支援、versions 不是 array，或沒有 mandatory V1，均不拒絕原本合法
的 signup，只記錄 diagnostic 並 fallback `{V1}`。未知 key 與未知 future version
會被忽略。

## Deterministic selection

| Local | Peer | Preferred | Active |
|---|---|---|---|
| V1 | V1 | V1 | V1 |
| V1,V2 | V1 | V1 | V1 |
| V1 | V1,V2 | V1 | V1 |
| V1,V2 | V1,V2 | V2 | V1 |

`ProtocolNegotiation` 是純 QtCore 演算法。`ProtocolSessionState` 是一般 value object，
沒有 global mutable state。production `Client` 與每個 `ServerPlayer` 各自持有一份
state，因此同一 dedicated server 的不同連線可以保存不同 peer capability。

## Compatibility matrix

| Client | Server | Contract |
|---|---|---|
| Old V1 | Old V1 | 原 V1 signup／wire 不變 |
| Old V1 | New | 三欄 signup；preferred／active 均 V1 |
| New | Old V1 | 舊 server 忽略第四欄；client 未見 token，active V1 |
| New | New | preferred V2；active V1 |

## Validation policy

Feature／PR branch 不執行 CTest。協商契約由
`qsanguosha_protocol_negotiation_tests` 直接執行；Linux PR gate 直接執行 network
integration level 2。`debug` push 只建置 headless server、協商測試與 network
integration targets，不編譯 Linux GUI。CTest workflow 僅在 `main` 執行。

## Non-goals

- 不新增 protocol switch／ack command
- 不改 packet envelope、newline framing、replay 或 gameplay payload
- 不把 active version 設為 V2
- 不因 `ProtocolV2Codec` 存在而建立 runtime codec registry

## Roadmap status

```text
V1 codec boundary            Complete
Capability negotiation       Complete
ProtocolMessage model        Complete
Protocol V2 envelope         Complete
Protocol V2 codec            Complete
Runtime V2 switching         Not Started
Gameplay V2 migration        Not Started
Replay V2                    Not Started
```
