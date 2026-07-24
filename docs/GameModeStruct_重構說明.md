# 遊戲模式結構化重構說明 (GameModeStruct Refactoring)

## 概述

本次重構將原有的 `QString GameMode`（僅存儲模式 ID 的字串）升級為 `GameModeStruct` 結構體，並新增一系列 API 支援動態模式創建、身份映射與模式分組。此舉旨在對齊「人間版 V3」的架構，提供更豐富的模式資訊，並增強代碼的類型安全與可維護性。

---

## Phase 1：核心結構體

### GameModeStruct (`src/core/structs.h` & `structs.cpp`)

| 成員 | 類型 | 說明 |
|------|------|------|
| `mode_id` | QString | 模式唯一標識符 |
| `display_name` | QString | 模式顯示名稱 |
| `player_count` | int | 人數 |
| `roles` | QString | 身份配置字串 |
| `is_scenario` | bool | 是否劇本模式 |
| `is_mini_scene` | bool | 是否小型場景 |
| `shuffle_seats` | bool | 是否依伺服器的隨機座次設定打亂玩家座次，預設 `true` |
| `lord_welfare` | bool | 是否有主公福利 |

### 全局配置變更

- `Config.GameMode` 類型由 `QString` → `GameModeStruct`
- 存取模式 ID 時使用 `Config.GameMode.mode_id`
- 賦值時使用 `Config.GameMode = Sanguosha->getGameMode("mode_id")`
- 設定檔只持久化 `mode_id`；載入不存在的模式時輸出警告並回退至已註冊的 `02p`
- 劇本與小型場景維持獨立註冊表，`getGameMode()` 查詢時才合成結構；不混入 `getAvailableModes()`

---

## Phase 2：新增 API

### 模式管理

```cpp
// C++ 直接註冊一個 GameModeStruct；重複 ID 會拒絕新模式並輸出警告
bool Engine::addGameMode(const GameModeStruct &mode);

// 設置模式特性
void Engine::setGameModeShuffleSeats(const QString &mode_id, bool shuffle_seats);
void Engine::setGameModeLordWelfare(const QString &mode_id, bool lord_welfare);
```

`shuffle_seats` 只控制 `Room::m_players` 的座次打亂，不控制身份字串順序；需要固定身份配置的模式應由 Lua 自行配置身份。

`getAvailableModes()` 與回傳 `GameModeStruct` 的 API 僅供 C++ 使用，不暴露給 Lua。未知 ID 的 `getGameMode()` 回傳無效空結構。

### 模式分組（合併）

```cpp
// 將多個模式 ID 歸入同一個分組
void Engine::addModeGroup(const QString &groupName, const QStringList &modeIds);

// 查詢某個分組下的所有模式 ID
QStringList Engine::getGroupModes(const QString &groupName) const;

// 根據模式 ID 查詢其所屬分組名稱
QString Engine::getModeGroup(const QString &modeId) const;
```

- 同名分組重複註冊時合併成員。
- 同一模式只能屬於一個分組；重複成員忽略，跨組成員忽略並警告。
- 不存在的模式 ID 忽略並警告。
- 內建身份模式在載入 Lua 前註冊至「身份模式」群組，因此伺服器模式 UI 會直接顯示群組下拉選單。

### 身份映射系統

```cpp
// 註冊新身份（身份名 → 縮寫字母）
bool Engine::addRoleMapping(const QString &roleName, const QString &abbreviation);

// 根據身份名取得縮寫
QString Engine::getRoleAbbreviation(const QString &roleName) const;

// 根據縮寫取得身份名
QString Engine::getRoleByAbbreviation(const QString &targetValue, const QString &defaultKey = "") const;

// 內建身份：lord(Z), loyalist(C), rebel(F), renegade(N)
// 可透過 addRoleMapping 動態附加自訂身份（如 villager → V）
```

縮寫必須是唯一的單一 ASCII 大寫字母。模式的 `roles` 只要包含任何未註冊縮寫，整個模式即拒絕。動態身份的 `getRoleEnum()` 回傳 `UnknownRole`，完整身份名稱仍由 `getRole()` 回傳。

身份映射必須在建立使用該縮寫的模式前註冊。自訂身份缺少 `image/system/roles/small-<role>.png` 時，身份列略過圖示並輸出警告。

### 模式特性管理

```cpp
// 跳過選將
void Engine::addSkipGeneralMode(const QString &mode);
bool Engine::hasSkipGeneralSelection(const QString &mode) const;

// 顯示身份
void Engine::addShowRoleMode(const QString &mode);
bool Engine::hasShowRoleMode(const QString &mode) const;
```

---

## Lua 介面

### createMode 函數 (`extensions/addFunction.lua`)

```lua
-- 單模式
createMode{
    name = "陳塘關模式",
    class = "ctg",
    roles = "ZCCCCC",
    shuffleSeats = false,
    skipChooseGeneral = true,
    showRole = true,
}
-- 生成模式 ID: 6_ctg

-- 模式組
createMode{
    name = "世家模式",
    class = "shijia",
    roles = {"ZNNNNN", "ZNNNNNN", "ZNNNNNNN", "ZNNNNNNNN", "ZNNNNNNNNN"},
    skipChooseGeneral = true,
    showRole = true,
}
-- 生成模式 ID: 06_shijia ~ 10_shijia，分組名稱為「世家模式」

-- 自訂名稱
createMode{
    name = "競技模式",
    class = "competitive",
    roles = {"ZCFF", "ZCCCFF", "ZCCCCFFF"},
    names = {"4人競技賽", "6人競技賽", "8人競技賽"},
    showRole = true,
}
```

`createMode()` 只接受 `shuffleSeats`，預設為 `true`；不再接受 `shuffleRoles` 或 `shuffle_roles`。函數回傳引擎成功註冊的模式 ID 陣列。多模式定義只要至少一個模式註冊成功，就會用成功項目呼叫 `addModeGroup()`。

`skipChooseGeneral = true` 會略過引擎選將、先將玩家設為 `anjiang`，讓 Lua 在 `GameReady` 完成選將。`showRole = true` 公開所有身份；為 `false` 時只公開主公，其餘僅通知本人。自訂模式的 `lordWelfare` 直接決定主公福利，不再依玩家數推導。

### 使用方法

`addFunction.lua` 由擴展載入流程先行載入後，可直接使用全域 `createMode{}`。

### 模式檢測

```lua
-- 單模式
if room:getMode() == "6_ctg" then ... end

-- 模式組
if sgs.Sanguosha:getModeGroup(room:getMode()) == "世家模式" then ... end
```

---

## 注意事項

- 本次重構涉及核心文件修改，**必須全編譯**。
- 若 Lua 無法識別 `GameModeStruct`，請確保已重新執行 SWIG 生成。
- `addRoleMapping` 可在 Lua 中呼叫以註冊自訂身份，如 `sgs.Sanguosha:addRoleMapping("villager", "V")`。
