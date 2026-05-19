# CardLimitation 系統說明

## 概述

CardLimitation 系統用於限制玩家對特定牌的操作，包括使用、打出、棄牌、移動等。本文件說明系統架構、API 使用方式及擴展方法。

---

## 核心概念

### HandlingMethod（操作類型）

定義於 `src/core/card.h`：

| 類型 | 說明 | 字串表示 |
|------|------|----------|
| `MethodNone` | 無操作 | - |
| `MethodUse` | 使用牌 | `use` |
| `MethodResponse` | 打出牌（響應） | `response` |
| `MethodDiscard` | 棄牌 | `discard` |
| `MethodRecast` | 重鑄 | `recast` |
| `MethodPindian` | 拼點 | `pindian` |
| `MethodIgnore` | 忽略 | `ignore` |
| `MethodEffect` | 效果 | `effect` |
| `MethodPlay` | 打出 | `play` |
| `MethodMove` | 移動 | `move` |

### 限制模式（Pattern）

使用表達式模式匹配牌：

| 模式 | 說明 |
|------|------|
| `Slash` | 指定牌名 |
| `BasicCard` | 指定牌類型 |
| `.|heart` | 指定花色 |
| `.|.|.|hand` | 指定區域（手牌） |
| `1` | 指定牌 ID |
| `.` | 匹配所有牌 |

---

## API 說明

### Player 方法

#### 檢查方法

```cpp
// 檢查是否可棄牌
bool canDiscard(const Player *to, const QString &flags) const;
bool canDiscard(const Player *to, int card_id) const;

// 檢查是否可移動
bool canMove(const Player *to, const QString &flags) const;
bool canMove(const Player *to, int card_id) const;

// 檢查牌是否受限
bool isCardLimited(const Card *card, Card::HandlingMethod method, bool isHandcard = false) const;
```

#### 設置/移除限制

```cpp
// 設置限制
void setCardLimitation(const QString &limit_list, const QString &pattern, 
                       const QString &reason = "", bool single_turn = false);

// 移除限制
void removeCardLimitation(const QString &limit_list, const QString &pattern, 
                          const QString &reason = "");

// 按原因移除限制
void removeCardLimitationByReason(const QString &reason);

// 清除限制
void clearCardLimitation(bool single_turn = false);

// 獲取限制原因列表
QStringList getCardLimitationReasons(Card::HandlingMethod method) const;
```

### Room 方法

```cpp
// 設置玩家限制（同步至客戶端）
void setPlayerCardLimitation(ServerPlayer*player, const QString&limit_list,
                             const QString&pattern, const QString&reason, bool single_turn);

// 移除玩家限制
void removePlayerCardLimitation(ServerPlayer*player, const QString&limit_list,
                                const QString&pattern, const QString&reason);

// 按原因移除限制
void removePlayerCardLimitationByReason(ServerPlayer*player, const QString&reason);

// 清除玩家限制
void clearPlayerCardLimitation(ServerPlayer*player, bool single_turn);
```

---

## 使用範例

### C++ 使用

#### 設置棄牌限制

```cpp
// 限制玩家不能棄掉手牌中的殺
room->setPlayerCardLimitation(player, "discard", "Slash|.|.|hand", "skill_name", true);

// 限制玩家不能棄掉裝備區的牌
room->setPlayerCardLimitation(player, "discard", ".|.|.|equip", "skill_name", false);

// 限制玩家不能使用錦囊牌
room->setPlayerCardLimitation(player, "use", "TrickCard", "skill_name", true);
```

#### 設置移動限制

```cpp
// 限制牌不能被移動（不能被獲得、不能被轉移）
room->setPlayerCardLimitation(player, "move", QString::number(card_id), "skill_name", false);

// 檢查是否可移動
if (player->canMove(target, card_id)) {
    room->obtainCard(target, card_id);
}
```

#### 移除限制

```cpp
// 移除特定原因的限制
room->removePlayerCardLimitationByReason(player, "skill_name");

// 移除特定限制
room->removePlayerCardLimitation(player, "discard", "Slash", "skill_name");
```

### Lua 使用

#### CardLimitSkill 定義

```lua
local skill = sgs.CreateCardLimitSkill{
    name = "jilei_limit",
    limit_list = function(player, card)
        -- 返回限制類型，多個用逗號分隔
        return "use,response,discard"
    end,
    limit_pattern = function(player, card)
        -- 返回匹配模式
        if player:getMark("jilei_mark") > 0 then
            return "TrickCard"  -- 限制錦囊牌
        end
        return ""
    end,
    limit_reason = function(player, card)
        -- 返回限制原因（可選）
        return "jilei"
    end
}
```

#### 移動限制技能

```lua
local skill = sgs.CreateCardLimitSkill{
    name = "immovable_limit",
    limit_list = function(player, card)
        return "move"  -- 限制移動
    end,
    limit_pattern = function(player, card)
        -- 限制特定牌不能被移動
        if player:hasFlag("immovable") then
            return card:objectName()
        end
        return ""
    end,
    limit_reason = function(player, card)
        return "immovable_skill"
    end
}
```

---

## 整合位置

### moveCardsAtomic

`src/server/room.cpp` 中的 `moveCardsAtomic()` 方法在移動牌之前會檢查 `canMove()`：

```cpp
foreach(int id, move.card_ids){
    if (move.from && !move.from->canMove(move.from, id))
        continue;  // 跳過不可移動的牌
    filtered_move.card_ids << id;
}
```

### canMoveField / moveField

`canMoveField()` 和 `moveField()` 方法會過濾不可移動的牌：

```cpp
foreach(const Card*c, p->getCards(flags)){
    if (!p->canMove(p, c->getEffectiveId())) continue;
    // ... 檢查其他條件
}
```

---

## Reason 參數說明

### 用途

- **追蹤限制來源**：記錄是哪個技能或效果設置的限制
- **批量移除**：可按原因一次性移除多個限制
- **除錯**：方便追蹤限制的設置與移除

### 使用建議

```cpp
// 設置限制時使用技能名稱作為 reason
room->setPlayerCardLimitation(player, "move", pattern, "my_skill", true);

// 技能失效時移除相關限制
room->removePlayerCardLimitationByReason(player, "my_skill");
```

---

## 向後相容

舊 API 仍然可用，內部會自動補上空的 reason 參數：

```cpp
// 舊寫法（仍然有效）
player->setCardLimitation("discard", "Slash", true);

// 等同於
player->setCardLimitation("discard", "Slash", "", true);
```

---

## 注意事項

1. **single_turn 參數**：設為 `true` 時，限制會在回合結束時自動清除
2. **Pattern 格式**：使用 `$0`（永久）或 `$1`（單回合）後綴，系統會自動處理
3. **客戶端同步**：使用 Room 方法而非直接調用 Player 方法，以確保客戶端同步
4. **移動限制**：`MethodMove` 會影響 `obtainCard`、`moveCardsAtomic`、`moveField` 等方法

---

## 相關文件

- [TriggerV2Skill 系統說明](TriggerV2Skill系統說明.md)
- [技能數值系統](TriggerV2Skill系統說明.md#技能數值系統)
