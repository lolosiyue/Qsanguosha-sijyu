# Aura 光環系統說明

## 概述

Aura 光環系統允許在遊戲中設定特殊狀態，改變BGM、背景，並觸發光環動畫效果。此系統移植自 AnimeMod-master 分支。

## 系統架構

```
[Lua Skill]
    → room:doAura(player, aura_name)
    → setAura(player, aura_name)
       → setTag("aura", aura_name)
       → setTag("aura_player", player)
       → changeBGM(aura_name, false)
       → changeBackground(aura_name)
       → doAnimate(S_ANIMATE_LIGHTBOX, "lani=aura", "3000:0")
```

## 函數列表

### 伺服器端 (room.h / room.cpp)

| 函數 | 用途 |
|------|------|
| `setAura(ServerPlayer*, QString)` | 設定光環狀態 |
| `hasAura()` | 檢查是否存在任何光環 |
| `hasAura(QString)` | 檢查是否為特定光環 |
| `getAura()` | 獲取當前光環名稱 |
| `getAuraPlayer()` | 獲取光環持有者 |
| `clearAura()` | 清除當前光環 |
| `doAura(ServerPlayer*, QString)` | 嘗試設定光環（已存在則失敗） |

### Lua 端 (SWIG Binding)

| 函數 | 用途 |
|------|------|
| `room:hasAura()` | 檢查是否存在光環 |
| `room:hasAura(aura)` | 檢查是否為特定光環 |
| `room:getAura()` | 獲取當前光環名稱 |
| `room:clearAura()` | 清除光環 |
| `room:doAura(player, aura)` | 設定光環 |
| `room:getAuraPlayer()` | 獲取光環持有者 |

## 實作差異

與原始 AnimeMod 版本的差異：

| 項目 | 原始版本 | 當前版本 |
|------|----------|----------|
| `changeBGM` | `void changeBGM(QString)` | `bool changeBGM(QString, bool, QList<ServerPlayer*>)` |
| `changeBG` | `void changeBG(QString)` | 使用 `changeBackground(QString)` |
| `redoEmotion` | 每秒刷新表情 | 未移植 |

### changeBGM 呼叫方式

原始版本：
```cpp
changeBGM(aura);  // 直接傳遞 aura 名稱
```

當前版本（需要 3 參數）：
```cpp
changeBGM(aura, false);  // 第二、三參數使用預設值
```

### changeBackground 資源路徑

| 版本 | 資源前綴 |
|------|----------|
| 原始 `changeBG` | `tableBg` |
| 當前 `changeBackground` | `image/system/backdrop/` |

光環資源需放置於 `image/system/backdrop/` 目錄。

## 使用範例

### Lua 技能中設定光環

```lua
-- 設定光環
room:doAura(player, "MacrossF")

-- 檢查光環
if room:hasAura() then
    local aura_name = room:getAura()
    local aura_player = room:getAuraPlayer()
end

-- 清除光環
room:clearAura()
```

### 特定光環判斷

```lua
if room:hasAura("MacrossF") then
    -- MacrossF 光環邏輯
end
```

## 客戶端處理

### Lightbox 動畫

`setAura` 會發送 `S_ANIMATE_LIGHTBOX` 指令，攜帶 `lani=aura` 參數。客戶端在 `doLightboxAnimation` 中處理：

- `lani=` 前綴被排除不套用深色遮罩（使用 `QColor(32,32,32,100)` 淺色遮罩）
- 動畫使用 `PixmapAnimation::GetPixmapAnimation(lightbox, "aura")` 播放光環特效
- 需在 `image/...` 目錄下有對應的光環動畫資源

### 背景變更

`changeBackground(aura)` 會發送 `background=<aura_name>` 參數。客戶端會在 `image/system/backdrop/` 目錄下查找對應資源。

## 待辦事項

- [ ] `redoEmotion` 未移植：每秒刷新表情機制
- [ ] `setLoopEmotion` 尚未正確實現（與 `redoEmotion` 功能重疊）
- [ ] 確認光環資源路徑是否正確配置

## 歷史

| 日期 | 異動 |
|------|------|
| 2026-05-08 | 初始移植 from AnimeMod-master (lines 5958-6019) |
| 2026-05-08 | 客戶端 `lani=` 動畫處理實作 (roomscene.cpp) |
