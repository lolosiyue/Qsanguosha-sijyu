# Excel client release gates

Excel bridge 遵循 [Excel IPC v1](excel-ipc.md)。CP0 只凍結 source inventory 與候選封裝輸入，不能宣稱可攜版完成。

使用者已同意 `.xlsm` 版不實作 QML 自訂互動（`qml_interact`／`qsanguosha.qml`），適用兩套執行期。驗收要求範圍內 28 類標準互動與其他必要特殊 presenter；QML 排除項不阻擋交付，但依賴它的內容路徑必須明確列示。

候選封裝分成兩個獨立 portable bundle：modern 使用 `QSanguoshaExcelBridge.exe` + `QSanguoshaExcelServer.exe`，不設前端玩家上限；legacy 使用 `QSanguoshaExcelBridge.exe` + `QSanguoshaXPServer.exe`，限制 2–10 人。兩者共用同一個實際 `.xlsm` 與相同 VBA source/hash。`package.py` 要求 `tools/excel/LaunchExcel.vbs` 並將它放到封裝根目錄；它以 private session settings 與 `QSAN_USER_DATA_ROOT` 啟動 bridge，不修改 Excel process environment 或 Trust Center。

| Gate | 自動化 | 證據／限制 |
|---|---|---|
| CP0 source inventory | `tools/excel/inventory.py` | HEAD、dirty、SHA-256、模式、能力與候選 literal catalog；外部 authority 只讀 |
| Candidate staging | `tools/excel/package.py` | 顯式新目的地、非空拒絕、PE 架構、必要素材樹、檔案雜湊；DLL／Qt plugins 的依賴閉包仍待乾淨環境驗收 |
| Existing workbook | package validator | 必須有 `xl/vbaProject.bin`；缺 binary 直接拒絕 |
| VBE module identity | `--vba-source` + `--vba-hashes` | 本任務在 VBE export/import/compile/save/reexport 後保存 `.bas`／`.cls` hash；缺 hash、空 export 或不符直接拒絕；工具不能假造 VBA |
| VBA/API static check | `tools/excel/check.py` | 檢查 WinHTTP、headers、IPC endpoints、OnTime 與 manifest coverage；不跑 Excel |
| Win10/11 x64 modern | manual + runtime | full Qt6 executable/DLL、Lua、AI、extensions、lang、image、audio 需另行建置與驗收 |
| XP/7/8.1/Win10 x86 legacy | manual + runtime | Qt5.6，最多 10 人；需另行相容性驗收 |
| Office hosts | manual VBE gate | Excel 2010 x86 與 modern Office 2016+ x86/x64 需逐一驗證；TrustAccess 不得變更 |

VBA 的 release 流程是：在受控 Excel 環境建立固定模板，由 VBE 匯入版本控制中的 `.bas`／`.cls`，編譯並儲存 `.xlsm`，重新匯出全部模組並比對來源；保存匯出檔 SHA-256 map、編譯與重匯出證據後才封裝。工具檢查 VBA binary 存在及匯出檔 hash，不能自行證明該 binary 與模組一致。建立含 VBA 的 `.xlsm` 是本專案的交付工作，玩家不需要自行匯入；目前尚未建立此交付物。

目前來源進度與未驗收項目見 [Excel implementation status](excel-implementation-status.md)。

圖片路徑必須落在封裝 image root；VBA 不組裝 wire card text 或 Protocol V2 packet。完整 gameplay、Excel UI、WinHTTP、VBE 與跨 Windows/Office acceptance 都是後續 checkpoint。
