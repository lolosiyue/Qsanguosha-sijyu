# 結算歷史（Resolution History）

結算歷史供伺服器規則腳本查詢。資料與查詢契約由 [ResolutionHistoryService](../src/core/resolution-history.h) 定義，Lua 入口為 [Room::queryHistoryFacts](../src/server/room.cpp) 等封裝及 [SWIG 綁定](../swig/resolution-history.i)。

## 用途與邊界

Room 持有一份 server authoritative 的 `ResolutionHistoryService`。它只供 rules Lua 讀取，保存可重播、可查詢的 primitive value tree；presentation、isolated AI 與 private request body 不會經由這個介面取得。

`room:getCardPlace(card_id)` 查詢現在的位置，`move` fact 記錄過去的移動。查得候選牌後，在 `obtainCard`／移牌前重新檢查 `room:getCardPlace` 與 owner，避免其他結算已將牌移走。

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

`show_cards` event／同名不可變 fact 記錄既有 `showCard`、`showAllCards` 的展示。
`data.player` 是展示角色，`all_hand` 區分全手牌入口，`cards` 是逐張
`{card_id, card, place, is_handcard}` 快照，`viewers` 是傳給原生展示入口的有效觀眾角色名稱（去重）。
觀眾指邏輯角色，不是網路送達或控制者連線的證明，也不改變既有公開／限定展示規則。
牌值在通知前凍結，fact 在通知後、`ShowCards` 回呼前追加；後續移牌、改牌或中斷不覆寫事實。
巢狀展示有自己的事件 ID；沒有牌的早退不產生展示紀錄。`showVirtualCard` 的外觀通知不算本紀錄。
查「本回合未展示」應指定 `kind="show_cards"`、`turn_id` 及 `player`，完整分頁後讀取實體 ID；
不要求技能歸因完整。舊 journal 缺此種類 coverage 時，`complete=false`，不能當成零次。

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

### 原生 C++ 技能查詢

`Room::queryCardHistory(player, scope, className, responses, playOnly)` 是同一 journal 的唯讀投影。
`scope` 可為 `turn`（預設）、`phase`、`round` 或 `game`；沒有當前 scope 時回傳空集合，
不把 scope 0 當作整場。`items` 是按 fact sequence 排序的牌值快照。
`className` 為空或 `"."` 時維持「普通牌」語意，排除 SkillCard；指定名稱時匹配快照的
`classes` 或該筆已解析的 `history_key`，並納入匹配的 SkillCard（如 `MTYinglveCard`、`ZhibaCard`）。
同一 fact 同時匹配 class 與 key 仍只回傳一次，不依查詢時的技能／實體牌狀態重建 key。
一般使用與 `is_use` 回應合計為使用，`responses=true` 則只列純回應，使用實際 responder。
`playOnly=true` 可取本回合所有出牌階段，並非只有最後一個出牌階段。
`countHistoryCards` 使用相同條件，資料不完整時回傳 `-1`，不得當成零次。

`use_card` 與 `respond_card` facts 在接受當次牌時保存 primitive `history_key`；普通轉換牌維持
實際 card class，V2 SkillCard 使用 `historyKey(request)`，舊 LuaSkillCard 使用原有 `#name`。
投影項目亦包含該 key。舊 fact 若缺 key 且不能由 class 確認匹配，名稱查詢回 `complete=false`，
不把未知別名判成零次；普通牌總數查詢不依賴 key。

目標重選的 `TargetModSkillQueryScope` 明確攜帶本次 `use_card` event ID。投影只排除同一 player
及該 ID 的 use fact；尚未 append 時不排除任何一筆，不按張數減一，不排除其他回應或巢狀用牌。
這也適用於不增加舊 Player counter 的正式用牌。原始 `queryHistoryFacts` 保留全部不可變 facts，
舊 Player counter 僅在明確已增加 `historyKey` 時保留原有的查詢補償。

`Room::queryCardUseDamage(useEventId=0)` 依指定用牌事件（預設為當前最近的 use_card 祖先）
查詢已提交傷害，排除獨立巢狀用牌及無關技能傷害。實體牌重複使用時，由事件 ID 區分，
不依賴 Card 指標或牌上的累積 tag。這兩個投影的 `attribution_complete` 指實際用牌者／
用牌事件的歸因；不要求造成效果的舊技能具有 skill owner。原始查詢的技能歸因契約不變。

牌快照另含 `classes`（primitive list）、`type`、`red`、`black`、`ndtrick`，
避免查詢時以已變化的實體牌重建歷史。缺少這些欄位的舊記錄不會被誤判為零次。

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

`move` fact 另有 `card_before`：在該次提交移除來源牌、執行裝備卸除及 wrapped-card
filter/reset 前複製的牌快照，保留來源區域當時的 `red`、`classes`、`type` 等資訊。
它由每次移牌提交的區域值保存，巢狀移牌不會覆寫；分段移牌的 loss/gain 各自保存該段
開始前的快照。原有 `card` 仍描述移動記錄提交時的牌。查來源顏色的技能必須讀
`card_before`；舊記錄缺欄位視為未知，不得拿 `card` 代替。

### `use_card` / `skill` / `respond_card`

`use_card_targets` fact 在 `TargetSpecified` 分派正常返回後追加到該次確切的 `use_card`
event；exception unwind 不會產生完成事實。`data` 包含 `use_event_id`、`from`、`player`
（實際用牌者）、`card` 與 `targets`，保存所有該時機修改完成後的目標。這個快照不聲稱
已造成效果，也不代表更晚時機的目標變更。原始 `use_card.targets` 保持接受用牌時的值，
不被覆寫。幻璃與原本觀察 `TargetSpecified` 的規則使用新事實，觀察 `CardUsed` 的規則
仍按其原時機查詢。

`skill_invoked` fact 記錄已接受的技能發動，於 `EventSkillInvoking` 分派前寫入，
不包含尚未通過成本／支付的 skill event。舊 TriggerSkill 沿用 `SkillTriggered`
通知入口；已有 legacy activation lifecycle 的技能仍只在舊通知記一次。
`data.player` 是通知中的發動角色，`invoked_skill` 是實際 activation 技能名；
`skill_name`／`skill_owner` 則保留 V2 的根來源歸因。舊通知沒有精確來源，
其 `attribution_complete=false`，不能據此推定技能持有者；按明確 `player`
計算發動次數則不需要推定來源。fact 表示發動，不代表效果成功完成。

事件的 `data` 保存公開執行 context，例如 `from`、`card`、`skill_name`、`skill_owner`、`invoker`、`instance_id`、`execution_id`、`activation_owner`、`activation_skill`、`activation_instance_id`。`use_card` fact 另有 `targets` 與 `is_handcard`（該次已接受用牌的所有實體材料是否來自手牌），供第一張手牌等規則查詢。`respond_card` fact 中，`from` 是原始發起者，`player` 是實際代打／作出 response 的角色；兩者不可混為一談。技能成本失敗、無結果、效果跳過、取消與 exception unwind 會以 event `outcome`（如 `cancelled`、`skipped`、`aborted`、`pay_failed`、`prevented`）反映，不應只依 event 存在判定效果已成功。

`skill.data.targets` 是每次更新時 `SkillContext.targets` 的角色名稱快照，保留順序及重複票數；包含技能在 effect 內手動選出的目標。Finished 正常返回與例外中止都回讀最後 context 後更新，並在釋放 execution 儲存前凍結。Trigger 的中止清理同樣保存 Finished observer 回傳／中斷前的目標。這是選擇紀錄，不證明每個目標均完成效果；要判斷完成仍須看 outcome 與相應提交 facts。舊 event 缺 `targets` 表示未知，不可當成空目標。

### damage、components 與 actual damage

`damage` event 的輸入 marker 可包含 `from`、`to`、`amount`、`card`、`reason_skill`；它本身不表示 HP 已實際扣除。armor 與 hp 是不可變的 `damage_component` facts；當不可逆 mutation 已生效時，才 append 一筆 `actual_damage` marker。raw `actual_damage` 的 `amount` 只代表首次已提交 component，`requested_amount` 才是引擎接受的原始請求量。`queryActualDamage` 以固定 watermark 查詢 marker，再同 event 聚合 `component == "armor"` 的 `amount` 為 `absorbed`、`component == "hp"` 的 `amount` 為 `hp_loss`，並輸出 `amount = absorbed + hp_loss`。傷害技能應使用這個 projection 統計已生效傷害，不要自行把 raw facts 加總。

例如原請求 `2 damage` 先扣除 `1` armor，之後在 `LostHujia` callback 中止，`queryActualDamage` 的結果是 `amount = 1`、`absorbed = 1`、`hp_loss = 0`；它不宣稱尚未發生的 HP 扣除。

因此 armor callback 中尚未提交的 pending HP loss 不得寫入已發生的歷史；`actual_damage` 也不能在 HP mutation 前先宣稱完整結果。讀取 projection 是 read-only，不能改寫舊 component。

`component == "armor"` 的新 fact 另帶 `armor_before`、`armor_after`，在 `loseHujia` 的實際扣除回呼內凍結，早於 `LostHujia`。該回呼提供已扣除量；`@HuJia` 寫入不觸發 `MarkChanged`，因此當下護甲值及其加回已扣除量分別就是本次提交後、提交前值。`queryActualDamage` 在同一 watermark 下投影第一筆 armor component 的 `armor_before` 與最後一筆的 `armor_after`，不受其後回呼補甲或 HP component 影響。沒有 armor component 或舊 fact 缺欄位時不合成這兩欄；缺欄位不能視為零。判斷本次傷害扣光護甲可要求 `absorbed > 0`、`armor_before > 0` 且 `armor_after == 0`。

### recover 與 actual recovery

`recover` event 包住 `StartHpRecover` 至 `HpRecover`，輸入的 `requested_amount` 不代表已回復。只有 HP setter 確實增加體力時，才在既有狀態提交回呼、`HpChanged` 之前寫入一筆 `actual_recover` fact。它帶 `from`（回復來源）、`to`／`player`（接受者）、`requested_amount`（攔截後、上限裁切前數量）、`amount`（本次實際增加量）、`hp_before`、`hp_after`、`card` 與既有原因／技能歸因欄位。全滿、取消或零增加不會產生此 fact；提交後的巢狀回復、扣血與中斷不會改寫它。

C++ 可用 `Room::recover(player, recover, setEmotion, &eventId)` 取得本次精確 event ID，再用 `queryHistoryFacts({{"kind", "actual_recover"}, {"event_id", eventId}})` 查結果，避免將同角色的巢狀回復混入。未開始回復或歷史停用時輸出 ID 為零；原三參介面保持不變，未新增 SWIG API。查詢須檢查 `complete` 並處理分頁；依技能來源篩選時另檢查 `attribution_complete`。明確 event／接受者的 HP 增量不依賴技能來源歸因，已知完整且同 event 無 fact 才代表本次沒有實際增加。

## Lua 合約：只用 plain tables

[`swig/resolution-history.i`](../swig/resolution-history.i) 只允許 Lua plain table／scalar crossing：bool、integer、finite number、string、plain list/map，深度上限 8。filter 必須是無 array part 的 flat scalar map；結果 map 若含 unsupported metatype 或過深巢狀值，wrapper 會拒絕。不可把 C++ pointer、QObject、Card、ServerPlayer、userdata、function 或 coroutine state 寫進 event/fact payload。

這個規則也限制 Lua 技能設計：把查詢結果轉成自己的 plain table 後再處理，保留 `complete`／`attribution_complete` 狀態；不要把歷史物件當成可變 runtime object。既有 RoomThread／技能回呼流程維持原樣，歷史只是 Room-owned journal 與 query facade。

## Snapshot、Replay 與 takeover

Room snapshot 將歷史保存於 `state.resolutionHistory`；32 位元版逐筆串流寫出，64 位元版維持 `resolutionHistory.serialize()` 路徑。該 journal 目前自身的序列化版本為 `version = 2`，必須含 `coverage`、`complete`、`next_id`、`next_sequence`、`events`、`facts`、`active` 等欄位，且 active scope 只能保留可恢復的 round。snapshot builder 會把不完整歷史標成 ineligible；deserialize/restore 會做 parent、id、sequence、scope、payload 與 counter validation。

Replay takeover 的外層 schema 目前為 3；可接管 snapshot 必須是完整 `fromstart` replay 的完整歷史 snapshot（不得淘汰歷史記錄）。缺少開局前資料、只保留部分歷史、歷史 incomplete、Lua takeover state 不可恢復、或 replay 起點不完整時，必須拒絕 takeover；不能用一個目前場面 snapshot 假裝完整歷史，也不能以空歷史當作「沒有發生過」。對局內的歷史快照共享不可變分頁；後續寫入只複製受影響的頁與索引樹路徑。錄影播放器按需從磁碟載入，快取最多保留一個 `QSharedPointer<GameSnapshot>`；正在使用它的 caller 可安全持有額外引用。

`coverage.events`／`coverage.facts` 是排序後的 kind 字串陣列，宣告該種類自 journal 起始即有原生記錄支援。新局宣告目前全部支援種類；copy、snapshot、remap、restore 保留原宣告，恢復後新增一筆新種類也不會補成開局覆蓋。v2 缺 coverage、錯型別、重複或未知 kind 會拒絕匯入；陣列可為空，代表沒有可證覆蓋種類。

v1 匯入仍保留原生基線已保證的 event 種類 `game/round/turn/phase/skill/use_card/respond_card/damage/move_cards/extra_turn` 及 fact 種類 `actual_damage/move/use_card/respond_card/damage_component`；其餘新種類視為未知，不因舊 journal 中恰有一筆而升格。重新匯出使用 v2 並保存這個較小集合，不修改既有 events/facts。

`queryHistoryEvents`／`queryHistoryFacts` 回傳 `coverage_complete`；指定 `kind` 時檢查該種類，未指定時須覆蓋目前全部 event／fact 種類。查詢的 `complete` 同時要求原 journal 完整、query 有效及種類覆蓋，未覆蓋時仍可列出已知 items，但不能將空列表推為零次。snapshot `isComplete` 仍表示結構及安全邊界完整，與某個新種類的查詢覆蓋分開；因此舊 snapshot 可以載入，新技能仍須遵守 unknown。只需要某種類的消費者應明確指定 `kind`。覆蓋只保證種類，不保證後加 payload 欄位；`targets`、`entered`、護甲提交值等仍須逐筆驗欄位。

restore 時先驗證並 remap snapshot player ids，再 restore history；pending extra turn 的 `causeEventId` 也必須指向存在的歷史 event。不能用 eviction 破壞已發布的 replay／shared snapshot。

## 記憶體成本

歷史會增加伺服器 RAM：每段結算保存 event，每張實體牌的移動保存 fact，另有傷害 component、值快照與查詢索引。資料保留至 Room 結束，沒有「只留最近幾百筆」的截斷；查詢 `limit` 只限制回傳頁大小，不限制儲存量。因此長局、大量移牌或技能連鎖的成本會隨記錄數持續增長。

目前以 256 筆分頁及共享快照降低複製量，後續記錄修改只複製受影響的頁與儲存樹路徑；不長期持有 Card／Player 指標或完整 execution。Replay 播放器最多快取一個載入的 snapshot。32 位元伺服器的已落盤快照只留路徑、雜湊與回合索引，按需載入時也只快取一筆；64 位元伺服器維持記憶體保存。32 位元存檔逐筆輸出事件、玩家及牌，以 64 KiB 輸出緩衝避免同時建立整份 QVariant 歷史、JSON 文件及輸出位元組；單筆 payload 的正規化仍有暫存成本。64 位元預設序列化仍建立完整值結構，載入／還原仍須解析整份文件及重建索引，因此仍可能產生歷史大小級別的記憶體峰值。記憶體需求仍隨歷史資料量增加。

目前沒有關閉歷史的同條件基準，不能將整段增幅全歸因於歷史，亦不能宣稱歷史只增加少量 RAM；隔離量測 journal 與 snapshot 保留量仍未完成。

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

## 各事件與 fact 的實作契約

### 死亡與回合 HP 邊界快照

`death` event/fact 在原生 killPlayer 通過 BeforeGameOverJudge，發布死亡之後、GameOverJudge／Death listener 之前寫一次；不依賴任何技能是否存在。data 包含 `victim`／`to`、`killer`／`from`（無傷害來源時空字串）、`victim_role`／`killer_role`、`victim_kingdom`／`killer_kingdom`、`killer_is_friend`，及 `attribution_complete=true`。陣營與來源在死亡 callback 前凍結；之後復活不刪除已發生死亡事實。`killer_is_friend` 使用 Player::isFriendWith；劍閣等以 role 同隊的規則應比對兩個 frozen role。

`death.data.cause_kind` 於 killPlayer 入口、所有死亡回呼之前，由原生原因參數凍結：`hp_lost` 表示提供了 HpLostStruct，否則有 DamageStruct 為 `damage`，兩者皆無為 `other`。若兩者同時存在，以原生 hplost 非空判據歸為 `hp_lost`；不從祖先事件或 HP 差推定。舊 event／fact 缺此欄位即為原因未知，查詢者不得當成 `other`，亦不得略過這名死者改查更早死亡。種類 coverage 僅保證 death 記錄存在，不保證舊 payload 擁有新增欄位。

`turn_hp_snapshot` fact 連至原 `turn` event，每位角色（含死亡角色）一筆，data 為 `player`、`hp`、`alive`、`phase`、`turn_owner`、`boundary`、`completion`、`attribution_complete=true`。`start/running` 在 TurnStart effects 前；`end/completed` 在整個 TurnStart dispatch（含結束效果）正常返回後，回傳 broken 時 completion 為 `broken`；TurnBroken 的模式清理完成後記 `end/interrupted`，不可將其冒充正常回合結束。尚未完成清理的中斷沒有 end 快照。

可依 `kind`、`event_id`／`turn_id`、`player` 查 facts，再讀 data.boundary／completion；查上一個自己的回合先查 turn events（player=自己），選當前 turn_id 之前最大 ID，再找該 event 的 end fact。上一回合缺快照應回 unknown，不得跳過到更早回合。所有分頁維持固定 watermark 並檢查 complete／attribution_complete。
### 執行入口與根來源

skill event data 的 `executing_skill` 是實際 activation/helper 名稱，`skill_name`／`skill_owner`／`instance_id` 保留根來源。移牌 `historyCause` 以 executing_skill 比對 CardMoveReason.m_skillName，匹配時繼承精確來源；只有無 activation 的 legacy scope 才以 skill_name 補執行名。一般 V2 trigger 的 activation 保留實際入口，source 解析至 exact root；存在 activation 卻無法解析 root 時不假造同名來源。

### 瀕死與拼點的事件身分

`dying` event 包住一次 `Room::enterDying`。EnterDying 前寫 `dying_start`，QuitDying callbacks 前寫 `dying_result`，各 fact data 包含 `player`／`to`、傷害來源 `from`（無則空）、`reason`、`hp`、`alive`、`attribution_complete=true`。result 表示該次流程退出狀態，event 仍在 QuitDying callbacks 期間可查；正常退出才 completed，例外沿既有 guard 記 interrupted。是否首輪首次進入濒死應查 round_id/player 的 dying_start，不能依目前技能是否持有或 HP 推算。

`pindian` event 包住 `ServerPlayer::finishPindian`。PindianVerifying 前寫 `pindian_start`；驗證後算定勝負、Pindian callbacks 前寫 `pindian_result`。data 包含 `from`、`to`、`reason`、`from_card`／`to_card`（值快照）、`from_number`／`to_number`、`attribution_complete=true`；result 另含 `success`。事實不記 C++ 指標；同玩家或同牌再次／巢狀拼點各有不同 event ID。

事件內用 `historyParent(currentHistoryEventId(), "dying" 或 "pindian", true)` 取得 exact event，再以該 event_id 查相應 fact。completion 與開始事實是不同時點；callback 跨事件的 receipt 應保存 event_id，不可只比對玩家或實體牌 ID。

### 摸牌與判定的事件身分

每次原生摸牌請求有獨立 `draw` event，`DrawStruct.historyEventId` 是伺服器指定的 ID，`DrawNCards`／`AfterDrawNCards` roundtrip 後恢復原值。`draw_start` 在回呼前記 `player`／`to`、`reason`、`requested_count`、`top`、`visible`，保留當時的來源歸因。零張、死亡而略過的請求也留下 start/result，不觸發原本不會觸發的 DrawNCards。

`draw_result` 在提交的移牌正常返回後、AfterDrawNCards 前記錄。它保留原 `player`（請求對象）、更新後的 `to`、`prepared`（DrawNCards 是否完整返回）、可用時的 `modified_count`、`submitted_card_ids` 及 `outcome=completed/skipped/aborted`。這些 ID 是提交給移牌流程的集合，**不是保證實際得到的牌或數量**；BeforeCardsMove 改寫、移牌替代及巢狀效果需另查 committed `move_cards` facts。中斷且尚無 result 時補 `aborted` result；若 AfterDrawNCards 才中斷，已凍結的 result 保留，event outcome 仍為 aborted。

批次摸牌先建立各請求的 sibling events，以 `beginEvent(..., activate=false)`／EventGuard 尾參 `activate=false` 暫不改 active context。各 DrawNCards、取牌與 AfterDrawNCards 以既有 ContextGuard 切入該 draw，整批仍只做一次 atomic move，其 parent 為原 batch caller，不能將它誤算成其中一名角色的專屬移牌。所有正常／中斷出口皆關閉已建立的 draw events；未處理到的批次請求在中斷時也有 aborted result。沒有把 siblings 偽造成巢狀 draw。

每次 `Room::judge` 建立獨立 `judge` event；`judge_start` 記 `player`／`to`、`reason`、`pattern`、`good`、`negative`。FinishRetrial 定案後、FinishJudge 回呼前，`judge_result` 凍結上述欄位及 `card` 值快照、`result_available`，有牌時另記 `is_good`／`is_effected`。`outcome=completed` 表示該判定定案；FinishRetrial 要求重判時先記 `restarted`，遞迴呼叫是獨立 child judge event。定案前中斷記 `aborted`、`result_available=false`，不把未知結果推成失敗；定案後 FinishJudge 中斷保留結果而 event 記 aborted。依 exact event_id 查詢，不用同名技能、牌 ID 或完成後牌區反推。

### 數值提交與初始基線

`player_state` event/fact 保存伺服器數值真正提交的前後值：`player`／`to`、`boundary=baseline/commit`、`mutation`、`hp_before/hp_after`、`maxhp_before/maxhp_after`、`hand_count_before/hand_count_after`，以及原生 `historyCause` 的來源資料與 `parent_event_id`。每筆有自己的 event ID、sequence、round/turn/phase scope；不依技能存在與否才記錄。`mutation=hp/maxhp/hand_add/hand_remove/hand_draw` 表示原生提交入口，不代表傷害／回復／失去體力的玩法分類；應以 parent 與 cause 查實際來源，不能把任意 HP 減少自動稱為傷害。

一般新局在角色初始化完成後、`constructTriggerTable()` 發出逐角色 GameReady 之前，對全體角色記一次 `mutation=baseline`（before=after）。之後 Player 原生 setter/list 寫入立刻經 ServerPlayer→Room 的同步、純紀錄 hook，先凍結數值再發原有 Qt signals 與遊戲回呼。直接 `setHp`／`setMaxHp` 也涵蓋；maxHP 下修同時夾低 HP 時，兩個欄位在同一筆 commit 反映，不製造中間 HP 狀態。相同值、重複加牌、移除不存在手牌不產生 commit；排序不改手牌數，也不記假變動。

手牌每次實際 list 增刪各記一筆，保留原批次移牌的先移除、後加入順序，原 atomic move 仍只提交一次；facts 的 `move_cards` 祖先識別同一批操作。中間數值是引擎實際已提交狀態，不是整批結束淨差，也不保證整批最終取得量。中斷不刪除已提交的數值事實。`drawCard` 的直接 list 入口同樣涵蓋；Client setter 與純物件 copy/import 不生成新遊戲歷史。

takeover 初始化不建立新的 baseline，restore 期間不記提交；GameReady 還原完成後才啟用未來提交，保留原 snapshot 的基線與歷史。舊 snapshot 缺 `player_state` baseline 時，不能由 journal 的 `complete=true` 推定此新增種類從開局完整：全局峰值／否定事實查詢必須確認該角色 baseline，固定 watermark 並讀完所有分頁；缺基線回 unknown，不能以目前值補造過去。數值查詢不要求來源 attribution 完整，但凡按技能來源分類仍須檢查 attribution。

### 階段真正進入

兩個原生 phase 路徑（`ServerPlayer::changePhase` 與 `play` 的 phase loop）在建立 event 時存 `entered=false`，且只在真正呼叫 EventPhaseStart 之前改為 `true`，包含 NotActive。Changing／Skipping 中斷或略過保留 false；Start dispatch 內中斷仍為 true。`outcome` 只表示完成／略過／中斷，不可替代 entered；舊 event 缺此欄位表示未知，不能當作 false 或推算 true。
### 失去體力的原生事件

`Room::loseHp` 對存活目標的正數請求建立 `lose_hp` event，包住 PreHpLost、原生護甲／HP 處理及 HpLost。event 初始 data 的 `from`／`to`／`player`、`requested_amount`、`ignore_hujia` 及原因是攔截前請求；PreHpLost 取消、或改寫後請求失效時 outcome 為 `prevented`，不產生 `hp_lost` fact。流程例外仍由 guard 標記 `aborted`。

只有真正走到 HpLost 派發前才寫 `hp_lost` fact，其 data 為攔截後本次採用的 `from`／`to`／`player`、`requested_amount`、`ignore_hujia`、`armor_requested_contribution` 與 `historyCause` 原因。護甲欄位是本次 loseHp 用於分攤請求的局部量，**不是實際護甲或 HP 減少量**；沒有 `amount`／`actual_hp_loss` 推斷。HpLost listener 內再中斷時保留已到達派發的 fact。需要判斷「發生過 HpLost」用此 fact；需要真 HP 數值變更則查 `player_state` 的提交前後值與 `lose_hp` 祖先，不拿請求量當結果，也不能把巢狀傷害／回復的數值差併成本次失去體力。`hp_lost.data.phase_entered` 在追加 fact 當下讀取該 fact 所屬 current phase event 的 `data.entered`，不是角色 `getPhase()`，也不使用 phase 後來完成時的值。它可區分同一 phase 的 Changing 期間（false）與 EventPhaseStart 已開始（true）；phase ID 沿用 fact 的 `phase_id`。沒有 phase 或舊資料缺 entered 時不填此欄位，須視為未知。

### 傷害與逐角色目標確認的派發事實

`damage_caused`／`damage_inflicted` 在原生 `DamageCaused`／`DamageInflicted` 即將派發時，各追加一筆至本次 `damage` event；早先步驟已阻止流程就不會產生後續階段事實。欄位為 `damage_event_id`、`player`（該次派發對象）、`from`、`to`、`requested_amount`、`nature`、`chain`、`transfer`、`card` 值快照與既有 `historyCause` 來源。這是到達鉤子的事實，並不表示成功造成傷害；鉤子內預防或中斷仍保留已寫事實。數值及牌的顏色採該次派發前快照，不從稍後的實際傷害反推。

`target_confirmed` 在每次 `TargetConfirmed` 派發前，僅當該派發角色確實在當時 `use.to` 內才寫入；單純接收廣播的旁觀者不計。事實歸屬精確 `use_card` event，保存 `use_event_id`、`player`／`to`（本次確認的對象）、`from`、`card` 與當時 `targets` 快照。較早角色的回呼若改動名單，後續角色依實際派發時名單記錄；這不等於 TargetSpecified 完成時的 `use_card_targets`。

三種 fact 均在技能持有與否以外原生記錄，新局加入種類覆蓋宣告；舊 snapshot 未宣告該種類時查詢回 `coverage_complete=false`／`complete=false`。當前鉤子查詢已包含本次 fact；判斷首次時應以精確事件 ID 與固定 watermark 讀取，不以當下技能計數或一律減一取代。來源限定查詢另檢查歸因完整性，純角色／牌類型計數不要求技能歸因。
