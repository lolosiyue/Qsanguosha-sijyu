# PreSelectionMetaSkill 選將階段發動技能

## 概述

`PreSelectionMetaSkill` 是一種特殊的技能類型，定義在武將上，在選將階段觸發。支援：
- 修改可選武將列表
- 在玩家未主動選擇時獲得特殊效果
- 遊戲開始時自動加入全域 TriggerSkill

## 流程

```
選將階段 (game_state = -1)
  │
  ├─ assignGeneralsForPlayers()
  │    → 生成候選武將列表
  │
  ├─ triggerPreSelectionSkills()
  │    │
  │    ├─ 遍歷候選武將
  │    │    if (武將.hasPreSelectionSkill())
  │    │        觸發 on_general_choosing()
  │    │        → 可修改 generals 列表
  │    │
  │    └─ 記錄 active_skills 到玩家屬性
  │
  ├─ 發送選將請求給客戶端
  │
  └─ 玩家選擇或超時
       │
       ├─ 玩家主動選擇 → 跳過
       │
       └─ 未主動選擇（超時/隨機）
            → 觸發 on_general_not_chosen()
            → 記錄 active_skills 到玩家屬性

遊戲開始 (game_state = 1)
  │
  ├─ GameStart 事件
  │
  └─ 讀取 preselection_active_skills 屬性
       → addTriggerSkill() 加入全域 TriggerSkill
```

## Lua API

### sgs.CreatePreSelectionMetaSkill(spec)

建立一個選將階段技能。

#### 參數

| 參數 | 類型 | 必填 | 說明 |
|------|------|------|------|
| `name` | string | 是 | 技能名稱 |
| `active_skills` | string | 否 | 遊戲開始時加入全域 TriggerSkill 的技能列表，用逗號分隔 |
| `on_general_choosing` | function | 否 | 候選列表生成後觸發的回調函數 |
| `on_general_not_chosen` | function | 否 | 玩家未主動選擇時觸發的回調函數 |

#### 回調函數簽名

##### on_general_choosing(self, room, player, generals, reason)

| 參數 | 類型 | 說明 |
|------|------|------|
| `self` | skill | 技能物件 |
| `room` | Room | 房間物件 |
| `player` | ServerPlayer | 正在選將的玩家 |
| `generals` | table | 候選武將列表（可修改） |
| `reason` | string | 選將原因（如 `"for_general"`、`"for_lord"`） |

**返回值**：修改後的武將列表（table）

##### on_general_not_chosen(self, room, player, generals, chosen, reason)

| 參數 | 類型 | 說明 |
|------|------|------|
| `self` | skill | 技能物件 |
| `room` | Room | 房間物件 |
| `player` | ServerPlayer | 正在選將的玩家 |
| `generals` | table | 候選武將列表 |
| `chosen` | string | 最終選中的武將名稱 |
| `reason` | string | 選將原因 |

**返回值**：無

## 使用範例

### 範例 1：修改候選列表

```lua
local modifyListSkill = sgs.CreatePreSelectionMetaSkill{
    name = "modify_list",
    on_general_choosing = function(self, room, player, generals, reason)
        -- 確保候選列表包含特定武將
        if not table.contains(generals, "liubei") then
            table.insert(generals, "liubei")
        end
        return generals
    end
}
```

### 範例 2：未選中時獲得技能

```lua
-- 定義 active_skill（全域觸發）
local bonusSkill = sgs.CreateTriggerSkill{
    name = "bonus_skill",
    events = {sgs.DrawNCards},
    global = true,
    on_trigger = function(self, event, player, data, room)
        local draw = data:toDrawStruct()
        draw.num = draw.num + 1
        data:setValue(draw)
        return false
    end
}

-- 定義 PreSelectionMetaSkill
local notChosenSkill = sgs.CreatePreSelectionMetaSkill{
    name = "not_chosen_bonus",
    active_skills = "bonus_skill",  -- 未選中時獲得 bonus_skill
    on_general_not_chosen = function(self, room, player, generals, chosen, reason)
        -- 額外效果：獲得標記
        room:addPlayerMark(player, "not_chosen_mark")
    end
}

-- 定義武將
local testGeneral = sgs.General:new("test_general", "測試武將", "qun", 4, true, true)
testGeneral:addSkill(notChosenSkill)
```

### 範例 3：根據 reason 區分行為

```lua
local contextSkill = sgs.CreatePreSelectionMetaSkill{
    name = "context_aware",
    on_general_choosing = function(self, room, player, generals, reason)
        if reason == "for_lord" then
            -- 主公選將時的邏輯
            if not table.contains(generals, "caocao") then
                table.insert(generals, "caocao")
            end
        elseif reason == "for_general" then
            -- 普通選將時的邏輯
            -- 移除某些武將
            local newGenerals = {}
            for _, gen in ipairs(generals) do
                if gen ~= "sujiang" then
                    table.insert(newGenerals, gen)
                end
            end
            return newGenerals
        end
        return generals
    end
}
```

## reason 參數說明

| reason | 說明 |
|--------|------|
| `"for_lord"` | 主公選將 |
| `"for_general"` | 普通玩家選將 |
| `"qiexie"` | 切邪武器選將 |

## 技術細節

### 屬性存儲

- `preselection_skills`：武將的選將階段技能列表（在 `General` 類別中）
- `preselection_active_skills`：玩家的 active_skills 屬性（在選將階段記錄，遊戲開始時讀取）

### 觸發時機

- `on_general_choosing`：在 `assignGeneralsForPlayers()` 之後、發送選將請求之前觸發
- `on_general_not_chosen`：在玩家未主動選擇（超時或隨機選擇）時觸發

### active_skills 處理

1. 在 `on_general_choosing` 或 `on_general_not_chosen` 中記錄到玩家屬性
2. 在 `GameStart` 事件時讀取並調用 `addTriggerSkill()` 加入全域 TriggerSkill

## 注意事項

1. `PreSelectionMetaSkill` 定義在武將上，但觸發時玩家尚未獲得該武將
2. `active_skills` 必須是 `TriggerSkill` 類型，且需要 `global = true`
3. 修改 `generals` 列表時，確保返回有效的武將名稱
4. 避免在 `on_general_choosing` 中執行耗時操作，會影響選將流程
