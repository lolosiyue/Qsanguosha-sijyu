# Hero-Skin 皮膚系統完整文檔

本文檔詳細說明 QSanguosha-v2 的武將皮膚系統結構、檔案格式與使用方式。

---

## 目錄結構總覽

```
hero-skin/
└── [generalName]/                 # 武將名稱（如 guanyu, zhonghui）
    └── [skinIndex]/               # 皮膚編號（1, 2, 3...）
        ├── full.png               # 靜態大頭像（必要）
        ├── full.gif               # 動態大頭像（選填）
        ├── card.jpg               # 卡片圖（必要）
        ├── fulldual.png           # 雙將頭像（選填）
        ├── death.ogg              # 死亡語音（選填）
        ├── [skillName].ogg        # 技能語音（選填）
        ├── [skillName]1.ogg       # 多句技能語音（選填）
        ├── [skillName]2.ogg
        ├── card/                  # 卡牌語音資料夾（選填）
        │   ├── slash.ogg
        │   └── peacock.ogg
        ├── [lang].lua             # 皮膚翻譯（選填，如 zh_CN.lua）
        └── dynamicSkin/           # Spine 動態皮膚（選填）
            ├── skeleton.skel      # Spine 骨架
            ├── skeleton.atlas     # Spine 圖集
            ├── skeleton.png       # Spine 圖片
            └── config.json        # 動畫配置
```

---

## 皮膚編號規則

| 編號 | 意義 | 位置 |
|------|------|------|
| `0` | 預設皮膚 | 不在 `hero-skin` 目錄，使用 `image/fullskin/` 預設圖片 |
| `1` | 第一款皮膚 | `hero-skin/[generalName]/1/` |
| `2` | 第二款皮膚 | `hero-skin/[generalName]/2/` |
| `n` | 第 n 款皮膚 | `hero-skin/[generalName]/n/` |

---

## 完整皮膚元素清單

### 必要元素

| 元素 | 檔案名 | 格式 | 說明 |
|------|--------|------|------|
| **靜態頭像** | `full.png` | PNG | 遊戲中角色顯示用，建議尺寸 250x292 |
| **卡片圖** | `card.jpg` | JPG | 選將介面卡片圖，建議尺寸 200x290 |

### 選填元素

| 元素 | 檔案名 | 格式 | 說明 |
|------|--------|------|------|
| **動態頭像** | `full.gif` | GIF | 長期循環播放的動畫頭像 |
| **雙將頭像** | `fulldual.png` | PNG | 雙將模式時的頭像，建議尺寸 124x292 |
| **死亡語音** | `death.ogg` | OGG | 角色死亡時播放 |
| **技能語音** | `[skillName].ogg` | OGG | 發動技能時的台詞 |
| **卡牌語音** | `card/[cardName].ogg` | OGG | 使用特定卡牌時的台詞 |
| **皮膚翻譯** | `[lang].lua` | Lua | 皮膚專屬翻譯檔 |

---

## 圖片檔案詳細說明

### full.png（靜態頭像）

- **用途**：遊戲中角色頭像顯示
- **建議尺寸**：250x292 像素
- **格式**：PNG（支援透明背景）
- **必要**：是

### full.gif（動態頭像）

- **用途**：長期循環播放的動畫頭像
- **播放行為**：
  - 載入後立即播放並持續循環
  - 若 GIF 不存在或配置關閉，顯示 `full.png` 靜態圖
- **啟用條件**：
  1. 檔案存在於皮膚目錄
  2. 配置 `EnableAnimatedGenerals = true`（預設啟用）
- **搜尋邏輯**：
  ```cpp
  // 自動將 full.png → full.gif
  gifPath.replace(QRegExp("\\.(jpg|png)$", Qt::CaseInsensitive), ".gif");
  ```
- **必要**：否

### card.jpg（卡片圖）

- **用途**：選將介面的卡片圖片
- **建議尺寸**：200x290 像素
- **格式**：JPG
- **必要**：是

### fulldual.png（雙將頭像）

- **用途**：雙將模式（國戰等）時的合體頭像
- **建議尺寸**：124x292 像素
- **必要**：否

---

## 語音檔案詳細說明

### 死亡語音（death.ogg）

- **觸發時機**：角色死亡時
- **搜尋路徑**：
  1. `hero-skin/[generalName]/[skinIndex]/death.ogg`
  2. `audio/death/[generalName].ogg`（預設）
- **相關程式碼**：`src/core/general.cpp:307-353`

### 技能語音（[skillName].ogg）

- **觸發時機**：發動技能時
- **命名規則**：
  - 單句：`[skillName].ogg`（如 `wusheng.ogg`）
  - 多句：`[skillName]1.ogg`, `[skillName]2.ogg`...
- **搜尋邏輯**：
  ```cpp
  // 先找編號語音（wusheng1.ogg, wusheng2.ogg...）
  for (int i = 1;; ++i) {
      QString effectFile = QString("hero-skin/%1/%2/%3%4.ogg")
          .arg(general).arg(skinId)
          .arg(skillName).arg(i);
      if (QFile::exists(effectFile)) ...
  }
  // 再找無編號語音
  QString effectFile = QString("hero-skin/%1/%2/%3.ogg")
      .arg(general).arg(skinId).arg(skillName);
  ```
- **相關程式碼**：`src/core/skill.cpp:322-336`

### 卡牌語音（card/[cardName].ogg）

- **觸發時機**：使用特定卡牌時
- **目錄結構**：
  ```
  hero-skin/[generalName]/[skinIndex]/card/
  ├── slash.ogg      # 使用【殺】時
  ├── peacock.ogg    # 使用【桃】時
  └── ...
  ```
- **搜尋優先級**：
  1. `hero-skin/[generalName]/[skinIndex]/card/[cardName].ogg`
  2. `hero-skin/[generalName]/[skinIndex]/[cardName].ogg`
  3. `audio/card/[generalName]/[cardName].ogg`（預設）
- **相關程式碼**：`src/ui/skin-bank.cpp:496-562`

---

## 皮膚翻譯檔（[lang].lua）

### 檔案位置

```
hero-skin/[generalName]/[skinIndex]/[lang].lua
```

例如：
```
hero-skin/guanyu/1/zh_CN.lua    # 簡體中文翻譯
hero-skin/guanyu/1/en_US.lua   # 英文翻譯
```

### 檔案格式

```lua
-- hero-skin/guanyu/1/zh_CN.lua

local t = {
    -- 技能名稱（皮膚專屬）
    ["wusheng"] = "青龙之魂",
    
    -- 技能描述
    [":wusheng"] = "你可以将一张红色牌当【杀】使用或打出。",
    
    -- 技能台詞
    ["$wusheng1"] = "看尔乃插标卖首！",
    ["$wusheng2"] = "汝比颜良、文丑如何？",
    
    -- 陣亡台詞
    ["~guanyu"] = "桃源之誓，竟成泡影...",
    
    -- 勝利台詞
    ["^guanyu"] = "义贯古今，神威浩荡！",
}

-- 使用皮膚專屬翻譯函數
sgs.LoadSkinTransltionTable(t)
```

### 翻譯函數說明

#### `sgs.LoadTranslationTable(t)` — 普通翻譯

用於全局翻譯，在 `lang/[lang]/Package/` 目錄下使用。

```lua
-- lang/zh_CN/Package/StandardPackage.lua
local t = {
    ["guanyu"] = "关羽",
    ["wusheng"] = "武圣",
}
sgs.LoadTranslationTable(t)
```

#### `sgs.LoadSkinTransltionTable(t)` — 皮膚專屬翻皮膚專屬翻譯

僅當該皮膚被選用時生效。

**實現機制**（`lua/sgs_ex.lua:924-932`）：

```lua
function sgs.LoadSkinTransltionTable(t)
    local generalName = sgs.Sanguosha:property("CurrentSkinGeneral"):toString()
    local skinId = sgs.Sanguosha:property("CurrentSkinId"):toInt()
    local prefix = "-" .. generalName .. "_" .. skinId
    -- 自動加上後綴如 "-guanyu_1"
    for k, v in pairs(t) do
        sgs.AddTranslationEntry(k .. prefix, v)
    end
end
```

---

## Spine 動態皮膚（dynamicSkin/）

### 目錄結構

動態皮膚直接放在皮膚目錄下，與其他皮膚資源結構一致：

```
hero-skin/[generalName]/[skinIndex]/dynamicSkin/
├── skeleton.skel      # Spine 骨架（二進位格式）
├── skeleton.atlas     # Spine 圖集定義
├── skeleton.png       # Spine 圖集圖片
└── config.json        # 動畫配置（選填）
```

**適用條件：**
- `skinIndex > 0`（皮膚索引大於 0）
- 路徑格式與其他皮膚資源一致

#### 原生武將（skinIndex == 0）

```
image/fullskin/dynamicSkin/[generalName]/dynamicSkin/
├── skeleton.skel
├── skeleton.atlas
├── skeleton.png
└── config.json
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

| skinIndex | 路徑格式 |
|-----------|---------|
| `> 0` | `hero-skin/[generalName]/[skinIndex]/dynamicSkin/` |
| `== 0` | `image/fullskin/dynamicSkin/[generalName]/dynamicSkin/` |

### 動畫觸發時機

| 觸發時機 | 動作類型 | 預設動畫名稱 |
|---------|---------|-------------|
| 遊戲開始 / 角色登場 | `entrance` | `ChuChang` |
| 使用攻擊（出現指示線） | `attack` | `GongJi` |
| 使用非攻擊技能 | `special` | `TeShu` |

### config.json 配置檔

```jsonc
{
    // ── 全域設定 ─────────────────────────────────────────
    "scale":          0.35,    // 縮放比例，預設 0.35
    "runtimeVersion": "3.8",   // Spine 版本（3.5.35 / 3.7 / 3.8 / 4.0 / 4.1）
    "idleAlpha":      1.0,     // 待機透明度，預設 1.0
    "background":     "",       // 頭像背景圖（相對 assets 前綴）

    // ── 個別動作設定 ─────────────────────────────────────
    "attack": {
        "animationName": "GongJi",       // 動畫名稱
        "skelBasePath":  "",              // 使用其他骨架（空＝使用當前）
        "scale":         0.35,            // 覆蓋全域 scale
        "flipX":         false,           // 是否水平翻轉
        "showTime":      1.5,             // 停留在場景中央的時間（秒）；0＝等動畫播完
        "runtimeVersion": ""              // 覆蓋全域 runtimeVersion
    },
    "special": {
        "animationName": "TeShu",
        "scale":         0.35,
        "flipX":         false,
        "showTime":      0
    },
    "entrance": {
        "animationName": "ChuChang",
        "scale":         0.35,
        "flipX":         false,
        "showTime":      0
    },

    // ── 指示線特效 ───────────────────────────────────────
    "indicator": {
        "enabled":        true,            // 是否啟用指示線特效
        "skelName":       "effects/beam",  // 指示線骨架路徑
        "effectName":     "effects/burst", // 命中特效骨架路徑
        "runtimeVersion": "",              // Spine 版本
        "delay":          0.3,             // 延遲生成（相對動作動畫時長的比例）
        "speed":          1.0,             // 指示線移動速度倍率
        "effectDelay":    0.5              // 命中特效延遲
    }
}
```

### 自動動畫偵測

若 `config.json` 中未指定動畫，系統會自動掃描骨架：

| 動作 | 自動比對的動畫名稱（依優先順序） |
|------|--------------------------------|
| 攻擊 | `GongJi`, `gongji`, `Attack`, `attack` |
| 技能 | `TeShu`, `teshu`, `JiNeng`, `jineng`, `JiNeng01`, `JiNeng02`, `Special`, `special`, `Skill`, `skill` |
| 出場 | `ChuChang`, `chuchang`, `Entrance`, `entrance`, `Appear`, `appear` |

**備援機制**：若上述名稱都不存在，自動使用持續時間最長的動畫。

---

## 皮膚別名系統（ResourceAlias）

### 用途

允許多個武將名稱共用同一套皮膚資源。

### 設定方式

在 Lua 腳本中使用：

```lua
-- 設定武將皮膚別名
-- 語法：addResourceAlias(資源類型, 顯示名稱, 實際路徑名稱)
sgs.Sanguosha:addResourceAlias("heroskin", "s4_zhonghui", "heg_zhonghui_2")

-- 多個 skinIndex
sgs.Sanguosha:addResourceAlias("heroskin", "guanxingzhangbao", "guanxingzhangbao_1")
sgs.Sanguosha:addResourceAlias("heroskin", "guanxingzhangbao", "guanxingzhangbao_2")
```

### 路徑解析

| 別名設定 | 實際路徑 |
|---------|---------|
| `addResourceAlias("heroskin", "s4_zhonghui", "heg_zhonghui_2")` | `hero-skin/heg_zhonghui_2/1/` |

### 動態皮膚特殊規則

若武將名稱結尾為 `_[數字]`（如 `heg_zhonghui_2`），自動使用 heroskin 路徑：

```cpp
// src/ui/CharacterSpineActionController.cpp:296-303
QRegularExpression heroskinPattern(R"(_\d+$)");
if (heroskinPattern.match(resolvedGeneral).hasMatch())
    return QString("heroskin/dynamicSkin/%1/dynamicSkin").arg(resolvedGeneral);
```

---

## 語音搜尋優先級總覽

### 技能語音

1. `hero-skin/[generalName]/[skinIndex]/[skillName]1.ogg`
2. `hero-skin/[generalName]/[skinIndex]/[skillName]2.ogg`
3. `hero-skin/[generalName]/[skinIndex]/[skillName].ogg`
4. `audio/skill/[skillName].ogg`（預設）

### 卡牌語音

1. `hero-skin/[generalName]/[skinIndex]/card/[cardName].ogg`
2. `hero-skin/[generalName]/[skinIndex]/[cardName].ogg`
3. `audio/card/[generalName]/[cardName].ogg`（預設）

### 死亡語音

1. `hero-skin/[generalName]/[skinIndex]/death.ogg`
2. `audio/death/[generalName].ogg`（預設）

---

## 完整範例：關羽皮膚

### 目錄結構

```
hero-skin/
└── guanyu/
    ├── 1/
    │   ├── full.png
    │   ├── full.gif
    │   ├── card.jpg
    │   ├── fulldual.png
    │   ├── death.ogg
    │   ├── wusheng.ogg
    │   ├── wusheng1.ogg
    │   ├── wusheng2.ogg
    │   ├── card/
    │   │   └── slash.ogg
    │   └── zh_CN.lua
    └── 2/
        ├── full.png
        ├── card.jpg
        └── ...

# Spine 動態皮膚使用特殊路徑（注意：不在 hero-skin/guanyu/1/ 下）
heroskin/dynamicSkin/guanyu_1/dynamicSkin/
├── skeleton.skel
├── skeleton.atlas
├── skeleton.png
└── config.json
```

### zh_CN.lua 內容

```lua
local t = {
    ["wusheng"] = "青龙之魂",
    [":wusheng"] = "你可以将一张红色牌当【杀】使用或打出。",
    ["$wusheng1"] = "看尔乃插标卖首！",
    ["$wusheng2"] = "汝比颜良、文丑如何？",
    ["~guanyu"] = "桃源之誓，竟成泡影...",
}

sgs.LoadSkinTransltionTable(t)
```

### config.json 內容

```json
{
    "scale": 0.35,
    "runtimeVersion": "3.8",
    "attack": {
        "animationName": "GongJi",
        "showTime": 1.5
    },
    "special": {
        "animationName": "TeShu"
    },
    "entrance": {
        "animationName": "ChuChang"
    }
}
```

---

## 相關程式碼參考

| 功能 | 檔案位置 |
|------|---------|
| 皮膚容器 UI | `src/ui/heroskincontainer.cpp` |
| 皮膚項 UI | `src/ui/skinitem.cpp` |
| GIF 動畫載入 | `src/ui/graphicspixmaphoveritem.cpp:268-331` |
| 技能語音搜尋 | `src/core/skill.cpp:322-336` |
| 卡牌語音搜尋 | `src/ui/skin-bank.cpp:485-562` |
| 死亡語音搜尋 | `src/core/general.cpp:307-353` |
| Spine 動態皮膚 | `src/ui/CharacterSpineActionController.cpp` |
| 皮膚翻譯載入 | `src/core/general.cpp:424-440` |
| 翻譯函數定義 | `lua/sgs_ex.lua:918-932` |

---

## 常見問題

### Q: 如何新增一個皮膚？

1. 建立目錄：`hero-skin/[generalName]/[skinIndex]/`
2. 放入必要檔案：`full.png`、`card.jpg`
3. （選填）加入語音、翻譯、動態皮膚

### Q: 皮膚編號從多少開始？

從 `1` 開始。`0` 是預設皮膚，不在 `hero-skin` 目錄。

### Q: 如何讓皮膚支援多句技能台詞？

使用編號命名：
- `[skillName]1.ogg`
- `[skillName]2.ogg`
- `[skillName]3.ogg`

遊戲會隨機播放或按索引播放。

### Q: GIF 動畫不播放？

檢查：
1. `full.gif` 是否存在於皮膚目錄
2. 配置 `EnableAnimatedGenerals` 是否為 `true`
3. GIF 檔案是否損壞

### Q: 皮膚翻譯不生效？

確認：
1. 翻譯檔位於正確目錄：`hero-skin/[generalName]/[skinIndex]/[lang].lua`
2. 使用正確函數：`sgs.LoadSkinTransltionTable(t)`（不是 `sgs.LoadTranslationTable`）
3. 語言設定正確（如 `zh_CN`）

### Q: Spine 動態皮膚不顯示？

確認：
1. **路徑是否正確**：
   - `skinIndex > 0`：`hero-skin/[generalName]/[skinIndex]/dynamicSkin/`
   - `skinIndex == 0`：`image/fullskin/dynamicSkin/[generalName]/dynamicSkin/`
2. **檔案是否完整**：`skeleton.skel`、`skeleton.atlas`、`skeleton.png` 三個檔案必須存在
3. **skinIndex 是否正確**：檢查配置 `HeroSkin/[generalName]` 的值

### Q: 動態皮膚路徑如何判斷？

根據 `skinIndex` 值自動判斷：
- `skinIndex > 0` → 使用 `hero-skin/[generalName]/[skinIndex]/dynamicSkin/`
- `skinIndex == 0` → 使用 `image/fullskin/dynamicSkin/[generalName]/dynamicSkin/`

---

## 更新日誌

- 2026-06-05：修改程式碼，讓 dynamicSkin 使用與皮膚資源一致的路徑結構
- 2026-06-05：修正 dynamicSkin 路徑說明，增加詳細的判斷邏輯
- 2026-06-05：初版建立，完整說明 hero-skin 系統結構
