# TUI 牌桌模式（board UI）設計

`qsanguosha_tui` 現時只有一套滾動行輸出。本文件規格化第二套呈現層：全螢幕
ASCII 牌桌（board），與現有行模式（classic）並存，由啟動時決定。

日期：2026-09-06

## 0. 決策摘要

| 項目 | 決定 |
|---|---|
| 畫面模型 | 全螢幕重繪 |
| 渲染架構 | 幀緩衝 + 差分輸出（cell grid，diff 後才發 ANSI） |
| 互動方式 | 保留現有打字 grammar；只新增行編輯（←→ / Tab / ↑↓） |
| 字元集 | 全程 Unicode 畫框 |
| 資訊範圍 | 只把現有資料上屏，不新增推導資訊（不加距離／攻擊範圍） |
| 兩套 UI | classic 與 board 並存，classic 為一等公民，非 fallback |
| 模式選擇 | `--ui classic\|board`；無旗標且為 TTY 時啟動前詢問一次，可記住 |
| 人數過多 | 容量分頁，非降級列表 |

明確不做（YAGNI）：熱鍵游標選牌、滑鼠、局中切換 UI 模式、主題／配色設定、
board 模式專屬的新指令（`/board` 除外）。

## 1. 不變式

以下三條是本設計的硬性約束，任何實作細節不得違反：

1. **視圖操作永不觸及 wire。** 翻頁、overlay、捲動、resize、重繪全部是本地
   視圖狀態，不產生、不延遲、不重排任何 protocol 訊息。
2. **`lineReady(QString)` 是唯一輸入出口。** parser、`ClientCore`、reply
   encoder 三層不知道 UI 模式存在，兩套 UI 交出的 `InteractionResponse`
   必須逐欄相同。
3. **classic 行為零改動。** 除下列兩項刻意的例外，classic 模式的輸出與現有
   實作逐位元組相同：一是 §6.2 明列的 Linux Ctrl+C 修正（原本完全沒有
   graceful disconnect，屬修正而非退化）；二是 `/board` 指令本身的加入——它
   出現在 classic 的 `/help` 文字、Tab 補全清單與（board 模式專屬指令在
   classic 下被拒絕時的）錯誤訊息路徑中，這是 classic 為了讓玩家知道 board
   模式存在而必須承載的最小接觸面，不是又一個未列出的偏差。

不變式 1 與 2 由 §7.2 的 parity 測試釘死，不依賴 code review。

## 2. 分層與模組邊界

在 controller 與輸出之間插入 `TuiPresenter` 介面，兩套 UI 各實作一次。
`TuiApplicationController` 現有 21 個 `writeOutput()` 呼叫點、全部 slot 接線與
`--script` 路徑不需改動。

```text
TuiApplicationController
  └── TuiPresenter *              啟動時決定，controller 不知道自己在哪個模式
        ├── TuiStreamPresenter    classic：包住現有 TuiRenderer + QTextStream(stdout)
        └── TuiBoardPresenter     board
              ├── TuiTerminal     raw mode / alternate screen / 尺寸 / SIGWINCH / 還原
              ├── TuiScreen       cell grid 幀緩衝 + diff → 最小 ANSI
              ├── TuiBoardLayout  純函數：(rows, cols, 玩家數) → pane rect + 座位幾何
              └── TuiBoardView    讀 ClientGameState 畫入 TuiScreen
```

### 2.1 新增檔案（全部 `src/tui/`）

| 檔案 | 職責 | 依賴 |
|---|---|---|
| `tui-presenter.h` | 抽象介面 | 無 |
| `tui-stream-presenter.{h,cpp}` | classic，現有行為搬入 | `TuiRenderer` |
| `tui-text-width.{h,cpp}` | East Asian Width、`displayWidth()`、`elide()` | 無 |
| `tui-screen.{h,cpp}` | grid、`putText`／`drawBox`、diff、`toPlainText()` | `tui-text-width` |
| `tui-board-layout.{h,cpp}` | 佈局與分頁計算，純函數，不碰 IO | 無 |
| `tui-board-view.{h,cpp}` | 遊戲狀態 → 畫面 | `TuiScreen`、resolvers |
| `tui-board-presenter.{h,cpp}` | 重繪排程、overlay、翻頁狀態 | 以上全部 |
| `tui-terminal.{h,cpp}` | 終端狀態進入與還原 | 平台 API |
| `tui-line-editor.{h,cpp}` | 行編輯，出口為 `lineReady(QString)` | `tui-text-width` |

### 2.2 對既有檔案的兩項改動

1. `TuiRenderer::Resolvers`（card / name / player / kingdom / cardHint /
   playerHint / cardTargets / handHint / skillHint）抽出至獨立
   `src/tui/tui-resolvers.h`。兩套 UI 共用同一組 engine 查詢。
   `TuiBoardView` **只重用 resolvers，不重用 `TuiRenderer` 的排版字串** ——
   它需要結構化資料，而非 `"武将=%1 势力=%2"`。
2. `TuiInput` 拆成「位元組來源」與「行組裝」兩截。classic 維持現有 cooked
   行為；board 先進 raw mode，再由 `TuiLineEditor` 組行。兩者共用同一個
   `lineReady` 出口。

### 2.3 重繪觸發

接現有 `ClientLiveSession::stateChanged`（`tui-application-controller.cpp:122`），
合併到下一個 event loop turn 才畫一次，避免單一訊息引發多次全畫面重算。

### 2.4 模式記憶

`QSettings` 寫入 `QStandardPaths::AppConfigLocation` 下的 ini。倉庫現時
TUI／client 完全沒有 `QSettings`，此為第一處。`QSettings` 與 `QStandardPaths`
均屬 Qt Core，不影響依賴閘。

## 3. 佈局

### 3.1 座位幾何

沿用桌面版座位表。`RoomScene::updateTable()`（`src/ui/roomscene.cpp:1771`）的
`s_regularSeatIndex` 按人數把每個座位派往一個區域，座位順序由自己的下家起算。
方位以 `roomscene.cpp:1848` 的佈局圖為準：

```text
// |_2_|______1_______|_0_| row1
// | 4 |    table     | 3 |
// region 5 = 0+3，region 6 = 2+4，region 7 = 0+1+2
```

即 **1／7 = 上排，3／5 = 右欄，4／6 = 左欄**。board 將其歸約為三區
（右欄／上排／左欄），自己固定在底部。

方位不可鏡像。`s_regularSeatIndex` 的每一列首項都是右側區域（例如 4 人局
`{3,1,1,4}`），意即下家在桌面版坐玩家右手邊；若 board 擺去左邊，「識 GUI 的人
一眼認得出誰是下家」這個本節存在的理由就沒有了。

每個玩家格固定 `20 × 3` 字元。房間區窄到放不下三欄時，環會依 §3.6 的階梯退化，
座位順序不變。

```text
┌ 房间 05p 轮次3 ────────────────────┬ 战报 ──────────────┐
│    [3]张飞     蜀 ♥♥♥♥             │ 时语 对 孙权       │
│    主公 手4 装2 判【乐】           │   使用【杀】       │
│                                    │ 孙权 打出【闪】    │
│ [2]曹操        [4]貂蝉             │ 张飞 摸了 2 张牌   │
│ 魏 ♥♥♥♡ 手7    群 ♥♥♡ 手2          │ ...                │
│ 装【护心镜】   ✖阵亡               │                    │
│                                    │                    │
│         牌堆 42   弃牌 17          │                    │
│                                    │                    │
│  ▶[1]时语(我)  蜀 ♥♥♥♡             │                    │
│    主公 手5 装【青釭剑】【八卦阵】 │                    │
├ 手牌 ──────────────────────────────┴────────────────────┤
│ [1]杀♠7  [2]闪♥2  [3]桃♥Q  [4]无懈♣3  [5]顺手牵羊♦6     │
├─────────────────────────────────────────────────────────┤
│ 出牌阶段 > 1 -> p2▌                                      │
└─────────────────────────────────────────────────────────┘
```

### 3.2 分欄規則

全部在 `TuiBoardLayout` 內計算，不碰 IO。

| 區 | 寬 | 高 |
|---|---|---|
| 戰報／訊息 | `clamp(cols × 0.3, 22, 34)` | 同房間區 |
| 房間 | 餘下 | 餘下，最少 9 |
| 手牌 | 全寬 | 需要行數，夾在 1–5；超出加 `…(+N)` |
| 輸入 | 全寬 | 1 行提示 + 1 行輸入 |

### 3.3 未開局時的房間區

`GAME_START` 之前沒有座位可排。房間區改為顯示等待室內容：伺服器名、模式、
人數進度（`3/5`）、已加入玩家與其就緒狀態。選將等開局前的互動照 §5.2 走
overlay 或提示行，不需要牌桌。座位環在收到 `GAME_START` 後才首次繪製。

### 3.4 血量表示

`♥♥♥♡` 顯示 hp/maxHp。maxHp 超過 8（界限突破等）改用數字式 `♥ 9/12`，
不撐爆格。

### 3.5 尺寸下限

下限 `60 × 18`。小於此值不強行繪製，改顯示
`终端太小(需 60×18,当前 52×14),请放大窗口或用 --ui classic 启动`。

此情形**不是錯誤**：程式繼續運行、繼續接收訊息，放大到足夠即恢復繪製。
互動請求仍可作答；提示行與輸入行是最後才犧牲的兩行。

### 3.6 容量分頁

塞不下的原因是容量，不是人數：9 人局在 80×24 一樣塞不下。因此不存在
「人數過多」的特例分支。

`TuiBoardLayout` 由房間區 rect 得出可容納的玩家格數：

```text
cellCols = floor(roomWidth  / 20)
cellRows = floor((roomHeight - 3) / 3)      // 減去自己那格佔的 3 行
capacity = cellCols × cellRows − (中央牌堆／棄牌那格)
```

自己的格永遠佔住底部、不進分頁。其餘 `n − 1` 個對手按座位順序（由下家起算）
切頁。房間區標題顯示頁碼 `‹2/2›`；單頁時不顯示。

**欄數退化階梯。** §3.1 的三區環需要 3 欄才畫得出；`cellCols` 不足時環會退化，
但座位順序（由下家起算、逆時針）在每一階都保持不變：

| `cellCols` | 佈局 |
|---|---|
| ≥ 3 | 完整三區環：左欄／上排／右欄，同 GUI 檯形 |
| 2 | 去掉上排，只剩左右兩欄，由下家起左右交替填 |
| 1 | 單欄垂直堆疊，由下家起由上而下 |

因此 60×18（下限尺寸）得出 `cellCols = 1`、`capacity ≈ 2`，5 人局分成 2 頁 ——
窄終端與多人局走的是同一條分頁路徑，沒有第二套邏輯。

**翻頁**：`PgUp`／`PgDn`，另加 `/board <頁碼>` 供無 PgUp 的終端與 script 使用。

**自動跟隨**（分頁最大的風險是「要看的人不在當前頁」）：

1. 回合轉換時，自動翻到當前玩家所在的頁。
2. 互動請求彈出時，自動翻到第一個候選目標所在的頁。

手動翻頁壓住自動跟隨，但只壓到下一次回合轉換或下一個請求為止。

**分頁永遠不會是「答不到題」的原因**：候選目標清單由 `TuiInteractionView`
完整列出（含 `p3`、玩家名），與分頁無關；輸入 `-> p7` 不需要 p7 在畫面上。
分頁只影響「看」，不影響「答」。

`20p` 因此不是特例，與 80×24 開 9 人局走同一條路徑。

### 3.7 色彩

board 只在 ANSI 可用時才啟動（`--plain`／`NO_COLOR` 強制 classic，見 §6.1），
因此色彩可無條件使用，但只承載**已在文字中表達過的資訊**，不作為唯一載體：
勢力著色、當前玩家高亮、瀕死與陣亡標色、自己那格加框。任一格在去色後仍須
可讀，golden test 比對的是去色後的純文字（`toPlainText()`）。

### 3.8 Resize

Unix `SIGWINCH` 經 self-pipe 轉為 Qt signal（不在 signal handler 內做事）；
Windows 輪詢 `GetConsoleScreenBufferInfo`。收到即重建 grid 並強制全畫一次；
`TuiScreen` 保存幀，resize 後第一幀為全量，之後回到 diff。

## 4. 終端狀態

### 4.1 進得去，一定出得返

`TuiTerminal` 為 RAII。`enter()` 做三件事：`tcsetattr` 關閉 `ICANON|ECHO`
（**保留 `ISIG`**）、進入 alternate screen（`ESC[?1049h`）、隱藏游標。還原序列
在 `enter()` 成功當刻預先組成 `static char[]`，因為它要在 signal handler 內使用。

還原掛在四條路徑上：

| 路徑 | 機制 |
|---|---|
| 正常結束 | 解構函式／`QCoreApplication::aboutToQuit` |
| Ctrl+C、SIGTERM、SIGHUP | handler 僅做 async-signal-safe 的 `write(STDOUT, 還原序列)` 並設 flag，再經 self-pipe 喚醒 event loop 做 graceful disconnect |
| 崩潰（SIGSEGV／SIGABRT） | handler `write()` 還原序列後 `signal(SIG_DFL); raise()`；不嘗試繼續，只保證終端不廢 |
| `_exit` 或 `kill -9` | 無法處理，為已知且接受的缺口 |

### 4.2 順帶修正的既有缺口

`interruptRequested` 目前只由 Windows console 分支發出（`tui-input.cpp:155`），
**Unix 側沒有任何 SIGINT 處理**：今日在 Linux 按 Ctrl+C 由終端驅動送 SIGINT、
行程直接死，沒有 graceful disconnect。`docs/tui-client.md` 現行敘述
「EOF、Ctrl+C 及 `/quit` 都會取消 active interaction、graceful disconnect」
對 Linux 而言不成立。

保留 `ISIG` 並補上 handler 後，兩個平台共用同一個 `interruptRequested` 出口。
**這是本設計唯一會改變 classic 既有輸出/行為的修正型改動**（`/board` 指令的
加入是另一項刻意的例外，但那是新增一個入口，不是改動既有路徑的行為——見 §1
不變式 3），且是修正而非退化；`docs/tui-client.md` 該段須同步更新。

## 5. 輸入

### 5.1 行編輯器

`TuiLineEditor` 吃按鍵、出行，唯一出口為現有的 `lineReady(QString)`。

- 插入 / ←→ / Home / End / Backspace / Delete
- ↑↓ 歷史，只存記憶體，不落磁碟
- Tab 補全，直接重用現有 `m_completer`（Linux 因此首次獲得補全）
- Ctrl+A / E / U / K / W
- **Esc 清空當前行，不取消互動。** 取消一律靠 `/cancel`；一個誤觸按鍵不應
  產生 wire 效果
- 游標依顯示寬度移動（`tui-text-width`），中文名不會走錯格
- 長度上限 16384，與現行一致

escape sequence parser 必須處理**跨 read 斷開的序列**（`ESC [` 與 `D` 分兩次
抵達是常態），因此是帶未完成緩衝的狀態機，不是字串比對。

### 5.2 board 模式下的文字輸出

`TuiBoardPresenter::writeOutput()` 分三路：

1. **短訊息／戰報／命令回饋** → 右側 pane 的 scrollback。戰報 pane 同時是
   訊息 pane，不另開區域。
2. **長 dump**（`/players` `/log` `/hand` `/skills` `/piles` `/equip`）→
   全螢幕 overlay，↑↓／PgUp／PgDn 捲動。**overlay 內容直接由現有
   `TuiRenderer` 產生**，classic 的排版原封不動，不為 board 重寫一次。
3. **錯誤**（`writeError`）→ 輸入區上方提示行，下次輸入時清除；同時進
   scrollback，不會一閃即逝。

Overlay 關閉：`Esc`／`q`／空白鍵關閉並吞掉該鍵；其他可打印字元關閉 overlay
**並送入行編輯器**，不吃掉使用者打的第一個字。

## 6. 模式決議與相容性

### 6.1 決議表（由上而下，第一個命中即決定）

| 條件 | 結果 |
|---|---|
| 有 `--script` | classic，強制，不詢問 |
| stdin 或 stdout 非 TTY | classic，強制，不詢問 |
| `--plain`／`--no-color`／`NO_COLOR` | classic，強制，不詢問 |
| 以上任一命中，且明寫 `--ui board` | **錯誤，exit 2**，說明衝突條件 |
| `--ui classic`／`--ui board` | 照跟 |
| 無 `--ui`，TTY，`QSettings` 有記錄 | 用記錄 |
| 無 `--ui`，TTY，無記錄 | 連線前詢問一次，可選記住 |

第四列為刻意設計：`--ui board --plain` 報錯而非靜默降級。靜默降級會令
「明明指定了 board 為何沒有牌桌」變成需要 debug 的問題；自動路徑（無 `--ui`）
才容許無聲退回，因為當下使用者未表達意圖。

啟動詢問僅在 stdin 為 TTY 時出現，且在 `connectToHost` 之前完成，不會出現
「已連線但停在選單」的狀態。

### 6.2 平台

**Windows**：除現有 UTF-8 code page 切換外，board 另需
`ENABLE_VIRTUAL_TERMINAL_PROCESSING`。設不到（舊 conhost）即 board 不可能運行：
明寫 `--ui board` 則 exit 2 並說明；自動／記憶路徑退回 classic 並印一行說明。
所有 console mode 改動依現行做法在結束時還原。

**Unix**：`termios`、`TIOCGWINSZ`、`SIGWINCH`，全部 POSIX，無新依賴。

### 6.3 不受影響的既有契約

- **依賴閘**：`QSettings`／`QStandardPaths` 屬 Qt Core；
  `cmake/VerifyTuiDependencies.cmake` 禁用清單與 Windows dumpbin gate 不需改動。
- **打包**：`DeployTui.cmake` 與 `dist/tui` 清單零改動；無新 runtime 檔案，
  ini 於執行期在使用者 config 目錄生成。
- **自動化**：`tools/autotest/tui_network_smoke.py` 使用 `--plain --script`，
  兩個條件皆命中 classic，行為與今日逐位元組相同。`--log-file` 的
  `[TUI_EVENT] GAME_OVER` 與 `STATE_SYNC_COMMITTED` marker 不受影響。
- **退出碼**：沿用現有分配，不新增。終端初始化失敗歸 `6`（本地 TUI
  runtime/input），`--ui` 用法錯誤歸 `2`。

## 7. 測試策略

### 7.1 新增單元／contract 測試

接上現有 `tests/tui/` 的七個檔案，加入同一測試目標。

| 檔案 | 釘住的行為 |
|---|---|
| `tui-text-width-test.cpp` | CJK 全形寬度、組合字元、`elide()` 邊界；尤其**永不從中間劈開一個全形字** |
| `tui-screen-test.cpp` | `putText` 裁切、畫框、`toPlainText()` golden；**diff 最小性**：未變更的 cell 必須產生零位元組輸出 |
| `tui-board-layout-test.cpp` | 60×18／80×24／120×40 的 pane rect、capacity、2–10 及 20 人的分頁切法、座位→區域映射與 `s_regularSeatIndex` 一致 |
| `tui-line-editor-test.cpp` | 跨 read 斷開的 escape sequence、CJK 游標、歷史、Tab 補全、Esc 清行、16384 上限 |
| `tui-board-view-test.cpp` | 由 `ClientGameState` fixture 出發的整幅畫面 golden |
| `tui-ui-parity-test.cpp` | §1 不變式 1 與 2 |

Golden 檔置於 `tests/tui/golden/*.txt`，以 `QSAN_TUI_GOLDEN_WRITE=1` 重生，
沿用現有 `QSAN_TUI_COVERAGE_WRITE=1` 的做法。

### 7.2 Parity 測試即驗收閘

`tui-ui-parity-test` 對 `artifacts/tui-flow-coverage.json` 中有 presenter 的
**29 個 interaction request** 逐一執行 classic／board 對照，board 側額外插入
翻頁、開關 overlay、resize 事件，斷言交給 `ClientCore` 的
`InteractionResponse` 逐欄相同。任何一條不同即紅燈。

這是不變式 1 與 2 唯一站得住的證據。

### 7.3 需要真 pty 的部分

raw mode、`SIGWINCH`、alternate screen 還原無法以單元測試覆蓋。
`tools/autotest/tui_board_smoke.py` 開 pty 執行 client，依序：進入 alternate
screen → resize 兩次 → 送 SIGINT → 斷言還原序列確實寫出且 termios 已復原。

**此為本機閘，不入 CI**：CI runner 無穩定 pty，納入只會製造間歇紅燈。定位與
`--network-ui-smoke` 相同。

### 7.4 必須維持綠燈且不應修改的既有閘

`qsanguosha_tui_contract_tests`、`qsanguosha_tui_live_tcp_tests`、七個現有
`tests/tui/*-test.cpp`、Windows dumpbin 依賴閘、`deploy-tui` package smoke、
`tui_network_smoke.py` 的 `03_1v2` 完整對局與 reconnect。

若其中任何一個需要修改才能通過，即代表抽象有漏，應返回修正設計，
不得修改測試遷就實作。CI 僅新增編譯目標，不新增對局。

### 7.5 驗收證據紀律

board 模式在 CI 完全無法執行（無 pty）。「board 可用」此一結論只能由本機 pty
smoke 與 golden test 支撐，**不得以 CI 綠燈冒充**。

## 8. 文件更新

實作完成後須同步：

- `docs/tui-client.md`：新增 `--ui` 選項列、board 模式章節、`/board` 指令，
  並修正 §4.2 指出的 Ctrl+C 敘述。
- `docs/tui-client-architecture.md`：加入 `TuiPresenter` 分層。

## 9. 實作階段

每一階段各自可獨立落地並保持全綠，不留半完成狀態。

| 階段 | 內容 | 完成條件 |
|---|---|---|
| P1 | `TuiPresenter` 抽象 + `TuiStreamPresenter`；`Resolvers` 抽出 | 現有七個 TUI 測試與 `tui_network_smoke.py` 全綠，輸出逐位元組不變 |
| P2 | `tui-text-width` + `TuiScreen` | `tui-text-width-test`、`tui-screen-test` 通過，含 diff 最小性 |
| P3 | `TuiTerminal` 與 §4.2 的 SIGINT 修正 | pty smoke 的還原斷言通過；classic 在兩平台共用 `interruptRequested` |
| P4 | `TuiLineEditor` | `tui-line-editor-test` 通過，含跨 read 斷開的序列 |
| P5 | `TuiBoardLayout` 與分頁 | `tui-board-layout-test` 涵蓋 2–10、20 人與三個尺寸 |
| P6 | `TuiBoardView`、`TuiBoardPresenter`、overlay | `tui-board-view-test` golden 通過 |
| P7 | `--ui` 決議、`QSettings`、啟動詢問 | §6.1 決議表逐列有測試 |
| P8 | 全量 parity 測試、pty smoke、文件更新 | §7.2 的 29 條全綠；§8 文件已改 |

P1 是唯一會碰到 classic 程式碼路徑的階段，因此它的驗收標準最嚴：輸出必須
逐位元組不變。P3 是例外中的例外——它刻意改變 Linux 上 Ctrl+C 的行為，屬 §4.2
所述的修正。
