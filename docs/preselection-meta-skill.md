# PreSelectionMetaSkill 說明

## 定位

`PreSelectionMetaSkill` 是掛在武將上的「選將前元技能」。它不是一般的 `TriggerSkill` / `ViewAsSkill`，而是在玩家正式取得武將前，先介入候選列表與後續開局狀態。

它目前主要解決兩類需求：

- 在送出選將請求前改寫候選武將列表
- 在選將流程結束後，為該玩家掛上需要於 `GameStart` 起作用的全域 `TriggerSkill`

## 對象關係

| 層級 | 位置 | 職責 |
|------|------|------|
| 基底類型 | `src/core/skill.h` / `skill.cpp` | 定義 `PreSelectionMetaSkill`、兩個回調、`active_skills` 字串 |
| 武將掛載 | `src/core/general.h` / `general.cpp` | 以 `preselection_skills` 保存武將上的 PreSelection 技能名 |
| 註冊入口 | `src/core/engine.cpp` | `Engine::addPackage()` 掃描武將額外技能；若技能繼承 `PreSelectionMetaSkill`，就呼叫 `General::addPreSelectionSkill()` |
| 選將觸發 | `src/server/room.cpp` | `triggerPreSelectionSkills()` 與 `triggerGeneralNotChosen()` 負責真正回調 |
| 開局回放 | `src/server/gamerule.cpp` | `GameStart` 時讀取 `preselection_active_skills`，再把對應 `TriggerSkill` 加進 `RoomThread` |
| Lua 工廠 | `lua/sgs_ex.lua` | `sgs.CreatePreSelectionMetaSkill(spec)` 建立 Lua 版技能物件 |

## 實際流程

```text
Package 載入
  -> Engine::addPackage()
  -> 發現某個 skill 繼承 PreSelectionMetaSkill
  -> General::addPreSelectionSkill(skill_name)

選將分配
  -> Room::assignGeneralsForPlayers()
  -> Room::triggerPreSelectionSkills(player, selected, "for_general")
  -> 逐個候選武將檢查 preselection_skills
  -> 呼叫 on_general_choosing()
  -> 如有 active_skills，立刻寫入 player.property("preselection_active_skills")
  -> 發送選將請求

若玩家未提交有效選擇
  -> 系統挑出 default general
  -> Room::triggerGeneralNotChosen(player, selected, chosen, "for_general")
  -> 只對最終被系統指派的 chosen general 回調 on_general_not_chosen()
  -> 如有 active_skills，再次追加到 preselection_active_skills

遊戲開始
  -> GameRule::GameStart
  -> 讀取 preselection_active_skills
  -> 逐個名稱找出 TriggerSkill
  -> RoomThread::addTriggerSkill(skill)
```

## 核心語義

### 1. `on_general_choosing()` 是改列表入口

- 觸發時機在候選列表生成之後、送出 `S_COMMAND_CHOOSE_GENERAL` 之前
- 回調收到的是「當前最新版本」的 `generals`；若前一個 PreSelection 技能已改過列表，後一個技能會看到改後結果
- 同一輪候選中，同名 PreSelection 技能只會執行一次；`Room::triggerPreSelectionSkills()` 內部有 `processedSkills` 去重

### 2. `on_general_not_chosen()` 不是「所有落選武將都會回調」

- 它只在玩家沒有提交有效選將結果時觸發
- 觸發對象不是所有候選武將，而是最後被系統指派的那名 `chosen` 武將所攜帶的 PreSelection 技能
- 參數 `generals` 仍然是整個候選列表，方便你根據上下文補償或記錄

### 3. `active_skills` 的語義是「記錄並於開局回放」

- `active_skills` 不是 timeout-only 開關，也不是「選到該武將才生效」
- 目前實作中，`triggerPreSelectionSkills()` 與 `triggerGeneralNotChosen()` 兩條路都會讀取 `skill->getActiveSkills()` 並把字串追加到 `preselection_active_skills`
- `GameRule` 在 `GameStart` 只負責把這些名字解析成全域 `TriggerSkill` 並加入 `RoomThread`
- `RoomThread::addTriggerSkill()` 會按技能指標去重，所以同名 `TriggerSkill` 即使字串重複，通常也不會重複掛載；但文檔與設計上仍建議避免寫出語義重疊的 `active_skills`

### 4. `reason` 目前不要過度假設

- 這條 PreSelection 路徑在本地分支的直接接線點是 `Room::chooseGenerals()`，目前傳入的是 `"for_general"`
- `Room::askForGeneral()` 仍可能收到 `"for_lord"`、`"qiexie"` 等原因字串，但那不代表它們已自動接上 `triggerPreSelectionSkills()`
- 若你的 Lua 邏輯要分流，建議先對 `reason` 做白名單判斷，不要把未接線的情境當成既定行為

## Lua API

### `sgs.CreatePreSelectionMetaSkill(spec)`

| 欄位 | 類型 | 必填 | 說明 |
|------|------|------|------|
| `name` | `string` | 是 | 技能名稱 |
| `active_skills` | `string` | 否 | 逗號分隔的全域 `TriggerSkill` 名稱，會在 `GameStart` 時嘗試掛入 |
| `on_general_choosing` | `function` | 否 | 候選列表回調，可直接返回新列表 |
| `on_general_not_chosen` | `function` | 否 | 玩家未提交有效選擇時的補償/記錄回調 |

#### `on_general_choosing(self, room, player, generals, reason)`

| 參數 | 說明 |
|------|------|
| `self` | 目前的 `PreSelectionMetaSkill` |
| `room` | `Room` 物件 |
| `player` | 當前正在選將的 `ServerPlayer` |
| `generals` | Lua table，內容為候選武將名，可直接回傳替換後列表 |
| `reason` | 目前選將原因字串 |

返回值：新的候選武將列表。

#### `on_general_not_chosen(self, room, player, generals, chosen, reason)`

| 參數 | 說明 |
|------|------|
| `self` | 目前的 `PreSelectionMetaSkill` |
| `room` | `Room` 物件 |
| `player` | 當前玩家 |
| `generals` | 原候選武將列表 |
| `chosen` | 最後被系統指派的武將名 |
| `reason` | 目前選將原因字串 |

返回值：無。

## 建議用法

| 需求 | 建議寫法 |
|------|----------|
| 改寫候選名單 | 在 `on_general_choosing()` 直接返回新 `generals` |
| 選將結束後補償 | 在 `on_general_not_chosen()` 寫 mark / flag / tag |
| 開局後啟用全域邏輯 | 把全域 `TriggerSkill` 名稱放進 `active_skills` |
| 只想在 timeout 時生效 | 不要把條件直接寄望於 `active_skills`；應讓 `active_skills` 掛上全域技能，再用 mark/flag 決定是否真的生效 |

## Lua 範例

下面的例子同時示範三件事：

- 在 `for_general` 情境下保底插入一名武將
- 先把全域 `TriggerSkill` 註冊進 `Sanguosha`
- 真正的 timeout 補償由 `on_general_not_chosen()` 寫 mark，`active_skills` 只負責把全域技能掛起來

```lua
local skillList = sgs.SkillList()
if not sgs.Sanguosha:getSkill("#sample_preselection_draw") then
    local samplePreselectionDraw = sgs.CreateTriggerSkill{
        name = "#sample_preselection_draw",
        global = true,
        events = {sgs.GameStart},
        on_trigger = function(self, event, player, data, room)
            if player:getMark("sample_preselection_timeout") > 0 then
                room:drawCards(player, 1, self:objectName())
            end
            return false
        end
    }
    skillList:append(samplePreselectionDraw)
    sgs.Sanguosha:addSkills(skillList)
end

local samplePreselection = sgs.CreatePreSelectionMetaSkill{
    name = "sample_preselection",
    active_skills = "#sample_preselection_draw",
    on_general_choosing = function(self, room, player, generals, reason)
        if reason ~= "for_general" then return generals end
        if not table.contains(generals, "liubei") then
            table.insert(generals, "liubei")
        end
        return generals
    end,
    on_general_not_chosen = function(self, room, player, generals, chosen, reason)
        room:addPlayerMark(player, "sample_preselection_timeout")
    end
}

sampleGeneral:addSkill(samplePreselection)
```

## 注意事項

1. 回調發生時，玩家尚未正式取得最終武將；不要把它當成一般出牌期技能來寫。
2. `active_skills` 必須能被 `Sanguosha->getTriggerSkill(name)` 找到；也就是說，對應技能本身要先完成註冊。
3. 若你在 `on_general_choosing()` 返回了非法武將名，後續選將請求會直接吃到錯誤資料。
4. 這條流程在選將前執行，避免寫耗時 Lua 邏輯或大量 I/O。
