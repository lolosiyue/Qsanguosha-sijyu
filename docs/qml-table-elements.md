# 牌桌 QML 掛載元件

擴展包可以在現有牌桌上掛載自己的 QML 元件，例如掛在座位上的技能計數器、置於桌面中央的計分板，或固定在螢幕角落的提示卡。牌桌本身仍是原有的 `QGraphicsScene`；元件畫在其上方一層透明的 QML 圖層，不改寫座位、手牌區、按鈕與記錄框。

掛載元件有兩條路線：

| 路線 | 觸發方式 | 適用場景 |
|------|----------|----------|
| 路線一：標記綁定 | `room:setPlayerMark` 設定的整數標記 | 元件跟隨某個標記，並且總是掛在座位上 |
| 路線二：Room 指令 | `room:addQmlElement` 等四個指令 | 桌面、螢幕或座位上的元件，可攜帶任意結構的資料 |

兩條路線的 QML 端合約相同（見 §3）。

## 1. 先看限制

撰寫元件前請先確認以下限制，詳細說明見 §7。

- 元件只在 GUI 客戶端顯示。TUI、Web、Excel、Sheets 客戶端收到相關通知時一律安靜忽略，**不可用元件承載遊戲必要的資訊**。
- 元件不應連網（圖層封鎖 `file:`、`qrc:` 以外的 URL，例外見 §7），也不能向伺服器回傳資料。需要玩家作答時，改用 `askForQml`（見 §8）。
- 圖層永遠位於所有牌桌物件之上，飛行中的卡牌會從元件底下經過。
- 玩家檢視面板（inspector）開啟，或座位捲動列出現（帶狀座位分頁時）時，與這些控制項重疊的元件會暫時隱藏。

## 2. Lua API

### 2.1 路線一：標記綁定

擴展載入時登記標記名稱與 QML 的對應。伺服器與客戶端都會執行擴展載入程式，所以這一行兩端都會執行：

```lua
-- Bind a mark (or a prefix) to a QML file and an anchor on the seat.
sgs.Sanguosha:addQmlMark("@jl_zhanyi", "extensions/jl/ui/zhanyi.qml", "mark-area")
sgs.Sanguosha:addQmlMark("@jl_*", "extensions/jl/ui/generic-mark.qml", "avatar")
```

- 第一個參數可為精確名稱，或以 `*` 結尾的前綴（例如 `"@jl_*"`）。同一標記同時符合精確登記與前綴登記時，精確名稱優先；符合多個前綴時，取最長者。單獨的 `"*"` 不接受。
- 第三個參數為座位錨點，省略時為 `mark-area`。可用值為 `mark-area`、`avatar`、`top`、`bottom`（定位方式見 §4）。路線一不能使用桌面與螢幕錨點。
- 重複登記相同的名稱或前綴時，後者覆蓋前者。
- 登記失敗（名稱為空、錨點不明、路徑不合規則）時，只在日誌記錄一行警告，不拋出錯誤。

標記照常以 `room:setPlayerMark` 設定：

```lua
room:setPlayerMark(player, "@jl_zhanyi", 3)  -- show the element, qs.data.value == 3
room:setPlayerMark(player, "@jl_zhanyi", 5)  -- same element, qs.data.value becomes 5
room:setPlayerMark(player, "@jl_zhanyi", 0)  -- remove the element
```

- 標記值大於 0 時顯示，等於 0 時移除。標記值變動時，既有元件不會重建，只更新 `qs.data.value`，元件內部狀態（例如已展開）得以保留。
- 符合登記的標記不再繪製為原本的文字按鈕，改由 QML 元件呈現。沒有登記的客戶端，以及未啟用 QML 的建置（例如 XP 版），照舊顯示文字按鈕。
- 標記值只能是整數。需要傳遞複雜資料時，改用路線二。
- 斷線重連與觀戰時，伺服器只重送名稱以 `@` 或 `&` 開頭的標記（`ServerPlayer::marshal`）。綁定的標記請使用這兩種前綴之一，元件才會自動恢復；其他名稱的標記在重連後不會出現，直到下一次 `setPlayerMark`。

### 2.2 路線二：Room 指令

```lua
-- Add a board at the table center, visible to everybody.
room:addQmlElement("jl_board", "extensions/jl/ui/board.qml", "table-center",
                   {round = 1, text = "Round one", rows = {{name = "A", score = 3}, {name = "B", score = 5}}})

-- Merge fields into the element's data (shallow merge).
room:updateQmlElement("jl_board", {round = 2})

-- Send an element to one player only.
room:addQmlElement("jl_hint", "extensions/jl/ui/hint.qml", "screen-bottom-right", {text = "Your turn"}, player)
room:updateQmlElement("jl_hint", {text = "Waiting"}, player)

room:removeQmlElement("jl_board")        -- remove one element
room:clearQmlElements()                  -- remove every broadcast element
room:clearQmlElements(player)            -- remove every element sent to this player
```

| 指令 | 說明 |
|------|------|
| `addQmlElement(id, qmlPath, anchor[, data[, player]])` | 新增元件。同一個 receiver 下 `id` 已存在時，整個元件被新的內容取代 |
| `updateQmlElement(id, data[, player])` | 將 `data` 淺層合併進已存在的元件；`id` 不存在時只記錄警告 |
| `removeQmlElement(id[, player])` | 移除單一元件 |
| `clearQmlElements([player])` | 移除該 receiver 的全部元件 |

- 錨點：`table-center`；`screen-top`、`screen-bottom`；四個角落 `screen-top-left`、`screen-top-right`、`screen-bottom-left`、`screen-bottom-right`；`seat:<objectName>`（元件置中於指定座位，並跟隨座位移動；`<objectName>` 為玩家的 `objectName`，例如 `sgs1`）。路線二不使用 `mark-area`、`avatar`、`top`、`bottom` 這四個座位錨點。
- `id` 不可為空。
- 省略 `player` 時廣播給所有人（包括觀戰者）；指定 `player` 時只送給該玩家，規則與 `setUiElement` 相同。
- 廣播元件與指定玩家的元件分屬不同範圍，各自管理：省略 `player` 的 `update`、`remove`、`clear` 只作用於廣播元件；指定 `player` 時只作用於送給該玩家的元件。因此同一個 `id` 可以同時存在於兩個範圍，互不影響。
- 伺服器保存的是元件的目前狀態，不是歷史紀錄。計分板每回合更新一次也不會讓重連時的重播量增長。斷線重連的玩家會收到全部廣播元件，加上送給自己的元件，`data` 為目前值。
- 錄影會記下這些通知，重播時預期依序收到新增、更新與移除（此點尚未實際驗證，見 §9）。

### 2.3 `data` 的資料型態

`data` 為 Lua table，字串鍵，值可為布林、數字、字串或巢狀 table，巢狀深度上限為 8 層。省略 `data` 或傳入 `nil` 時視為空 table。

```lua
local data = {
    title = "Board",                -- string
    round = 2,                      -- integer (stays an integer in QML)
    ratio = 0.5,                    -- number
    visible = true,                 -- boolean
    rows = {                        -- a sequence 1..n becomes a JS array
        {name = "A", score = 3},
        {name = "B", score = 5},
    },
    style = {color = "#c0392b"},    -- a table with string keys becomes a JS object
}
```

- 鍵為字串的 table 轉為物件；鍵為 `1..n` 連續整數的 table 轉為陣列。同時含兩種鍵的混合 table、函數、userdata、非有限的數字（`inf`、`nan`）都不接受。
- 頂層的 `data` 必須是以字串為鍵的 table，傳入陣列式的 table 不接受。
- 空 table `{}` 一律轉為空物件，不會是空陣列。預計之後以 `update` 填入清單的欄位，請在 QML 端以 `Array.isArray` 判斷，或一開始就送出非空的清單。
- 資料型態不合規時，指令會向 Lua 拋出錯誤（訊息為 `QML element data must be a table with string keys ...`）。請在擴展的測試中確認。至於超過上限（見 §7）的情況，伺服器只記錄警告並拒絕送出，不拋出錯誤，以免中斷對局。

## 3. QML 端合約

兩條路線的合約相同。元件的根物件必須是 `Item`（或其子類型），並宣告 `property var qs`。圖層會注入以下唯讀內容：

| 欄位 | 內容 |
|------|------|
| `qs.data` | 路線一為 `{mark, value}`；路線二為 `data`（`update` 之後為合併後的目前值） |
| `qs.player` | `{objectName, general, seat, kingdom, alive, self}`；桌面與螢幕元件為 `null` |
| `qs.scale` | 元件目前的顯示縮放倍率 |
| `qs.profile` | 版面類型，見下表 |
| `qs.compact` | 座位收合為帶狀（ribbon）排列時為真；與 `qs.profile` 是否為 `portrait` 無必然關係 |

`qs.player` 隨座位錨點而定：路線一的元件為被標記的玩家；`seat:<objectName>` 為被指定的座位；其他錨點為 `null`。玩家狀態變動（陣亡、換將、勢力變更）時，圖層會推送新的快照，`qs` 以整份快照替換，請以屬性綁定讀取，不要在建立時複製。

`qs.profile` 的值：

| 值 | 說明 |
|----|------|
| `landscape` | 傳統橫向版面（未啟用響應式版面時亦為此值） |
| `portrait` | 直向精簡版面（不代表 `qs.compact` 為真：座位少時仍以馬蹄形排列，`qs.compact` 為假） |
| `compact-landscape` | 橫向精簡版面（座位收合為帶狀時，`qs.compact` 亦可為真） |
| `medium` | 中等版面 |
| `split` | 展開分欄版面 |
| `book` | 書本型版面 |
| `tabletop` | 桌面型版面 |
| `large-room` | 大房間版面 |

幾點需要注意：

- **尺寸**：圖層讀取根物件的 `implicitWidth` 與 `implicitHeight`；兩者為 0 時，改取根物件的 `width` 與 `height`。請以 1 倍縮放時的邏輯像素設計元件，圖層會依 `qs.scale` 整體縮放，**不要在元件內再乘一次 `qs.scale`**。`qs.scale` 供需要判斷實際顯示大小的元件使用，例如在縮得很小時隱藏細節。
- **注入時機**：`qs` 在元件建立之後、首次排版時才注入，`Component.onCompleted` 當下尚不可用。請以屬性綁定讀取（綁定會在 `qs` 到達後自動重新計算），並預期 `qs` 可能暫時為空。
- **互動**：根物件宣告 `property bool qsInteractive: true` 才接收滑鼠與觸控事件。未宣告的元件只用來顯示，指標事件一律落到牌桌。
- **單向**：元件沒有回傳伺服器的通道。
- **視覺模式**：灰階與高對比模式會套用到圖層；使用軟體 scene graph 後端時不支援，此時不套用，元件照常顯示。

範例：

```qml
// extensions/jl/ui/zhanyi.qml
import QtQuick

Item {
    id: root
    property var qs                       // injected by the table layer
    property bool qsInteractive: true     // this element handles taps

    // Fixed size at 1x; the layer scales the whole element.
    implicitWidth: 56
    implicitHeight: 56

    readonly property int value: qs && qs.data ? qs.data.value : 0
    property bool expanded: false         // survives mark value changes

    Rectangle {
        anchors.fill: parent
        radius: 8
        color: root.expanded ? "#aa2b1d" : "#cc4a38"

        Text {
            anchors.centerIn: parent
            color: "white"
            font.pixelSize: 20
            // Switch to a terse look when the seats are collapsed into the ribbon strip.
            text: root.qs && root.qs.compact ? root.value : "Zhanyi " + root.value
        }
    }

    // Click, not hover: touch screens have no hover.
    MouseArea {
        anchors.fill: parent
        onClicked: root.expanded = !root.expanded
    }
}
```

## 4. 錨點

### 4.1 座位錨點（路線一與 `seat:`）

| 錨點 | 位置 |
|------|------|
| `mark-area` | 座位矩形的左上角內側（與座位邊緣相距 2 像素），向右排成一列 |
| `avatar` | 置中於座位矩形（即頭像上） |
| `top` | 座位上緣外側，與座位左緣對齊 |
| `bottom` | 座位下緣外側，與座位左緣對齊 |
| `seat:<objectName>` | 置中於指定座位（僅路線二） |

- 自己的座位以 dashboard 的**頭像區**作為座位矩形，不是整個 dashboard。直向版面中 dashboard 橫跨全寬，以整個 dashboard 為準會讓元件落在手牌上。
- 座位元件依座位實際顯示尺寸縮放，`qs.scale` 已含這個縮放。
- 錨定的座位不可見時（大房間分頁、帶狀排列分頁時不在目前頁面的座位、`LargeRoomOverview`），元件隨之隱藏。座位重新出現時元件恢復，內部狀態保留。
- 同一座位、同一錨點有多個元件時，圖層把它們排成一列（間距 4 像素），不插入原生標記按鈕之間。`avatar` 與 `seat:<objectName>` 的元件在同一座位上共用同一個置中的列。排列順序固定：依元件在圖層中的內部鍵排序，廣播元件在前，其次為路線一的標記元件，最後是送給該玩家的元件。重連之後順序不變。

### 4.2 桌面與螢幕錨點（路線二）

| 錨點 | 位置 |
|------|------|
| `table-center` | 置中於桌面中心（牌堆所在處） |
| `screen-top` | 水平置中，位於 header 下緣之下（相距 8 像素）；未啟用響應式版面時沒有 header，改為位於牌桌區上緣之下 8 像素 |
| `screen-bottom` | 水平置中，位於操作區（`interactionRect`，含手牌區與操作按鈕）上緣之上（相距 8 像素） |
| `screen-top-left`、`screen-top-right` | 主區域的上方兩角，位於 header 下緣之下，與側邊相距 8 像素；未啟用響應式版面時沒有 header，改為位於牌桌區上緣之下 8 像素 |
| `screen-bottom-left`、`screen-bottom-right` | 主區域的下方兩角，位於操作區上緣之上，與側邊相距 8 像素 |

同一桌面或螢幕錨點有多個元件時，同樣排成一列（間距 4 像素）；置中的錨點（`table-center`、`screen-top`、`screen-bottom`）以整列置中；角落錨點則由角落起算排列。

底部錨點不會蓋住手牌區與操作按鈕。螢幕鍵盤彈出使可用高度縮短時，底部錨點會依縮短後的版面重新計算。

### 4.3 帶狀座位（`qs.compact` 為真）

房間版面無法容納環形或馬蹄形座位排列時（座位很多或螢幕很小），版面引擎會退回帶狀（ribbon）排列：座位收合為一條帶狀列並分頁，每個座位都很小，且會出現原生的座位捲動列。`qs.compact` 即表示這個狀態；常見於直向，但直向且座位少時仍使用馬蹄形，`qs.compact` 為假，橫向精簡版面在座位收合時也可能為真。此時其他玩家座位上的 `top`、`bottom`、`mark-area` 三種錨點一律改為**座位矩形內的底部一列**（左側內縮 2 像素、下方內縮 2 像素），並裁切在座位矩形之內，避免壓到相鄰座位或 header。元件可依 `qs.compact` 切換為精簡外觀（見 §3 的範例）。

- `avatar` 與 `seat:<objectName>` 仍置中於座位，不受影響。
- 自己的座位不採用此規則，維持 §4.1 的定位。
- 帶狀座位太小放不下的部分會被裁掉，因此帶狀排列下的座位元件應盡量小。

## 5. 互動與觸控

- 元件需要接收指標事件時，必須宣告 `qsInteractive: true`。游標或觸點進入元件可見矩形時，圖層才接收事件；離開後事件重新落到牌桌，因此元件之外的牌桌操作不受影響。
- 建議以 `MouseArea`（`onClicked`）處理點擊。圖層依 QML 是否接受按下事件來決定點擊由元件或牌桌處理，`MouseArea` 會明確接受它所覆蓋區域內的按下事件。
- 使用 `TapHandler` 時，必須設定 `gesturePolicy: TapHandler.ReleaseWithinBounds`（取得獨占抓取）。預設的手勢策略只取得被動抓取，按下事件可能被回報為未接受，點擊會同時落到牌桌。此做法目前尚未經執行期驗證，優先使用 `MouseArea`。
- QML 沒有接受的點擊，會落到牌桌。例如元件內的 `MouseArea` 只覆蓋部分區域，其餘區域的點擊仍由牌桌處理。
- **觸控沒有懸停**。不要把重要操作放在 `HoverHandler` 或 `hoverEnabled` 的 `MouseArea` 上；改用 `MouseArea.onClicked`。懸停只適合作為滑鼠使用者的額外提示。
- 可互動範圍不小於 48 邏輯像素（對應 `ResponsiveInput::minimumTouchTarget`）。元件本身較小時，請在根物件內加大可點擊的區域，例如在一個 48 像素以上的透明 `Item` 內放置填滿它的 `MouseArea`。
- 目前預期觸控主要以合成的滑鼠事件送達 QML（Qt 的「觸控轉滑鼠」），所以 `MouseArea` 的點擊與拖曳應可使用（`TapHandler` 的條件見上）。多點觸控手勢（例如 `PinchHandler`）尚未驗證，請勿依賴。
- 旋轉裝置或版面改變時，版面重算會觸發圖層重新定位，並推送新的 `qs.profile` 與 `qs.compact`。元件本身不重建，內部狀態保留。
- 被原生控制項蓋住的部分不會接收事件，見 §7。

## 6. 路徑規則

`qmlPath`（路線一的登記與路線二的新增）必須符合：

- 是相對路徑，相對於遊戲素材根目錄（開發樹為 repo 根目錄）。範例中的 `extensions/jl/ui/zhanyi.qml` 即位於素材根目錄之下。
- 副檔名為 `.qml`（不分大小寫）。
- 不接受絕對路徑、`..` 開頭的路徑，也不接受含 `:` 的字串（因此 `qrc:`、`http:` 與 Windows 磁碟機代號一律拒絕）。
- 路徑先經過正規化（反斜線視為 `/`，清除 `.`），再檢查。

路線一在登記時檢查；路線二在伺服器新增時與客戶端建立時各檢查一次，三處使用同一個檢查函式。不合規的元件不會顯示，並在日誌留下警告。

QML 檔案不存在或編譯失敗時，同一路徑只記錄一次警告，並略過該元件，不中斷對局。QML 的 `import` 路徑為 Qt 預設路徑加素材根目錄，擴展可以把自己的共用元件放在素材根目錄下並以相對路徑匯入。

## 7. 限制

- **只在 GUI 客戶端顯示。** TUI、Web、Excel、Sheets 不顯示，也不輸出日誌。元件只能作為裝飾與輔助顯示，遊戲規則需要的資訊（點數、可選項、倒數）必須同時以原有的協議顯示。
- **不應連網。** 圖層的網路存取只放行 `file:` 與 `qrc:` 兩種 scheme，其他一律失敗，因此 `XMLHttpRequest`、`Image` 與 `Loader` 等經由 QML 引擎發出的 URL 請求不能取得遠端資源，也不能載入遠端程式碼。自行開啟 socket 的 QML 模組（例如部署了 QtWebSockets 時的 `WebSocket`）不經過這道限制，圖層無法攔截；擴展不得依賴或使用這類模組連網。
- **沒有回傳通道。** 元件的狀態只存在於本機。
- **資料上限。** 單一元件的 `data` 序列化後不超過 16 KB；每個 receiver（全體或單一玩家）最多 64 個元件。`update` 合併後的結果也受 16 KB 上限約束。超過上限時伺服器拒絕送出，並在伺服器日誌記錄一行警告，不向 Lua 拋出錯誤。
- **位於所有牌桌物件之上。** 圖層疊在牌桌之上、原生浮層之下，所以飛行中的卡牌會從元件底下經過。
- **被原生控制項遮住時隱藏。** 玩家檢視面板（inspector）與座位捲動列（帶狀座位分頁時出現）是原生控制項，圖層無法疊在它們之下。與這些控制項重疊的元件，在控制項顯示期間整個隱藏，控制項收起後恢復。需要隨時可見的元件，請避免放在 header 的座位捲動列或檢視面板可能覆蓋的位置。
- **只有顯示中的部分可互動。** 隱藏或被遮住的元件不接收事件。
- **以快照傳遞資料。** `qs.player` 與 `qs.data` 都是複本，元件拿不到玩家物件，也不要試圖保存它們以外的全域狀態。
- **客戶端須載入同一份擴展。** 路線一的登記要在客戶端也執行；客戶端沒有登記時，標記退回原本的文字按鈕。

## 8. 需要玩家作答時

元件沒有回傳伺服器的通道。需要玩家作答（選擇、確認、輸入）時，使用 `askForQml`，它由伺服器發起、有逾時，結果由伺服器取得。詳見 [ask-for-qml.md](ask-for-qml.md)。

典型的搭配：以掛載元件長期顯示狀態（計分板、計數器），以 `askForQml` 在需要作答時彈出互動面板。

## 9. 驗證方式

端到端腳本為 `tools/autotest/qml_table_layer_smoke.py`。它會建立一個探針擴展（綁定 `@qmlprobe_*` 標記，並新增廣播元件、僅限單一玩家的元件、跟隨座位的元件、檔案不存在的元件，以及一個要求素材目錄之外路徑而必須被伺服器拒絕的元件），然後橫向（1280x720）與直向（540x960，響應式版面）各打一局，依客戶端回報的 `qml_elements:<n>` 階段與元件快照判斷結果。

```bash
python3 tools/autotest/qml_table_layer_smoke.py --exe-root . \
    --server-exe debug/qsanguosha_server --client-exe debug/QSanguosha \
    --artifact-dir artifacts/qml-table-layer --seed 20261007 --no-xvfb --platform xcb
```

`--orientation landscape`、`--orientation portrait` 可只跑單一方向。產物寫入 `--artifact-dir`。

**目前狀態：此腳本尚未端到端執行過。** 在實際跑完之前，請不要把它視為已通過的證明。

自行驗證擴展的元件時，可用下列方式：

- 實際開局，確認元件顯示在預期的錨點。
- 在橫向與直向各看一次；並在座位很多或螢幕很小、座位收合為帶狀時，確認 `qs.compact` 為真時的外觀與裁切。
- 在日誌中尋找以 `Engine::addQmlMark`、`Room::addQmlElement`、`Room::updateQmlElement`、`QmlTableLayer` 開頭的警告，它們會指出路徑、錨點、資料上限與載入失敗的原因。

尚未驗證的項目：錄影重播、TUI 實機（確認安靜忽略）、Android 觸控。
