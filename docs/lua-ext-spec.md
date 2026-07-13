# Lua 擴展規範 (Extension Specification)

本文件記錄 `extensions/` 下 Lua 腳本的撰寫慣例、API 用法與最佳實踐。

---

## 1. 檔案結構

```
1. 模組宣告（module() 或無）
2. Package 建立
3. 自訂輔助函數
4. 武將定義（sgs.General）
5. 技能定義（sgs.CreateTriggerSkill 等）
6. 卡牌定義（sgs.CreateSkillCard 等）
7. 技能綁定（addSkill / addRelateSkill / insertRelatedSkills）
8. 全域技能註冊（sgs.SkillList + addSkills）
9. 翻譯表（sgs.LoadTranslationTable）
```

---

## 2. 模組宣告

### 風格 A：舊式 `module()`（部分檔案使用）

```lua
module("extensions.meizl", package.seeall)
```

### 風格 B：無 `module()`（新檔案皆此風格）

直接以 `sgs.Package` 開頭，無需模組宣告。

**建議新檔案使用風格 B。**

---

## 3. Package 建立

```lua
-- 武將包
extension = sgs.Package("my_pack", sgs.Package_GeneralPack)

-- 卡牌包
extension_card = sgs.Package("my_card", sgs.Package_CardPack)

-- 省略類型參數（預設 GeneralPack）
extension = sgs.Package("my_pack")
```

### Package 類型常數

| 常數 | 說明 |
|------|------|
| `sgs.Package_GeneralPack` | 武將包 |
| `sgs.Package_CardPack` | 卡牌包 |

### 多 Package 慣例

```lua
extension = sgs.Package("main_pack", sgs.Package_GeneralPack)
extension_sub = sgs.Package("main_pack_sub", sgs.Package_GeneralPack)
extension_card = sgs.Package("main_pack_card", sgs.Package_CardPack)
```

Package 變數賦值後即向引擎註冊，無需手動呼叫 `addPackage`。

---

## 4. 武將定義 (General)

### 建構子

```lua
sgs.General(package, name, kingdom, max_hp, is_female, hidden)
```

| 參數 | 類型 | 說明 |
|------|------|------|
| `package` | `sgs.Package` | 所屬 Package 物件 |
| `name` | `string` | 內部名稱（同時為翻譯表鍵值） |
| `kingdom` | `string` | 勢力：`"wei"` `"shu"` `"wu"` `"qun"` `"god"` `"magic"` 等 |
| `max_hp` | `number` / `string` | 體力上限 |
| `is_female` | `boolean` | `true` 女性，省略/`false` 男性 |
| `hidden` | `boolean` | 是否隱藏（預設 `false`） |

### 範例

```lua
my_guanyu = sgs.General(extension, "my_guanyu", "shu", 4, true)
my_caocao = sgs.General(extension, "my_caocao$", "wei", 4)       -- 主公（尾綴 $）
my_god = sgs.General(extension, "my_god", "god", 3, false, true) -- 隱藏
```

### 命名慣例

`{包前綴}_{武將名}`，例如 `meizldaqiao`、`blood_zhaozilong`、`sk_zhangning`。

主公標記在內部名稱尾綴 `$`。

---

## 5. 技能定義

### 5.1 TriggerSkill（最常用）

```lua
skill_name = sgs.CreateTriggerSkill {
    name = "skill_name",
    frequency = sgs.Skill_NotFrequent,   -- 頻率常數
    events = { sgs.Damaged },            -- 事件表（單一事件可用 number）
    priority = 3,                        -- 優先級（可選）
    global = true,                       -- 全域觸發器（可選）
    view_as_skill = some_vs,             -- 關聯轉化技（可選）
    limit_mark = "@mark_name",           -- 限定技標記（可選）
    waked_skills = "skill1,skill2",      -- 覺醒後獲得技能（可選）
    change_skill = true,                 -- 變換技能（可選）
    limited_skill = true,                -- 限定技（可選）
    hide_skill = true,                   -- 隱藏技能（可選）
    shiming_skill = true,                -- 使命技（可選）
    guhuo_type = "H",                    -- 蠱惑對話框類型（可選）
    juguan_type = "@@pattern",           -- 距關對話框（可選）
    dynamic_frequency = function(self, player) ... end,  -- 動態頻率（可選）
    on_trigger = function(self, event, player, data)
        -- 4 參數風格：self, event, player, data
        local room = player:getRoom()
        -- ...
        return false  -- 返回 false 不中斷事件鏈；true 中斷
    end,
    can_trigger = function(self, target)
        return target and target:isAlive()
    end,
    can_wake = function(self, player)
        return player:getMark("skill_name") == 0
    end,
}
```

**也可以使用 5 參數風格（room 直接傳入）：**

```lua
on_trigger = function(self, event, player, data, room)
    -- room 已從 player:getRoom() 取得，可直接使用
end
```

### 頻率常數

| 常數 | 說明 |
|------|------|
| `sgs.Skill_NotFrequent` | 一般主動技（預設） |
| `sgs.Skill_Frequent` | 可選發動 |
| `sgs.Skill_Compulsory` | 鎖定技 |
| `sgs.Skill_Limited` | 限定技（須搭配 `limit_mark`） |
| `sgs.Skill_Wake` | 覺醒技（須搭配 `waked_skills`） |

### 5.2 TriggerV2Skill（V2 技能系統）

```lua
skill_name = sgs.CreateTriggerV2Skill {
    name = "skill_name",
    frequency = sgs.Skill_NotFrequent,
    events = { sgs.Damaged },
    global = true,
    view_as_skill = some_vs,
    limit_mark = "@mark",
    priority = 3,
    waked_skills = "skill1",            -- 覺醒後獲得技能
    change_skill = true,                -- 變換技能
    hide_skill = true,                  -- 隱藏技能
    shiming_skill = true,               -- 使命技
    base_amount = 1,                    -- 基礎數值
    limit_scope = 1,                    -- 限制範圍
    max_usage_limit = 1,                -- 最大使用次數
    phase_name = "play",                -- 階段名稱
    -- V2 特有回調
    on_record = function(self, event, player, data) end,           -- 記錄階段
    on_cost = function(self, event, player, data, ask_who) ... end,      -- 消耗階段
    on_pay = function(self, event, player, data, ask_who) ... end,      -- 支付階段
    on_effect = function(self, event, player, data, ask_who) ... end,   -- 效果階段
    on_effect_target = function(self, event, player, data, ask_who) ... end,  -- 目標效果
    on_turn_broken = function(self, event, player, data, ask_who) ... end,   -- 中斷回調
    can_trigger = function(self, target) ... end,
    check_custom_usage = function(self, player, data) ... end,
    on_shiming_success = function(self, event, player, data, ask_who) ... end,
    on_shiming_fail = function(self, event, player, data, ask_who) ... end,
    dynamic_frequency = function(self, player) ... end,
}
```

參考 `docs/TriggerV2Skill系統說明.md` 獲取完整 V2 流程說明。

### 5.3 ViewAsSkill（轉化技）

```lua
skill_name = sgs.CreateViewAsSkill {
    name = "skill_name",
    n = 2,                                    -- 所需卡牌張數
    response_pattern = "@@pattern",            -- 回應模式（可選）
    response_or_use = true,                    -- 回應或使用（可選）
    expand_pile = "pile_name",                 -- 擴展牌堆名（可選）
    frequency = sgs.Skill_NotFrequent,         -- 頻率（可選）
    limit_mark = "@mark",                      -- 限定標記（可選）
    guhuo_type = "H",                          -- 蠱惑類型（可選）
    view_filter = function(self, selected, to_select)
        return not to_select:isEquipped()
    end,
    view_as = function(self, cards)
        if #cards ~= 2 then return nil end
        local card = sgs.SkillCard():clone()
        card:addSubcard(cards[1])
        card:addSubcard(cards[2])
        card:setSkillName(self:objectName())
        return card
    end,
    enabled_at_play = function(self, player)
        return not player:hasUsed("#card_name")
    end,
    enabled_at_response = function(self, player, pattern)
        return pattern == "@@some_pattern"
    end,
    enabled_at_nullification = function(self, player)
        return true
    end,
    should_be_visible = function(self, player)
        return true
    end,
}
```

### 5.4 OneCardViewAsSkill（單卡轉化）

```lua
skill_name = sgs.CreateOneCardViewAsSkill {
    name = "skill_name",
    filter_pattern = ".|black",                -- 過濾模式
    view_filter = function(self, to_select)    -- 或自訂過濾
        return to_select:isRed()
    end,
    view_as = function(self, card)
        local slash = sgs.Sanguosha:cloneCard("slash", card:getSuit(), card:getNumber())
        slash:setSkillName(self:objectName())
        slash:addSubcard(card:getId())
        return slash
    end,
    enabled_at_play = function(self, player)
        return not player:hasUsed("#slash")
    end,
}
```

### 5.5 ZeroCardViewAsSkill（零卡轉化）

```lua
skill_name = sgs.CreateZeroCardViewAsSkill {
    name = "skill_name",
    view_as = function(self)
        return skill_card:clone()
    end,
    enabled_at_play = function(self, player)
        return not player:hasUsed("#skill_name") and not player:isKongcheng()
    end,
    enabled_at_response = function(self, player, pattern)
        return pattern == "@@pattern"
    end,
}
```

### 5.6 CreateSkillCard

```lua
skill_card = sgs.CreateSkillCard {
    name = "skill_card_name",
    skill_name = "skill_name",                 -- 關聯技能名（可選）
    target_fixed = false,                      -- 無需目標
    will_throw = true,                         -- 使用後棄置
    handling_method = sgs.Card_MethodNone,     -- 處理方式（可選）
    can_recast = true,                         -- 可重鑄（可選）
    mute = true,                               -- 靜音（可選）
    filter = function(self, targets, to_select, player)
        if #targets > 0 then return false end
        return to_select:objectName() ~= player:objectName()
    end,
    feasible = function(self, targets)
        return #targets == 1
    end,
    about_to_use = function(self, room, card_use)  -- 即將使用（可選）
        card_use.from:getRoom():broadcastSkillInvoke("skill_name")
    end,
    on_use = function(self, room, source, targets)  -- 使用時
    end,
    on_effect = function(self, effect)              -- 對目標生效
        local room = effect.from:getRoom()
        room:damage(sgs.DamageStruct(...))
    end,
    on_validate = function(self, card_use)          -- 驗證（可選）
    end,
    on_validate_in_response = function(self, card_use)  -- 回應驗證（可選）
    end,
}
```

### 5.7 DistanceSkill（距離修正）

```lua
skill_name = sgs.CreateDistanceSkill {
    name = "#skill_name",
    correct_func = function(self, from, to)
        if to:hasSkill("#skill_name") and not to:faceUp() then
            return 1
        end
        return 0
    end,
    fixed_func = function(self, from, to)           -- 固定距離（可選）
        return 2
    end,
}
```

### 5.8 MaxCardsSkill（手牌上限修正）

```lua
skill_name = sgs.CreateMaxCardsSkill {
    name = "#skill_name",
    extra_func = function(self, target)
        if target:hasSkill(self:objectName()) then
            return 2
        end
        return 0
    end,
    fixed_func = function(self, target)             -- 固定上限（可選）
        return 4
    end,
}
```

### 5.9 TargetModSkill（目標修正）

```lua
skill_name = sgs.CreateTargetModSkill {
    name = "skill_name",
    pattern = "Slash",                               -- 卡牌模式
    frequency = sgs.Skill_Compulsory,
    residue_func = function(self, from, card)        -- 額外使用次數
        if from:hasSkill(self:objectName()) then return 1 end
        return 0
    end,
    distance_limit_func = function(self, from, card) -- 距離限制
        return 99
    end,
    extra_target_func = function(self, from, card)   -- 額外目標
        if from:hasSkill(self:objectName()) then return 1 end
        return 0
    end,
}
```

### 5.10 ProhibitSkill（禁止指定目標）

```lua
skill_name = sgs.CreateProhibitSkill {
    name = "skill_name",
    frequency = sgs.Skill_Compulsory,
    is_prohibited = function(self, from, to, card)
        return to:hasSkill("skill_name") and card:isKindOf("Lightning")
    end,
}
```

### 5.11 FilterSkill（卡牌過濾）

```lua
skill_name = sgs.CreateFilterSkill {
    name = "skill_name",
    view_filter = function(self, card)
        return card:isRed()
    end,
    view_as = function(self, card)
        local new = sgs.Sanguosha:cloneCard("fire_slash", card:getSuit(), card:getNumber())
        new:addSubcard(card:getId())
        new:setSkillName(self:objectName())
        return new
    end,
}
```

### 5.12 InvaliditySkill（技能無效化）

```lua
skill_name = sgs.CreateInvaliditySkill {
    name = "skill_name",
    skill_valid = function(self, player, skill)
        if skill:objectName() == "wusheng" then return false end
        return true
    end,
}
```

### 5.13 AttackRangeSkill（攻擊範圍修正）

```lua
skill_name = sgs.CreateAttackRangeSkill {
    name = "skill_name",
    extra_func = function(self, player)
        if player:hasSkill(self:objectName()) then return 1 end
        return 0
    end,
    fixed_func = function(self, player)          -- 固定範圍（可選）
        return 3
    end,
}
```

### 5.14 CardLimitSkill（卡牌使用限制）

```lua
skill_name = sgs.CreateCardLimitSkill {
    name = "skill_name",
    limit_list = function(self, player)           -- 傳回禁用的卡牌列表
        return { "Slash", "SavageAssault" }
    end,
    limit_pattern = function(self, player, to)    -- 過濾模式
        return ".|spade"
    end,
    limit_reason = function(self, player, card)   -- 禁用原因
        return "skill_name_limit"
    end,
}
```

### 5.15 ViewAsEquipSkill（裝備轉化）

```lua
skill_name = sgs.CreateViewAsEquipSkill {
    name = "skill_name",
    view_as_equip = function(self, player)
        local card = sgs.Sanguosha:cloneCard("QinggangSword", sgs.Card_NoSuit, 0)
        card:setSkillName(self:objectName())
        return card
    end,
}
```

### 5.16 簡化版技能（sgs_ex.lua 封裝）

#### MasochismSkill（受傷觸發）

```lua
skill_name = sgs.CreateMasochismSkill {
    name = "skill_name",
    on_damaged = function(self, player, damage, room)
        -- 相當於 TriggerSkill + events = { sgs.Damaged }
    end,
}
```

#### PhaseChangeSkill（階段變更）

```lua
skill_name = sgs.CreatePhaseChangeSkill {
    name = "skill_name",
    on_phasechange = function(self, player, room)
        -- 相當於 TriggerSkill + events = { sgs.EventPhaseStart }
    end,
}
```

#### DrawCardsSkill（摸牌階段）

```lua
skill_name = sgs.CreateDrawCardsSkill {
    name = "skill_name",
    is_initial = true,                                   -- 初始手牌階段（可選）
    draw_num_func = function(self, player, num, room)    -- 返回調整後的摸牌數
        return num + 1
    end,
}
```

#### GameStartSkill（遊戲開始）

```lua
skill_name = sgs.CreateGameStartSkill {
    name = "skill_name",
    on_gamestart = function(self, player, room)
        -- 相當於 TriggerSkill + events = { sgs.GameStart }
    end,
}
```

#### RetrialSkill（判定修改）

```lua
skill_name = sgs.CreateRetrialSkill {
    name = "skill_name",
    exchange = true,                                     -- 是否交換判定牌
    on_retrial = function(self, player, judge, room)
        local card = room:getCard(room:drawCard())
        return card                                      -- 傳回替代的 Card
    end,
}
```

### 5.17 AnytimeSkill（任意時機技能）

```lua
skill_name = sgs.CreateAnytimeSkill {
    name = "skill_name",
    frequency = sgs.Skill_NotFrequent,
    can_trigger = function(self, player)
        return player:isAlive()
    end,
    on_trigger = function(self, room, player)
        -- 技能實際執行邏輯
        return false
    end,
}
```

AnytimeSkill 不由事件觸發，由玩家主動按下技能按鈕觸發。  
參考 `docs/anytime-skill.md`。

### 5.18 BattleArraySkill（陣法技）

```lua
skill_name = sgs.CreateBattleArraySkill {
    name = "skill_name",
    array_type = "formation",                           -- "formation" 或 "siege"
    frequency = sgs.Skill_NotFrequent,
    on_summon = function(self, room, player, players)
        -- 陣法形成/解除時觸發
    end,
}
```

### 5.19 PreSelectionMetaSkill（選將階段）

```lua
skill_name = sgs.CreatePreSelectionMetaSkill {
    name = "skill_name",
    active_skills = "skill1,skill2",
    on_general_choosing = function(self, player) end,
    on_general_not_chosen = function(self, player) end,
}
```

---

## 6. 卡牌定義

### 6.1 BasicCard（基本牌）

```lua
card = sgs.CreateBasicCard {
    name = "my_card",
    class_name = "my_card",          -- 類別名（可選，預設同 name）
    suit = sgs.Card_Spade,           -- 花色（可選）
    number = 1,                      -- 點數（可選）
    target_fixed = true,
    can_recast = true,               -- 可重鑄（可選）
    damage_card = true,              -- 是否為傷害牌（可選）
    is_gift = true,                  -- 贈禮牌（可選）
    is_transferable = true,          -- 可轉移（可選）
    single_target = true,            -- 單目標牌（可選）
    filter = function(self, targets, to_select)
        return #targets == 0
    end,
    feasible = function(self, targets)
        return #targets == 1
    end,
    available = function(self, player)  -- 可用性檢查（可選）
        return true
    end,
    about_to_use = function(self, room, card_use) end,
    on_use = function(self, room, source, targets) end,
    on_effect = function(self, effect) end,
    on_validate = function(self, card_use) end,
    on_validate_in_response = function(self, card_use) end,
}
```

### 6.2 TrickCard（錦囊牌）

```lua
card = sgs.CreateTrickCard {
    name = "my_trick",
    suit = sgs.Card_Heart,
    number = 1,
    subclass = sgs.LuaTrickCard_TypeNormal,          -- 錦囊子類別
    target_fixed = false,
    can_recast = true,
    damage_card = true,
    is_gift = false,
    is_transferable = true,
    single_target = true,
    movable = true,                                   -- 延時錦囊可移動
    is_cancelable = function(self, effect) return true end,
    filter = function(self, targets, to_select)
        return #targets == 0 and sgs.Self:canPindian(to_select)
    end,
    feasible = function(self, targets)
        return #targets == 1
    end,
    available = function(self, player) end,
    on_effect = function(self, effect) end,
    on_use = function(self, room, source, targets) end,
    on_nullified = function(self, target) end,
}
```

### 錦囊子類別常數

| 常數 | 說明 |
|------|------|
| `sgs.LuaTrickCard_TypeNormal` | 普通錦囊（預設） |
| `sgs.LuaTrickCard_TypeDelayedTrick` | 延時錦囊（須設 `movable`） |
| `sgs.LuaTrickCard_TypeAOE` | 群體錦囊（自動設定 `target_fixed=true`） |
| `sgs.LuaTrickCard_TypeGlobalEffect` | 全場錦囊（自動設定 `target_fixed=true`） |

### 6.3 EquipCard（裝備牌）

#### Weapon（武器）

```lua
weapon = sgs.CreateWeapon {
    name = "my_weapon",
    suit = sgs.Card_Spade,
    number = 5,
    range = 3,                                        -- 攻擊範圍
    equip_skill = some_skill_object,                  -- 裝備技能（可選）
    on_install = function(self, player) end,           -- 裝備時（可選）
    on_uninstall = function(self, player) end,         -- 卸裝時（可選）
}
```

#### Armor（防具）

```lua
armor = sgs.CreateArmor {
    name = "my_armor",
    suit = sgs.Card_Club,
    number = 2,
    equip_skill = some_skill_object,
    on_install = function(self, player) end,
    on_uninstall = function(self, player) end,
}
```

#### OffensiveHorse（進攻馬）

```lua
horse = sgs.CreateOffensiveHorse {
    name = "my_horse",
    suit = sgs.Card_Heart,
    number = 5,
    correct = -1,                                     -- 距離修正值（預設 -1）
}
```

#### DefensiveHorse（防禦馬）

```lua
horse = sgs.CreateDefensiveHorse {
    name = "my_horse",
    suit = sgs.Card_Diamond,
    number = 5,
    correct = 1,                                      -- 距離修正值（預設 1）
}
```

#### Treasure（寶物）

```lua
treasure = sgs.CreateTreasure {
    name = "my_treasure",
    suit = sgs.Card_Heart,
    number = 5,
    equip_skill = some_skill_object,
}
```

### 花色常數

| 常數 | 說明 |
|------|------|
| `sgs.Card_Spade` | 黑桃♠ |
| `sgs.Card_Club` | 梅花♣ |
| `sgs.Card_Heart` | 紅心♥ |
| `sgs.Card_Diamond` | 方塊♦ |
| `sgs.Card_NoSuit` | 無花色 |
| `sgs.Card_SuitToBeDecided` | 待定（建立時決定） |

---

## 7. 技能綁定與註冊

### 7.1 綁定技能到武將

```lua
-- 直接綁定技能物件
my_general:addSkill(skill_object)
my_general:addSkill(another_skill)

-- 綁定內建技能（字串）
my_general:addSkill("wusheng")
my_general:addSkill("longdan")

-- 關聯技能（不直接持有）
my_general:addRelateSkill("longhun")

-- 綁定卡牌
my_general:addCard(my_card)
```

### 7.2 關聯子技能

```lua
-- DistanceSkill / MaxCardsSkill 等修正技能須以 # 前綴關聯
extension:insertRelatedSkills("parent_skill", "#child_skill1")
extension:insertRelatedSkills("parent_skill", "#child_skill2")
```

### 7.3 動態獲得/移除技能

```lua
room:handleAcquireDetachSkills(player, "skill_name")      -- 獲得
room:handleAcquireDetachSkills(player, "-skill_name")     -- 移除
room:handleAcquireDetachSkills(player, "skill1,skill2")   -- 批量
```

### 7.4 全域技能註冊

```lua
local skills = sgs.SkillList()
if not sgs.Sanguosha:getSkill("skill_name") then
    skills:append(skill_object)
end
if not sgs.Sanguosha:getSkill("another_skill") then
    skills:append(another_skill)
end
sgs.Sanguosha:addSkills(skills)
```

### 7.5 Scenario 註冊

```lua
scenario = sgs.CreateScenario {
    name = "scenario_name",
    expose = true,                                      -- 是否暴露身份（可選）
    roles = {                                           -- 角色配置
        lord = "caocao",
        loyalist = "xiahoudun",
        rebel = "guanyu",
        renegade = "zhaoyun",
    },
    rule = scenario_rule,                               -- 場景規則（可選）
}

scenario_rule = sgs.CreateScenarioRule {
    scenario = "scenario_name",
    events = { sgs.GameStart },
    priority = 1,
    can_trigger = function(self, player) end,
    on_trigger = function(self, event, player, data) end,
}
```

---

## 8. 事件系統

### 8.1 常用事件常數

| 事件 | 資料類型 | 說明 |
|------|----------|------|
| `sgs.GameStart` | — | 遊戲開始 |
| `sgs.EventPhaseStart` | — | 階段開始 |
| `sgs.EventPhaseEnd` | — | 階段結束 |
| `sgs.EventPhaseChanging` | `sgs.QVariant` → `sgs.PhaseChangeStruct` | 階段切換 |
| `sgs.DrawNCards` | `sgs.QVariant` → `sgs.DrawNCards` | 摸牌階段 |
| `sgs.CardUsed` | `sgs.QVariant` → `sgs.CardUseStruct` | 卡牌使用 |
| `sgs.CardFinished` | `sgs.QVariant` → `sgs.CardUseStruct` | 卡牌結算結束 |
| `sgs.CardEffected` | `sgs.QVariant` → `sgs.CardEffectStruct` | 卡牌生效 |
| `sgs.Damage` | `sgs.QVariant` → `sgs.DamageStruct` | 傷害造成 |
| `sgs.DamageInflicted` | `sgs.QVariant` → `sgs.DamageStruct` | 傷害將受 |
| `sgs.DamageCaused` | `sgs.QVariant` → `sgs.DamageStruct` | 傷害將致 |
| `sgs.Damaged` | `sgs.QVariant` → `sgs.DamageStruct` | 受傷後 |
| `sgs.Dying` | `sgs.QVariant` → `sgs.DyingStruct` | 瀕死 |
| `sgs.AskForPeaches` | `sgs.QVariant` → `sgs.DyingStruct` | 求桃 |
| `sgs.Death` | `sgs.QVariant` → `sgs.DeathStruct` | 死亡 |
| `sgs.CardsMoveOneTime` | `sgs.QVariant` → `sgs.CardsMoveOneTimeStruct` | 卡牌移動 |
| `sgs.BeforeCardsMove` | `sgs.QVariant` → `sgs.CardsMoveOneTimeStruct` | 卡牌移動前 |
| `sgs.TurnStart` | — | 回合開始 |
| `sgs.EventLoseSkill` | `sgs.QVariant` → `string` | 失去技能 |
| `sgs.EventAcquireSkill` | `sgs.QVariant` → `string` | 獲得技能 |
| `sgs.TargetConfirming` | `sgs.QVariant` → `sgs.CardUseStruct` | 目標確認中 |
| `sgs.TargetConfirmed` | `sgs.QVariant` → `sgs.CardUseStruct` | 目標已確認 |
| `sgs.PreCardUsed` | `sgs.QVariant` → `sgs.CardUseStruct` | 卡牌使用前 |
| `sgs.Pindian` | `sgs.QVariant` → `sgs.PindianStruct` | 拼點 |
| `sgs.AskForRetrial` | `sgs.QVariant` → `sgs.JudgeStruct` | 判定修改前 |
| `sgs.ChoiceMade` | `sgs.QVariant` → `string` | 玩家做出選擇 |
| `sgs.Run` | `sgs.QVariant` → `sgs.RunStruct` | 鏖戰模式 |
| `sgs.EventStart` | — | 遊戲流程開始 |

### 8.2 資料轉換

```lua
local damage = data:toDamage()
local effect = data:toCardEffect()
local move = data:toMoveOneTime()
local use = data:toCardUse()
local judge = data:toJudge()
local dying = data:toDying()
local death = data:toDeath()
local draw = data:toDraw()
local recover = data:toRecover()
local pindian = data:toPindian()
local phase = data:toPhaseChange()
```

---

## 9. 常用 Room 方法

```lua
-- 廣播與日誌
room:broadcastSkillInvoke("skill_name")              -- 播放技能音效
room:broadcastSkillInvoke("skill_name", n)            -- 指定語音編號
room:notifySkillInvoked(player, "skill_name")         -- 通知技能發動
room:sendCompulsoryTriggerLog(player, "skill_name")   -- 鎖定技日誌
room:sendLog(log_message)                             -- 發送日誌
room:doLightbox("$animation_key", duration)           -- 播放動畫文字

-- 詢問玩家
room:askForSkillInvoke(player, "skill_name")                  -- 是否發動
room:askForChoice(player, "skill_name", "opt1+opt2")          -- 選擇選項
room:askForPlayerChosen(player, players, "skill_name")        -- 選擇玩家
room:askForUseCard(player, pattern, prompt)                   -- 要求使用卡牌
room:askForDiscard(player, "skill_name", min, max)            -- 要求棄牌
room:askForAG(player, card_ids, refusable, prompt)            -- 選牌（手推車）
room:askForGuanxing(player, cards, type)                      -- 觀星
room:askForPindian(player, "skill_name", target)              -- 拼點

-- 屬性修改
room:setPlayerProperty(player, "hp", sgs.QVariant(n))
room:setPlayerProperty(player, "maxhp", sgs.QVariant(n))

-- 體力與標記
room:loseHp(player, n, true, source, "skill_name")            -- 流失體力
room:damage(damage_struct)                                    -- 造成傷害
room:recover(player, recover_struct)                          -- 回復體力
room:addPlayerMark(player, "mark_name")                       -- 增加標記
room:removePlayerMark(player, "mark_name")                    -- 移除標記
room:setPlayerMark(player, "mark_name", n)                    -- 設定標記
room:getMark("mark_name")                                     -- 獲取標記值

-- 覺醒
room:changeMaxHpForAwakenSkill(player, delta, "skill_name")
room:handleAcquireDetachSkills(player, "new_skill")

-- 卡牌操作
room:moveCardTo(card, target, place, reason)                  -- 移動卡牌
room:moveCardsAtomic(move, false)                             -- 原子移動
room:obtainCard(player, card)                                 -- 獲得卡牌
room:throwCard(card, reason, nil)                             -- 棄牌
room:drawCard()                                               -- 抽一張牌
room:setCardFlag(card, "flag_name")                           -- 設定卡牌標記

-- 手推車選牌
room:fillAG(card_ids, player)
room:clearAG(player)
room:takeAG(player, card_id)

-- 標籤
room:setTag("tag_name", sgs.QVariant(value))
room:getTag("tag_name")

-- 其他
room:useCard(card_use_struct)                                 -- 使用卡牌
room:retrial(card, player, judge, "skill_name", exchange)     -- 改判
room:setPlayerCardLimitation(player, "use,response", pattern) -- 設定卡牌限制
room:removePlayerCardLimitation(player, "use,response", pattern)
room:showCard(player, card_ids)                               -- 明置卡牌
room:setCardTip(id, "tip_text")                               -- 設定卡牌提示
room:clearCardTip(id)                                          -- 清除卡牌提示
player:addToPile("pile_name", cards, false)                   -- 加入牌堆
```

### 玩家階段常數

```lua
sgs.Player_Start        -- 開始階段
sgs.Player_Judge        -- 判定階段
sgs.Player_Draw         -- 摸牌階段
sgs.Player_Play         -- 出牌階段
sgs.Player_Discard      -- 棄牌階段
sgs.Player_Finish       -- 結束階段
sgs.Player_NotActive    -- 非活躍階段（回合外）
```

### 卡牌位置常數

```lua
sgs.Player_PlaceHand          -- 手牌區
sgs.Player_PlaceEquip         -- 裝備區
sgs.Player_PlaceDelayedTrick  -- 判定區
sgs.Player_PlaceSpecial       -- 特殊牌堆
sgs.Player_PlaceTable         -- 桌上（處理中）
sgs.Player_DiscardPile        -- 棄牌堆
sgs.Player_DrawPile           -- 牌堆
sgs.Player_PlaceJudge         -- 判定牌位置
```

---

## 10. 翻譯表 (Translation Table)

### 格式

```lua
sgs.LoadTranslationTable {
    -- Package 名稱
    ["package_name"] = "顯示名稱",

    -- 武將
    ["internal_name"] = "顯示名",
    ["&internal_name"] = "簡稱（選將框顯示）",
    ["#internal_name"] = "稱號",
    ["~internal_name"] = "死亡台詞",
    ["designer:internal_name"] = "設計者",
    ["cv:internal_name"] = "配音",
    ["illustrator:internal_name"] = "插畫",

    -- 技能
    ["skill_name"] = "技能名",
    [":skill_name"] = "技能描述",
    ["$skill_name"] = "技能台詞",
    ["$skill_name1"] = "台詞1",
    ["$skill_name2"] = "台詞2",
    ["skill_name_Card"] = "技能卡牌名",
    ["#skill_name"] = "提示文字",
    ["~skill_name"] = "操作提示",
    ["@skill_name"] = "標記提示",
    ["@@skill_name"] = "雙標記提示",
    ["#skill_name_msg"] = "日誌訊息",
    ["#skill_name_log"] = "日誌格式",
}
```

### 描述格式慣例

```lua
-- HTML 顏色標籤
[":SomeSkill"] = '<font color="blue"><b>锁定技，</b></font>...'
[":OtherSkill"] = '<font color="green"><b>出牌阶段限一次，</b></font>...'
[":LordSkill"] = '<font color="orange"><b>主公技，</b></font>...'
[":WakeSkill"] = '<font color="purple"><b>觉醒技，</b></font>...'

-- 多行描述（使用 \ 折行）
[":SomeSkill"] = "每当你受到1点伤害后，你可以观看牌堆顶的两张牌，\
将其中一张放置在合理的位置。"
```

### 佈局慣例

- 小型檔案：單一 `LoadTranslationTable` 統一管理
- 大型檔案：多個 `LoadTranslationTable` 分組（Package 名稱、各武將）
- 亦可在各武將定義後緊接其翻譯表

---

## 11. 日誌系統 (LogMessage)

```lua
local log = sgs.LogMessage()
log.type = "#skill_name_trigger"        -- 日誌類型（對應翻譯表鍵值）
log.from = player                       -- 來源玩家
log.to:append(target)                   -- 目標玩家列表
log.card_str = card:toString()          -- 卡牌字串
log.arg = "skill_name"                  -- 參數1
log.arg2 = "value"                      -- 參數2
room:sendLog(log)
```

### 日誌類型慣例

| 類型格式 | 說明 |
|----------|------|
| `#skill_name_trigger` | 技能觸發 |
| `#skill_name_effect` | 技能效果 |
| `#skill_name_log` | 技能日誌 |
| `#skill_name_msg` | 技能訊息 |
| `#SkillAvoid` | 技能迴避 |
| `#SkillAvoidFrom` | 來源技能迴避 |
| `#ChoiceWithTarget` | 含目標的選擇 |
| `#choice` | 一般選擇 |
| `#CardNullified` | 卡牌無效化 |
| `#NoJudgeAreaAvoid` | 無判定區迴避 |
| `#UseCard` | 使用卡牌 |

---

## 12. 輔助函數（定義於 extra.lua）

| 函數 | 簽章 | 說明 |
|------|------|------|
| `HeavenMove` | `(player, id, movein, private_pile_name?)` | 卡牌偽移動至私人牌堆 |
| `Table2IntList` | `(theTable)` → `sgs.IntList` | Lua Table 轉 IntList |
| `SendComLog` | `(self, player, n, invoke?)` | 發送鎖定技提示 |
| `ChoiceLog` | `(player, choice, to?, skillName?)` | 發送選擇日誌 |
| `Set` | `(list)` → `table` | 列表轉集合 |
| `getPos` | `(table, value)` → `int` | 陣列索引查找 |
| `generateAllCardObjectNameTablePatterns` | `()` → `string[]` | 所有卡牌名稱列表 |
| `UniversalCardDisplayMove` | `(ids, movein, player, pile_name?)` | 通用明置牌機制 |

---

## 13. 命名慣例

| 類別 | 模式 | 範例 |
|------|------|------|
| 武將變數 | `{前綴}_{名稱}` | `blood_zhaozilong`、`meizldaqiao` |
| 技能變數 | `{前綴}_{技能}` | `blood_gudan`、`sk_leiji` |
| 卡牌物件 | `{名稱}Card` | `blood_hjcard`、`JiuziCard` |
| ViewAs 技能 | `{名稱}VS` / `{名稱}_vs` | `fatewangzheVS`、`Jiuzivs` |
| 修正技能 | `#{技能名}D` | `#LuaTiebiD` |
| 全域技能 | `#{描述}` | `#shikistart`、`#yinyangDetach`、`#universal_card_display_global` |
| 舊式命名 | `Lua{英譯}` | `LuaSP_guanyu`、`Lua_caoren` |
| 神將 | `God{技能}` | `GodXiaoshi`、`GodMeizi` |

---

## 14. 常用 Room 相關資料結構

### DamageStruct

```lua
local dmg = sgs.DamageStruct()
dmg.from = source                     -- 傷害來源
dmg.to = target                       -- 受害者
dmg.damage = 1                        -- 傷害量
dmg.nature = sgs.DamageStruct_Normal  -- 屬性（Normal / Fire / Thunder）
dmg.card = card                       -- 關聯卡牌
dmg.reason = "skill_name"             -- 原因
room:damage(dmg)
```

### RecoverStruct

```lua
local rec = sgs.RecoverStruct()
rec.recover = 1                       -- 回復量
rec.who = source                      -- 回復來源
room:recover(player, rec)
```

### CardEffectStruct

```lua
-- data:toCardEffect()
local effect = data:toCardEffect()
-- effect.from, effect.to, effect.card, effect.multiple
```

### CardUseStruct

```lua
local use = sgs.CardUseStruct(card, source, target)
room:useCard(use)
-- data:toCardUse() → use.from, use.to, use.card, use.nullified_list
```

### CardsMoveOneTimeStruct

```lua
local move = data:toMoveOneTime()
-- move.from, move.to, move.card_ids, move.from_places, move.to_place, move.reason
```

### DyingStruct

```lua
local dying = data:toDying()
-- dying.who, dying.damage
```

### JudgeStruct

```lua
local judge = data:toJudge()
-- judge.who, judge.card, judge.pattern, judge.good, judge.reason
```

### PindianStruct

```lua
local pindian = data:toPindian()
-- pindian.from, pindian.to, pindian.from_card, pindian.to_card, pindian.from_number, pindian.to_number
```

### DeathStruct

```lua
local death = data:toDeath()
-- death.who, death.damage
```

### CardMoveReason

```lua
local reason = sgs.CardMoveReason(
    sgs.CardMoveReason_S_REASON_USE,     -- 原因類型
    player:objectName(),                  -- 來源
    "skill_name",                         -- 技能名
    ""                                    -- 事件名
)
```

### 移動原因常數

| 常數 | 說明 |
|------|------|
| `sgs.CardMoveReason_S_REASON_USE` | 使用卡牌 |
| `sgs.CardMoveReason_S_REASON_PUT` | 放置 |
| `sgs.CardMoveReason_S_REASON_GOTCARD` | 獲得卡牌 |
| `sgs.CardMoveReason_S_REASON_DISCARD` | 棄置 |
| `sgs.CardMoveReason_S_REASON_TRANSFER` | 轉移 |
| `sgs.CardMoveReason_S_REASON_NATURAL_ENTER` | 自然進入 |
| `sgs.CardMoveReason_S_MASK_BASIC_REASON` | 基礎原因遮罩 |

---

## 15. 技能類型速查表

| 建構子 | 用途 | 檔案位置 |
|--------|------|----------|
| `sgs.CreateTriggerSkill` | 事件觸發技 | `lua/sgs_ex.lua:4` |
| `sgs.CreateTriggerV2Skill` | V2 觸發技 | `lua/sgs_ex.lua:62` |
| `sgs.CreateViewAsSkill` | 轉化技（n 張牌） | `lua/sgs_ex.lua:788` |
| `sgs.CreateOneCardViewAsSkill` | 單牌轉化 | `lua/sgs_ex.lua:816` |
| `sgs.CreateZeroCardViewAsSkill` | 零牌轉化 | `lua/sgs_ex.lua:837` |
| `sgs.CreateSkillCard` | 技能卡牌 | `lua/sgs_ex.lua:389` |
| `sgs.CreateBasicCard` | 基本牌 | `lua/sgs_ex.lua:431` |
| `sgs.CreateTrickCard` | 錦囊牌 | `lua/sgs_ex.lua:727` |
| `sgs.CreateEquipCard` | 裝備牌（通用） | `lua/sgs_ex.lua:849` |
| `sgs.CreateWeapon` | 武器 | `lua/sgs_ex.lua:893` |
| `sgs.CreateArmor` | 防具 | `lua/sgs_ex.lua:898` |
| `sgs.CreateOffensiveHorse` | 進攻馬 | `lua/sgs_ex.lua:903` |
| `sgs.CreateDefensiveHorse` | 防禦馬 | `lua/sgs_ex.lua:908` |
| `sgs.CreateTreasure` | 寶物 | `lua/sgs_ex.lua:913` |
| `sgs.CreateDistanceSkill` | 距離修正 | `lua/sgs_ex.lua:195` |
| `sgs.CreateMaxCardsSkill` | 手牌上限修正 | `lua/sgs_ex.lua:210` |
| `sgs.CreateTargetModSkill` | 目標修正 | `lua/sgs_ex.lua:225` |
| `sgs.CreateProhibitSkill` | 禁止指定目標 | `lua/sgs_ex.lua:165` |
| `sgs.CreateProhibitPindianSkill` | 禁止拼點 | `lua/sgs_ex.lua:174` |
| `sgs.CreateFilterSkill` | 卡牌過濾/轉化 | `lua/sgs_ex.lua:183` |
| `sgs.CreateInvaliditySkill` | 技能無效化 | `lua/sgs_ex.lua:241` |
| `sgs.CreateAttackRangeSkill` | 攻擊範圍修正 | `lua/sgs_ex.lua:250` |
| `sgs.CreateCardLimitSkill` | 卡牌使用限制 | `lua/sgs_ex.lua:274` |
| `sgs.CreateViewAsEquipSkill` | 裝備轉化 | `lua/sgs_ex.lua:265` |
| `sgs.CreateMasochismSkill` | 受傷觸發（簡化） | `lua/sgs_ex.lua:289` |
| `sgs.CreatePhaseChangeSkill` | 階段變更（簡化） | `lua/sgs_ex.lua:300` |
| `sgs.CreateDrawCardsSkill` | 摸牌階段（簡化） | `lua/sgs_ex.lua:309` |
| `sgs.CreateGameStartSkill` | 遊戲開始（簡化） | `lua/sgs_ex.lua:324` |
| `sgs.CreateRetrialSkill` | 判定修改（簡化） | `lua/sgs_ex.lua:334` |
| `sgs.CreatePreSelectionMetaSkill` | 選將階段技能 | `lua/sgs_ex.lua:348` |
| `sgs.CreateAnytimeSkill` | 任意時機技能 | `lua/sgs_ex.lua:361` |
| `sgs.CreateBattleArraySkill` | 陣法技 | `lua/sgs_ex.lua:374` |
| `sgs.CreateScenario` | 場景模式 | `lua/sgs_ex.lua:136` |
| `sgs.CreateScenarioRule` | 場景規則 | `lua/sgs_ex.lua:104` |
| `sgs.CreateCardActionButton` | 卡牌動作按鈕 | `lua/sgs_ex.lua:934` |
| `sgs.SetCardActionButtons` | 設定卡牌按鈕 | `lua/sgs_ex.lua:966` |
| `sgs.LoadTranslationTable` | 翻譯表載入 | `lua/sgs_ex.lua:918` |
| `sgs.LoadSkinTransltionTable` | 皮膚翻譯載入 | `lua/sgs_ex.lua:924` |

---

## 16. 通用模式與最佳實踐

### 16.1 技能觸發流程

```lua
skill = sgs.CreateTriggerSkill {
    name = "my_skill",
    events = { sgs.Damaged },
    on_trigger = function(self, event, player, data)
        local room = player:getRoom()
        local damage = data:toDamage()

        -- 1. 條件檢查
        if not player:isAlive() then return false end

        -- 2. 可觸發性提示（鎖定技用）
        room:sendCompulsoryTriggerLog(player, self:objectName())
        room:broadcastSkillInvoke(self:objectName())

        -- 3. 詢問玩家是否發動（非鎖定技）
        if not player:askForSkillInvoke(self:objectName(), data) then
            return false
        end

        -- 4. 效果執行
        room:drawCards(player, 1)

        return false
    end,
}
```

### 16.2 限定技模式

```lua
skill = sgs.CreateTriggerSkill {
    name = "my_limited",
    frequency = sgs.Skill_Limited,
    limit_mark = "@my_mark",
    events = { sgs.Damaged },
    on_trigger = function(self, event, player, data)
        if player:getMark("@my_mark") == 0 then return false end
        -- ...
        room:removePlayerMark(player, "@my_mark")
        return true
    end,
}
```

### 16.3 覺醒技模式

```lua
skill = sgs.CreateTriggerSkill {
    name = "my_wake",
    frequency = sgs.Skill_Wake,
    events = { sgs.EventPhaseStart },
    waked_skills = "new_skill1,new_skill2",
    can_wake = function(self, player)
        return player:getMark("my_wake") == 0
    end,
    on_trigger = function(self, event, player, data)
        if player:getPhase() ~= sgs.Player_Start then return false end
        if not self:can_wake(player) then return false end
        local room = player:getRoom()
        room:broadcastSkillInvoke(self:objectName())
        room:doLightbox("$my_wakeAnimate", 3000)
        room:changeMaxHpForAwakenSkill(player, -1, self:objectName())
        room:handleAcquireDetachSkills(player, "new_skill1")
        room:addPlayerMark(player, "my_wake")
        return false
    end,
}
```

### 16.4 主公技模式

武將名稱尾綴 `$`：

```lua
my_lord = sgs.General(extension, "my_lord$", "wei", 4)
```

技能內使用：

```lua
on_trigger = function(self, event, player, data)
    if player:hasLordSkill(self:objectName()) then
        -- 觸發主公技
    end
end
```

### 16.5 技能標記 (Mark) 使用

```lua
-- 使用 @ 前綴的標記（會在 UI 顯示）
room:addPlayerMark(player, "@my_mark", n)
room:removePlayerMark(player, "@my_mark", n)
player:getMark("@my_mark")

-- 無 UI 顯示的內部標記
room:setPlayerMark(player, "internal_flag", 1)
player:getMark("internal_flag")
```

---

## 17. 召喚物/臨時卡牌模式

```lua
-- 建立召喚物
local summon = sgs.Sanguosha:cloneCard("fire_slash", sgs.Card_NoSuit, 0)
summon:setSkillName("my_skill")
summon:addSubcard(real_card:getId())
summon:deleteLater()

-- 卡牌複製
local new_card = existing_card:clone()
new_card:addSubcard(existing_card:getId())
```

---

## 18. require 與外部模組

```lua
require "ExtraTurnUtils"          -- 載入外部工具
require "lua.config"              -- 載入配置
-- 注意：require 使用不帶副檔名的模組名稱
```

---

## 19. 版本號與元資訊慣例

檔案開頭可包含版本資訊註解：

```lua
--遊戲包名稱：魅包
--版本號：V2.35.1
--最後更新時間：2/3/2014
```

---

## 20. 相容性注意事項

- **避免 C++20 語法**：編譯器可能不支援
- **Lua 5.2 標準**：不使用 Lua 5.3+ 特有的函數
- **module() 已棄用**：新檔案不應使用 `module()`，直接以 `sgs.Package` 開頭
- **SWIG 綁定**：修改 `src/core/` `src/server/` 的公開 API 後須更新 `swig/*.i`
- **Q_SKILL 巨集**：Lua 技能無需此巨集，僅供 C++ 技能使用
- **card_use 結構**：對 AOE/GlobalEffect 錦囊，框架自動填充目標列表
- **延時錦囊**：須設定 `subclass = sgs.LuaTrickCard_TypeDelayedTrick` 與 `movable`
