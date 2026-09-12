# Excel 前端實作與驗收狀態

本頁記錄 2026-09-12 L 工作樹的來源檢查點。這不是完整 Excel 遊戲交付，也不是建置／可玩證據。工作期間其他任務可更新共同 HEAD；發行仍須重新凍結來源與外部擴展。

| 檢查點 | 目前來源狀態 | 尚缺證據或功能 |
|---|---|---|
| CP0 固定範圍 | 來源／素材 hash inventory 工具及候選清單格式 | 固定版本 inventory、逐模式與武將／技能可達映射未完成 |
| CP1 共用原生核心 | 原生來源、Qt6／Qt5 Debug build 及三個現代 focused executable 通過 | 實際會話／程序清理與舊版執行尚未驗證 |
| CP2 可玩牌桌 | 五工作表 VBA 來源、範圍內 28 類操作路徑及非同步 transport 已接線；QML 經使用者同意排除 | 實際 `.xlsm`、VBE 編譯、Excel 操作未完成 |
| CP3 全內容接通 | catalog、圖片音效、重連、分頁、宣告與技能實例已接線 | 全技能覆蓋、複合物件型房間設定編輯、巨集重設後首頁會話管理仍缺 |
| CP4 交付驗收 | 候選封裝的 PE 架構／素材樹／hash 檢查工具 | 工作簿、兩套 portable bundle、DLL／plugins 依賴閉包、Office／Windows 矩陣與完整對局未交付 |

## CP1 原生來源範圍

- `qsanguosha_client_session` 共用連線，Excel 沿用 `ClientCore` 狀態與回覆驗證、`ClientRulesSession` 串行規則查詢。
- loopback HTTP／JSON v1、私有 bootstrap、字串 uint64、指令去重、互動版本檢查、有界事件與快照恢復。
- 原生 worksheet row descriptors；`declaration` 映射既有 rules `user_string`，卡牌文字由原生產生。
- 父程序 handle／建立時間、單會話停止、HTTP 回覆排空、owned helper 清理、慢啟動取消及私有 settings。
- legacy 在開房、admission 與 reconnect 依房間設定容量限制 2–10 人；現代版不加前端人數上限。
- Qt5 rules 產品與 Qt6 probe 條件拆分；XP 支援 FMOD／NULL 音效，明確拒絕 Qt6 QT backend。

## 使用者確認的 QML 排除範圍

使用者已確認 QML 不必實作於 `.xlsm` 版。現代與舊版 Excel 套件均排除 `qml_interact`／`qsanguosha.qml`，不再以缺少 QML presenter 阻擋完整交付驗收。標準互動驗收範圍因此為 28 類；原生登錄仍有 29 類。

若遠端伺服器送出 QML 請求，目前橋接會明確拒絕並中止該會話，不要求玩家輸入 JSON，也不偽造遊戲回覆。內容盤點仍須標示依賴 QML 的技能／模式操作，不能將這些排除路徑宣稱為 Excel 可玩。

`excel/frontend-coverage.json` 的 28 條映射只是來源路徑。仍需固定內容呼叫清單、QML 以外每個必要特殊 presenter 及逐技能案例；缺少範圍內必要映射時完整交付 gate 不通過。

## 檢查與下一個授權範圍

| 類別 | 證據 |
|---|---|
| 靜態檢查 | `git diff --check`、三個 Python 工具 AST parse、兩份 Excel JSON parse 通過；整合覆核發現項目已修正 |
| 現代／舊版建置 | Qt6 x64／Qt5.6.3 v141_xp x86 Debug bridge 與 helper 建置成功，兩組 exit 0；Release／部署未執行 |
| focused tests | 現代 IPC exit 0；互動 QtTest 7 passed；呈現修正 fixture 後 9 passed；均低於 60 秒，未使用 CTest |
| 真實 Excel／VBE | 未執行，未產生含 VBA 的 `.xlsm` |
| 完整對局／清理 | 未執行；來源所有權處理不等於清理驗收 |
| CI／跨版本矩陣 | 未啟動 |

使用者已授權建置且明確解除建置的 60 秒限制。已完成現代 `qsanguosha_excel_bridge`／`qsanguosha_excel_server`、舊版 `qsanguosha_excel_bridge`／`QSanguoshaXPServer`，以及三個現代 `qsanguosha_excel_*_tests` 的 Debug 建置。後續使用者要求自動測試，已直接執行三組 focused tests，每個設定 60 秒外部上限；不包含本地 CTest、完整對局或矩陣。

focused 證據為 `builds/excel-cp1-focused/results.json` 與同目錄 QtTest 日誌。首輪呈現測試錯把第一個玩家列當成對手；改為依玩家 ID 查找，保留暗牌數量及牌面隱藏斷言後重建該 target，9/9 通過。7/9 的 QtTest 計數含 init／cleanup。原生產品來源未因此修改；初次失敗日誌及新測試產物 hash 均保留。這些案例不涵蓋完整 28 類 Excel 操作或真實 Engine 全技能規則預檢。

建置日誌：`builds/excel-cp1-build-modern-final.log`、`builds/excel-cp1-build-legacy-final.log`。產物／來源 hash 與 PE 架構檢查：`builds/excel-cp1-build-evidence.json`；現代為 x64，舊版為 x86／subsystem 5.01。這些是建置證據，不是 XP 實機驗收。

建置修正包含測試的 protocol 標頭、Engine Settings 編譯定義、Qt5.6 QSet 建構式及 QUuid 格式相容。初次 MSBuild FileTracker 沙箱存取失敗已以正常權限重試完成。舊版仍有 v141_xp 棄用與 C4819 字碼頁警告，文字顯示須在實際 Excel／XP 驗收。

VBE／`.xlsm` 與遊戲驗收另立檢查點。發行工作簿須在 VBE 匯入、編譯、儲存、重新匯出比對，不修改全域 AccessVBOM 或 Trust Center。
