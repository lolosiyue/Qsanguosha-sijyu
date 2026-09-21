# Windows 建置與執行

本指南適用於現代 Windows x64 桌面版本。XP／Win7 x86 使用[獨立建置流程](windows-xp-legacy-build.md)。

## 工具與建置

安裝 Visual Studio 2026 C++ 工具鏈及 Qt 6.11.1 `msvc2022_64` kit。Windows preset 使用 CMake 4.2 以上；產生器、架構與建置目錄由 [CMakePresets.json](../CMakePresets.json) 的 `vs2026-x64` 定義。語言標準與各產品選項見 [CMakeLists.txt](../CMakeLists.txt) 的 `CMAKE_CXX_STANDARD`、`QSAN_BUILD_GUI`、`QSAN_BUILD_SERVER` 與 `QSAN_BUILD_TUI`。

在倉庫根目錄執行：

```powershell
$env:QTDIR = 'C:/Qt/6.11.1/msvc2022_64' # 改成已安裝的 kit 路徑。
cmake --preset vs2026-x64
cmake --build --preset release
```

後續增量建置只需執行第二條 CMake 命令。修改建置設定、來源清單或 Qt 路徑後重新 configure。Debug 使用 `cmake --build --preset debug`。

也可使用 [tools/build-cmake.ps1](../tools/build-cmake.ps1)，明確傳入 `-QtRoot`：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-cmake.ps1 `
    -Configuration Release -QtRoot $env:QTDIR
```

腳本的 `Configuration`、`QtRoot`、`CMakeExe`、`Deploy` 與 `FmodRuntime` 參數定義於檔案開頭；`Test-CMake42Plus` 檢查 CMake 版本。

<a id="runtime-deployment"></a>
## 執行期部署與內容

Release 部署需要 `fmodex64.dll` 的實際位置。以下路徑為佔位範例，須替換為持有使用權的 runtime：

```powershell
cmake --preset vs2026-x64 -DQSAN_FMOD_RUNTIME=C:/path/to/fmodex64.dll
cmake --build --preset deploy-release
Push-Location release
try { .\QSanguosha.exe } finally { Pop-Location }
```

`deploy` 目標與 `QSAN_FMOD_RUNTIME` 定義在 [CMakeLists.txt](../CMakeLists.txt)，部署步驟由 [cmake/Deploy.cmake](../cmake/Deploy.cmake) 執行。內容包布局與外部素材設定見[套件指南](package-modularity.md)。

Debug GUI 使用 `deploy-debug`。伺服器與 TUI 的 `deploy-server` 目標只部署 Lua；直接執行時需讓同一套 Qt 的 `bin` 位於程序 `PATH`。輸出目錄由 `RUNTIME_OUTPUT_DIRECTORY_DEBUG`／`RUNTIME_OUTPUT_DIRECTORY_RELEASE` 定義，分別為 `debug/` 與 `release/`。

```powershell
$savedPath = $env:PATH
try {
    $env:PATH = "$env:QTDIR\bin;$env:PATH"
    .\debug\qsanguosha_server.exe --help
} finally {
    $env:PATH = $savedPath
}
```

伺服器設定見 [server.ini.example](server.ini.example)；終端操作見 [TUI 指南](tui-client.md)。
