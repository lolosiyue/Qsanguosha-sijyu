# ClientCore 與結構化 interaction request

- Status: **F1 第一個垂直切片已落地**（2026-08-29）
- Milestone: Client Architecture F1
- Scope: client 端 interaction 中間層；不改 wire protocol、server request 語意、
  卡牌規則、extensions Lua API，亦不重寫 Client 或 RoomScene。

## 1. 問題

Client 收到 server request 之後，interaction 一直係直接落 UI：

```
Client → RoomScene → Dashboard/Dialog → replyToServer()
```

規則約束（可以揀邊個、要揀幾多個、可唔可以取消、幾時過期）散落喺 `Client`
幾個公開可寫欄位（`players_to_choose`、`choose_max_num`、`choose_min_num`、
`m_isDiscardActionRefusable`、`skill_name`…）同 `RoomScene::updateStatus()` 嘅
一個大 switch 入面。冇一個地方講得出「而家等緊乜嘢答案」，所以：

- 同一個 request 覆兩次（連撳兩下 OK）冇任何保護；
- 遲到／過期／唔屬於當前 request 嘅答案照樣上線；
- 第二個 front-end（text client、Android、WASM）冇嘢可以接，唯一嘅
  interaction 描述就係 RoomScene 本身。

## 2. 分層

```
Protocol / Client
        ↓  beginRequest(InteractionRequest)
    ClientCore
        ↓  IClientInteractionView::presentRequest()
    DesktopInteractionView   （本 PR）
    未來 TextClient / Android Client / WASM Lite
        ↓  submitResponse(InteractionResponse)
    ClientCore  ← 驗證 + exactly-once
        ↓  完成之後
Protocol / Client → replyToServer()
```

| 元件 | 位置 | 依賴 |
|---|---|---|
| `ClientCore`、interaction model、`ClientGameState` | `src/client/core/` | 只有 `Qt6::Core` |
| `IClientInteractionView` | `src/client/core/client-interaction-view.h` | 只有 `Qt6::Core` |
| `DesktopInteractionView` | `src/client/desktop-interaction-view.*` | `Client` |
| request builder／reply 出口 | `src/client/client.cpp` | GUI target |

`qsanguosha_client_core` 係獨立 static library，`CMakeLists.txt` 有同 engine
一樣嘅 link allowlist gate，只准 `Qt6::Core`。所以「ClientCore 唔依賴 GUI」係
由 build system 保證，唔係靠自律：喺 core 度 include `QWidget`／`QQuickItem`／
`Dashboard`／`RoomScene` 都會喺 configure 或者 link 爆。

佢亦刻意**唔** link `qsanguosha_engine`：interaction model 只講卡 id、玩家
`objectName` 同 option 字串，唔認識 `Card`／`Player`／`Skill`。dedicated server
build 一樣會起呢個 target，所以 server-only configure 都驗得到佢冇 GUI 依賴。

## 3. 資料模型

`src/client/core/interaction-model.h`：

| 型別 | 用途 |
|---|---|
| `InteractionType` | 五類已遷移 interaction 嘅穩定 tag |
| `InteractionOption` | 一個可揀項：wire `value`、顯示 `label`、`enabled`、metadata |
| `CardSelectionState` | 可選／禁用卡 id、min-max、pattern、handling method、係咪枚舉 |
| `PlayerSelectionState` | 可選玩家 objectName、min-max |
| `InteractionRequest` | request id、server serial、command、skill、prompt、options、卡／玩家約束、cancelable、timeout／deadline、context metadata |
| `InteractionResponse` | request id、答案種類、option／players／cards／card text／payload |
| `InteractionValidation` | 接納 或 一個 `InteractionRejection` 理由 |
| `ClientGameState` | 自己係邊個、局入面有邊啲玩家、卡 id 值域 |

`toSnapshot()` 出 compact JSON。`QJsonObject` 本身按 key 排序，所以同一個
request 喺任何平台都出同一串 bytes——snapshot test 靠呢個：

```json
{"cancelable":false,"max":1,"min":1,"request_id":42,"selectable_cards":[7,12],"type":"response_card"}
```

## 4. ClientCore 保證

### 4.1 驗證

`ClientCore::submitResponse()` 拒絕：

| 情況 | 理由 |
|---|---|
| 唔存在嘅 option | `unknown_option` |
| server 標咗唔可揀嘅 option | `disabled_option` |
| 非 selectable player | `unknown_player` |
| 同一個玩家揀兩次 | `duplicate_player` |
| 非 selectable card／超出卡 id 值域 | `unknown_card` |
| server 標咗禁用嘅卡 | `disabled_card` |
| 同一張卡交兩次 | `duplicate_card` |
| selection 數量錯誤 | `selection_count_out_of_range` |
| 唔准取消嘅 request 收到空答案 | `not_cancelable` |
| request 已過期 | `request_expired` |
| request id 不相符 | `request_id_mismatch` |
| duplicate reply | `already_completed` |
| request 已取消後再 reply | `request_cancelled` |
| 答案種類同 request 對唔上 | `kind_mismatch` |

被拒嘅答案**唔會上線**，而 request **唔會收檔**：佢仲喺度等一個好答案。

### 4.2 Exactly-once

一個 request 只完成得一次。被接納嘅答案即刻收檔（`m_active` 清空 + 記入
completion history），先至通知 view，所以任何 handler 喺嗰一刻再 submit 都只
會攞到 `already_completed`。History 深度 `CompletedHistoryLimit = 32`，足夠接住
任何合理嘅 double click，又唔會無限增長。

呈現係可以重入嘅：desktop 嘅 `setStatus()` 會即刻叫 `RoomScene::updateStatus()`，
而嗰度有幾條路會喺同一個 call stack 入面就答返呢個 request（例如 responding
狀態搵唔到可用嘅 view-as skill，就直接覆一個空答案）。所以 `beginRequest()` 傳
畀 view 嘅係一份 copy，唔係 `m_active` 嘅 reference。

### 4.3 Timeout / cancel

死線刻意用 **server** 嗰個 timeout（client timeout + `ServerTimeoutGraciousPeriod`）
再加 5 秒 margin，而唔係 UI 倒數用嗰個 client timeout：`RoomScene::doTimeout()`
就係喺 client timeout 嗰刻先送安全預設答案，如果 core 喺同一刻過期，呢個答案
就會被自己攔住，一局變成要等 server timeout 先行得落去。過咗呢條線嘅答案，
server 一定已經放棄咗。

`ServerInfo.OperationTimeout < 1`（冇限期）就唔會有死線。

Request 會喺以下情況被取消（唔會送任何 reply）：

| 原因 | 觸發點 |
|---|---|
| `superseded` | 下一個 server request 到埗（`Client::processServerRequest()`） |
| `abandoned` | 本機主動放棄（例如 `onPlayerChooseGeneral("")`） |
| `expired` | 過咗死線 |
| `disconnected` | `Client::disconnectFromHost()`（`gameOver()` 亦經呢度） |

### 4.4 View 生命週期

View 唔屬於 ClientCore。View 死之前要 `detachView()`（`DesktopInteractionView`
喺自己 destructor 度做）。Core 先係真相：view 死咗，pending request 照樣喺度，
之後嘅答案照樣驗得到。後嚟先接上嘅 view 會即刻收到未答嘅 request，唔會對住空
畫面等。

## 5. Desktop 相容策略

`DesktopInteractionView` 唔 include `RoomScene`、`Dashboard` 或者任何 `QWidget`。
Desktop 嘅呈現一直都係由 `Client` 嘅 signal 同 status 驅動，所以 adapter 只係
call `Client` 上面幾個 `presentXxx()` port，而每一個 port 就係原本 `askForXxx()`
尾段嗰一兩行（emit signal + `setStatus()`）原封不動搬過嚟。RoomScene／Dashboard
嘅 slot 一行都唔使改，外觀同操作亦因此保證唔變。

`RoomScene::updateStatus()` 嘅 `AskForPlayerChoose` 分支改為由 request 讀約束
（可選玩家、min／max、cancelable），數值同以前逐個一樣——`askForPlayerChosen()`
就係由同一份 server payload 砌呢個 request。分別係規則約束而家有一個單一出處，
而唔係散落喺 `Client` 嘅可寫欄位度。

換一個 front-end 只需要：

```cpp
ClientInstance->interactionCore()->setView(myView);
```

## 6. 已遷移 / 未遷移

Client 一共有 29 個 interactive command（`Client::m_interactions`）。本 PR 遷移
五個。

### 已遷移

| Command | Handler | Reply 出口 | 備註 |
|---|---|---|---|
| `S_COMMAND_CHOOSE_GENERAL` | `askForGeneral()` | `onPlayerChooseGeneral()` | option 清單係**建議**，見下 |
| `S_COMMAND_MULTIPLE_CHOICE` | `askForChoice()` | `onPlayerMakeChoice()` | 額外加一個 `cancel` sentinel |
| `S_COMMAND_CHOOSE_PLAYER` | `askForPlayerChosen()` | `onPlayerChoosePlayer()` | 枚舉玩家 + min／max |
| `S_COMMAND_INVOKE_SKILL` | `askForSkillInvoke()` | `onPlayerInvokeSkill()` | yes／no |
| `S_COMMAND_RESPONSE_CARD` | `askForCardOrUseCard()` | `onPlayerResponseCard()` | pattern 配對，唔枚舉 |

兩個刻意唔強制嘅地方，兩個都係跟返 server 自己嘅行為：

- **choose general 唔枚舉 option。** Server 喺 `Config.FreeChoose` 之下接受清單
  以外嘅武將（`src/server/player-decision-service.cpp:332`），而 free-choose
  dialog 同 `--test-general` 自動選將（`src/ui/roomscene.cpp:2255`）正正會咁答。
  ClientCore 唔可以攔一啲 server 本身收得起嘅答案，所以清單入咗 model 做建議，
  `optionsEnumerated = false`。
- **response card 唔枚舉可選牌。** 合法牌嘅集合係 pattern 配對嘅結果，而 pattern
  配對係 engine 規則；server 冇喺 request 入面列出可選牌。ClientCore 唔應該扮
  規則引擎自己猜一份出嚟——猜錯就會攔住一個合法回覆。呢類 request 只驗數量、
  取消權、卡 id 值域同 exactly-once。將來 server 有列清單嘅 interaction
  （`CHOOSE_CARD` 嘅 disabled ids、`AMAZING_GRACE`、`SKILL_GUANXING`…）遷移嗰陣
  就會行 `enumerated = true` 嗰條路。

`choice` 嘅 `cancel` sentinel：揀項 dialog 自己嘅 `objectName` 就係 `"cancel"`
（`src/ui/roomscene.cpp:2549`），撳 Esc 關窗會經同一個 slot 用 `"cancel"` 覆，
`BossModeExpStore` 亦有一個永遠 enabled 嘅 cancel 掣。Server 收得起，所以 core
亦要收得起，否則關窗會變成冇覆。

### 未遷移（24 個）

| Command | Handler | Reply 出口 |
|---|---|---|
| `S_COMMAND_CHOOSE_ROLE` | `askForAssign()` | `onPlayerAssignRole()` |
| `S_COMMAND_CHOOSE_DIRECTION` | `askForDirection()` | `onPlayerMakeChoice()`（共用） |
| `S_COMMAND_EXCHANGE_CARD` | `askForExchange()` | `onPlayerDiscardCards()` |
| `S_COMMAND_ASK_PEACH` | `askForSinglePeach()` | `onPlayerResponseCard()`（共用） |
| `S_COMMAND_SKILL_GUANXING` | `askForGuanxing()` | `onPlayerReplyGuanxing()` |
| `S_COMMAND_SKILL_GONGXIN` | `askForGongxin()` | `onPlayerReplyGongxin()` |
| `S_COMMAND_SKILL_YIJI` | `askForYiji()` | `onPlayerReplyYiji()` |
| `S_COMMAND_PLAY_CARD` | `activate()` | `onPlayerResponseCard()`（共用） |
| `S_COMMAND_DISCARD_CARD` | `askForDiscard()` | `onPlayerDiscardCards()` |
| `S_COMMAND_CHOOSE_SUIT` | `askForSuit()` | `onPlayerChooseSuit()` |
| `S_COMMAND_CHOOSE_KINGDOM` | `askForKingdom()` | `onPlayerChooseKingdom()` |
| `S_COMMAND_TRIGGER_ORDER` | `askForTriggerOrder()` | `ChooseTriggerOrderBox` |
| `S_COMMAND_NULLIFICATION` | `askForNullification()` | `onPlayerResponseCard()`（共用） |
| `S_COMMAND_SHOW_CARD` | `askForCardShow()` | `onPlayerResponseCard()`（共用） |
| `S_COMMAND_AMAZING_GRACE` | `askForAG()` | `onPlayerChooseAG()` |
| `S_COMMAND_PINDIAN` | `askForPindian()` | `onPlayerResponseCard()`（共用） |
| `S_COMMAND_CHOOSE_CARD` | `askForCardChosen()` | `onPlayerChooseCard()` |
| `S_COMMAND_CHOOSE_ORDER` | `askForOrder()` | `onPlayerChooseOrder()` |
| `S_COMMAND_CHOOSE_ROLE_3V3` | `askForRole3v3()` | `onPlayerChooseRole3v3()` |
| `S_COMMAND_SURRENDER` | `askForSurrender()` | `onPlayerInvokeSkill()`（共用） |
| `S_COMMAND_LUCK_CARD` | `askForLuckCard()` | `onPlayerInvokeSkill()`（共用） |
| `S_COMMAND_ASK_GENERAL` | `askForGeneral3v3()` | `RoomScene`（3v3／1v1 選將） |
| `S_COMMAND_ARRANGE_GENERAL` | `startArrange()` | `RoomScene`（3v3 排將） |
| `S_COMMAND_QML_INTERACT` | `askForQml()` | `replyQml()` |

## 7. 相容橋（點解未遷移嘅嘢唔會爆）

好幾個 reply 出口係共用嘅：`onPlayerResponseCard()` 同時服務出牌階段、無懈可擊、
求桃同 show／pindian；`onPlayerMakeChoice()` 同時服務揀項、花色、勢力同方向
dialog；`onPlayerInvokeSkill()` 同時服務發動技能、投降表決同 luck card。

所以每個出口都行同一條規：

```cpp
if (completeInteraction(<type>, response) == InteractionOutcome::Rejected)
    return;                 // core 拒絕咗 → 唔准送 reply
// Accepted 或者 Passthrough → 照送
```

`completeInteraction()` 喺 core 冇對應嘅 active request 嗰陣回 `Passthrough`，
即係「呢條 reply 路徑今次係為咗一個未遷移嘅 interaction 而行」，行舊路。

另外，`Client::processServerRequest()` 一收到新 request 就取消舊嗰個
（`superseded`）。呢個係正確嘅生命週期規則（server 已經行咗落去），亦順帶保證
未遷移嘅 interaction 唔會撞到殘留嘅 core request。

## 8. 測試

`tests/client_core/client-core-test.cpp`（CTest：`qsanguosha_client_core_contract`，
labels `client-core;fast`）。呢個 test 只 link `Qt6::Core` 同
`qsanguosha_client_core`——佢一旦要拉 GUI 先 link 得到，就代表 ClientCore 已經
漏咗 GUI 依賴入去，所以佢本身就係「ClientCore 唔依賴 GUI」呢條完成標準嘅 gate。
Server-only configure 一樣跑得。

覆蓋：request／response deterministic JSON snapshot、request id 同 correlation、
option／player／card 驗證、cancelable 同 selection 數量、exactly-once 同
duplicate reply、cancel／timeout（注入時鐘）、view 生命週期（request 途中
view 死亡、後接 view、無 view）、view callback 契約（每個 request 恰好 present
一次、恰好收檔一次、reject 唔算收檔）、呈現途中重入答題、`ClientGameState`。

Desktop adapter 嘅真正呈現由現有 GUI runner 驗：

```bash
python3 tools/autotest/skill_ui_runner.py --exe relwithdebinfo/QSanguosha
python3 tools/autotest/gui_network_smoke.py --exe-root . --mode 02p \
    --seed 20260828 --artifact-dir art --no-xvfb --platform xcb
```

## 9. 非目標

Text client、Protocol V2、WebSocket、Android、WASM、其餘 24 個 interaction、
RoomScene 重寫、UI 重新設計。
