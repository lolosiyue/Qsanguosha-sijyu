# SkillCard V2 時機橋接計劃

## 目標

讓 SkillCard（含 LuaSkillCard）在 `Room::useCard()` 中觸發 V2Skill 的時機事件，使其他 V2Skill 能攔截/修改 SkillCard 的使用。不影響現有 SkillCard 代碼。

## 設計原則

1. **復用現有 V2 事件**：`EventSkillWillInvoke` / `EventSkillPay` / `EventSkillTargetConfirming` / `EventSkillInvoking` / `EventSkillEffect` / `EventSkillEffectTarget` / `EventSkillEffectFinished`
2. **最小侵入性**：不強制修改現有 SkillCard 代碼，新增欄位有預設值
3. **SkillContext 通過 Room Tag 傳遞**：避免修改 CardUseStruct 結構
4. **Tag 名稱格式**：`"SkillCardContext_" + skillName + "_" + instanceId`（避免多個同名技能實例衝突）
5. **validate() 中設置 owner**：SkillCard 可在 validate() 中調用 setSkillOwner()
6. **bypass_cost 與 will_throw 整合**：EventSkillPay 中設置 bypass_cost 可跳過 will_throw 棄牌
7. **EventSkillEffectTarget 在 GameRule 中觸發**：在 CardEffected 處理中，onEffect() 調用前觸發

## 時機流程

```
Room::useCard(CardUseStruct&use)
    │
    ├─ use.card->validate(use)
    │       └─ SkillCard::setSkillOwner() 可在此設置技能擁有者
    │
    ├─ 【SkillCard only】構建 SkillContext
    │       └─ Tag Key: "SkillCardContext_" + skillName + "_" + instanceId
    │
    ├─ EventSkillWillInvoke
    │       └─ 若 ctx.is_canceled → return false（取消卡牌使用）
    │
    ├─ EventSkillPay
    │       └─ 若 ctx.bypass_cost → use.bypass_cost = true（跳過 will_throw）
    │
    ├─ EventSkillTargetConfirming
    │       └─ use.to = ctx.updated_targets（目標修改）
    │
    ├─ ... 原有 history 處理 ...
    │
    ├─ EventSkillInvoking
    │
    ├─ EventSkillEffect
    │       └─ 若返回 true → skipOnUse = true（跳過 onUse）
    │
    ├─ if (!skipOnUse)
    │       use.card->onUse(this, use)
    │       │
    │       ├─ PreCardUsed
    │       ├─ will_throw 處理（檢查 use.bypass_cost）
    │       ├─ CardUsed
    │       │
    │       ├─ Card::use() → room->cardEffect()（如果走標準流程）
    │       │       │
    │       │       └─ GameRule::CardEffected
    │       │               │
    │       │               ├─ EventSkillEffectTarget  ← 新增：對每個目標
    │       │               │       └─ 若返回 true → 跳過此目標的 onEffect
    │       │               │
    │       │               └─ effect.card->onEffect(effect)
    │       │
    │       └─ CardFinished
    │
    └─ EventSkillEffectFinished
            └─ 清理 room->removeTag(tagKey)
```

## 事件語義對照

| 事件 | 觸發時機 | data 類型 | 作用 |
|------|----------|-----------|------|
| `EventSkillWillInvoke` | 卡牌使用前 | `SkillContext` | 設 `is_canceled=true` 可取消 |
| `EventSkillPay` | 代價支付前 | `SkillContext` | 設 `bypass_cost=true` 可跳過 will_throw |
| `EventSkillTargetConfirming` | 目標確認 | `SkillContext` | 修改 `updated_targets` 可改變目標 |
| `EventSkillInvoking` | 正式宣告 | `SkillContext` | 通知技能即將發動 |
| `EventSkillEffect` | 效果執行前 | `SkillContext` | 返回 `true` 可跳過 onUse |
| `EventSkillEffectTarget` | onEffect 前（對每個目標） | `SkillContext` | 返回 `true` 可跳過該目標的 onEffect |
| `EventSkillEffectFinished` | 效果完成後 | `SkillContext` | 後處理、清理 |

## 修改清單

### 1. src/core/card.h

**SkillCard 新增欄位：**

```cpp
class SkillCard : public Card {
    Q_OBJECT
public:
    SkillCard();
    // ... 現有方法 ...
    
    // --- 新增 ---
    void setSkillInstanceId(int id) { m_skillInstanceId = id; }
    int getSkillInstanceId() const { return m_skillInstanceId; }
    void setSkillOwner(ServerPlayer *owner) { m_skillOwner = owner; }
    ServerPlayer *getSkillOwner() const { return m_skillOwner; }
    
protected:
    QString user_string;
    // --- 新增 ---
    int m_skillInstanceId;
    ServerPlayer *m_skillOwner;
};
```

### 2. src/core/card.cpp

**SkillCard 建構子初始化新欄位：**

```cpp
SkillCard::SkillCard() : Card(NoSuit, 0), m_skillInstanceId(0), m_skillOwner(nullptr)
{
}
```

**Card::onUse() 中 will_throw 檢查 bypass_cost：**

```cpp
// L695 原來：
} else if (card_use.card->willThrow()){

// 改為：
} else if (card_use.card->willThrow() && !card_use.bypass_cost){
```

### 3. src/core/structs.h

**CardUseStruct 新增 bypass_cost 欄位：**

```cpp
struct CardUseStruct {
    // ... 現有字段 ...
    bool bypass_cost;  // 新增：是否跳過代價（will_throw）
    
    CardUseStruct();  // 建構子中 bypass_cost = false
    // ...
};
```

### 4. src/core/structs.cpp

**CardUseStruct 建構子初始化：**

```cpp
CardUseStruct::CardUseStruct()
    : card(nullptr), from(nullptr), m_isOwnerUse(true), m_addHistory(true),
      m_isHandcard(false), whocard(nullptr), who(nullptr), extra_use(0),
      bypass_cost(false)  // 新增
{
}
```

### 5. src/core/lua-wrapper.h

**LuaSkillCard 新增 on_validate 回調：**

```cpp
class LuaSkillCard : public SkillCard {
    Q_OBJECT
public:
    // ... 現有方法 ...
    
    virtual const Card *validate(CardUseStruct &cardUse) const;
    
    // --- 新增 ---
    LuaFunction on_validate;
    
protected:
    // ...
};
```

### 6. src/core/lua-wrapper.cpp

**LuaSkillCard::validate() 調用 on_validate 回調：**

```cpp
const Card* LuaSkillCard::validate(CardUseStruct &cardUse) const
{
    if (on_validate == 0) {
        return this;
    }
    
    Room *room = cardUse.from->getRoom();
    lua_State *L = room->getLuaState();
    
    lua_rawgeti(L, LUA_REGISTRYINDEX, on_validate);
    
    LuaSkillCard *self = const_cast<LuaSkillCard *>(this);
    SWIG_NewPointerObj(L, self, SWIGTYPE_p_LuaSkillCard, 0);
    
    SWIG_NewPointerObj(L, &cardUse, SWIGTYPE_p_CardUseStruct, 0);
    
    int error = lua_pcall(L, 2, 1, 0);
    if (error) {
        const char *error_msg = lua_tostring(L, -1);
        lua_pop(L, 1);
        room->output(error_msg);
        return this;
    }
    
    // 返回值可以是 self 或 nil（表示不變）
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return this;
    }
    
    // 如果返回另一個 Card，則使用該 Card
    // 目前簡化處理，返回 this
    lua_pop(L, 1);
    return this;
}
```

**LuaSkillCard::clone() 複製新欄位：**

```cpp
LuaSkillCard *LuaSkillCard::clone() const
{
    LuaSkillCard *c = new LuaSkillCard(objectName(), m_skillName);
    c->m_skillInstanceId = m_skillInstanceId;  // 新增
    c->m_skillOwner = m_skillOwner;            // 新增
    // ... 其他複製 ...
    return c;
}
```

### 7. src/server/room.cpp

**useCard() 中新增 SkillCard V2 時機橋接：**

```cpp
bool Room::useCard(CardUseStruct&use, bool add_history)
{
    // ... L5011-5017 原有校驗和 validate ...
    if ((!use.card->canRecast()||use.from->isCardLimited(use.card,Card::MethodRecast))&&use.from->isCardLimited(use.card,use.card->getHandlingMethod()))
        return false;
    use.m_addHistory = add_history;
    if (Sanguosha->hasResidueUnlimited(use.from, use.card, use.to.isEmpty() ? nullptr : use.to.first()))
        use.m_addHistory = false;
    const Card *card = use.card->validate(use);
    if (card == nullptr) return false;
    
    // === 新增：SkillCard V2 時機橋接 ===
    bool isSkillCard = card->isKindOf("SkillCard");
    SkillContext skillCardCtx;
    QVariant skillCardCtxData;
    bool skipOnUse = false;
    QString tagKey;  // Tag key 用於存儲 SkillContext
    
    if (isSkillCard) {
        SkillCard *skillCard = qobject_cast<SkillCard*>(card);
        
        // 構建 SkillContext
        skillCardCtx.skill_name = card->getSkillName().isEmpty() 
                                  ? card->objectName() : card->getSkillName();
        skillCardCtx.invoker = use.from;
        skillCardCtx.owner = skillCard->getSkillOwner() 
                             ? skillCard->getSkillOwner() : use.from;
        skillCardCtx.targets = use.to;
        skillCardCtx.instanceID = skillCard->getSkillInstanceId();
        skillCardCtx.use_card = card;
        
        // 計算 Tag Key: "SkillCardContext_" + skillName + "_" + instanceId
        tagKey = "SkillCardContext_" + skillCardCtx.skill_name + "_" 
                 + QString::number(skillCardCtx.instanceID);
        
        // 存入 Room Tag
        setTag(tagKey, QVariant::fromValue(skillCardCtx));
        
        // 1. EventSkillWillInvoke
        skillCardCtx.current_event = EventSkillWillInvoke;
        skillCardCtxData = QVariant::fromValue(skillCardCtx);
        thread->trigger(EventSkillWillInvoke, this, use.from, skillCardCtxData);
        skillCardCtx = skillCardCtxData.value<SkillContext>();
        if (skillCardCtx.is_canceled) {
            removeTag(tagKey);
            return false;
        }
        
        // 2. EventSkillPay
        if (!skillCardCtx.bypass_cost) {
            skillCardCtx.current_event = EventSkillPay;
            skillCardCtxData = QVariant::fromValue(skillCardCtx);
            thread->trigger(EventSkillPay, this, use.from, skillCardCtxData);
            skillCardCtx = skillCardCtxData.value<SkillContext>();
        }
        use.bypass_cost = skillCardCtx.bypass_cost;
        
        // 3. EventSkillTargetConfirming
        skillCardCtx.updated_targets = skillCardCtx.targets;
        skillCardCtx.current_event = EventSkillTargetConfirming;
        skillCardCtxData = QVariant::fromValue(skillCardCtx);
        thread->trigger(EventSkillTargetConfirming, this, use.from, skillCardCtxData);
        skillCardCtx = skillCardCtxData.value<SkillContext>();
        if (!skillCardCtx.updated_targets.isEmpty()) {
            use.to = skillCardCtx.updated_targets;
        }
        
        // 更新 Tag
        setTag(tagKey, QVariant::fromValue(skillCardCtx));
    }
    // === 新增結束 ===
    
    // ... L5018-5034 原有 ids/history 處理 ...
    QList<int> ids;
    if (use.card->isVirtualCard()) ids = use.card->getSubcards();
    else ids << use.card->getId();
    foreach(int id, ids){
        use.m_isHandcard = use.from->handCards().contains(id);
        if (!use.m_isHandcard) break;
    }
    QString key = use.card->getClassName();
    tag.remove("UseHistory"+use.card->toString());
    if (use.card->inherits("LuaSkillCard")) key = "#"+use.card->objectName();
    if(_m_roomState.getCurrentCardUseReason()==CardUseStruct::CARD_USE_REASON_PLAY)
        addPlayerHistory(nullptr, "pushPile");
    if (use.m_addHistory){
        add_history = true;
        addPlayerHistory(use.from, key);
    }
    
    // === 新增：EventSkillInvoking ===
    if (isSkillCard) {
        skillCardCtx = getTag(tagKey).value<SkillContext>();
        skillCardCtx.current_event = EventSkillInvoking;
        skillCardCtxData = QVariant::fromValue(skillCardCtx);
        thread->trigger(EventSkillInvoking, this, use.from, skillCardCtxData);
        skillCardCtx = skillCardCtxData.value<SkillContext>();
        setTag(tagKey, QVariant::fromValue(skillCardCtx));
    }
    
    // === 新增：EventSkillEffect ===
    if (isSkillCard) {
        skillCardCtx = getTag(tagKey).value<SkillContext>();
        skillCardCtx.current_event = EventSkillEffect;
        skillCardCtxData = QVariant::fromValue(skillCardCtx);
        skipOnUse = thread->trigger(EventSkillEffect, this, use.from, skillCardCtxData);
        skillCardCtx = skillCardCtxData.value<SkillContext>();
        setTag(tagKey, QVariant::fromValue(skillCardCtx));
    }
    
    try {
        if (use.card->getRealCard() == card){
            if (!use.card->isVirtualCard()){
                WrappedCard*wrapped = Sanguosha->getWrappedCard(ids.first());
                if (wrapped->isModified()) broadcastUpdateCard(m_players, ids.first(), wrapped);
            } else if (use.card->getTypeId() != Card::TypeSkill) {
                showVirtualCard(use.from, use.card);
            }
            
            // === 修改：檢查 skipOnUse ===
            if (!skipOnUse) {
                use.card->onUse(this, use);
            }
        } else {
            use.card = card;
            return useCard(use, add_history);
        }
    } catch (TriggerEvent triggerEvent){
        // ... L5049-5071 原有 exception handling ...
        if (triggerEvent == StageChange || triggerEvent == TurnBroken){
            CardMoveReason reason(CardMoveReason::S_REASON_UNKNOWN, use.from->objectName(), use.card->getSkillName(), "");
            if (use.to.size() == 1) reason.m_targetId = use.to.first()->objectName();
            foreach(int id, ids){
                if (getCardPlace(id) != Player::PlaceTable)
                    ids.removeOne(id);
            }
            moveCardsAtomic(CardsMoveStruct(ids, use.from, nullptr, Player::PlaceTable, Player::DiscardPile, reason), true);
            QVariant data = QVariant::fromValue(use);
            use.from->setFlags("Global_ProcessBroken");
            thread->trigger(CardFinished, this, use.from, data);
            use.from->setFlags("-Global_ProcessBroken");
            foreach(ServerPlayer*p, m_alivePlayers)
                p->removeQinggangTag(use.card);
            foreach(int id, pile1){
                if (getCardPlace(id) == Player::PlaceJudge)
                    moveCardTo(Sanguosha->getCard(id), nullptr, Player::DiscardPile, true);
                setCardFlag(id, "-using");
            }
        }
        
        // === 新增：異常時也要觸發 EventSkillEffectFinished ===
        if (isSkillCard) {
            skillCardCtx = getTag(tagKey).value<SkillContext>();
            skillCardCtx.current_event = EventSkillEffectFinished;
            skillCardCtxData = QVariant::fromValue(skillCardCtx);
            thread->trigger(EventSkillEffectFinished, this, use.from, skillCardCtxData);
            removeTag(tagKey);
        }
        
        throw triggerEvent;
    }
    
    // === 新增：EventSkillEffectFinished ===
    if (isSkillCard) {
        skillCardCtx = getTag(tagKey).value<SkillContext>();
        skillCardCtx.current_event = EventSkillEffectFinished;
        skillCardCtxData = QVariant::fromValue(skillCardCtx);
        thread->trigger(EventSkillEffectFinished, this, use.from, skillCardCtxData);
        removeTag(tagKey);
    }
    // === 新增結束 ===
    
    if(add_history&&!use.m_addHistory)
        addPlayerHistory(use.from, key, -1);
    return true;
}
```

### 8. src/server/gamerule.cpp

**在 CardEffected 處理中插入 EventSkillEffectTarget：**

```cpp
case CardEffected: {
    CardEffectStruct effect = data.value<CardEffectStruct>();
    if(effect.nullified) {
        LogMessage log;
        log.type = "#CardNullified";
        log.from = effect.to;
        log.card_str = effect.card->toString();
        room->sendLog(log);
        return true;
    }else if(effect.card->getTypeId()>0){
        if(effect.offset_card==nullptr){
            effect.offset_card = room->isCanceled(effect);
            data.setValue(effect);
        }
        if(effect.offset_card) {
            if(!room->getThread()->trigger(CardOffset,room,effect.from,data)){
                effect.to->setFlags("Global_NonSkillNullify");
                return true;
            }
        }
        room->getThread()->trigger(CardOnEffect,room,effect.to,data);
    }
    
    // === 新增：SkillCard EventSkillEffectTarget ===
    bool skipThisTarget = false;
    if (effect.card->isKindOf("SkillCard")) {
        SkillCard *skillCard = qobject_cast<SkillCard*>(effect.card);
        QString skillName = effect.card->getSkillName().isEmpty() 
                            ? effect.card->objectName() : effect.card->getSkillName();
        QString tagKey = "SkillCardContext_" + skillName + "_" 
                         + QString::number(skillCard->getSkillInstanceId());
        
        SkillContext ctx = room->getTag(tagKey).value<SkillContext>();
        ctx.current_event = EventSkillEffectTarget;
        
        QVariant ctxData = QVariant::fromValue(ctx);
        skipThisTarget = room->getThread()->trigger(EventSkillEffectTarget, room, effect.to, ctxData);
        
        // 更新 Tag
        ctx = ctxData.value<SkillContext>();
        room->setTag(tagKey, QVariant::fromValue(ctx));
    }
    // === 新增結束 ===
    
    for (int i=0;i<=effect.extra_effect;i++){
        if(effect.to->isAlive()) {
            // === 修改：檢查 skipThisTarget ===
            if (!skipThisTarget) {
                effect.card->onEffect(effect);
            }
        } else {
            break;
        }
    }
    break;
}
```

### 9. swig/sanguosha.i

**SkillCard SWIG 綁定新增：**

```swig
class SkillCard : public Card {
public:
    // ... 現有方法 ...
    
    void setSkillInstanceId(int id);
    int getSkillInstanceId() const;
    void setSkillOwner(ServerPlayer *owner);
    ServerPlayer *getSkillOwner() const;
};
```

**CardUseStruct SWIG 綁定新增：**

```swig
struct CardUseStruct {
    // ... 現有字段 ...
    bool bypass_cost;
};
```

### 10. swig/luaskills.i

**LuaSkillCard 新增 on_validate 綁定：**

```swig
class LuaSkillCard : public SkillCard {
public:
    // ... 現有欄位 ...
    LuaFunction on_validate;
};
```

### 11. lua/sgs_ex.lua

**CreateSkillCard 支援新參數：**

```lua
function sgs.CreateSkillCard(spec)
    assert(spec.name)
    if spec.skill_name then assert(type(spec.skill_name)=="string") end
    local card = sgs.LuaSkillCard(spec.name, spec.skill_name)
    
    -- ... 現有處理 ...
    
    -- 新增：on_validate 回調
    if type(spec.on_validate) == "function" then
        card.on_validate = spec.on_validate
    end
    
    -- 新增：skill_instance_id
    if type(spec.skill_instance_id) == "number" then
        card:setSkillInstanceId(spec.skill_instance_id)
    end
    
    return card
end
```

## Lua 使用範例

### 1. 定義帶 owner 的 SkillCard（如 Huangtian 模式）

```lua
local huangtianCard = sgs.CreateSkillCard{
    name = "HuangtianCard",
    skill_name = "huangtian",
    
    on_validate = function(self, use)
        -- 設置技能擁有者（張角）
        if not use.to:isEmpty() and use.to:first():hasLordSkill("huangtian") then
            self:setSkillOwner(use.to:first())
        end
        return self
    end,
    
    on_use = function(self, room, source, targets)
        -- 可訪問 ctx（Tag Key: "SkillCardContext_huangtian_0"）
        local tagKey = "SkillCardContext_" .. self:getSkillName() .. "_" .. self:getSkillInstanceId()
        local ctx = room:getTag(tagKey):toSkillContext()
        -- ctx.owner 是張角，ctx.invoker 是使用卡牌的群勢力角色
        
        -- 原有效果
        foreach _, target in sgs.qlist(targets) do
            target:obtainCard(self)
        end
    end
}
```

### 2. V2Skill 攔截 SkillCard 使用

```lua
interceptor = sgs.CreateTriggerV2Skill{
    name = "interceptor",
    events = {sgs.EventSkillWillInvoke},
    can_trigger = function(skill, event, room, player, data)
        local ctx = data:toSkillContext()
        if ctx.skill_name == "huangtian" then
            return "interceptor"
        end
        return false
    end,
    on_effect = function(skill, event, room, player, ctx)
        ctx.is_canceled = true  -- 取消黃天卡牌使用
        return false
    end
}
```

### 3. V2Skill 修改 SkillCard 目標

```lua
redirector = sgs.CreateTriggerV2Skill{
    name = "redirector",
    events = {sgs.EventSkillTargetConfirming},
    can_trigger = function(skill, event, room, player, data)
        local ctx = data:toSkillContext()
        if ctx.skill_name == "shensu" then
            return "redirector"
        end
        return false
    end,
    on_effect = function(skill, event, room, player, ctx)
        local newTarget = room:askForPlayerChosen(ctx.owner, room:getOtherPlayers(ctx.owner), 
            "redirector", "redirector-choose")
        if newTarget then
            ctx.updated_targets:clear()
            ctx.updated_targets:append(newTarget)
        end
        return false
    end
}
```

### 4. V2Skill 跳過 SkillCard 代價

```lua
freeSkill = sgs.CreateTriggerV2Skill{
    name = "free_skill",
    events = {sgs.EventSkillPay},
    can_trigger = function(skill, event, room, player, data)
        local ctx = data:toSkillContext()
        if ctx.skill_name == "myskill" and ctx.owner:hasSkill("free_skill") then
            return "free_skill"
        end
        return false
    end,
    on_effect = function(skill, event, room, player, ctx)
        ctx.bypass_cost = true  -- 跳過 will_throw 棄牌
        return false
    end
}
```

### 5. V2Skill 完全跳過 SkillCard 效果

```lua
skipEffect = sgs.CreateTriggerV2Skill{
    name = "skip_effect",
    events = {sgs.EventSkillEffect},
    can_trigger = function(skill, event, room, player, data)
        local ctx = data:toSkillContext()
        if ctx.skill_name == "myskill" and someCondition then
            return "skip_effect"
        end
        return false
    end,
    on_effect = function(skill, event, room, player, ctx)
        return true  -- 跳過 onUse()
    end
}
```

### 6. V2Skill 跳過 SkillCard 對特定目標的效果

```lua
skipTarget = sgs.CreateTriggerV2Skill{
    name = "skip_target",
    events = {sgs.EventSkillEffectTarget},
    can_trigger = function(skill, event, room, player, data)
        local ctx = data:toSkillContext()
        if ctx.skill_name == "myskill" and player:hasSkill("skip_target") then
            return "skip_target"
        end
        return false
    end,
    on_effect = function(skill, event, room, player, ctx)
        return true  -- 跳過此目標的 onEffect()
    end
}
```

## 不影響原功能保證

| 項目 | 保證 |
|------|------|
| 普通卡牌 | `isKindOf("SkillCard")` 排除基本牌/錦囊/裝備 |
| 舊 SkillCard | `m_skillInstanceId=0`、`m_skillOwner=nullptr` 預設值保證行為不變 |
| CardUseStruct | `bypass_cost=false` 預設值保證原有 will_throw 行為 |
| will_throw | `!card_use.bypass_cost` 預設為 `true`，原有行為不變 |
| 無 V2Skill 監聽 | `thread->trigger()` 直接返回，不影響流程 |
| Room Tag | 使用專用 key `"SkillCardContext_" + skillName + "_" + instanceId`，結束後清理 |
| 序列化 | `bypass_cost`、`m_skillInstanceId`、`m_skillOwner` 不需要序列化（伺服器端臨時狀態）|
| GameRule | EventSkillEffectTarget 只在 `isKindOf("SkillCard")` 時觸發，不影響其他卡牌的 onEffect |

## 邊緣情況處理

### 1. validate() 返回不同卡牌

當 `use.card->getRealCard() != card` 時，進入 `return useCard(use, add_history)` 分支。

**處理**：`bypass_cost` 已在 CardUseStruct 中，遞迴調用時會正確傳遞。

### 2. LuaSkillCard::clone()

需要複製 `m_skillInstanceId` 和 `m_skillOwner`。

### 3. catch 塊中的 EventSkillEffectFinished

如果 `onUse()` throw 了 `TurnBroken/StageChange`，在 catch 塊中仍需觸發 `EventSkillEffectFinished` 並清理 Tag。

### 4. Card::onUse() 中的 bypass_cost 檢查

只對 SkillCard 有意義，因為只有 SkillCard 的 `getTypeId() == TypeSkill` 才會走到 `willThrow()` 分支。

### 5. EventSkillEffectTarget 與多目標

`Room::cardEffect()` 對每個目標單獨調用。`GameRule::CardEffected` 中 `EventSkillEffectTarget` 也對每個目標單獨觸發。如果某個目標被跳過，不影響其他目標。

### 6. Tag Key 衝突

`"SkillCardContext_" + skillName + "_" + instanceId` 格式保證唯一性：
- 不同技能名不同 key
- 同名技能不同實例（instanceId）不同 key
- 同時多個 SkillCard 使用時不衝突

## 測試計劃

1. **基本功能測試**：
   - 定義一個簡單的 SkillCard，驗證 EventSkillWillInvoke/EffectFinished 正常觸發
   - 定義 V2Skill 攔截該 SkillCard，驗證 is_canceled 正常工作

2. **Huangtian 模式測試**：
   - 定義帶 on_validate 的 SkillCard，設置 setSkillOwner()
   - 驗證 ctx.owner 正確

3. **bypass_cost 測試**：
   - 定義 will_throw=true 的 SkillCard
   - 定義 V2Skill 在 EventSkillPay 中設置 bypass_cost=true
   - 驗證卡牌不被棄掉

4. **目標修改測試**：
   - 定義 V2Skill 在 EventSkillTargetConfirming 中修改目標
   - 驗證 use.to 正確更新

5. **skipOnUse 測試**：
   - 定義 V2Skill 在 EventSkillEffect 中返回 true
   - 驗證 onUse() 被跳過

6. **EventSkillEffectTarget 測試**：
   - 定義多目標 SkillCard
   - 定義 V2Skill 在 EventSkillEffectTarget 中跳過特定目標
   - 驗證其他目標正常受影響，被跳過目標不受影響

7. **Tag Key 唯一性測試**：
   - 同一玩家獲得多個同名技能實例
   - 同時使用多個同名 SkillCard
   - 驗證 ctx 不互相覆蓋

## 後續工作

1. 實作所有修改
2. 編譯測試
3. 編寫測試用例
4. 更新 memory.md

---

# ViewAs V2 Bridge 計劃

## 目標

讓 ViewAsSkill 產生的虛擬牌（如武聖轉換的殺）在 `Room::useCard()` 中觸發 V2Skill 的時機事件，使其他 V2Skill 能攔截/修改轉換技虛擬牌的使用。

## 背景與區分

### 卡牌類型對照

| 類型 | 判斷條件 | 例子 | V2 Bridge |
|------|----------|------|-----------|
| **SkillCard** | `isKindOf("SkillCard")` | 黃天卡、神速卡 | SkillCard V2 Bridge（上文） |
| **轉換技虛擬牌** | `isVirtualCard() && !getSkillName().isEmpty() && !isKindOf("SkillCard")` | 武聖紅牌當殺、龍魂方片當火殺 | **ViewAs V2 Bridge（本文）** |
| **原生實體牌** | `!isVirtualCard()` | 普通殺、閃、桃 | 無 V2 事件 |

### 現有轉換技實作方式

以武聖為例：

```cpp
// standard-generals.cpp
class Wusheng : public OneCardViewAsSkill
{
public:
    Wusheng() : OneCardViewAsSkill("wusheng")
    {
        response_or_use = true;
    }

    bool viewFilter(const Card *card) const
    {
        return card->isRed();  // 只接受紅色牌
    }

    const Card *viewAs(const Card *originalCard) const
    {
        Card *slash = new Slash(originalCard->getSuit(), originalCard->getNumber());
        slash->addSubcard(originalCard->getId());
        slash->setSkillName(objectName());  // 設置技能名
        return slash;
    }
};
```

**關鍵特徵**：
- 返回的 `Slash` 是虛擬牌（`m_id < 0`）
- `getSkillName() == "wusheng"`（非空）
- `isKindOf("Slash") == true`（仍是原生牌類型）

## 設計原則

1. **復用現有 V2 事件**：與 SkillCard 共用相同事件
2. **區分標記**：透過 `SkillContext::is_viewAs_skill` 區分 SkillCard 和 ViewAs 虛擬牌
3. **instanceID 來源**：使用 `ViewAsSkill::getInstanceId()`（繼承自 `Skill`）
4. **owner 預設值**：`owner = invoker`，可被 V2Skill 修改
5. **Tag 名稱格式**：`"ViewAsContext_" + skillName + "_" + instanceId`

## 時機流程

```
Room::useCard(CardUseStruct&use)
    │
    ├─ use.card->validate(use)
    │
    ├─ 【檢測類型】
    │   isSkillCard = card->isKindOf("SkillCard")
    │   isViewAsCard = !isSkillCard && card->isVirtualCard() 
    │                   && !card->getSkillName().isEmpty()
    │
    ├─ if (isSkillCard || isViewAsCard)
    │   │
    │   ├─ 構建 SkillContext
    │   │   ctx.skill_name = card->getSkillName()
    │   │   ctx.invoker = use.from
    │   │   ctx.owner = use.from  // 預設，V2Skill 可修改
    │   │   ctx.is_viewAs_skill = isViewAsCard
    │   │   ctx.use_card = card
    │   │   ctx.instanceID = getViewAsSkill(card->getSkillName())->getInstanceId()
    │   │
    │   ├─ 計算 Tag Key: "ViewAsContext_" + skillName + "_" + instanceId
    │   │
    │   ├─ EventSkillWillInvoke
    │   │   └─ 若 ctx.is_canceled → return false（取消卡牌使用）
    │   │
    │   ├─ EventSkillPay
    │   │   └─ 若 ctx.bypass_cost → use.bypass_cost = true
    │   │
    │   ├─ EventSkillTargetConfirming
    │   │   └─ use.to = ctx.updated_targets（目標修改）
    │   │
    │   ├─ EventSkillInvoking
    │   │
    │   ├─ EventSkillEffect
    │   │   └─ 若返回 true → skipOnUse = true
    │   │
    │   └─ 更新 Tag
    │
    ├─ if (!skipOnUse)
    │   use.card->onUse(this, use)
    │   │
    │   ├─ PreCardUsed
    │   ├─ will_throw 處理
    │   ├─ CardUsed
    │   │
    │   ├─ Card::use() → room->cardEffect()
    │   │   │
    │   │   └─ GameRule::CardEffected
    │   │   │
    │   │   ├─ EventSkillEffectTarget  ← 對每個目標
    │   │   │   └─ 若返回 true → 跳過此目標的 onEffect
    │   │   │
    │   │   └─ effect.card->onEffect(effect)
    │   │
    │   └─ CardFinished
    │
    └─ EventSkillEffectFinished
        └─ 清理 room->removeTag(tagKey)
```

## 修改清單

### 1. src/core/skill.h

**SkillContext 新增欄位：**

```cpp
struct SkillContext {
    QString skill_name;
    ServerPlayer *invoker;
    ServerPlayer *owner;
    QList<ServerPlayer *> targets;
    QList<ServerPlayer *> updated_targets;
    const Card *use_card;
    QVariant *original_data;
    int instanceID;

    ServerPlayer *preferredTarget;
    int preferredTargetSeat;

    bool is_forced;
    bool is_canceled;
    bool bypass_cost;
    bool manual_effect;
    bool is_viewAs_skill;  // 新增：標記是否為 ViewAs 虛擬牌
    TriggerEvent current_event;

    int amount;
    int modified_amount;
    int trigger_count;
    int multiplier;

    QString choice;
    QVariant extra_data;

    SkillContext() : invoker(nullptr), owner(nullptr), use_card(nullptr),
                     original_data(nullptr), instanceID(0), preferredTarget(nullptr), preferredTargetSeat(-1),
                     is_forced(false), is_canceled(false),
                     bypass_cost(false), manual_effect(false), is_viewAs_skill(false), current_event(NonTrigger),
                     amount(1), modified_amount(0), trigger_count(0), multiplier(1) {}

    QVariant toVariant() const;
};
```

### 2. src/core/skill.cpp

**SkillContext::toVariant() 支援新欄位：**

```cpp
QVariant SkillContext::toVariant() const
{
    QVariantMap map;
    // ... 現有欄位 ...
    map["is_viewAs_skill"] = is_viewAs_skill;
    // ...
    return map;
}
```

### 3. src/server/room.cpp

**useCard() 中新增 ViewAs 虛擬牌檢測與 V2 橋接：**

```cpp
bool Room::useCard(CardUseStruct&use, bool add_history)
{
    // ... L5011-5017 原有校驗和 validate ...
    if ((!use.card->canRecast()||use.from->isCardLimited(use.card,Card::MethodRecast))&&use.from->isCardLimited(use.card,use.card->getHandlingMethod()))
        return false;
    use.m_addHistory = add_history;
    if (Sanguosha->hasResidueUnlimited(use.from, use.card, use.to.isEmpty() ? nullptr : use.to.first()))
        use.m_addHistory = false;
    const Card *card = use.card->validate(use);
    if (card == nullptr) return false;
    
    // === 新增：SkillCard / ViewAs V2 時機橋接 ===
    bool isSkillCard = card->isKindOf("SkillCard");
    bool isViewAsCard = !isSkillCard && card->isVirtualCard() && !card->getSkillName().isEmpty();
    SkillContext ctx;
    QVariant ctxData;
    bool skipOnUse = false;
    QString tagKey;
    
    if (isSkillCard || isViewAsCard) {
        // 構建 SkillContext
        ctx.skill_name = card->getSkillName();
        ctx.invoker = use.from;
        ctx.owner = use.from;  // 預設，V2Skill 可修改
        ctx.targets = use.to;
        ctx.use_card = card;
        ctx.is_viewAs_skill = isViewAsCard;
        
        // 獲取 instanceID
        if (isSkillCard) {
            SkillCard *skillCard = qobject_cast<SkillCard*>(card);
            ctx.instanceID = skillCard->getSkillInstanceId();
        } else {
            const ViewAsSkill *vsSkill = Sanguosha->getViewAsSkill(card->getSkillName());
            ctx.instanceID = vsSkill ? vsSkill->getInstanceId() : 0;
        }
        
        // 計算 Tag Key
        QString prefix = isViewAsCard ? "ViewAsContext_" : "SkillCardContext_";
        tagKey = prefix + ctx.skill_name + "_" + QString::number(ctx.instanceID);
        
        // 存入 Room Tag
        setTag(tagKey, QVariant::fromValue(ctx));
        
        // 1. EventSkillWillInvoke
        ctx.current_event = EventSkillWillInvoke;
        ctxData = QVariant::fromValue(ctx);
        thread->trigger(EventSkillWillInvoke, this, use.from, ctxData);
        ctx = ctxData.value<SkillContext>();
        if (ctx.is_canceled) {
            removeTag(tagKey);
            return false;
        }
        
        // 2. EventSkillPay
        if (!ctx.bypass_cost) {
            ctx.current_event = EventSkillPay;
            ctxData = QVariant::fromValue(ctx);
            thread->trigger(EventSkillPay, this, use.from, ctxData);
            ctx = ctxData.value<SkillContext>();
        }
        use.bypass_cost = ctx.bypass_cost;
        
        // 3. EventSkillTargetConfirming
        ctx.updated_targets = ctx.targets;
        ctx.current_event = EventSkillTargetConfirming;
        ctxData = QVariant::fromValue(ctx);
        thread->trigger(EventSkillTargetConfirming, this, use.from, ctxData);
        ctx = ctxData.value<SkillContext>();
        if (!ctx.updated_targets.isEmpty()) {
            use.to = ctx.updated_targets;
        }
        
        // 更新 Tag
        setTag(tagKey, QVariant::fromValue(ctx));
    }
    // === 新增結束 ===
    
    // ... L5018-5034 原有 ids/history 處理 ...
    QList<int> ids;
    if (use.card->isVirtualCard()) ids = use.card->getSubcards();
    else ids << use.card->getId();
    foreach(int id, ids){
        use.m_isHandcard = use.from->handCards().contains(id);
        if (!use.m_isHandcard) break;
    }
    QString key = use.card->getClassName();
    tag.remove("UseHistory"+use.card->toString());
    if (use.card->inherits("LuaSkillCard")) key = "#"+use.card->objectName();
    if(_m_roomState.getCurrentCardUseReason()==CardUseStruct::CARD_USE_REASON_PLAY)
        addPlayerHistory(nullptr, "pushPile");
    if (use.m_addHistory){
        add_history = true;
        addPlayerHistory(use.from, key);
    }
    
    // === 新增：EventSkillInvoking ===
    if (isSkillCard || isViewAsCard) {
        ctx = getTag(tagKey).value<SkillContext>();
        ctx.current_event = EventSkillInvoking;
        ctxData = QVariant::fromValue(ctx);
        thread->trigger(EventSkillInvoking, this, use.from, ctxData);
        ctx = ctxData.value<SkillContext>();
        setTag(tagKey, QVariant::fromValue(ctx));
    }
    
    // === 新增：EventSkillEffect ===
    if (isSkillCard || isViewAsCard) {
        ctx = getTag(tagKey).value<SkillContext>();
        ctx.current_event = EventSkillEffect;
        ctxData = QVariant::fromValue(ctx);
        skipOnUse = thread->trigger(EventSkillEffect, this, use.from, ctxData);
        ctx = ctxData.value<SkillContext>();
        setTag(tagKey, QVariant::fromValue(ctx));
    }
    
    try {
        if (use.card->getRealCard() == card){
            if (!use.card->isVirtualCard()){
                WrappedCard*wrapped = Sanguosha->getWrappedCard(ids.first());
                if (wrapped->isModified()) broadcastUpdateCard(m_players, ids.first(), wrapped);
            } else if (use.card->getTypeId() != Card::TypeSkill) {
                showVirtualCard(use.from, use.card);
            }
            
            if (!skipOnUse) {
                use.card->onUse(this, use);
            }
        } else {
            use.card = card;
            return useCard(use, add_history);
        }
    } catch (TriggerEvent triggerEvent){
        // ... L5049-5071 原有 exception handling ...
        if (triggerEvent == StageChange || triggerEvent == TurnBroken){
            CardMoveReason reason(CardMoveReason::S_REASON_UNKNOWN, use.from->objectName(), use.card->getSkillName(), "");
            if (use.to.size() == 1) reason.m_targetId = use.to.first()->objectName();
            foreach(int id, ids){
                if (getCardPlace(id) != Player::PlaceTable)
                    ids.removeOne(id);
            }
            moveCardsAtomic(CardsMoveStruct(ids, use.from, nullptr, Player::PlaceTable, Player::DiscardPile, reason), true);
            QVariant data = QVariant::fromValue(use);
            use.from->setFlags("Global_ProcessBroken");
            thread->trigger(CardFinished, this, use.from, data);
            use.from->setFlags("-Global_ProcessBroken");
            foreach(ServerPlayer*p, m_alivePlayers)
                p->removeQinggangTag(use.card);
            foreach(int id, pile1){
                if (getCardPlace(id) == Player::PlaceJudge)
                    moveCardTo(Sanguosha->getCard(id), nullptr, Player::DiscardPile, true);
                setCardFlag(id, "-using");
            }
        }
        
        // 新增：異常時也要觸發 EventSkillEffectFinished
        if (isSkillCard || isViewAsCard) {
            ctx = getTag(tagKey).value<SkillContext>();
            ctx.current_event = EventSkillEffectFinished;
            ctxData = QVariant::fromValue(ctx);
            thread->trigger(EventSkillEffectFinished, this, use.from, ctxData);
            removeTag(tagKey);
        }
        
        throw triggerEvent;
    }
    
    // === 新增：EventSkillEffectFinished ===
    if (isSkillCard || isViewAsCard) {
        ctx = getTag(tagKey).value<SkillContext>();
        ctx.current_event = EventSkillEffectFinished;
        ctxData = QVariant::fromValue(ctx);
        thread->trigger(EventSkillEffectFinished, this, use.from, ctxData);
        removeTag(tagKey);
    }
    // === 新增結束 ===
    
    if(add_history&&!use.m_addHistory)
        addPlayerHistory(use.from, key, -1);
    return true;
}
```

### 4. src/server/gamerule.cpp

**在 CardEffected 處理中擴展 EventSkillEffectTarget 支援 ViewAs 虛擬牌：**

```cpp
case CardEffected: {
    CardEffectStruct effect = data.value<CardEffectStruct>();
    if(effect.nullified) {
        LogMessage log;
        log.type = "#CardNullified";
        log.from = effect.to;
        log.card_str = effect.card->toString();
        room->sendLog(log);
        return true;
    }else if(effect.card->getTypeId()>0){
        if(effect.offset_card==nullptr){
            effect.offset_card = room->isCanceled(effect);
            data.setValue(effect);
        }
        if(effect.offset_card) {
            if(!room->getThread()->trigger(CardOffset,room,effect.from,data)){
                effect.to->setFlags("Global_NonSkillNullify");
                return true;
            }
        }
        room->getThread()->trigger(CardOnEffect,room,effect.to,data);
    }
    
    // === 擴展：SkillCard / ViewAs EventSkillEffectTarget ===
    bool skipThisTarget = false;
    bool isSkillCard = effect.card->isKindOf("SkillCard");
    bool isViewAsCard = !isSkillCard && effect.card->isVirtualCard() 
                        && !effect.card->getSkillName().isEmpty();
    
    if (isSkillCard || isViewAsCard) {
        QString skillName = effect.card->getSkillName();
        QString prefix = isViewAsCard ? "ViewAsContext_" : "SkillCardContext_";
        
        int instanceId = 0;
        if (isSkillCard) {
            SkillCard *skillCard = qobject_cast<SkillCard*>(effect.card);
            instanceId = skillCard->getSkillInstanceId();
        } else {
            const ViewAsSkill *vsSkill = Sanguosha->getViewAsSkill(skillName);
            instanceId = vsSkill ? vsSkill->getInstanceId() : 0;
        }
        
        QString tagKey = prefix + skillName + "_" + QString::number(instanceId);
        
        SkillContext ctx = room->getTag(tagKey).value<SkillContext>();
        ctx.current_event = EventSkillEffectTarget;
        
        QVariant ctxData = QVariant::fromValue(ctx);
        skipThisTarget = room->getThread()->trigger(EventSkillEffectTarget, room, effect.to, ctxData);
        
        ctx = ctxData.value<SkillContext>();
        room->setTag(tagKey, QVariant::fromValue(ctx));
    }
    // === 擴展結束 ===
    
    for (int i=0;i<=effect.extra_effect;i++){
        if(effect.to->isAlive()) {
            if (!skipThisTarget) {
                effect.card->onEffect(effect);
            }
        } else {
            break;
        }
    }
    break;
}
```

### 5. swig/sanguosha.i

**SkillContext SWIG 綁定新增：**

```swig
struct SkillContext {
    // ... 現有欄位 ...
    bool is_viewAs_skill;
};
```

## Lua 使用範例

### 1. 攔截轉換技虛擬牌

```lua
-- 攔截武聖轉換的殺
interceptWusheng = sgs.CreateTriggerV2Skill{
    name = "intercept_wusheng",
    events = {sgs.EventSkillWillInvoke},
    can_trigger = function(skill, event, room, player, data)
        local ctx = data:toSkillContext()
        if ctx.is_viewAs_skill and ctx.skill_name == "wusheng" then
            return "intercept_wusheng"
        end
        return false
    end,
    on_effect = function(skill, event, room, player, ctx)
        ctx.is_canceled = true  -- 取消這張殺的使用
        return false
    end
}
```

### 2. 修改轉換技虛擬牌的 owner

```lua
-- 讓其他玩家的紅牌當殺視為自己使用
ownerChange = sgs.CreateTriggerV2Skill{
    name = "owner_change",
    events = {sgs.EventSkillWillInvoke},
    can_trigger = function(skill, event, room, player, data)
        local ctx = data:toSkillContext()
        if ctx.is_viewAs_skill and ctx.skill_name == "wusheng" then
            return "owner_change"
        end
        return false
    end,
    on_effect = function(skill, event, room, player, ctx)
        ctx.owner = player  -- 改為擁有此技能的玩家
        return false
    end
}
```

### 3. 修改轉換技虛擬牌的目標

```lua
-- 修改武聖殺的目標
redirectWusheng = sgs.CreateTriggerV2Skill{
    name = "redirect_wusheng",
    events = {sgs.EventSkillTargetConfirming},
    can_trigger = function(skill, event, room, player, data)
        local ctx = data:toSkillContext()
        if ctx.is_viewAs_skill and ctx.skill_name == "wusheng" then
            return "redirect_wusheng"
        end
        return false
    end,
    on_effect = function(skill, event, room, player, ctx)
        local newTarget = room:askForPlayerChosen(ctx.invoker, room:getOtherPlayers(ctx.invoker), 
            "redirect_wusheng", "choose-new-target")
        if newTarget then
            ctx.updated_targets:clear()
            ctx.updated_targets:append(newTarget)
        end
        return false
    end
}
```

### 4. 跳過轉換技虛擬牌的代價

```lua
-- 免費武聖（不棄牌）
freeWusheng = sgs.CreateTriggerV2Skill{
    name = "free_wusheng",
    events = {sgs.EventSkillPay},
    can_trigger = function(skill, event, room, player, data)
        local ctx = data:toSkillContext()
        if ctx.is_viewAs_skill and ctx.skill_name == "wusheng" 
           and ctx.invoker:hasSkill("free_wusheng") then
            return "free_wusheng"
        end
        return false
    end,
    on_effect = function(skill, event, room, player, ctx)
        ctx.bypass_cost = true  -- 跳過 will_throw 棄牌
        return false
    end
}
```

### 5. 完全跳過轉換技虛擬牌的效果

```lua
skipEffect = sgs.CreateTriggerV2Skill{
    name = "skip_effect",
    events = {sgs.EventSkillEffect},
    can_trigger = function(skill, event, room, player, data)
        local ctx = data:toSkillContext()
        if ctx.is_viewAs_skill and ctx.skill_name == "wusheng" and someCondition then
            return "skip_effect"
        end
        return false
    end,
    on_effect = function(skill, event, room, player, ctx)
        return true  -- 跳過 onUse()
    end
}
```

### 6. 跳過轉換技虛擬牌對特定目標的效果

```lua
skipTarget = sgs.CreateTriggerV2Skill{
    name = "skip_target",
    events = {sgs.EventSkillEffectTarget},
    can_trigger = function(skill, event, room, player, data)
        local ctx = data:toSkillContext()
        if ctx.is_viewAs_skill and ctx.skill_name == "wusheng" 
           and player:hasSkill("skip_target") then
            return "skip_target"
        end
        return false
    end,
    on_effect = function(skill, event, room, player, ctx)
        return true  -- 跳過此目標的 onEffect()
    end
}
```

### 7. 區分 SkillCard 和 ViewAs 虛擬牌

```lua
-- 同時攔截 SkillCard 和 ViewAs 虛擬牌
interceptAll = sgs.CreateTriggerV2Skill{
    name = "intercept_all",
    events = {sgs.EventSkillWillInvoke},
    can_trigger = function(skill, event, room, player, data)
        local ctx = data:toSkillContext()
        if ctx.skill_name == "wusheng" then
            return "intercept_all"
        end
        return false
    end,
    on_effect = function(skill, event, room, player, ctx)
        if ctx.is_viewAs_skill then
            -- ViewAs 虛擬牌邏輯
            room:sendLogMessage("#ViewAsIntercepted", ctx.invoker, ctx.skill_name)
        else
            -- SkillCard 邏輯
            room:sendLogMessage("#SkillCardIntercepted", ctx.invoker, ctx.skill_name)
        end
        return false
    end
}
```

## 不影響原功能保證

| 項目 | 保證 |
|------|------|
| 普通卡牌 | `!isVirtualCard()` 排除實體牌 |
| 原有 ViewAsSkill | `is_viewAs_skill=false` 預設值，原有行為不變 |
| 原有 SkillCard | 走原有 SkillCard V2 Bridge 流程 |
| 無 V2Skill 監聽 | `thread->trigger()` 直接返回，不影響流程 |
| Room Tag | 使用專用 key，結束後清理 |
| 序列化 | `is_viewAs_skill` 不需要序列化（伺服器端臨時狀態） |

## 邊緣情況處理

### 1. ViewAsSkill 未註冊

若 `Sanguosha->getViewAsSkill(skillName)` 返回 `nullptr`，`instanceID` 設為 0。

### 2. 多個同名轉換技

ViewAsSkill 的 `instanceId` 在創建時由全局計數器分配，不同技能實例有不同 ID。

### 3. Tag Key 衝突

`"ViewAsContext_" + skillName + "_" + instanceId` 格式保證唯一性：
- 不同技能名不同 key
- 同名技能不同實例不同 key

### 4. EventSkillEffectTarget 與多目標

`Room::cardEffect()` 對每個目標單獨調用。若某個目標被跳過，不影響其他目標。

### 5. 嵌套 ViewAsSkill

若一個 ViewAsSkill 返回的虛擬牌又被另一個 ViewAsSkill 處理，每層都會觸發獨立的 V2 事件鏈。

## 測試計劃

1. **基本功能測試**：
   - 武聖紅牌當武聖紅牌當殺，驗證 EventSkillWillInvoke/EffectFinished 正常觸發
   - 定義 V2Skill 攔截武聖，驗證 is_canceled 正常工作

2. **owner 修改測試**：
   - 定義 V2Skill 在 EventSkillWillInvoke 中修改 owner
   - 驗證 ctx.owner 正確更新

3. **bypass_cost 測試**：
   - 定義 V2Skill 在 EventSkillPay 中設置 bypass_cost=true
   - 驗證卡牌不被棄掉

4. **目標修改測試**：
   - 定義 V2Skill 在 EventSkillTargetConfirming 中修改目標
   - 驗證 use.to 正確更新

5. **skipOnUse 測試**：
   - 定義 V2Skill 在 EventSkillEffect 中返回 true
   - 驗證 onUse() 被跳過

6. **EventSkillEffectTarget 測試**：
   - 定義多目標轉換技
   - 定義 V2Skill 跳過特定目標
   - 驗證其他目標正常受影響，被跳過目標不受影響

7. **SkillCard 與 ViewAs 區分測試**：
   - 定義 V2Skill 同時監聽兩者
   - 驗證 `is_viewAs_skill` 正確區分

---

**文檔版本**：1.2
**創建日期**：2026-06-01
**更新日期**：2026-06-01
**更新內容**：新增 ViewAs V2 Bridge 章節
