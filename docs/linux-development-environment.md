# Linux Development Environment

Linux 本階段只交付 **無頭伺服器 (headless server)**，唔會 build GUI 客戶端。目標 output 係 `qsanguosha_server`，使用 `QCoreApplication`，唔需要 X11／Wayland、FMOD 或任何 GUI／Qt Widgets／Quick／Multimedia 依賴。

- Status: CMake／CI Complete；M5 完整對局驗證仍 In Progress（`debug` branch）
- Last Updated: 2026-08-25
- 對應 Windows 開發環境請見 [`README.md`](../README.md) 嘅 🛠️ Development Environment section。

## 1. 平台基線

| 項目 | 基線 |
|---|---|
| 平台 | Ubuntu 24.04 x64（亦相容其他 Linux distro） |
| C++ | C++17（`CMAKE_CXX_STANDARD 17`） |
| Qt | 系統 Qt 6（本機 developer 裝 `qt6-base-dev`，版本 6.10.2） |
| Lua | 內建喺 `src/lua/`（SWIG 自動生成 binding） |
| 建置系統 | CMake 4.2+（`CMakeLists.txt` 下限係 3.28）、Ninja |
| Generator | Ninja（本機 `build-linux-gcc/`、`build-linux-clang/` 均用 Ninja） |
| 編譯器 | GCC（`/usr/bin/c++`）或者 Clang（`/usr/bin/clang++`） |

> 注意：Windows 用 Qt 6.11.1 `msvc2022_64`；Linux 直接用系統套件嘅 Qt 6，唔會 lock 到 6.11.1。只要 CMake 搵到 Qt6 Core／Network 就編到。

## 2. 系統依賴

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

如果想用 **Clang** 編譯，加：

```bash
sudo apt install -y clang
```

揀用邊個 compiler 就喺 configure 時將 `CMAKE_CXX_COMPILER` 指向對應嘅 path。

## 3. Configure + Build

呢個 repo 嘅 `CMakePresets.json` 目前淨係定義咗 Windows（`vs2026-x64`）preset，Linux 冇 preset，所以要直接行 CMake 命令。以下兩個目錄係本機已存在嘅 Linux build tree：

### 3.1 GCC 版

```bash
cmake -S . -B build-linux-gcc -G Ninja \
    -DBUILD_TESTING=ON \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=/usr/bin/gcc \
    -DCMAKE_CXX_COMPILER=/usr/bin/g++
cmake --build build-linux-gcc
```

### 3.2 Clang 版

```bash
cmake -S . -B build-linux-clang -G Ninja \
    -DBUILD_TESTING=ON \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=/usr/bin/clang \
    -DCMAKE_CXX_COMPILER=/usr/bin/clang++
cmake --build build-linux-clang
```

### 3.3 Build 做完嘅 output

`qsanguosha_server` 會按 build type 生成喺 source tree 嘅 `debug/`、`release/` 或 `relwithdebinfo/`（例如 Debug 係 `debug/qsanguosha_server`）。

> `qsanguosha_engine` 係 STATIC library，用 [`$<LINK_LIBRARY:WHOLE_ARCHIVE,...>`](../CMakeLists.txt) 在 `qsanguosha_server` 引入 source，淨係 link `Qt6::Core` 同 `Qt6::Network`。CMake 有 allowlist gate，link 咗其他 Qt target（例如 Widgets）會立刻 `FATAL_ERROR`。

## 4. Lua / SWIG

- Lua 原始碼 commit 喺 `src/lua/`（`lapi.c`、`lbaselib.c` 等），直接 build 入 `qsanguosha_engine`。
- SWIG binding 係 CMake 自動生成：`${binaryDir}/generated/sanguosha_wrap.cxx`，由 `swig/sanguosha.i` 生成。wrapper 唔會 commit，亦唔會放喺 source tree。
- 需要 `swig` 喺 PATH；CMake 用 `find_program(QSAN_SWIG_EXECUTABLE NAMES swig swig.exe ... REQUIRED)`。

## 5. Deploy server

Build 完 `qsanguosha_server`，要將 `lua/` 目錄 copy 去 executable 旁邊先至可以正常行（Lua 技能／extensions 需要呢份 runtime data）：

```bash
cmake --build build-linux-gcc --target deploy-server
```

`deploy-server` target 會執行 `${CMAKE_COMMAND} -E copy_directory lua $<TARGET_FILE_DIR:qsanguosha_server>/lua`。

或者手動 copy：

```bash
cp -r lua <executable_dir>/lua
```

## 6. 執行 headless server

CLI 未指定嘅值會沿用 Linux QSettings；CLI override 只影響今次 process，唔會寫回設定檔。先用以下指令查看完整介面同可用模式：

```bash
./qsanguosha_server --help
./qsanguosha_server --version
./qsanguosha_server --list-game-modes
```

常用啟動方式：

```bash
# 基本啟動（沿用已保存設定）
./qsanguosha_server

# 公開監聽 IPv4、指定 port／模式／名稱
./qsanguosha_server --bind-address any-ipv4 --port 9527 \
    --game-mode 10p --server-name "Linux server"

# 測試用：固定 random seed、停用 AI、取消操作時限
./qsanguosha_server --seed 12345 --ai off --operation-timeout 0

# 輸出 [AUTOTEST] marker 去檔案（stdout redirect 會 buffer，檔案較可靠）
./qsanguosha_server --autotest-log /tmp/autotest.log

# 顯示合併 QSettings 與 CLI override 後嘅有效設定，唔會 listen
./qsanguosha_server --port 19527 --game-mode 02p --print-config
```

| 參數 | 用途 |
|---|---|
| `-h`, `--help` | 顯示 help 後退出 |
| `-v`, `--version` | 顯示版本後退出 |
| `-p`, `--port <1-65535>` | TCP listen port |
| `--bind-address <value>` | 數字 IPv4／IPv6，或 `any`、`any-ipv4`、`any-ipv6` |
| `-m`, `--game-mode <id>` | 今次使用嘅遊戲模式 |
| `-n`, `--server-name <name>` | 對外顯示嘅伺服器名稱 |
| `--operation-timeout <0-86400>` | 操作時限（秒）；`0` 代表無限 |
| `--ai <on\|off>` | 啟用／停用 server AI |
| `--ai-delay <0-600000>` | AI 延遲（毫秒） |
| `-s`, `--seed <uint64>` | 固定遊戲 seed，方便重現測試 |
| `--autotest-log <path>` | 將 automation marker 寫入檔案 |
| `--list-game-modes` | 列出模式 ID／名稱後退出 |
| `--print-config` | 顯示有效設定後退出 |

格式錯誤或不支援嘅參數會以 exit code `64` 結束；初始化失敗係 `1`，listen 失敗係 `2`。Linux 上 `SIGINT`／`SIGTERM` 會 clean shutdown。解析器位於 `src/server/server-command-line.cpp`，process 啟動流程位於 `src/server-main.cpp`。

## 7. 測試 (CTest)

`tests/` 目錄有 CTest。Configure 之後直接行：

```bash
cmake --build build-linux-gcc
ctest --test-dir build-linux-gcc --output-on-failure
```

測試包括 server CLI parser／help／version、engine smoke test、card-lifetime、player-decision-service、room-runtime-isolation、protocol messages、request-coordinator、room-roster、player-lifecycle-service、skill-runtime-coordinator、lua-runtime-isolation、extra-turn-scheduler 等。可配置 `-DBUILD_TESTING=OFF` 跳過。

## 8. GitHub Actions CI

`.github/workflows/linux-server-ci.yml` 會喺 Ubuntu 24.04 並行驗證 GCC 同 Clang：

1. 安裝 Qt6／Ninja 同 hash-pinned SWIG 4.3.1。
2. 下載 `lua/ai/`、`extensions/` 同共用 Lua runtime。
3. 以 RelWithDebInfo configure、build，再執行完整 CTest。
4. 啟動 `qsanguosha_server`，確認成功 listen 並可由 SIGTERM clean shutdown。
5. 無論成功或失敗都上傳 JUnit 同 server log。

本機可以用相同 smoke script 驗證：

```bash
QSAN_SERVER_SMOKE_TIMEOUT_SECONDS=8 \
    bash tools/ci/server-shutdown-smoke.sh \
    debug/qsanguosha_server /tmp/server-shutdown.log
```

## 9. 常見問題

- **搵唔到 Qt6**：確認裝咗 `qt6-base-dev`；啟用 CTest 時亦要裝 `qt6-5compat-dev`。亦可設定 `-DCMAKE_PREFIX_PATH=/path/to/qt6`。
- **搵唔到 swig**：`sudo apt install swig`，或將 swig 放喺 `tools/swig/`。
- **`qsanguosha_engine` link 到 Qt 之外嘅 target 而 FATAL_ERROR**：呢個係 design 嘅 allowlist gate，唔可以 hack 過去。
- **build 完跑唔起，話搵唔到 lua**：行 `--target deploy-server`，或手動 `cp -r lua <exe_dir>/lua`。
- **Deterministic 對比**：用 `--seed` 令 `QT_HASH_SEED=0`，配合相同 package set／AI 喺 Windows／Linux 產生相同 hash。
