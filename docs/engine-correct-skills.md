# 引擎全域修正技能 (Engine Correct Skills)

## 概述

引擎提供四種「全域修正技能」(Global Correct Skill)，在遊戲核心運算時自動對全場生效，不需要玩家主動發動：

| 基類 | 修正目標 | 核心虛擬函式 | 引擎入口 |
|------|---------|-------------|---------|
| `DistanceSkill` | 角色間距離 | `getCorrect(from,to)` / `getFixed(from,to)` | `Engine::correctDistance` |
| `MaxCardsSkill` | 手牌上限 | `getExtra(target)` / `getFixed(target)` | `Engine::correctMaxCards` |
| `TargetModSkill` | 卡牌目標數 / 距離限制 / 使用次數 | `getResidueNum` / `getDistanceLimit` / `getExtraTargetNum` | `Engine::correctCardTarget` |
| `AttackRangeSkill` | 攻擊範圍 | `getExtra(target,include_weapon)` / `getFixed(target,include_weapon)` | `Engine::correctAttackRange` |

## 多實例疊加機制

### 設計目標

同一玩家可能透過遊戲進程獲得多個同名技能實例（例：數個 `mashu` 進攻距離 +1），引擎應**按有效實例數疊加**，而非只計一次。

### 實例數計算

```cpp
// Player::getValidSkillInstanceIds(name)
// 回傳該技能在 player 身上所有有效實例的 ID：
//   - innate (instanceId=0)：技能為武將天生自帶
//   - acquired (#N)：透過 Player::acquireSkill 獲得的動態實例
// 每實例逐一經 isSkillInvalid(skill, instanceId) 過濾失效者
```

### 持有者遍歷

引擎對四位修正技能的**非 fixed 分支**，統一遍歷**全場存活角色**（`aliveSiblings` 含目標自身），對每一位有同名技能有效實例的玩家疊加貢獻：

```
total = 0
for each skill in getXxxSkills():
    native = skill->getXxx(...)   // 原生回傳值（取一次）
    for each player in getAliveSiblings(true):
        total += sumSkillContribution(player, skill, native)
```

此設計同時處理三種持有情境：
- **自身持有**（如 YingziMaxCards：周瑜自身手牌上限 +1）
- **第三方持有**（如周處技能令全場手牌上限 -1，實例在周處身上）
- **目標持有**（如防禦型距離技能在防守方身上）

### fixed 分支（不疊加）

`getFixed` 分支維持 `max(getFixed(...))` 取最大值，不疊加、不遍歷多實例。

## NoInstanceMultiply 屬性

對**自管實例邏輯**的技能（其 `getXxx` 內部已自行依 mark 等累加），設定此 property 告知引擎**不進行疊加**：

```cpp
// 在建構子中設定
MyComplexSkill() : DistanceSkill("my_complex") {
    setProperty("NoInstanceMultiply", true);
}
```

`NoInstanceMultiply = true` 時引擎僅**單次套用**原生回傳值，不遍歷持有者實例、不查 modified_amount 覆寫。

預設為 `false`（啟用疊加）。

## modified_amount 後置覆寫

### 用途

遊戲中可透過 `Player` 的 API **後置調整**某一技能每實例的貢獻數值，而不修改技能程式碼。

### API

| 方法 | 說明 |
|------|------|
| `setSkillAmountOverride(name, amount, instanceId=0)` | 設定覆寫值。instanceId=0 為全體覆寫 |
| `hasSkillAmountOverride(name, instanceId=0)` | 查詢是否有覆寫 |
| `getSkillAmountOverride(name, instanceId=0)` | 取得覆寫值 |
| `removeSkillAmountOverride(name, instanceId=0)` | 移除覆寫 |
| `clearSkillAmountOverrides()` | 清除所有覆寫 |

### 覆寫優先序

對每個實例，取值的查詢順序為：

1. **單實例覆寫** `"skillName#N"` — 僅套用至 `#N` 實例
2. **全體覆寫** `"skillName"` — 套用至所有同名實例（含 innate）
3. **技能原生回傳值** — `getCorrect/getExtra/...` 的一次呼叫結果

### 結算公式

```
每實例有效貢獻 = 單實例覆寫 ?? 全體覆寫 ?? 原生回傳值
總貢獻 = Σ 每有效實例的有效貢獻
```

`-1` 統一視為 `1000`（用於 TargetModSkill Residue 的「無限」語意）。

## 使用範例

### 1. Mashu（進攻距離 -1 每實例）

```cpp
// standard-generals.cpp
class Mashu : public DistanceSkill {
    int getCorrect(const Player *from, const Player *) const {
        if (from->hasSkill(objectName()))
            return -1;   // 每實例 -1 距離
        return 0;
    }
};
```

若玩家有 innate mashu + 2 個 acquired (`mashu#1`/`mashu#2`) = 3 有效實例 → 總貢獻 -3。

### 2. 遊戲中後置增強

```lua
-- Lua 側將某玩家的 mashu 全體改為 -2（每實例 -2）
player:setSkillAmountOverride("mashu", -2, 0)
```

3 個有效實例 → 總貢獻 -6。

### 3. 只改一個實例

```lua
-- 只把 mashu#1 改為 -3
player:setSkillAmountOverride("mashu", -3, 1)
```

innate(0)=原生 -1，mashu#1=覆寫 -3，mashu#2=原生 -1 → 總 -5。

## 相關檔案

| 檔案 | 內容 |
|------|------|
| `src/core/skill.h` | DistanceSkill / MaxCardsSkill / TargetModSkill / AttackRangeSkill 基底類別 |
| `src/core/player.h` / `player.cpp` | `getValidSkillInstanceIds` / `m_skillAmountOverride` / modified_amount API |
| `src/core/engine.h` / `engine.cpp` | `sumSkillContribution` / 四個 `correct*` 函式 / `hasResidueUnlimited` |
# 2026-07-16 InstanceStackable 現行規則

本節優先於下方舊版 `NoInstanceMultiply` 說明。

| Skill property | 聚合規則 |
|---|---|
| 未設定或 `InstanceStackable=false` | 相同 base skill 的有效實例只套用一次，維持舊技能行為 |
| `InstanceStackable=true` | 對每個有效 instanceID 分別求值並加總 |

`NoInstanceMultiply` 已廢止。需要多實例疊加的修正技能必須明確設定：

```cpp
setProperty("InstanceStackable", true);
```

封禁、`modified_amount` 與 override 均以精確 instanceID 計算；封禁 `skill#1` 不影響 `skill#2`。未標記的舊修正技能即使玩家持有多份也只貢獻一次。
