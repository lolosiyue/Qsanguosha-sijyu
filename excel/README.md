# Excel client CP0 tools

這些工具只產生可審核的靜態資料或候選封裝，不代表 Excel／VBA／遊戲已驗收。

## CP0 inventory

```powershell
python tools/excel/inventory.py --root . --output artifacts/excel-baseline.json
```

工具記錄 Git HEAD、完整 dirty 清單、來源檔 SHA-256、模式與 runtime tier，並以原始碼字面量建立 modes/generals/skills 的候選目錄。外部 extensions authority 只記錄路徑與唯讀狀態；不 fetch、copy 或 push。註冊表、生成檔及外部腳本仍需人工覆核。封裝工具需要 Python 3.10 以上，在現代建置環境執行，不屬於 XP 玩家執行期依賴。

## Candidate package

`package.py` 要求顯式 `--dest` 與 `--tier`，每次只建立一個 bundle：modern 使用 bridge + `QSanguoshaExcelServer.exe`，legacy 使用 bridge + `QSanguoshaXPServer.exe`。兩者都必須帶實際含 VBA binary 的 `.xlsm`、非空 VBE export/hash map，以及 Lua/AI、extensions、lang、image、audio 依賴樹。目的地必須不存在或為空目錄；任何非空目錄都會拒絕。空檔、非 PE 的 exe/dll、內容含明顯 placeholder marker 的輸入會 fail closed。

```powershell
python tools/excel/package.py --tier modern --source-root . --inventory artifacts/excel-inventory.json --dest .\staging\excel-modern `
  --bridge .\QSanguoshaExcelBridge.exe --server .\QSanguoshaExcelServer.exe `
  --tree .\lua `
  --tree .\extensions --tree .\lang --tree .\image --tree .\audio `
  --existing-xlsm .\artifacts\QSanguoshaExcel.xlsm --vba-source excel\vba\export `
  --vba-hashes excel\vba\release-hashes.json
```

`.xlsm` 必須含非空 `xl/vbaProject.bin`；沒有 binary 會拒絕。VBE 匯出模組與 SHA-256 hash map 同樣是必要輸入，hash 不符或缺漏會拒絕。manifest 會記錄每個 packaged file 與 dependency；TrustAccess 固定記錄為 `unchanged`。

`lua` 樹必須包含 `lua/ai`，不另部署頂層 `ai`。使用重複 `--dll` 指定同架構的 DLL；Qt plugins 以保留目錄結構的 `--tree` 加入。這些輸入須來自對應版本的建置／部署，工具尚不驗證完整 DLL 依賴閉包。

封裝器要求 `tools/excel/LaunchExcel.vbs` 並將它複製到 package root。固定 binary target 名稱為 `QSanguoshaExcelBridge.exe`、`QSanguoshaExcelServer.exe`、`QSanguoshaXPServer.exe`。

## Static check

```powershell
python tools/excel/check.py --tier modern --vba-source excel/vba --manifest staging/excel-modern/release-manifest.json
```

這只檢查 VBA/API marker 與 manifest coverage，不啟動 Excel 或 runtime。
