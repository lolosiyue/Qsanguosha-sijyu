# Protocol V2 TUI 客戶端

`qsanguosha_tui` 是正式的 live TCP 終端客戶端，入口為
`QCoreApplication`，只連結 Qt Core／Network 與無 GUI 的專案核心。

產品邊界固定如下：

```text
Replay: Permanently Unsupported
Protocol V1: Unsupported
GUI dependencies: Forbidden
```

它不建立 `QApplication`、MainWindow、RoomScene、Dashboard 或 QML engine，
也不載入圖片、音訊、動畫及 Replay。Gameplay legality 由 production server
決定；TUI 只接受 server 在 typed Protocol V2 request 明確提供的候選資料。

## 共用架構

```text
NativeClientSocket
  -> ClientLiveSession
       -> Protocol V2 decode / lifecycle / correlation
  -> ClientGameStateReducer
       -> ClientGameState
  -> TuiApplicationController
       -> TuiRenderer / TuiInput / TuiInteractionView
       -> ClientCore::submitInteractionResponse()
       -> ClientLiveSession -> typed V2 reply
```

Production GUI 同樣使用 `ClientLiveSession`、`ClientGameStateReducer`、
`ClientGameState`、`ClientCore` 及相同 reply encoder；`Client` 只把共用訊息轉成
既有 GUI signals。Replay 仍走獨立 Replay V2 playback 邊界，從不進入 TUI。

Room→Client 的機器可讀覆蓋表位於
[`artifacts/tui-flow-coverage.json`](../artifacts/tui-flow-coverage.json)：107 條
production flow 已逐條記錄 parser／DTO、reducer、affected state、renderer
visibility、reconnect behavior 及 focused test。當前 gate 為 63/63 個
state-bearing flow 有 reducer、29/29 個 interaction request 有 presenter、
unclassified=0、silent drops=0。音訊與動畫是已登記的 text-mode no-op；emotion
與 log 類流程會成為 presentation event。戰鬥日誌（`S_COMMAND_LOG_SKILL`）與
game event（`S_COMMAND_LOG_EVENT`）由 `src/tui/tui-log-text.cpp` 呼叫 Engine
`formatClientLog` 組句：`#UseCard` 片語走 `lang/zh_CN/Common.lua` 的
`#UseCardPhrase_*`，目標「自己」走 `#LogSelf`。摸牌／裝卸／體力等正常戰報大半
不是 `sendLog`：`src/tui/tui-synthesized-log.cpp` 在 reducer 之後用
`src/client/core/client-move-log.cpp` 合成 `$DrawCards`／`$addRenPile`／`$removeRenPile`／`#GetHp` 等，再以
`S_COMMAND_LOG_SKILL` 寫入 `presentationEvents`。互動 `prompt` 由
`src/client/core/client-prompt.cpp` 組句（與 GUI `Client::formatPromptList` 同一套
colon list：`key:%src:%dest:%arg:%arg2`）。出牌階段 ViewAs／SkillCard 清單與
`viewAs` 組線在 `src/tui/tui-play-skills.cpp`。仁區追蹤對齊 `RoomScene::RenPile`，
`GAME_START`／`STATE_SYNC begin` 清空。`event 9`
（`S_GAME_EVENT_UPDATE_SKILL`）仍不進戰報。core reducer 不把 GET_CARD 改成
presentation。分類異動後需以 `QSAN_TUI_COVERAGE_WRITE=1` 重跑
`qsanguosha_tui_contract_tests` 重生上述 artifact。

## 啟動及 CLI

```powershell
debug\qsanguosha_tui.exe --host 127.0.0.1 --port 9527 `
  --name terminal-player --avatar caocao --plain
```

| 選項 | 用途 |
|---|---|
| `--host <address>`／`--port <port>` | production server 位址；預設 `127.0.0.1:9527` |
| `--name <name>`／`--avatar <general>` | typed signup 資料 |
| `--reconnect` | 初次 signup 即請求接管既有玩家 |
| `--plain`／`--no-color` | 關閉 ANSI；非 TTY 或 `NO_COLOR` 亦自動關閉 |
| `--language <locale>` | 設定 Qt locale，TUI 固定文字預設簡體中文 |
| `--log-file <path>` | 寫入已清理的語意輸出，不 dump raw/private payload |
| `--script <path>` | 使用與真人相同的 input/controller pipeline |
| `--asset-root <directory>` | 明確指定 runtime data root |
| `--dump-translations <path>` | 初始化 Engine 後把翻譯表寫成 compact JSON 並結束；給 [`web-client.md`](web-client.md) 使用 |
| `--ui <classic\|board>` | 界面模式；不帶旗標且 stdin／stdout 皆為 TTY 時，啟動前詢問一次並可記住這次選擇（見下方「Board 模式」） |
| `--help`／`--version` | UTF-8 usage／版本輸出，不初始化 GUI |

固定 exit code：正常 `0`、CLI 使用錯誤 `2`、連線 `3`、Protocol `4`、
版本／signup 拒絕 `5`、本地 TUI runtime/input `6`、script `7`。

## 全域命令

```text
/help /status /players /hand /equip /piles /skills /log
/chat <text> /trust [on|off] /addrobot [all|count]
/surrender /reconnect /quit
/board <頁碼>
```

`/chat`、`/trust`、`/addrobot` 與 `/surrender` 先產生 typed intent，再由
controller 送到 live session；command parser 不建立 wire payload。Active prompt
期間只接受 `/cancel`、`/quit` 與唯讀查詢命令，避免 slash command 被誤認為
interaction answer。`/board` 只在 board 模式下可用，是純本地視圖指令（翻到指定
頁碼），不產生 wire 訊息，也不受上述「active prompt 期間只接受唯讀查詢」限制
影響作答（見下方「Board 模式」）。

## Interaction grammar

全部 29 個 production interaction 都走同一條路徑：typed request →
`ProtocolInteractionRequestBuilder` → canonical `InteractionRequest` →
`TuiInteractionView` → `ClientCore` exactly-once validation → production reply
encoder。`reply_to` 保留完整 `quint64`。

```text
單選／別名       2        或 rebel
多選／範圍       1 3 5    或 1-4
卡牌及目標       1 2 -> p1 p3
固定牌字串       card <Card::toString()> -> p1
技能牌           4 1 -> 2（出牌階段編號：技能＋子卡＋目標）
                 skill longdan#4: card 1 -> 2 3
重新排列         1 3 | 2 4
分配             cards 1 2 -> p1
角色分配         p1=lord p2=rebel
取消             /cancel
布林             yes / no
structured custom  一個 JSON value
```

Prompt 顯示的 index snapshot 是唯一數字映射；沒有 authorized candidate list 時
不得輸入任意 card ID。Duplicate、out-of-range、disabled、數量錯誤、stale、
expired 與第二次 Enter 都會在送上線前被 parser／ClientCore 拒絕。Unknown custom
type 或 legacy `qml_path` 會 fail closed，不讀取 QML path。

牌名、武將、勢力、模式及技能顯示使用 Engine translation；wire card text 另以
精確 `Card::toString()` 保留，顯示翻譯不會改動 protocol response。所有固定文字
經 `QCoreApplication::translate("QSanguoshaTui", ...)`，缺翻譯時安全退回簡體中文
source text／object name。

## State、輸入與安全

`ClientGameState` 保存 connection/setup、玩家 public/private presentation、卡牌
owner/place/pile、技能 instance、marks、距離、牌堆、回合階段、temporary selection、
log 與 game-over。Renderer 只列出自己已知手牌、公開區域與 server 已授權資料；
不顯示其他玩家未知手牌、未公開身分／武將或 private pile。

stdin 由 Windows waitable console handle 或 Unix `QSocketNotifier` 非同步讀取，不在
Qt event loop 執行 blocking `getline()`，也不由 worker thread 修改 state。EOF、
Ctrl+C 及 `/quit` 都會取消 active interaction、graceful disconnect，並恢復 Windows
console mode 與 UTF-8 前的 code page。

兩平台現在共用同一條 `TuiInput::interruptRequested` 路徑：Windows 由 console 分支
送出，Unix 由 `tuiInstallInterruptHandler()` 安裝的 SIGINT handler 送出——board 模式
下這個 handler 與 `TuiTerminal::enter()` 的終端還原共用同一個 `sigaction(SIGINT, ...)`
安裝點，兩者裝哪個都不會使另一個失效（見下方「Board 模式」）。這是修正過的行為：
在此之前 `interruptRequested` 只有 Windows console 分支會 emit，Linux 上完全沒有
對應處理，Ctrl+C 走的是預設 SIGINT 行為，直接終止行程，不會 graceful disconnect。
Server／chat／username 輸出的 ANSI escape 與 control character 會清理；command
4096 字元、chat 1000 字元，renderer/log 亦有長度上限。

## Board 模式

`--ui board`（或啟動時詢問後選擇 board）把輸出換成全螢幕 ASCII 牌桌，取代 classic
的逐行輸出；兩者共用同一套 `lineReady(QString)` 輸入出口、`ClientCore` 與 reply
encoder，parser 不知道自己在哪個模式（見上方「共用架構」與
[`tui-board-ui.md`](tui-board-ui.md) §1、§2）。畫面分四塊：房間區（座位環，開局前
顯示等待室：伺服器、模式、人數進度、已加入玩家）、戰報／訊息欄、手牌欄、輸入欄
（提示行＋一行行編輯，支援 ←→／Tab／↑↓）。

座位放不下時走容量分頁而非降級列表：房間區標題顯示 `‹頁碼/總頁數›`，`PgUp`／
`PgDn` 或 `/board <頁碼>` 翻頁；回合轉換或互動請求彈出時自動翻到相關頁，手動翻頁
只壓住自動跟隨到下一次回合轉換或下一個請求為止。`/players`、`/log`、`/hand` 等
長內容改走全螢幕 overlay（而非把牌桌擠出畫面），內容即 classic 排版本身，
`Esc`／`q`／空格關閉，方向鍵／`PgUp`／`PgDn` 捲動。

終端尺寸下限 `60×18`；小於此值不強行繪製，顯示提示訊息並持續接收訊息與正常作答，
放大到足夠即恢復繪製（詳見 `tui-board-ui.md` §3.5）。

**Windows 上暫不提供 board 模式**：`TuiTerminal` 在 Windows 上只有安全的空實作
（raw mode／alternate screen 皆未實作），因此明確指定 `--ui board` 會以 exit code
`2` 拒絕並說明原因，未指定 `--ui` 的自動判斷路徑則靜默退回 classic。這不是暫時的
潤飾缺口，而是需要另外設計 Windows console API 對應行為的後續工作。

**證據紀律**：board 模式在 CI 完全無法執行（CI runner 沒有穩定的 pty），因此
「board 可用」這個結論只由本機 `tools/autotest/tui_board_smoke.py`（raw mode／
`SIGWINCH`／終端還原）與 `tests/tui/*-test.cpp` 的 golden test 支撐；CI 綠燈不得
被當成 board 模式本身可用的證明（見 `tui-board-ui.md` §7.5）。

## Reconnect

每個 TCP connection 都有 generation。`/reconnect` 先關閉舊 socket，清除舊
outbound queue，令舊 callback/timer 失效，並只取消一次 pending interaction；舊
reply、chat、ready 或 terminal input不會重送。新 generation 重新 signup。

Server 以 `STATE_SYNC begin/end` 包住 reconnect marshal；live session 將通知 reduce
到 staging state，匹配的 `end` 才原子替換 live `ClientGameState` 並向 GUI/TUI
發布。若先前已開 trust，session active 後會用 typed control 恢復 trust。

## Script automation

普通 script 行直接送進 `TuiApplicationController::handleInputLine()`，因此不會跳過
input parser、state reducer、ClientCore 或 live TCP。同步 directive：

```text
wait active [timeout_ms]
wait interaction <type> [timeout_ms]
wait game_started [timeout_ms]
wait sync_complete [timeout_ms]
wait game_over [timeout_ms]
wait player <object_name> [timeout_ms]
wait state <dot.path> <expected> [timeout_ms]
wait log <visible text> [timeout_ms]
assert state <dot.path> <expected>
assert log <visible text>
assert <同一個可見 condition>
```

`state` 路徑從 deterministic `ClientGameState` snapshot 根節點開始，例如
`assert state game.game_over true`。`log` 搜尋最近 200 筆已分類的語意事件；兩者都只讀
Client 已接收的可見狀態，不會讀取 server private state。空行與 `#` comment 會略過；
逾時／assert 失敗以 exit code 7 結束。

## Build、dependency 與 deployment

```powershell
cmake --build --preset debug --target qsanguosha_tui --parallel 8
cmake --build --preset deploy-tui-debug
```

Windows package 位於 `dist/tui/<Configuration>`，只含 executable、Qt
Core／Network runtime、必要 network plugins、Lua、extensions、translation、LICENSE
與 core runtime data。禁止 image、audio、video、QML、Replay、FMOD、Qt Gui／Widgets／
Quick／QML／Multimedia／OpenGL／WebSockets。Post-link 與 extracted-package smoke 都執行
dependency gate、`--help` 及 `--version`。Linux 以 `qsan_tui` component 安裝 binary，
共用既有 `qsan_data`。

## Validation gates

本機允許的短 gate：targeted Debug compile、直接執行 `qsanguosha_tui_contract_tests`、
`qsanguosha_tui_live_tcp_tests`、Windows `dumpbin` dependency gate、`deploy-tui`
package smoke、`git diff --check`。本機不執行 CTest 或長時間 gameplay。

短時 real-TCP connection smoke 可加 `--connection-only`；它只證明 production
server／TUI 的 connect、signup、setup、ready 與 clean exit，不會被列為 full-game
證據。Runner 的子行程都建立獨立 process group：Windows 以 `CTRL_BREAK_EVENT`、
Unix 以 `SIGTERM` 要求 server 走 Qt event loop 的 graceful shutdown，逾時後才強制
清理，並要求無 orphan 且監聽 port 已釋放。

Remote CI 已註冊 Windows RelWithDebInfo product/package/focused contract、Linux GCC／
Clang build、Linux dependency/install、production server + production TUI 的固定 seed
`03_1v2` full game，以及 mid-game disconnect → STATE_SYNC → continue → GAME_OVER。
完整對局使用只包含本倉庫 `lua`／`lang` 與空白 `extensions` 的隔離 core-game runtime，
避免外部 optional extension 的語法錯誤污染 TUI acceptance；AI 仍使用 CI 當次抓取的
`lua/ai`，summary 會記錄該 extensions commit 與 server／TUI SHA-256。
`--log-file` 另外寫入不含 private payload 的穩定 `[TUI_EVENT] GAME_OVER` 與
`[TUI_EVENT] STATE_SYNC_COMMITTED`，讓 automation 不依賴任何顯示語言；這些 marker
不寫到真人 terminal 畫面。
以下命令屬 remote-only；未取得 CI 結果前不能視為完成證據：

```bash
python3 tools/autotest/tui_network_smoke.py --exe-root . --mode 03_1v2 \
  --seed 20260831 --artifact-dir ci-logs/tui-real-complete
python3 tools/autotest/tui_network_smoke.py --exe-root . --mode 03_1v2 \
  --seed 20260831 --reconnect --artifact-dir ci-logs/tui-real-reconnect
```

目前剩餘驗收風險是尚未實際取得 Linux build/dependency、production deterministic
GAME_OVER、mid-game reconnect GAME_OVER 與完整 GUI remote regression 結果；fake
socket、coverage artifact 或 waiting-room connect 皆不能取代這些證據。
