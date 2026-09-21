# 結算歷史驗證報告 — 2026-09-20

[API 與使用規格](../resolution-history.md)。結果對應下列證據目錄保存的來源與執行檔快照。

2026-09-20 驗證結果如下，證據保存在 `builds/resolution-history-validation-20260920-154324/`：

| Gate | 狀態 | 範圍 |
| --- | --- | --- |
| Debug build | PASS | GUI、server、TUI、兩個 history test targets；SWIG 重新生成並編譯 |
| Native history | PASS | 6 個獨立 case：late-acquisition、damage、pile-moves、nested-turn、pagination、cleanup |
| Lua / Room integration | PASS | rules-lua、isolated-lua、room-wrappers、snapshot |
| Replay takeover focused | PASS | `qsanguosha_core_tests --suite takeover-snapshot`；含 Room 實際還原 |
| 05P SmartAI real TCP 初次 | FAIL（TUI） | 伺服器自然結局 `lord+loyalist`；TUI 訊息落後、600 秒等待超時，退出碼 7 |
| 05P SmartAI real TCP 修正後 | PASS | 相同種子與伺服器 binary；TUI 收到 GAME_OVER、script 完成、client/server exit 0、無 orphan、TCP/WS port 釋放；證據在 `builds/tui-delay-fix-20260920/` |
| CTest / GUI 人工 / CI / 跨平台矩陣 | NOT RUN | 未執行 |

新 fixture 的初次執行暴露了缺少 Room context/card-state reset，以及監聽 `ChoiceMade` 時被 trigger-order 選擇遞迴觸發的問題；均已在 fixture 修正。isolated AI fixture 改為將 self 與 other-player list 分開，符合既有 facade 合約。原始失敗日誌與可取得的 dump 均保留，最終 10 個 history case 正常退出。

初次完整對局失敗證據保留。TUI 延遲修正移除 classic 模式逐通知建立未使用摘要的運算，重用文字清理的固定正規表示式，並通過四項 focused 測試及一次同種子重測。兩次的 246 筆 `[LOG]`／`[AUTOTEST]` 去除時間後順序一致；新版 TUI 的完整結局日誌在 server GAME_OVER 後約 0.70 秒寫完。驗收範圍為 classic/script TCP；可見 board、GUI 與 history RAM 增幅未驗收。
