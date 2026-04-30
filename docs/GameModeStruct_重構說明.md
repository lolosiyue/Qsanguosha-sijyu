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
| `rule_mode` | QString | 基礎規則模式 |
| `is_scenario` | bool | 是否劇本模式 |
| `is_mini_scene` | bool | 是否小型場景 |
| `shuffle_roles` | bool | 是否洗身份 |
| `lord_welfare` | bool | 是否有主公福利 |

### 全局配置變更

- `Config.GameMode` 類型由 `QString` → `GameModeStruct`
- 存取模式 ID 時使用 `Config.GameMode.mode_id`
- 賦值時使用 `Config.GameMode = Sanguosha->getGameMode("mode_id")`

---

## Phase 2：新增 API

### 模式管理

```cpp
// 直接註冊一個 GameModeStruct
void Engine::addGameMode(const GameModeStruct &mode);

// 設置模式特性
void Engine::setGameModeShuffleRoles(const QString &mode_id, bool shuffle_roles);
void Engine::setGameModeLordWelfare(const QString &mode_id, bool lord_welfare);
```

### 模式分組（合併）

```cpp
// 將多個模式 ID 歸入同一個分組
void Engine::addModeGroup(const QString &groupName, const QStringList &modeIds);

// 查詢某個分組下的所有模式 ID
QStringList Engine::getGroupModes(const QString &groupName) const;

// 根據模式 ID 查詢其所屬分組名稱
QString Engine::getModeGroup(const QString &modeId) const;
```

### 身份映射系統

```cpp
// 註冊新身份（身份名 → 縮寫字母）
void Engine::addRoleMapping(const QString &roleName, const QString &abbreviation);

// 根據身份名取得縮寫
QString Engine::getRoleAbbreviation(const QString &roleName) const;

// 根據縮寫取得身份名
QString Engine::getRoleByAbbreviation(const QString &targetValue, const QString &defaultKey = "") const;

// 內建身份：lord(Z), loyalist(C), rebel(F), renegade(N)
// 可透過 addRoleMapping 動態附加自訂身份（如 villager → V）
```

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

### createMode 函數 (`lua/lib/createMode.lua`)

```lua
-- 單模式
createMode{
    name = "陳塘關模式",
    class = "ctg",
    roles = "ZCCCCC",
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

### 使用方法

在 Lua 擴展中 `require "lib/createMode"` 後即可使用 `createMode{}` 函數。

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
