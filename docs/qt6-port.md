# Qt 6 移植紀錄 (Qt 6 Port)

> **現行建置（2026-08-18）**：Windows CMake 已改為 Visual Studio 2026 v145 + Qt 6.11.1 `msvc2022_64`（`H:\Qt6111\6.11.1\msvc2022_64`），preset `vs2026-x64`。下文為 Qt 6.5.3／VS 2019 過渡切片的歷史紀錄，不得當作現行規範。

## 狀態

| 項目 | 結果 |
|---|---|
| 正式分支 | `qt6-port` |
| Qt | 6.5.3 `mingw_64`、6.5.3 `msvc2019_64` |
| 編譯器 | MinGW-w64 11.2.0 x64、MSVC 19.29 x64 |
| IDE | Visual Studio 2019 + Qt VS Tools 3.5.0；Visual Studio 2026 可作編輯器 |
| 建置系統 | qmake 過渡入口；CMake／Lua 5.4 不屬於本次相容切片 |
| 相容策略 | Qt 6-only；不保留 `QT_VERSION` 判斷或 Qt 5 API 分支 |
| Qt 6 Release | 編譯及連結成功 |

來源參考為 `doom` 分支，但所有修改均逐項適配目前架構，未直接合併或批量 cherry-pick。`include/` 與 `lib/` 內的第三方檔案沒有修改。

## 建置

### Visual Studio 2019／MSVC（建議的 Windows 人工建置）

已驗證的 Qt kit：

```text
C:\Qt\6.5.3\msvc2019_64\bin\qmake.exe
```

Visual Studio 2019 內設定一次：

1. 開啟 `Extensions > Qt VS Tools > Qt Versions`。
2. 選擇 `Add`，名稱填入 `Qt-6.5.3-msvc2019_64`。
3. `Location` 選擇 `C:\Qt\6.5.3\msvc2019_64\bin\qmake.exe`，並設為預設版本。
4. 使用 `Extensions > Qt VS Tools > Open Qt Project File (.pro)` 開啟 `QSanguosha.pro`，組態選擇 `Release | x64`。

也可用已驗證腳本產生 `.vcxproj` 並由 MSBuild 編譯：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-qt6-msvc.ps1 `
  -SwigExe <SWIG-4.3.1路徑> `
  -BuildDir <無空白的隔離建置目錄>
```

腳本預設使用 `C:\Qt\6.5.3\msvc2019_64` 與 Visual Studio 2019 Build Tools，產生 `QSanguosha.vcxproj` 及 `release\QSanguosha.exe`。如需可攜式開發部署，再加入：

```powershell
-Deploy -FmodRuntime <fmodex64.dll>
```

MSVC 版 FreeType 已由既有 `lib\win\x64\freetype.lib` 靜態連結；執行時只需另外提供 `fmodex64.dll`，其餘 Qt／QML／MSVC runtime 由 `windeployqt` 部署。

### MinGW

使用 PowerShell 執行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-qt6.ps1 `
  -SwigExe <SWIG-4.3.1路徑> `
  -LegacyLibDir <MinGW64匯入函式庫目錄> `
  -BuildDir <無空白的隔離建置目錄>
```

`LegacyLibDir` 必須提供下列 x64 匯入函式庫：

| 檔案 | 用途 |
|---|---|
| `libfmodex.a`、`libfmodexL.a` | FMOD Ex |
| `libfreetype.a`、`libfreetype_D.a` | FreeType |

腳本會將匯入函式庫複製到隔離建置目錄，不會寫入倉庫的 `lib/`。因來源路徑含空白，腳本會建立指向倉庫的無空白 Junction，避免舊 qmake 規則拆錯路徑。

若需建立本機可啟動的開發部署，再加入：

```powershell
-Deploy -FmodRuntime <fmodex64.dll> -FreeTypeRuntime <libfreetype-6.dll>
```

Qt 6.5.3 MinGW kit 的 `windeployqt` 會把 Release 外掛誤判為 Debug，並回報找不到 platform plugin；MinGW 建置腳本因此使用明確的 Release DLL、外掛及 QML 模組部署清單。MSVC kit 的 `windeployqt` 已驗證可正常部署。

## 主要相容修正

| 區域 | 修正 |
|---|---|
| QtCore | `QTextStream::setEncoding`、`Qt::SkipEmptyParts`、`QMetaType`、`QMultiMap::insert`、`QRecursiveMutex` |
| QtGui／Widgets | `QEnterEvent`、`QBitmap::fromPixmap`、靜態 `QFontDatabase`、`horizontalAdvance` |
| Network | 直接使用 `QAbstractSocket::errorOccurred` |
| QML／Quick | 無版本匯入、載入前注入 context property、延後啟動動畫 |
| JSON | 嚴格區分陣列／物件，補齊 64 位整數判定 |
| SWIG | `QList::swapItemsAt`，wrapper 僅由 SWIG 4.3.1 產生 |
| 無頭模式 | Lua 錯誤輸出至標準錯誤，不建立 `QMessageBox` |

## 驗證結果

| 驗證 | 結果 |
|---|---|
| Qt 6 Release 全量建置 | 通過，退出碼 0 |
| Qt 6.5.3 MSVC 2019 x64 Release | 通過；`QSanguosha.exe` SHA-256 `3664FA4F0CF3797F39E30E8282927B1029D7302B2A8E7995D54B48E7133A2C2D` |
| `QSanguosha.exe -manual` | 完整載入所有包，退出碼 0 |
| `skill-instance-utils-test.exe` | `skill-instance-utils tests passed` |
| MSVC GUI `QT_QPA_PLATFORM=offscreen` | 8 秒內持續執行且 `Responding=True`，無 QML 載入錯誤 |

## 尚待人工驗收

- 在實體 Windows 顯示環境逐一觸發技能 QML、Spine、GIF 與音訊，確認視覺及透明疊加效果。
- `lua/test/examples/test_basic.lua` 目前會進入測試牌局但不自行結束；需另行把互動式案例改為有完成條件的自動測試。
- QtCore5Compat 的 `QLinkedList` 在 MinGW 11.2 會產生 `free-nonheap-object` 警告；現階段保留相容層，後續應遷移至 `QList`。
- CMake、Lua 5.4、Linux／Android 與正式安裝包仍屬跨平台現代化計劃的後續里程碑。
