# TriggerV2Skill 系統說明

## 概述

TriggerV2Skill 是重構後的觸發技能系統，設計目標：
1. 向後相容現有 TriggerSkill
2. 支持同名技能的多實例（透過 instanceId 區分）
3. 標準化觸發流程：record → triggerable → cost → effect
4. 細粒度時機鉤子：willInvoke → targetConfirming → invoking → effect → effectFinished

## 類層次結構

```
Skill (QObject)
└── TriggerSkill
    └── TriggerV2Skill
        └── LuaTriggerV2Skill (Lua 绑定)
```

## 核心資料結構

### TriggerList

```cpp
typedef QMap<ServerPlayer*, QStringList> TriggerList;
```

| Key | Value | 說明 |
|-----|-------|------|
| `ServerPlayer*` | `QStringList` | 技能持有者 → 可觸發的技能名列表 |

技能名格式：`source'name*multiplier#instanceId`
- `source`：來源武將前綴（如 `sgs1'baGua`）
- `multiplier`：同名牌疊加數量
- `instanceId`：實例唯一 ID

### SkillContext 結構

**位置**: `src/core/skill.h` (line 9-40)

```cpp
struct SkillContext {
    QString skill_name;            // 發動的技能名稱
    ServerPlayer *invoker;        // 實際發動者（動作執行人）
    ServerPlayer *owner;          // 技能擁有者（技能來源）
    QList<ServerPlayer *> targets;           // 技能目標
    QList<ServerPlayer *> updated_targets;   // 目標替換（targetConfirming 用）
    const Card *use_card;         // 關聯卡牌（可為 nullptr）
    QVariant *original_data;      // 原始觸發數據載體（指針，可訪問原始事件數據）
    int instanceID;               // 實例 ID（區分同名技能）

    ServerPlayer *preferredTarget;    // 優先目標
    int preferredTargetSeat;          // 優先目標座位號

    bool is_forced;               // 是否強制發動
    bool is_canceled;             // 是否被取消（willInvoke 用）
    bool bypass_cost;             // 是否免除代價（willInvoke 用）
    bool manual_effect;           // 是否手動調用 skillEffect（框架不自動遍歷）
    TriggerEvent current_event;   // 當前觸發時機

    int amount;                   // 技能基礎數值
    int modified_amount;          // 修改後數值
    int trigger_count;            // 已觸發次數
};
```

**重要**：`original_data` 是指向原始事件數據的指針，在 `on_cost`、`on_effect` 等回調中可通過它訪問原始事件數據。

### Instance ID 機制

每個 Skill 實例具有唯一的 `m_instanceId`，用於區分同名技能的不同實例。

| 欄位 | 說明 |
|------|------|
| `m_instanceId` | 實例唯一 ID（從 1 遞增） |
| `m_globalInstanceCount` | 全局計數器 |

```cpp
// 技能獲取時的儲存格式
"baGua"           // instanceId = 0（無指定）
"baGua#1"         // instanceId = 1
"baGua#2"         // instanceId = 2
```

## TriggerV2Skill 類定義

### 位置
- Header: `src/core/skill.h` (line 293-320)
- Implementation: `src/core/skill.cpp` (line 562-648)

### 虛方法

#### 核心流程方法

| 方法 | 返回值 | 說明 |
|------|--------|------|
| `triggerable(event, room, player, data)` | `TriggerList` | 收集可觸發的技能 |
| `record(event, room, player, data, owner)` | `void` | 記錄階段（全技能執行） |
| `cost(event, room, player, data, ask_who)` | `bool` | 消耗階段（是否執行） |
| `effect(event, room, player, data, ask_who)` | `bool` | 效果階段（是否中斷） |
| `trigger(event, room, player, data, owner)` | `bool` | 完整流程（cost → effect） |

### 觸發流程

```
triggerV2Skills(event, room, target, data)
    │
    ├─ 1. record()      ← 所有 V2 技能（不管是否 triggerable）
    │
    ├─ 2. triggerable() ← 收集可觸發技能
    │
    ├─ 3. mergeSkillNames() ← 合併同名技能（name*multiplier）
    │
    ├─ 4. askForTriggerOrder ← 玩家選擇
    │
    └─ 5. 對每個被選中的技能：
            │
            ├─ cost() → bool
            │     └─ 詢問是否發動 + 選目標
            │     └─ 返回 false 則跳過
            │
            ├─ trigger(EventSkillWillInvoke, ctx)    ← 公共事件
            │     └─ 註冊該事件的技能執行，可修改 ctx.is_canceled、ctx.bypass_cost
            │
            ├─ trigger(EventSkillPay, ctx)           ← 公共事件（若 bypass_cost = true 則跳過）
            │     └─ 可修改代價相關數值
            │
            ├─ pay() → bool（若 bypass_cost = true 則跳過）
            │     └─ 支付代價
            │     └─ 返回 false 則跳過
            │
            ├─ trigger(EventSkillTargetConfirming, ctx)  ← 公共事件
            │     └─ 註冊該事件的技能執行，可修改 ctx.updated_targets
            │
            ├─ trigger(EventSkillInvoking, ctx)     ← 公共事件
            │     └─ 註冊該事件的技能執行
            │
            ├─ trigger(EventSkillEffect, ctx)       ← 公共事件
            │     └─ 返回 true 則跳過全部效果
            │
            ├─ effect() → bool（若 EventSkillEffect 返回 true 則跳過）
            │     └─ 一次性效果
            │     └─ 可手動調用 skill:skillEffect(target) 處理目標
            │     └─ 若 ctx.manual_effect = true，框架不自動遍歷
            │     └─ 返回 true 表示 broken
            │
            ├─ if ctx.manual_effect == false and ctx.targets 不為空:
            │     │
            │     └─ for each target:
            │           │
            │           └─ skillEffect(target) → 內部觸發 EventSkillEffectTarget
            │                 └─ 若 EventSkillEffectTarget 未跳過，調用 effectTarget()
            │
            └─ trigger(EventSkillEffectFinished, ctx) ← 公共事件
```

### skillEffect 方法

用於手動調用目標效果，觸發 `EventSkillEffectTarget`：

**簽名**：
```cpp
bool skillEffect(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, 
                 SkillContext &ctx, ServerPlayer *target) const;
```

**Lua 調用**：
```lua
skill:skillEffect(event, room, player, ctx, target)
```

**完整範例**：
```lua
on_effect = function(skill, event, room, player, ctx)
    -- ctx 是 SkillContext 引用
    
    -- 一次性效果
    player:drawCards(1)
    
    -- 手動調用目標效果
    for _, target in sgs.qlist(ctx.targets) do
        skill:skillEffect(event, room, player, ctx, target)  -- 觸發 EventSkillEffectTarget + effectTarget
    end
    ctx.manual_effect = true  -- 標記已手動處理，框架不自動遍歷
    
    -- 清理資料
    -- ...
    
    return false
end
```

### ctx 傳遞方式

```cpp
SkillContext ctx;
ctx.skill_name = skillName;
ctx.invoker = p;
ctx.owner = target;
ctx.instanceID = instanceId;
ctx.current_event = EventSkillWillInvoke;

QVariant ctx_data = QVariant::fromValue(ctx);
trigger(EventSkillWillInvoke, room, p, ctx_data);
ctx = ctx_data.value<SkillContext>();  // 取回修改後的 ctx
```

## TriggerEvent 時機枚舉

**位置**: `src/core/structs.h` (line 727-731)

| 枚舉值 | 觸發時機 | 典型應用 | data 類型 |
|--------|----------|----------|-----------|
| `EventSkillWillInvoke` | 代價支付前 | 封印技能 (`is_canceled=true`)、免費施放 (`bypass_cost=true`) | `SkillContext` |
| `EventSkillPay` | 代價支付時 | 修改代價數值、代價類型 | `SkillContext` |
| `EventSkillTargetConfirming` | 目標確認 | 目標轉移（修改 `updated_targets`） | `SkillContext` |
| `EventSkillInvoking` | 正式宣告 | 觸發「技能發動後」的聯動技能 | `SkillContext` |
| `EventSkillEffect` | 效果執行前 | 替換效果（返回 `true` 跳過原效果） | `SkillContext` |
| `EventSkillEffectTarget` | 單目標效果前 | 對特定目標無效化（返回 `true` 跳過該目標） | `SkillContext` |
| `EventSkillEffectFinished` | 效果執行後 | 清理臨時數據、後續事件 | `SkillContext` |

## Lua 綁定

### LuaTriggerV2Skill

**位置**: `src/core/lua-wrapper.h` (line 73-127)

### 回调函数

#### 核心流程回調

| 欄位 | 類型 | 對應 C++ 方法 | 職責 |
|------|------|---------------|------|
| `can_trigger` | `LuaFunction` | `triggerable()` | 條件檢查 |
| `on_record` | `LuaFunction` | `record()` | 記錄階段 |
| `on_cost` | `LuaFunction` | `cost()` | 詢問是否發動 + 選目標 |
| `on_pay` | `LuaFunction` | `pay()` | 支付代價 |
| `on_effect` | `LuaFunction` | `effect()` | 一次性效果（無論有無目標都執行） |
| `on_effect_target` | `LuaFunction` | `effectTarget()` | 對單一目標執行效果 |
| `on_turn_broken` | `LuaFunction` | 回合被打斷 | - |
| `check_custom_usage` | `LuaFunction` | `checkCustomUsage()` | 自定義次數限制 |


### Lua 工廠函數

**位置**: `lua/sgs_ex.lua` (line 62-85)

```lua
sgs.CreateTriggerV2Skill {
    name = "baGua",
    frequency = sgs.Skill_Compulsory,
    events = {sgs.DamageCaused, sgs.DamageInflicted},
    limit_scope = sgs.Limit_Round,
    max_usage_limit = 2,
    can_trigger = function(skill, event, room, player, data)
        -- data 是原始事件數據（QVariant*），可用 data:toDamage() 等方法轉換
        -- 返回格式: "skillName" 或 "player, skillName"
        return "baGua"
    end,
    on_cost = function(skill, event, room, player, ctx)
        -- ctx 是 SkillContext 引用，直接修改即可
        -- 返回 true 表示執行效果
        return true
    end,
    on_effect = function(skill, event, room, player, ctx)
        -- ctx 是 SkillContext 引用
        -- 可通過 ctx.original_data 訪問原始事件數據
        -- 返回 true 表示中斷事件
        return false
    end,
    priority = 1
}

-- 監聽技能發動時機的技能
sgs.CreateTriggerV2Skill {
    name = "fengyin",
    frequency = sgs.Skill_Compulsory,
    events = {sgs.EventSkillWillInvoke},  -- 註冊監聽 EventSkillWillInvoke
    can_trigger = function(skill, event, room, player, data)
        -- 時機鉤子事件的 data 是 SkillContext
        local ctx = data:toSkillContext()
        if ctx.skill_name == "baGua" then
            return "fengyin"  -- 響應 baGua 的 willInvoke 時機
        end
        return false
    end,
    on_effect = function(skill, event, room, player, ctx)
        -- 時機鉤子事件的 ctx 也是 SkillContext
        ctx.is_canceled = true  -- 封印 baGua 技能
        -- 注意：ctx 是引用傳遞，直接修改即可，不需要 setValue
        return false
    end
}
```

### Lua 回調函數參數詳解

### 參數傳遞規則

**重要**：不同回調函數的第 5 個參數類型不同！

| 回調函數 | 第 5 個參數 | 類型 | 說明 |
|----------|------------|------|------|
| `can_trigger` | `data` | `QVariant*` | 原始事件數據，可用 `data:toDamage()` 等方法轉換 |
| `on_record` | `ctx` | `SkillContext*` | 技能上下文，引用傳遞 |
| `on_cost` | `ctx` | `SkillContext*` | 技能上下文，引用傳遞 |
| `on_pay` | `ctx` | `SkillContext*` | 技能上下文，引用傳遞 |
| `on_effect` | `ctx` | `SkillContext*` | 技能上下文，引用傳遞 |
| `on_effect_target` | `ctx` | `SkillContext*` | 技能上下文，引用傳遞 |
| `on_turn_broken` | `ctx` | `SkillContext*` | 技能上下文，引用傳遞 |
| `check_custom_usage` | `ctx` | `SkillContext*` | 技能上下文，引用傳遞 |

### SkillContext 引用傳遞

`ctx` 是 `SkillContext` 的引用（指針），**直接修改即可，不需要 `data:setValue(ctx)`**：

```lua
on_cost = function(skill, event, room, player, ctx)
    local target = room:askForPlayerChosen(player, room:getOtherPlayers(player), 
        "skill", "skill-invoke", true, true)
    if target then
        ctx.targets:append(target)  -- 直接修改，自動生效
        return true
    end
    return false
end
```

### ctx.targets 索引注意事項

**重要**：`ctx.targets` 是 `QList<ServerPlayer*>`，SWIG 綁定為 `SPlayerList`。

| 方法 | 索引方式 | 說明 |
|------|----------|------|
| `ctx.targets:first()` | 推薦 | 返回第一個元素 |
| `ctx.targets:last()` | 推薦 | 返回最後一個元素 |
| `ctx.targets:at(0)` | 0-based | 返回第一個元素 |
| `ctx.targets[1]` | ❌ 不推薦 | Lua 1-based 索引會調用 `QList::at(1)`，返回第二個元素或 nil |

**錯誤範例**：
```lua
-- ❌ 錯誤：ctx.targets[1] 在 Lua 中會調用 QList::at(1)，返回 nil（若只有一個元素）
local target = ctx.targets[1]

-- ✅ 正確：使用 first() 或 at(0)
local target = ctx.targets:first()
-- 或
local target = ctx.targets:at(0)
```

### ctx.extra_data 設置注意事項

`ctx.extra_data` 是 `QVariant` 類型。直接賦值整數可能不會正確工作。

**錯誤範例**：
```lua
-- ❌ 錯誤：直接賦值 int 可能不會正確轉換為 QVariant
ctx.extra_data = card_id
```

**正確範例**：
```lua
-- ✅ 正確：使用 setValue() 方法
ctx.extra_data:setValue(card_id)
```

### 訪問原始事件數據

在 `on_cost`、`on_effect` 等回調中，可通過 `ctx.original_data` 訪問原始事件數據：

```lua
on_effect = function(skill, event, room, player, ctx)
    -- ctx.original_data 是 QVariant*，指向原始事件數據
    local move = ctx.original_data:toMoveOneTime()
    -- 或其他類型轉換
    local damage = ctx.original_data:toDamage()
    return false
end
```

### 時機鉤子事件

對於 `EventSkillWillInvoke` 等時機鉤子事件：
- `can_trigger` 的 `data` 是 `SkillContext`（已包裝為 QVariant）
- 需用 `data:toSkillContext()` 轉換

```lua
can_trigger = function(skill, event, room, player, data)
    if event == sgs.EventSkillWillInvoke then
        local ctx = data:toSkillContext()  -- 時機鉤子事件需要轉換
        if ctx.skill_name == "targetSkill" then
            return "mySkill"
        end
    elseif event == sgs.DamageCaused then
        local damage = data:toDamage()  -- 基礎事件直接轉換
        -- ...
    end
    return false
end
```

### 常見錯誤

```lua
-- 錯誤：on_cost 的第 5 個參數是 ctx，不是 data
on_cost = function(skill, event, room, player, data)
    local ctx = data:toSkillContext()  -- 錯誤！data 是 SkillContext，不是 QVariant
    return true
end

-- 正確
on_cost = function(skill, event, room, player, ctx)
    -- ctx 已經是 SkillContext，直接使用
    return true
end

-- 錯誤：不需要 setValue
on_effect = function(skill, event, room, player, ctx)
    ctx.manual_effect = true
    data:setValue(ctx)  -- 錯誤！ctx 是引用，不需要 setValue
    return false
end

-- 正確
on_effect = function(skill, event, room, player, ctx)
    ctx.manual_effect = true  -- 直接修改即可
    return false
end
```

## Lua triggerable 返回值格式

本系統支援三種返回格式（格式三本項目不支援）：

### 格式一：單一技能擁有者觸發

```lua
-- 單一技能
return "baGua"

-- 多技能（同一玩家，用 + 分隔多次觸發）
return "baGua+baGua"  -- 觸發 2 次

-- 指定玩家（返回兩個值）
return "baGua", ownerPlayer

-- 攜帶 instanceId
return "baGua#" .. skill:getInstanceId()

-- 攜帶 multiplier（觸發多次）
return "baGua*3"
```

### 格式二：多個技能擁有者可觸發

適用於「技能觸發者 ≠ 技能擁有者」的情況，如國戰的輸糧、骁果、襲射等。

#### 返回格式

```lua
-- 返回兩個字符串，用 | 分隔
-- 返回值 1：技能名列表（如 "shuliang|shuliang"）
-- 返回值 2：玩家 objectName 列表（如 "player1|player2")

return table.concat(trigger_list_skill, "|"), table.concat(trigger_list_who, "|")
```

#### ctx.owner vs ctx.invoker

| 欄位 | 格式一 | 格式二 |
|------|--------|--------|
| `ctx.owner` | `target`（事件觸發者） | 技能擁有者（返回值 2 中的玩家） |
| `ctx.invoker` | `target`（事件觸發者） | `target`（事件觸發者） |
| `on_cost(player)` | 技能擁有者 | **技能擁有者** |
| `on_cost(ctx.invoker)` | 技能擁有者 | **事件觸發者** |

#### 完整範例：襲射（s4_xishe）

```lua
s4_xishe = sgs.CreateTriggerV2Skill{
    name = "s4_xishe",
    events = {sgs.EventPhaseProceeding},
    
    can_trigger = function(skill, event, room, player, data)
        if event == sgs.EventPhaseProceeding then
            if player:getPhase() ~= sgs.Player_Start then return false end
            
            -- 格式二：收集所有可觸發的技能擁有者
            local trigger_list_skill, trigger_list_who = {}, {}
            for _, owner in sgs.qlist(room:findPlayersBySkillName("s4_xishe")) do
                if owner:objectName() ~= player:objectName() 
                   and owner:isAlive()
                   and player:getEquips():length() > 0 
                   and owner:canDiscard(player, "e") then
                    local ctx = sgs.SkillContext()
                    ctx.invoker = owner
                    ctx.owner = owner
                    ctx.instanceID = skill:getInstanceId()
                    if skill:isUsable(ctx) then
                        table.insert(trigger_list_skill, "s4_xishe")
                        table.insert(trigger_list_who, owner:objectName())
                    end
                end
            end
            if #trigger_list_skill > 0 then
                return table.concat(trigger_list_skill, "|"), 
                       table.concat(trigger_list_who, "|")
            end
        end
        return false
    end,
    
    on_cost = function(skill, event, room, player, ctx)
        -- 格式二：player 是技能擁有者（襲射擁有者）
        -- ctx.invoker 是事件觸發者（開始階段的角色）
        local target = room:getCurrent()  -- 獲取當前回合玩家
        if room:askForSkillInvoke(player, "s4_xishe", ToData(target)) then
            ctx.targets:append(target)
            return true
        end
        return false
    end,
    
    on_effect_target = function(skill, event, room, player, ctx, target)
        -- player 是技能擁有者，對 target 使用殺
        local slash = sgs.Sanguosha:cloneCard("slash", sgs.Card_NoSuit, 0)
        slash:setSkillName("s4_xishe")
        slash:deleteLater()
        if player:canSlash(target, slash, false) then
            local use = sgs.CardUseStruct()
            use.card = slash
            use.from = player
            use.to:append(target)
            room:useCard(use)
        end
    end
}
```

#### 常見錯誤

```lua
-- ❌ 錯誤：循環內直接 return，只返回第一個擁有者
for _, owner in sgs.qlist(room:findPlayersBySkillName("s4_xishe")) do
    if condition then
        return "s4_xishe", owner  -- 只觸發第一個！
    end
end

-- ✅ 正確：收集所有擁有者後用格式二返回
local trigger_list_skill, trigger_list_who = {}, {}
for _, owner in sgs.qlist(room:findPlayersBySkillName("s4_xishe")) do
    if condition then
        table.insert(trigger_list_skill, "s4_xishe")
        table.insert(trigger_list_who, owner:objectName())
    end
end
if #trigger_list_skill > 0 then
    return table.concat(trigger_list_skill, "|"), 
           table.concat(trigger_list_who, "|")
end
return false
```

#### 返回值處理流程

| 步驟 | Lua 返回值 | SWIG 解析 | roomthread 處理 |
|------|------------|-----------|-----------------|
| 1 | `"skill1|skill2", "owner1|owner2"` | `TriggerList {[owner1]: ["skill1"], [owner2]: ["skill2"]}` | 遍歷 `TriggerList` 構建 `skillContexts` |
| 2 | - | - | `ctx.owner = owner1`, `ctx.invoker = target` |
| 3 | - | - | `askForTriggerOrder` 返回 `"skillName:ownerObjectName"` |
| 4 | - | - | 解析 `ownerObjectName`，找到對應 `selected_ctx` |
| 5 | - | - | `cost(skill_owner)` 使用 `ctx.owner` 作為 `player` |

#### askForTriggerOrder 返回格式

| 格式 | 返回值 | 解析結果 |
|------|--------|----------|
| 格式一 | `"skillName"` | `skillName`, `ownerObjectName = ""` |
| 格式二 | `"skillName:ownerObjectName"` | `skillName`, `ownerObjectName` |

#### 格式一 vs 格式二 TriggerList 對比

| 格式 | 返回值 1 | 返回值 2 | TriggerList 結果 |
|------|----------|----------|------------------|
| 格式一（單技能） | `"skillName"` | `nil` | `{[target]: ["skillName"]}` |
| 格式一（指定玩家） | `"skillName"` | `ServerPlayer*` | `{[ask_who]: ["skillName"]}` |
| 格式二（多擁有者） | `"skill1\|skill2"` | `"owner1\|owner2"` | `{[owner1]: ["skill1"], [owner2]: ["skill2"]}` |

#### 重要說明

1. **格式二不支援 `+` 分隔多次觸發**：每個 `|` 分隔項只觸發一次
2. **格式二時 `ctx.owner` ≠ `ctx.invoker`**：技能擁有者和事件觸發者不同
3. **格式二需要 `roomthread.cpp` 正確解析 `ownerObjectName`**：2026-05-29 修復後支援

### 格式三：技能擁有者對目標發動（不支援）

本項目不支援此格式：
```lua
-- 格式："skill_name->target1+target2"
return self:objectName().."->"..table.concat(targets, "+")
``

### 返回值處理流程

| 格式 | 返回值 1 | 返回值 2 | TriggerList 結果 |
|------|----------|----------|------------------|
| 格式一（單技能） | `"skillName"` | `nil` 或 `ServerPlayer*` | `{[player]: ["skillName"]}` |
| 格式一（多觸發） | `"skillName*3"` | `nil` | `{[player]: ["skillName"]}`（加入 3 個 SkillContext） |
| 格式二（多擁有者） | `"skill1\|skill2"` | `"player1\|player2"` | `{[p1]: ["skill1"], [p2]: ["skill2"]}` |

### 重要說明

1. **格式二時 `ctx.owner` vs `ctx.invoker`**：
   - `ctx.owner` = 技能擁有者（返回值 2 中的玩家）
   - `ctx.invoker` = 事件觸發者（原 `player` 參數）

2. **格式二不支援 `+` 分隔多次觸發**：每個 `|` 分隔項只觸發一次

3. **格式二與格式一互斥**：若第二返回值為字符串，則按格式二解析；若為 `ServerPlayer*` 或 `nil`，則按格式一解析

## Instance ID 相關 API

### Skill 類新增

```cpp
// src/core/skill.h
class Skill {
    int getInstanceId() const { return m_instanceId; }
    static int getGlobalInstanceCount() { return m_globalInstanceCount; }
};
```

### Engine 類新增

```cpp
// src/core/engine.h
const TriggerSkill* getTriggerSkill(const QString &skill_name, int instanceId) const;
QMap<QPair<QString, int>, const TriggerSkill*> m_triggerSkillsByInstance;
```

### Player 類新增

```cpp
// src/core/player.h
void acquireSkill(const QString &skill_name, int instanceId);
bool hasSkill(const QString &skill_name, bool include_lose = false) const;
void detachSkill(const QString &skill_name);
```

### Room::acquireSkill 行為

| 呼叫方式 | 行為 |
|---------|------|
| `acquireSkill(player, "baGua")` | TriggerV2Skill 自動分配新 instanceId |
| `acquireSkill(player, "baGua#3")` | 精確獲得 instanceId=3（如已存在則忽略）|
| `acquireSkill(player, skillPtr)` | 使用技能自身的 instanceId |

## parseSkillName 格式解析

**位置**: `src/core/skill.cpp` (line 589-617)

```
格式: source'name*multiplier#instanceId
```

| 範例 | skillName | source | multiplier | instanceId |
|------|-----------|--------|------------|------------|
| `baGua` | baGua | - | 1 | 0 |
| `baGua*2` | baGua | - | 2 | 0 |
| `sgs1'baGua*3` | baGua | sgs1 | 3 | 0 |
| `baGua#5` | baGua | - | 1 | 5 |
| `sgs1'baGua*2#3` | baGua | sgs1 | 2 | 3 |

## 重名技能處理

### 收集階段（Collection Phase）

```
skill_table[event] → [skillA_ptr, skillB_ptr]  (同名技能的不同實例)
```

### 合併階段（Merge Phase）

```cpp
// mergeSkillNames() 將同名技能合併
["baGua", "baGua", "baGua"] → "baGua*3"
```

### 選擇階段（Selection Phase）

- 玩家通過 `askForTriggerOrder` 選擇
- 名稱格式：`skillName*multiplier#instanceId`
- 精確通過 `getTriggerSkill(skillName, instanceId)` 查找

## 向後相容

| 場景 | 處理方式 |
|------|----------|
| 舊 TriggerSkill | `triggerable()` 返回 `bool`，不受影響 |
| `getTriggerSkill(name)` | 等同於 `getTriggerSkill(name, 0)` |
| Lua 返回不帶 `#` | instanceId 為 0，降級處理 |

## 與舊系統差異

| 項目 | 舊 TriggerSkill | TriggerV2Skill |
|------|---------------|----------------|
| 觸發返回 | `bool triggerable()` | `TriggerList triggerable()` |
| 多目標 | 需自行遍歷 | 直接返回 `QMap<Player*, QStringList>` |
| 同名處理 | 字串合併 | instanceId 精確定位 |
| 流程分割 | `trigger()` 單一 | `cost()` + `effect()` 分離 |
| record 階段 | 無獨立鉤子 | `record()` 單獨方法 |
| 時機鉤子 | 無 | 5 個細粒度時機（willInvoke、targetConfirming、invoking、effect、effectFinished） |

## 使用範例

### C++ 繼承 TriggerV2Skill

```cpp
class BaGuaSkill : public TriggerV2Skill {
    Q_OBJECT
public:
    BaGuaSkill() : TriggerV2Skill("baGua") {
        events << DamageCaused << DamageInflicted;
    }

    TriggerList triggerable(TriggerEvent event, Room *room,
                           ServerPlayer *player, QVariant &data) const override {
        TriggerList result;
        if (player->hasSkill("baGua")) {
            result[player] << "baGua";
        }
        return result;
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *player,
             QVariant &data, ServerPlayer *ask_who) const override {
        return room->askForDiscard(player, objectName(), 1, true);
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player,
               QVariant &data, ServerPlayer *ask_who) const override {
        // 八卦效果邏輯
        return false;
    }
};
```

### Lua 技能定義

```lua
local baGua = sgs.CreateTriggerV2Skill {
    name = "baGua",
    frequency = sgs.Skill_Compulsory,
    events = {sgs.DamageCaused, sgs.DamageInflicted},

    can_trigger = function(skill, event, room, player, data)
        if not player:hasSkill(skill:objectName()) then return false end
        if event == sgs.DamageCaused then
            local damage = data:toDamage()  -- data 是原始事件數據
            if damage and damage.card and damage.card:isKindOf("Slash") then
                return "baGua"
            end
        end
        return false
    end,

    on_cost = function(skill, event, room, player, ctx)
        -- ctx 是 SkillContext 引用
        return room:askForCard(player, ".|black", "@baGua", ctx.original_data)
    end,

    on_effect = function(skill, event, room, player, ctx)
        -- 八卦置換效果
        return false
    end
}
```

## 技能數值系統 (Amount)

### 概述

`amount` 系統允許技能的數值被其他技能動態修改，支援 Roguelike 玩法和技能互動。

### SkillContext 新增欄位

```cpp
struct SkillContext {
    // ... 其他欄位 ...
    int amount;           // 技能基礎數值（預設 1）
    int modified_amount;  // 修改後數值（0 表示未被修改）
};
```

### TriggerV2Skill 新增方法

| 方法 | 說明 |
|------|------|
| `getBaseAmount()` | 返回技能基礎數值，預設 1 |
| `getEffectiveAmount(ctx)` | 返回有效數值（優先 `modified_amount`，其次 `amount`，最後 `baseAmount`） |

### Lua 技能定義

```lua
s4_cangzhuo = sgs.CreateTriggerV2Skill{
    name = "s4_cangzhuo",
    base_amount = 1,  -- 可選，預設 1
    -- ...
    on_effect = function(skill, event, room, player, ctx)
        -- ctx 是 SkillContext 引用
        local amount = skill:getEffectiveAmount(ctx)
        
        for _, p in sgs.qlist(ctx.targets) do
            for i = 1, amount do
                getYing(p)
            end
        end
        return false
    end
}
```

### 修改技能數值

其他技能可在 `EventSkillWillInvoke` 時機修改 `ctx.modified_amount`：

```lua
modifier = sgs.CreateTriggerV2Skill{
    name = "modifier",
    events = {sgs.EventSkillWillInvoke},
    can_trigger = function(skill, event, room, player, data)
        -- 時機鉤子事件的 data 是 SkillContext
        local ctx = data:toSkillContext()
        if ctx.skill_name == "s4_cangzhuo" then
            return "modifier"
        end
        return false
    end,
    on_effect = function(skill, event, room, player, ctx)
        -- ctx 是 SkillContext 引用，直接修改
        ctx.modified_amount = (ctx.amount or 1) * 2  -- 雙倍效果
        return false
    end
}
```

### 數值優先級

```
getEffectiveAmount(ctx) 返回值：
1. ctx.modified_amount > 0 → 使用 modified_amount
2. ctx.amount > 0 → 使用 amount
3. 否則 → 使用 skill:getBaseAmount()
```

---

## 技能使用次數限制系統

### SkillContext 結構

**位置**: `src/core/skill.h` (line 9-27)

```cpp
struct SkillContext {
    QString skill_name;            // 發動的技能名稱
    ServerPlayer *invoker;         // 實際發動者（動作執行人）
    ServerPlayer *owner;           // 技能擁有者（技能來源）
    QList<ServerPlayer *> targets; // 技能目標
    QList<ServerPlayer *> updated_targets; // 目標替換（targetConfirming 用）
    const Card *use_card;          // 關聯卡牌（可為 nullptr）
    QVariant original_data;        // 原始觸發數據載體
    int instanceID;                // 實例 ID（區分同名技能）

    bool is_forced;                // 是否強制發動
    bool is_canceled;              // 是否被無效化（willInvoke 用）
    bool bypass_cost;              // 是否免除代價（willInvoke 用）
    bool manual_effect;            // 是否手動調用 skillEffect（框架不自動遍歷）
    TriggerEvent current_event;    // 當前觸發時機

    int amount;                    // 技能基礎數值
    int modified_amount;           // 修改後數值
};
```

**使用次數歸屬**: 記錄在 `owner`（技能擁有者）身上，非 `invoker`（發動者）。適用於放權等跨角色發動情境。

### SkillLimitScope 枚舉

**位置**: `src/core/skill.h`

```cpp
enum LimitScope {
    Limit_None,    // 無限制
    Limit_Round,    // 每輪限 X 次
    Limit_Turn,     // 每回合限 X 次
    Limit_Phase,    // 每階段限 X 次
    Limit_Game,     // 每場遊戲限 X 次
    Limit_Target,   // 對每個目標限 X 次
    Limit_Custom    // 自定義 Lua 邏輯限制
};
```

### Mark 命名格式

| Scope | Tag Key | 自動清除時機 |
|-------|---------|-------------|
| `Limit_Turn` | `Usage_技能名_instanceID-Clear` | 回合結束 |
| `Limit_Round` | `Usage_技能名_instanceID_lun` | 輪次結束 |
| `Limit_Phase` | `Usage_技能名_instanceID-PhaseClear` | 階段結束 |
| `Limit_Game` | `Usage_技能名_instanceID_game` | 整場不清除 |
| `Limit_Target` | `Usage_技能名_instanceID_目標ObjectName-Clear` | 回合結束 |

### 核心方法

**位置**: `src/core/skill.cpp`

| 方法 | 說明 |
|------|------|
| `getLimitScope()` | 返回限制類型，預設 `Limit_None` |
| `getMaxUsageLimit(ctx)` | 返回最大使用次數，預設 1 |
| `isUsable(ctx)` | 核心校驗：是否還有剩餘次數 |
| `addUsage(ctx)` | 增加使用次數（在技能發動成功後調用） |
| `resetUsage(owner, target)` | 清除使用紀錄（供外部效果调用） |
| `checkCustomUsage(ctx)` | 自定義 Lua 校驗，`Limit_Custom` 時調用 |
| `getUsageHolder(ctx)` | 返回次數歸屬者，預設 `owner` |
| `getUsageTagKey(ctx)` | 取得 Tag 鍵值 |

### 使用流程

```
技能發動前
    │
    ├─ 1. isUsable(ctx)  ← 檢查是否還有剩餘次數
    │
    ├─ 2. cost()         ← 消耗階段
    │
    └─ 3. effect()        ← 效果階段
            │
            └─ addUsage(ctx)  ← 結算成功後增加使用次數
```

### C++ 範例

```cpp
class BaGuaSkill : public TriggerV2Skill {
    Q_OBJECT
public:
    BaGuaSkill() : TriggerV2Skill("baGua") {
        events << DamageCaused << DamageInflicted;
    }

    LimitScope getLimitScope() const override {
        return Limit_Round;
    }

    int getMaxUsageLimit(const SkillContext &) const override {
        return 2;
    }

    TriggerList triggerable(TriggerEvent event, Room *room,
                           ServerPlayer *player, QVariant &data) const override {
        TriggerList result;
        if (player->hasSkill("baGua")) {
            SkillContext ctx;
            ctx.invoker = player;
            ctx.instanceID = getInstanceId();
            if (isUsable(ctx)) {
                result[player] << "baGua";
            }
        }
        return result;
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *player,
             QVariant &data, ServerPlayer *ask_who) const override {
        // ... 消耗階段邏輯
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player,
               QVariant &data, ServerPlayer *ask_who) const override {
        SkillContext ctx;
        ctx.invoker = player;
        ctx.owner = player;
        ctx.instanceID = getInstanceId();
        addUsage(ctx);  // 結算成功後增加次數
        // ... 效果邏輯
        return false;
    }
};
```

### Lua 範例

```lua
local baGua = sgs.CreateTriggerV2Skill {
    name = "baGua",
    frequency = sgs.Skill_Compulsory,
    events = {sgs.DamageCaused},
    limit_scope = sgs.Limit_Round,
    max_usage_limit = 2,
    check_custom_usage = function(skill, ctx)
        -- 自定義邏輯（可選）
        return true
    end,

    can_trigger = function(skill, event, room, player, data)
        if not player:hasSkill(skill:objectName()) then return false end
        local ctx = sgs.SkillContext()
        ctx.invoker = player
        ctx.instanceID = skill:getInstanceId()
        if not skill:isUsable(ctx) then return false end
        return "baGua"
    end,

    on_cost = function(skill, event, room, player, ctx)
        -- ctx 是 SkillContext 引用
        return room:askForCard(player, ".|black", "@baGua", ctx.original_data)
    end,

    on_effect = function(skill, event, room, player, ctx)
        -- ctx 是 SkillContext 引用，直接修改
        local usageCtx = sgs.SkillContext()
        usageCtx.invoker = player
        usageCtx.owner = player
        usageCtx.instanceID = skill:getInstanceId()
        skill:addUsage(usageCtx)
        return false
    end
}
```

## 技能無效化系統 (SkillInvalidity)

### 概述

技能無效化系統用於臨時禁用 TriggerV2Skill，支援：
- 指定技能失效（可區分 instanceId）
- 全域失效（`"all"` 失效所有武將技能，裝備技除外）
- 來源追蹤（死亡/離場時自動清理）

### 與現有系統並行

本系統與 `InvaliditySkill`（透過 `Engine::correctSkillValidity()`）並行運作：
- `isSkillInvalid()` 同時檢查兩者
- `IgnoreInvalidity` 屬性可跳過本系統檢查

### 資料結構

#### Record 格式

```
"skillName#instanceId|sourceName|reason"
```

| 欄位 | 說明 |
|------|------|
| `skillName` | 技能名稱（可為 `"all"` 表示所有技能） |
| `instanceId` | 實例 ID（0 = 第一個實例，省略 `#0` 亦可） |
| `sourceName` | 造成失效的來源武將 objectName |
| `reason` | 失效原因描述 |

#### 匹配邏輯

| 輸入 | Record | 結果 |
|------|--------|------|
| `skillName="fire slash", instanceId=5` | `"fire slash#5\|src\|r"` | 匹配 ✓ |
| `skillName="fire slash", instanceId=5` | `"fire slash#0\|src\|r"` | 匹配 ✓（0 = 第一實例） |
| `skillName="fire slash", instanceId=5` | `"fire slash#3\|src\|r"` | 不匹配 ✗ |
| `skillName="all", instanceId=任意` | `"all\|src\|r"` | 匹配 ✓（不區分 instanceId） |

### 核心 API

#### Player 類

**位置**: `src/core/player.h` (line 162-163)

```cpp
bool isSkillInvalid(const Skill *skill) const;
bool isSkillInvalid(const QString &skill_name, int instanceId = 0) const;
```

#### Room 類

**位置**: `src/server/room.h` (line 310-312)

```cpp
void addSkillInvalidity(ServerPlayer *target, const QString &skillName,
                        const QString &sourceName, const QString &reason,
                        int instanceId = 0);
void removeSkillInvalidity(ServerPlayer *target, const QString &skillName,
                           const QString &sourceName, const QString &reason,
                           int instanceId = 0);
void clearSkillInvalidityBySource(ServerPlayer *source);
```

### isEquipSkill 虛方法

**位置**: `src/core/skill.h` (line 104)

用於判斷是否為裝備技（裝備技不受 `"all"` 失效影響）：

| 子類 | 回傳值 |
|------|--------|
| `Skill` | `false`（預設） |
| `WeaponSkill` | `true` |
| `ArmorSkill` | `true` |
| `TreasureSkill` | `true` |

### 觸發過濾

**位置**: `src/server/roomthread.cpp` (line 707-715)

在 `triggerV2Skills()` 中，`triggerable()` 返回的技能會經過 `isSkillInvalid()` 檢查：

```cpp
foreach (const QString &skill, skills) {
    QString skillName = skill;
    int instanceId = 0;
    int split = skill.indexOf('#');
    if (split != -1) {
        instanceId = skill.mid(split + 1).toInt();
        skillName = skill.left(split);
    }
    if (!p->isSkillInvalid(skillName, instanceId) && !trigger_who[p].contains(skill))
        trigger_who[p] << skill;
}
```

### 自動清理

**位置**: `src/server/gamerule.cpp` (line 1164)

當武將死亡時（`BuryVictim`），自動清理該武將造成的所有失效狀態：

```cpp
case BuryVictim: {
    // ...
    room->clearSkillInvalidityBySource(player);  // 新增
    // ...
}
```

### TriggerEvent 觸發

**位置**: `src/core/structs.h` (line 722-723)

新增兩個 TriggerEvent 用於監聽技能失效狀態變更：

| Event | 觸發時機 | Data 格式 |
|-------|----------|-----------|
| `EventSkillInvalidated` | 技能被無效化時 | `"skillName#instanceId"` |
| `EventSkillValidityRestored` | 技能恢復有效時 | `"skillName#instanceId"` |

#### 觸發時機

| 方法 | Event | 目標玩家 | 返回值作用 |
|------|-------|----------|-----------|
| `addSkillInvalidity()` | `EventSkillInvalidated` | `target` | 返回 `true` 可中止無效化 |
| `removeSkillInvalidity()` | `EventSkillValidityRestored` | `target` | - |
| `clearSkillInvalidityBySource()` | `EventSkillValidityRestored` | 受影響的存活玩家 | - |

#### Lua V2Skill 範例

```lua
-- 傾城技能：失效所有敵方武將技能，來源死亡時自動恢復
local Qingcheng = sgs.CreateTriggerV2Skill {
    name = "qingcheng",
    events = {sgs.EventPhaseChanging, sgs.EventSkillInvalidated, sgs.EventSkillValidityRestored},

    can_trigger = function(skill, event, room, player, data)
        if event == sgs.EventPhaseChanging then
            if player:getPhase() == sgs.Player_Start and player:hasSkill("qingcheng") then
                return "qingcheng"
            end
        elseif event == sgs.EventSkillInvalidated then
            local invalidSkill = data:toString()
            if invalidSkill == "qingcheng" then
                -- 傾城啟動：失效所有敵方武將技能
                local source = room:findPlayerBySkillName("qingcheng")
                if source then
                    for _, p in sgs.qlist(room:getAlivePlayers()) do
                        if p:getRole() ~= source:getRole() then
                            room:addSkillInvalidity(p, "all", source:objectName(), "qingcheng_active")
                        end
                    end
                end
                return true  -- 確認無效化，不中止
            end
            -- 返回 true 可中止其他技能的無效化
            return true
        elseif event == sgs.EventSkillValidityRestored then
            local restoredSkill = data:toString()
            if restoredSkill == "qingcheng" then
                local source = room:findPlayerBySkillName("qingcheng")
                if source and source:isAlive() then
                    for _, p in sgs.qlist(room:getAlivePlayers()) do
                        if p:getRole() ~= source:getRole() then
                            room:removeSkillInvalidity(p, "all", source:objectName(), "qingcheng_active")
                        end
                    end
                end
            end
        end
        return false
    end,

    on_cost = function(skill, event, room, player, data)
        return true
    end,

    on_effect = function(skill, event, room, player, data)
        return false
    end
}
```

### 客戶端同步

使用現有的 `S_COMMAND_UPDATE_SKILL` 命令通知客戶端技能狀態變更：

- 格式：`"skillName#instanceId"`
- 客戶端解析 `#` 取得 instanceId 來更新對應技能按鈕

### 使用範例

```cpp
// 使 target 的 "qingcheng" 技能失效（第一個實例）
room->addSkillInvalidity(target, "qingcheng", source->objectName(), "reason");

// 使 target 的 "qingcheng" 技能失效（指定實例 instanceId=5）
room->addSkillInvalidity(target, "qingcheng", source->objectName(), "reason", 5);

// 使 target 的所有武將技能失效（裝備技除外）
room->addSkillInvalidity(target, "all", source->objectName(), "qingcheng_active");

// 解除失效
room->removeSkillInvalidity(target, "qingcheng", source->objectName(), "reason");

// 武將死亡時自動調用 clearSkillInvalidityBySource() 清理
```

## 歷史

| 日期 | 異動 |
|------|------|
| 2026-05-08 | 初始實作：Skill 類新增 instanceId 機制 |
| 2026-05-08 | Engine 新增 `getTriggerSkill(name, instanceId)` |
| 2026-05-08 | Player 新增 `acquireSkill(name, instanceId)` |
| 2026-05-08 | Room::acquireSkill 支援重名技能自動分配 instanceId |
| 2026-05-08 | TriggerV2Skill::parseSkillName 新增 instanceId 解析 |
| 2026-05-08 | triggerV2Skills 使用 instanceId 精確查找 |
| 2026-05-08 | 新增 Skill 使用次數限制系統（SkillLimitScope） |
| 2026-05-08 | 新增技能無效化系統（SkillInvalidity） |
| 2026-05-08 | 新增 `EventSkillInvalidated` 和 `EventSkillValidityRestored` TriggerEvent |
| 2026-05-09 | 新增五個時機鉤子：willInvoke、targetConfirming、invoking、effect、effectFinished |
| 2026-05-09 | SkillContext 新增 `updated_targets` 和 `current_event` 欄位 |
| 2026-05-09 | 新增 TriggerEvent：EventSkillWillInvoke、EventSkillTargetConfirming、EventSkillInvoking、EventSkillEffect、EventSkillEffectFinished |
| 2026-05-09 | LuaTriggerV2Skill 新增五個時機回調 |
| 2026-05-09 | 改為通過 `trigger()` 觸發時機事件，技能需註冊監聽對應 TriggerEvent |
| 2026-05-18 | 新增技能數值系統（amount、modified_amount） |
| 2026-05-18 | TriggerV2Skill 新增 `getBaseAmount()`、`getEffectiveAmount()` |
| 2026-05-18 | LuaTriggerV2Skill 新增 `setBaseAmount()` |
| 2026-05-18 | `CreateTriggerV2Skill` 支援 `base_amount` 參數 |
| 2026-05-18 | `EventSkillEffect` 移至 `effect()` 前，返回 `true` 可跳過原效果 |
| 2026-05-18 | 分離 `cost()` 和 `pay()`，新增 `EventSkillPay` 時機 |
| 2026-05-18 | `cost()` 負責詢問是否發動 + 選目標，`pay()` 負責支付代價 |
| 2026-05-18 | `bypass_cost` 可跳過 `pay()` 階段 |
| 2026-05-18 | 新增 `use()` 和 `effectTarget()`，支援單目標結算 |
| 2026-05-18 | 新增 `EventSkillEffectTarget` 時機，可攔截單目標效果 |
| 2026-05-18 | `on_effect` 改名為 `on_use`，新增 `on_effect_target` |
| 2026-05-18 | 流程：`use()` → 遍歷目標 → `EventSkillEffectTarget` → `effectTarget(target)` |
| 2026-05-18 | 移除 `on_use`，恢復 `on_effect` 命名 |
| 2026-05-18 | `on_effect` 無論有無目標都執行 |
| 2026-05-18 | `EventSkillEffectTarget` 返回 `true` 只跳過該目標，不 `break` |
| 2026-05-18 | 新增 `skillEffect()` 方法，手動調用目標效果 |
| 2026-05-18 | 新增 `ctx.manual_effect` 欄位，標記是否手動處理目標 |
| 2026-05-18 | 若 `manual_effect = true`，框架不自動遍歷目標 |
| 2026-05-19 | 新增動態重檢機制：每次技能結算後重新調用 `triggerable()` |
| 2026-05-19 | 支援 multiplier：`can_trigger` 返回 `"skillName*3"` 觸發 3 次 |
| 2026-05-19 | `SkillContext` 新增 `trigger_count` 欄位，記錄已觸發次數 |
| 2026-05-19 | 移除 `mergeSkillNames()` 依賴，直接解析 multiplier |
| 2026-05-21 | **重要修正**：文檔更新，明確 Lua 回調函數參數類型差異 |
| 2026-05-21 | `can_trigger` 第 5 參數為 `QVariant* data`（原始事件數據） |
| 2026-05-21 | `on_cost`、`on_effect` 等第 5 參數為 `SkillContext* ctx`（引用傳遞） |
| 2026-05-21 | `ctx` 是引用傳遞，直接修改即可，不需要 `data:setValue(ctx)` |
| 2026-05-21 | 新增 `ctx.original_data` 訪問原始事件數據的說明 |
| 2026-05-21 | 新增「Lua 回調函數參數詳解」章節，說明常見錯誤 |
| 2026-05-29 | **格式二完整支援**：修復 `roomthread.cpp` 和 `room.cpp` 讓格式二真正可用 |
| 2026-05-29 | `roomthread.cpp`：解析 `askForTriggerOrder` 返回的 `ownerObjectName`，正確查找 `selected_ctx` |
| 2026-05-29 | `room.cpp`：`askForTriggerOrder` 返回 `"skillName:ownerObjectName"` 格式 |
| 2026-05-29 | `cost`/`pay`/`effect` 使用 `selected_ctx->owner` 作為 `player` 參數 |
| 2026-05-29 | 更新文檔：補充格式二完整範例（襲射）、常見錯誤、返回值處理流程 |

---

## 動態重檢機制

### 概述

動態重檢機制允許技能在每次結算後重新評估 `can_trigger`，支援：
- 技能結算後狀態變化導致其他技能不再適合發動
- 根據傷害值等動態觸發多次

### 核心變更

#### 舊流程

```
收集 skillContexts → while 循環 → 選擇 → 結算 → 移除
```

#### 新流程

```
while 循環：
    重新調用所有 v2 技能的 triggerable()
    解析 multiplier，加入對應數量的 SkillContext
    注入 trigger_count（已觸發次數）
    若 skillContexts 為空，break
    選擇技能 → 結算
    更新 triggerCounts[key]++
    不移除，讓下次循環重新決定
```

### SkillContext 新增欄位

```cpp
struct SkillContext {
    // ... 其他欄位 ...
    int trigger_count;  // 該技能實例在本次事件中已觸發次數
};
```

### trigger_count 計算邏輯

`triggerCounts` 的 key 為 `"skillName#instanceId"`，每個實例獨立計算：

| 情況 | key | trigger_count |
|------|-----|---------------|
| 實例 1 (`baGua#1`) | `"baGua#1"` | 獨立計算 |
| 實例 2 (`baGua#2`) | `"baGua#2"` | 獨立計算 |
| 實例 3 (`baGua#3`) | `"baGua#3"` | 獨立計算 |

**結論**：不同實例的相同技能，`trigger_count` 互不影響。

### multiplier 支援

`can_trigger` 可返回 `"skillName*multiplier"` 格式，系統會自動加入對應數量的 SkillContext：

```lua
-- 返回 "xxx*3" 表示觸發 3 次
can_trigger = function(skill, event, room, player, data)
    local damage = data:toDamage()
    if damage and damage.damage > 0 then
        return skill:objectName() .. "*" .. damage.damage
    end
    return false
end
```

### Lua 使用範例

#### 方式一：使用 multiplier（系統自動觸發多次）

```lua
skill = sgs.CreateTriggerV2Skill{
    name = "xxx",
    events = {sgs.Damaged},
    can_trigger = function(skill, event, room, player, data)
        -- data 是原始事件數據
        local damage = data:toDamage()
        if damage and damage.damage > 0 then
            -- 返回 "xxx*3" 表示觸發 3 次
            return skill:objectName() .. "*" .. damage.damage
        end
        return false
    end,
    on_effect = function(skill, event, room, player, ctx)
        -- ctx 是 SkillContext 引用
        player:drawCards(1)
        return false
    end
}
```

#### 方式二：使用 trigger_count（手動控制）

```lua
skill = sgs.CreateTriggerV2Skill{
    name = "yyy",
    events = {sgs.Damaged},
    can_trigger = function(skill, event, room, player, data)
        -- data 是原始事件數據
        local damage = data:toDamage()
        if not damage or damage.damage <= 0 then return false end
        
        local ctx = data:toSkillContext()
        -- ctx.trigger_count 是該技能實例在本次事件中已觸發次數
        if ctx.trigger_count >= damage.damage then
            return false  -- 已觸發足夠次數
        end
        
        return skill:objectName()
    end,
    on_effect = function(skill, event, room, player, ctx)
        -- ctx 是 SkillContext 引用
        player:drawCards(1)
        return false
    end
}
```

### 注意事項

1. **避免無限循環**：`can_trigger` 必須在適當條件下返回 `false`，否則會無限循環
2. **性能考量**：每次循環都會重新調用所有 v2 技能的 `triggerable()`
3. **實例獨立**：不同 instanceId 的技能實例，`trigger_count` 獨立計算

---

## SkillContext 擴展：choice 與 extra_data

### 概述

`SkillContext` 新增 `choice` 和 `extra_data` 欄位，支援跨階段傳遞選擇結果和額外資料。

### 新增欄位

```cpp
struct SkillContext {
    // ... 其他欄位 ...
    QString choice;        // askForChoice 結果
    QVariant extra_data;   // 任意額外資料
};
```

### 新增事件

```
EventSkillWillInvoke
EventSkillAskForChoice  // 新增：詢問選擇前觸發，其他技能可修改選項
EventSkillPay
EventSkillTargetConfirming
...
```

### 使用方式

#### on_cost：詢問選擇

```lua
on_cost = function(skill, event, room, player, ctx)
    local choices = {"option1", "option2", "beishui"}
    local choice = room:askForChoice(player, "skill", table.concat(choices, "+"))
    ctx.choice = choice  -- 存入 ctx
    return true
end
```

#### on_pay：根據選擇支付代價

```lua
on_pay = function(skill, event, room, player, ctx)
    if ctx.choice == "beishui" then
        local damage = sgs.DamageStruct()
        damage.from = player
        damage.to = player
        damage.damage = 1
        room:damage(damage)
        
        if not player:isAlive() then
            return false  -- 玩家死亡，不執行 effect
        end
    end
    return true
end
```

#### on_effect：根據選擇執行效果

```lua
on_effect = function(skill, event, room, player, ctx)
    if ctx.choice == "option1" then
        -- 效果 1
    elseif ctx.choice == "option2" then
        -- 效果 2
    elseif ctx.choice == "beishui" then
        -- 背水效果
    end
    return false
end
```

#### 其他技能讀取 choice

```lua
-- 監聽 EventSkillPay
on_trigger = function(skill, event, room, player, data)
    local ctx = data:toSkillContext()
    if ctx.choice == "beishui" then
        -- 做出反應
    end
    return false
end
```

---

## TriggerV2Skill 規範要求

### 1. 基本結構規範

#### 必填欄位

| 欄位 | 類型 | 說明 |
|------|------|------|
| `name` | `string` | 技能名稱（必須唯一） |
| `events` | `table` | 觸發事件列表 |

#### 可選欄位

| 欄位 | 類型 | 預設值 | 說明 |
|------|------|--------|------|
| `frequency` | `enum` | `Skill_NotFrequent` | 技能頻率 |
| `base_amount` | `int` | `1` | 技能基礎數值 |
| `limit_scope` | `enum` | `Limit_None` | 使用次數限制範圍 |
| `max_usage_limit` | `int` | `1` | 最大使用次數 |
| `priority` | `int` | `2` | 觸發優先級 |
| `global` | `bool` | `false` | 是否全局技能 |
| `waked_skills` | `string` | `""` | 覺醒後獲得的技能 |

### 2. 回調函數規範

#### can_trigger

**職責**：檢查技能是否可觸發

**返回值**：
- `false` — 不可觸發
- `"skillName"` — 可觸發
- `"skillName*multiplier"` — 觸發多次
- `player, "skillName"` — 指定玩家觸發

**規範**：
```lua
can_trigger = function(skill, event, room, player, data)
    -- 1. 檢查玩家是否擁有技能
    if not player:hasSkill(skill:objectName()) then return false end
    
    -- 2. 檢查事件條件
    if event == sgs.Damaged then
        local damage = data:toDamage()
        if not damage or damage.damage < 1 then return false end
        return skill:objectName() .. "*" .. damage.damage
    end
    
    return false
end
```

#### on_cost

**職責**：詢問是否發動 + 選擇目標

**返回值**：
- `true` — 繼續執行
- `false` — 取消發動

**規範**：
```lua
on_cost = function(skill, event, room, player, ctx)
    -- 1. 詢問選擇（若有選項）
    local choices = {"option1", "option2"}
    local choice = room:askForChoice(player, skill:objectName(), table.concat(choices, "+"))
    ctx.choice = choice
    
    -- 2. 選擇目標（若需目標）
    local target = room:askForPlayerChosen(player, room:getOtherPlayers(player), skill:objectName())
    if not target then return false end
    ctx.targets:append(target)
    
    return true
end
```

#### on_pay

**職責**：支付代價

**返回值**：
- `true` — 支付成功，繼續執行
- `false` — 支付失敗，取消發動

**規範**：
```lua
on_pay = function(skill, event, room, player, ctx)
    -- 1. 根據 choice 決定代價
    if ctx.choice == "beishui" then
        local damage = sgs.DamageStruct()
        damage.from = player
        damage.to = player
        damage.damage = 1
        room:damage(damage)
        
        -- 2. 檢查玩家是否存活
        if not player:isAlive() then
            return false  -- 玩家死亡，不執行 effect
        end
    end
    
    return true
end
```

#### on_effect

**職責**：執行一次性效果

**返回值**：
- `true` — 中斷事件（break）
- `false` — 繼續事件

**規範**：
```lua
on_effect = function(skill, event, room, player, ctx)
    -- 1. 發送技能發動日誌
    room:sendCompulsoryTriggerLog(player, skill:objectName())
    room:broadcastSkillInvoke(skill:objectName())
    
    -- 2. 取得有效數值
    local amount = skill:getEffectiveAmount(ctx)
    
    -- 3. 執行效果
    if ctx.choice == "option1" then
        player:drawCards(amount)
    elseif ctx.choice == "option2" then
        -- 其他效果
    end
    
    return false
end
```

#### on_effect_target

**職責**：對單一目標執行效果

**返回值**：
- `true` — 中斷事件
- `false` — 繼續事件

**規範**：
```lua
on_effect_target = function(skill, event, room, player, ctx, target)
    -- 1. 檢查目標有效性
    if not target:isAlive() then return false end
    
    -- 2. 取得有效數值
    local amount = skill:getEffectiveAmount(ctx)
    
    -- 3. 對目標執行效果
    for i = 1, amount do
        getYing(target, skill:objectName())
    end
    
    return false
end
```

### 3. 數值系統規範

#### getEffectiveAmount 使用

```lua
on_effect = function(skill, event, room, player, ctx)
    local amount = skill:getEffectiveAmount(ctx)
    
    -- 使用 amount 執行效果
    for i = 1, amount do
        -- 效果邏輯
    end
    
    return false
end
```

#### modified_amount 設定

```lua
-- 在 on_cost 或其他階段設定
ctx.modified_amount = 3  -- 覆蓋基礎數值
```

### 4. 目標處理規範

#### 自動遍歷目標

```lua
on_effect = function(skill, event, room, player, ctx)
    -- 不設定 ctx.manual_effect
    -- 框架自動遍歷 ctx.targets，調用 on_effect_target
    return false
end,

on_effect_target = function(skill, event, room, player, ctx, target)
    -- 對每個目標執行
    room:doDamage(player, target, 1)
    return false
end
```

#### 手動遍歷目標

```lua
on_effect = function(skill, event, room, player, ctx)
    ctx.manual_effect = true  -- 標記手動處理
    
    -- 自訂遍歷邏輯
    local targets = sgs.SPlayerList()
    targets:append(player)
    for _, t in sgs.qlist(ctx.targets) do
        targets:append(t)
    end
    room:sortByActionOrder(targets)
    
    for _, p in sgs.qlist(targets) do
        skill:skillEffect(event, room, player, ctx, p)
    end
    
    return false
end
```

### 5. 常見模式範例

#### 模式 A：單目標技能

```lua
s4_cangzhuo = sgs.CreateTriggerV2Skill{
    name = "s4_cangzhuo",
    events = {sgs.CardsMoveOneTime},
    frequency = sgs.Skill_Frequent,
    base_amount = 1,
    
    can_trigger = function(skill, event, room, player, data)
        if not player:hasSkill("s4_cangzhuo") then return false end
        -- 條件檢查...
        return "s4_cangzhuo"
    end,
    
    on_cost = function(skill, event, room, player, ctx)
        local target = room:askForPlayerChosen(player, room:getOtherPlayers(player), 
            "s4_cangzhuo", "s4_cangzhuo-invoke", true, true)
        if target then
            ctx.targets:append(target)
            return true
        end
        return false
    end,
    
    on_effect = function(skill, event, room, player, ctx)
        room:broadcastSkillInvoke("s4_cangzhuo")
        ctx.manual_effect = true
        
        -- 包含自己一起結算
        local targets = sgs.SPlayerList()
        targets:append(player)
        for _, t in sgs.qlist(ctx.targets) do
            targets:append(t)
        end
        room:sortByActionOrder(targets)
        
        for _, p in sgs.qlist(targets) do
            skill:skillEffect(event, room, player, ctx, p)
        end
        return false
    end,

    on_effect_target = function(skill, event, room, player, ctx, target)
        local amount = skill:getEffectiveAmount(ctx)
        for i = 1, amount do
            getYing(target, skill:objectName())
        end
        return false
    end,
}
```

#### 模式 B：多目標技能

```lua
s4_lizhan = sgs.CreateTriggerV2Skill{
    name = "s4_lizhan",
    events = {sgs.Damaged},
    frequency = sgs.Skill_Frequent,
    base_amount = 0,
    
    can_trigger = function(skill, event, room, player, data)
        if not player:isAlive() then return false end
        if not player:hasSkill("s4_lizhan") then return false end
        local damage = data:toDamage()
        if not damage or damage.damage < 1 then return false end
        return "s4_lizhan*" .. damage.damage
    end,
    
    on_cost = function(skill, event, room, player, ctx)
        local targets = sgs.SPlayerList()
        for _, p in sgs.qlist(room:getAlivePlayers()) do
            if p:isWounded() then
                targets:append(p)
            end
        end
        if targets:isEmpty() then return false end
        
        local chosen_players = room:askForPlayersChosen(player, targets, "s4_lizhan", 
            0, targets:length(), "@s4_lizhan-choose", true, true)
        if not chosen_players or chosen_players:isEmpty() then return false end
        
        for _, p in sgs.qlist(chosen_players) do
            ctx.targets:append(p)
        end
        ctx.modified_amount = ctx.targets:length()
        return true
    end,
    
    on_effect = function(skill, event, room, player, ctx)
        local amount = skill:getEffectiveAmount(ctx)
        player:drawCards(amount, "s4_lizhan")
        return false
    end,
    
    on_effect_target = function(skill, event, room, player, ctx, target)
        if player:isKongcheng() or player:objectName() == target:objectName() then 
            return false 
        end
        if not player:isAlive() or not target:isAlive() then return false end
    
        local card = room:askForCard(player, ".!", "@s4_lizhan-give::"..target:objectName(), 
            ToData(target), sgs.Card_MethodNone)
        if card then
            room:giveCard(player, target, card, skill:objectName())
        end
        return false
    end,
}
```

#### 模式 C：選擇分支技能

```lua
s4_zhiji = sgs.CreateTriggerV2Skill{
    name = "s4_zhiji",
    events = {sgs.EventPhaseProceeding},
    frequency = sgs.Skill_Compulsory,
    waked_skills = "guanxing+kanpo",
    base_amount = 1,
    
    can_trigger = function(skill, event, room, player, data)
        if not player:hasSkill("s4_zhiji") then return false end
        if player:getPhase() ~= sgs.Player_Start then return false end
        return "s4_zhiji"
    end,
    
    on_cost = function(skill, event, room, player, ctx)
        local choices = {"s4_zhiji_guanxing", "s4_zhiji_kanpo", "beishui"}
        local choice = room:askForChoice(player, "s4_zhiji", table.concat(choices, "+"), ctx.original_data)
        ctx.choice = choice
        return true
    end,
    
    on_pay = function(skill, event, room, player, ctx)
        if ctx.choice == "beishui" then
            local amount = skill:getEffectiveAmount(ctx)
            local damage = sgs.DamageStruct()
            damage.from = player
            damage.to = player
            damage.damage = amount
            room:damage(damage)
            if not player:isAlive() then
                return false
            end
        end
        return true
    end,
    
    on_effect = function(skill, event, room, player, ctx)
        room:sendCompulsoryTriggerLog(player, "s4_zhiji")
        room:broadcastSkillInvoke("s4_zhiji")
        
        if ctx.choice == "s4_zhiji_guanxing" then
            room:acquireNextTurnSkills(player, "s4_zhiji", "guanxing")
        elseif ctx.choice == "s4_zhiji_kanpo" then
            room:acquireNextTurnSkills(player, "s4_zhiji", "kanpo")
        elseif ctx.choice == "beishui" then
            room:acquireNextTurnSkills(player, "s4_zhiji", "guanxing")
            room:acquireNextTurnSkills(player, "s4_zhiji", "kanpo")
        end
        
        return false
    end,
}
```

#### 模式 D：多事件技能

```lua
s4_banjiang = sgs.CreateTriggerV2Skill{
    name = "s4_banjiang",
    events = {sgs.Damaged, sgs.DrawNCards, sgs.ChangeSlash},
    frequency = sgs.Skill_Compulsory,
    base_amount = 1,
    
    can_trigger = function(skill, event, room, player, data)
        if not player:hasSkill("s4_banjiang") then return false end
        
        if event == sgs.Damaged then
            local damage = data:toDamage()
            if damage and damage.damage > 0 then
                return "s4_banjiang*" .. damage.damage
            end
        elseif event == sgs.DrawNCards then
            local draw = data:toDraw()
            if draw.reason == "draw_phase" then
                if player:getMark("s4_banjiang_draw-SelfdrawClear") > 0 then
                    return "s4_banjiang"
                end
            end
        elseif event == sgs.ChangeSlash then
            if player:getPhase() == sgs.Player_Play and 
               player:getMark("s4_banjiang_slash-SelfPlayClear") > 0 then
                return "s4_banjiang"
            end
        end
        
        return false
    end,
    
    on_effect = function(skill, event, room, player, ctx)
        if event == sgs.Damaged then
            local amount = skill:getEffectiveAmount(ctx)
            
            room:sendCompulsoryTriggerLog(player, "s4_banjiang")
            room:broadcastSkillInvoke("s4_banjiang")
            
            room:addPlayerMark(player, "s4_banjiang_draw-SelfdrawClear", amount)
            room:addPlayerMark(player, "s4_banjiang_slash-SelfPlayClear", amount)
            room:addPlayerMark(player, "&s4_banjiang+sys_-SelfClear", amount)
            
        elseif event == sgs.DrawNCards then
            local draw = ctx.original_data:toDraw()
            if draw.reason == "draw_phase" then
                local bonus = player:getMark("s4_banjiang_draw-SelfdrawClear")
                if bonus > 0 then
                    room:sendCompulsoryTriggerLog(player, "s4_banjiang")
                    draw.num = draw.num + bonus
                    ctx.original_data:setValue(draw)
                end
            end
            
        elseif event == sgs.ChangeSlash then
            local use = ctx.original_data:toCardUse()
            if use.card and use.card:isKindOf("Slash") and 
               use.from:objectName() == player:objectName() then
                room:sendCompulsoryTriggerLog(player, "s4_banjiang")
                local ice_slash = sgs.Sanguosha:cloneCard("ice_slash", 
                    use.card:getSuit(), use.card:getNumber())
                ice_slash:addSubcards(use.card:getSubcards())
                ice_slash:setSkillName("s4_banjiang")
                use:changeCard(ice_slash)
                ctx.original_data:setValue(use)
            end
        end
        
        return false
    end,
}
```

### 6. 常見錯誤與修正

#### 錯誤 1：忘記檢查技能擁有

```lua
-- ❌ 錯誤
can_trigger = function(skill, event, room, player, data)
    return "skill_name"
end

-- ✅ 正確
can_trigger = function(skill, event, room, player, data)
    if not player:hasSkill(skill:objectName()) then return false end
    return "skill_name"
end
```

#### 錯誤 2：忘記返回 false

```lua
-- ❌ 錯誤
can_trigger = function(skill, event, room, player, data)
    if condition then
        return "skill_name"
    end
    -- 缺少 return false
end

-- ✅ 正確
can_trigger = function(skill, event, room, player, data)
    if condition then
        return "skill_name"
    end
    return false
end
```

#### 錯誤 3：on_pay 未檢查玩家存活

```lua
-- ❌ 錯誤
on_pay = function(skill, event, room, player, ctx)
    room:damage(damage)
    return true  -- 玩家可能已死亡
end

-- ✅ 正確
on_pay = function(skill, event, room, player, ctx)
    room:damage(damage)
    if not player:isAlive() then
        return false  -- 玩家死亡，不執行 effect
    end
    return true
end
```

#### 錯誤 4：忘記 setValue 寫回

```lua
-- ❌ 錯誤
on_effect = function(skill, event, room, player, ctx)
    local draw = ctx.original_data:toDraw()
    draw.num = draw.num + 1
    -- 忘記寫回
end

-- ✅ 正確
on_effect = function(skill, event, room, player, ctx)
    local draw = ctx.original_data:toDraw()
    draw.num = draw.num + 1
    ctx.original_data:setValue(draw)
end
```

### 7. 效能優化建議

1. **can_trigger 盡早返回**：先檢查最便宜的条件
2. **避免重複計算**：將計算結果存入 `ctx.extra_data`
3. **善用 multiplier**：讓系統自動觸發多次，而非手動循環
4. **減少 on_effect_target 檢查**：在 on_cost 已篩選目標
