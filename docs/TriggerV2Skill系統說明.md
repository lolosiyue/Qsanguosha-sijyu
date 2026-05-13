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

**位置**: `src/core/skill.h` (line 9-27)

```cpp
struct SkillContext {
    QString skill_name;            // 發動的技能名稱
    ServerPlayer *invoker;        // 實際發動者（動作執行人）
    ServerPlayer *owner;          // 技能擁有者（技能來源）
    QList<ServerPlayer *> targets;           // 技能目標
    QList<ServerPlayer *> updated_targets;   // 目標替換（targetConfirming 用）
    const Card *use_card;         // 關聯卡牌（可為 nullptr）
    QVariant original_data;       // 原始觸發數據載體
    int instanceID;               // 實例 ID（區分同名技能）

    bool is_forced;               // 是否強制發動
    bool is_canceled;             // 是否被取消（willInvoke 用）
    bool bypass_cost;             // 是否免除代價（willInvoke 用）
    TriggerEvent current_event;   // 當前觸發時機
};
```

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
            │     └─ 返回 false 則跳過
            │
            ├─ trigger(EventSkillWillInvoke, ctx)    ← 公共事件
            │     └─ 註冊該事件的技能執行，可修改 ctx.is_canceled
            │
            ├─ trigger(EventSkillTargetConfirming, ctx)  ← 公共事件
            │     └─ 註冊該事件的技能執行，可修改 ctx.updated_targets
            │
            ├─ trigger(EventSkillInvoking, ctx)     ← 公共事件
            │     └─ 註冊該事件的技能執行
            │
            ├─ effect() → bool
            │     └─ 返回 true 表示 broken
            │
            ├─ trigger(EventSkillEffect, ctx)       ← 公共事件
            │
            └─ trigger(EventSkillEffectFinished, ctx) ← 公共事件
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
| `EventSkillTargetConfirming` | 目標確認 | 目標轉移（修改 `updated_targets`） | `SkillContext` |
| `EventSkillInvoking` | 正式宣告 | 觸發「技能發動後」的聯動技能 | `SkillContext` |
| `EventSkillEffect` | 結算前 | 終極無效化判定（預留） | `SkillContext` |
| `EventSkillEffectFinished` | 結算後 | 清理臨時數據、後續事件 | `SkillContext` |

## Lua 綁定

### LuaTriggerV2Skill

**位置**: `src/core/lua-wrapper.h` (line 73-127)

### 回调函数

#### 核心流程回調

| 欄位 | 類型 | 對應 C++ 方法 |
|------|------|---------------|
| `can_trigger` | `LuaFunction` | `triggerable()` |
| `on_record` | `LuaFunction` | `record()` |
| `on_cost` | `LuaFunction` | `cost()` |
| `on_effect` | `LuaFunction` | `effect()` |
| `on_turn_broken` | `LuaFunction` | 回合被打斷 |
| `check_custom_usage` | `LuaFunction` | `checkCustomUsage()` |


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
        -- 返回格式: "skillName" 或 "player+skillName1+skillName2"
        return "baGua"
    end,
    on_cost = function(skill, event, room, player, data)
        -- 返回 true 表示執行效果
        return true
    end,
    on_effect = function(skill, event, room, player, data)
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
        local ctx = data:toSkillContext()
        if ctx.skill_name == "baGua" then
            return "fengyin"  -- 響應 baGua 的 willInvoke 時機
        end
        return false
    end,
    on_effect = function(skill, event, room, player, data)
        local ctx = data:toSkillContext()
        ctx.is_canceled = true  -- 封印 baGua 技能
        -- 注意：需要將修改後的 ctx 寫回 data
        return false
    end
}
```

### Lua triggerable 返回值格式

```lua
-- 單一技能
return "baGua"

-- 多技能（同一玩家）
return "baGua+weni"

-- 指定玩家
return player, "baGua"

-- 攜帶 instanceId
return "baGua#" .. skill:getInstanceId()
```

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
            local damage = data:toDamage()
            if damage and damage.card and damage.card:isKindOf("Slash") then
                return "baGua"
            end
        end
        return false
    end,

    on_cost = function(skill, event, room, player, data)
        return room:askForCard(player, ".|black", "@baGua", data)
    end,

    on_effect = function(skill, event, room, player, data)
        -- 八卦置換效果
        return false
    end
}
```

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
    TriggerEvent current_event;    // 當前觸發時機
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

    on_cost = function(skill, event, room, player, data)
        return room:askForCard(player, ".|black", "@baGua", data)
    end,

    on_effect = function(skill, event, room, player, data)
        local ctx = sgs.SkillContext()
        ctx.invoker = player
        ctx.owner = player
        ctx.instanceID = skill:getInstanceId()
        skill:addUsage(ctx)
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
