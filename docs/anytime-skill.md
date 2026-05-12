# AnytimeSkill 隨時可發動技能

## 概述

AnytimeSkill 是一種特殊技能類型，玩家可以在任何時候按下技能按鈕發動，技能會在下一個 event 結算後插入執行。

與傳統 TriggerSkill 不同，AnytimeSkill：
- 不監聽特定事件
- 按鈕始終可按（不檢查 `isEnabledAtPlay`/`isEnabledAtResponse`）
- 按下後進入 pending 狀態，等待下一個 event 結算後執行

## 流程

```
玩家按下 AnytimeSkill 按鈕（始終亮著）
       ↓
  Client::triggerAnytimeSkill()
       ↓
  加入 m_anytimeSkillPending
       ↓
  按鈕變暗（disabled）
       ↓
  發送 S_COMMAND_ANYTIME_SKILL
       ↓
Server 收到
       ↓
  Room::handleAnytimeSkillRequest()
       ↓
  加入 ServerPlayer::m_pendingAnytimeSkills
       ↓
RoomThread::trigger(event) 結算完成
       ↓
  processPendingAnytimeSkills()
       ↓
  執行 AnytimeSkill::onTrigger()
       ↓
  notifyAnytimeSkillDone()
       ↓
  發送 S_COMMAND_ANYTIME_SKILL_DONE
       ↓
Client 收到
       ↓
  handleAnytimeSkillDone()
       ↓
  移除 m_anytimeSkillPending
       ↓
  按鈕變亮（enabled）
```

## Lua API

### 基本用法

```lua
local skill = sgs.CreateAnytimeSkill{
    name = "my_anytime_skill",
    frequency = sgs.Skill_Frequent,  -- 可選：Frequent, Compulsory, NotFrequent
    can_trigger = function(self, player)
        -- 返回是否可以發動
        return player:isAlive() and player:getHp() > 1
    end,
    on_trigger = function(self, room, player)
        -- 技能發動邏輯
        room:loseHp(player, 1)
        room:sendLog("#TriggerSkill", player:objectName(), self:objectName())
        return true  -- 返回 true 表示成功
    end
}
```

### 參數說明

| 參數 | 類型 | 必填 | 說明 |
|------|------|------|------|
| `name` | string | 是 | 技能名稱 |
| `frequency` | number | 否 | 技能頻率，預設 `sgs.Skill_NotFrequent` |
| `can_trigger` | function | 否 | 判斷是否可發動，預設返回 true |
| `on_trigger` | function | 否 | 技能發動邏輯，預設返回 false |

### 完整範例

```lua
-- 定義一個隨時可發動的技能：消耗 1 HP 摸 2 張牌
local bloodDrawSkill = sgs.CreateAnytimeSkill{
    name = "blooddraw",
    frequency = sgs.Skill_NotFrequent,
    can_trigger = function(self, player)
        return player:isAlive() and player:getHp() > 1
    end,
    on_trigger = function(self, room, player)
        room:loseHp(player, 1)
        room:drawCards(player, 2, self:objectName())
        room:sendLog("#TriggerSkill", player:objectName(), self:objectName())
        return true
    end
}

-- 將技能添加到武將
local general = sgs.General(extension, "my_general", "wei", 4, true, true)
general:addSkill(bloodDrawSkill)
```

## 技術細節

### 類別結構

```cpp
class AnytimeSkill : public Skill
{
public:
    AnytimeSkill(const QString &name);
    
    virtual bool canTrigger(ServerPlayer *player) const;
    virtual bool onTrigger(Room *room, ServerPlayer *player) const;
    
    bool isAnytime() const { return true; }
};
```

### 協議命令

| 命令 | 方向 | 說明 |
|------|------|------|
| `S_COMMAND_ANYTIME_SKILL` | Client → Server | 玩家按下 AnytimeSkill |
| `S_COMMAND_ANYTIME_SKILL_DONE` | Server → Client | 技能結算完成，可再次按下 |

### UI 按鈕類型

```cpp
enum SkillType
{
    ...
    S_SKILL_ANYTIME,  // 新增：AnytimeSkill 按鈕類型
    S_NUM_SKILL_TYPES
};
```

### 插入時機

AnytimeSkill 在 `RoomThread::trigger()` 的 event 結算 **後** 插入：

```cpp
bool RoomThread::trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *target, QVariant &data)
{
    // ... 執行原本的 event 結算 ...
    
    if (event_stack.isEmpty()) {
        // ... 其他處理 ...
        
        room->processPendingAnytimeSkills();  // 在此處插入 AnytimeSkill
    }
    
    return broken;
}
```

## 注意事項

1. **按鈕狀態**：按下後按鈕會變暗，直到收到 `S_COMMAND_ANYTIME_SKILL_DONE` 才恢復
2. **防止重複**：pending 狀態期間不能再次按下同一技能
3. **執行順序**：多個玩家的 pending skills 按玩家順序依次執行
4. **event 結算後**：技能在 event 結算後執行，不會打斷當前結算流程

## 待完成

- 建立 `anytime` 按鈕樣式圖示（`image/system/button/skill/anytime/*.png`）
- 編譯驗證