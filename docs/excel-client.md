# Excel client release gates

Excel bridge 遵循 [Excel IPC v1](excel-ipc.md)。CP0 只凍結 source inventory 與候選封裝輸入，不能宣稱可攜版完成。

`.xlsm` 版不實作 QML 自訂互動（`qml_interact`／`qsanguosha.qml`），適用兩套執行期。驗收要求範圍內 28 類標準互動與其他必要特殊 presenter；QML 排除項不阻擋交付，但依賴它的內容路徑必須明確列示。

modern／legacy 的封裝輸入、執行檔與 VBA 匯出流程見[封裝指南](excel-packaging.md)。前端互動範圍由 [frontend-coverage.json](../excel/frontend-coverage.json) 記錄。

| Gate | 自動化 | 證據／限制 |
|---|---|---|
| CP0 source inventory | `tools/excel/inventory.py` | HEAD、dirty、SHA-256、模式、能力與候選 literal catalog；外部 authority 只讀 |
| Candidate staging | `tools/excel/package.py` | 顯式新目的地、非空拒絕、PE 架構、必要素材樹、檔案雜湊；DLL／Qt plugins 的依賴閉包仍待乾淨環境驗收 |
| Existing workbook | package validator | 必須有 `xl/vbaProject.bin`；缺 binary 直接拒絕 |
| VBE module identity | `--vba-source` + `--vba-hashes` | 在 VBE export/import/compile/save/reexport 後保存 `.bas`／`.cls` hash；缺 hash、空 export 或不符直接拒絕；工具不能假造 VBA |
| VBA/API static check | `tools/excel/check.py` | 檢查 WinHTTP、headers、IPC endpoints、OnTime 與 manifest coverage；不跑 Excel |
| Win10/11 x64 modern | manual + runtime | full Qt6 executable/DLL、Lua、AI、extensions、lang、image、audio 需另行建置與驗收 |
| XP/7/8.1/Win10 x86 legacy | manual + runtime | Qt5.6，最多 10 人；需另行相容性驗收 |
| Office hosts | manual VBE gate | Excel 2010 x86 與 modern Office 2016+ x86/x64 需逐一驗證；TrustAccess 不得變更 |

正式交付需提供已編譯 VBA 的 `.xlsm`，並保存 VBE 匯出比對與實際 Excel 操作證據。

圖片路徑必須落在封裝 image root；VBA 不組裝 wire card text 或 Protocol V2 packet。完整 gameplay、Excel UI、WinHTTP、VBE 與跨 Windows/Office acceptance 都是後續 checkpoint。
