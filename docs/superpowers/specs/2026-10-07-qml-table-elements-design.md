# 牌桌 QML 掛載元件設計

日期：2026-10-07
狀態：設計已確認，待撰寫實作計畫

## 目標

讓擴展包在現有牌桌（`QGraphicsScene`）上掛載自訂的 QML 元件，作法參考 FreeKill 的
`Engine:addQmlMark` 與桌面元件。現有牌桌不改寫為 QML。

## 範圍

納入：

- 以玩家標記驅動、掛在座位上的 QML 元件（路線一）。
- 以 Room 指令驅動、掛在桌面、螢幕或座位上的 QML 元件（路線二）。
- 元件可在本機互動（懸停、展開、動畫）。

不納入：

- 以 QML 改寫或重新排版既有的座位、手牌區、按鈕、記錄框。
- 元件主動回傳伺服器的通道。需要玩家作答時，仍使用 `askForQml`。
- TUI、Web、Excel、Sheets 等客戶端的渲染。

## 渲染方式

採用透明 QML 圖層：在 `FitView` 的 viewport 上方疊一層透明 `QQuickWidget`。
不採用的兩個方案及原因：

- 以 `QQuickRenderControl` 離屏渲染後放進場景：每一幀都要回讀到 CPU，滑鼠事件需手動轉發，
  在 WSLg 與軟體渲染下風險最高。
- 每個元件各包一個 `QGraphicsProxyWidget(QQuickWidget)`：在 GL viewport 下會退回 grab 渲染，
  速度慢，且容易出錯。

已知代價：圖層永遠位於所有牌桌物件之上，飛行中的卡牌會從元件底下經過。若日後個別元件需要
疊在卡牌之下，可單獨改走離屏渲染，擴展 API 不受影響。

## 擴展 API

### 路線一：標記綁定

擴展載入時登記標記名稱與 QML 的對應。伺服器與客戶端都會執行這行程式：

```lua
sgs.Sanguosha:addQmlMark("@jl_zhanyi", "extensions/jl/ui/zhanyi.qml", "mark-area")
```

- 標記名稱可為精確名稱，或以 `*` 結尾的前綴（例如 `"@jl_*"`）。
- 錨點：`mark-area`（現有標記列旁）、`avatar`（頭像上）、`top`、`bottom`（座位上緣、下緣）。
- 標記照常以 `room:setPlayerMark` 設定。值大於 0 時顯示，等於 0 時移除。
- 符合登記的標記不再繪製為原本的文字按鈕，改由 QML 元件呈現。
- 標記值只能是整數。需要傳遞複雜資料時，改用路線二。

### 路線二：Room 指令

```lua
room:addQmlElement("jl_board", "extensions/jl/ui/board.qml", "table-center", {round = 1, text = "..."}, player)
room:updateQmlElement("jl_board", {round = 2}, player)   -- 淺層合併進 data
room:removeQmlElement("jl_board", player)
room:clearQmlElements(player)
```

- 錨點：`table-center`；`screen-top`、`screen-bottom` 與四個角落；`seat:<objectName>`（跟隨指定座位）。
- `data` 為 Lua table，沿用 `askForQml` 的 `QVariantMap` typemap。
- 省略 `player` 時廣播給所有人（包括觀戰者）；指定 `player` 時只送給該玩家，規則與
  `setUiElement` 相同。

### QML 端合約

兩條路線相同。元件根物件只需宣告 `property var qs`，即會注入以下唯讀內容：

| 欄位 | 內容 |
|---|---|
| `qs.data` | 路線一為 `{mark, value}`；路線二為 `data` |
| `qs.player` | `{objectName, general, seat, kingdom, alive, self}`；桌面與螢幕元件為 `null` |
| `qs.scale` | 目前的 UI 縮放倍率 |
| `qs.profile` | 版面類型，見「直向與響應式版面」 |
| `qs.compact` | 座位為帶狀精簡排列時為真 |

- 元件尺寸取根物件的 `implicitWidth`、`implicitHeight`。
- 根物件宣告 `property bool qsInteractive: true` 才接收滑鼠事件；未宣告的元件，點擊一律落到牌桌。
- 元件不提供回傳伺服器的通道。

### 路徑限制

QML 路徑只能位於素材根目錄或 `extensions/` 之下，不接受絕對路徑或 `..`。路線一在登記時檢查，
路線二在伺服器與客戶端各檢查一次，與 `setUiFile` 使用同一套檢查。

## 協定與斷線重連

### 路線一

不新增協定。登記表存放於 `Engine`，伺服器與客戶端各自在載入擴展時建立。標記沿用現有
`setPlayerMark` 的同步機制，斷線重連、觀戰、錄影皆已涵蓋。沒有登記的客戶端照舊顯示文字按鈕。

### 路線二

新增通知 `S_COMMAND_QML_ELEMENT = 136`，payload 欄位：

| 欄位 | 說明 |
|---|---|
| `op` | `add`、`update`、`remove`、`clear` |
| `id` | 元件 id（`clear` 不帶） |
| `qml` | QML 路徑（僅 `add`） |
| `anchor` | 錨點（僅 `add`） |
| `data` | `add` 為完整資料，`update` 為要合併的欄位 |

需要登記或修改的位置：

- `src/core/protocol.h`
- `src/core/protocol/protocol-payload-registry.cpp`（`QmlElementPayload`）
- `src/client/client.cpp` 的 callback
- `src/client/core/client-game-state-reducer.cpp`：認得此指令，但不改變遊戲狀態
- `src/client/client-log-formatter.cpp`
- `artifacts/protocol-v2-flow-matrix.json`

### 伺服器保存目前狀態

`Room::m_qmlElements` 的結構為 `receiver（"" 代表全體）→ id → {qml, anchor, data}`，保存的是
目前狀態，不是歷史紀錄：

- `update` 直接合併進已保存的 `data`。
- `remove` 刪除單一項目；`clear` 刪除該 receiver 的全部項目。

理由：計分板一類的元件可能每回合更新一次，保存歷史會無限增長，重連時也需要整串重播。這一點
與 `m_uiThemeHistory` 的做法不同。

### 重連與觀戰

- 斷線重連：在 `player-lifecycle-service.cpp` 補送 `m_uiThemeHistory` 之後，對該玩家逐一送出
  `add`。送出的範圍是全體元件，加上 receiver 為該玩家本人的元件，`data` 為目前值。
- 觀戰：若觀戰者加入時走另一條路徑，同樣補送全體元件。實作時須先確認觀戰加入的路徑。
- 錄影重播：通知本身會寫入錄影，重播時依序收到 `add`、`update`、`remove`，不另做處理。

### 其他客戶端

TUI、Web、Excel、Sheets 收到此通知時一律安靜忽略，不輸出日誌。擴展作者須知道，這類元件只在
GUI 客戶端顯示，不可用來承載遊戲必要的資訊。此點須寫入擴展文件。

### 上限

- 單一元件的 `data` 序列化後不超過 16 KB。
- 每個 receiver 最多 64 個元件。
- 超過上限時伺服器拒絕送出，並在伺服器日誌記錄一行警告，不向 Lua 拋出錯誤，以免中斷對局。

## 客戶端圖層 `QmlTableLayer`

### 結構

- 新增 `src/ui/qml-table-layer.{h,cpp}`，類別為透明 `QQuickWidget`，作為 `FitView::viewport()`
  的子元件，位於 `RoomOverlayHost` 之下，生命週期比照 `FitView::m_overlay`：每個 `RoomScene`
  建立一次，離開房間時銷毀。
- 整層共用一個 `QQmlEngine`，元件以 QML 路徑為鍵快取 `QQmlComponent`。
- 根 QML 為內建的 `qrc:/QSanguosha/Table/TableLayer.qml`，內含一個 `Repeater`，為每個元件套上
  一個定位用的容器。
- `QSAN_ENABLE_QML=0` 的建置不建立此圖層；已登記的標記退回原本的文字按鈕。

### 資料一律為快照

`qs.player` 與 `qs.data` 都是 `QVariantMap` 複本，不把 `ClientPlayer*` 或其他 `QObject` 指標交給
QML。牌桌收尾時發生的兩次 UAF（`GameSnapshot` 與 Lua 持有的 `QVariant`）都源於持有方比被持有物
活得更久；改用快照可從結構上避免同類問題。玩家狀態改變（陣亡、換將、勢力變更）時，由
`RoomScene` 推送新的快照。

### 定位

- 座位錨點：取 `Photo` 或 `Dashboard` 的 `sceneBoundingRect()`，經 `FitView::mapFromScene`
  轉為圖層座標。`table-center` 與螢幕錨點直接依 viewport 計算。
- 更新時機：監聽 `QGraphicsScene::changed`，合併為每幀最多計算一次，只有矩形實際變動時才寫回
  QML。如此可涵蓋 refit、UI 縮放、座位分頁與換位動畫，不必逐一接線。
- 錨定的座位不可見時（大房間分頁、`LargeRoomOverview`），元件隨之隱藏。
- 同一座位、同一錨點有多個元件時，由圖層排成一列，不插入原生標記按鈕之間。

### 滑鼠事件

`setMask` 會同時裁掉繪製區域，因此不能沿用 `RoomOverlayHost::updateMask` 的做法。改為：

- 圖層預設設定 `Qt::WA_TransparentForMouseEvents`。
- 在 viewport 上安裝事件過濾器：游標進入任何 `qsInteractive` 元件的矩形時，解除穿透；離開時恢復。
- Wayland 下的 hover 事件沿用 `pointer-hover-delivery.h` 的轉送方式。

### 視覺模式

GL viewport 的灰階、高對比濾鏡不會套用到這個圖層。根 QML 外層包一個 `MultiEffect`，其
`saturation` 與 `contrast` 與 `FitView::applyVisualMode` 同步。

### 錯誤處理與安全

- 元件載入失敗時，同一路徑只記錄一次警告，並略過該元件，不中斷對局。
- 引擎安裝一個拒絕所有請求的 network access manager factory；import 路徑只包含素材根目錄。
  擴展的 QML 不能連網，也不能從外部載入程式碼。

### 標記綁定的接入點

`PlayerCardContainer::updateMark`（`src/ui/generic-cardcontainer-ui.cpp`）遇到符合 `Engine`
登記表的標記時，不建立 `QPushButton` 代理，改為發出 `qmlMarkChanged(player, mark, value)`
交給圖層；值為 0 時移除元件。斷線重連與觀戰時收到的標記同樣經由此路徑。

## 直向與響應式版面

響應式版面（`Config.responsiveUiEnabled()`）在直向時採用 `Profile::CompactPortrait`：座位改為
`SeatPresentation::Ribbon` 帶狀排列並分頁，dashboard 改用 `setResponsiveGeometry` 重新排版，
另有 header、安全區與螢幕鍵盤造成的可用高度縮減。圖層依下列規則處理。

### 版面資料來源

`RoomScene` 每次套用 `RoomLayoutEngine::ResponsiveResult` 時，同步交給圖層（與
`RoomOverlayHost::setLayoutResult` 同一時點）。未啟用響應式版面時，圖層退回以 viewport 與座位
矩形計算。

### 座位可見性

直向分頁時，不在目前頁面的座位是以 `setOpacity(0.0)` 隱藏，`isVisible()` 仍為真。圖層判斷座位
是否可見時，必須同時檢查 `isVisible()` 與 `effectiveOpacity() > 0`。

### 座位元件縮放與精簡模式

- 座位元件依座位實際顯示尺寸縮放：以座位對映後矩形寬度除以座位原生寬度，作為容器的縮放倍率。
- `qs` 增加兩個欄位：`qs.profile`（`landscape`、`portrait`、`large-room` 等，對應
  `Profile`）與 `qs.compact`（帶狀座位時為真）。
- `qs.compact` 為真時，`top`、`bottom`、`mark-area` 三種座位錨點一律改為座位矩形內的底部列，
  並裁切在座位矩形之內，避免壓到相鄰座位或 header。元件可依 `qs.compact` 切換為精簡外觀。

### 自己的座位

自己座位的錨點矩形一律取 dashboard 的頭像區，而不是整個 dashboard。直向時 dashboard 橫跨全寬，
以整個 dashboard 為錨點會讓元件落在手牌上。

### 桌面與螢幕錨點

- `table-center` 取 `ResponsiveResult::tableCenter`。
- `screen-top` 取 header 下緣；`screen-bottom` 取 `interactionRect` 上緣，不得覆蓋手牌區與
  操作按鈕。
- 四個角落限定在 `mainRect` 之內，並扣除 `FitView` 的安全區邊距。
- 螢幕鍵盤彈出時，底部錨點依縮短後的可用區重新計算。

### 疊放順序

圖層位於 `RoomOverlayHost` 之下。直向開啟玩家檢視面板（inspector）或座位捲動列時，這些原生
控制項自然蓋在 QML 元件之上，不另做處理。

### 觸控

直向主要用於觸控裝置。視埠事件過濾器除滑鼠事件外，也處理觸控事件：觸點落在 `qsInteractive`
元件矩形內時交給圖層。觸控沒有懸停，擴展文件須提醒作者以 `TapHandler` 取代懸停互動，並讓可互動
範圍不小於 `ResponsiveInput::minimumTouchTarget`（48 邏輯像素）。

### 旋轉

直向、橫向互換時，版面重算會觸發 `QGraphicsScene::changed`，圖層據此重新定位，並推送新的
`qs.profile` 與 `qs.compact`。元件本身不重建，內部狀態（例如已展開）得以保留。

## 未決事項

- 觀戰者加入房間時的補送路徑，需於實作前確認。
