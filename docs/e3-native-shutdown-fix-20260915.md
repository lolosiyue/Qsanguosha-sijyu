# E3：共用原生引擎關閉修正

日期：2026-09-15。工作區：`L:\finaldebug\QSanguosha-v2`。基準 HEAD：`f3966fb8fbcf4a62cb21cf060e0938d2294f2876`。

## 根因與範圍

Windows/MSVC 原先以 `/EHsc` 編譯 `qsanguosha_engine`，假設 `extern "C"` 函式不會傳出 C++ 例外。但房間停止會由 Lua → SWIG → Room 路徑拋出 `TriggerEvent::GameFinished`，穿過 Lua C API 回到 C++。編譯器省略的解構處理會使 `LuaInvocationScope` 未正常釋放，留下 invocation depth 與 Lua pin。之後 `LuaRuntime::shutdown()` 拒絕關閉，worker-final 也因非零 pin 拒絕回收。

新增案例使用真正的 RoomRuntime 與工作執行緒，走 C++ → Lua → C++ → Lua → C++，在內層拋出 `GameFinished`。原設定實測已捕獲例外、內層回呼解構已執行，但 `depth=2`、`domain_pins=1`、`runtime_pins=1`，退出碼 3。這重現了 9 月 15 日 Sheets E3 日誌中殘留 1 個 domain pin 的失敗機制。

Sheets helper 與 Web 外部 Windows server 共用這個 engine target。這項根因不是 Apps Script 專屬；歷史 Web 等待逾時是否還有其他成因，仍需真實入口重測，不能僅以本案例排除。

## 修改

- 僅 engine 的 MSVC C++ 編譯增加 `/EHs`、`/EHc-`；包含預編譯標頭、LuaRuntime、AI、Room、產生的 SWIG。保留同步 C++ 例外解構，不將原生存取違規當作可恢復例外。
- 分開指定兩個旗標，讓 Visual Studio 產生 `SyncCThrow`，並讓 Ninja 明確取消既有 `/EHsc` 的 C 函式假設。
- 新增 `lua-exception-unwind` 精準案例，並納入既有 runtime contract 子案例清單；檢查 depth/pins 歸零、兩個 Lua runtime 關閉及 RoomRuntime `Closed`。
- 現有 `QSAN_XP_SHUTDOWN_TRACE` 增加 game/AI depth、closed 狀態及 domain pin 診斷。
- 保留原有 scope/pin、Card lease、執行緒歸屬與 worker-final 檢查；未修改 Lua 原始碼或強制清零引用。

編譯器語意依據：[Microsoft `/EH` 文件](https://learn.microsoft.com/en-us/cpp/build/reference/eh-exception-handling-model)。

## 驗證

證據目錄：`builds/e3-shutdown-20260915/`。直接執行精準測試，每次外部上限 60 秒；未執行本地 CTest。

| 項目 | 結果 |
|---|---|
| 修正前 targeted build | PASS |
| 修正前 nested Lua exception | 預期失敗：exit 3；depth 2、domain/runtime pin 1；13.875 秒 |
| 修正後 targeted build | PASS：runtime tests、Sheets bridge／ExcelServer helper、dedicated server |
| 實際編譯參數 | PASS：所有組態為 SyncCThrow；Debug PCH、LuaRuntime、SWIG 的 CL tlog 均為 `/EHs /EHc-` |
| 修正後同一 nested Lua exception | PASS：exit 0，17.109 秒；depth/pins 歸零，兩個 VM Closed，`CARD_LIFETIME_ZERO`，RoomRuntime Closed |
| 原有 Lua-held CardUseStruct teardown | PASS：exit 0，13.610 秒；工作執行緒回收及最終 gauge 歸零 |
| 原有 active Lua pin 負向案例 | PASS（預期拒絕）：12.781 秒；`ROOM_RUNTIME_BLOCKED`、`CARD_LIFETIME_SHUTDOWN_FAILED` 均指出 pin 1；退出碼 `0xC0000409`，未將拒絕當正常關閉 |
| 靜態差異與獨立審閱 | PASS；未放寬 shutdown 檢查 |
| 真實 Sheets/Web 關閉、完整對局、其他平台 | NOT RUN；不以 focused 結果代替入口驗收 |
| Release 實際執行與遠端 CI | NOT RUN；Release 目前僅確認生成的編譯設定 |

未 commit、未 push。以上為 E3 focused 檢查點的原始範圍。

後續另行授權的 Sheets 單局於 2026-09-15 21:01 完成：第五輪 GAME_OVER、主公＋忠臣勝，正常關閉 exit 0、無強制終止且程序／埠清理通過，見[真人驗收報告](google-sheets-live-acceptance-20260915-fixed.md)。Web 與其他平台仍未由此結果驗收。
