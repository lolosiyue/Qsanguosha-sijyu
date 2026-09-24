QSanguosha Excel runtime-only 組裝指南

沒有 `.xlsm` 時只能執行 runtime-only 檢查；正式封裝必須使用含 `xl/vbaProject.bin` 的活頁簿，並由 `tools/excel/package.py` 與 `release-manifest.json` 驗證。

適用：Windows 10/11 x64，桌面 Excel 2016 以上，Office 32/64 位元皆可。
不適用：Windows XP、Mac、Excel 網頁版、Google 試算表。
執行檔、DLL、Lua/AI、已宣告擴展與翻譯、圖片音效依對應版本 manifest 放置。
不必在公司電腦安裝 Qt、Python、Visual Studio；公司須允許巨集、EXE 及 Windows Script Host。
不要繞過公司政策，不修改 Trust Center 或「信任 VBA 專案物件模型存取」。

一、封裝條件
1. 在可編輯的 Excel 製作包含 VBA 的 `.xlsm`。
2. VBE 編譯後匯出全部模組，依 `release-manifest.json` 比對 SHA-256。
3. 使用 `tools/excel/check.py` 檢查 manifest coverage；VBE 與實際牌桌測試另行記錄。
4. 暫存擴展、未宣告 Lua 與自訂劇本不在封裝內容中。

二、在可用 Excel 上組成活頁簿（需要公司允許）
1. 先將整個 excel-release 資料夾複製到可寫入的本機位置，不要只複製單一檔案。
2. 新增空白活頁簿，另存新檔為本資料夾根目錄的 QSanguoshaExcel.xlsm。
   檔案類型選「Excel 啟用巨集的活頁簿 (*.xlsm)」。
3. 按 Alt+F11 進入 VBE；選取這本活頁簿的專案。
4. 檔案 → 匯入檔案，依 release-manifest.json 的 vba.modules 清單，逐一匯入
   vba-source/import-cp950-crlf 內的 .bas；ThisWorkbook.cls 依下一步處理。
   此匯入副本採 CP950，適用繁體中文 Windows 字碼環境。其他系統地區須先確認
   VBE 匯入文字正確；原始 UTF-8 來源保留於 vba-source/original-utf8。
5. 不要將 ThisWorkbook.cls 匯入成另一個類別。
   開啟專案原有的 ThisWorkbook 程式碼視窗，貼入 ThisWorkbook.cls 從
   Option Explicit 開始的內容；不要貼 VERSION 或 Attribute 行。
6. 偵錯 → 編譯 VBAProject。若有錯誤，記錄完整訊息與被反白的程式行，停止開局。
7. 將全部模組及 ThisWorkbook 重新匯出到 `vbe-export`，再依 manifest 的模組清單與
   SHA-256 比對；不要以不同編碼的原始 bytes 直接比較。
8. 儲存後關閉並重新開啟 QSanguoshaExcel.xlsm。
   只在 Office 現有政策允許時啟用這本活頁簿的巨集，不全域開放所有巨集。
9. 程式應建立首頁、房間設定、牌桌、詳情、戰報五張工作表。
   若沒有，回報巨集警告與 VBE 編譯結果；不要直接雙擊背景 EXE 當成遊戲入口。

三、第一輪試用
先測本機 AI 05P：首頁 → 本機對局 → 選將 → 選牌/目標 → 確認或取消。
檢查牌面、繁體中文、技能說明、戰報及結算；再正常關閉活頁簿。
若需要終止背景會話，先使用活頁簿的停止/返回操作；失效時回報本次會話，
不要按執行檔名稱批次殺掉其他遊戲或 Excel 程序。

四、回報格式
Windows 版本與位元數：
Excel 版本與位元數（檔案 → 帳戶 → 關於 Excel）：
編譯是否成功：
操作到哪一步：
預期結果／實際結果：
完整錯誤訊息或畫面：
是否正常結算與關閉：
可附本次專屬會話 data\server\logs 中的相關日誌；不要附 bootstrap.json，
它包含連線 token。不要附公司文件、帳戶登入資訊或其他工作內容。

五、證據與報告
Runtime-only 的建置、依賴、載入與 hash 摘要由 `diagnostics/` 及
`release-manifest.json` 保存。實際 Excel／VBA 操作、失敗記錄與完整對局結果
寫入對應版本的報告，不在本組裝指南重複。
