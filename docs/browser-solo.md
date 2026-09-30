# Browser single-player offline package

此文件定義瀏覽器單人模式（Browser Solo）的 source checkpoint 與發布流程。
目標平台是 Windows 10/11 x64，使用 Chrome 或 Edge；交付物是完整 ZIP，內含
`StartGame.exe`、Web HTML/JavaScript、WASM、Lua/AI、圖片（`assets/`）與 `audio/`。
使用者雙擊 `StartGame.exe` 後，launcher 從 exe 所在目錄提供本機靜態檔案，固定
綁定 `127.0.0.1:9529` 並開啟預設瀏覽器。瀏覽器關頁代表該局結束；瀏覽器的
localStorage 本機偏好可保留。這條路徑不啟動、連接或代理外部
`qsanguosha_server`。

## 目前邊界

Qt 6.11.1 `wasm_multithread` 的 QtBase 與 WebSockets 已補齊，位於
`H:/Qt6111/6.11.1/wasm_multithread`；既有 native 與 singlethread kits 保留。
`cmake/QSanguoshaWebSolo.cmake` 要求 `QT_FEATURE_thread`，目前 configure 已通過。
Emscripten 版本基線是 4.0.7。單執行緒 CLI/client runtime 與新的多執行緒 Solo
server Worker 必須使用不同 build directory；pthread ABI 不能在同一 build tree
混用。Web client 的同源產物（HTML、rules runtime 與其 manifest/sidecar）也必須
來自同一組配對建置與發布目錄。

第一條 vertical slice 是 05P browser solo 對局。所有其他模式、完整互動覆蓋、
重連/部署矩陣與 Win10/11 Chrome/Edge 真機驗收仍是後續 gates，不能由 source
checkpoint 推定完成。

## Launcher build

`src/web-launcher/CMakeLists.txt` 建立 `qsanguosha_web_launcher`，要求 Windows
x64 MSVC、C++17、`WIN32` subsystem，並連結 `ws2_32`、`shell32`、`user32`、`ole32`；
`MSVC_RUNTIME_LIBRARY` 使用 static CRT。從 repository root 執行以下命令即可
產生待放入 ZIP 的 `StartGame.exe`：

```powershell
cmake -S src/web-launcher -B builds/web-launcher -G "Visual Studio 18 2026" -A x64
cmake --build builds/web-launcher --config Release --target qsanguosha_web_launcher
```

## Native rules export

Solo package 的 `--rules-bundle` 必須由相同來源建置的 native 匯出器產生。
桌面工作目錄含有 `etc/` 或未宣告 Lua 時，原生匯出會拒絕 `rules_content_unsupported`。先建立乾淨的 declared closure，保留原始
`lua/config.lua` bytes 與宣告順序，再於獨立 userdata 匯出；不可放寬原生檢查。
`--prepare-content` 支援目前產生器輸出的 literal `extension_names` 表格，
不執行任意 Lua；原生匯出仍是規則身份的最終判據。

> ⚠️ **匯出器已不在倉庫。** 原本的 `qsanguosha_rules_fixture_runner --export-rules-bundle`
> 隨 `720a8df`（2026-09-25，移除全部測試）一併刪除，現時沒有任何 target 能產生
> `native-rules-bundle.json`，因此完整 Solo 打包（`package-web-solo.py` 需要
> `--rules-bundle`）無法從乾淨 checkout 重現。要打包須先自 `720a8df^` 還原該 target
> （`cmake/QSanguoshaRulesFixtures.cmake` 與其 fixture support 原始碼），或另寫匯出器。
> 匯出時的環境要求不變：獨立 `QSAN_USER_DATA_ROOT`、清除 `LUA_*` 環境變數，並以
> `--asset-root` 指向上面的 declared closure。

```powershell
python tools/package-web-solo.py --prepare-content --asset-root . `
  --destination builds/browser-solo-artifacts/declared-content
```

五個 builtin Lua 檔案是 core identity 輸入，但 declared-v1 manifest 也可包含宣告的
`extensions/*.lua` 與 `lang/*.lua`；所有列出的檔案大小與 SHA-256 必須和實際發布
asset root 一致。`lua/ai/` 與 `lua/lib/middleclass.lua` 是發布給 AI 的內容，由
packaging script 另外收集；它們不改變 browser WASM 的 core rules identity。Mini/custom
劇本及其他非 Lua 劇本目前不在交付物，第一條 05P slice 不涵蓋它們。

## Web client and Solo WASM builds

先以單執行緒 kit 在獨立 build directory 建立 browser client rules runtime，再以
多執行緒 kit 在另一個 directory 建立 Solo server Worker。以下是對應目前 CMake
選項的範例；`QT_NATIVE` 是 native Qt host tools，`QT_WASM_SINGLE` 和
`QT_WASM_MULTI` 必須分別指向匹配的 Qt 6.11.1 kits：

```powershell
$env:EMSDK = "<emsdk-4.0.7>"
$env:EM_CONFIG = "$env:EMSDK/.emscripten"
$QT_NATIVE = "<Qt6.11.1-msvc2022_64>"
$QT_WASM_SINGLE = "<Qt6.11.1-wasm_singlethread>"
$QT_WASM_MULTI = "<Qt6.11.1-wasm_multithread>"

cmake -S . -B builds/web-wasm-single -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE="$QT_WASM_SINGLE/lib/cmake/Qt6/qt.toolchain.cmake" `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DQT_HOST_PATH="$QT_NATIVE" `
  -DQSAN_BUILD_GUI=OFF -DQSAN_BUILD_TUI=OFF `
  -DQSAN_BUILD_SERVER=OFF `
  -DQSAN_BUILD_WASM_WEB_CLIENT=ON `
  -DQSAN_BUILD_WASM_SOLO=OFF
cmake --build builds/web-wasm-single --target qsanguosha_client_wasm

cmake -S . -B builds/web-solo -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE="$QT_WASM_MULTI/lib/cmake/Qt6/qt.toolchain.cmake" `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DQT_HOST_PATH="$QT_NATIVE" `
  -DQSAN_BUILD_GUI=OFF -DQSAN_BUILD_TUI=OFF `
  -DQSAN_BUILD_SERVER=OFF `
  -DQSAN_BUILD_WASM_WEB_CLIENT=OFF `
  -DQSAN_BUILD_WASM_SOLO=ON
cmake --build builds/web-solo --target qsanguosha_solo_wasm
```

The Solo target uses `-sPTHREAD_POOL_SIZE=8` and
`-sPTHREAD_POOL_SIZE_STRICT=2`. Its post-build command seals the paired
`qsanguosha_solo_wasm.mjs`/`.wasm` with `tools/package-web-solo.py --seal-runtime
--solo-module <path-to-qsanguosha_solo_wasm.mjs>`. Do not reuse a single-thread
module or sidecar in the Solo package.

## Web artifact pairing and ZIP staging

For the production client runtime, package the three matching Web artifacts before
the normal Web frontend build:

```powershell
python tools/package-web-runtime.py `
  --module builds/web-wasm-single/web-wasm/RelWithDebInfo/qsanguosha_client_wasm.mjs `
  --destination web/public/rules
```

Then run the Web production build to produce `web/dist`; its `/rules/` files
must retain the generated `.mjs`, `.wasm` and `.bundle.json` pairing. The
`package-web-runtime.py` command validates and records the `.mjs`/`.wasm` pair and
the generated deployment sidecar; it does not produce an `.assets.json` file.
The final offline distribution is assembled by the exact interface of
`tools/package-web-solo.py`:

```powershell
# Use Node 20.19+ or 22.12+. These commands do not invoke the test suite.
Push-Location web
node node_modules/typescript/bin/tsc --noEmit
node node_modules/vite/bin/vite.js build
Pop-Location

python tools/package-web-solo.py `
  --web-dist web/dist `
  --asset-root . `
  --rules-bundle builds/browser-solo-artifacts/native-rules-bundle.json `
  --solo-module builds/web-solo/web-solo/RelWithDebInfo/qsanguosha_solo_wasm.mjs `
  --launcher builds/web-launcher/Release/qsanguosha_web_launcher.exe `
  --destination builds/browser-solo-artifacts/qsanguosha-solo
python -m zipfile -c builds/browser-solo-artifacts/qsanguosha-solo-win-x64.zip `
  builds/browser-solo-artifacts/qsanguosha-solo
```

The package script rejects mixed runtime hashes, unsafe/reparse content paths,
missing Lua/AI trees, media collisions and a nonempty destination. It writes
`StartGame.exe`, `solo/`, `rules/content/`, `assets/`, `audio/`,
`game-ui-config.json` and `README-offline.txt` into the distribution directory.

## Verification

Log rendering fails and the package lifetime gates are not measured; both remain known gaps.
