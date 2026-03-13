# 動態皮膚功能使用文檔

本系統支援兩種方式播放 Spine 動態皮膚：

| 方式 | 適用場景 |
|------|---------|
| **A. `room:doLightbox("spine=…")`** | 技能特效、全螢幕演出，Lua 腳本主動呼叫 |
| **B. `dynamicSkinConfig.json` 自動觸發** | 角色日常出場、攻擊、技能的座位彈出動畫 |

兩種方式互相獨立，可同時使用。

---

## 部分 A：`room:doLightbox("spine=…")` — Lua 主動呼叫

### 功能說明

在 Lua 技能腳本中透過 `room:doLightbox()` 播放任意 Spine 動畫，動畫播完後自動從場景移除。適合製作技能演出、特殊效果等不屬於特定角色座位的全場景動畫。

### 語法

```lua
room:doLightbox("spine=<路徑>[:<動畫名稱>][@<版本>]")
```

### 三個組成部分

| 部分 | 意義 | 是否必填 |
|------|------|---------|
| `<路徑>` | Spine 骨架檔案路徑（不含副檔名），相對於遊戲執行目錄 | **必填** |
| `:<動畫名稱>` | 指定播放的動畫名稱；省略時自動選用最長動畫 | 選填 |
| `@<版本>` | Spine Runtime 版本提示（`3.5.35` / `3.7` / `3.8` / `4.0` / `4.1`）；省略時自動偵測 | 選填 |

> **路徑規則：** 系統先以路徑原文尋找，找不到時再在遊戲執行目錄下尋找相同路徑。  
> Spine 骨架需要 `.skel`（二進位）或 `.json` + `.atlas` 三個檔案存在於同一目錄。

### 使用範例

```lua
-- 最簡用法：自動選動畫、自動偵測版本
room:doLightbox("spine=assets/dynamic/diaochan/test/skin_diaochan_ZhanChang")

-- 指定動畫名稱
room:doLightbox("spine=assets/dynamic/diaochan/test/skin_diaochan_ZhanChang:GongJi")

-- 指定版本 + 動畫名稱
room:doLightbox("spine=assets/dynamic/diaochan/test/skin_diaochan_ZhanChang:GongJi@3.8")

-- 只指定版本（動畫仍自動選取）
room:doLightbox("spine=assets/dynamic/diaochan/test/skin_diaochan_ZhanChang@4.1")
```

### 播放行為

- 動畫播放一次（非循環）
- 動畫自動佔滿整個場景（以場景中心為錨點）
- 播放完畢後自動移除，不需要手動清理
- 若動畫載入失敗，系統會靜默移除物件並在 debug log 中留下錯誤訊息

---

## 部分 B：`dynamicSkinConfig.json` — 座位自動觸發

### 功能說明

設定好 JSON 後，遊戲開始時系統會自動為每位使用對應武將的玩家載入 Spine 骨架，並在以下時機自動彈出角色動畫：

| 觸發時機 | 動作類型 |
|---------|---------|
| 遊戲開始 / 角色登場 | `entrance`（出場） |
| 玩家使用攻擊（出現指示線） | `attack`（攻擊） |
| 玩家使用非攻擊技能 | `special`（技能） |

彈出動畫從角色頭像位置飛出至場景中央，播完後自動飛回並淡出。

### 檔案位置

```
assets/dynamic/dynamicSkinConfig.json
```

### JSON 結構

```jsonc
{
  // 鍵：武將內部名稱（英文，與 General objectName 一致）
  "diaochan": {
    // 鍵：皮膚識別名（自訂字串，同一武將可設多個皮膚，目前自動取第一個）
    "default": {
      // ── 必填 ──────────────────────────────────────────────────────
      "basePath": "diaochan/default/skin_diaochan",  // Spine 骨架路徑（不含副檔名），相對於 assets/dynamic/

      // ── 選填：全域設定 ───────────────────────────────────────────
      "scale":          0.35,   // 皮膚縮放比例，預設 0.35
      "runtimeVersion": "3.8",  // Spine 版本提示；省略時自動偵測所有版本
      "idleAlpha":      1.0,    // 待機狀態透明度（保留欄位，暫不使用）
      "background":     "",     // 頭像背景圖路徑（選填，設定後觸發 skinBackgroundChanged 訊號）

      // ── 選填：個別動作 ───────────────────────────────────────────
      // 若省略某個動作，系統會對骨架中的動畫名稱進行自動比對（見下方「自動偵測」說明）

      "attack": {
        "animationName": "GongJi",      // 骨架中的動畫名稱（必填）
        "skelBasePath":  "",            // 若攻擊動畫在另一個骨架，填入該骨架路徑；空字串表示與 basePath 相同
        "scale":         0.35,          // 此動作的縮放（覆蓋全域設定）
        "flipX":         false,         // 是否水平翻轉
        "showTime":      1.5,           // 停留在場景中央的時間（秒）；0 表示等動畫播完
        "runtimeVersion": ""            // 此動作的版本提示；空字串繼承全域設定
      },

      "special": {
        "animationName": "TeShu",
        "skelBasePath":  "",
        "scale":         0.35,
        "flipX":         false,
        "showTime":      0,
        "runtimeVersion": ""
      },

      "entrance": {
        "animationName": "ChuChang",
        "skelBasePath":  "",
        "scale":         0.35,
        "flipX":         false,
        "showTime":      0,
        "runtimeVersion": ""
      },

      // ── 選填：攻擊指示線特效 ─────────────────────────────────────
      "indicator": {
        "enabled":        false,        // 是否啟用指示線，預設 false
        "skelName":       "",           // 指示線 Spine 骨架基礎路徑
        "effectName":     "",           // 命中特效 Spine 骨架基礎路徑
        "runtimeVersion": "",           // 指示線骨架版本提示
        "delay":          0.3,          // 延遲多久後生成指示線（攻擊動畫時長的比例），預設 0.3
        "speed":          1.0,          // 指示線移動速度倍率，預設 1.0
        "effectDelay":    0.5           // 命中特效延遲（指示線時長的比例），預設 0.5
      }
    }
  }
}
```

### 自動動畫偵測（不設定個別動作時）

若皮膚物件中沒有 `attack` / `special` / `entrance` 任何一個，系統會掃描骨架中的所有動畫，依下列表格自動比對：

| 動作 | 自動比對的動畫名稱（依優先順序） |
|------|-------------------------------|
| 攻擊 | `GongJi`, `gongji`, `Attack`, `attack` |
| 技能 | `TeShu`, `teshu`, `JiNeng`, `jineng`, `JiNeng01`, `JiNeng02`, `Special`, `special`, `Skill`, `skill` |
| 出場 | `ChuChang`, `chuchang`, `Entrance`, `entrance`, `Appear`, `appear` |

**最終備援：** 若上述名稱都不存在，系統會將骨架中持續時間最長的動畫自動套用至所有動作類型。這表示即使骨架內動畫只叫 `play`、`animation` 等任意名稱，皮膚仍然可以正常運作。

### 五種最小設定範例

#### 1. 最簡設定（完全自動偵測）

```json
{
  "diaochan": {
    "default": {
      "basePath": "diaochan/default/skin_diaochan",
      "scale": 0.35
    }
  }
}
```

#### 2. 指定版本 + 全自動偵測動畫

```json
{
  "diaochan": {
    "default": {
      "basePath": "diaochan/default/skin_diaochan",
      "scale": 0.4,
      "runtimeVersion": "3.8"
    }
  }
}
```

#### 3. 手動指定攻擊動畫，其餘自動偵測

```json
{
  "diaochan": {
    "default": {
      "basePath": "diaochan/default/skin_diaochan",
      "scale": 0.35,
      "attack": {
        "animationName": "GongJi",
        "showTime": 2.0
      }
    }
  }
}
```

#### 4. 攻擊使用不同骨架

```json
{
  "diaochan": {
    "default": {
      "basePath": "diaochan/default/skin_diaochan_idle",
      "scale": 0.35,
      "attack": {
        "animationName": "GongJi",
        "skelBasePath": "diaochan/default/skin_diaochan_attack",
        "showTime": 1.8
      }
    }
  }
}
```

#### 5. 完整設定（含指示線）

```json
{
  "guanyu": {
    "default": {
      "basePath": "guanyu/default/skin_guanyu",
      "scale": 0.4,
      "runtimeVersion": "3.8",
      "attack": {
        "animationName": "GongJi",
        "showTime": 2.0
      },
      "special": {
        "animationName": "TeShu",
        "showTime": 1.5
      },
      "entrance": {
        "animationName": "ChuChang",
        "showTime": 1.2
      },
      "indicator": {
        "enabled": true,
        "skelName": "effects/sword_beam",
        "effectName": "effects/hit_burst",
        "runtimeVersion": "3.8",
        "delay": 0.3,
        "speed": 1.2,
        "effectDelay": 0.5
      }
    }
  }
}
```

### 檔案目錄規範

`basePath` / `skelBasePath` 的路徑以 `assets/dynamic/` 為根目錄，**不含副檔名**。  
系統會自動在同一路徑下尋找以下三個檔案：

```
assets/dynamic/<basePath>.skel   ← 骨架二進位
assets/dynamic/<basePath>.atlas  ← 紋理集描述
assets/dynamic/<basePath>.png    ← 紋理圖片（可能為多個）
```

所有 Spine 相關資源統一放在 `assets/dynamic/` 下，建議依武將分資料夾：

```
assets/dynamic/
  dynamicSkinConfig.json
  diaochan/
    default/
      skin_diaochan.skel
      skin_diaochan.atlas
      skin_diaochan.png
  guanyu/
    default/
      skin_guanyu.skel
      skin_guanyu.atlas
      skin_guanyu.png
```

### JSON 內的 `_` 開頭鍵

任何以 `_` 開頭的頂層鍵（如 `"_comment"`）會被系統忽略，可用於寫備註：

```json
{
  "_comment": "最後更新：2026-03-13",
  "diaochan": { ... }
}
```

---

## 快速對比

| 項目 | `room:doLightbox` | `dynamicSkinConfig.json` |
|------|-------------------|--------------------------|
| 呼叫方式 | Lua 腳本手動呼叫 | JSON 設定後自動觸發 |
| 適用內容 | 技能特效、全場景演出 | 角色座位出場 / 攻擊 / 技能彈出 |
| 動畫位置 | 填滿整個場景 | 從頭像飛出至場景中央，再飛回 |
| 多角色同時 | 每次呼叫獨立播放 | 每個座位各自獨立管理 |
| 版本偵測 | `@版本` 後綴 | `runtimeVersion` 欄位 |
| 動畫指定 | `:動畫名` 後綴 | `animationName` 欄位 |
| 資源路徑 | 相對執行目錄 | 相對 `assets/dynamic/` |
