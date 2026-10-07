# 主題包自製指南

主題包可以替換牌桌上的介面素材（牌背、指示線、emotion 特效、dashboard、勾玉、按鈕、戰報背景、CardContainer 背景等），做法類似 Minecraft 材質包：

- 一個主題包 = 一個資料夾 + 一份 `theme.json`。
- 可以同時啟用多個主題包並排定先後次序；**同一個素材槽由排在最高者提供**，所有已啟用主題都未提供的槽位則沿用預設素材。
- 未啟用任何主題時，遊戲外觀與沒有此功能時完全相同。
- 對局進行中，擴展包也可以用 Lua 函數臨時改動介面元素（見 §7）。

示範：倉庫附有兩個主題包，[`themes/demo-crimson`](../themes/demo-crimson/theme.json)（八個槽位）與 [`themes/demo-jade`](../themes/demo-jade/theme.json)（只有兩個槽位，用於示範疊加）。其圖片由 [`tools/themes/make-demo-theme.py`](../tools/themes/make-demo-theme.py) 生成。

## 1. 放置位置

遊戲會掃描兩個資料夾，其中含有 `theme.json` 的子資料夾即視為一個主題包：

| 位置 | 用途 |
|------|------|
| `<使用者資料>/themes/` | 玩家自行放置主題包之處；主題管理頁的「開啟主題資料夾」會直接開啟此處 |
| `<遊戲資料>/themes/` | 隨遊戲附帶的主題包 |

開發樹（直接在 repo 根目錄執行）中，兩個位置是同一個 `themes/`。安裝版（Linux）的使用者資料夾為 `~/.local/share/QSanguosha/themes/`。兩處有相同 `id` 的主題時，以使用者資料夾者優先。

## 2. 啟用、停用與優先次序

入口（三者為同一頁面）：

- 首頁「设置 → 显示 → 主题包 → 主题包管理…」
- 舊設定對話框（遊戲內選單）的「主题包管理…」按鈕
- 選單列「Packages → 主题包管理…」

管理頁左側為可用主題，右側為已啟用主題，**越上方優先度越高**。選取主題可查看預覽、作者、版本、描述、所提供的槽位與字色，以及 manifest 是否有問題（警告）。

- 「启用 →」／雙擊：移至已啟用清單最上方。
- 「← 停用」：移回可用清單。
- 「↑ 提高优先级」「↓ 降低优先级」：調整次序。
- 「重新扫描」：放入新主題包後按一下即可，無須重開遊戲。
- 「应用」／「确定」：即時儲存次序（設定鍵 `ThemePacks/Enabled`）。

**生效時機**：套用後解析表即時更新。之後開啟的房間，以及套用後才出現的元件（新開的 CardContainer、新播放的 emotion、新繪製的指示線、新發的牌、新的戰報行與聊天訊息）會立即採用新素材與字色；已在牌桌上的 dashboard、座位框、戰報框須重新入房才會更換。最穩妥的做法是在大廳改好再開局。（§7 的局中改動則會立即重繪。）

## 3. `theme.json` 格式

```json
{
    "format": 1,
    "id": "my-theme",
    "name": "我的主題",
    "author": "作者名",
    "version": "1.0.0",
    "description": "一句介紹。",
    "preview": "preview.png",
    "slots": {
        "card-back": "card-back.png",
        "card-container-bg": "panels/container.png",
        "emotion": "emotion/"
    },
    "colors": {
        "log-text": "#202020"
    },
    "files": {
        "image/system/tip.png": "misc/tip.png"
    }
}
```

| 欄位 | 必填 | 說明 |
|------|------|------|
| `format` | 否 | 目前為 `1`。較新的格式仍會讀取，不認識的欄位略過並發出警告 |
| `id` | 否 | 只可使用英文字母、數字、`_`、`.`、`-`，最多 64 字；未填則用資料夾名。啟用清單記錄的是 id |
| `name`／`author`／`version`／`description` | 否 | 顯示於管理頁 |
| `preview` | 否 | 預覽圖，預設 `preview.png`；建議 16:9（例如 256×144） |
| `slots` | 否 | 槽位 id → 主題包內的檔案（或資料夾，見 §4） |
| `colors` | 否 | 字色 id → 顏色（見 §4「文字顏色」） |
| `files` | 否 | 進階：直接覆蓋某個舊素材路徑（見 §5） |

規則：

- 所有路徑皆相對於主題包資料夾，使用 `/`；不得使用絕對路徑，也不得以 `..` 跳出資料夾，指向外部的 symlink 亦不算數。
- 檔案不存在或槽位 id 打錯時，該項會被略過並列於管理頁「警告」；其他項照常生效。
- JSON 可寫 `//` 註解。
- PNG 可另外放一張 `@2x` 版本（例如 `card-back@2x.png`），高 DPI 螢幕會自動採用。

## 4. 素材槽一覽

槽位定義於 [`skins/theme-slots.json`](../skins/theme-slots.json)。每個槽位的解析次序：**局中改動（§7）→ 已啟用主題（由高至低）→ 目前 skin 的 image key → 槽位預設檔**。

| 槽位 id | 名稱 | 預設素材 | 建議尺寸 | 備註 |
|---------|------|----------|----------|------|
| `card-back` | 牌背 | `image/system/card-back.png` | 93×130 | 手牌背面、暗置牌、對手手牌 |
| `general-card-back` | 武將牌背 | `image/system/unknown.png` | 同原圖 | 未翻開的武將卡 |
| `indicator-line` | 指示線 | 無（程式繪製） | 256×16 | 圖片沿起點→目標方向拉伸，由起點逐步顯示；圖片高度即線的粗細。未提供時沿用勢力色漸層線 |
| `emotion` | emotion 播放特效 | `image/system/emotion/` | 同原圖 | **資料夾**槽位，見下文 |
| `dashboard-equip` | Dashboard 裝備區 | `image/fullskin/system/dashboard-equip.png` | 164×170 | 左側裝備欄底圖 |
| `dashboard-hand` | Dashboard 手牌區 | `image/fullskin/system/dashboard-hand.png` | 746×170 | 中間手牌區底圖，會依視窗寬度拉伸 |
| `dashboard-avatar` | Dashboard 頭像區 | `image/fullskin/system/dashboard-avatar.png` | 171×197 | **頭像畫在此圖下方**：`[3, 3, 165, 191]` 範圍須透明，否則會遮住武將 |
| `dashboard-button-platter` | Dashboard 按鈕托盤 | `image/system/button/platter/bg.png` | 106×168 | 確定／取消／結束按鈕後方的底板 |
| `log-box-bg` | 戰報背景 | 無（沿用全域 `image/system/border.png`） | 例如 40×40 | 九宮格邊框圖，四邊各切 10px；只影響戰報框，不影響其他文字框 |
| `card-container-bg` | CardContainer 背景 | `image/system/card-container.png` | 716×346 | 五穀、觀星等彈出容器。關閉按鈕固定於 (517, 21)，請盡量保持原尺寸 |
| `heroskin-container-bg` | 換皮膚面板背景 | `image/system/heroskin-container.png` | 同原圖 | |
| `bubble-chat-bg` | 聊天氣泡背景 | `image/system/bubble.png` | 同原圖 | |
| `photo-frame` | 座位框 | `image/fullskin/system/photo-back.png` | 157×181 | 其他玩家的座位底框 |
| `table-bg` | 牌桌背景 | `image/system/backdrop/default.jpg` | 任意 | 只替換 skin 的 `tableBg`；勢力背景（`tableBgwei` 等）與玩家自訂背景不受影響 |
| `magatamas` | 體力勾玉 | `image/fullskin/system/magatamas/` | 同原圖 | **資料夾**：`0.png`–`5.png` 勾玉、`bg1.png`–`bg4.png` 底圖 |
| `phase-icons` | 階段圖示 | `image/system/phase/` | 同原圖 | **資料夾**：`round_start`、`start`、`judge`、`draw`、`play`、`discard`、`finish` |
| `focus-frames` | 座位高亮框 | `image/system/frame/` | 同原圖 | **資料夾**：`playing`、`responding`、`sos`、`photoSelected`、`dashboardSelected` |
| `handcard-count-bg` | 手牌數底圖 | `image/fullskin/system/handcard/` | 同原圖 | **資料夾**：每個勢力一張 `<勢力>.png` |
| `kingdom-frames` | 勢力邊框 | `image/fullskin/kingdom/frame/` | 同原圖 | **資料夾**：座位用 `<勢力>.png`，dashboard 用 `dashboard/<勢力>.png` |
| `kingdom-icons` | 勢力圖示 | `image/kingdom/icon/` | 同原圖 | **資料夾**：每個勢力一張 `<勢力>.png` |
| `chain-icon` | 橫置圖示 | `image/system/chain.png` | 同原圖 | dashboard 會裁成 134×19，請盡量保持原尺寸 |
| `faceturned-mask` | 翻面遮罩 | `image/fullskin/generals/faceturned.png` | 同原圖 | |
| `save-me-icon` | 瀕死圖示 | `image/system/death/save-me.png` | 同原圖 | |
| `death-icons` | 陣亡圖示 | `image/system/death/` | 同原圖 | **資料夾**：每個身分一張 `<身分>.png`（`lord`、`loyalist`、`rebel`、`renegade`…） |
| `delayed-trick-icons` | 判定區圖示 | `image/icon/` | 同原圖 | **資料夾**：每張延時錦囊一張 `<牌名>.png`，另有 `Judgelose.png` |
| `skill-buttons` | 技能按鈕 | `image/fullskin/system/button/skill/` | 同原圖 | **資料夾**：`<類型>/<狀態>-<寬度>.png` |
| `dashboard-buttons` | 確定／取消／棄牌按鈕 | `image/system/button/platter/` | 同原圖 | **資料夾**：`<按鈕>/<狀態>.png` |
| `card-suits` | 卡牌花色 | `image/system/cardsuit/` | 同原圖 | **資料夾**：`spade`、`heart`、`club`、`diamond`、`no_suit`… |
| `card-numbers-red` | 紅色點數 | `image/system/red/` | 同原圖 | **資料夾**：`0.png`–`14.png` |
| `card-numbers-black` | 黑色點數 | `image/system/black/` | 同原圖 | **資料夾**：`0.png`–`14.png` |

**資料夾槽位**（emotion 除外）逐檔回退：主題包資料夾中有該檔就用主題的版本，沒有則用原檔，因此只需放入想替換的幾張。

### emotion（資料夾槽位）

`"emotion": "emotion/"` 指向主題包內的一個資料夾，結構與 `image/system/emotion/` 相同：

```
my-theme/emotion/
    peach/0.png, 1.png, 2.png, …      ← 動畫：由 0.png 起連續編號
    killer/0.png, …
    question.png                       ← 單張圖 emotion
```

- 每個 emotion 的**整段動畫**由「含有其 `0.png`」的最高優先主題提供，不會混用兩個主題的影格。
- 主題未提供的 emotion 沿用預設。
- 影格數由資料夾內 `*.png` 的數量決定，畫面每 50ms 換一格；位置依第一格的尺寸置中。

### 文字顏色（`colors`）

若將 `table-bg`、`log-box-bg` 換成淺色背景，預設白字會難以辨識，因此須以 `colors` 一併調整字色。字色 id 定義於 [`skins/theme-slots.json`](../skins/theme-slots.json) 的 `colors`；與槽位相同，由設定了該 id 的最高優先主題提供。

| 字色 id | 名稱 | 未設定時 | 影響範圍 |
|---------|------|----------|----------|
| `table-text` | 牌桌文字颜色 | 玩家「文本框字体」顏色（預設白） | 直接疊在牌桌背景上的文字：聊天框、錄影時間 |
| `log-text` | 战报文字颜色 | 玩家「文本框字体」顏色（預設白） | 戰報正文與分隔線 |
| `log-from` | 战报行动角色颜色 | 綠 | 戰報中行動的角色 |
| `log-to` | 战报目标角色颜色 | 紅 | 戰報中的目標角色 |
| `log-highlight` | 战报卡牌与数值颜色 | 黃 | 戰報中的牌名、技能名與數值 |

- 值可寫 `#RRGGBB` 或 SVG 顏色名（例如 `"darkred"`）；透明度會被忽略。
- 主題設定的字色會覆蓋玩家在「文本框字体」選擇的顏色；停用主題即恢復玩家設定。
- id 寫錯或顏色無效時，該項會被略過並列於管理頁「警告」。
- 按鈕文字與牌堆數字繪於各自的底圖上，不受 `colors` 影響。

## 5. 進階：以 `files` 直接覆蓋舊路徑

槽位以外的素材，可以用 `files` 依原路徑覆蓋：

```json
"files": {
    "image/system/tip.png": "misc/tip.png",
    "image/system/chatface/": "chatface/"
}
```

- 左側必須是以 `image/` 開頭的原素材路徑（不分大小寫）；以 `/` 結尾即代表整個資料夾，其中每個檔案逐一回退。
- 只對經由 skin 載入的素材有效（大部分牌桌素材皆是）；少數直接讀檔的舊程式碼不受影響。
- 同一主題內 `files` 優先於 `slots`；不同主題之間依優先次序。

## 6. 與 skin json 的關係

`skins/*.image.json`（例如 `fulldefaultSkin.image.json`）照常有效，主題包疊加於 skin 之上：

- 主題包未提供的槽位，使用 skin 的 image key（例如 `handCardBack`、`dashboardLeftFrame`）。
- 新槽位可在 skin json 加入對應 key 以更改預設（`cardContainerBg`、`heroSkinContainerBg`、`bubbleChatBg`、`indicatorLine`、`logBoxBorder`）；未加入則使用 `theme-slots.json` 的預設檔，因此舊 skin json 無須修改。
- 文字 skin（`fulltextSkin`）同樣套用主題包：主題提供的圖片會覆蓋文字 skin 程式生成的圖片。

## 7. 對局中以函數修改（Lua）

擴展包的技能或事件可以在**對局進行中**修改介面元素，效果只維持至本局結束，不會寫入玩家設定。這些改動疊加於玩家自行啟用的主題包**之上**，因此一律優先。斷線重連的玩家會重新收到同一批改動。

```lua
-- 所有玩家：牌桌背景換成魏國背景
room:setUiSlot("table-bg", "image/system/backdrop/wei.jpg")
-- 只修改某一位玩家看到的牌背
room:setUiSlot("card-back", "extensions/mypack/image/card-back.png", player)
-- 資料夾槽位：換成另一套勾玉
room:setUiSlot("magatamas", "extensions/mypack/image/magatamas/")
-- 借用已安裝主題包的某個槽位
room:setUiSlot("dashboard-hand", "theme:demo-crimson")
-- 戰報字色
room:setUiColor("log-text", "#202020")
-- 直接覆蓋舊路徑（與 theme.json 的 files 相同）
room:setUiFile("image/system/tip.png", "extensions/mypack/image/tip.png")
-- 疊加或移除整個已安裝的主題包
room:setUiThemePack("demo-crimson")
room:setUiThemePack("demo-crimson", false)
-- 還原單一項目（值傳空字串），或全部還原
room:setUiSlot("table-bg", "")
room:resetUi()
```

| 函數 | 說明 |
|------|------|
| `room:setUiSlot(slot, path[, player])` | 槽位 id 見 §4；`path` 為遊戲資料夾內的相對路徑（資料夾槽位須為以 `/` 結尾的資料夾），或 `theme:<主題 id>` |
| `room:setUiColor(id, color[, player])` | 字色 id 見 §4「文字顏色」；`color` 可寫 `#RRGGBB`、顏色名或 `theme:<主題 id>` |
| `room:setUiFile(imagePath, path[, player])` | `imagePath` 為以 `image/` 開頭的舊路徑，以 `/` 結尾即代表整個資料夾 |
| `room:setUiThemePack(id[, enabled[, player]])` | 將一個**玩家本機已安裝**的主題包疊加至最上層；傳 `false` 則移除 |
| `room:resetUi([player])` | 清除本局所有局中改動 |
| `room:setUiElement(kind, id, value[, player])` | 以上各函數的通用版本，`kind` 為 `slot`／`color`／`file`／`pack`／`reset` |

- 未傳 `player` 即作用於所有玩家；傳入則只改動該玩家的畫面。
- 值傳空字串即還原該項。
- 路徑由**各 client 自行**解析：找不到檔案、跳出遊戲資料夾（絕對路徑或 `..`）、主題包未安裝時，該 client 會在 log 發出警告並忽略，對局照常進行。
- 改動後立即重繪：座位框、dashboard、勾玉、階段圖示、技能按鈕、確定／取消按鈕、手牌與場上的牌、戰報框與字色；牌桌背景只在改動 `table-bg`（或還原、疊加的主題包涉及 `table-bg`）時才更換，不會覆蓋勢力背景。
- 網頁版 client 沒有主題包，只會跟隨 `table-bg` 與 `resetUi`。
- 協定：`S_COMMAND_SET_UI_THEME`（135），payload 為 `{kind, id, value}`；錄影會一併記錄。
- 端對端驗證：`python3 tools/autotest/ui_theme_runtime_smoke.py --exe-root . --artifact-dir artifacts/ui-theme-runtime --no-xvfb --platform xcb`。

## 8. 自製步驟

1. 在 `themes/`（或管理頁「打开主题文件夹」）建立新資料夾，例如 `my-theme/`。
2. 從 [`themes/demo-crimson/theme.json`](../themes/demo-crimson/theme.json) 複製一份 `theme.json`，修改 `id`、`name`，只保留想替換的槽位。
3. 放入圖片，盡量依照 §4 的建議尺寸；若換成淺色背景，請一併設定 `colors`。
4. 開啟管理頁按「重新扫描」，選取你的主題查看「提供的素材槽」與「警告」，然後啟用並套用。
5. 開一局查看效果；修改圖片後重新入房即會重新讀取。

## 9. 尚未拆分的素材

以下元素暫時不是槽位，原因見括號；部分可用 §5 的 `files` 覆蓋：

- 手牌區按鈕、錄影按鈕、CardContainer 按鈕（`image/system/button/` 其餘子資料夾；可用 `files` 覆蓋）。
- 進度條（`progressBar` 是依數值分段的圖組）。
- 聊天表情面板（`emotionpanel` 自行掃描資料夾，不經 skin；尚未支援）。
- 1v1／3v3 選將排陣圖、角色身分圖（直接以路徑載入；尚未支援）。
- 主介面（QML 首頁）背景與圖示（不屬於牌桌 skin）。
- Spine 指示線特效（`SpineIndicatorLine`，使用 Spine 資源而非圖片）。
