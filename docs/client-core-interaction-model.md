# ClientCore canonical interaction architecture

- 狀態：F1.1 Architecture Cleanup 完成（2026-08-29）
- 範圍：PR #13 的 client interaction 中間層
- 相容邊界：不改 server gameplay、RoomScene UI 或 ClientCore typed model；
  Protocol V2 已逐連線啟用，首個 typed wire payload 僅涵蓋
  `S_COMMAND_MULTIPLE_CHOICE`。TUI、Android、WASM 與 structured `askForQml`
  仍不在本階段。

## 最終分類

| 類別 | 數量 | 定義 |
|---|---:|---|
| Canonical typed | 28 | request 與 response 都只使用 typed `std::variant` payload |
| Legacy adapter | 1 | `S_COMMAND_QML_INTERACT` 的既有 `[qmlPath, params]` 協定 |
| Implicit passthrough | 0 | 不存在未登記或繞過 descriptor 的 built-in interaction |

`QML_INTERACT` 是明確的 legacy exception，不得描述為 canonical structured interaction。未識別的 structured custom type 會安全拒絕；完整遷移留給 F1.2。

## 資料流

```text
Protocol V1 request，或 Protocol V2 typed request
  -> ProtocolGameplayPayloadRegistry 正規化成 legacy-compatible logical payload
  -> InteractionDescriptorRegistry request builder
  -> InteractionRequestFactory
  -> canonical InteractionRequest
  -> ClientCore validation / deadline / exactly-once
  -> DesktopInteractionView
  -> typed presenter port
  -> RoomScene / Dashboard / dialogs

UI response
  -> canonical InteractionResponse
  -> Client::submitInteractionResponse()
  -> ClientCore validation
  -> descriptor-selected LegacyV1InteractionReplyAdapter（logical reply）
  -> exactly one replyToServer()
  -> ProtocolGameplayPayloadRegistry 依 active version 編成 V1 scalar 或 V2 object
```

`DesktopInteractionView` 只查 registry 並呼叫 typed presenter port；舊 `Client::presentStructuredInteraction()` 及其 20+ case switch 已移除。

## 單一真相來源

### Request

`InteractionRequest` 只保留 identity、共用顯示資料、deadline、response shape 與一個 `InteractionPayload`：

```cpp
struct InteractionRequest {
    quint64 requestId;
    uint serverSerial;
    int command;
    InteractionType type;
    QString skillName;
    QString prompt;
    bool cancelable;
    qint64 timeoutMs;
    qint64 deadlineMs;
    InteractionResponseShape responseSchema;
    InteractionPayload payload;
    QVariantMap metadata;
};
```

已移除 `options`、`optionsEnumerated`、`cards`、`players` 與 `context`。Production builder、presenter、validator 都不得 fallback 到舊欄位。

### Response

`InteractionResponse` 只保留 identity、kind 與一個 `Payload` variant。支援 cancel、option、player selection、card selection、assignment、rearrangement、distribution、general arrangement、custom response；舊 scalar/list 欄位、generic `QVariantMap payload` 與 `structuredPayload` 已移除。

## Production registry

`InteractionDescriptorRegistry` 是 command/type mapping 的唯一來源。每個 entry 直接保存實際行為：

| 欄位 | 行為 |
|---|---|
| `command` / `type` | Protocol V1 command 與 canonical identity |
| `builder` | 真正的 `Client::Callback` request builder |
| `responseShape` | ClientCore shape-validator strategy |
| `presenter` | `IClientInteractionPresenter` typed member-function pointer |
| `replyEncoder` | 真正的 V1 encoder function pointer |
| `support` | `canonical_typed` 或 `legacy_adapter` |

Client constructor 由 registry 建立 `m_interactions`；Desktop view 亦由同一 registry dispatch。`ClientCore` 使用八個 shape-validator member pointers，不以 29-command god switch 驗證。

## Metadata 規則

Gameplay constraint 必須位於 typed payload。Request metadata 僅允許：

- `eligibility_diagnostic`

ClientCore 在 request 啟動時移除其他 key。Choice 的 `synthetic_cancel` 是 `InteractionOption::metadata`，只標示 desktop 顯示行為，不承載 gameplay rule。

## 特殊 interaction

| Interaction | Canonical semantics |
|---|---|
| Guanxing | `UpOnly`、`BothSides`、`DownOnly`；上下限與完整卡集合由 validator 檢查 |
| Gongxin | target、visible cards、selectable cards、heart operation 分開；visible 不等於 selectable |
| Yiji | available cards、allowed targets、per-response min/max、remaining count |
| Pindian | opponent、card constraints、hidden-until-resolved / reveal policy；不依賴 `Client::Status` 作語意 |
| Amazing Grace | authoritative available/disabled、taken cards、selectable、cancelable |
| Arrange General | `GeneralArrangement` response；檢查順序、slot count、duplicate 與 unknown general |
| ChooseRole | multi-player `RoleAssignment` |
| ChooseRole3v3 | single canonical `Option`，不冒充 assignment |
| Play/Response Card | pattern、handling method、targets、card text 與 virtual-card permission 均在 typed payload |

`ICardEligibilityProvider` 的輸出是 `suggestedCards`、`suggestedDisabledCards`、diagnostic，永遠只是 presentation hint；不得覆蓋 server-authoritative selection。

## Logical reply adapter 與 wire boundary

所有 accepted UI response 經 `Client::submitInteractionResponse()` 補上 request id、server serial、command，再由 ClientCore 驗證。只有 accepted response 才可進入 `LegacyV1InteractionReplyAdapter`。該 adapter 名稱保留歷史來源；其輸出現在是 logical payload，不代表連線必定使用 V1。

`S_COMMAND_MULTIPLE_CHOICE` 的 logical request 仍為四個字串，logical reply
仍為 scalar choice。`ProtocolCodecRouter` 的 registry 只在 V2 wire 邊界把兩者
轉為 schema-versioned objects；因此 V1/V2 產生完全相同的 canonical
`InteractionRequest`，ClientCore 與 Desktop presenter 不含 protocol-version branch。

Adapter 保留既有 wire 細節，包括：

- Exchange 仍以 `S_COMMAND_DISCARD_CARD` 回覆。
- Play/Response/Peach/Nullification/Show/Pindian 仍以四欄 card response 回覆。
- Guanxing 為 `[topCards, bottomCards]`。
- Yiji 為 `[cardIds, target]`。
- Amazing Grace cancel 仍為 `-1`。
- Legacy QML 保留任意 `QVariant` reply，不強制轉為 object。

Invalid、duplicate、stale 或 expired response 不會產生 wire reply；valid response 只產生一次。

## Inventory 與 matrix

提交 artifact：`artifacts/client-core-interaction-matrix.json`

它不是手寫 source。Production GUI executable 直接從 registry 生成：

```powershell
debug\QSanguosha.exe --interaction-inventory artifacts\client-core-interaction-matrix.json
```

`qsanguosha_client_core_contract` 會透過 production executable 再次生成臨時檔，
驗證以下摘要後與提交 artifact 逐字比較；不再為 matrix 另建第二條 CTest：

- schema version 2
- total 29
- canonical typed 28
- legacy adapter 1
- implicit passthrough 0
- missing builder/presenter/validator/reply encoder 全為 0
- fake recorder 實際 presenter dispatch 29 次，覆蓋 29 個 type

## 驗證入口

| Gate | 覆蓋 |
|---|---|
| `qsanguosha_client_core_contract` | canonical snapshot、identity、special semantics、metadata allowlist、deadline、cancel、duplicate/stale、production registry 生成、28+1 分類、29 presenter dispatch、artifact drift |
| `qsanguosha_interaction_reply_adapter_contract` | Protocol V1 scalar/card/structured/custom wire encoding |
| local response UI suite | production RoomScene/Dashboard intent 與實際 reply capture |
| GUI/network/server/Docker gates | integration 與跨平台 regression |

`qsanguosha_client_core` 仍只 link `Qt6::Core`，不 link engine 或 GUI。

## 後續工作

- F1.2 structured custom interaction / `askForQml` migration
- TUI、Android、WASM front-end
- Desktop RoomScene redesign

| Protocol roadmap item | 狀態 |
|---|---|
| Protocol V1 codec boundary | Complete |
| Protocol V2 codec／runtime activation | Complete |
| Typed gameplay payload migration | In Progress（1/29：`MULTIPLE_CHOICE`） |
| Capability negotiation | Complete |
| Replay version bump | Not Started |

Replay version bump 與其餘 28 個 payload 不屬 F1.1。
