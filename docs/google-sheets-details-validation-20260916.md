# Google Sheets 說明與座位詳情檢查點（2026-09-16）

使用者批准 Sheets 增量建置及前端短測試；通過後更新既有線上 Apps Script。
未執行 CTest、原生 focused executable 或新對局。結果綁定 `debug` 未提交來源，
SHA-256、HEAD、命令、耗時與退出碼見 `builds/sheets-details-checkpoint-20260916/`。

| Gate | 結果 | 證據 |
|---|---|---|
| Sheets bridge 增量建置 | PASS，exit 0，12.391 秒 | `build.log`、`build.json` |
| 前端短測試 | PASS，30/30，包裝耗時 0.328 秒 | `frontend.log`、`frontend.json` |
| 線上腳本更新 | PASS，Client.gs／Table.gs／Sidebar.html 儲存並完整讀回一致 | `apps-script-deployment.json`、更新前備份 |
| 線上入口 | 新版「查詢詳情（座位／項目）」選單、座位操作提示及側欄「查詢詳情」按鈕已出現 | `sidebar-deployed.png` |
| 新版對局內座位／圖片查詢 | NOT RUN | 舊會話已關閉，本輪未開新局；下次連線刷新才重繪舊牌桌資料 |

建置命令：`cmake --build --preset debug --target qsanguosha_sheets_bridge --parallel 8`。
短測試：`node --test google-sheets/tests/client.test.cjs`。
兩者均由 60 秒限時包裝執行，本輪沒有 timeout、失敗或重跑。

## 回歸涵蓋

- 清單讀取原生完整 `description` 並相容 `detail`，不顯示未翻譯的 `:lookup_key`。
- 武將排序候選保留完整描述；D 欄仍是布林勾選，圖片識別碼不寫入可見清單。
- 房間座位合併標題／內容以玩家 ID 查詢，包含陣亡且座位號重複的情況；中央區、
  跨出座位的選取會被拒絕，避免查錯玩家。
- 玩家詳情在側欄與 QSAN Details 顯示；圖片仍走已授權素材請求，巢狀裝備的素材
  識別碼不出現在詳情文字。標記鍵名維持原樣。
- 一般是／否選項不誤作武將查詢；側欄保留最近一次查閱內容，輪詢不覆蓋它。

這些前端案例使用服務替身，不等同 Google Sheets 對局內點選驗收，也不證明每個
擴展均有原生翻譯。原生武將完整技能描述接線已成功編譯，未逐武將執行驗收。
未重新啟動 gateway／tunnel，未 commit／push。
