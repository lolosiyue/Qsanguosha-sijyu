# 動態皮膚功能使用文檔

本系統支援兩種方式播放 Spine 動態皮膚：

| 方式 | 適用場景 |
|------|---------|
| **A. `room:doLightbox("spine=…")`** | 技能特效、全螢幕演出，Lua 腳本主動呼叫 |
| **B. 路徑自動發現** | 角色日常出場、攻擊、技能的座位彈出動畫 |

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
|------|------|----------|
| `<路徑>` | Spine 骨架檔案路徑（不含副檔名），相對於遊戲執行目錄 | **必填** |
| `:<動畫名稱>` | 指定播放的動畫名稱；省略時自動選用最長動畫 | 選填 |
| `@<版本>` | Spine Runtime 版本提示（`3.5.35` / `3.7` / `3.8` / `4.0` / `4.1`）；省略時自動偵測 | 選填 |

> **路徑規則：** 系統先以路徑原文尋找，找不到時再在遊戲執行目錄下尋找相同路徑。
> Spine 骨架需要 `.skel`（二進位）或 `.json` + `.atlas` 三個檔案存在於同一目錄。

### 使用範例

```lua
room:doLightbox("spine=assets/dynamic/diaochan/test/skin_diaochan_ZhanChang")
room:doLightbox("spine=assets/dynamic/diaochan/test/skin_diaochan_ZhanChang:GongJi@3.8")
```

### 播放行為

- 動畫播放一次（非循環）
- 動畫自動佔滿整個場景（以場景中心為錨點）
- 播放完畢後自動移除，不需要手動清理
- 若動畫載入失敗，系統會靜默移除物件並在 debug log 中留下錯誤訊息

---

## 部分 B：路徑自動發現 — 座位自動觸發

### 功能說明

系統根據路徑慣例自動發現並載入 Spine 骨架，在以下時機自動彈出角色動畫：

| 觸發時機 | 動作類型 | 預設動畫名稱 |
|---------|---------|-------------|
| 遊戲開始 / 角色登場 | `entrance`（開場） | `ChuChang` |
| 玩家使用攻擊（出現指示線） | `attack`（攻擊） | `GongJi` |
| 玩家使用非攻擊技能 | `special`（技能） | `TeShu` |

彈出動畫從角色頭像位置飛出至場景中央，播完後自動飛回並淡出。

### 目錄結構

#### Heroskin（皮膚索引 > 0）

```
hero-skin/[generalName]/[skinIndex]/dynamicSkin/
├── config.json          # 動畫配置（選填）
├── skeleton.skel        # Spine 骨架
├── skeleton.atlas       # Spine 圖集
└── skeleton.png         # Spine 圖片
```

**適用條件：**
- `skinIndex > 0`（皮膚索引大於 0）
- 路徑與其他皮膚資源結構一致

#### 原生武將（皮膚索引 == 0）

```
image/fullskin/dynamicSkin/[generalName]/dynamicSkin/
├── config.json          # 動畫配置（選填）
├── skeleton.skel        # Spine 骨架
├── skeleton.atlas       # Spine 圖集
└── skeleton.png         # Spine 圖片
```

**適用條件：**
- `skinIndex == 0`（預設皮膚）
- `generalName` 為武將原名（如 `guanyu`、`zhonghui`）

### 路徑判斷邏輯

```cpp
// src/ui/CharacterSpineActionController.cpp:295-303
static QString buildDynamicSkinRoot(const QString &resolvedGeneral, int skinIndex)
{
    if (skinIndex > 0) {
        return QString("hero-skin/%1/%2/dynamicSkin").arg(resolvedGeneral).arg(skinIndex);
    }
    return QString("fullskin/dynamicSkin/%1/dynamicSkin").arg(resolvedGeneral);
}
```

**判斷依據：**

| skinIndex | 路徑格式 | 範例 |
|-----------|---------|------|
| `> 0` | `hero-skin/[generalName]/[skinIndex]/dynamicSkin/` | `hero-skin/guanyu/1/dynamicSkin/` |
| `== 0` | `image/fullskin/dynamicSkin/[generalName]/dynamicSkin/` | `image/fullskin/dynamicSkin/guanyu/dynamicSkin/` |

### 設定檔 `config.json`

放在與 `skeleton.skel` 同一目錄下，**所有欄位皆為選填**。

```jsonc
{
    // ── 全域設定 ────────────────────────────────────────────
    "scale":          0.35,   // 縮放比例，預設 0.35
    "runtimeVersion": "3.8",  // Spine 版本提示；省略時自動偵測
    "idleAlpha":      1.0,   // 待機透明度（預設 1.0）
    "background":     "",     // 頭像背景圖（相對 assets 前綴）

    // ── 個別動作 ────────────────────────────────────────────
    // 若省略，系統自動從骨架掃描比對（見「自動動畫偵測」章節）
    "attack": {
        "animationName": "GongJi",      // 動畫名稱
        "skelBasePath":  "",            // 若動畫在另一骨架，填路徑；空＝與 basePath 相同
        "scale":         0.35,          // 覆蓋全域 scale
        "flipX":         false,          // 是否水平翻轉
        "showTime":      1.5,            // 停留在場景中央的時間（秒）；0＝等動畫播完
        "runtimeVersion": ""             // 覆蓋全域 runtimeVersion
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

    // ── 指示線特效 ──────────────────────────────────────────
    "indicator": {
        "enabled":        true,           // 是否啟用，預設 false
        "skelName":       "effects/beam", // 指示線骨架路徑（相對於 assets 目錄）
        "effectName":     "effects/burst", // 命中特效骨架路徑
        "runtimeVersion": "",             // 覆蓋全域版本
        "delay":          0.3,           // 延遲多久後生成（相對於動作動畫時長的比例），預設 0.3
        "speed":          1.0,           // 指示線移動速度倍率，預設 1.0
        "effectDelay":    0.5            // 命中特效延遲（相對於指示線時長的比例），預設 0.5
    }
}
```

### 自動動畫偵測

若 `config.json` 中未指定 `attack` / `special` / `entrance`，系統會掃描骨架中的所有動畫，依下列表格自動比對：

| 動作 | 自動比對的動畫名稱（依優先順序） |
|------|-------------------------------|
| 攻擊 | `GongJi`, `gongji`, `Attack`, `attack` |
| 技能 | `TeShu`, `teshu`, `JiNeng`, `jineng`, `JiNeng01`, `JiNeng02`, `Special`, `special`, `Skill`, `skill` |
| 出場 | `ChuChang`, `chuchang`, `Entrance`, `entrance`, `Appear`, `appear` |

**最終備援：** 若上述名稱都不存在，系統會將骨架中持續時間最長的動畫自動套用至所有動作類型。

### 皮膚索引設定

皮膚索引從配置中讀取：

```cpp
// src/ui/roomscene.cpp:6538
int skinIndex = Config.value(QString("HeroSkin/%1").arg(generalName), 0).toInt();
```

在遊戲中選擇皮膚時，配置會自動更新為對應的 `skinIndex` 值。

---

## 快速對比

| 項目 | `room:doLightbox` | 路徑自動發現 |
|------|-------------------|--------------|
| 呼叫方式 | Lua 腳本手動呼叫 | 路徑存在即自動 |
| 適用內容 | 技能特效、全場景演出 | 角色座位出場 / 攻擊 / 技能彈出 |
| 動畫位置 | 填滿整個場景 | 從頭像飛出至場景中央，再飛回 |
| 多角色同時 | 每次呼叫獨立播放 | 每個座位各自獨立管理 |
| 版本偵測 | `@版本` 後綴 | 自動偵測 |
| 動畫指定 | `:動畫名` 後綴 | 自動偵測或 `config.json` |
| 資源路徑 | 相對執行目錄 | `hero-skin/[general]/[skinIndex]/dynamicSkin/` 或 `image/fullskin/dynamicSkin/` |

---

## 相關文檔

- **完整皮膚系統說明**：請參考 `hero-skin-guide.md`
- **程式碼參考**：`src/ui/CharacterSpineActionController.cpp:295-303`

---

## 更新日誌

- 2026-06-05：修改程式碼，讓 dynamicSkin 使用與皮膚資源一致的路徑結構
- 2026-06-05：增加詳細的路徑判斷邏輯說明，與 hero-skin-guide.md 保持一致
- 2026-06-05：初版建立
