# Linux Development Environment

Linux 交付兩個產品：**無頭伺服器 (headless server)** `qsanguosha_server`，同埋 **GUI client** `QSanguosha`。兩者由同一份 `CMakeLists.txt` 產生，用 `QSAN_BUILD_SERVER`／`QSAN_BUILD_GUI` 兩個 option 分別開關。

Server 使用 `QCoreApplication`，唔需要 X11／Wayland、FMOD 或任何 GUI／Qt Widgets／Quick／Multimedia 依賴；GUI client 就要完整 Qt6 Widgets／Quick／Multimedia 同系統 Freetype。

## 分階段狀態

| 階段 | 狀態 |
|---|---|
| Linux Server（build／CI／三級 TCP network integration／systemd） | **Complete** |
| Linux GUI M0（configure ＋ compile ＋ link） | **Complete** — `linux-gui-ci.yml` 喺 ubuntu-24.04 ＋ Qt 6.11.1 驗證 |
| Linux GUI M1（GUI startup：`QApplication`／`MainWindow`／HomeScene／event loop） | **Complete** — `linux-gui-ci.yml` 喺 Xvfb ＋ `xcb` 跑 `--ui-startup-smoke`；WSLg 人手驗證 |
| Linux GUI M2（RoomScene 真實 TCP 對局） | **Complete** — 由 `gui_network_smoke.py` 喺**齊資產嘅本機**驗；唔喺 CI 跑（見 [§4.6](#46-linux-gui-m2-network-smoke真實-tcp-對局)） |
| Linux GUI M2B-A（Qt multimedia：短音效／語音／BGM／影片背景降級） | **Complete** — `linux-gui-ci.yml` 跑 `--multimedia-smoke`（見 [§4.7](#47-linux-gui-m2b-a-multimedia-smoke)） |
| Linux GUI M2B-B（效果 profile：Spine／GIF／動畫降級） | **Complete** — `linux-gui-ci.yml` 跑 `--effects-smoke` full／reduced／none 三個 profile（見 [§4.8](#48-linux-gui-m2b-b-effects-smoke)） |
| Linux packaging（AppImage／deb／desktop entry／installer） | **Not started**（M3） |

M0 的定義固定為 **configure ＋ compile ＋ link**，加上一個 binary capability smoke。
M1 的定義固定為 **真正行完一次 GUI startup path 然後自動正常退出**。
M2B-A 的定義固定為 **audio backend 同 QML media component 建得起、收得落 media
source、缺資產／缺裝置有明確降級、收得乾淨**；佢**唔包括**「真係聽到聲」——
CI runner 冇音訊裝置。
M2B-B 的定義固定為 **一個 client 三個效果 profile（full／reduced／none）行同一
條集中 policy、動畫 completion 保證 exactly once、缺／壞資產降級成靜態 UI、
NONE 唔建立 Spine／QMovie／video object**；佢**唔包括**「畫面睇落一樣」——
CI runner 冇正式美術資產，pixel diff 唔會做 blocking gate。

以下全部 **未完成**，唔喺 M0／M1／M2／M2B-A／M2B-B 範圍：

```text
Linux packaging（AppImage／deb／installer）
Android／直版 UI／WASM
Protocol V2／WebSocket
```

> ⚠️ `--local-response-ui-capabilities` 喺建立 `QApplication` 之前就直接回傳 JSON，所以佢係
> **binary capability smoke**，唔係 GUI／offscreen startup smoke。真正的 `QApplication`／
> `MainWindow`／`HomeScene` 啟動驗證係 M1 的 `--ui-startup-smoke`（見 [§4.5](#45-linux-gui-m1-startup-smoke)）。

- Status: Linux Server Complete；Linux GUI M0（configure／compile／link）Complete；Linux GUI M1（GUI startup）Complete；Linux GUI M2（network game）Complete；Linux GUI M2B-A（multimedia）Complete；Linux GUI M2B-B（effects profiles）Complete
- Last Updated: 2026-08-28
- 對應 Windows 開發環境請見 [`README.md`](../README.md) 嘅 🛠️ Development Environment section。

## 1. 平台基線

| 項目 | 基線 |
|---|---|
| 平台 | Ubuntu 24.04 x64（CI 基線；本機亦驗證過 Ubuntu 26.04，其他 Linux distro 一般相容） |
| C++ | C++17（`CMAKE_CXX_STANDARD 17`） |
| Qt | Server：distro Qt 6.x（`qt6-base-dev`，Ubuntu 24.04 為 6.4.2）<br>GUI：**Qt 6.11.1**，與 Windows 同一 baseline |
| Lua | 內建喺 `src/lua/`（SWIG 自動生成 binding） |
| 建置系統 | CMake 4.2+（`CMakeLists.txt` 下限係 3.28）、Ninja |
| Generator | Ninja（本機 `build-linux-gcc/`、`build-linux-clang/` 均用 Ninja） |
| 編譯器 | GCC（`/usr/bin/c++`）或者 Clang（`/usr/bin/clang++`） |

> **Qt baseline 分層**：GUI client 喺 Windows 同 Linux 都要求 Qt **6.11** 或以上
> （`QSAN_QT_GUI_MINIMUM_VERSION`），因為 GUI source 用咗 Qt 6.5+ 先有嘅 API
> （例如 `QStyleHints::colorScheme()`），而 Windows 正式 toolchain 已經係
> Qt 6.11.1 `msvc2022_64`。
>
> dedicated server **唔受呢個下限限制** — server-only configure（`QSAN_BUILD_GUI=OFF`）
> 只要 CMake 搵到 Qt6 Core／Network 就編到，可以繼續用 distro Qt（Ubuntu 24.04 的
> 6.4.2 亦可）。Linux Server CI 就係咁行。

## 2. 系統依賴

### 2.1 Server-only（`QSAN_BUILD_GUI=OFF`）

Ubuntu / Debian：

```bash
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    ninja-build \
    qt6-5compat-dev \
    qt6-base-dev \
    swig
```

對應套件包：

- **build-essential** — `gcc`／`g++`、`make` 等
- **cmake** — 建置系統
- **ninja-build** — Ninja generator（快速）
- **qt6-base-dev** — Qt6 Core／Network head 同 library（`/usr/lib/x86_64-linux-gnu/cmake/Qt6*`）
- **qt6-5compat-dev** — CTest replay 測試所需嘅 Qt6 Core5Compat；`BUILD_TESTING=OFF` 嘅 server-only build 唔需要
- **swig** — 生成 C++／Lua binding

### 2.2 GUI client（`QSAN_BUILD_GUI=ON`）

GUI 要求 **Qt 6.11 或以上**。distro apt 的 Qt 版本要視乎發行版：

| 發行版 | apt Qt 版本 | 夠唔夠 GUI？ |
|---|---|---|
| Ubuntu 24.04 LTS | 6.4.2 | ✗ 太舊，要另外裝 Qt 6.11.1 |
| Ubuntu 26.04 LTS | 6.10.2 | ✗ 差少少，要另外裝 Qt 6.11.1 |

先裝非 Qt 的系統依賴（任何發行版都要）：

```bash
sudo apt install -y \
    libfreetype-dev \
    libgl-dev
```

再用 `aqtinstall` 取得官方 Qt 6.11.1（唔需要 root，同 CI 完全一致）：

```bash
python3 -m venv ~/.venvs/aqt
~/.venvs/aqt/bin/pip install \
    'git+https://github.com/miurahr/aqtinstall.git@16db45a70b5905ad596941b223469bc86a56901e'
~/.venvs/aqt/bin/aqt install-qt linux desktop 6.11.1 linux_gcc_64 \
    -m qt5compat qtmultimedia -O ~/Qt
```

> aqt 3.3.0（最新 release）未支援 Qt 6.11+ 的新 repo 目錄結構
> （[miurahr/aqtinstall#959](https://github.com/miurahr/aqtinstall/issues/959)），
> 所以固定到已合併修復的 commit — 同 `.github/workflows/ci.yml` 及
> `linux-gui-ci.yml` 用同一個 pin。

configure 時指向該 Qt：

```bash
CMAKE_PREFIX_PATH=~/Qt/6.11.1/gcc_64 cmake --preset linux-gui-gcc-debug
```

如果你的發行版 apt 本身已經提供 Qt ≥ 6.11，亦可以直接裝 distro 套件代替 aqt：

```bash
sudo apt install -y \
    qt6-declarative-dev \
    qt6-l10n-tools \
    qt6-multimedia-dev \
    qt6-tools-dev \
    qt6-tools-dev-tools
```

按依賴類別對應：

| 類別 | Ubuntu 套件 | 供應嘅 CMake component |
|---|---|---|
| compiler / build | `build-essential`、`cmake`、`ninja-build` | — |
| SWIG | `swig` | — |
| Qt Base | `qt6-base-dev` / aqt base | `Core`、`Gui`、`Network`、`Widgets`、`OpenGLWidgets` |
| Qt 5Compat | `qt6-5compat-dev` / aqt `-m qt5compat` | `Core5Compat` |
| Qt Declarative / QML / Quick | `qt6-declarative-dev`（另 `qt6-declarative-dev-tools` 提供 `qmlcachegen`／`qmltyperegistrar`，一般由前者拉入）/ aqt base | `Qml`、`Quick`、`QuickControls2`、`QuickWidgets` |
| Qt Multimedia | `qt6-multimedia-dev` / aqt `-m qtmultimedia` | `Multimedia` |
| Qt Tools / LinguistTools | `qt6-tools-dev`、`qt6-tools-dev-tools`、`qt6-l10n-tools` / aqt base | `LinguistTools`（`lrelease`／`lupdate`） |
| OpenGL development | `libgl-dev`（連帶 `libglx-dev`） | `WrapOpenGL` |
| Freetype development | `libfreetype-dev` | `Freetype::Freetype` |

> 套件名喺唔同 Ubuntu／Debian repository 可能有出入，用 `apt-cache search qt6` 或者 `apt-cache policy <package>` 確認先安裝。

Linux **唔需要** FMOD：`AUDIO_SUPPORT` 同 bundled `lib/win/x64/fmodex.lib` 只喺 Windows Release 生效。Linux 亦唔用 `include/freetype` 入面嘅 bundled Windows header，改用系統 `libfreetype-dev`。

如果想用 **Clang** 編譯，加：

```bash
sudo apt install -y clang
```

揀用邊個 compiler 就喺 configure 時將 `CMAKE_CXX_COMPILER` 指向對應嘅 path。

## 3. 產品選項（`QSAN_BUILD_GUI` / `QSAN_BUILD_SERVER`）

同一份 `CMakeLists.txt` 用兩個 option 決定要 build 邊個產品：

| Option | Windows 預設 | Linux 預設 | 說明 |
|---|---|---|---|
| `QSAN_BUILD_GUI` | `ON` | `OFF` | GUI client `QSanguosha` |
| `QSAN_BUILD_SERVER` | `ON` | `ON` | dedicated server `qsanguosha_server` |

Linux 預設 `OFF` 係為咗保護現有 Linux Server CI：server-only configure 只會 `find_package` Core／Network（加 `BUILD_TESTING=ON` 時嘅 Core5Compat／Gui），唔會因為 GUI source 存在而要求 Quick／Widgets／Multimedia。要 build Linux GUI 就顯式開 `-DQSAN_BUILD_GUI=ON`（下面嘅 preset 已經設定好）。

`BUILD_TESTING=ON` 需要 `QSAN_BUILD_SERVER=ON`（CTest 直接驅動 `qsanguosha_server`），CMake 會喺 configure 階段 `FATAL_ERROR` 提示。

## 4. Configure + Build

### 4.1 Preset（建議）

`CMakePresets.json` 提供 Linux preset（`condition` 限定 `hostSystemName == Linux`，唔會影響 Windows 嘅 `vs2026-x64`／`debug`／`release`／`deploy-*`）：

| Configure preset | Build preset | binaryDir | 產品 |
|---|---|---|---|
| `linux-server-gcc-debug` | `linux-server-debug` | `builds/cmake-linux-server-gcc-debug` | server + CTest |
| `linux-gui-gcc-debug` | `linux-gui-debug` | `builds/cmake-linux-gui-gcc-debug` | GUI + server + CTest |

Linux GUI（`CMAKE_PREFIX_PATH` 指向 Qt 6.11.1，見 [2.2](#22-gui-clientqsan_build_guion)）：

```bash
CMAKE_PREFIX_PATH=~/Qt/6.11.1/gcc_64 cmake --preset linux-gui-gcc-debug
cmake --build --preset linux-gui-debug --parallel
```

如果 distro Qt 本身已經 ≥ 6.11，可以省略 `CMAKE_PREFIX_PATH`。Qt 版本不足時
configure 會直接失敗並列出搵到嘅版本，唔會走到 compile 先爆。

Linux server：

```bash
cmake --preset linux-server-gcc-debug
cmake --build --preset linux-server-debug --parallel
ctest --test-dir builds/cmake-linux-server-gcc-debug --output-on-failure
```

### 4.2 直接行 CMake（GCC）

```bash
cmake -S . -B build-linux-gcc -G Ninja \
    -DBUILD_TESTING=ON \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=/usr/bin/gcc \
    -DCMAKE_CXX_COMPILER=/usr/bin/g++
cmake --build build-linux-gcc
```

加 `-DQSAN_BUILD_GUI=ON` 就會連 `QSanguosha` 一齊 build。

### 4.3 直接行 CMake（Clang）

```bash
cmake -S . -B build-linux-clang -G Ninja \
    -DBUILD_TESTING=ON \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=/usr/bin/clang \
    -DCMAKE_CXX_COMPILER=/usr/bin/clang++
cmake --build build-linux-clang
```

### 4.4 Build 做完嘅 output

`qsanguosha_server` 同 `QSanguosha` 都會按 build type 生成喺 source tree 嘅 `debug/`、`release/` 或 `relwithdebinfo/`（例如 Debug 係 `debug/qsanguosha_server`、`debug/QSanguosha`）。

驗證 Linux GUI executable：

```bash
file debug/QSanguosha
ldd debug/QSanguosha | grep 'not found'   # 應該冇輸出
```

M0 的 binary capability smoke：

```bash
./debug/QSanguosha --local-response-ui-capabilities
# {"schema_version":1,"auto":true,"show":true,"inspect":true}
```

> 呢個 flag 喺建立 `QApplication` 之前就回傳，所以毋須 `QT_QPA_PLATFORM=offscreen`，
> 亦唔算 GUI startup 驗證。以上全部只驗證 configure／compile／link 同 binary 可執行。
> 真正的 GUI 啟動驗證見下面 4.5。

## 4.5 Linux GUI M1 startup smoke

`--ui-startup-smoke` 係 M1 的自動化啟動驗證。同 `--local-response-ui-capabilities`
最大的分別係：佢**唔會**喺 `QApplication` 之前 return，而係行足產品正常的啟動路徑：

```text
QApplication → Engine/runtime → MainWindow → HomeScene/QML → Qt event loop
    → ready condition → 自動正常退出
```

佢唔會另外複製一份假的 HomeScene 啟動流程：MainWindow 照常 `setupHomePage()`
載入 `qrc:/QSanguosha/Home/HomeScene.qml`，smoke 只係透過 MainWindow 公開的
`homeSceneReady()`／`homeSceneFailed()` signal 觀察結果。

```bash
# 可見桌面（WSLg／X11／Wayland）
./debug/QSanguosha --ui-startup-smoke

# 明確指定 platform plugin 同 app 內部 timeout，並且輸出完整 JSON report
QT_QPA_PLATFORM=xcb ./debug/QSanguosha \
    --ui-startup-smoke \
    --ui-startup-timeout-ms 30000 \
    --ui-startup-report /tmp/ui-startup.json

# 完全冇 X server（次要驗證，唔可以當作 M1 的唯一證據）
QT_QPA_PLATFORM=offscreen ./debug/QSanguosha --ui-startup-smoke
```

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

失敗時 `ok` 為 `false`，`stage` 指出失敗喺邊一步，`reason` 分辨 `stage_failed`
同 `timeout`，並帶 `error` 文字。任何退出路徑（包括舊有 `exit(1)`）都會補一行
result marker，所以 CI 可以將「marker 缺失」直接當失敗。

### Ready condition

`home_scene` stage 唔係靠 `QTimer::singleShot(0, quit)` 就算數，要同時成立：

| 條件 | 判定 |
|---|---|
| `QApplication` 已建立 | `qobject_cast<QApplication *>(qApp)` |
| Engine/runtime 已就緒 | `Sanguosha != nullptr`，回報版本同武將數 |
| `MainWindow` 已建立並顯示 | `isVisible()` ＋ `windowHandle() != nullptr` |
| HomeScene/QML 已載入 | `QQuickWidget::Ready` ＋ `rootObject() != nullptr` |
| event loop 真的行過 | 入到 `exec()` 之後的 queued callback |
| top-level GUI object 捱得住 startup | 再行 250ms event loop 後 MainWindow 同 QML root 仍然生存 |

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
| App 內部 | `--ui-startup-timeout-ms`（預設 15000，範圍 100–120000）。同步階段亦會主動比對 deadline，因為 `QTimer` 喺 `exec()` 之前唔會觸發。 |
| Runner 外部 | `tools/ci/linux-gui-startup-smoke.sh` 的 `timeout --kill-after`，防止 Qt event loop 完全 hang 死。收工前會清走自己 process group 內剩低的 `QSanguosha`／`Xvfb`。 |

### 缺少 optional 美術資源

Clean checkout **冇入庫** `qml/home/icons/`、`image/system/backdrop/` 等 optional
美術資源，所以啟動時會見到一批 `QML Image: Cannot open: ...` warning。呢啲會被
分類為 optional asset warning 記錄落 report，**唔會**升級成 fatal——真正的 QML
component 失敗係由 `QQuickWidget::Error` 判定，兩者唔會混淆。

### CI／本機一鍵驗證

```bash
# Xvfb + xcb（CI 的主驗證）
bash tools/ci/linux-gui-startup-smoke.sh ./relwithdebinfo/QSanguosha artifacts \
    --platform xcb --label xvfb-xcb

# 可見桌面（WSLg），唔開 Xvfb
bash tools/ci/linux-gui-startup-smoke.sh ./debug/QSanguosha artifacts \
    --no-xvfb --platform xcb --label wslg
```

## 4.6 Linux GUI M2 network smoke（真實 TCP 對局）

> ⚠️ **呢個 smoke 唔喺 CI 跑，係本機 gate。** runner 冇美術／音訊資產，缺資產
> 之下 client 同 server 打完一局會一齊 SIGSEGV（已實測：同一個 binary 喺齊資產
> 嘅本機 8/8 PASS）。Windows 環境有同樣問題。所以呢個 runner 保留、但要喺
> **齊資產** 嘅本機行，唔會 gate PR。詳見 `AGENTS.md`「GUI runtime 唔入 CI」。

M1 證明 GUI **啟動得到**；M2 證明 GUI **玩得到**：一個獨立的 Linux
`qsanguosha_server` process、一個獨立的 Linux GUI client process、中間行真正的
TCP，走完 連線 → signup → RoomScene/Dashboard → 選將 → 出牌／askFor → game over
→ 正常離開。

### 模式 ID（以 registry 為準）

M2 用 registry 真正註冊咗嘅身分模式，唔靠猜：

| 用途 | mode ID | 人數 | 出處 |
|---|---|---|---|
| 2 人局 | `02p` | 2 | `src/core/engine.cpp` `modes.insert("02p", ...)`；`qsanguosha_server --list-game-modes` 亦會列出 |
| 5 人局 | `05p` | 5 | `src/core/engine.cpp` `modes.insert("05p", ...)`；同上 |

想自己確認：

```bash
./relwithdebinfo/qsanguosha_server --list-game-modes | grep -E '^(02p|05p)\b'
```

### `--network-ui-smoke`

client 端嘅入口。**唔會**改變正常玩家啟動：所有測試行為都要顯式 flag。

| Flag | 意思 |
|---|---|
| `--network-ui-smoke` | 啟用；同時要有 `-connect:<host>[:<port>]` 先有 Client 可以觀察 |
| `--network-ui-smoke-result <path>` | 寫低完整 JSON report（stages／responder 統計／失敗時嘅最後 UI state） |
| `--network-ui-smoke-timeout-ms <ms>` | app 層總 timeout（預設 600000） |
| `--network-ui-smoke-stall-ms <ms>` | 單一 request 未經 UI 回覆嘅容忍時間，超過就切 trustee 並記錄（預設 20000） |
| `--network-ui-smoke-screenshot <path>` | 失敗時影一張 PNG 作診斷（唔係 pixel gate） |

呢個入口係**觀察者**，唔係第二套流程：MainWindow、Client、RoomScene、Dashboard
全部係產品自己嗰啲，smoke 只係接產品已有嘅訊號
（`Client::socket_connected` / `server_connected` / `game_started` / `game_over`、
`MainWindow::roomSceneCreated`），再由 `NetworkUiSmokeResponder` 代替滑鼠去撳真正
被 enable 嘅 `CardItem`／`Photo`／`QSanButton`，最後行 RoomScene 自己嘅
`doOkButton()`／`doCancelButton()`／`doTimeout()` 把回覆送返 server。

Responder **唔會**自己砌 protocol packet。遇到 M2 未特別處理嘅互動形態，會走
RoomScene 自己嘅安全預設回覆（`doTimeout()`）；真係卡住超過 `--stall-ms` 先至切
trustee，而且一定會喺 report 記低 `trustee_fallback`，唔會扮成正常路徑。

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

次序同任務書列出嘅稍有不同，係**刻意**跟產品真實流程：RoomScene 喺 client 收到
setup 之後即刻由 `MainWindow::enterRoom()` 建立，早過選將請求，所以
`room_scene`／`dashboard` 排喺 `general_selected` 之前。

### Exit code：每種故障有自己嘅編號

| Exit code | 意思 |
|---|---|
| 0 | PASS |
| 1 | 參數不合法 |
| 2 | 連唔上（server 未啟動／port 錯） |
| 3 | signup／setup 未完成 |
| 4 | RoomScene 未建立 |
| 5 | Dashboard 未建立 |
| 6 | 選將請求未回覆 |
| 7 | 未開局 |
| 8 | askFor 無法經 UI 回覆 |
| 9 | 開咗局但冇 game over |
| 10 | 局中被 server 斷線 |
| 11 | app 內部總 timeout |
| 12 | 其他內部錯誤 |

`reason` 欄再分辨 `stage_failed` / `timeout` / `disconnected` /
`interaction_stalled`。client crash 同 shutdown hang 冇 result marker，由 runner 靠
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

- 借一個**空閒 TCP port**（平行 CI job 唔會撞）
- 用**固定 seed**，並且喺 summary 記低 mode／seed／port／server 同 client 嘅
  SHA-256／extensions commit／timeout 設定
- 寫一份確定性嘅 server INI overlay（關掉 RandomSeat／雙將／作弊／幸運牌），
  唔靠開發機留低嘅 `config.ini`
- 兩層有界 timeout：`--client-timeout-ms`（app 層）＋ `--process-timeout`（runner 層）
- 成功同失敗路徑都行同一條清理：graceful shutdown → terminate → kill，之後確認
  冇孤兒、port 已釋放
- **冇任何 retry**。一局就係一局，唔會跑到偶然 PASS 為止。

`--require-interactions` 可以要求某啲互動一定要經真 UI 覆蓋過（預設
`choose_general,play_phase`）。CI 只 gate `choose_general`，因為對局點樣展開受
server 端 AI 影響，其餘覆蓋率照樣寫入 artifact 供檢視。

跨平台部分（執行檔定位、process group spawn、process-tree 清理、exit code 解讀、
空閒 port）抽咗喺 `tools/autotest/runner_common.py`，`network_runner.py` 同
`gui_network_smoke.py` 共用；`network_runner.py` 亦因此喺 Linux 行得到。

### 已知的 base 缺陷:`server-teardown-crash`

M2 的 runner 揾到一個**同本分支無關**的 server 缺陷,並且刻意唔隱藏佢:

> 對局打完、client 正常離開之後,`qsanguosha_server` 喺拆房嗰陣
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

即係 snapshot 入面嘅 `CardUseStruct` 活得過佢引用嘅 `Card`。

三重對照,證明同 M2 無關:

1. 本分支改過嘅檔案入面,**冇一個**會編入 `qsanguosha_engine` 或者
   `qsanguosha_server`(唯一入到 server-only build 嘅係兩個 CTest 專用檔案)。
2. 用 M1 merge base(`50e5750`)編出嚟嘅 `qsanguosha_server` 配同一個 client,
   一樣重現同一個 SIGSEGV。
3. 完全唔用 `--network-ui-smoke`、行返舊有 `--auto-robots` 托管流程,一樣重現。

所以 runner 有一個**明確而且有界**嘅降級開關:

```bash
python3 tools/autotest/gui_network_smoke.py ...     --known-base-defect server-teardown-crash
```

呢個開關**唔係**靜音掣:

* 崩潰照樣偵測、照樣列印(`KNOWN BASE DEFECT (downgraded, still recorded)`)、
  照樣寫入 `summary["known_base_defects"]`;
* 只有喺 server 已經寫出**帶勝方嘅 game over**、而且 client 已經 **exit 0**
  之後發生嘅 server 崩潰先會被降級。對局途中死掉嘅 server 永遠係失敗;
* 缺陷 id 係一個封閉清單(`KNOWN_BASE_DEFECTS`),加一個新 id 係一次要 review
  嘅改動;
* 復原條件:card-lifetime / GameSnapshot 嘅擁有權修好之後,喺 CI 拿走呢個
  flag 即可。

### 已知的 5 人局 client 繪製崩潰(暫時非阻擋)

`05p` 的 GUI client 會喺對局途中 SIGSEGV,backtrace 全部落喺 Qt Widgets 的
`QGraphicsView::paintEvent` → `QGraphicsScene` 繪製路徑,冇任何 QSanguosha frame。

已知邊界:

* `02p` 用同一條 responder 路徑**唔會**重現 → 唔係 responder 本身的邏輯問題;
* 同一個 client、改行舊有 `--auto-robots` 托管流程(完全唔經 UI responder)
  **唔會**重現 → 要有真實 UI 互動先觸發;
* 即係 5 人版面特有的繪製問題,唔屬於 M2 的修復範圍。

CI 的 05p job 因此暫時 `continue-on-error: true`,但 seed 固定、artifact 照樣
上傳、失敗照樣顯示。移除條件:5 人局 RoomScene 的繪製崩潰修好之後拿走該行。
02p job 永遠阻擋,唔會被遮蓋。

### 素材

自動化測試喺**冇美術素材**嘅情況下跑（`image/`、`audio/`、`font/`、`hero-skin/`
唔入庫）。缺素材只會產生 optional asset warning，唔可以令 RoomScene、Dashboard、
選目標、網絡回覆或者對局流程崩潰 — 呢個係 M2 要證明嘅嘢之一。

## 4.7 Linux GUI M2B-A multimedia smoke

M1 證明 GUI **啟動得到**，M2 證明 GUI **玩得到**，M2B-A 證明 GUI 的
**多媒體子系統建得起、失敗降得到、關得乾淨**。

### Audio 架構

產品一直只有 `Audio` 一個 facade（`src/core/audio.h`）。M2B-A **冇**另開第二套
facade，只係將實作交畀 `IAudioBackend`（`src/ui/audio/audio-backend.h`）：

```text
Audio  ──►  IAudioBackend
              ├── FmodAudioBackend      Windows GUI Release（行為不變）
              ├── QtMediaAudioBackend   Linux GUI（Qt Multimedia）
              └── NullAudioBackend      dedicated server／CI／降級
```

揀邊個 backend 只喺兩個地方發生：CMake 的 `QSAN_AUDIO_BACKEND` 同
`src/ui/audio/audio-backend-factory.cpp`。call site 一個 `#ifdef Q_OS_LINUX`
都冇。

| 產品 | 預設 | 備註 |
|---|---|---|
| Windows GUI Release | `FMOD` | bundled `fmodex.lib`，同以前完全一樣 |
| Windows GUI Debug | `FMOD`（實際落 null） | FMOD 只喺 Release 連結，Debug 一直都冇聲 |
| Linux GUI | `QT` | Qt Multimedia，唔會連 `fmodex.lib`，亦唔會 include Windows FMOD header |
| Dedicated server | `NULL` | `qsanguosha_engine`／`qsanguosha_server` 完全唔會拉到 Qt Multimedia |

`AUDIO_SUPPORT` 的意思由「呢個 target 連得到 FMOD」改成「呢個 target 有 audio
facade」，只喺 GUI target 定義；engine／server target 照舊冇。FMOD header 改由
`QSAN_AUDIO_BACKEND_FMOD` 守住。

Qt backend 的資源策略（三條路徑刻意分開）：

| 用途 | 實作 | 上限 |
|---|---|---|
| 短 UI 音效 | `QSoundEffect` 預載 `button-down`／`button-hover`／`choose-item`／`pop-up` | 預載 4 個 |
| 短音效 fallback | 獨立細 player pool（`QSoundEffect` 解唔到嗰啲，例如某些機的 `.ogg`） | 4 個 slot |
| 武將語音 | 可重用 `QMediaPlayer` + `QAudioOutput` pool | 8 個 slot |
| BGM | 獨立一個 player／output，`QMediaPlayer::Infinite` | 1 |

要點：

* 武將語音**唔會**整批轉 WAV，亦唔會預載入記憶體。
* pool 滿就搶最舊嗰個 slot，所以播放永遠唔會 `new` 一對新 player／output。
  撳掣嘅短音效有自己嗰個 pool，唔會打斷一句語音。
* `superpose=false` 的舊語義保留：同一個檔案響緊就唔重疊播。
* 缺檔案只係 `qWarning`；冇音訊裝置只係 `hasOutputDevice()=false`，兩者都唔會 crash。
* `Audio::quit()` 會拆晒 player／output（全部掛喺一個 parent `QObject` 下），
  唔會留低 active QObject 或者 decoder thread。`quit()` 之後 facade 會連 backend
  一齊 delete，所以再有播放請求係 no-op —— 同 Windows FMOD 一樣係終局，M2B-A
  冇改呢個語義。（`StartScene::switchToServer()` 嗰條 `Audio::quit()` 只會喺
  「只做 host、自己唔入局」嘅 `accept_type == 1` 路徑行到；正常「開房兼玩」
  行 `startConnection()`，唔會經過。）

### 影片背景

`HomeController::hasVideoSupport()` 以前只搵 `*.dll`，即係 Linux 永遠答 false。
而家改成用 glob 認 plugin 名，並且連 `QLibraryInfo` 的 plugin 路徑一齊搵。

`qml/home/VideoOverlay.qml` 由 `Video` 改成 `MediaPlayer` + `VideoOutput`：
`Video` 冇 expose `mediaStatus`，分唔出「載入成功」同「格式唔支援」。

影片背景**唔係** HomeScene 啟動的必要條件。QML 每次都會報告一個分類結果：

```text
VIDEO_BACKEND_RESULT {"schema_version":1,"available":true,"loaded":false,
                      "fallback":true,"reason":"asset_missing","error":"..."}
```

| `reason` | 意思 |
|---|---|
| `ok` | 影片載入成功 |
| `not_requested` | 背景本身係圖片，冇要求過影片 |
| `disabled` | 使用者喺設定關咗影片背景 |
| `asset_missing` | 指定咗影片但檔案唔存在 |
| `backend_unavailable` | 搵唔到 Qt multimedia plugin |
| `codec_unsupported` | backend 在但解唔到呢個格式 |
| `playback_error` | 其餘播放錯誤 |

失敗嗰陣原因**唔會**被 `fallback_ok` 蓋走：靜態背景頂上之後只會額外標記
`fallback_confirmed`，所以 CI 分得出係缺資產定係 codec 唔支援。

### 執行 smoke

```bash
# 主驗證（CI 用 Xvfb；本機 WSLg 用 --no-xvfb）
bash tools/ci/linux-gui-multimedia-smoke.sh ./relwithdebinfo/QSanguosha artifacts \
    --no-xvfb --platform xcb --label wslg --expect-backend qt

# 影片降級路徑：特登指一個唔存在的 .mp4
bash tools/ci/linux-gui-multimedia-smoke.sh ./relwithdebinfo/QSanguosha artifacts \
    --no-xvfb --platform xcb --label video-missing \
    --video-source tests/fixtures/media/no-such-clip.mp4 \
    --expect-video-reason asset_missing
```

直接叫 binary（runner 額外提供 process-level timeout、artifact 收集同 orphan 清理）：

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
`4` app 內部 timeout、`5` 參數錯、`6` internal。app 內部 timeout **一定**會回
非零 exit code 同 `reason:"timeout"`，唔會靜靜掛住等 runner 斬 —— 卡死的 media
decoder 正正就係咁死。

### 冇硬件的 CI

GitHub runner 冇實體音訊裝置，亦冇入庫任何影片資產。所以 smoke 驗嘅係：

```text
backend 揀啱（--expect-backend qt，防止靜靜地退返 null backend）
media source 收得落
player／QSoundEffect 建得起、pool 有上限
缺檔案 fallback（missing_files > 0）
冇裝置 fallback（output_device=false 唔算失敗）
影片路徑有明確分類 + 靜態背景頂上
Audio::quit() 之後 backend 真係 "none"
冇 crash／hang
```

**唔會**用「冇 console error」做成功條件。

測試 fixture 喺 `tests/fixtures/media/`，全部係
`tools/ci/make-media-fixtures.py` 生成的合成正弦波（1–5 KB），唔係正式遊戲資產。
`button-down.wav` 個名唔可以改：`classifyAudioFile()` 靠 basename 認短 UI 音效。
刻意**冇**影片 fixture（見該目錄的 `README.md`）。

### 設定

| Key | 預設 | 說明 |
|---|---|---|
| `MasterVolume` | `1.0` | 總增益，套用喺所有通道 |
| `EffectVolume` | `1.0` | 既有 key |
| `VoiceVolume` | `1.0` | 語音喺 `EffectVolume` 之上再多一級 trim |
| `BGMVolume` / `FrontBGMVolume` | `1.0` | 既有 key |
| `AudioMuted` | `false` | 全部靜音 |
| `EnableBackgroundVideo` | `true` | 首頁影片背景開關 |

Key 名 Windows／Linux 共用；舊設定檔冇呢幾個 key 時有穩定預設。所有音量都經
`clampVolume()`，設定檔被手改成非數字／負數／NaN 都唔會傳落 backend。
Dedicated server 唔需要讀呢啲值。

## 4.8 Linux GUI M2B-B effects smoke

M2B-A 證明**多媒體**建得起同降得到級，M2B-B 證明**視覺特效**可以喺同一個
client 用三個正式 profile 行，而且三個 profile 有完全相同嘅遊戲規則同網絡回覆。

```text
FULL      完整動畫 + Spine + GIF + QML 技能特效 + 影片背景（Windows 現有行為）
REDUCED   保留必要狀態提示，動畫縮到約 30%，停用 Spine／影片／QML 疊層，GIF 只用首幀
NONE      唔等任何裝飾動畫，遊戲狀態即刻到達最終位置，
          一個 Spine skeleton／QMovie／video object 都唔建立
```

### 集中 policy

UI code **唔准**自己 `#ifdef Q_OS_LINUX` 跳動畫。單一入口：

| 部件 | 檔案 | 依賴 |
|---|---|---|
| Profile 契約（名稱／gate／duration scale／CLI 同設定解析） | `src/ui/effects/effects-profile.{h,cpp}` | 只 Qt Core |
| exactly-once completion 保證 | `src/ui/effects/effects-completion.{h,cpp}` | 只 Qt Core |
| Runtime 門面 `G_EFFECTS`、物件記數 | `src/ui/effects/effects-policy.{h,cpp}` | `Settings` |

```cpp
#include "effects/effects-policy.h"
if (!G_EFFECTS.animationsEnabled()) { /* 落最終狀態 */ return; }
animation->setDuration(G_EFFECTS.scaledDuration(600));
```

Gate 只可以**收窄**：`videoEnabled()` 係
`profileAllowsVideo && Config.EnableBackgroundVideo`，所以 FULL 唔會幫使用者
開返佢自己關咗嘅影片背景；`gifEnabled()` 同樣夾埋 `EnableAnimatedGenerals`。

解析次序：**CLI override > 使用者設定 > 預設（`full`）**。設定對話框
（顯示 → 視覺特效，`QSettings` key `EffectsProfile`）同 `--effects-profile`
行同一個 `VisualEffectsPolicy`，唔係兩套開關。打錯 profile 名會**即刻**以
exit code 5 退出 —— 靜靜退返 `full` 會令三個 profile 嘅 CI matrix 變成
「同一個 profile 跑咗三次」。

### 點解 NONE 唔係將 duration 設做 0

zero-duration 嘅 `QAbstractAnimation` 會喺 `start()` **入面同步** emit
`finished()`。所有靠 `finished()` 續流程嘅 call site 都會喺自己未砌好狀態
之前俾人重入 —— double callback 同 use-after-free 就係咁嚟。

所以 NONE 行 skip branch 直接落最終狀態；仲要派 callback 嘅就經
`EffectsCompletion::completeNow()`（綁 context object 嘅 queued invocation）。
`scaledDuration()` 守住另一半：REDUCED 最少 1ms，永遠唔會變 0。

### Completion 契約

| 路徑 | 結果 |
|---|---|
| 動畫播完 | callback 一次 |
| 動畫被跳過／未起動 | callback 一次（queued） |
| 動畫播緊俾人拆 | callback 一次（流程唔可以就咁卡住） |
| watchdog timeout | callback 一次 |
| context 銷毀 | 取消 —— 唔會 callback 落死物 |

冇 double，冇 never。`deliveredCount()` / `cancelledCount()` 令佢可觀察。

### 執行 smoke

```bash
# 一個 profile 一次（CI 用 Xvfb；本機 WSLg 用 --no-xvfb）
bash tools/ci/linux-gui-effects-smoke.sh ./relwithdebinfo/QSanguosha artifacts \
    --profile none --no-xvfb --platform xcb --label wslg

# 直接叫 binary
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

真正證明「跳咗動畫都唔會卡死」嘅係網絡 runner（真 TCP、真 `RoomScene`）：

```bash
python3 tools/autotest/gui_network_smoke.py --exe-root . \
    --mode 02p --seed 20260828 --artifact-dir artifacts \
    --no-xvfb --platform xcb --effects-profile none \
    --known-base-defect server-teardown-crash \
    --require-interactions choose_general,play_phase,ask_for_card
```

Runner 會驗 client 真係由 CLI 解析出要求嗰個 profile，而 `none` 嗰次仲要驗
成局打完之後 Spine／QMovie／QML 疊層／video object 全部係 0。同 §4.6 一樣，
呢個係**本機** gate，唔入 CI。

### 物件預算

`EffectsSmokeReport::budgetFor()` 就係每個 profile 嘅可執行定義，
`validate-effects-smoke.py` 亦有一份，所以單邊「改鬆咗預算」嘅 regression
一樣會紅：

| Profile | Spine item | QMovie | QML 疊層 | Video |
|---|---|---|---|---|
| `none` | 0 | 0 | 0 | 0 |
| `reduced` | 0 | 不限（只用首幀） | 0 | 0 |
| `full` | 不限 | 不限 | 不限 | 不限 |

### 冇資產的 CI

`linux-gui-ci.yml` 嘅 `gui-effects` job 係 **blocking matrix**：三個 profile
各跑一次，每個 profile 再分 `xcb` 同 `offscreen`，另加兩個負向契約（打錯
profile 名要被拒；app 內部 timeout 要出 `reason:"timeout"`）。

整個 matrix 只用 `tests/fixtures/effects/` 嘅合成 fixture（4x4 GIF、幾張
8x8 PNG、一個特登整壞嘅 Spine 目錄），全部由
`tools/ci/make-effects-fixtures.py` 用標準庫生成，唔係遊戲資產。
**有齊正式資產嘅 production smoke 唔會成為 clean checkout 嘅 blocker。**
點解冇合法 Spine fixture、將來要加要點做，見該目錄嘅 `README.md`。

驗嘅係行為，唔係 pixel。screenshot 只作 failure artifact。

### 順手整返好嘅缺資產處理

**`PixmapAnimation::valid()` 以前永遠都係 true。** `setPath()` 用 `do`-`while`，
即係喺未驗過 frame 0 存唔存在之前就已經 append 咗一格；而
`getPixmapFromFileName()` 缺檔案時回嘅係一張 1x1 佔位圖（**唔係** null pixmap）。
所以 `frames` 永遠唔會空，`valid()` 永遠 true，全部「缺資產就唔好播」嘅分支
根本從來冇行過：

* `GetPixmapAnimation()` 從來冇行過 `else { delete pma; return nullptr; }`，
  所以查 `nullptr` 嘅 caller（例如 `doPindianAnimation()` 嘅
  `else pindian_box->disappear()`）從來冇收過；
* `_createEquipBorderAnimations()` 從來冇行過 `!valid()`，`_m_equipBorders[i]`
  從來冇被設做 `nullptr`。

冇資產時真正發生嘅係：每個動畫多一個睇唔到嘅 1x1 sprite 加一個 20Hz timer。
唔係 crash，但 fallback 從來冇跑過 —— REDUCED／NONE 一旦開始靠佢哋，就正正
係最唔想見到嘅狀態。`setPath()` 改成普通 `while`，只讀真係存在嘅 frame。
資產齊嗰陣行為完全一樣（loop 條件本來就係同一個 `QFile::exists()`）。

`valid()` 誠實返之後，以下三條路由「不可達」變成「可達」，所以要補守衛：

* **永久黑幕**：`doLightboxAnimation()` 嘅 `anim=` 分支起咗塊 80% 不透明嘅
  rect，只喺 `PixmapAnimation::finished()` 先拆。而家
  `GetPixmapAnimation()` 真係會回 `nullptr`，塊 rect 就會永遠留喺畫面 ——
  遊戲仲玩得但係乜都睇唔到。已改成即刻拆走並且 warn。
* **裝備牌 nullptr deref**：`_setEquipBorderAnimation()` 用 `Q_ASSERT` 守住
  `_m_equipBorders[index]`，但 `Q_ASSERT` 喺 Release／RelWithDebInfo 係 no-op。
  已改成真 null check。
* **`PixmapAnimation` 自己**：`_m_timerId`／`current`／`off_x`／`off_y` 都未
  初始化（未 `start()` 就 `stop()` 會殺一個垃圾 timer id），而
  `paint()`／`boundingRect()`／`advance()` 都冇檢查 `frames` 空唔空。四樣都
  補咗。

同上面無關、獨立嘅一個：

* **動態立繪 nullptr deref**：`GraphicsPixmapHoverItem` 喺 item 未入 scene
  時 `m_proxyWidget` 會留低 null，跟住照 `->show()`。已改成落返靜態立繪。

### 設定

| Key | 預設 | 說明 |
|---|---|---|
| `EffectsProfile` | `full` | `full` / `reduced` / `none`；設定對話框「視覺特效」 |

Key 名 Windows／Linux 共用；舊設定檔冇呢個 key 或者值唔認得都會落返 `full`
並且喺 log 講明點解。

## 4.9 WSLg 人手驗證

Xvfb CI 通過**唔可以**取代 WSLg 人手驗證：Xvfb 冇 compositor，亦唔會行 WSLg 的
Wayland／X11 橋接。喺 WSLg 下重複以下步驟：

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
| Renderer | 預設（OpenGL）同 `QT_QUICK_BACKEND=software` 都通過 |
| `--ui-startup-smoke` | PASS，exit code 0，6 個 stage 全部 `ok:true` |
| 可見啟動 `./debug/QSanguosha` | PASS，開到主視窗，`Home QML status: QQuickWidget::Ready` |

> `qsanguosha_engine` 係 STATIC library，用 [`$<LINK_LIBRARY:WHOLE_ARCHIVE,...>`](../CMakeLists.txt) 在 `qsanguosha_server` 引入 source，淨係 link `Qt6::Core` 同 `Qt6::Network`。CMake 有 allowlist gate，link 咗其他 Qt target（例如 Widgets）會立刻 `FATAL_ERROR`。

## 5. Lua / SWIG

- Lua 原始碼 commit 喺 `src/lua/`（`lapi.c`、`lbaselib.c` 等），直接 build 入 `qsanguosha_engine`。
- SWIG binding 係 CMake 自動生成：`${binaryDir}/generated/sanguosha_wrap.cxx`，由 `swig/sanguosha.i` 生成。wrapper 唔會 commit，亦唔會放喺 source tree。
- 需要 `swig` 喺 PATH；CMake 用 `find_program(QSAN_SWIG_EXECUTABLE NAMES swig swig.exe ... REQUIRED)`。

## 6. Deploy server

Build 完 `qsanguosha_server`，要將 `lua/` 目錄 copy 去 executable 旁邊先至可以正常行（Lua 技能／extensions 需要呢份 runtime data）：

```bash
cmake --build build-linux-gcc --target deploy-server
```

`deploy-server` target 會執行 `${CMAKE_COMMAND} -E copy_directory lua $<TARGET_FILE_DIR:qsanguosha_server>/lua`。

或者手動 copy：

```bash
cp -r lua <executable_dir>/lua
```

## 7. 執行 headless server

Dedicated server 可以用獨立 INI 完整設定，唔需要開 GUI `ServerDialog`。設定優先次序固定為：

```text
程式預設 → 原有 QSettings → --config 指定嘅 INI → CLI --xxx override
```

INI overlay 同 CLI override 只影響今次 process，唔會寫回來源 INI。先複製範例並驗證：

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

# 由完整 INI 啟動，再用 CLI 覆蓋最常改嘅 port／模式
./qsanguosha_server --config server.ini --port 9527 --game-mode 10p

# 測試／編排用：由 kernel 選擇未使用 port
./qsanguosha_server --config server.ini --bind-address 127.0.0.1 --port 0

# 測試用：固定 random seed、停用 AI、取消操作時限
./qsanguosha_server --seed 12345 --ai off --operation-timeout 0

# 輸出 [AUTOTEST] marker 去檔案（stdout redirect 會 buffer，檔案較可靠）
./qsanguosha_server --autotest-log /tmp/autotest.log

# 顯示完整有效設定，唔會 listen
./qsanguosha_server --config server.ini --print-config
./qsanguosha_server --config server.ini --print-config --json
```

| 參數 | 用途 |
|---|---|
| `-h`, `--help` | 顯示 help 後退出 |
| `-v`, `--version` | 顯示版本後退出 |
| `-p`, `--port <0-65535>` | TCP listen port；`0` 由 kernel 分配 ephemeral port |
| `--bind-address <value>` | 數字 IPv4／IPv6，或 `any`、`any-ipv4`、`any-ipv6` |
| `-m`, `--game-mode <id>` | 今次使用嘅遊戲模式 |
| `-n`, `--server-name <name>` | 對外顯示嘅伺服器名稱 |
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

- 基本設定：`ServerName`、`GameMode`、`BindAddress`、`ServerPort`、操作／開局倒數。
- 遊戲規則：`BanPackages`、`RandomSeat`、作弊／自由選將、雙將、同將、暗將、國戰、混戰及體力方案。
- AI／服務：AI delay、禁聊、同 IP 限制、投降、手氣卡、Lua、神將、UPnP／列表伺服器。
- 模式設定：`1v1/*`、`3v3/*`、`XMode/*`、`Banlist/*`。
- Boss mode：難度 bitmask、十殿閻羅、經驗、可選 Boss、無盡及回合限制。

完整可修改範例見 [`server.ini.example`](server.ini.example)。未知 key、錯誤 boolean／enum 或超出範圍嘅數字會令 `--check-config` 失敗；啟動時讀到無效外部 INI 亦會拒絕啟動。

CLI 格式錯誤使用 exit code `64`，設定錯誤使用 `78`，初始化失敗係 `1`，listen 失敗係 `2`。Linux 上 `SIGINT`／`SIGTERM` 會 clean shutdown。CLI parser 位於 `src/server/server-command-line.cpp`，INI schema 位於 `src/server/server-config.cpp`，process 啟動流程位於 `src/server-main.cpp`。

成功 listen 後會輸出 socket 實際綁定嘅 endpoint，例如 `Listening on 127.0.0.1:43817`；使用 `--port 0` 時，CI client 應由呢一行取得 kernel 分配嘅 port。

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
| `kick <player-id>` | 按 `players` 顯示嘅精確 ID 斷開玩家 |
| `shutdown` | 經正常 Qt shutdown 流程停止 server |

Console 只經 `Server` 嘅 snapshot／管理 API 操作，唔會持有 `Room *` 或 `ServerPlayer *`。stdin 使用 Qt socket notifier 非阻塞讀取；stdin 關閉時只停用 console，server 仍繼續運行，適合由 systemd 配合 signal 管理。

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

Qt warning／critical 亦會經相同 sink，以 `component=qt` 記錄。`--autotest-log` 仍係獨立 automation marker，唔應當作 production log。

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

`LogsDirectory=qsanguosha` 會建立可寫嘅 `/var/log/qsanguosha`。若安裝 prefix 唔係 `/usr/local`，以 CMake 產生並安裝嘅 unit 內實際路徑為準。

## 8. 測試 (CTest)

`tests/` 目錄有 CTest。Configure 之後直接行：

```bash
cmake --build build-linux-gcc
ctest --test-dir build-linux-gcc --output-on-failure
```

Linux CTest 的單一 `qsanguosha_network_integration` suite 依序執行三級真實 TCP
network integration，並逐 level 輸出 PASS/FAIL 與結尾摘要：

1. Level 1：啟動 server、TCP connect／disconnect，確認 server 仍可回應 console，再以 SIGTERM 正常退出。
2. Level 2：完成 version／setup handshake、signup，從 `players` snapshot 確認 server 已識別玩家，再正常斷線。
3. Level 3：兩個 TCP client handshake／signup、填滿 `02p` room、開局後轉托管、完成自動對局、收到 game over、等待 room dispose，再驗證 SIGTERM clean exit 同 `CARD_LIFETIME_ZERO`。

三個 child case 使用獨立臨時 `XDG_CONFIG_HOME`、CLI `--port 0` 同固定 seed，
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

Linux 有兩個獨立 workflow，刻意唔合併：server CI 保持穩定，唔會俾 GUI dependency／runtime 問題污染。

### 9.1 `linux-server-ci.yml`

會喺 Ubuntu 24.04 並行驗證 GCC 同 Clang：

1. 安裝 Qt6／Ninja 同 hash-pinned SWIG 4.3.1。
2. 下載 `lua/ai/`、`extensions/` 同共用 Lua runtime。
3. 以 RelWithDebInfo configure、build，再執行完整 CTest，包括三級 TCP network integration。
4. 另跑 server process smoke，從實際 endpoint 建立 TCP 連線，再確認可由 SIGTERM clean shutdown。
5. 無論成功或失敗都上傳 JUnit 同 server log。

本機可以用相同 smoke script 驗證：

```bash
QSAN_SERVER_SMOKE_TIMEOUT_SECONDS=8 \
    bash tools/ci/server-shutdown-smoke.sh \
    debug/qsanguosha_server /tmp/server-shutdown.log

QSAN_SERVER_SMOKE_TIMEOUT_SECONDS=8 \
    bash tools/ci/server-console-smoke.sh \
    debug/qsanguosha_server /tmp/server-console.log
```

### 9.2 `linux-gui-ci.yml`

Linux GUI M0 compile CI，Ubuntu 24.04 + GCC + Ninja + RelWithDebInfo：

1. 用 `jurplel/install-qt-action` 裝官方 **Qt 6.11.1**（`linux_gcc_64`，modules `qt5compat qtmultimedia`），同 Windows job 用同一個 aqt pin。Ubuntu 24.04 apt 只有 Qt 6.4.2，唔夠 GUI baseline。
2. apt 裝非 Qt 依賴（`libfreetype-dev`、`libgl-dev`、`ninja-build` 等）同 hash-pinned SWIG 4.3.1。
3. 下載 `lua/ai/`、`extensions/` 同共用 Lua runtime。
4. 以 `-DQSAN_BUILD_GUI=ON -DQSAN_BUILD_SERVER=ON` configure。
5. 只 build target `QSanguosha`，驗證 configure／compile／link。
6. `file` + `ldd` 檢查，出現 `not found` 即 fail。
7. 跑 `--local-response-ui-capabilities` binary capability smoke。

M1 喺同一個 job 加咗 Xvfb + xcb 嘅 runtime startup smoke（見 [4.5](#45-linux-gui-m1-startup-smoke)）。

M2 再加兩個**分層**嘅 job，用 artifact 收下 compile job 砌好嘅同一份 binary 同
runtime Lua 內容（唔重新 fetch extensions，免得兩個 job 拎到唔同版本）：

```
Linux GUI compile ── M1 startup smoke
        ↓ linux-gui-runtime-bundle
Linux GUI M2B-A multimedia
```

M2B-A 的 multimedia job（見 [4.7](#47-linux-gui-m2b-a-multimedia-smoke)）接
`linux-gui-runtime-bundle`，用同一份 binary 行四個步驟：Xvfb+xcb 主驗證、影片缺
資產降級、offscreen 次要驗證、timeout 負向契約。四個都係阻擋性。artifact 收
multimedia report JSON、stdout/stderr、Qt multimedia plugin 診斷同 exit status。

> **M2 的 network game job 已經由 CI 移除（2026-08-28）。** runner 冇美術／音訊
> 資產，喺無資產環境下 client 同 server 打完一局之後會一齊 SIGSEGV；同一個
> binary 喺齊資產嘅本機係 8/8 PASS。Windows 環境同樣有呢個問題，headless mode
> 閃退本身亦係遊戲中已有現象。所以 `gui_network_smoke.py` 改為**本機 gate**，
> 見 [§4.6](#46-linux-gui-m2-network-smoke真實-tcp-對局)。

呢個 workflow **刻意唔做**：Spine／GIF／完整動畫 profile、visible startup、
Wayland、AppImage、pixel screenshot gate。呢啲留俾 M2B-B／M3。

Linux Server CI 繼續用 distro Qt，唔會受 GUI 的 Qt 6.11 baseline 影響。

## 10. 常見問題

- **搵唔到 Qt6**：確認裝咗 `qt6-base-dev`；啟用 CTest 時亦要裝 `qt6-5compat-dev`。亦可設定 `-DCMAKE_PREFIX_PATH=/path/to/qt6`。
- **`QSAN_BUILD_GUI=ON` 之後 configure 話 Qt6 版本不相容**：GUI 要求 Qt ≥ 6.11。用 [2.2](#22-gui-clientqsan_build_guion) 的 aqt 步驟裝 Qt 6.11.1，再用 `CMAKE_PREFIX_PATH` 指過去。
- **`QSAN_BUILD_GUI=ON` 之後 configure 話搵唔到 `LinguistTools`／`Quick`／`Multimedia`**：裝齊 [2.2](#22-gui-clientqsan_build_guion) 嘅 GUI 套件。Qt6 嘅 component config 必須同 `Qt6Config.cmake` 放喺同一個 cmake 目錄，所以唔可以只靠 `CMAKE_PREFIX_PATH` 指去另一個 prefix 補件（混用 distro Qt ＋ 另一個 prefix 唔會 work）。
- **`ldd` 見到 `libQt6QuickTemplates2.so.6 => not found`**：Qt Quick Controls 嘅間接依賴。正常系統安裝唔會出現；如果 Qt 唔喺標準 loader path，要設定 `LD_LIBRARY_PATH`（`RUNPATH` 唔會傳遞到間接依賴）。
- **搵唔到 swig**：`sudo apt install swig`，或將 swig 放喺 `tools/swig/`。
- **`qsanguosha_engine` link 到 Qt 之外嘅 target 而 FATAL_ERROR**：呢個係 design 嘅 allowlist gate，唔可以 hack 過去。
- **build 完跑唔起，話搵唔到 lua**：行 `--target deploy-server`，或手動 `cp -r lua <exe_dir>/lua`。
- **Deterministic 對比**：用 `--seed` 令 `QT_HASH_SEED=0`，配合相同 package set／AI 喺 Windows／Linux 產生相同 hash。
