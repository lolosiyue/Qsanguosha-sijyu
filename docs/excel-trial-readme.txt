QSanguosha Excel 試用準備包 — 請先讀這份說明

狀態：尚未含 QSanguoshaExcel.xlsm，目前不能直接開玩。
本機 Office 啟用畫面阻擋 VBE 編輯，使用者已同意先完成其餘套件。
本資料夾不是已完成驗收的 Excel 遊戲，也不是空白巨集假檔。

適用：Windows 10/11 x64，桌面 Excel 2016 以上，Office 32/64 位元皆可。
不適用：Windows XP、Mac、Excel 網頁版、Google 試算表。
執行檔、DLL、Lua/AI、已宣告擴展與翻譯、圖片音效均放在本資料夾。
不必在公司電腦安裝 Qt、Python、Visual Studio；公司須允許巨集、EXE 及 Windows Script Host。
不要繞過公司政策，不修改 Trust Center 或「信任 VBA 專案物件模型存取」。

一、仍缺什麼
1. 在可編輯的 Excel 製作真正包含 VBA 的 .xlsm。
2. VBE 編譯、匯出比對及實際牌桌測試。
3. 修正後 Release 完整對局與 Excel 正常關閉驗收。
4. 全部模式、武將技能、28 類互動、舊版平台與 CI 驗收。
QML 互動已按要求排除。暫存擴展、未宣告 Lua 與 etc 自訂劇本不在本包。

二、在可用 Excel 上組成活頁簿（需要公司允許）
1. 先將整個 excel-release 資料夾複製到可寫入的本機位置，不要只複製單一檔案。
2. 新增空白活頁簿，另存新檔為本資料夾根目錄的 QSanguoshaExcel.xlsm。
   檔案類型選「Excel 啟用巨集的活頁簿 (*.xlsm)」。
3. 按 Alt+F11 進入 VBE；選取這本活頁簿的專案。
4. 檔案 → 匯入檔案，逐一匯入 vba-source\import-cp950-crlf 內七個 .bas。
   這份來源適合繁體中文 Windows 傳統字碼 CP950。其他系統地區請先停止並回報，避免中文亂碼。
5. 不要將 ThisWorkbook.cls 匯入成另一個類別。
   開啟專案原有的 ThisWorkbook 程式碼視窗，貼入 ThisWorkbook.cls 從
   Option Explicit 開始的內容；不要貼 VERSION 或 Attribute 行。
6. 偵錯 → 編譯 VBAProject。若有錯誤，記錄完整訊息與被反白的程式行，停止開局。
7. 將七個模組及 ThisWorkbook 重新匯出到新建的 vbe-export 資料夾，供後續比對。
   vba-source\original-utf8 保留原始 UTF-8 來源；不能用 CP950 原始 bytes hash 直接比較 UTF-8。
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

五、現有證據與限制
先前 Debug 橋接經 Python HTTP 驅動、真實 Excel 父程序跑完 05P，第 5 輪反賊勝。
橋接/helper 正常退出且 Card gauges 歸零；Excel Quit 被拒絕後需強制停止。
後續修正了公開快照中的牌堆順序洩漏，現代定向測試 10 項通過。
這些證據不代表本包 .xlsm/VBA 或 Release 完整對局已驗收。
diagnostics 內保存本次實際執行的建置/依賴/載入檢查摘要；
release-manifest.json 記錄所有封裝檔案的 SHA-256 與來源狀態。
