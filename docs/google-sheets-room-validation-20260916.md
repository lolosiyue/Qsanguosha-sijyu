# Google Sheets 房間布局與 TUI 共用戰報驗證（2026-09-16）

後續已完成 [線上部署與 05P 託管完整對局](google-sheets-room-trust-acceptance-20260916.md)，
含 GAME_OVER、主公＋忠臣勝、正常退出及清理；另記錄實際版面的可讀性問題。
下列 NOT RUN 為原始來源檢查點的範圍記錄。

## 範圍與結果

使用者在來源檢查點完成後批准增量建置 Sheets／TUI，以及前端與 TUI 文字短測試。
本輪沿用 L 工作樹的 Debug／VS 2026／Qt 6.11.1 設定；沒有重新 configure、clean
或執行本地 CTest。下列結果綁定本輪尚未提交的來源，不能視為遠端 CI 結果。

| Gate | 結果 | 證據 |
|---|---|---|
| Sheets bridge 增量建置 | PASS | `builds/sheets-room-checkpoint-20260916/build.log` |
| TUI 主程式及測試執行檔增量建置 | PASS | 同上，CMake exit 0 |
| 前端測試 | PASS，27/27，約 0.48 秒 | `frontend-fixed.log` |
| TUI `card-text` | PASS，exit 0，7.610 秒 | `card-text.log`、`native-tests.json` |
| TUI `log-text` | PASS，exit 0，6.985 秒 | `log-text.log`、`native-tests.json` |
| JavaScript 語法／diff 靜態檢查 | PASS | `node --check`、`git diff --check` |
| Google Apps Script 部署／真實版面 | NOT RUN | 線上文件尚未更新 |
| 完整對局／多人／CI | NOT RUN | 本輪未啟動 |

表中的相對日誌檔案均位於 `builds/sheets-room-checkpoint-20260916/`。
`native-tests.json` 保存 TUI 測試執行檔 SHA-256、工作目錄、Qt PATH 及退出碼；
`evidence.json` 保存來源與建置產物摘要。

## 命令與驗證內容

```powershell
& 'H:\Program file\visualstudio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build --preset debug --target qsanguosha_sheets_bridge qsanguosha_tui qsanguosha_tui_tests --parallel 8
node --test google-sheets/tests/client.test.cjs
# 原生短測試由有 60 秒 timeout 的 subprocess 包裝執行，PATH 前置同版 Qt bin。
builds/cmake-vs2026/tests/Debug/qsanguosha_tui_tests.exe --suite card-text
builds/cmake-vs2026/tests/Debug/qsanguosha_tui_tests.exe --suite log-text
```

| 驗證內容 | 涵蓋範圍 |
|---|---|
| 房間布局 | 1–20 人座位排序、自身在下、保留陣亡席位、區域不重疊、手牌增長 |
| 可見資料 | 中央只使用已授權的 PlaceTable，暗牌不展開，戰報最新在上 |
| 草稿與既有操作 | 版面搬移保留卡牌與重複目標順序；原有七種 response shape、選牌／預檢與指令恢復案例 |
| TUI 共用文字 | 牌面花色／點數、技能／傷害翻譯、HTML 清理、移牌／仁區、TUI 補充戰報入口 |

## 本輪修正及工具紀錄

- 首輪前端為 26/27：特定手牌數令右側戰報末列延伸至候選清單標題列。將戰報容量
  的邊界從 `first - 5` 改為 `first - 6`，保持戰報合併區在標題列上方；重跑為 27/27。
  原始失敗日誌 `frontend.log` 保留，未覆寫。
- `card-text` 已正常 exit 0 並保存日誌；Python 首個包裝器在向 cp950 console
  印出簡體結果時遇到 UnicodeEncodeError。修正包裝器輸出為 UTF-8 後只繼續執行
  尚未跑的 `log-text`，沒有重跑已通過的 `card-text`。這是輸出包裝器錯誤。

## 仍待驗收

實際 Google Sheets 的合併儲存格呈現、文字可讀範圍、更新延遲，以及在新版房間頁
完成真人操作，仍需部署後驗收。原生 focused 結果不代替 Sheets GUI 或完整對局證據。
上游未提供翻譯的擴展詞條仍沿用 TUI fallback，本輪沒有宣稱所有擴展文本均已翻譯。
