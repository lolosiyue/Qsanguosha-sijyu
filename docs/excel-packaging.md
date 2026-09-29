# Excel 封裝指南

本頁定義 Excel modern／legacy bundle 的輸入、封裝與靜態檢查。CP0 inventory、VBA
來源、runtime-only staging 和正式封裝使用同一套工具；實際 Excel 操作與完整對局屬另行驗收，
不在本頁記錄。

## 支援範圍與限制

Excel 橋接遵循 [Excel IPC v1](excel-ipc.md)。

| 執行期 | 環境 | 限制 |
|------|------|------|
| modern | Windows 10／11 x64、Qt 6 full runtime | 需完整的 Qt6 執行檔／DLL、Lua、AI、extensions、lang、image、audio |
| legacy | Windows XP SP3／7／8.1／10 x86、Qt 5.6 | 最多 10 人 |

- `.xlsm` 版不實作 QML 自訂互動（`qml_interact`／`qsanguosha.qml`），因此同一份活頁簿可適用兩套執行期；依賴 QML 自訂互動的內容路徑必須明確排除。
- 前端互動覆蓋範圍記錄於 [excel/frontend-coverage.json](../excel/frontend-coverage.json)。
- 正式交付需提供已編譯 VBA 的 `.xlsm`。
- 圖片路徑必須落在封裝 image root；VBA 不組裝 wire card text 或 Protocol V2 packet。
- 支援的 Office host 為 Excel 2010 x86 與 modern Office 2016+ x86／x64；TrustAccess 不得變更。

## 輸入

[tools/excel/package.py](../tools/excel/package.py) 需要對應版本的 bridge、server、Lua／AI、extensions、lang、
image、audio、實際含 `xl/vbaProject.bin` 的 `.xlsm`、VBE 匯出目錄及模組 SHA-256 map。
VBA 模組清單見 [stage-trial.py](../tools/excel/stage-trial.py) 的 `VBA_FILES`；封裝器要求 hash map
涵蓋每個匯出檔且不可有多餘檔案。

## 封裝

```powershell
python tools/excel/inventory.py --root . --output artifacts/excel-inventory.json
python tools/excel/package.py --tier modern --source-root . `
  --inventory artifacts/excel-inventory.json --dest .\staging\excel-modern `
  --bridge .\excel-release\QSanguoshaExcelBridge.exe --server .\excel-release\QSanguoshaExcelServer.exe `
  --tree .\lua --tree .\extensions --tree .\lang --tree .\image --tree .\audio `
  --existing-xlsm .\artifacts\QSanguoshaExcel.xlsm `
  --vba-source .\excel\vba\export --vba-hashes .\excel\vba\release-hashes.json
python tools/excel/check.py --tier modern --vba-source excel/vba `
  --manifest staging/excel-modern/release-manifest.json
```

`--tier legacy` 使用 `QSanguoshaXPServer.exe`。目的地必須不存在或為空目錄；工具會
拒絕空檔、非 PE binary、缺少 VBA binary、VBE hash 不符及未涵蓋的來源檔。Qt plugin、
FMOD 與其他 DLL 以保留目錄結構的 `--tree` 或 `--dll` 加入；完整 DLL 相依閉包仍需
在乾淨 Windows 環境驗證。

## 沒有活頁簿時

```powershell
python tools/excel/stage-trial.py --root . --binary-dir .\excel-release --dest .\staging\excel-runtime-only
```

產物標記 `workbook.status=missing`，只能檢查 runtime。建立含 VBA 的
`.xlsm` 後重新匯出全部模組與 hash，才可執行正式 `package.py` gate。

建置輸出位置由 [QSanguoshaExcel.cmake](../cmake/QSanguoshaExcel.cmake) 的 `RUNTIME_OUTPUT_DIRECTORY_RELEASE` 定義。`stage-trial.py::_base_manifest` 記錄來源身分；`finalize()` 重新建立檔案雜湊與來源資訊，只用於同一批封裝，舊版本封裝保留原 manifest。
