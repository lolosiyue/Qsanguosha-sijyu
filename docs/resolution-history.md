# 結算歷史（Resolution History）

本文是 QSanguosha server 規則腳本可使用的結算歷史規格。內容以目前 `src/core/resolution-history.h/.cpp`、`src/server/room.h/.cpp`、`src/server/card-movement-service.cpp` 與 `swig/resolution-history.i` 的已落地介面為準；若 core 的 storage 或 query validation 後續調整，應先同步更新本文的 API 表與例子。

## 用途與邊界

Room 持有一份 server authoritative 的 `ResolutionHistoryService`。它只供 rules Lua 讀取，保存可重播、可查詢的 primitive value tree；presentation、isolated AI 與 private request body 不會經由這個介面取得。

歷史不是目前場面（current state）的替代品。`room:getCardPlace(card_id)` 只回答現在的位置；要知道某張牌曾由哪裡移到哪裡，必須查 `move` fact。反過來，歷史也不保證取代當前狀態：技能在查到候選牌後，仍要在真正 `obtainCard`／移牌前重新檢查 `room:getCardPlace` 與 owner。這是避免查詢與實際取牌之間牌已被其他結算移走的必要競態防護。

歷史未知（`complete == false` 或 `attribution_complete == false`）不等於沒有符合項目。未知只能表示資料不完整或歸因不足；規則不能把空結果解讀為「沒有發生」。

## 三種資料：events、facts、active stack

| 類型 | 意義 | 典型 kind | 是否可更新 |
| --- | --- | --- | --- |
| event | 一段有生命週期的結算範圍，具有 parent、scope、status、outcome | `round`、`turn`、`phase`、`skill`、`use_card`、`respond_card`、`damage`、`move_cards`、`extra_turn` | 可用 `updateEvent` 補充 data，最後由 `finishEvent` 結束 |
| fact | 已發生且不可變的觀察，掛在一個 event 下，以全域 `sequence` 排序 | `move`、`use_card`、`actual_damage`、`damage_component` | 不可修改或刪除 |
| active stack | 目前尚未 finish 的 event id 堆疊；新 event 的 parent 是 stack 頂端 | 例如 round → turn → phase → skill | 由 begin/finish 自動維護 |

`parent_id` 只代表直接父事件。`historyParent(id, kind, includeSelf)` 會沿 parent chain 向上找第一個符合 kind 的祖先；查詢範圍是明確的祖先鏈，沒有 descendant semantics。給定一個 skill event，不會因為某個 child event 或 sibling event 的資料相同而匹配它們。

`round` 是 scope anchor，不應假設 active stack 永遠是 `round → turn → phase`。首個 turn 內可能建立 round，round 的 parent 可能是 turn；技能應以 `historyScopes()` 回傳的 `round_id`／`turn_id`／`phase_id` 作顯式 scope，不要從 parent chain 推測結構。

事件的輸出 map 欄位為：`id`、`parent_id`、`kind`、`round_id`、`turn_id`、`phase_id`、`status`、`outcome`、`data`。fact 的輸出 map 欄位為：`id`、`sequence`、`event_id`、`kind`、`round_id`、`turn_id`、`phase_id`、`data`。id 與 sequence 透過 SWIG 以 Lua integer 或 decimal string 讀入；輸出目前是字串化的 id。

## Room API

| API | 回傳／用途 |
| --- | --- |
| `room:currentHistoryEventId()` | active stack 頂端 event id；Lua 收到 decimal string，沒有 active event 時為 `"0"` |
| `room:historyEvent(id)` | 單一 event map；不存在時為空 table |
| `room:historyParent(id, kind, includeSelf)` | 祖先鏈上第一個指定 kind；找不到時為空 table |
| `room:historyScopes()` | `{round_id, turn_id, phase_id}`，目前 scope id |
| `room:queryHistoryEvents(filter)` | 分頁 event 查詢 |
| `room:queryHistoryFacts(filter)` | 分頁 fact 查詢 |
| `room:queryHistoryMoves(filter)` | 等同 facts query，但強制 `kind = "move"` |
| `room:queryActualDamage(filter)` | 查 `actual_damage`，再以同 event 的 immutable `damage_component` 聚合 armor/hp |

`queryHistoryEvents` 的結果 map 具有 `events` 與相同內容的 `items`、`has_more`、`attribution_complete`、`complete`、`watermark`、`next_after`。`queryHistoryFacts`／`queryHistoryMoves`／`queryActualDamage` 對應的列表鍵是 `facts` 與 `items`，其餘分頁鍵相同。`watermark` 是本次查詢固定的上界；`next_after` 是最後一筆的 event `id` 或 fact `sequence`。`limit` 必須是正整數；`limit <= 0`、未知 key 或錯型別會回傳 `error = "invalid_query"`，並以空 `items`／alias 和 `complete = false` 表示查詢無效。預設上限為 100。

可用 filter key（C++ query 與 SWIG Lua 白名單）只有：

`kind`、`round_id`、`turn_id`、`phase_id`、`player`、`from`、`to`、`skill_name`、`skill_owner`、`after`、`limit`、`watermark`；`event_id` 也已由 SWIG Lua wrapper 開放。

未知 key、巢狀 filter、table value、function、userdata 都不應送入 Lua wrapper；wrapper 只接受空陣列的 flat scalar map，且數值須為 finite。

## 範圍、分頁與一致性

### 技能中的最短用法：本回合是否造成過傷害

在既有技能回呼取得 `room` 和 `player` 後即可查詢，不必由此技能預先監聽或設置 mark：

```lua
local function dealtDamageThisTurn(room, player)
  local turn = room:historyScopes().turn_id
  if turn == "0" then return false end -- 目前沒有回合範圍
  local page = room:queryActualDamage {
    turn_id = turn,
    from = player:objectName(),
    limit = 1, -- 只問是否存在；不需要讀完全部頁面
  }
  if page.error or not page.complete or not page.attribution_complete then
    return nil -- 未知，不是「沒有造成傷害」
  end
  return #page.items > 0
end
```

`turn_id` 指現在正在結算的回合，不代表該角色最近一次自己的回合；插入的額外回合有不同 ID。回合中途取得技能也能查到取得之前的記錄。若要列出全部事件或加總傷害，必須使用下面的分頁模式；`limit = 1` 僅適合存在性查詢。`queryActualDamage` 包含已扣護甲，若技能只計算 HP 損失，須讀取各筆的 `data.hp_loss` 並另行篩選。

以 `watermark = room:currentHistoryEventId()` 作為 facts 查詢的 watermark 並不正確：fact 使用獨立的 `sequence`。正確做法是先讀結果的 `watermark`，再以結果的 `next_after` 繼續同一批查詢。若要在整個本回合內穩定分頁，先取得 turn scope，再使用 `turn_id` filter 和第一次回應的 watermark：

```lua
local scope = room:historyScopes()
local page = room:queryHistoryFacts {
  kind = "move",
  turn_id = scope.turn_id,
  limit = 64,
}
local watermark = page.watermark
local moves = page.items
while page.has_more do
  page = room:queryHistoryFacts {
    kind = "move",
    turn_id = scope.turn_id,
    after = page.next_after,
    watermark = watermark,
    limit = 64,
  }
  for _, item in ipairs(page.items) do table.insert(moves, item) end
end
```

若 `complete` 或 `attribution_complete` 為 false，技能應停止把結果當作完整否定證據；可使用已知項目作提示或記錄，但不能宣稱「本回合沒有該事件」。

## 合成技能例：本回合由其他技能造成的牌進入棄牌堆

以下是以歷史作候選來源、以當前 Room 狀態作最後裁決的模式。它查詢本 turn 的 `move` facts，要求牌是 `from == self` 的失牌，且 `skill_owner` 非 self、非空；dedup card id，逐頁固定 watermark，最後在 `obtainCard` 前重查位置。歷史上的 `to_place` 不作最後條件：牌可以先移到任意區域，之後才在目前狀態進入棄牌堆。`getCardPlace` 是最後一道保護，不能由 fact 中的 `to_place` 取代。

```lua
local function queryHistoryMovesThisTurn(room, self)
  local scope = room:historyScopes()
  if scope.turn_id == "0" then return {} end

  local page = room:queryHistoryMoves {
    turn_id = scope.turn_id,
    from = self:objectName(),
    limit = 64,
  }
  if page.error or page.complete == false or page.attribution_complete == false then
    return {}
  end

  local watermark = page.watermark
  local seen, ids = {}, {}
  while true do
    for _, fact in ipairs(page.items) do
      local d = fact.data or {}
      if d.from == self:objectName()
          and d.skill_owner ~= nil and d.skill_owner ~= ""
          and d.skill_owner ~= self:objectName() then
        local id = d.card_id
        if id and not seen[id] then
          seen[id] = true
          table.insert(ids, id)
        end
      end
    end
    if not page.has_more then break end
    page = room:queryHistoryMoves {
      turn_id = scope.turn_id,
      from = self:objectName(),
      after = page.next_after,
      watermark = watermark,
      limit = 64,
    }
    if page.error or page.complete == false or page.attribution_complete == false then
      return {}
    end
  end

  local obtained = {}
  for _, id in ipairs(ids) do
    -- History is a candidate list. Re-check the authoritative current state.
    if room:getCardPlace(id) == sgs.Player_DiscardPile then
      room:obtainCard(self, id, false)
      table.insert(obtained, id)
    end
  end
  return obtained
end
```

此例使用既有 `obtainCard(ServerPlayer *, int, bool)` overload，逐張取得前重查位置；前一張牌引發的技能也可能改變後一張的位置。若某個 `move` 沒有 skill owner 或 attribution 不完整，應視為未知歸因，不得把它當成 self 以外的技能移牌。

## 實際已記錄的 payload

### `move_cards` event / `move` fact

card movement service 對每張實體牌 append 一筆 `move` fact。常用 `data` key 為：`card_id`、`from`、`to`、`from_place`、`to_place`、`from_pile`、`to_pile`、`reason`、`reason_player`、`reason_skill`、`reason_event`、`skill_name`、`skill_owner`、`instance_id`、`execution_id`、`attribution_complete`、`cause_event_id`、`card`。`card` 是 immutable card snapshot（`id`、`name`、`class_name`、`suit`、`number`、`virtual`、`subcards`、`skill_name`）。欄位會依移牌原因與歸因是否可得而存在；查詢端必須容忍缺欄位。

### `use_card` / `skill` / `respond_card`

事件的 `data` 保存公開執行 context，例如 `from`、`card`、`skill_name`、`skill_owner`、`invoker`、`instance_id`、`execution_id`、`activation_owner`、`activation_skill`、`activation_instance_id`。`use_card` fact 另有 `targets`。`respond_card` fact 中，`from` 是原始發起者，`player` 是實際代打／作出 response 的角色；兩者不可混為一談。技能成本失敗、無結果、效果跳過、取消與 exception unwind 會以 event `outcome`（如 `cancelled`、`skipped`、`aborted`、`pay_failed`、`prevented`）反映，不應只依 event 存在判定效果已成功。

### damage、components 與 actual damage

`damage` event 的輸入 marker 可包含 `from`、`to`、`amount`、`card`、`reason_skill`；它本身不表示 HP 已實際扣除。armor 與 hp 是不可變的 `damage_component` facts；當不可逆 mutation 已生效時，才 append 一筆 `actual_damage` marker。raw `actual_damage` 的 `amount` 只代表首次已提交 component，`requested_amount` 才是引擎接受的原始請求量。`queryActualDamage` 以固定 watermark 查詢 marker，再同 event 聚合 `component == "armor"` 的 `amount` 為 `absorbed`、`component == "hp"` 的 `amount` 為 `hp_loss`，並輸出 `amount = absorbed + hp_loss`。傷害技能應使用這個 projection 統計已生效傷害，不要自行把 raw facts 加總。

例如原請求 `2 damage` 先扣除 `1` armor，之後在 `LostHujia` callback 中止，`queryActualDamage` 的結果是 `amount = 1`、`absorbed = 1`、`hp_loss = 0`；它不宣稱尚未發生的 HP 扣除。

因此 armor callback 中尚未提交的 pending HP loss 不得寫入已發生的歷史；`actual_damage` 也不能在 HP mutation 前先宣稱完整結果。讀取 projection 是 read-only，不能改寫舊 component。

## Lua 合約：只用 plain tables

`swig/resolution-history.i` 只允許 Lua plain table／scalar crossing：bool、integer、finite number、string、plain list/map，深度上限 8。filter 必須是無 array part 的 flat scalar map；結果 map 若含 unsupported metatype 或過深巢狀值，wrapper 會拒絕。不可把 C++ pointer、QObject、Card、ServerPlayer、userdata、function 或 coroutine state 寫進 event/fact payload。

這個規則也限制 Lua 技能設計：把查詢結果轉成自己的 plain table 後再處理，保留 `complete`／`attribution_complete` 狀態；不要把歷史物件當成可變 runtime object。既有 RoomThread／技能回呼流程維持原樣，歷史只是 Room-owned journal 與 query facade。

## Snapshot、Replay 與 takeover

Room snapshot 將歷史保存於 `state.resolutionHistory`；32 位元版逐筆串流寫出，64 位元版維持 `resolutionHistory.serialize()` 路徑。該 journal 目前自身的序列化版本為 `version = 1`，必須含 `complete`、`next_id`、`next_sequence`、`events`、`facts`、`active` 等欄位，且 active scope 只能保留可恢復的 round。snapshot builder 會把不完整歷史標成 ineligible；deserialize/restore 會做 parent、id、sequence、scope、payload 與 counter validation。

Replay takeover 的外層 schema 目前為 3；可接管 snapshot 必須是完整 `fromstart` replay 的完整歷史 snapshot（不得淘汰歷史記錄）。缺少開局前資料、只保留部分歷史、歷史 incomplete、Lua takeover state 不可恢復、或 replay 起點不完整時，必須拒絕 takeover；不能用一個目前場面 snapshot 假裝完整歷史，也不能以空歷史當作「沒有發生過」。對局內的歷史快照共享不可變分頁；後續寫入只複製受影響的頁與索引樹路徑。錄影播放器按需從磁碟載入，快取最多保留一個 `QSharedPointer<GameSnapshot>`；正在使用它的 caller 可安全持有額外引用。

restore 時先驗證並 remap snapshot player ids，再 restore history；pending extra turn 的 `causeEventId` 也必須指向存在的歷史 event。不能用 eviction 破壞已發布的 replay／shared snapshot。

## 記憶體成本

歷史會增加伺服器 RAM：每段結算保存 event，每張實體牌的移動保存 fact，另有傷害 component、值快照與查詢索引。資料保留至 Room 結束，沒有「只留最近幾百筆」的截斷；查詢 `limit` 只限制回傳頁大小，不限制儲存量。因此長局、大量移牌或技能連鎖的成本會隨記錄數持續增長。

目前以 256 筆分頁及共享快照降低複製量，後續記錄修改只複製受影響的頁與儲存樹路徑；不長期持有 Card／Player 指標或完整 execution。Replay 播放器最多快取一個載入的 snapshot。32 位元伺服器的已落盤快照只留路徑、雜湊與回合索引，按需載入時也只快取一筆；64 位元伺服器維持記憶體保存。32 位元存檔逐筆輸出事件、玩家及牌，以 64 KiB 輸出緩衝避免同時建立整份 QVariant 歷史、JSON 文件及輸出位元組；單筆 payload 的正規化仍有暫存成本。64 位元預設序列化仍建立完整值結構，載入／還原仍須解析整份文件及重建索引，因此仍可能產生歷史大小級別的記憶體峰值。這些措施降低重複保存，不能讓歷史免費，也不構成 RAM 上限。

2026-09-20 的 05P 重測僅量到整個程序 working set：採樣中伺服器最低約 343 MB、最高約 1,419 MB，TUI 約 183–188 MB（十進位 MB）。伺服器數字同時包含規則、Lua、AI、replay 等配置，沒有關閉歷史的同條件基準，不能將這段增幅全歸因於歷史，亦不能宣稱歷史只增加少量 RAM。隔離量測 journal 與 snapshot 保留量仍是未完成的效能驗證。

## 參考實作

FreeKill 的事件模型可作概念參考，但不是本專案的 API 來源：

| 主題 | 相對參考路徑（FreeKill-release 根目錄） | 可借鑑之處 |
| --- | --- | --- |
| event id、parent、status、interrupted | `lua/server/gameevent.lua` | `findParent` 的祖先鏈語意、事件生命週期與中斷概念 |
| scope 內查事件 | `lua/server/gamelogic.lua`、`lua/server/gameevent.lua` | `getEventsOfScope` 類型的回合／階段查詢思路 |
| 移牌前後事件 | `ltk/server/events/movecard.lua` | `BeforeCardsMove`／實際套用／`AfterCardsMove` 的邊界 |
| HP、shield、damage 分段 | `ltk/server/events/hp.lua` | armor/shield 與 HP mutation 應分開觀察 |
| 技能 effect 執行 | `ltk/server/events/skill.lua`、`ltk/server/events/usecard.lua` | skill/use card 的巢狀 execution context |
| 克己（Keji）查本回合 Slash | `packages/standard/pkg/skills/keji.lua` | 先找 Play phase，再按 event scope 過濾本角色的 UseCard/RespondCard；本專案應改以固定 watermark 的歷史 facts 實作 |

FreeKill 的 `getEventsOfScope`／`findParent` 是設計參考；本專案的 `historyParent` 明確只查 ancestor，`queryHistory*` 的 filter key、結果 key、watermark 與 completeness semantics 以本文上方的 C++/SWIG 介面為準。

## 作者與驗證狀態

**Authored**：凜依目前 source code、Room wrappers、SWIG typemap、snapshot restore 路徑與 FreeKill 定向參考撰寫本規格。

2026-09-20 經使用者授權後完成下列驗證，證據保存在 `builds/resolution-history-validation-20260920-154324/`：

| Gate | 狀態 | 範圍 |
| --- | --- | --- |
| Debug build | PASS | GUI、server、TUI、兩個 history test targets；SWIG 重新生成並編譯 |
| Native history | PASS | 6 個獨立 case：late-acquisition、damage、pile-moves、nested-turn、pagination、cleanup |
| Lua / Room integration | PASS | rules-lua、isolated-lua、room-wrappers、snapshot |
| Replay takeover focused | PASS | `qsanguosha_core_tests --suite takeover-snapshot`；含 Room 實際還原 |
| 05P SmartAI real TCP 初次 | FAIL（TUI） | 伺服器自然結局 `lord+loyalist`；TUI 訊息落後、600 秒等待超時，退出碼 7 |
| 05P SmartAI real TCP 修正後 | PASS | 相同種子與伺服器 binary；TUI 收到 GAME_OVER、script 完成、client/server exit 0、無 orphan、TCP/WS port 釋放；證據在 `builds/tui-delay-fix-20260920/` |
| CTest / GUI 人工 / CI / 跨平台矩陣 | NOT RUN | 本輪未宣稱這些 gate 已通過 |

新 fixture 的初次執行暴露了缺少 Room context/card-state reset，以及監聽 `ChoiceMade` 時被 trigger-order 選擇遞迴觸發的問題；均已在 fixture 修正。isolated AI fixture 改為將 self 與 other-player list 分開，符合既有 facade 合約。原始失敗日誌與可取得的 dump 均保留，最終 10 個 history case 正常退出。

初次完整對局失敗證據保留。使用者另行授權定位並修復 TUI 延遲後，移除 classic 模式逐通知建立未使用摘要的運算，重用文字清理的固定正規表示式，並通過四項 focused 測試及一次同種子重測。兩次的 246 筆 `[LOG]`／`[AUTOTEST]` 去除時間後順序一致；新版 TUI 的完整結局日誌在 server GAME_OVER 後約 0.70 秒寫完。這是 classic/script TCP 驗收，不代表可見 board／GUI 或 history RAM 增幅已驗收。
