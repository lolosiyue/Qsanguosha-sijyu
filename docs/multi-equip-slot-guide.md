# 多装备栏功能说明

## 概述

多装备栏功能允许单个装备卡占用多个装备栏位，例如一个武器同时占用武器栏和进攻马栏。此功能支持双栏装备及多个同类型装备栏的场景。

## 装备栏索引

| 索引 | 装备栏 | 说明 |
|------|--------|------|
| 0 | WeaponLocation | 武器栏 |
| 1 | ArmorLocation | 防具栏 |
| 2 | DefensiveHorseLocation | 防御马栏 (+1坐骑) |
| 3 | OffensiveHorseLocation | 进攻马栏 (-1坐骑) |
| 4 | TreasureLocation | 宝物栏 |

## C++ 接口

### EquipCard 类

```cpp
// 获取装备实际占用的所有栏位
virtual QList<int> getOccupyLocations() const;

// 设置装备占用的栏位列表（用于Lua创建的装备）
void setOccupyLocations(const QList<int> &locations);
```

默认实现返回单个 `location()`，子类可重写或通过 `setOccupyLocations()` 设置。

### Player 类

```cpp
// 获取所有指定类型的装备
QList<const EquipCard *> getWeapons() const;
QList<const EquipCard *> getArmors() const;
QList<const EquipCard *> getDefensiveHorses() const;
QList<const EquipCard *> getOffensiveHorses() const;
QList<const EquipCard *> getTreasures() const;

// 检查是否拥有指定类型的装备
bool hasWeapons() const;
bool hasArmors() const;
bool hasDefensiveHorses() const;
bool hasOffensiveHorses() const;
bool hasTreasures() const;

// 获取指定类型装备的数量
int getWeaponsCount() const;
int getArmorsCount() const;
int getDefensiveHorsesCount() const;
int getOffensiveHorsesCount() const;
int getTreasuresCount() const;

// 获取装备实际占用的栏位
QList<int> getEquipRealSlots(int card_id) const;
```

## Lua 接口

### EquipCard

```lua
-- 设置双栏装备占用栏位 0 和 3（武器栏 + 进攻马栏）
card:setOccupyLocations({0, 3})

-- 获取装备占用的栏位
local slots = card:getOccupyLocations()
-- 返回 {0, 3}
```

### Player

```lua
-- 获取角色所有武器
local weapons = player:getWeapons()
for _, weapon in ipairs(weapons) do
    room:sendLog({
        type = "#Log",
        arg = weapon:objectName()
    })
end

-- 检查是否有多个武器
if player:getWeaponsCount() > 1 then
    -- 该角色有多个武器栏
end

-- 检查是否装备了武器
if player:hasWeapons() then
    -- 至少有一件武器
end
```

## 使用示例

### 创建双栏装备

在 Lua 中创建同时占用武器栏和进攻马栏的装备：

```lua
-- 创建一个占用武器栏(0)和进攻马栏(3)的装备
local double_slot_equip = sgs.CreateWeapon{
    name = "double_strike_weapon",
    suit = sgs.Card_Spade,
    number = 1,
    range = 3,
    on_install = function(player)
        -- 装备安装时的逻辑
    end
}
double_slot_equip:setOccupyLocations({0, 3})
```

### 检查装备栏状态

```lua
-- 检查所有武器栏位是否可用
function checkWeaponSlots(player)
    local weapons = player:getWeapons()
    for _, weapon in ipairs(weapons) do
        local slots = player:getEquipRealSlots(weapon:getEffectiveId())
        room:sendLog({
            type = "#Log",
            from = player,
            arg = string.format("武器 %s 占用栏位: %s", 
                weapon:objectName(), 
                table.concat(slots, ", "))
        })
    end
end
```

## 实现细节

### 服务器端交换判定

`installEquip` 函数已更新支持多栏装备：

```cpp
void Room::installEquip(ServerPlayer* player, const QString& equip_name)
{
    // ...
    const EquipCard* equip = /* ... */;
    QList<int> occupy_slots = equip->getOccupyLocations();
    
    // 检查所有需要的栏位是否存在
    foreach(int slot, occupy_slots) {
        if(!player->hasEquipArea(slot)) return;
    }
    
    // 卸除所有已占用栏位的原有装备
    foreach(int slot, occupy_slots) {
        const Card* exEquip = player->getEquip(slot);
        if(exEquip) {
            // 移至弃牌堆
        }
    }
    // ...
}
```

### UI 显示支持

`PlayerCardContainer::_updateEquips()` 支持多栏装备显示：

1. 记录所有被占用的装备栏位（多栏装备的次要占据位置）
2. 主显示位置显示装备图标
3. 次要占据位置显示"被占用"图标和提示

## SWIG 接口更新

以下接口已添加到 `swig/sanguosha.i`：

- `swig/card.i`: `EquipCard::getOccupyLocations()`, `EquipCard::setOccupyLocations()`
- `swig/sanguosha.i`: `Player::getWeapons()`, `Player::getArmors()`, `Player::getTreasures()` 等

## 注意事项

1. `getWeapons()` 等函数返回列表，需要遍历处理
2. `hasWeapons()` 等同于 `not getWeapons():isEmpty()`
3. `getWeaponsCount()` 等同于 `getWeapons():length()`
4. Lua 列表索引从 1 开始，不是 0
5. 多栏装备移动时，目标玩家的所有占用栏位都必须为空

## 相关文件

- `src/package/standard.cpp` - `EquipCard::getOccupyLocations()` 默认实现
- `src/package/standard.h` - `EquipCard` 类定义
- `src/core/player.cpp` - `getWeapons()`, `getEquipRealSlots()` 等实现
- `src/core/player.h` - `Player` 类多装备栏函数声明
- `src/server/room.cpp` - `installEquip()`, `moveField()` 多栏支持
- `swig/card.i` - SWIG `EquipCard` 接口
- `swig/sanguosha.i` - SWIG `Player` 接口
