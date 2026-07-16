# 多技能實例系統設計文檔

## 目標

1. **技能疊加**：主將馬超 + 副將龐德 → 距離 -2
2. **分來源失效**：可精確失效特定來源的技能
3. **引擎自動處理**：instanceId 由引擎自動分配，技能開發者無需關心
4. **現有技能代碼不需修改**：`getCorrect()` 等方法保持原樣

---

## 現有問題分析

### 技能儲存結構

```cpp
// player.h
QStringList skills;                          // 技能名列表（去重）
QStringList acquired_skills;                 // 動態獲得技能
QMap<QString, bool> head_skills;             // 主將技能
QMap<QString, bool> deputy_skills;           // 副將技能
QSet<QString> head_acquired_skills;          // 主將獲得技能
QSet<QString> deputy_acquired_skills;        // 副將獲得技能
```

### 現有問題

| 問題 | 說明 |
|------|------|
| `skills` 去重 | `addSkill()` 會檢查 `contains()`，同一技能只存一次 |
| 無法區分來源 | 主將和副將都有 `mashu` 時，`skills` 只有一個 `mashu` |
| 無法分來源失效 | `SkillInvalidityRecords` 格式支援 `skillName#instanceId`，但武將技能無 instanceId |

### 距離計算流程

```
Engine::correctDistance()
    ↓
遍歷 getDistanceSkills()（全局 DistanceSkill 列表）
    ↓
對每個 DistanceSkill 呼叫 getCorrect()
    ↓
getCorrect() 內呼叫 from->hasSkill(objectName())
    ↓
hasSkill() 檢查 skills 容器 + 有效性
```

---

## 核心概念區分

### Skill::m_instanceId vs 玩家技能 instanceId

| 概念 | 說明 | 範例 |
|------|------|------|
| `Skill::m_instanceId` | Skill **物件**的 ID（全局唯一，創建時分配） | `Mashu` 物件的 ID = 42 |
| 玩家技能 instanceId | 玩家**擁有**該技能的來源 ID（用於區分不同來源） | 主將 mashu = 1，副將 mashu = 1001 |

**關鍵**：`Mashu` Skill 物件是全局單例，所有玩家共用同一個物件。`distSkill->getInstanceId()` 返回的是物件 ID，不是玩家技能來源 ID。

### 參考：Hegemony 版本的做法

| 專案 | 做法 | 能否疊加 | 分來源失效 |
|------|------|---------|-----------|
| QSanguosha-For-Hegemony | 每武將獨立實例（`mashu_machao`） | 能 | 不能 |
| Qsanguosha-sijyu（本方案） | instanceId 區分（`mashu#1`、`mashu#1001`） | 能 | 能 |

---

## 設計方案

### 方案選擇：QList\<SkillSource\> + instanceId

#### 為什麼選擇 QList 而非 QMap

| 維度 | QMap + 完整名 | QList\<SkillSource\> + instanceId |
|------|--------------|----------------------------------|
| 數據結構 | 複用現有 QMap，字符串拼接 | 新增結構體，語義明確 |
| 重複技能 | 通過完整名區分 | 通過 instanceId 區分 |
| 序列化 | 簡單（QStringList） | 需序列化結構體 |
| SWIG 綁定 | 簡單 | 需綁定結構體 |
| 語義清晰度 | 一般（字符串拼接） | 好（結構體明確） |

**選擇 QList\<SkillSource\> 的理由**：
1. 語義更清晰：技能名和 instanceId 分離，不依賴字符串拼接
2. 可重複：同一技能名可出現多次
3. 擴展性好：未來可在 SkillSource 中加入更多欄位
4. 避免 `#` 字符串解析錯誤

---

### SkillSource 結構體定義

```cpp
// player.h
struct SkillSource {
    QString name;       // 技能名（不含 instanceId）
    int instanceId;     // 來源 ID
    bool preshowed;     // 是否預亮

    QString fullName() const {
        if (instanceId > 0)
            return QString("%1#%2").arg(name).arg(instanceId);
        return name;
    }
};
```

### instanceId 分配規則

| 來源 | instanceId 範圍 | 說明 |
|------|----------------|------|
| 主將技能 | 1 - 1000 | 每個技能遞增 |
| 副將技能 | 1001 - 2000 | 每個技能遞增 |
| 動態獲得 | 2001+ | 每個技能遞增 |

---

### 數據結構變更

```cpp
// player.h - 變更前
class Player {
protected:
    QStringList skills;
    QStringList acquired_skills;
    QMap<QString, bool> head_skills;
    QMap<QString, bool> deputy_skills;
    QSet<QString> head_acquired_skills;
    QSet<QString> deputy_acquired_skills;
};

// player.h - 變更後
class Player {
protected:
    QList<SkillSource> head_skills;      // 主將技能（可重複）
    QList<SkillSource> deputy_skills;    // 副將技能（可重複）
    QList<SkillSource> acquired_skills;  // 動態獲得技能（可重複）
};
```

### `skills` 容器的處理

**問題**：現有 `skills`（QStringList）用於快速檢查 `hasSkill()`，改為 QList\<SkillSource\> 後是否保留？

**方案**：移除 `skills`，統一使用 `head_skills` + `deputy_skills` + `acquired_skills`

**理由**：
- `skills` 的存在是為了快速查找，但改為 QList 後查找效率相當
- 維護兩份數據容易不一致
- `hasSkill()` 改為遍歷 `head_skills` + `deputy_skills` + `acquired_skills`

### `head_acquired_skills` / `deputy_acquired_skills` 的處理

**方案**：合併到 `acquired_skills`，通過 `SkillSource` 中的欄位區分來源

```cpp
struct SkillSource {
    QString name;
    int instanceId;
    bool preshowed;
    enum Source { Head, Deputy, Acquired } source;  // 來源標記
};
```

**或者**：保留 `head_acquired_skills` / `deputy_acquired_skills` 作為獨立容器

---

### 序列化/反序列化

#### 網絡傳輸

現有協議使用 `QStringList` 傳輸技能列表。需要擴展：

```cpp
// 序列化
QStringList Player::getSkillNames() const {
    QStringList names;
    foreach (const SkillSource &s, head_skills + deputy_skills + acquired_skills) {
        names << s.fullName();  // "mashu#1"
    }
    return names;
}

// 反序列化
void Player::setSkillsFromNames(const QStringList &names) {
    foreach (const QString &name, names) {
        SkillSource source;
        int split = name.indexOf('#');
        if (split != -1) {
            source.name = name.left(split);
            source.instanceId = name.mid(split + 1).toInt();
        } else {
            source.name = name;
            source.instanceId = 0;
        }
        // 根據 instanceId 範圍判斷來源
        if (source.instanceId >= 1 && source.instanceId <= 1000)
            head_skills.append(source);
        else if (source.instanceId >= 1001 && source.instanceId <= 2000)
            deputy_skills.append(source);
        else if (source.instanceId >= 2001)
            acquired_skills.append(source);
    }
}
```

#### 存檔

現有存檔機制使用 JSON/INI，技能列表為字符串數組。擴展方式同上。

#### 客戶端同步

現有客戶端通過 `S_COMMAND_ADD_SKILL` / `S_COMMAND_LOSE_SKILL` 同步技能。需要擴展：

```cpp
// 服務器發送
room->notify(player, S_COMMAND_ADD_SKILL, QStringList() << skillName << QString::number(instanceId));

// 客戶端接收
void Client::onAddSkill(const QStringList &args) {
    QString skillName = args[0];
    int instanceId = args[1].toInt();
    player->addSkill(skillName, headSkill, instanceId);
}
```

---

### SWIG 綁定

```swig
// sanguosha.i
struct SkillSource {
    QString name;
    int instanceId;
    bool preshowed;
    QString fullName();
};

%template(SkillSourceList) QList<SkillSource>;
%template(SkillIntPairList) QList<QPair<const Skill*, int>>;
```

---

## 修改清單

### 1. `src/core/player.h`

- 新增 `SkillSource` 結構體
- `head_skills` / `deputy_skills` / `acquired_skills` 改為 `QList<SkillSource>`
- 移除 `skills`（QStringList）
- `addSkill()` 增加 instanceId 參數
- `hasSkill()` 增加 instanceId 參數
- 新增 `getSkillListWithSources()`

### 2. `src/core/player.cpp`

- 重寫 `addSkill()`、`hasSkill()`、`getSkillList()`、`getSkillListWithSources()`
- 修改所有使用 `skills`、`head_skills`、`deputy_skills` 的方法

### 3. `src/server/room.cpp`

- `preparePlayers()` 自動分配 instanceId
- `acquireSkill()` 自動分配 instanceId
- `detachSkillFromPlayer()` 支援 instanceId

### 4. `src/core/engine.cpp`

- `correctDistance()` 遍歷玩家技能列表（`getSkillListWithSources()`）
- `correctMaxCards()` 同上
- `correctCardTarget()` 同上
- 移除全局 `getDistanceSkills()` / `getMaxCardsSkills()` / `getTargetModSkills()` 的依賴

### 5. `src/client/client.cpp`

- 客戶端同步支援 instanceId

### 6. `swig/sanguosha.i`

- 綁定 `SkillSource` 和 `getSkillListWithSources()`

---

## 核心方法實作

### addSkill()

```cpp
void Player::addSkill(const QString &skill_name, bool head_skill, int instanceId)
{
    const Skill *skill = Sanguosha->getSkill(skill_name);
    if (!skill) return;

    SkillSource source;
    source.name = skill_name;
    source.instanceId = instanceId;
    source.preshowed = !skill->canPreshow() || (head_skill ? general_showed : general2_showed);

    if (head_skill) {
        head_skills.append(source);
    } else {
        deputy_skills.append(source);
    }
}
```

### hasSkill()

```cpp
bool Player::hasSkill(const QString &skill_name, bool include_lose, int instanceId) const
{
    if (skill_name.isEmpty()) return false;

    // 檢查主將技能
    foreach (const SkillSource &s, head_skills) {
        if (s.name == skill_name && (instanceId == 0 || s.instanceId == instanceId)) {
            if (include_lose) return true;
            return checkSkillValidity(s.name, s.instanceId);
        }
    }

    // 檢查副將技能
    foreach (const SkillSource &s, deputy_skills) {
        if (s.name == skill_name && (instanceId == 0 || s.instanceId == instanceId)) {
            if (include_lose) return true;
            return checkSkillValidity(s.name, s.instanceId);
        }
    }

    // 檢查動態獲得技能
    foreach (const SkillSource &s, acquired_skills) {
        if (s.name == skill_name && (instanceId == 0 || s.instanceId == instanceId)) {
            if (include_lose) return true;
            return checkSkillValidity(s.name, s.instanceId);
        }
    }

    return false;
}
```

### hasSkill(const Skill *skill) 多載

```cpp
bool Player::hasSkill(const Skill *skill, bool include_lose, int instanceId) const
{
    if (!skill) return false;
    return hasSkill(skill->objectName(), include_lose, instanceId);
}
```

### getSkillListWithSources()

```cpp
QList<QPair<const Skill*, int>> Player::getSkillListWithSources(bool include_equip, bool visible_only) const
{
    QList<QPair<const Skill*, int>> result;

    // 1. 主將技能
    foreach (const SkillSource &s, head_skills) {
        if (!include_equip && hasEquipSkill(s.name)) continue;
        const Skill *skill = Sanguosha->getSkill(s.name);
        if (skill && (!visible_only || skill->isVisible()))
            result << qMakePair(skill, s.instanceId);
    }

    // 2. 副將技能
    foreach (const SkillSource &s, deputy_skills) {
        if (!include_equip && hasEquipSkill(s.name)) continue;
        const Skill *skill = Sanguosha->getSkill(s.name);
        if (skill && (!visible_only || skill->isVisible()))
            result << qMakePair(skill, s.instanceId);
    }

    // 3. 動態獲得技能
    foreach (const SkillSource &s, acquired_skills) {
        if (!include_equip && hasEquipSkill(s.name)) continue;
        const Skill *skill = Sanguosha->getSkill(s.name);
        if (skill && (!visible_only || skill->isVisible()))
            result << qMakePair(skill, s.instanceId);
    }

    return result;
}
```

---

## Engine correct 方法修改

### correctDistance()

```cpp
int Engine::correctDistance(const Player *from, const Player *to, bool fixed) const
{
    bool locked = lua_mutex.tryLock();
    if (!locked) {
        if (from && from->inherits("ClientPlayer")) return 0;
        lua_mutex.lock();
        locked = true;
    }

    int correct = 0;

    foreach (auto &pair, from->getSkillListWithSources(true, false)) {
        const Skill *skill = pair.first;
        int instanceId = pair.second;

        if (skill->inherits("DistanceSkill")) {
            const DistanceSkill *distSkill = qobject_cast<const DistanceSkill*>(skill);

            if (from->hasSkill(skill->objectName(), false, instanceId)) {
                if (fixed) {
                    int f = distSkill->getFixed(from, to);
                    if (f > correct) correct = f;
                } else {
                    correct += distSkill->getCorrect(from, to);
                }
            }
        }
    }

    // 馬匹距離（遍歷所有馬）
    foreach (const EquipCard *e, from->getOffensiveHorses()) {
        const Horse *oh = qobject_cast<const Horse*>(e);
        if (oh) correct += oh->getCorrect(from);
    }
    foreach (const EquipCard *e, to->getDefensiveHorses()) {
        const Horse *dh = qobject_cast<const Horse*>(e);
        if (dh) correct += dh->getCorrect(to);
    }

    if (locked) lua_mutex.unlock();
    return correct;
}
```

### correctMaxCards()

```cpp
int Engine::correctMaxCards(const Player *target, bool fixed) const
{
    bool locked = lua_mutex.tryLock();
    if (!locked) {
        if (target && target->inherits("ClientPlayer")) return 0;
        lua_mutex.lock();
        locked = true;
    }

    int ex = -1;

    foreach (auto &pair, target->getSkillListWithSources(true, false)) {
        const Skill *skill = pair.first;
        int instanceId = pair.second;

        if (skill->inherits("MaxCardsSkill")) {
            const MaxCardsSkill *maxSkill = qobject_cast<const MaxCardsSkill*>(skill);

            if (target->hasSkill(skill->objectName(), false, instanceId)) {
                if (fixed) {
                    int f = maxSkill->getFixed(target);
                    if (f > ex) ex = f;
                } else {
                    if (ex == -1) ex = 0;
                    ex += maxSkill->getExtra(target);
                }
            }
        }
    }

    if (locked) lua_mutex.unlock();
    return ex;
}
```

### correctCardTarget()

```cpp
int Engine::correctCardTarget(const TargetModSkill::ModType type, const Player *from, const Card *card, const Player *to) const
{
    if (!from || !card) return 0;

    bool locked = lua_mutex.tryLock();
    if (!locked) {
        if (from && from->inherits("ClientPlayer")) return 0;
        lua_mutex.lock();
        locked = true;
    }

    int x = 0;
    QStringList subcardNames;
    if (card->isVirtualCard()) {
        foreach (const Card *e, from->getEquips()) {
            if (card->getSubcards().contains(e->getId()))
                subcardNames << e->objectName();
        }
    }

    foreach (auto &pair, from->getSkillListWithSources(true, false)) {
        const Skill *skill = pair.first;
        int instanceId = pair.second;

        if (skill->inherits("TargetModSkill")) {
            const TargetModSkill *modSkill = qobject_cast<const TargetModSkill*>(skill);

            if (subcardNames.contains(skill->objectName())) continue;
            if (!matchExpPattern(modSkill->getPattern(), from, card)) continue;

            if (from->hasSkill(skill->objectName(), false, instanceId)) {
                if (type == TargetModSkill::Residue) {
                    int n = modSkill->getResidueNum(from, card, to);
                    if (n == -1) n = 1000;
                    x += n;
                } else if (type == TargetModSkill::DistanceLimit) {
                    x += modSkill->getDistanceLimit(from, card, to);
                } else if (type == TargetModSkill::ExtraTarget) {
                    x += modSkill->getExtraTargetNum(from, card);
                }
            }
        }
    }

    if (locked) lua_mutex.unlock();
    return x;
}
```

---

## 行為對照

| 情境 | 現有行為 | 新行為 |
|------|---------|--------|
| 主將 mashu + 副將 mashu | 距離 -1 | 距離 -2 |
| 失效 `mashu#1` | 無法精確失效 | 只有主將的 mashu 失效，距離變 -1 |
| 失效 `mashu`（無 instanceId） | 失效所有 mashu | 失效所有 mashu |
| `hasSkill("mashu")` | 返回 true/false | 模糊匹配（任一來源有效即 true） |
| `hasSkill("mashu", false, 1)` | 不支援 | 精確匹配 instanceId=1 |
| 兩匹 -1 馬 | 只計一匹 | 距離 -2 |
| 隱藏技疊加 | 不疊加 | 疊加 |
| 主公技疊加 | 不疊加 | 疊加 |

---

## 現有技能代碼是否需要修改？

**不需要！**

- `getCorrect()` 內的 `hasSkill(objectName())` 繼續運作（模糊匹配）
- `correctDistance()` 層面已做精確有效性檢查
- `getCorrect()` 內的 `hasSkill()` 檢查輕微冗餘但安全

---

## 馬匹距離處理

### 現有問題

- `Player::distanceTo()` 直接計算馬匹距離（不走 `correctDistance()`）
- `getOffensiveHorse()` 只返回一匹馬
- 需要統一處理

### 方案

1. `Player::distanceTo()` 移除馬匹直接計算，改為走 `correctDistance()`
2. `correctDistance()` 遍歷所有馬匹
3. 需要確認多馬匹區是否已支援（`getOffensiveHorses()` / `getDefensiveHorses()`）

---

## 待確認問題

1. **`head_acquired_skills` / `deputy_acquired_skills` 是否合併到 `acquired_skills`？**
   - 合併：數據結構更簡潔，需在 SkillSource 中加 source 欄位
   - 保留：改動較小，但多兩個容器

2. **`hasSkill("mashu")` 模糊匹配行為**：
   - 任一來源有效即返回 true（建議，相容現有行為）

3. **`skills`（QStringList）移除後的影響**：
   - 所有使用 `skills` 的地方需改為遍歷 `head_skills` + `deputy_skills` + `acquired_skills`
   - 需要完整盤點所有引用點

---

## 參考

- `SkillInvalidityRecords` 格式：`skillName#instanceId|sourceName|reason`
- `isSkillInvalid()` 已支援 instanceId 參數
- `acquireSkill()` 已支援 instanceId 參數（用於動態技能）
