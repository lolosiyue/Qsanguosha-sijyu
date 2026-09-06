# Linux Development Environment

Linux 交付三個產品：**無頭伺服器 (headless server)** `qsanguosha_server`、**GUI client** `QSanguosha`，以及 **Protocol V2 終端客戶端 (TUI client)** `qsanguosha_tui`。三者由同一份 `CMakeLists.txt` 產生，用 `QSAN_BUILD_SERVER`／`QSAN_BUILD_GUI`／`QSAN_BUILD_TUI` 三個 option 分別開關（TUI 見 [`docs/tui-client.md`](tui-client.md)）。

Server 使用 `QCoreApplication`，不需要 X11／Wayland、FMOD 或任何 GUI／Qt Widgets／Quick／Multimedia 依賴；GUI client 則需要完整 Qt6 Widgets／Quick／Multimedia 與系統 Freetype；TUI client 與 server 一樣只用 Qt Core／Network，沒有任何 GUI 依賴。

## 分階段狀態

| 階段 | 狀態 |
|---|---|
| Linux Server（build／CI／三級 TCP network integration／systemd） | **Complete** |
| Linux GUI M0（configure ＋ compile ＋ link） | **Complete** — 本機驗證；沒有獨立 Linux GUI compile CI |
| Linux GUI M1（GUI startup：`QApplication`／`MainWindow`／HomeScene／event loop） | **Complete** — `linux-package-ci.yml` 由成品在 Xvfb ＋ `xcb` 跑 `--ui-startup-smoke`；WSLg 手動驗證 |
| Linux GUI M2（RoomScene 真實 TCP 對局） | **Complete** — 由 `gui_network_smoke.py` 在**資產齊全的本機**驗證；不在 CI 跑（見 [§4.6](#46-linux-gui-m2-network-smoke真實-tcp-對局)） |
| Linux GUI M2B-A（Qt multimedia：短音效／語音／BGM／影片背景降級） | **Complete** — `linux-package-ci.yml` 由成品跑 `--multimedia-smoke`（見 [§4.7](#47-linux-gui-m2b-a-multimedia-smoke)） |
| Linux GUI M2B-B（效果 profile：Spine／GIF／動畫降級） | **Complete** — `linux-package-ci.yml` 由成品跑 `--effects-smoke` full／reduced／none 三個 profile（見 [§4.8](#48-linux-gui-m2b-b-effects-smoke)） |
| Linux packaging（portable tar.zst／AppImage／desktop entry） | **Complete**（M3）；`.deb` 延後至 M3.1 |

M0 的定義固定為 **configure ＋ compile ＋ link**，加上一個 binary capability smoke。
M1 的定義固定為 **真正執行完一次 GUI startup path 然後自動正常退出**。
M2B-A 的定義固定為 **audio backend 與 QML media component 建立得起、能接收 media
source、缺資產／缺裝置有明確降級、清理得乾淨**；它**不包括**「真的聽到聲音」——
CI runner 沒有音訊裝置。
M2B-B 的定義固定為 **一個 client 三個效果 profile（full／reduced／none）執行同一
條集中 policy、動畫 completion 保證 exactly once、缺／壞資產降級成靜態 UI、
NONE 不建立 Spine／QMovie／video object**；它**不包括**「畫面看起來一樣」——
CI runner 沒有正式美術資產，pixel diff 不會做 blocking gate。

以下全部 **未完成**，不在 M0／M1／M2／M2B-A／M2B-B 範圍：

```text
Linux .deb packaging（M3.1）
Android／直版 UI／WASM
```

> ⚠️ `--local-response-ui-capabilities` 在建立 `QApplication` 之前就直接回傳 JSON，所以它是
> **binary capability smoke**，不是 GUI／offscreen startup smoke。真正的 `QApplication`／
> `MainWindow`／`HomeScene` 啟動驗證是 M1 的 `--ui-startup-smoke`（見 [§4.5](#45-linux-gui-m1-startup-smoke)）。

- Status: Linux Server Complete；Linux GUI M0（configure／compile／link）Complete；Linux GUI M1（GUI startup）Complete；Linux GUI M2（network game）Complete；Linux GUI M2B-A（multimedia）Complete；Linux GUI M2B-B（effects profiles）Complete
- Last Updated: 2026-09-06
- 對應 Windows 開發環境請見 [`README.md`](../README.md) 的 🛠️ Development Environment section。

## 1. 平台基線

| 項目 | 基線 |
|---|---|
| 平台 | Ubuntu 24.04 x64（CI 基線；本機亦驗證過 Ubuntu 26.04，其他 Linux distro 一般相容） |
| C++ | C++17（`CMAKE_CXX_STANDARD 17`） |
| Qt | Server：distro Qt 6.x（`qt6-base-dev`，Ubuntu 24.04 為 6.4.2）<br>GUI：**Qt 6.11.1**，與 Windows 同一 baseline |
| Lua | 內建於 `src/lua/`（SWIG 自動生成 binding） |
| 建置系統 | CMake 4.2+（`CMakeLists.txt` 下限是 3.28）、Ninja |
| Generator | Ninja（本機 `build-linux-gcc/`、`build-linux-clang/` 均用 Ninja） |
| 編譯器 | GCC（`/usr/bin/c++`）或 Clang（`/usr/bin/clang++`） |

> **Qt baseline 分層**：GUI client 在 Windows 與 Linux 都要求 Qt **6.11** 或以上
> （`QSAN_QT_GUI_MINIMUM_VERSION`），因為 GUI source 用了 Qt 6.5+ 才有的 API
> （例如 `QStyleHints::colorScheme()`），而 Windows 正式 toolchain 已經是
> Qt 6.11.1 `msvc2022_64`。
>
> dedicated server **不受這個下限限制** — server-only configure（`QSAN_BUILD_GUI=OFF`）
> 只要 CMake 找到 Qt6 Core／Network／WebSockets 就能編譯，可以繼續用 distro Qt（Ubuntu 24.04 的
> 6.4.2 亦可）。Linux Server CI 就是這樣執行。

## 2. 系統依賴

### 2.1 無 GUI build（`QSAN_BUILD_GUI=OFF`）

Ubuntu / Debian：

```bash
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    ninja-build \
    qt6-base-dev \
    qt6-websockets-dev \
    swig
```

對應套件包：

- **build-essential** — `gcc`／`g++`、`make` 等
- **cmake** — 建置系統
- **ninja-build** — Ninja generator（快速）
- **qt6-base-dev** — Qt6 Core／Network head 與 library（`/usr/lib/x86_64-linux-gnu/cmake/Qt6*`）
- **qt6-websockets-dev** — Qt6 WebSockets（dedicated server 與 GUI 內嵌 server 的 WS 閘道）
- **swig** — 生成 C++／Lua binding

這個清單同時 cover 預設開啟的 TUI client（`qsanguosha_tui` 只用 Qt Core／Network，無需額外套件）。

> 舊版指南曾要求 `qt6-5compat-dev`（CTest 測試用的 Qt6 Core5Compat）；Core5Compat
> 已自建置移除，現在 `BUILD_TESTING=ON` 都只是額外找 `Qt6::Gui`，這個套件已不需要。

### 2.2 GUI client（`QSAN_BUILD_GUI=ON`）

GUI 要求 **Qt 6.11 或以上**。distro apt 的 Qt 版本要視乎發行版：

| 發行版 | apt Qt 版本 | 是否滿足 GUI？ |
|---|---|---|
| Ubuntu 24.04 LTS | 6.4.2 | ✗ 太舊，要另外裝 Qt 6.11.1 |
| Ubuntu 26.04 LTS | 6.10.2 | ✗ 差一點，要另外裝 Qt 6.11.1 |

先裝非 Qt 的系統依賴（任何發行版都要）：

```bash
sudo apt install -y \
    libfreetype-dev \
    libgl-dev
```

再用 `aqtinstall` 取得官方 Qt 6.11.1（不需要 root，與 CI 完全一致）：

```bash
python3 -m venv ~/.venvs/aqt
~/.venvs/aqt/bin/pip install \
    'git+https://github.com/miurahr/aqtinstall.git@16db45a70b5905ad596941b223469bc86a56901e'
~/.venvs/aqt/bin/aqt install-qt linux desktop 6.11.1 linux_gcc_64 \
    -m qtwebsockets qtmultimedia -O ~/Qt
```

> aqt 3.3.0（最新 release）未支援 Qt 6.11+ 的新 repo 目錄結構
> （[miurahr/aqtinstall#959](https://github.com/miurahr/aqtinstall/issues/959)），
> 所以固定到已合併修復的 commit — 與 `.github/workflows/ci.yml` 及
> `linux-package-ci.yml` 用同一個 pin。

configure 時指向該 Qt：

```bash
CMAKE_PREFIX_PATH=~/Qt/6.11.1/gcc_64 cmake --preset linux-gui-gcc-debug
```

如果你的發行版 apt 本身已經提供 Qt ≥ 6.11，亦可以直接裝 distro 套件代替 aqt：

```bash
sudo apt install -y \
    qt6-base-dev \
    qt6-declarative-dev \
    qt6-l10n-tools \
    qt6-multimedia-dev \
    qt6-tools-dev \
    qt6-tools-dev-tools \
    qt6-websockets-dev
```

按依賴類別對應：

| 類別 | Ubuntu 套件 | 供應的 CMake component |
|---|---|---|
| compiler / build | `build-essential`、`cmake`、`ninja-build` | — |
| SWIG | `swig` | — |
| Qt Base | `qt6-base-dev` / aqt base | `Core`、`Gui`、`Network`、`Widgets`、`OpenGLWidgets` |
| Qt WebSockets | `qt6-websockets-dev` / aqt `-m qtwebsockets` | `WebSockets` |
| Qt Declarative / QML / Quick | `qt6-declarative-dev`（另 `qt6-declarative-dev-tools` 提供 `qmlcachegen`／`qmltyperegistrar`，一般由前者引入）/ aqt base | `Qml`、`Quick`、`QuickControls2`、`QuickWidgets` |
| Qt Multimedia | `qt6-multimedia-dev` / aqt `-m qtmultimedia` | `Multimedia` |
| Qt Tools / LinguistTools | `qt6-tools-dev`、`qt6-tools-dev-tools`、`qt6-l10n-tools` / aqt base | `LinguistTools`（`lrelease`／`lupdate`） |
| OpenGL development | `libgl-dev`（連帶 `libglx-dev`） | `WrapOpenGL` |
| Freetype development | `libfreetype-dev` | `Freetype::Freetype` |

> 套件名在不同 Ubuntu／Debian repository 可能有出入，用 `apt-cache search qt6` 或 `apt-cache policy <package>` 先確認再安裝。

Linux **不需要** FMOD：`AUDIO_SUPPORT` 與 bundled `lib/win/x64/fmodex.lib` 只在 Windows Release 生效。Linux 亦不使用 `include/freetype` 內的 bundled Windows header，改用系統 `libfreetype-dev`。

如果想用 **Clang** 編譯，加：

```bash
sudo apt install -y clang
```

選用哪個 compiler 就在 configure 時將 `CMAKE_CXX_COMPILER` 指向對應的 path。

## 3. 產品選項（`QSAN_BUILD_GUI` / `QSAN_BUILD_SERVER`）

同一份 `CMakeLists.txt` 用兩個 option 決定要 build 哪個產品：

| Option | Windows 預設 | Linux 預設 | 說明 |
|---|---|---|---|
| `QSAN_BUILD_GUI` | `ON` | `OFF` | GUI client `QSanguosha` |
| `QSAN_BUILD_SERVER` | `ON` | `ON` | dedicated server `qsanguosha_server` |
| `QSAN_BUILD_TUI` | `ON` | `ON` | Protocol V2 終端客戶端 `qsanguosha_tui`（見 [`docs/tui-client.md`](tui-client.md)） |

Linux 的 `QSAN_BUILD_GUI` 預設 `OFF` 是為了保護現有 Linux Server CI：server-only configure 只會 `find_package` Core／Network／WebSockets（加 `BUILD_TESTING=ON` 時的 `Gui`），不會因為 GUI source 存在而要求 Quick／Widgets／Multimedia。TUI 預設 `ON`，與 server 一起編譯，同樣只需要 Core／Network。要 build Linux GUI 就顯式開 `-DQSAN_BUILD_GUI=ON`（下面的 preset 已經設定好）。Windows XP／Qt 5.6.3 的 `QSAN_BUILD_XP_LEGACY` 會從 `QSAN_QT_COMPONENTS` 拿掉 WebSockets，引擎亦不編 `websocketsocket.cpp`。

`BUILD_TESTING=ON` 需要 `QSAN_BUILD_SERVER=ON`（CTest 直接驅動 `qsanguosha_server`），CMake 會在 configure 階段 `FATAL_ERROR` 提示。

## 4. Configure + Build

### 4.1 Preset（建議）

`CMakePresets.json` 提供 Linux preset（`condition` 限定 `hostSystemName == Linux`，不會影響 Windows 的 `vs2026-x64`／`debug`／`release`／`deploy-*`）：

| Configure preset | Build preset | binaryDir | 產品 |
|---|---|---|---|
| `linux-server-gcc-debug` | `linux-server-debug` | `builds/cmake-linux-server-gcc-debug` | server + TUI + CTest |
| `linux-gui-gcc-debug` | `linux-gui-debug` | `builds/cmake-linux-gui-gcc-debug` | GUI + server + TUI + CTest |

Linux GUI（`CMAKE_PREFIX_PATH` 指向 Qt 6.11.1，見 [2.2](#22-gui-clientqsan_build_guion)）：

```bash
CMAKE_PREFIX_PATH=~/Qt/6.11.1/gcc_64 cmake --preset linux-gui-gcc-debug
cmake --build --preset linux-gui-debug --parallel
```

如果 distro Qt 本身已經 ≥ 6.11，可以省略 `CMAKE_PREFIX_PATH`。Qt 版本不足時
configure 會直接失敗並列出找到的版本，不會等到 compile 階段才失敗。

Linux server：

```bash
cmake --preset linux-server-gcc-debug
cmake --build --preset linux-server-debug --parallel
ctest --test-dir builds/cmake-linux-server-gcc-debug --output-on-failure
```

### 4.2 直接執行 CMake（GCC）

```bash
cmake -S . -B build-linux-gcc -G Ninja \
    -DBUILD_TESTING=ON \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=/usr/bin/gcc \
    -DCMAKE_CXX_COMPILER=/usr/bin/g++
cmake --build build-linux-gcc
```

加 `-DQSAN_BUILD_GUI=ON` 就會連 `QSanguosha` 一起 build。

### 4.3 直接執行 CMake（Clang）

```bash
cmake -S . -B build-linux-clang -G Ninja \
    -DBUILD_TESTING=ON \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=/usr/bin/clang \
    -DCMAKE_CXX_COMPILER=/usr/bin/clang++
cmake --build build-linux-clang
```

### 4.4 Build 完成後的 output

`qsanguosha_server`、`qsanguosha_tui` 與 `QSanguosha` 都會按 build type 生成在 source tree 的 `debug/`、`release/` 或 `relwithdebinfo/`（例如 Debug 是 `debug/qsanguosha_server`、`debug/qsanguosha_tui`、`debug/QSanguosha`）。

驗證 Linux GUI executable：

```bash
file debug/QSanguosha
ldd debug/QSanguosha | grep 'not found'   # 應該沒有輸出
```

M0 的 binary capability smoke：

```bash
./debug/QSanguosha --local-response-ui-capabilities
# {"schema_version":1,"auto":true,"show":true,"inspect":true}
```

> 這個 flag 在建立 `QApplication` 之前就回傳，所以毋須 `QT_QPA_PLATFORM=offscreen`，
> 亦不算 GUI startup 驗證。以上全部只驗證 configure／compile／link 與 binary 可執行。
> 真正的 GUI 啟動驗證見下面 4.5。

## 4.5 Linux GUI M1 startup smoke

`--ui-startup-smoke` 是 M1 的自動化啟動驗證。與 `--local-response-ui-capabilities`
最大的分別是：它**不會**在 `QApplication` 之前 return，而是完整執行產品正常的啟動路徑：

```text
QApplication → Engine/runtime → MainWindow → HomeScene/QML → Qt event loop
    → ready condition → 自動正常退出
```

它不會另外複製一份假的 HomeScene 啟動流程：MainWindow 照常 `setupHomePage()`
載入 `qrc:/QSanguosha/Home/HomeScene.qml`，smoke 只是透過 MainWindow 公開的
`homeSceneReady()`／`homeSceneFailed()` signal 觀察結果。

```bash
# 可見桌面（WSLg／X11／Wayland）
./debug/QSanguosha --ui-startup-smoke

# 明確指定 platform plugin 與 app 內部 timeout，並且輸出完整 JSON report
QT_QPA_PLATFORM=xcb ./debug/QSanguosha \
    --ui-startup-smoke \
    --ui-startup-timeout-ms 30000 \
    --ui-startup-report /tmp/ui-startup.json

# 完全沒有 X server（次要驗證，不可以當作 M1 的唯一證據）
QT_QPA_PLATFORM=offscreen ./debug/QSanguosha --ui-startup-smoke
```

首頁 render host 的開發用 A/B 可加以下參數；預設仍是 `widget`：

```bash
./debug/QSanguosha --ui-startup-smoke --home-render-host=widget
./debug/QSanguosha --ui-startup-smoke --home-render-host=view
```

report 的 `home_scene.render_host`、`root_width`、`root_height` 可確認實際 host
與有效畫布尺寸。`view` 使用 `QQuickView`＋`createWindowContainer()`，只影響首頁；
進房後既有 `QGraphicsView` 路徑不變。

### Stage 與結果 marker

每個 stage 一行 `UI_STARTUP_STAGE`，最後一定有一行 `UI_STARTUP_RESULT`：

```text
UI_STARTUP_STAGE {"schema_version":1,"stage":"application","ok":true,...}
UI_STARTUP_STAGE {"schema_version":1,"stage":"engine","ok":true,...}
UI_STARTUP_STAGE {"schema_version":1,"stage":"main_window","ok":true,...}
UI_STARTUP_STAGE {"schema_version":1,"stage":"event_loop","ok":true,...}
UI_STARTUP_STAGE {"schema_version":1,"stage":"home_scene","ok":true,...}
UI_STARTUP_STAGE {"schema_version":1,"stage":"shutdown","ok":true,...}
UI_STARTUP_RESULT {"schema_version":1,"ok":true,"stage":"shutdown","reason":"ok","exit_code":0,...}
```

失敗時 `ok` 為 `false`，`stage` 指出失敗在哪一步，`reason` 分辨 `stage_failed`
與 `timeout`，並帶 `error` 文字。任何退出路徑（包括舊有 `exit(1)`）都會補一行
result marker，所以 CI 可以將「marker 缺失」直接當失敗。

### Ready condition

`home_scene` stage 不是靠 `QTimer::singleShot(0, quit)` 就算數，要同時成立：

| 條件 | 判定 |
|---|---|
| `QApplication` 已建立 | `qobject_cast<QApplication *>(qApp)` |
| Engine/runtime 已就緒 | `Sanguosha != nullptr`，回報版本與武將數 |
| `MainWindow` 已建立並顯示 | `isVisible()` ＋ `windowHandle() != nullptr` |
| HomeScene/QML 已載入 | 選定 host 回報 `Ready`，且 `rootObject() != nullptr`、尺寸大於零 |
| event loop 真的執行過 | 進入 `exec()` 之後的 queued callback |
| top-level GUI object 經得起 startup | 再執行 250ms event loop 後 MainWindow 與 QML root 仍然生存 |

### Exit code

| Exit code | 意思 |
|---|---|
| `0` | 全部 stage 通過並正常退出 |
| `1` | `QApplication`／engine／`MainWindow` 建立失敗 |
| `2` | HomeScene／QML component 載入失敗 |
| `3` | app 內部 timeout |
| `4` | `--ui-startup-*` 參數不合法 |
| `5` | 內部錯誤 |

### Timeout：兩層保護

| 層 | 機制 |
|---|---|
| App 內部 | `--ui-startup-timeout-ms`（預設 15000，範圍 100–120000）。同步階段亦會主動比對 deadline，因為 `QTimer` 在 `exec()` 之前不會觸發。 |
| Runner 外部 | `tools/ci/linux-gui-startup-smoke.sh` 的 `timeout --kill-after`，防止 Qt event loop 完全 hang 死。結束前會清除自己 process group 內剩餘的 `QSanguosha`／`Xvfb`。 |

### 缺少 optional 美術資源

Clean checkout **沒有入庫** `qml/home/icons/`、`image/system/backdrop/` 等 optional
美術資源，所以啟動時會見到一批 `QML Image: Cannot open: ...` warning。這些會被
分類為 optional asset warning 記錄到 report，**不會**升級成 fatal——真正的 QML
component 失敗是由選定 host 的 `Error` 狀態判定，兩者不會混淆。

### CI／本機一鍵驗證

```bash
# Xvfb + xcb（CI 的主驗證）
bash tools/ci/linux-gui-startup-smoke.sh ./relwithdebinfo/QSanguosha artifacts \
    --platform xcb --label xvfb-xcb

# 可見桌面（WSLg），不開 Xvfb
bash tools/ci/linux-gui-startup-smoke.sh ./debug/QSanguosha artifacts \
    --no-xvfb --platform xcb --label wslg
```

## 4.6 Linux GUI M2 network smoke（真實 TCP 對局）

> ⚠️ **這個 smoke 不在 CI 跑，是本機 gate。** runner 沒有美術／音訊資產，缺資產
> 之下 client 與 server 打完一局會一起 SIGSEGV（已實測：同一個 binary 在資產齊全
> 的本機 8/8 PASS）。Windows 環境有同樣問題。所以這個 runner 保留、但要在
> **資產齊全** 的本機執行，不會 gate PR；這個政策的正式記錄在 `linux-package-ci.yml`
> 頂部註解。

M1 證明 GUI **能夠啟動**；M2 證明 GUI **能夠遊玩**：一個獨立的 Linux
`qsanguosha_server` process、一個獨立的 Linux GUI client process、中間執行真正的
TCP，走完 連線 → signup → RoomScene/Dashboard → 選將 → 出牌／askFor → game over
→ 正常離開。

### 模式 ID（以 registry 為準）

M2 用 registry 真正註冊過的身分模式，不靠猜：

| 用途 | mode ID | 人數 | 出處 |
|---|---|---|---|
| 2 人局 | `02p` | 2 | `src/core/engine.cpp` `modes.insert("02p", ...)`；`qsanguosha_server --list-game-modes` 亦會列出 |
| 5 人局 | `05p` | 5 | `src/core/engine.cpp` `modes.insert("05p", ...)`；同上 |

想自己確認：

```bash
./relwithdebinfo/qsanguosha_server --list-game-modes | grep -E '^(02p|05p)\b'
```

### `--network-ui-smoke`

client 端的入口。**不會**改變正常玩家啟動：所有測試行為都要顯式 flag。

| Flag | 意思 |
|---|---|
| `--network-ui-smoke` | 啟用；同時要有 `-connect:<host>[:<port>]` 才有 Client 可以觀察 |
| `--network-ui-smoke-result <path>` | 寫下完整 JSON report（stages／responder 統計／失敗時的最後 UI state） |
| `--network-ui-smoke-timeout-ms <ms>` | app 層總 timeout（預設 600000） |
| `--network-ui-smoke-stall-ms <ms>` | 單一 request 未經 UI 回覆的容忍時間，超過就切換 trustee 並記錄（預設 20000） |
| `--network-ui-smoke-screenshot <path>` | 失敗時擷取一張 PNG 作診斷（不是 pixel gate） |

這個入口是**觀察者**，不是第二套流程：MainWindow、Client、RoomScene、Dashboard
全部是產品自己的，smoke 只是接產品已有的訊號
（`Client::socket_connected` / `server_connected` / `game_started` / `game_over`、
`MainWindow::roomSceneCreated`），再由 `NetworkUiSmokeResponder` 代替滑鼠去按真正
被 enable 的 `CardItem`／`Photo`／`QSanButton`，最後執行 RoomScene 自己的
`doOkButton()`／`doCancelButton()`／`doTimeout()` 把回覆送回 server。

Responder **不會**自己建構 protocol packet。遇到 M2 未特別處理的互動形態，會採用
RoomScene 自己的安全預設回覆（`doTimeout()`）；真的卡住超過 `--stall-ms` 才切換
trustee，而且一定會在 report 記下 `trustee_fallback`，不會假裝成正常路徑。

### Stage 與結果 marker

```
NETWORK_UI_STAGE {"schema_version":1,"stage":"connected","ok":true,...}
NETWORK_UI_STAGE {"schema_version":1,"stage":"signed_up","ok":true,...}
NETWORK_UI_STAGE {"schema_version":1,"stage":"room_scene","ok":true,...}
NETWORK_UI_STAGE {"schema_version":1,"stage":"dashboard","ok":true,...}
NETWORK_UI_STAGE {"schema_version":1,"stage":"general_selected","ok":true,...}
NETWORK_UI_STAGE {"schema_version":1,"stage":"game_started","ok":true,...}
NETWORK_UI_STAGE {"schema_version":1,"stage":"game_over","ok":true,...}
NETWORK_UI_STAGE {"schema_version":1,"stage":"shutdown","ok":true,...}
NETWORK_UI_RESULT {"schema_version":1,"ok":true,"stage":"shutdown","exit_code":0,"reason":"ok",...}
```

次序與任務書列出的稍有不同，是**刻意**跟產品真實流程：RoomScene 在 client 收到
setup 之後立即由 `MainWindow::enterRoom()` 建立，早於選將請求，所以
`room_scene`／`dashboard` 排在 `general_selected` 之前。

### Exit code：每種故障有自己的編號

| Exit code | 意思 |
|---|---|
| 0 | PASS |
| 1 | 參數不合法 |
| 2 | 連不上（server 未啟動／port 錯） |
| 3 | signup／setup 未完成 |
| 4 | RoomScene 未建立 |
| 5 | Dashboard 未建立 |
| 6 | 選將請求未回覆 |
| 7 | 未開局 |
| 8 | askFor 無法經 UI 回覆 |
| 9 | 已開局但沒有 game over |
| 10 | 局中被 server 斷線 |
| 11 | app 內部總 timeout |
| 12 | 其他內部錯誤 |

`reason` 欄再分辨 `stage_failed` / `timeout` / `disconnected` /
`interaction_stalled`。client crash 與 shutdown hang 沒有 result marker，由 runner 靠
exit code（POSIX 訊號）判定。

### Runner：`tools/autotest/gui_network_smoke.py`

```bash
# 本機（WSLg，用現有 DISPLAY）
python3 tools/autotest/gui_network_smoke.py \
    --exe-root . --mode 02p --seed 20260828 \
    --artifact-dir gui-network-artifacts --no-xvfb --platform xcb

# CI（Xvfb）
python3 tools/autotest/gui_network_smoke.py \
    --exe-root . --mode 05p --seed 20260828 \
    --artifact-dir gui-network-artifacts --xvfb --platform xcb
```

Runner 負責：

- 借一個**空閒 TCP port**（平行 CI job 不會衝突）
- 用**固定 seed**，並且在 summary 記下 mode／seed／port／server 與 client 的
  SHA-256／extensions commit／timeout 設定
- 寫一份確定性的 server INI overlay（關掉 RandomSeat／雙將／作弊／幸運牌），
  不靠開發機留下的 `config.ini`
- 兩層有界 timeout：`--client-timeout-ms`（app 層）＋ `--process-timeout`（runner 層）
- 成功與失敗路徑都執行同一條清理：graceful shutdown → terminate → kill，之後確認
  沒有孤兒、port 已釋放
- **沒有任何 retry**。一局就是一局，不會跑到偶然 PASS 為止。

`--require-interactions` 可以要求某些互動一定要經真 UI 覆蓋過（預設
`choose_general,play_phase`）。CI 只 gate `choose_general`，因為對局如何展開受
server 端 AI 影響，其餘覆蓋率照樣寫入 artifact 供檢視。

跨平台部分（執行檔定位、process group spawn、process-tree 清理、exit code 解讀、
空閒 port）抽出到 `tools/autotest/runner_common.py`，`network_runner.py` 與
`gui_network_smoke.py` 共用；`network_runner.py` 亦因此在 Linux 可執行。

### 已知的 base 缺陷:`server-teardown-crash`

M2 的 runner 找到一個**與本分支無關**的 server 缺陷,並且刻意不隱藏它:

> 對局打完、client 正常離開之後,`qsanguosha_server` 在拆房時
> SIGSEGV/SIGABRT。

Backtrace(以 `LD_PRELOAD` 掛一個 `backtrace()` handler 取得,再用 `addr2line`
還原):

```
Room::~Room()                              src/server/room.cpp:243
  → GameSnapshotService::~GameSnapshotService()   src/server/game-snapshot-service.cpp:15
    → GlobalSnapshot::~GlobalSnapshot()           src/util/game-snapshot.h:54
      → QMap<QString, QVariant>::~QMap()
        → CardUseStruct::~CardUseStruct()         src/server/roomthread.cpp:278
          → QSharedPointer<Card> deref → Card::deleteLater()   src/core/card.cpp:66
            → CardLifetimeManager::observeCard()  src/core/card-lifetime-manager.cpp:208
              → QObject::thread()   ← SIGSEGV(Card 已經被釋放)
```

也就是 snapshot 內的 `CardUseStruct` 活得比它引用的 `Card` 久。

三重對照,證明與 M2 無關:

1. 本分支改過的檔案中,**沒有一個**會編入 `qsanguosha_engine` 或
   `qsanguosha_server`(唯一能進入 server-only build 的是兩個 CTest 專用檔案)。
2. 用 M1 merge base(`50e5750`)編譯出的 `qsanguosha_server` 配同一個 client,
   一樣重現同一個 SIGSEGV。
3. 完全不用 `--network-ui-smoke`、改用舊有 `--auto-robots` 托管流程,一樣重現。

所以 runner 有一個**明確而且有界**的降級開關:

```bash
python3 tools/autotest/gui_network_smoke.py ...     --known-base-defect server-teardown-crash
```

這個開關**不是**靜音開關:

* 崩潰照樣偵測、照樣列印(`KNOWN BASE DEFECT (downgraded, still recorded)`)、
  照樣寫入 `summary["known_base_defects"]`;
* 只有在 server 已經寫出**帶勝方的 game over**、而且 client 已經 **exit 0**
  之後發生的 server 崩潰才會被降級。對局途中死掉的 server 永遠是失敗;
* 缺陷 id 是一個封閉清單(`KNOWN_BASE_DEFECTS`),加一個新 id 是一次要 review
  的改動;
* 復原條件:card-lifetime / GameSnapshot 的擁有權修好之後,在 CI 拿走這個
  flag 即可。

### 已知的 5 人局 client 繪製崩潰(暫時非阻擋)

`05p` 的 GUI client 會在對局途中 SIGSEGV,backtrace 全部落在 Qt Widgets 的
`QGraphicsView::paintEvent` → `QGraphicsScene` 繪製路徑,沒有任何 QSanguosha frame。

已知邊界:

* `02p` 用同一條 responder 路徑**不會**重現 → 不是 responder 本身的邏輯問題;
* 同一個 client、改用舊有 `--auto-robots` 托管流程(完全不經 UI responder)
  **不會**重現 → 要有真實 UI 互動才觸發;
* 也就是 5 人版面特有的繪製問題,不屬於 M2 的修復範圍。

M2 的 network job 已經由 CI 移除（見 [§9.2](#92-linux-gui-驗證政策)），所以這個
繪製崩潰目前不會阻擋任何 CI job；在**資產齊全**本機執行 05p 對局仍然會遇到，
`02p` 不受影響。修好 5 人局 RoomScene 的繪製崩潰之前，05p 只適合作診斷用途。

### 素材

自動化測試在**沒有美術素材**的情況下跑（`image/`、`audio/`、`font/`、`hero-skin/`
不入庫）。缺素材只會產生 optional asset warning，不可以令 RoomScene、Dashboard、
選目標、網絡回覆或對局流程崩潰 — 這個是 M2 要證明的事項之一。

## 4.7 Linux GUI M2B-A multimedia smoke

M1 證明 GUI **能夠啟動**，M2 證明 GUI **能夠遊玩**，M2B-A 證明 GUI 的
**多媒體子系統建立得起、失敗能夠降級、關閉得乾淨**。

### Audio 架構

產品一直只有 `Audio` 一個 facade（`src/core/audio.h`）。M2B-A **沒有**另開第二套
facade，只是將實作交給 `IAudioBackend`（`src/ui/audio/audio-backend.h`）：

```text
Audio  ──►  IAudioBackend
              ├── FmodAudioBackend      Windows GUI Release（行為不變）
              ├── QtMediaAudioBackend   Linux GUI（Qt Multimedia）
              └── NullAudioBackend      dedicated server／CI／降級
```

選擇哪個 backend 只在兩個地方發生：CMake 的 `QSAN_AUDIO_BACKEND` 與
`src/ui/audio/audio-backend-factory.cpp`。call site 一個 `#ifdef Q_OS_LINUX`
都沒有。

| 產品 | 預設 | 備註 |
|---|---|---|
| Windows GUI Release | `FMOD` | bundled `fmodex.lib`，與以前完全一樣 |
| Windows GUI Debug | `FMOD`（實際為 null） | FMOD 只在 Release 連結，Debug 一直都沒有聲音 |
| Linux GUI | `QT` | Qt Multimedia，不會連結 `fmodex.lib`，亦不會 include Windows FMOD header |
| Dedicated server | `NULL` | `qsanguosha_engine`／`qsanguosha_server` 完全不會引入 Qt Multimedia |

`AUDIO_SUPPORT` 的意思由「這個 target 能連結 FMOD」改成「這個 target 有 audio
facade」，只在 GUI target 定義；engine／server target 照舊沒有。FMOD header 改由
`QSAN_AUDIO_BACKEND_FMOD` 守住。

Qt backend 的資源策略（三條路徑刻意分開）：

| 用途 | 實作 | 上限 |
|---|---|---|
| 短 UI 音效 | `QSoundEffect` 預載 `button-down`／`button-hover`／`choose-item`／`pop-up` | 預載 4 個 |
| 短音效 fallback | 獨立小 player pool（`QSoundEffect` 解決不了的那些，例如某些機的 `.ogg`） | 4 個 slot |
| 武將語音 | 可重用 `QMediaPlayer` + `QAudioOutput` pool | 8 個 slot |
| BGM | 獨立一個 player／output，`QMediaPlayer::Infinite` | 1 |

要點：

* 武將語音**不會**整批轉 WAV，亦不會預載入記憶體。
* pool 滿就搶最舊的那個 slot，所以播放永遠不會 `new` 一對新 player／output。
  按鈕的短音效有自己那個 pool，不會打斷一句語音。
* `superpose=false` 的舊語義保留：同一個檔案正在播放時就不重疊播放。
* 缺檔案只是 `qWarning`；沒有音訊裝置只是 `hasOutputDevice()=false`，兩者都不會 crash。
* `Audio::quit()` 會拆掉所有 player／output（全部掛在一個 parent `QObject` 下），
  不會留下 active QObject 或 decoder thread。`quit()` 之後 facade 會連 backend
  一起 delete，所以再有播放請求是 no-op —— 與 Windows FMOD 一樣是終局，M2B-A
  沒有改這個語義。（`StartScene::switchToServer()` 那條 `Audio::quit()` 只會在
  「只做 host、自己不入局」的 `accept_type == 1` 路徑執行；正常「開房兼玩」
  執行 `startConnection()`，不會經過。）

### 影片背景

`HomeController::hasVideoSupport()` 以前只找 `*.dll`，也就是 Linux 永遠回傳 false。
現在改成用 glob 識別 plugin 名，並且連 `QLibraryInfo` 的 plugin 路徑一起找。

`qml/home/VideoOverlay.qml` 由 `Video` 改成 `MediaPlayer` + `VideoOutput`：
`Video` 沒有 expose `mediaStatus`，分不出「載入成功」與「格式不支援」。

影片背景**不是** HomeScene 啟動的必要條件。QML 每次都會報告一個分類結果：

```text
VIDEO_BACKEND_RESULT {"schema_version":1,"available":true,"loaded":false,
                      "fallback":true,"reason":"asset_missing","error":"..."}
```

| `reason` | 意思 |
|---|---|
| `ok` | 影片載入成功 |
| `not_requested` | 背景本身是圖片，沒有要求過影片 |
| `disabled` | 使用者在設定關閉了影片背景 |
| `asset_missing` | 指定了影片但檔案不存在 |
| `backend_unavailable` | 找不到 Qt multimedia plugin |
| `codec_unsupported` | backend 存在但無法解碼這個格式 |
| `playback_error` | 其餘播放錯誤 |

失敗時原因**不會**被 `fallback_ok` 覆蓋：靜態背景頂上之後只會額外標記
`fallback_confirmed`，所以 CI 分辨得出是缺資產還是 codec 不支援。

### 執行 smoke

```bash
# 主驗證（CI 用 Xvfb；本機 WSLg 用 --no-xvfb）
bash tools/ci/linux-gui-multimedia-smoke.sh ./relwithdebinfo/QSanguosha artifacts \
    --no-xvfb --platform xcb --label wslg --expect-backend qt

# 影片降級路徑：特意指一個不存在的 .mp4
bash tools/ci/linux-gui-multimedia-smoke.sh ./relwithdebinfo/QSanguosha artifacts \
    --no-xvfb --platform xcb --label video-missing \
    --video-source tests/fixtures/media/no-such-clip.mp4 \
    --expect-video-reason asset_missing
```

直接呼叫 binary（runner 額外提供 process-level timeout、artifact 收集與 orphan 清理）：

```bash
./relwithdebinfo/QSanguosha --multimedia-smoke \
    --multimedia-timeout-ms 60000 \
    --multimedia-report artifacts/multimedia.json
```

輸出 marker：

```text
MULTIMEDIA_STAGE {"stage":"backend","ok":true,...}
MULTIMEDIA_STAGE {"stage":"ui_effect","ok":true,...}
MULTIMEDIA_STAGE {"stage":"voice","ok":true,...}
MULTIMEDIA_STAGE {"stage":"bgm","ok":true,...}
MULTIMEDIA_STAGE {"stage":"missing_asset","ok":true,...}
VIDEO_BACKEND_RESULT {...}
MULTIMEDIA_STAGE {"stage":"video","ok":true,...}
MULTIMEDIA_STAGE {"stage":"shutdown","ok":true,...}
MULTIMEDIA_RESULT {"schema_version":1,"ok":true,...}
```

exit code：`0` pass、`1` GUI setup、`2` audio stage、`3` video stage、
`4` app 內部 timeout、`5` 參數錯、`6` internal。app 內部 timeout **一定**會回傳
非零 exit code 與 `reason:"timeout"`，不會靜靜掛住等 runner 砍掉 —— 卡死的 media
decoder 正是這樣死的。

### 沒有硬件的 CI

GitHub runner 沒有實體音訊裝置，亦沒有入庫任何影片資產。所以 smoke 驗的是：

```text
backend 選對（--expect-backend qt，防止靜靜地退回 null backend）
media source 能接收
player／QSoundEffect 建立得起、pool 有上限
缺檔案 fallback（missing_files > 0）
沒有裝置 fallback（output_device=false 不算失敗）
影片路徑有明確分類 + 靜態背景頂上
Audio::quit() 之後 backend 真的是 "none"
沒有 crash／hang
```

**不會**用「沒有 console error」做成功條件。

測試 fixture 在 `tests/fixtures/media/`，全部是
`tools/ci/make-media-fixtures.py` 生成的合成正弦波（1–5 KB），不是正式遊戲資產。
`button-down.wav` 名稱不可以改：`classifyAudioFile()` 靠 basename 識別短 UI 音效。
刻意**沒有**影片 fixture（見該目錄的 `README.md`）。

### 設定

| Key | 預設 | 說明 |
|---|---|---|
| `MasterVolume` | `1.0` | 總增益，套用於所有通道 |
| `EffectVolume` | `1.0` | 既有 key |
| `VoiceVolume` | `1.0` | 語音在 `EffectVolume` 之上再多一級 trim |
| `BGMVolume` / `FrontBGMVolume` | `1.0` | 既有 key |
| `AudioMuted` | `false` | 全部靜音 |
| `EnableBackgroundVideo` | `true` | 首頁影片背景開關 |

Key 名 Windows／Linux 共用；舊設定檔沒有這幾個 key 時有穩定預設。所有音量都經
`clampVolume()`，設定檔被手改成非數字／負數／NaN 都不會傳到 backend。
Dedicated server 不需要讀這些值。

## 4.8 Linux GUI M2B-B effects smoke

M2B-A 證明**多媒體**建立得起、也能降級，M2B-B 證明**視覺特效**可以在同一個
client 用三個正式 profile 執行，而且三個 profile 有完全相同的遊戲規則與網絡回覆。

```text
FULL      完整動畫 + Spine + GIF + QML 技能特效 + 影片背景（Windows 現有行為）
REDUCED   保留必要狀態提示，動畫縮到約 30%，停用 Spine／影片／QML 疊層，GIF 只用首幀
NONE      不等任何裝飾動畫，遊戲狀態立即到達最終位置，
          一個 Spine skeleton／QMovie／video object 都不建立
```

### 集中 policy

UI code **不准**自己 `#ifdef Q_OS_LINUX` 跳動畫。單一入口：

| 部件 | 檔案 | 依賴 |
|---|---|---|
| Profile 契約（名稱／gate／duration scale／CLI 與設定解析） | `src/ui/effects/effects-profile.{h,cpp}` | 只 Qt Core |
| exactly-once completion 保證 | `src/ui/effects/effects-completion.{h,cpp}` | 只 Qt Core |
| Runtime 門面 `G_EFFECTS`、物件記數 | `src/ui/effects/effects-policy.{h,cpp}` | `Settings` |

```cpp
#include "effects/effects-policy.h"
if (!G_EFFECTS.animationsEnabled()) { /* 進入最終狀態 */ return; }
animation->setDuration(G_EFFECTS.scaledDuration(600));
```

Gate 只可以**收窄**：`videoEnabled()` 是
`profileAllowsVideo && Config.EnableBackgroundVideo`，所以 FULL 不會幫使用者
重新開啟他自己關閉的影片背景；`gifEnabled()` 同樣結合 `EnableAnimatedGenerals`。

解析次序：**CLI override > 使用者設定 > 預設（`full`）**。設定對話框
（顯示 → 視覺特效，`QSettings` key `EffectsProfile`）與 `--effects-profile`
執行同一個 `VisualEffectsPolicy`，不是兩套開關。打錯 profile 名會**立即**以
exit code 5 退出 —— 靜靜退回 `full` 會令三個 profile 的 CI matrix 變成
「同一個 profile 跑了三次」。

### 為什麼 NONE 不是將 duration 設為 0

zero-duration 的 `QAbstractAnimation` 會在 `start()` **內部同步** emit
`finished()`。所有靠 `finished()` 續流程的 call site 都會在自己未建好狀態
之前被人重入 —— double callback 與 use-after-free 就是這樣來的。

所以 NONE 執行 skip branch 直接進入最終狀態；還需要派 callback 的就經
`EffectsCompletion::completeNow()`（綁 context object 的 queued invocation）。
`scaledDuration()` 守住另一半：REDUCED 最少 1ms，永遠不會變 0。

### Completion 契約

| 路徑 | 結果 |
|---|---|
| 動畫播完 | callback 一次 |
| 動畫被跳過／未起動 | callback 一次（queued） |
| 動畫播放中被拆除 | callback 一次（流程不可以就這樣卡住） |
| watchdog timeout | callback 一次 |
| context 銷毀 | 取消 —— 不會 callback 到已銷毀的物件 |

沒有 double，沒有 never。`deliveredCount()` / `cancelledCount()` 令它可觀察。

### 執行 smoke

```bash
# 一個 profile 一次（CI 用 Xvfb；本機 WSLg 用 --no-xvfb）
bash tools/ci/linux-gui-effects-smoke.sh ./relwithdebinfo/QSanguosha artifacts \
    --profile none --no-xvfb --platform xcb --label wslg

# 直接呼叫 binary
./relwithdebinfo/QSanguosha --effects-smoke --effects-profile reduced \
    --effects-timeout-ms 60000 --effects-report artifacts/effects.json
```

輸出 marker：

```text
EFFECTS_PROFILE_RESULT {"profile":"none","source":"cli",...}
EFFECTS_STAGE {"stage":"policy","ok":true,...}
EFFECTS_STAGE {"stage":"completion","ok":true,...}
EFFECTS_STAGE {"stage":"animation","ok":true,...}
EFFECTS_STAGE {"stage":"gif","ok":true,...}
EFFECTS_STAGE {"stage":"spine","ok":true,...}
EFFECTS_STAGE {"stage":"budget","ok":true,...}
EFFECTS_STAGE {"stage":"shutdown","ok":true,...}
EFFECTS_RESULT {"schema_version":1,"ok":true,...}
```

exit code：`0` pass、`1` GUI setup、`2` policy、`3` completion、
`4` asset fallback、`5` budget／shutdown、`6` app 內部 timeout、`7` 參數錯、
`8` internal。

### 打完一整局

真正證明「跳過動畫都不會卡死」的是網絡 runner（真 TCP、真 `RoomScene`）：

```bash
python3 tools/autotest/gui_network_smoke.py --exe-root . \
    --mode 02p --seed 20260828 --artifact-dir artifacts \
    --no-xvfb --platform xcb --effects-profile none \
    --known-base-defect server-teardown-crash \
    --require-interactions choose_general,play_phase,ask_for_card
```

Runner 會驗 client 真的由 CLI 解析出要求的那個 profile，而 `none` 那次還要驗
成局打完之後 Spine／QMovie／QML 疊層／video object 全部是 0。與 §4.6 一樣，
這個是**本機** gate，不入 CI。

### 物件預算

`EffectsSmokeReport::budgetFor()` 就是每個 profile 的可執行定義，
`validate-effects-smoke.py` 亦有一份，所以單邊「改鬆了預算」的 regression
一樣會紅：

| Profile | Spine item | QMovie | QML 疊層 | Video |
|---|---|---|---|---|
| `none` | 0 | 0 | 0 | 0 |
| `reduced` | 0 | 不限（只用首幀） | 0 | 0 |
| `full` | 不限 | 不限 | 不限 | 不限 |

### 沒有資產的 package CI

獨立 Linux GUI compile／effects workflow 已移除。`linux-package-ci.yml` 會對
portable 與 AppImage 成品各跑 full／reduced／none profile；這個是成品 gate，
不會因一般 GUI source 改動而單獨觸發。

成品 smoke 只用 `tests/fixtures/effects/` 的合成 fixture（4x4 GIF、幾張
8x8 PNG、一個特意弄壞的 Spine 目錄），全部由
`tools/ci/make-effects-fixtures.py` 用標準庫生成，不是遊戲資產。
**備齊正式資產的 production smoke 不會成為 clean checkout 的 blocker。**
為什麼沒有合法 Spine fixture、將來要加時怎麼做，見該目錄的 `README.md`。

驗的是行為，不是 pixel。screenshot 只作 failure artifact。

### 順手修好的缺資產處理

**`PixmapAnimation::valid()` 以前永遠都是 true。** `setPath()` 用 `do`-`while`，
也就是在未驗證 frame 0 是否存在之前就已經 append 了一格；而
`getPixmapFromFileName()` 缺檔案時回傳的是一張 1x1 佔位圖（**不是** null pixmap）。
所以 `frames` 永遠不會空，`valid()` 永遠 true，全部「缺資產就不要播」的分支
根本從來沒有執行過：

* `GetPixmapAnimation()` 從來沒有執行過 `else { delete pma; return nullptr; }`，
  所以查 `nullptr` 的 caller（例如 `doPindianAnimation()` 的
  `else pindian_box->disappear()`）從來沒有收到過；
* `_createEquipBorderAnimations()` 從來沒有執行過 `!valid()`，`_m_equipBorders[i]`
  從來沒有被設為 `nullptr`。

沒有資產時真正發生的是：每個動畫多一個看不到的 1x1 sprite 加一個 20Hz timer。
不是 crash，但 fallback 從來沒跑過 —— REDUCED／NONE 一旦開始靠它們，就正是
最不想見到的狀態。`setPath()` 改成普通 `while`，只讀真正存在的 frame。
資產齊全時行為完全一樣（loop 條件本來就是同一個 `QFile::exists()`）。

`valid()` 回復誠實之後，以下三條路由「不可達」變成「可達」，所以要補守衛：

* **永久黑幕**：`doLightboxAnimation()` 的 `anim=` 分支建立了一塊 80% 不透明的
  rect，只在 `PixmapAnimation::finished()` 才拆除。現在
  `GetPixmapAnimation()` 真的會回傳 `nullptr`，這塊 rect 就會永遠留在畫面 ——
  遊戲還能玩但什麼都看不到。已改成立即拆除並且 warn。
* **裝備牌 nullptr deref**：`_setEquipBorderAnimation()` 用 `Q_ASSERT` 守住
  `_m_equipBorders[index]`，但 `Q_ASSERT` 在 Release／RelWithDebInfo 是 no-op。
  已改成真 null check。
* **`PixmapAnimation` 自己**：`_m_timerId`／`current`／`off_x`／`off_y` 都未
  初始化（未 `start()` 就 `stop()` 會殺一個垃圾 timer id），而
  `paint()`／`boundingRect()`／`advance()` 都沒有檢查 `frames` 是否為空。四樣都
  補上了。

同上面無關、獨立的一個：

* **動態立繪 nullptr deref**：`GraphicsPixmapHoverItem` 在 item 未進入 scene
  時 `m_proxyWidget` 會留下 null，接著照 `->show()`。已改成落回靜態立繪。

### 設定

| Key | 預設 | 說明 |
|---|---|---|
| `EffectsProfile` | `full` | `full` / `reduced` / `none`；設定對話框「視覺特效」 |

Key 名 Windows／Linux 共用；舊設定檔沒有這個 key 或值無法識別時都會落回 `full`
並且在 log 講明原因。

## 4.9 WSLg 手動驗證

Xvfb CI 通過**不可以**取代 WSLg 手動驗證：Xvfb 沒有 compositor，亦不會執行 WSLg 的
Wayland／X11 橋接。在 WSLg 下重複以下步驟：

```bash
CMAKE_PREFIX_PATH=~/Qt/6.11.1/gcc_64 cmake --preset linux-gui-gcc-debug
cmake --build --preset linux-gui-debug --parallel

# 自動 startup smoke
./debug/QSanguosha --ui-startup-smoke

# 正常可見啟動（要自己關窗）
./debug/QSanguosha
```

記錄以下環境資料（`UI_STARTUP_RESULT` marker 本身已經包含大部分）：

```bash
echo "DISPLAY=$DISPLAY"
echo "WAYLAND_DISPLAY=$WAYLAND_DISPLAY"
echo "XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR"
```

| 項目 | 2026-08-28 於 WSLg 實測 |
|---|---|
| `DISPLAY` | `:0` |
| `WAYLAND_DISPLAY` | `wayland-0` |
| `XDG_RUNTIME_DIR` | `/run/user/1000` |
| Qt platform plugin | `xcb` |
| Qt 版本 | 6.11.1 |
| Renderer | 預設（OpenGL）與 `QT_QUICK_BACKEND=software` 都通過 |
| `--ui-startup-smoke` | PASS，exit code 0，6 個 stage 全部 `ok:true` |
| 可見啟動 `./debug/QSanguosha` | PASS，能開啟主視窗，`Home QML status: QQuickWidget::Ready` |

> `qsanguosha_engine` 是 STATIC library，用 [`$<LINK_LIBRARY:WHOLE_ARCHIVE,...>`](../CMakeLists.txt) 在 `qsanguosha_server` 引入 source，只 link `Qt6::Core` 與 `Qt6::Network`。CMake 有 allowlist gate，link 了其他 Qt target（例如 Widgets）會立刻 `FATAL_ERROR`。

## 5. Lua / SWIG

- Lua 原始碼 commit 在 `src/lua/`（`lapi.c`、`lbaselib.c` 等），直接 build 入 `qsanguosha_engine`。
- SWIG binding 是 CMake 自動生成：`${binaryDir}/generated/sanguosha_wrap.cxx`，由 `swig/sanguosha.i` 生成。wrapper 不會 commit，亦不會放在 source tree。
- 需要 `swig` 在 PATH；CMake 用 `find_program(QSAN_SWIG_EXECUTABLE NAMES swig swig.exe ... REQUIRED)`。

## 6. Deploy server

Build 完 `qsanguosha_server`，要將 `lua/` 目錄 copy 到 executable 旁邊才可以正常執行（Lua 技能／extensions 需要這份 runtime data）：

```bash
cmake --build build-linux-gcc --target deploy-server
```

`deploy-server` target 會執行 `${CMAKE_COMMAND} -E copy_directory lua $<TARGET_FILE_DIR:qsanguosha_server>/lua`。

或者手動 copy：

```bash
cp -r lua <executable_dir>/lua
```

## 7. 執行 headless server

Dedicated server 可以用獨立 INI 完整設定，不需要開 GUI `ServerDialog`。設定優先次序固定為：

```text
程式預設 → 原有 QSettings → --config 指定的 INI → CLI --xxx override
```

INI overlay 與 CLI override 只影響本次 process，不會寫回來源 INI。先複製範例並驗證：

```bash
./qsanguosha_server --help
./qsanguosha_server --version
./qsanguosha_server --list-game-modes
cp docs/server.ini.example server.ini
./qsanguosha_server --config server.ini --check-config
```

常用啟動方式：

```bash
# 基本啟動（沿用已保存設定）
./qsanguosha_server

# 由完整 INI 啟動，再用 CLI 覆蓋最常改的 port／模式
./qsanguosha_server --config server.ini --port 9527 --game-mode 10p

# 測試／編排用：由 kernel 選擇未使用 port
./qsanguosha_server --config server.ini --bind-address 127.0.0.1 --port 0

# 測試用：固定 random seed、停用 AI、取消操作時限
./qsanguosha_server --seed 12345 --ai off --operation-timeout 0

# 輸出 [AUTOTEST] marker 到檔案（stdout redirect 會 buffer，檔案較可靠）
./qsanguosha_server --autotest-log /tmp/autotest.log

# 顯示完整有效設定，不會 listen
./qsanguosha_server --config server.ini --print-config
./qsanguosha_server --config server.ini --print-config --json
```

| 參數 | 用途 |
|---|---|
| `-h`, `--help` | 顯示 help 後退出 |
| `-v`, `--version` | 顯示版本後退出 |
| `-p`, `--port <0-65535>` | TCP listen port；`0` 由 kernel 分配 ephemeral port |
| `--websocket-port <0-65535>` | WebSocket listen port；`0` 由 kernel 分配 ephemeral port |
| `--bind-address <value>` | 數字 IPv4／IPv6，或 `any`、`any-ipv4`、`any-ipv6` |
| `-m`, `--game-mode <id>` | 本次使用的遊戲模式 |
| `-n`, `--server-name <name>` | 對外顯示的伺服器名稱 |
| `--operation-timeout <0-86400>` | 操作時限（秒）；`0` 代表無限 |
| `--ai <on\|off>` | 啟用／停用 server AI |
| `--ai-delay <0-600000>` | AI 延遲（毫秒） |
| `-s`, `--seed <uint64>` | 固定遊戲 seed，方便重現測試 |
| `--autotest-log <path>` | 將 automation marker 寫入檔案 |
| `--log-level <level>` | 最低 production log level：`debug`、`info`、`warning`、`error` |
| `--log-file <path>` | append log 至指定檔案；未指定時寫 stdout |
| `--log-format <format>` | production log 格式：`text` 或 newline-delimited `json` |
| `-c`, `--config <path>` | 以獨立 INI 覆蓋原有 QSettings |
| `--list-game-modes` | 列出模式 ID／名稱後退出 |
| `--check-config` | 驗證完整有效設定後退出 |
| `--print-config` | 顯示有效設定後退出 |
| `--json` | 配合 `--print-config` 輸出 JSON |

`--config` 支援完整 server-side 設定，包括：

- 基本設定：`ServerName`、`GameMode`、`BindAddress`、`ServerPort`、`WebSocketPort`、操作／開局倒數。
- 遊戲規則：`BanPackages`、`RandomSeat`、作弊／自由選將、雙將、同將、暗將、國戰、混戰及體力方案。
- AI／服務：AI delay、禁聊、同 IP 限制、投降、手氣卡、Lua、神將、UPnP／列表伺服器。
- 模式設定：`1v1/*`、`3v3/*`、`XMode/*`、`Banlist/*`。
- Boss mode：難度 bitmask、十殿閻羅、經驗、可選 Boss、無盡及回合限制。

完整可修改範例見 [`server.ini.example`](server.ini.example)。未知 key、錯誤 boolean／enum 或超出範圍的數字會令 `--check-config` 失敗；啟動時讀到無效外部 INI 亦會拒絕啟動。

CLI 格式錯誤使用 exit code `64`，設定錯誤使用 `78`，初始化失敗是 `1`，listen 失敗是 `2`。Linux 上 `SIGINT`／`SIGTERM` 會 clean shutdown。CLI parser 位於 `src/server/server-command-line.cpp`，INI schema 位於 `src/server/server-config.cpp`，process 啟動流程位於 `src/server-main.cpp`。

成功 listen 後會輸出 socket 實際綁定的 endpoint，例如 `Listening on 127.0.0.1:43817`；使用 `--port 0` 時，CI client 應由這一行取得 kernel 分配的 port。WebSocket 另寫 `WebSocket listening on 127.0.0.1:43818`；使用 `--websocket-port 0` 時由該行取得 WS port。多個測試實例應同時傳 `--port 0 --websocket-port 0`，避免搶預設 9528。

### Server Console

成功 listen 後，前景 terminal 會進入非阻塞管理 console：

```text
QSanguosha Server 20251231
Listening on 0.0.0.0:9527
Mode: 10p

server> status
server> players
server> rooms
server> say Server maintenance in 10 minutes
server> kick p001
server> shutdown
```

首版固定提供 7 個 command：

| Command | 用途 |
|---|---|
| `help` | 顯示 command help |
| `status` | 顯示 uptime、listen endpoint、模式、房間／玩家數、AI／Lua 狀態 |
| `players` | 顯示玩家 ID、名稱、房間及連線狀態 |
| `rooms` | 顯示房間 ID、狀態、模式、人數及 uptime |
| `say <message>` | 以管理員訊息廣播到所有房間 |
| `kick <player-id>` | 按 `players` 顯示的精確 ID 斷開玩家 |
| `shutdown` | 經正常 Qt shutdown 流程停止 server |

Console 只經 `Server` 的 snapshot／管理 API 操作，不會持有 `Room *` 或 `ServerPlayer *`。stdin 使用 Qt socket notifier 非阻塞讀取；stdin 關閉時只停用 console，server 仍繼續運行，適合由 systemd 配合 signal 管理。

### Production logging

預設使用 `info` level、`text` 格式並寫 stdout；指定 `--log-file` 時會以 append 模式寫入檔案，每筆立即 flush。目標目錄必須已存在且可寫，否則以 exit code `73` 拒絕啟動。

```bash
# journal／terminal 友善文字
./qsanguosha_server --config server.ini \
    --log-level info --log-format text

# 每行一個 JSON object，方便 Loki／Vector／Fluent Bit 收集
./qsanguosha_server --config server.ini \
    --log-level info --log-format json \
    --log-file /var/log/qsanguosha/server.log
```

每筆 JSON 固定包含：

- `timestamp`：UTC ISO-8601，包含毫秒。
- `level`：`debug`／`info`／`warning`／`error`。
- `component`：例如 `server`、`room`、`player`、`qt`。
- `room_id`、`player_id`：無相關 context 時為 `null`。
- `message`：事件名稱或診斷內容。

額外欄位會直接加入同一筆 record，例如實際 listen `address`／`port`、room `mode`、玩家 `name`、game over `winner`。典型 text log：

```text
2026-08-26T13:30:42.123Z INFO server Listening on 0.0.0.0:9527 address=0.0.0.0 mode=10p port=9527
2026-08-26T13:31:04.456Z INFO player room_id=3 player_id=p001 joined name=playerA
2026-08-26T13:32:11.789Z INFO room room_id=3 game_started mode=10p
```

Qt warning／critical 亦會經相同 sink，以 `component=qt` 記錄。`--autotest-log` 仍是獨立 automation marker，不應當作 production log。

### systemd

CMake install 會部署 server binary、`lua/`、`extensions/`、文件及 `qsanguosha-server.service`。Unit 使用 foreground `Type=simple`、`SIGTERM` graceful shutdown、`Restart=on-failure`、dynamic user 與 systemd sandbox；不會呼叫 legacy `Server::daemonize()`。

```bash
sudo cmake --install build-linux-gcc

sudo install -d -m 0755 /etc/qsanguosha
sudo install -m 0644 docs/server.ini.example /etc/qsanguosha/server.ini
sudo systemctl daemon-reload
sudo systemctl enable --now qsanguosha-server.service

systemctl status qsanguosha-server.service
journalctl -u qsanguosha-server.service -f
```

預設 unit 以 text log 寫 journald，由 systemd 負責 rotation。需要 JSON file 時，用 `sudo systemctl edit qsanguosha-server.service` 清空並覆蓋 `ExecStart`：

```ini
[Service]
ExecStart=
ExecStart=/usr/local/bin/qsanguosha_server --config /etc/qsanguosha/server.ini --log-level info --log-format json --log-file /var/log/qsanguosha/server.log
```

`LogsDirectory=qsanguosha` 會建立可寫的 `/var/log/qsanguosha`。若安裝 prefix 不是 `/usr/local`，以 CMake 產生並安裝的 unit 內實際路徑為準。

## 8. 測試 (CTest)

`tests/` 目錄有 CTest。Configure 之後直接執行：

```bash
cmake --build build-linux-gcc
ctest --test-dir build-linux-gcc --output-on-failure
```

Linux CTest 的單一 `qsanguosha_network_integration` suite 依序執行三級真實 TCP
network integration，並逐 level 輸出 PASS/FAIL 與結尾摘要：

1. Level 1：啟動 server、TCP connect／disconnect，確認 server 仍可回應 console，再以 SIGTERM 正常退出。
2. Level 2：完成 version／setup handshake、signup，從 `players` snapshot 確認 server 已識別玩家，再正常斷線。
3. Level 3：兩個 TCP client handshake／signup、填滿 `02p` room、開局後轉托管、完成自動對局、收到 game over、等待 room dispose，再驗證 SIGTERM clean exit 與 `CARD_LIFETIME_ZERO`。

三個 child case 使用獨立臨時 `XDG_CONFIG_HOME`、CLI `--port 0` 與固定 seed，
從 `Listening on` 取得實際 port；suite 標記為 `network` 並強制 serial 執行。
只跑 network suite：

```bash
ctest --test-dir build-linux-gcc --output-on-failure -L network
```

其餘測試以 `qsanguosha_server_cli_contract`、`qsanguosha_server_unit`、
`qsanguosha_runtime_contract` 等 suite 整理 parser／help／version、INI
validation／precedence、engine smoke、card-lifetime、player-decision、room-runtime、
protocol messages、request、room-roster、player-lifecycle、skill-runtime、lua-runtime、
extra-turn 等 coverage。7-command console smoke 因 runtime/failure domain 不同仍獨立。
可配置 `-DBUILD_TESTING=OFF` 跳過。

## 9. GitHub Actions CI

Linux 有兩個獨立 workflow，刻意不合併：server CI 保持穩定，不會被 GUI dependency／runtime 問題污染。

### 9.1 `linux-server-ci.yml`

Ubuntu 24.04 依事件分成日常 gate 與完整 gate：

| 事件 | 編譯器 | 驗證 | install | shutdown |
|---|---|---|---|---|
| PR → `debug`／`main`、`push debug` | GCC | 直接執行 protocol contract suites（`qsanguosha_protocol_tests`）＋ TUI contract／live-tcp gates（`qsanguosha_tui_tests`）＋ network Level 2（handshake／signup） | 不跑 | 跑 |
| `push main`、`workflow_dispatch` | GCC + Clang | 完整 CTest，包含 network Level 1–3 ＋ TUI deterministic 完整對局與中途重連 smoke（`03_1v2`） | deploy-server、install、systemd | 跑 |

兩層都安裝 Qt6／Ninja、hash-pinned SWIG 4.3.1，下載 `lua/ai/`、`extensions/`
與共用 Lua runtime，再以 RelWithDebInfo configure／build。日常 gate 已經不執行
`ctest -L fast`，是直接驅動合併後的測試執行檔（每個執行檔用 `--suite` 選子案）；
Level 1 與獨立 shutdown smoke 重疊，完整 `02p` 自動對局 Level 3 留在 main／
手動 gate。無論成功或失敗都上傳 JUnit 與 server log。沒有 nightly schedule。

本機可以用相同 smoke script 驗證：

```bash
QSAN_SERVER_SMOKE_TIMEOUT_SECONDS=8 \
    bash tools/ci/server-shutdown-smoke.sh \
    debug/qsanguosha_server /tmp/server-shutdown.log

QSAN_SERVER_SMOKE_TIMEOUT_SECONDS=8 \
    bash tools/ci/server-console-smoke.sh \
    debug/qsanguosha_server /tmp/server-console.log
```

### 9.2 Linux GUI 驗證政策

`linux-gui-ci.yml` 已於 2026-08-30 移除；一般 GUI source 改動沒有獨立 Linux GUI
compile gate。本機驗證沿用 [§4.5](#45-linux-gui-m1-startup-smoke)、
[§4.7](#47-linux-gui-m2b-a-multimedia-smoke) 與
[§4.8](#48-linux-gui-m2b-b-effects-smoke) 的指令。

CI 只在 `linux-package-ci.yml` 的成品 gate 建 GUI：Ubuntu 24.04、Qt 6.11.1、
GCC、Ninja、RelWithDebInfo 產生 portable tar.zst 與 AppImage，再由兩個成品跑
M1 startup、M2B-A multimedia 與 M2B-B full／reduced／none。這個 workflow 由
packaging 路徑、相關 PR、`push main`、tag 或手動 dispatch 觸發，不是一般 GUI
compile CI。

> **M2 的 network game job 已經由 CI 移除（2026-08-28）。** runner 沒有美術／音訊
> 資產，在無資產環境下 client 與 server 打完一局之後會一起 SIGSEGV；同一個
> binary 在資產齊全的本機是 8/8 PASS。Windows 環境同樣有這個問題，headless mode
> 閃退本身也是遊戲中已有現象。所以 `gui_network_smoke.py` 改為**本機 gate**，
> 見 [§4.6](#46-linux-gui-m2-network-smoke真實-tcp-對局)。

成品 workflow **刻意不做**：visible startup、Wayland、pixel screenshot gate。

Linux Server CI 繼續用 distro Qt，不會受 GUI 的 Qt 6.11 baseline 影響。

## 10. 常見問題

- **找不到 Qt6**：確認已安裝 `qt6-base-dev` 與 `qt6-websockets-dev`。亦可設定 `-DCMAKE_PREFIX_PATH=/path/to/qt6`。
- **`QSAN_BUILD_GUI=ON` 之後 configure 報 Qt6 版本不相容**：GUI 要求 Qt ≥ 6.11。用 [2.2](#22-gui-clientqsan_build_guion) 的 aqt 步驟裝 Qt 6.11.1，再用 `CMAKE_PREFIX_PATH` 指向該路徑。
- **`QSAN_BUILD_GUI=ON` 之後 configure 報找不到 `LinguistTools`／`Quick`／`Multimedia`**：裝齊 [2.2](#22-gui-clientqsan_build_guion) 的 GUI 套件。Qt6 的 component config 必須與 `Qt6Config.cmake` 放在同一個 cmake 目錄，所以不可以只靠 `CMAKE_PREFIX_PATH` 指向另一個 prefix 補件（混用 distro Qt ＋ 另一個 prefix 不會 work）。
- **`ldd` 見到 `libQt6QuickTemplates2.so.6 => not found`**：Qt Quick Controls 的間接依賴。正常系統安裝不會出現；如果 Qt 不在標準 loader path，要設定 `LD_LIBRARY_PATH`（`RUNPATH` 不會傳遞到間接依賴）。
- **找不到 swig**：`sudo apt install swig`，或將 swig 放在 `tools/swig/`。
- **`qsanguosha_engine` link 到 Qt 之外的 target 而 FATAL_ERROR**：這個是 design 的 allowlist gate，不可以 hack 過去。
- **build 完跑不起來，報找不到 lua**：執行 `--target deploy-server`，或手動 `cp -r lua <exe_dir>/lua`。
- **Deterministic 對比**：用 `--seed` 令 `QT_HASH_SEED=0`，配合相同 package set／AI 在 Windows／Linux 產生相同 hash。
