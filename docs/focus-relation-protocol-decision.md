# 焦點與結算關係：協議決策

[`ui-roadmap.md`](ui-roadmap.md) §2.7 把這件事標為「M1 動工前必須先有結論」。本文保留第一輪協議調查，以下現況描述包含本批修改前的基線。

> **2026-09-16 審計修訂**：完整決策以 [50 人 UI 協議審計](large-room-ui-protocol-audit.md) 為準。
> 「只補投影」適用於**近期戰報關係**，不涵蓋**活動結算的開始／父子關係／結束／重連快照**。
> 最新工作樹已見事件關係投影及回合方向傳送的未提交實作；本文「丟失 payload」「方向僅在伺服器」等敘述應視為修正前狀態。
> 本文 §1.4 的重連可接受判斷、§4.2 的 GUI 已證明安全判斷，不作為完整結算鏈的完成或驗收結論。

## 摘要

「來源→目標」與「出牌方向」看起來是同一個協議決策，查下來不是：

| | 協議現況 | 缺口在哪 | 建議 |
|---|---|---|---|
| 來源→目標 | **已經是結構化欄位** | 客戶端投影層丟了兩次 | 不動協議，補投影 |
| 出牌方向 | 只有一行戰報 | 重連後無法重建 | 補協議 |

§2.7 的前提句「『來源→目標』的結算關係不是協議上的一等公民」在 `MOVE_FOCUS` 上成立，
在戰報流上不成立——本文第 1 節是證據。

---

## 1. 來源→目標：協議已經有了

### 1.1 `MOVE_FOCUS` 確實只有三個欄位

`Room::notifyMoveFocus()`（[`src/server/room.cpp`](../src/server/room.cpp) 約 1229–1238 行）
只送 `player_names`、`command`、`countdown`，客戶端 `Client::moveFocus()` 與 reducer 也只讀這三項，
存成 `focus` 與 `focus_countdown` 兩個 game value。§2.7 對這一點的描述正確。

### 1.2 但戰報流帶著完整的關係

`S_COMMAND_LOG_SKILL` 的 `SkillLogPayload` 註冊欄位是
（[`protocol-payload-registry.cpp:1367`](../src/core/protocol/protocol-payload-registry.cpp#L1367)）：

```
log_type / from_player / to_players / card_string / arguments
```

而且 §2.7 舉的兩個例子都確實有填：

- **傷害** —— [`gamerule.cpp`](../src/server/gamerule.cpp) 約 1031–1046 行：
  `log.from = damage.from`、`log.to << damage.to`、`arg` 是傷害值、`arg2` 是屬性。
- **他人的無懈** —— [`standard-cards.cpp`](../src/package/standard-cards.cpp) 約 985–989 行：
  `log.from = effect.from`、`log.to << effect.to`、`card_str` 是被無懈的那張牌。

`LogMessage` 本身（[`roomthread.h:11`](../src/server/roomthread.h#L11)）就是
`type / from / to / card_str / arg…arg5`，`from` 與 `to` 是它的固有結構，不是附帶資訊。

### 1.3 資料在客戶端被丟了兩次

`ClientGameState::appendPresentationEvent()`
（[`client-game-state.cpp:203`](../src/client/core/client-game-state.cpp#L203)）**完整保留** payload。
往下兩層各丟一次：

1. [`game-event-stream.cpp:41-43`](../src/client/core/game-event-stream.cpp#L41-L43) ——
   `GamePresentationEvent` 有 `payload` 欄位，但 `synchronize()` 建構時硬填 `QVariant()`。
2. [`game-view-state.cpp`](../src/client/core/game-view-state.cpp) 的 `recentEvents` ——
   只留 `command` 與 `text`。

所以缺口在兩個投影層，不在協議。這跟批次 1 的期限（`deadlineMs` 已在模型裡、只是每個殼各自重算）
是同一個形狀的問題。

### 1.4 補了協議也解決不了的三件事

這三條限制不是「MOVE_FOCUS 缺欄位」造成的，補 `MOVE_FOCUS` 一條都不會消失：

- **戰報是敘述流，不是結算鏈。** 相鄰的 log 之間沒有父子關係。要回答「這次無懈是為了擋哪一次錦囊」，
  仍然得靠 `card_string` 之類的欄位去比對，不是靠多一個 `target` 欄位。
- **`log_type` 是字串。** 客戶端要判讀 `#Damage`、`#NullificationDetails` 這些字串常數，
  是一種弱型別耦合。這是既有設計，本文不提議改它。
- **重連前的事件拿不到。** `presentationEvents` 上限 200 筆且不參與 marshal。
  焦點面板只能從連上之後開始建立——這對 §2.7 的「現在是誰的事件」是可接受的（那是當下狀態），
  對「回顧整條結算鏈」則不是。

### 1.5 建議：不動協議

1. [`game-event-stream.cpp:43`](../src/client/core/game-event-stream.cpp#L43) 改成帶 payload（欄位已存在）。
2. `GameViewState` 增加結構化的最近事件投影，保留 `from_player` / `to_players`，
   與現有的純文字 `recentEvents` 並存（純文字那份是戰報用的，不該被取代）。
3. 在 `game-presentation-test.cpp` 加斷言：`#Damage` 與 `#NullificationDetails` 的
   from/to 必須抵達共用模型。

成本是兩處單行加一個投影加一組測試。

---

## 2. 出牌方向：協議真的缺

### 2.1 現況

反轉狀態只活在伺服器：`RoomRoster::m_playOrderReversed`
（[`room-roster.h:52`](../src/server/room-roster.h#L52)）。

對客戶端唯一的訊號是 [`room.cpp:4406`](../src/server/room.cpp#L4406) 的一行戰報，
`log.type` 是 `#ReversePlayOrder` 或 `#RestorePlayOrder`。

而且這行戰報連「狀態改變時必定送出」都不成立：`Room::reversePlayOrder()`
（[`room.cpp:4399`](../src/server/room.cpp#L4399)）先呼叫 `m_roster->reversePlayOrder()`
翻轉狀態，**才**檢查 `if (count < 2) return;`。少於兩人的房間狀態變了卻沒有任何訊號。
這是邊緣案例，但它說明戰報本來就不是為了承載狀態而設計的。

座次清單不含方向：`RoomRoster::relinkPlayers()`
（[`room-roster.cpp`](../src/server/room-roster.cpp) 約 284 行）反轉的是每個玩家的 `next` 指標，
`m_players` 列表本身不動；而 `S_COMMAND_ARRANGE_SEATS` 送的正是
`m_roster.players()` 的原始順序（[`player-lifecycle-service.cpp`](../src/server/player-lifecycle-service.cpp) 約 690–696 行），
客戶端 reducer 也只拿它來設 `seat = i + 1`。

`marshal()` 的重連流程沒有任何一步帶方向。

### 2.2 後果

- 對局中一直連著的客戶端：可以監看那兩個 `log_type` 自己維護一個 bool。
- **重連、觀戰、中途加入：拿不到，只能猜。**
- 每個殼都得自己寫一份字串比對——正是 §13 風險表「各殼分頭演化」那一條。

§2.1 要求「上家與下家的方位可讀，且與 `CHOOSE_DIRECTION` 決定的順逆時針一致」。
目前三個殼沒有一個做得到。

### 2.3 建議：擴充 `ArrangeSeatsPayload`

`ArrangeSeatsPayload` 目前註冊欄位只有 `player_names`
（[`protocol-payload-registry.cpp:1355`](../src/core/protocol/protocol-payload-registry.cpp#L1355)）。
伺服器送的雖然是裸 JSON 陣列，但房間邊界的 `scalarPayload(value, "player_names", output)`
已經把它正規化成具名物件，所以**加欄位是 schema 升版，不是形狀改變**。

建議加 `play_order_reversed`（bool），`schema_version` 由 1 升 2，
照 `SignupRequestPayload` 的先例保留 schema 1 的接受路徑
（見 [`protocol-v2.md`](protocol-v2.md) §Framing and errors）。

**為什麼是 `ARRANGE_SEATS` 而不是新命令：**

- §2.1 本來就把座次號與方向當成同一件事說明；座次環的定義不完整到缺了方向就沒法用。
- 重連路徑 `marshal()` 已經在送 `ARRANGE_SEATS`，方向搭車即可；
  新增獨立命令則需要在每條同步路徑上都記得送它，是新的漏送面。
- 反轉發生時 `Room::reversePlayOrder()` 已經是一個集中點，再廣播一次 `ARRANGE_SEATS` 成本極低。

**同時保留** 現有的 `#ReversePlayOrder` / `#RestorePlayOrder` 戰報。
那是給人看的敘述（§2.4 的「戰報」流），不該被狀態欄位取代——這是兩種訊息流，不是重複。

---

## 3. 被否決的方案

**A. 兩件事都靠客戶端合成。** 方向那一半在重連後必然是錯的，而且錯得無聲。否決。

**B. 兩件事都補協議，`MOVE_FOCUS` 加 `source` / `target`。**
付出協議改動的成本，但 §1.4 的三條限制一條都沒解決；
更糟的是會讓「結算關係」有兩個來源（`MOVE_FOCUS` 與戰報），兩者矛盾時以誰為準沒有答案，
而它們必然會矛盾——`MOVE_FOCUS` 是「誰正在被詢問」，戰報是「發生了什麼」，本來就不是同一件事。
不建議。

---

## 4. 三項查證的結果

### 4.1 Replay 時間軸：吃得到，但有一個硬性前提

`Replayer::seekToPosition()`（[`recorder.cpp:834`](../src/util/recorder.cpp#L834)）的 seek 就是
**從 0 重放到目標 index**：

```cpp
for (int i = 0; i <= pairIndex; i++)
    emitCommand(i);
```

沒有任何 client 端 reset——`jumpToNode()` 與 `jumpToElapsed()` 都收斂到這一條。
所以「任意時間點的狀態」是靠從頭 fold 一次得到的，只要每個 handler 是絕對賦值就會收斂；
reducer 的 `setGameValue` 正是絕對賦值。

`S_COMMAND_ARRANGE_SEATS` 的 replay policy 是 `Record`
（`addRoomNotification` 的預設值，[`protocol-payload-registry.cpp:1063`](../src/core/protocol/protocol-payload-registry.cpp#L1063)
與 [`:1157`](../src/core/protocol/protocol-payload-registry.cpp#L1157)），所以它確實在 replay 檔裡；
[`record-analysis.cpp:75`](../src/core/record-analysis.cpp#L75) 也已經在從錄影裡讀它。

**前提：這個欄位必須出現在每一次 `ARRANGE_SEATS`，包含開局那一次。**
往回 seek 時，重放 0..N 不會「取消」N 之後設過的值，只會覆蓋 0..N 裡有設到的 key。
開局那一次（`Room::adjustSeats()`，[`room.cpp:2325`](../src/server/room.cpp#L2325)，
所有模式都會走）帶著 `false`，往回 seek 才會自動修正回去；
若只在反轉時才附這個欄位，往回 seek 會留下 stale `true`。
換句話說 §2.3 的「反轉時重播一次」是必要條件，不是最佳化。

### 4.2 舊客戶端：結論不變，但編碼路徑有一個順序陷阱

`ProtocolPayloadRegistry::encodeObjectPayload()`
（[`protocol-payload-registry.cpp:1505`](../src/core/protocol/protocol-payload-registry.cpp#L1505)）
有一條 pass-through：payload 已經是 `QVariantMap` 且 `schema_version` 通過
`schemaVersionAllowed()` 就原樣送出；否則落到 `encodeRoomNotificationPayload()` →
`scalarPayload(value, "player_names", output)`。

而 `scalarPayload`（[`:700`](../src/core/protocol/protocol-payload-registry.cpp#L700)）是**無條件包裝**，
它不檢查傳進來的是不是已經成形的物件。兩個後果：

1. **可以逐點遷移。** 現有五個呼叫點送的是裸陣列（`JsonUtils::toJsonArray`），
   會繼續走 scalarPayload 蓋 schema 1，不用一次改完。
2. **順序是硬性的。** 必須先讓 `schemaVersionAllowed("ArrangeSeatsPayload", 2)` 回 true
   （[`:536-542`](../src/core/protocol/protocol-payload-registry.cpp#L536-L542)，
   `SignupRequestPayload` 的先例用 struct 常數，這裡沒有 struct 只能寫字面量 `2`），
   **才**可以有任何送 schema 2 物件的呼叫點。反過來的話那個物件會被 scalarPayload
   二次包裝成 `player_names: { player_names: […] }`，validate 失敗，
   [`serverplayer.cpp:566-572`](../src/server/serverplayer.cpp#L566-L572) 直接
   `disconnectSocketFromOwnerThread()`。這不是優雅降級，是斷線。

`ArrangeSeatsPayload` 沒有 C++ struct（只存在於 registry 的欄位表與 descriptor 名字），
走 `validateGenericSchema` 的欄位型別表，所以 `play_order_reversed` 要在
[`:486-493`](../src/core/protocol/protocol-payload-registry.cpp#L486-L493) 那張表登記成 `Boolean`。

客戶端落點是 [`client-game-state-reducer.cpp:500`](../src/client/core/client-game-state-reducer.cpp#L500)
加一行 `setGameValue`。

**GUI 殼的既有行為已經證明安全**：seek 每次都會重新 dispatch index 0 的 `ARRANGE_SEATS`，
`Client::arrangeSeats()` → `RoomScene::arrangeSeats()` 這條路本來就在對局中段被重複走過。
反轉時的重播因為 `player_names` 順序不變，photo 交換迴圈是 no-op，只多一次 `updateTable()`。

### 4.3 1v3 / 3v3：沒有專用座次表，不受影響

1v1 / 3v3 / XMode 走的是同一條 `Room::adjustSeats()`
（[`roomthread1v1.cpp:89`](../src/server/roomthread1v1.cpp#L89)、
[`roomthread3v3.cpp:72`](../src/server/roomthread3v3.cpp#L72)、
[`roomthreadxmode.cpp:26`](../src/server/roomthreadxmode.cpp#L26)），
換位是 `Room::swapSeat()`（[`room.cpp:2310`](../src/server/room.cpp#L2310)），兩者都廣播同一個
`ARRANGE_SEATS`。五個送出點都可以直接讀 `m_roster.isPlayOrderReversed()`，沒有模式分支。

順帶兩個發現，本文不提議修：

- `RoomRoster::swapSeats()`（[`room-roster.cpp:223`](../src/server/room-roster.cpp#L223)）
  結尾呼叫 `relinkPlayers()`，方向會被保留；
  `RoomRoster::adjustSeats()`（[`:241`](../src/server/room-roster.cpp#L241)）沒有呼叫。
  它只在開局跑，當時方向必為 `false`，所以今天無害。
- `Room::reversePlayOrder()` 在庫內唯一的呼叫點是 cheat
  （[`room.cpp:4907`](../src/server/room.cpp#L4907) 的 `reverse_play_order`），
  其餘經 SWIG 暴露給 Lua（[`swig/sanguosha.i:2450`](../swig/sanguosha.i#L2450)）。
  所以「3v3 中反轉」今天不是活的情境——這降低了 4.3 的風險，但不改變欄位的形狀。
