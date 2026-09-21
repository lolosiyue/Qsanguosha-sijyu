# 50 人 Attention UI：協議決策拆分與靜態審計

> 2026-09-19：新增 [M1 實作檢查點](process/large-room-ui-implementation.md)。以下保留 09-16 審計基線；首批生命週期協議、桌面大局布局與焦點操作已通過建置及 focused 契約檢查。後續模式定義為 1 主／23 忠／25 反／1 內，已補建 `50p`，第二檢查點建置、模式登錄與身份分配檢查通過；下列「不新增開房模式」是首批歷史範圍，不再代表後續工作範圍。

審計基線：`debug@1ac792c` 加 2026-09-16 審閱時的未提交變更。
審計範圍為原始碼；建置、GUI 與完整對局結果見末節驗收表。
工作樹在審閱期間仍有更新，因此以下以函式與欄位為證據錨點，行號僅供定位。

## 1. 結論：四項資料契約，不能用一個「焦點」代替

| 問題 | 資料契約 | 目前來源 | 協議決策 |
| --- | --- | --- | --- |
| 現在等誰回答？ | 回應焦點（Response Focus） | `MOVE_FOCUS` 的玩家名單、命令與倒數；本人的 typed interaction | 沿用；不把它改成結算來源／目標 |
| 最近誰影響了誰？ | 近期事件關係（Recent Relations） | `LOG_SKILL` 的 `log_type/from_player/to_players/card_string` | 沿用戰報協議，補具名投影；這部分已見未提交實作 |
| 現在哪一層事件仍在結算？ | 活動結算上下文（Active Resolution Context） | 伺服器控制流程有上下文，但沒有對應的完整客戶端生命週期契約 | 需要補結算 ID、父子關係、開始／更新／結束，以及重連快照 |
| 回合沿座次哪個方向前進？ | 回合方向（Play Order Direction） | `RoomRoster`；本批新增 `ARRANGE_SEATS` schema 2 的 `play_order_reversed` | 沿用新增方向欄位；與結算來源→目標分開 |

**「不補協議、只補投影」只適用於第二列；不適用於第三列。**

「出牌方向」容易同時被理解為「A 對 B 出牌」與「回合順／逆序」。文件及介面應改用表內名稱，避免把兩種方向混為一談。
當前回合玩家 `currentPlayer` 另作背景狀態，不能自動取代回應者或結算主角。

### 已確認的產品邊界

- 本次目標包含完整結算生命週期，不只是顯示最後一條戰報。
- 大局呈現只落地桌面 Qt；共用協議的其他消費端仍需正確接受與保存資料。
- 本次不新增 `50p` 開房模式，不決定身份配比、牌堆、將池或勝利條件。
- 伺服器與客戶端同步更新，沿用現有 Protocol V2 政策，不新增舊版混接能力協商。
- 支援舊錄影的 schema 1，不等於舊版二進位客戶端能接受新的 schema／命令。

## 2. 最新實作盤點

| 項目 | 本次實際看到的來源 | 結論 |
| --- | --- | --- |
| 回應焦點 | `Room::notifyMoveFocus` 廣播玩家名單、命令、倒數；reducer 保存 `focus/focus_countdown` | 已有，但不是結算鏈 |
| 近期關係投影 | `GameEventStream::synchronize` 已保留 payload；`GameViewState::fromState` 對 `LOG_SKILL` 建立 `recentRelations`；`toJson` 輸出 `recent_relations` | 上輪「投影丟失兩次」已是修正前狀態，不應再當最新缺陷 |
| 回合方向 | `ArrangeSeatsMessage` schema 2；開局／換座、反轉、中途加入、marshal、控制上下文座次通知均有相關發送接點；reducer 與 `GameViewState` 已保存／輸出方向 | 已見來源實作，尚未執行驗證 |
| 活動結算 | 共用呈現模型尚無活動堆疊；近期關係沒有父結算 ID 或結束標記 | 未完成 |
| 布局安全 | `RoomLayoutEngine` 經典與自適應入口都限制其他玩家數不超過 19 | 已防止座位表越界；仍沒有 21–50 人有效布局 |
| 詳細檢視與選取 | 已有 `RoomOverlayHost`、Inspector、多票 intent；Photo 以透明度保留原選取草稿 | 可以沿用，不必把全面重寫選取模型當作前置條件 |

核心來源：

- [Room::notifyMoveFocus／broadcastSeatRing／reversePlayOrder](../src/server/room.cpp)
- [ClientGameStateReducer：MOVE_FOCUS、ARRANGE_SEATS](../src/client/core/client-game-state-reducer.cpp)
- [GameEventStream::synchronize](../src/client/core/game-event-stream.cpp)
- [GameViewState::fromState／toJson](../src/client/core/game-view-state.cpp)
- [ArrangeSeatsMessage](../src/core/protocol/arrange-seats-message.h)
- [PlayerLifecycleService::marshal／insertPlayerMidGame](../src/server/player-lifecycle-service.cpp)
- [RequestCoordinator::notifyArrangeSeats](../src/server/request-coordinator.cpp)

## 3. 為甚麼戰報關係不等於活動結算

### 3.1 無懈戰報的 from，不一定是打出無懈的人

[standard-cards.cpp](../src/package/standard-cards.cpp) 的 `#NullificationDetails` 發送點（約 978–992 行）：

| 值 | 實際來源 | 不能直接推論成 |
| --- | --- | --- |
| 打出無懈者 | 當次流程的 `source` | 戰報 `from_player` |
| `log.from` | `effect.from` | 當前回應者 |
| `log.to` | `effect.to` | 無懈鏈所有相關玩家 |
| `log.card_str` | `effect.card->toString()` | 唯一結算 ID |

例：A 的錦囊影響 B，C 打出無懈。這條 details 戰報描述的是被無懈的效果，不能一律翻譯成「A 正在對 B 使用無懈」。
不同 `log_type` 的欄位必須按各自語義呈現，不能把所有 `from/to` 都當成當下行動者／受影響者。

`card_string` 可以幫助顯示牌名或辨識相關內容，但沒有每次結算唯一性與生命週期保證；同一張牌再次使用、多目標效果、巢狀無懈都不能只靠它配對。
既有 [CardProvenanceMessage](../src/core/protocol/card-provenance-message.h) 記錄牌與技能來源，也沒有補齊逐次結算的 parent／end，不能直接替代。

### 3.2 有關係，仍然不知道事件是否結束

| 流程 | 只取最新關係可能顯示甚麼 | 正確活動狀態 |
| --- | --- | --- |
| A 對 B 用牌，等待 B 回應 | 最近一條出牌／戰報關係 | A 的出牌下，B 的效果／回應仍在進行 |
| C 無懈，D 反無懈 | 最近一條無懈 details | 明確知道目前子層以及它影響的父層 |
| D 的子事件結束 | 最後一條關係仍是 D 附近的事件 | 返回尚未結束的父層 |
| 傷害被防止或回合中斷 | 最近傷害／用牌仍停留 | 關閉對應活動層，不再標成正在結算 |
| 全鏈結束 | 最後一條戰報不會自行消失 | 活動上下文為空；歷史仍可保留 |

因此可保留「最近事件」卡片，但必須明示為歷史，不能沿用「正在結算」標籤。

### 3.3 重連缺的是當下狀態，不只是歷史回顧

[PlayerLifecycleService::marshal](../src/server/player-lifecycle-service.cpp) 目前以 `STATE_SYNC begin/end` 包住玩家、座次、牌區、技能與控制上下文同步；未看到活動結算堆疊的發送。
`presentationEvents` 是裁剪為 200 筆的近期事件窗口，不是活動上下文快照。

反例：A→B 的效果正在等待 C 的無懈回應，此時客戶端重連。即使之後收到新回應提示，也不能只憑重連後的戰報確定父效果、目前子層或返回位置。
因此原決策文件「重連前事件拿不到，對現在是誰的事件仍可接受」不符合本次完整結算鏈的完成條件。

## 4. 拆分後的實作決策

### D1：回應焦點沿用現有協議

- `MOVE_FOCUS` 繼續回答「正在等誰」；本人操作的合法選項、提交、期限仍由既有 interaction／ClientCore 負責。
- 名單可能有多人，空名單在 GUI adapter 還可能擴成全部存活者；不能拿名單第一人當唯一結算主角。
- 不把 `source/target` 硬塞進 `MOVE_FOCUS` 來取代整個生命週期；也不以新呈現事件改動回覆編碼或超時規則。

### D2：近期關係沿用戰報，保留本批投影

- 保留 `recentEvents` 純文字與 `recentRelations` 具名欄位；UI 依 `log_type` 解釋來源／目標。
- 不把「最後一條關係」直接設定為活動結算；未知 log 類型顯示通用歷史描述。
- `GameEventStream` 現在帶完整收件者 payload；這不等於可把原始 payload 整包輸出到任意公開快照。保留 `GameViewState` 的欄位白名單，新增欄位仍核對原本資訊可見性。
- 需要完整上下文時使用 D3 的活動狀態，不解析翻譯後戰報、不猜測 `card_string` 的父子關係。

### D3：新增活動結算生命週期契約

建議沿用先前規劃的獨立型別化通知 `S_COMMAND_RESOLUTION_STATE`。此名稱是待實作設計，不是目前已存在的命令。
原因是這份資料具有狀態生命週期，與戰報敘述、回應名單各有不同責任。

| 最小語義 | 要解決的具體問題 |
| --- | --- |
| 每次結算的穩定 ID、父 ID | 同牌重用、多目標、巢狀回應不能靠文字配對 |
| 事件種類、行動者、來源、目標、目前受影響者 | 無懈者與被無懈效果的來源可能不同 |
| `begin/update/end/reset` | 子層返回、取消、目標轉移與清空不再靠時間猜測 |
| 接收者可見的牌／技能描述 | 只披露原本允許看見的資訊，不公開伺服器內部資料 |
| 與現有待回應請求的關聯 | 顯示正在等誰，但回覆仍走既有 interaction |

- 伺服器在實際執行的出牌、逐目標效果、技能、判定、傷害／回復、瀕死與詢問流程維護上下文；不能將每次技能候選掃描都公布成一個事件。
- 使用作用域守衛收束正常返回與例外返回；防止效果、取消、超時、`TurnBroken/StageChange/GameFinished` 都須關閉對應層，且不改規則執行順序。
- 客戶端將活動狀態存入 `ClientGameState`，由 `GameViewState` 投影給 UI；不要靠可能被裁剪的 200 筆歷史重建它。
- 重連在既有 staging 同步中傳入收件者可見的活動上下文，`STATE_SYNC end` 才原子發布。資訊不足時明示「結算資訊未同步」，不顯示猜測關係。
- Replay 記錄生命週期；seek 必須先重置活動上下文再重放。現有 `Replayer::seekToPosition` 從 0 重放，不能據此假設新增的 push/pop 狀態可在未清空時重複套用。
- 舊 Replay 缺少生命週期時仍可提供舊有回合／回應／歷史資訊，但標示完整結算資訊不可用。
- ID 使用現有協議的十進位字串慣例；不輸出原始 C++ 指標，不改 Room／Lua／SWIG 既有公開呼叫語義。
- 新通知必須同步更新 registry、reducer、各協議消費端及覆蓋矩陣；各客戶端均須處理新通知。

### D4：回合方向沿用 ARRANGE_SEATS 擴充

- 保留本批 `play_order_reversed` 狀態與既有方向戰報；前者重建狀態，後者供人閱讀。
- 開局、換座、反轉、中途加入、重連與控制上下文切換，方向都應以絕對值重申，不能只在反轉時送一次。
- 座次環順序與回合行進方向分開：反轉不應重新按勢力／存活或焦點排列總覽。
- schema 1 沒有權威方向欄位，目前 reducer 使用 `false`。這是相容預設，不能宣稱可精確還原所有含反轉的舊錄影；UI 的方向可信度需明確處理。
- schema 2 應有方向欄位的缺失／錯型別驗收。registry 將它列為 optional，接收端尚須驗證缺失欄位的行為。

## 5. 與 50 人介面的接合及審計發現

| 優先級 | 發現 | 具體接合要求 |
| --- | --- | --- |
| P1 | `RoomLayoutEngine::selectSeatRegions` 與 `computeResponsive` 均拒絕其他玩家數 >19 | 總人數 ≥21 進入大局布局，不能僅移除表索引保護 |
| P1 | `applyResponsiveLayout` 無效時返回，原 Dashboard 可能已因預覽切換設為透明 | 大局／無效幾何需有完整有效輸出或可見回退，不可留下透明操作區 |
| P1 | 近期關係不能支撐完整活動結算 | Current Resolution Panel 讀 D3；D1 顯示等待回應，D2 作近期歷史 |
| P2 | `RoomScene::moveFocus` 仍只改 Photo 倒數與回應框 | Photo 透明時，縮略位／焦點面板也要顯示相同回應狀態 |
| P2 | 現有 Ribbon 以長文字按鈕巡覽，沒有完整大局總覽與結算雙角色面板 | 沿用 Overlay 的 intent／Inspector，新增固定環序 Mini 與結算面板 |

Photo 使用 `setOpacity(0)` 保留原物件與草稿；實際可選取性須由互動案例確認。
多票已經透過 `selectedVotes/maxVotes` 與加減票 intent 接到原容器。新 Mini 可以先沿用這條橋接；若之後改成真正隱藏或刪除 Photo，才需先解決選取權威對圖元的依賴。

UI 三種狀態不得相互覆蓋：**結算焦點**跟隨 D3、**檢視焦點**允許使用者鎖定玩家、**輸入焦點**保持正在操作的控制項。
自動更新不應搶走正在指定的目標或鍵盤焦點。

來源：[布局引擎](../src/ui/room-layout-engine.cpp)、[RoomScene](../src/ui/roomscene.cpp)、[RoomOverlayHost](../src/ui/room-overlay-host.cpp)、[桌面意圖轉接](../src/ui/desktop-game-presentation.cpp)。

## 6. 驗收清單與證據

| 區域 | 必要案例 |
| --- | --- |
| 回應／歷史語義 | 多人等待、空名單、無來源傷害；無懈者與被無懈效果來源不同；未知 log 類型不被誤標為正在結算 |
| 生命週期 | 殺→閃、多層無懈、多目標逐人結算、傷害→瀕死→求桃、連鎖／轉移、同牌再用；子層結束回到父層 |
| 中斷 | 防止、取消、超時、回合中斷與遊戲結束；堆疊沒有殘留 |
| 重連／Replay | 等待回應中重連、深層巢狀重連、向前／向後 seek、同點重複 seek、跨對局 reset、舊錄影缺少新資料 |
| 方向 | 正序→反序→正序、反序中 marshal／換座／加入／控制切換、schema 1 預設、schema 2 缺失與錯型別 |
| 隱私 | 私人詢問、暗將／技能、未知牌與私人牌堆不得因通知或原始事件輸出擴大可見性 |
| 大局 | 20／21 邊界及 30／50 人；死亡不縮短環序；離屏合法目標、多票、取消、選取中焦點改變；無效布局仍有可見操作入口 |

目前已有新增測試來源：[game-presentation-test.cpp](../tests/client_core/game-presentation-test.cpp)、[protocol-flow-inventory-test.cpp](../tests/protocol/protocol-flow-inventory-test.cpp)、[room-notifier-test.cpp](../tests/room-notifier-test.cpp)，涉及事件投影及方向。
上述案例的執行狀態列於下表。
[room-layout-engine-test.cpp](../tests/room-layout-engine-test.cpp) 目前對 20 個其他玩家仍斷言無效，是安全拒絕測試，不是 50 人布局驗收。

| Gate | 狀態 |
| --- | --- |
| 原始碼與文件靜態審計 | 完成；區分既有來源、未提交實作與待設計契約 |
| 建置／focused executable／本地 CTest | NOT RUN |
| GUI／50 人資料場景／完整對局／遠端 CI | NOT RUN |

後續驗證項目為受影響目標建置、focused 契約及表列 GUI／完整對局案例。
50 人資料場景通過只證明 UI 與資料契約，不能宣告完整 50 人玩法或對局已驗收。

## 7. 對原協議決策文件的修訂結論

| 原說法 | 修訂後 |
| --- | --- |
| 來源→目標已有，因此不用補協議 | 近期戰報關係不必新增重複欄位；活動結算仍需生命週期契約 |
| 戰報流帶完整關係 | 部分事件有結構化關係，但欄位語義依 log 類型而異，未包含通用行動者與活動層 |
| 重連前事件缺失對「當下」可接受 | 完整結算目標下不可接受；活動上下文應由狀態同步提供 |
| 兩層投影丟資料、方向只在伺服器 | 是本批修正前狀態；最新工作樹已有投影與方向傳送來源 |
| 既有 seek 重複走安排座次，證明 GUI 安全 | 只能證明有既有呼叫路徑；不能替代反轉、重連、選取及新生命週期的執行驗收 |

第一輪調查文件 `focus-relation-protocol-decision.md` 已於 2026-09-20 刪除（原文見 git 歷史）；後續結算需求與實作分析見本文。
