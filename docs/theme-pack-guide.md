# 主題包自製指南

主題包可以替換牌桌上嘅界面素材（牌背、指示線、emotion 特效、dashboard、戰報背景、CardContainer 背景等），做法類似 Minecraft 材質包：

- 一個主題包 = 一個資料夾 + 一份 `theme.json`。
- 可以同時啟用多個主題包，排先後次序；**同一個素材槽由排最高嗰個提供**，所有已啟用主題都冇提供嘅槽位用返預設素材。
- 冇啟用任何主題時，遊戲外觀同冇呢個功能時完全一樣。

示範：倉庫附兩個主題包，[`themes/demo-crimson`](../themes/demo-crimson/theme.json)（八個槽位）同 [`themes/demo-jade`](../themes/demo-jade/theme.json)（只得兩個槽位，用嚟示範疊加）。佢哋嘅圖由 [`tools/themes/make-demo-theme.py`](../tools/themes/make-demo-theme.py) 生成。

## 1. 放喺邊

遊戲會掃兩個資料夾，每個子資料夾有 `theme.json` 就當係一個主題包：

| 位置 | 用途 |
|------|------|
| `<使用者資料>/themes/` | 玩家自己放主題包嘅地方；主題管理頁「開啟主題資料夾」直接打開佢 |
| `<遊戲資料>/themes/` | 隨遊戲附帶嘅主題包 |

開發樹（直接喺 repo 根目錄運行）兩個位置係同一個 `themes/`。安裝版（Linux）嘅使用者資料夾係 `~/.local/share/QSanguosha/themes/`。兩邊有相同 `id` 嘅主題時，使用者資料夾嗰個優先。

## 2. 啟用、停用、排優先次序

入口（三個都係同一頁）：

- 首頁「設定 → 顯示 → 主題包 → 主題包管理…」
- 舊設定對話框（遊戲內選單）「主題包管理…」掣
- 選單列「Packages → 主題包管理…」

管理頁左邊係可用主題，右邊係已啟用主題，**越上面優先度越高**。揀一個主題可以睇預覽、作者、版本、描述、佢提供咗邊啲槽位，同埋 manifest 有冇問題（警告）。

- 「啟用 →」／雙擊：放去已啟用清單最頂。
- 「← 停用」：拎返落可用清單。
- 「↑ 提高優先」「↓ 降低優先」：調次序。
- 「重新掃描」：放咗新主題包入資料夾之後撳一下，唔使重開遊戲。
- 「套用」／「確定」：即時儲存次序（設定鍵 `ThemePacks/Enabled`）。

**生效時機**：套用後解析表即時更新。之後開嘅房間，同埋套用後先出現嘅元件（新開嘅 CardContainer、新播嘅 emotion、新畫嘅指示線、新發嘅牌）即刻用新素材；已經喺牌桌上嘅 dashboard、座位框、戰報框要重新入房先會換。最穩陣係喺大廳改好先開局。

## 3. `theme.json` 格式

```json
{
    "format": 1,
    "id": "my-theme",
    "name": "我嘅主題",
    "author": "作者名",
    "version": "1.0.0",
    "description": "一句介紹。",
    "preview": "preview.png",
    "slots": {
        "card-back": "card-back.png",
        "card-container-bg": "panels/container.png",
        "emotion": "emotion/"
    },
    "files": {
        "image/system/tip.png": "misc/tip.png"
    }
}
```

| 欄位 | 必填 | 說明 |
|------|------|------|
| `format` | 否 | 目前係 `1`。較新嘅格式會照讀，唔識嘅欄位略過並出警告 |
| `id` | 否 | 只可以用英文字母、數字、`_`、`.`、`-`，最多 64 字；冇寫就用資料夾名。啟用清單記嘅係 id |
| `name`／`author`／`version`／`description` | 否 | 顯示喺管理頁 |
| `preview` | 否 | 預覽圖，預設 `preview.png`；建議 16:9（例如 256×144） |
| `slots` | 否 | 槽位 id → 主題包內嘅檔案（或資料夾，見 §4） |
| `files` | 否 | 進階：直接覆蓋某個舊素材路徑（見 §5） |

規則：

- 所有路徑都係相對主題包資料夾，用 `/`；唔可以用絕對路徑、唔可以 `..` 跳出資料夾，symlink 指出去都唔算。
- 檔案唔存在、槽位 id 打錯，嗰一項會略過並喺管理頁「警告」列出；其他項照常生效。
- JSON 可以寫 `//` 註解。
- PNG 可以另外放一張 `@2x` 版本（例如 `card-back@2x.png`），高 DPI 螢幕會自動用。

## 4. 素材槽一覽

槽位定義喺 [`skins/theme-slots.json`](../skins/theme-slots.json)。每個槽位嘅解析次序：**已啟用主題（由高到低）→ 目前 skin 嘅 image key → 槽位預設檔**。

| 槽位 id | 名稱 | 預設素材 | 建議尺寸 | 備註 |
|---------|------|----------|----------|------|
| `card-back` | 牌背 | `image/system/card-back.png` | 93×130 | 手牌背、暗置牌、對手手牌 |
| `general-card-back` | 武將牌背 | `image/system/unknown.png` | 同原圖 | 未翻開嘅武將卡 |
| `indicator-line` | 指示線 | 冇（程式畫線） | 256×16 | 圖片沿起點→目標方向拉伸，由起點逐步顯示；圖片高度就係線嘅粗度。冇提供就照舊用勢力色漸變線 |
| `emotion` | emotion 播放特效 | `image/system/emotion/` | 同原圖 | **資料夾**槽位，見下面 |
| `dashboard-equip` | Dashboard 裝備區 | `image/fullskin/system/dashboard-equip.png` | 164×170 | 左邊裝備欄底圖 |
| `dashboard-hand` | Dashboard 手牌區 | `image/fullskin/system/dashboard-hand.png` | 746×170 | 中間手牌區底圖，會按視窗闊度拉伸 |
| `dashboard-avatar` | Dashboard 頭像區 | `image/fullskin/system/dashboard-avatar.png` | 171×197 | **頭像畫喺呢張圖下面**：`[3, 3, 165, 191]` 範圍要透明，否則會遮住武將 |
| `dashboard-button-platter` | Dashboard 按鈕托盤 | `image/system/button/platter/bg.png` | 106×168 | 確定／取消／結束按鈕後面嗰塊 |
| `log-box-bg` | 戰報背景 | 冇（沿用全域 `image/system/border.png`） | 例如 40×40 | 九宮格邊框圖，四邊各切 10px；只影響戰報框，唔影響其他文字框 |
| `card-container-bg` | CardContainer 背景 | `image/system/card-container.png` | 716×346 | 五穀、觀星等彈出容器。關閉掣固定喺 (517, 21)，盡量保持原尺寸 |
| `heroskin-container-bg` | 換皮膚面板背景 | `image/system/heroskin-container.png` | 同原圖 | |
| `bubble-chat-bg` | 聊天氣泡背景 | `image/system/bubble.png` | 同原圖 | |
| `photo-frame` | 座位框 | `image/fullskin/system/photo-back.png` | 157×181 | 其他玩家座位底框 |
| `table-bg` | 牌桌背景 | `image/system/backdrop/default.jpg` | 任意 | 只換 skin 嘅 `tableBg`；勢力背景（`tableBgwei` 等）同玩家自訂背景唔受影響 |

### emotion（資料夾槽位）

`"emotion": "emotion/"` 指向主題包入面一個資料夾，結構同 `image/system/emotion/` 一樣：

```
my-theme/emotion/
    peach/0.png, 1.png, 2.png, …      ← 動畫：由 0.png 起連續編號
    killer/0.png, …
    question.png                       ← 單張圖 emotion
```

- 每個 emotion **整個動畫**由「有佢 `0.png`」嘅最高優先主題提供，唔會將兩個主題嘅幀撈埋一齊。
- 主題冇嘅 emotion 照用預設。
- 幀數由資料夾入面 `*.png` 數量決定，畫面每 50ms 換一幀；位置按第一幀尺寸置中。

## 5. 進階：`files` 直接覆蓋舊路徑

槽位以外嘅素材，可以用 `files` 按原本路徑覆蓋：

```json
"files": {
    "image/system/tip.png": "misc/tip.png",
    "image/system/chatface/": "chatface/"
}
```

- 左邊必須係 `image/` 開頭嘅原素材路徑（大小寫唔拘）；以 `/` 結尾就係成個資料夾，入面每個檔案逐個 fallback。
- 只對經 skin 載入嘅素材有效（大部分牌桌素材都係）；少數直接讀檔嘅舊碼唔會跟。
- 同一主題入面 `files` 比 `slots` 優先；唔同主題之間照優先次序。

## 6. 同 skin json 嘅關係

`skins/*.image.json`（例如 `fulldefaultSkin.image.json`）照舊有效，主題包係疊喺 skin 上面：

- 主題包冇提供嘅槽位，用 skin 嘅 image key（例如 `handCardBack`、`dashboardLeftFrame`）。
- 新槽位可以喺 skin json 加對應 key 改預設（`cardContainerBg`、`heroSkinContainerBg`、`bubbleChatBg`、`indicatorLine`、`logBoxBorder`），唔加就用 `theme-slots.json` 嘅預設檔，所以舊 skin json 唔使改。
- 文字 skin（`fulltextSkin`）都食主題包：主題提供嘅圖會蓋過文字 skin 程式生成嘅圖。

## 7. 自製步驟

1. 喺 `themes/`（或者管理頁「開啟主題資料夾」）開一個新資料夾，例如 `my-theme/`。
2. 由 [`themes/demo-crimson/theme.json`](../themes/demo-crimson/theme.json) 抄一份 `theme.json`，改 `id`、`name`，只留你想換嘅槽位。
3. 放圖，盡量跟 §4 嘅建議尺寸。
4. 開管理頁撳「重新掃描」，揀你嘅主題睇「提供素材槽」同「警告」，然後啟用、套用。
5. 開一局睇效果；改完圖再入房就會重新讀。

## 8. 未拆嘅素材

以下元素暫時唔係槽位，原因見括號；部分可以用 §5 `files` 覆蓋：

- 技能按鈕、確定／取消按鈕圖（`button-*` 一組幾十張，用 `files` 覆蓋 `image/system/button/` 或者改 skin json）。
- 勾玉、體力條、階段圖、勢力框（多張成組；用 `files` 覆蓋 `image/fullskin/system/magatamas/` 等資料夾）。
- 聊天表情面板（`emotionpanel` 自己掃資料夾，唔經 skin；未支援）。
- 1v1／3v3 選將排陣圖、角色身份圖（直接用路徑載入；未支援）。
- 主介面（QML 首頁）背景同圖示（唔屬牌桌 skin）。
- Spine 指示線特效（`SpineIndicatorLine`，用 Spine 資源，唔係圖片）。
