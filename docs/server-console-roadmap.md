# Server 執行期主控台路線圖（Server Console Roadmap）

> 版本：2026-09-07 ｜ 狀態：計劃（未排程）
> 範圍：`qsanguosha_server`（Qt 6 x64）執行期 console 的功能補齊，以及 XP legacy 產品 `-server` 帶局模式的 console 接入。
> 不動範圍：Protocol V1/V2 wire、SWIG 綁定、`executeCommand()`（現約 `server-console.cpp:353`）既有指令層語意。

---

## 1. 現況基線

| 項目 | 現況 |
|------|------|
| 指令集 | `help`／`status`／`players`／`rooms`／`say <msg>`／`kick <id>`／`shutdown` 共 7 個 |
| 指令層 | `ServerConsole`（`server-console.h:41`），指令在主執行緒執行，`m_handlingCommand` 維護 prompt 語意 |
| 輸入架構 | Unix：`QSocketNotifier` 非阻塞讀（現約 `server-console.cpp:216`）；Windows：`ConsoleInputThread`（`server-console.h:18`）worker 逐行讀取後 queued 派發（2026-09-07 `0ec8fa2`） |
| 互動判定 | `_isatty`／`isatty`；piped 時不印 banner／prompt、log 不重複回顯 |
| 中文輸入 | TTY 走 `ReadConsoleW` 取 UTF-16，不受 codepage 影響；pipe 假設 UTF-8 |
| 驗收基準 | `tools/ci/server-console-smoke.sh`：七指令 piped 逐行執行、`shutdown` 後 `exit_code=0` |
| 平台覆蓋 | Linux／macOS（Unix 路徑）與 Windows x64（Qt 6）已覆蓋；XP legacy 產品帶局模式無 console（見 §3） |

---

## 2. 缺口與功能計畫（按嚴重度排序）

| 嚴重度 | 層級 | 缺口 | 說明 | 狀態 |
|--------|------|------|------|------|
| 高 | 平台死區 | Windows 無法輸入指令 | stdin 讀取整段包在 `#if defined(Q_OS_UNIX)`，主要開發／部署平台 win32 上整組指令是半死碼 | **已完成**（`0ec8fa2`，piped 冒煙 PASS） |
| 高 | 控制 | 無房間層級處置 | `kick` 只能踢人；不能關閉指定房、強制結束進行中的局、清掉卡死的等待房。Server 亦無對應公開方法（`roomSnapshots()`（`server-core.h:49`）唯讀） | 計劃 |
| 中 | 控制 | 無維護模式 | 不能「停止收新玩家但不斷現有局」，只能 `shutdown` 一刀切 | 計劃 |
| 中 | 控制 | 無機器人／開局介入 | 等待房缺人時 console 無法補 robot 或強制開局；`setNextGameSessionConfig`（`server-core.h:58`）與 `startHeadlessGame`（`server-core.h:61`）已存在但未接 console | 計劃 |
| 中 | 觀測 | 無動態 log 調整 | log level／file／format 只能在啟動時給定，線上排查需重啟 | 計劃 |
| 低 | 自動化 | 輸出非機器可讀 | `status`／`rooms` 是對齊人眼的表格，pipe 腳本沒有 JSON 形式；自動化正路目前是 `--autotest-log` marker，console 不是自動化介面 | 計劃 |

### 2.1 房間層級處置（嚴重度：高）

| 項目 | 內容 |
|------|------|
| 新指令 | `close <room-id>`（解散等待房：斷開等待者並移除房）、`end-game <room-id>`（結束進行中的局）、可選 `close all` |
| 需要的路徑 | `Server` 新增公開房間處置方法；`Server` 目前只有唯讀 `roomSnapshots()` 與踢人 `kickPlayer`（`server-core.h:51`） |
| 執行緒邊界 | 房間與 `RoomThread` 在各自執行緒；console 指令在主執行緒。Server→Room 呼叫必須走 queued／`invokeMethod` 進房執行緒，禁止主執行緒直接改 `RoomThread` 狀態（既有 Room socket assert 教訓） |
| 結束局語意 | `end-game` 應復用正常收束路徑（`GameFinished`／`GAME_OVER` 廣播、replay 收尾），而非拆線棄局；abort 語意若需要另立指令並明確標示 |
| 驗證 | focused smoke（開一局→`end-game`→觀察 `GAME_OVER` 與房間清空）＋人工 GUI 對局中斷驗收 |

### 2.2 維護模式（嚴重度：中）

| 項目 | 內容 |
|------|------|
| 新指令 | `maintenance on`／`maintenance off`；`status` 顯示旗標 |
| 語意 | 開啟後：拒絕新 signup（沿用既有 Server 拒絕路徑，回語意化原因，而非 TCP 拒連）；已連線玩家、進行中局、機器人不受影響 |
| 取捨 | 照常 accept TCP 再在 signup 拒絕，讓 client 收到明確訊息；不關 listener |
| 驗證 | smoke：開維護後新連線被拒、既有房續玩不中斷 |

### 2.3 機器人／開局介入（嚴重度：中）

| 項目 | 內容 |
|------|------|
| 新指令 | `addrobot [n\|all]`（等待房補 AI）、`start`（強制開局） |
| 既有能力 | `setNextGameSessionConfig`（server.cpp:2089）、`startHeadlessGame`（server.cpp:2609）存在但僅服務 headless／自動化路徑，未接 console |
| 開局路徑 | 機器人補滿後由 robot `signup` 的 ready 路徑自然開局（與 GUI `fillRobots()`／TUI `/addrobot` 同一機制），console 不另送 `READY` |
| 邊界 | 僅等待房可介入；進行中的局不在本指令範圍（由 §2.1 `end-game` 處理） |
| 驗證 | smoke：等待房 `addrobot all` → 觀察 `GAME_START` marker |

### 2.4 動態 log 調整（嚴重度：中）

| 項目 | 內容 |
|------|------|
| 新指令 | `log-level <debug\|info\|warning\|error>`、`log-format <text\|json>`、`log-file <path\|off>` |
| 既有能力 | `parseServerLogLevel`／`parseServerLogFormat`（`server-logger.h:33-34`）解析可重用；`ServerLogger::start`／`stop`（`server-logger.h:46-47`）可重建組態 |
| 邊界 | 切換 log file 時先 flush／close 舊檔；新組態失敗（開檔失敗等）保持原組態並回報，不得讓 logger 進入無輸出狀態 |
| 驗證 | smoke：線上調級後觀察輸出即時變化 |

### 2.5 機器可讀輸出（嚴重度：低）

| 項目 | 內容 |
|------|------|
| 新指令 | `status --json`／`rooms --json`／`players --json` |
| 定位 | console 仍是人工維運介面；自動化主路維持 `--autotest-log` marker。JSON 輸出僅供輕量 pipe 檢查，凜建議只承諾最小欄位集（對齊 `ServerStatusSnapshot`／`RoomStatusSnapshot` 已有欄位），不做穩定 schema 版本承諾 |
| 驗證 | smoke：`status --json` 經 `python -m json.tool` 解析成功 |

---

## 3. WinXP 32-bit server（XP legacy 產品補齊）

### 3.1 現況

| 項目 | 現況 |
|------|------|
| 產品形態 | 單一 `QSanguoshaXP.exe`，`-server` 帶局（見 `docs/windows-xp-legacy-build.md`） |
| 帶局路徑 | `xp-main.cpp` serverMode 分支（現約 `:109` 判定、`:139` 起執行）直接 `new Server`＋`listen()`＋`exec()`，**未實例化 `ServerConsole`**——XP 帶局目前沒有任何執行期 console |
| 建置閘 | `QSAN_BUILD_XP_LEGACY` 時 `QSAN_BUILD_SERVER_DEFAULT OFF`（現約 `CMakeLists.txt:44`）；`server-console.cpp` 只列在 `qsanguosha_server` target，故 `0ec8fa2` 不影響 XP client exe |
| 相容層 | `legacy/xp/compat/qt5/qsan-qt5-compat.h`（`/FI` 強制含入）已提供 `qsizetype`、`Qt::endl`；`QDeadlineTimer` shim 是 `hasExpired()` 輪詢計時器，**不是**可傳給 `QThread::wait()` 的時限物件 |

### 3.2 實作計畫

| 步驟 | 內容 | 層級 |
|------|------|------|
| 1. `CancelSynchronousIo` 動態化 | 該函式是 Vista+ 的 kernel32 匯出；**靜態 import 會令 `QSanguoshaXP.exe` 在 XP 載入期直接「入口點找不到」**。改 `GetProcAddress` 動態解析（`stopInputThread`，現約 `server-console.cpp:306`）；XP 取不到就跳過 cancel、直接洩漏 detach——與現行逾時路徑同一後果，process 結束時 OS 收掉 worker。`OpenThread` 為 XP 既有匯出，可靜態使用 | 致命（載入期），必改 |
| 2. `wait()` 版本分流 | `wait(QDeadlineTimer(2000))` 在 Qt 5.6 編譯不過（只有 `wait(unsigned long)`）。以 `QT_VERSION` 分流：Qt5→`wait(2000)`；Qt6 維持 `QDeadlineTimer` 寫法 | 編譯期，必改 |
| 3. 來源接線 | `server-console.cpp`／`.h` 加入 XP exe 來源清單（嵌入式 console，不新增獨立 XP server target，維持單一 exe 產品邊界） | 建置 |
| 4. `xp-main.cpp` 實例化 | serverMode 分支建立 `ServerConsole` 並 `start()`；對齊 `server-main.cpp` 慣例：`SetConsoleCtrlHandler`（xp-main 現未安裝）與 `Server::isHeadlessMode`（`server-core.h:39`，xp-main 現未設定） | 接線 |
| 5. API 面檢查 | `ReadConsoleW`／`ReadFile`／`_isatty`／`GetStdHandle`／`GetCurrentThreadId`／`std::atomic` 均為 XP SP3 x86 可用；程式碼無 64-bit 假設（`DWORD`／`HANDLE`／atomic 在 win32 皆 32-bit）；4 KiB wchar 分段緩衝在預設 1 MB stack 內 | 已核可 |

### 3.3 驗證

| 項目 | 方式 |
|------|------|
| piped | XP SP3 VM 內 `echo status \| QSanguoshaXP.exe -server` 逐行執行、`shutdown` 乾淨退出 |
| TTY | VM 實機開 console 驗 prompt／中文 `say`／Ctrl+C |
| 產品閘 | 維持 `windows-xp-legacy-build.md` 的 PE 5.01 gate、post-XP import 掃描（動態 `GetProcAddress` 不進 import table，可過掃）與 guest runtime 驗收 |
| 流程 | 依 [Style]：XP 工作在新 feature branch，完成後合回 `debug`；不與 Qt6 主線混驗 |

---

## 4. 建議實作順序

| 序 | 項目 | 理由 |
|----|------|------|
| 1 | 動態 log 調整（§2.4） | 最小改動、不開新跨執行緒路徑 |
| 2 | 機器可讀輸出（§2.5） | 純呈現層，風險最低 |
| 3 | 機器人／開局介入（§2.3） | 接既有 Server 方法，無新邊界 |
| 4 | 房間層級處置（§2.1） | 需新增 Server→Room queued 路徑，影響面最大 |
| 5 | 維護模式（§2.2） | 依賴 signup 拒絕語意與 client 顯示配合 |
| 6 | XP 32-bit server console（§3） | 相容修補獨立於 Qt6 主線，可與 1–2 並行 |

> 全部項目共同驗證規則：`tools/ci/server-console-smoke.sh` 為 piped 驗收基準並逐項擴充；遵 AGENTS.md §7.6／§7.7（整批修改、targeted compile、60 秒內 focused check，長 gate 交遠端 CI）。
