# Linux Development Environment

Linux 本階段只交付 **無頭伺服器 (headless server)**，唔會 build GUI 客戶端。目標 output 係 `qsanguosha_server`，使用 `QCoreApplication`，唔需要 X11／Wayland、FMOD 或任何 GUI／Qt Widgets／Quick／Multimedia 依賴。

- Status: In Progress（`linux-headless-server` branch）
- Last Updated: 2026-08-24
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
    qt6-base-dev \
    swig
```

對應套件包：

- **build-essential** — `gcc`／`g++`、`make` 等
- **cmake** — 建置系統
- **ninja-build** — Ninja generator（快速）
- **qt6-base-dev** — Qt6 Core／Network head 同 library（`/usr/lib/x86_64-linux-gnu/cmake/Qt6*`）
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
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_COMPILER=/usr/bin/c++
cmake --build build-linux-gcc
```

### 3.2 Clang 版

```bash
cmake -S . -B build-linux-clang -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_COMPILER=/usr/bin/clang++
cmake --build build-linux-clang
```

### 3.3 Build 做完嘅 output

`qsanguosha_server` 會生成喺 each build tree 嘅 subdirectory（例如 `build-linux-gcc/qsanguosha_server`）。因為 `CMakeLists.txt` 用 `RUNTIME_OUTPUT_DIRECTORY_*` 指去 `sourceDir/debug`、`release`、`relwithdebinfo`，Linux 都會照跟（Windows 先至係主要用家，不過無妨）。

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

確認 `config.ini` 用邊個 GameMode 同 port；`config.ini` 由 QSettings 處理。

```bash
# 基本啟動（listen 喺 config 指定 port）
./qsanguosha_server

# 指定 game mode，例如 10p / 20p / 02_1v1 / 05p
./qsanguosha_server --game-mode 10p

# 固定 random seed（test 用，同一 seed 結果 deterministic）
./qsanguosha_server --seed 12345

# 輸出 [AUTOTEST] marker 去檔案（stdout redirect 會 buffer，檔案較可靠）
./qsanguosha_server --autotest-log /tmp/autotest.log
```

所有參數喺 `src/server-main.cpp` 嘅 `parseArguments()` 處理。Linux 上 `SIGINT`／`SIGTERM` 會 clean shutdown（見 `src/server-main.cpp`）。

## 7. 測試 (CTest)

`tests/` 目錄有 CTest。Configure 之後直接行：

```bash
cmake --build build-linux-gcc
ctest --test-dir build-linux-gcc --output-on-failure
```

測試包括 engine smoke test、card-lifetime、player-decision-service、room-runtime-isolation、protocol messages、request-coordinator、room-roster、player-lifecycle-service、skill-runtime-coordinator、lua-runtime-isolation、extra-turn-scheduler 等。可配置 `-DBUILD_TESTING=OFF` 跳過。

## 8. 常見問題

- **搵唔到 Qt6**：確認裝咗 `qt6-base-dev`，或設定 `-DCMAKE_PREFIX_PATH=/path/to/qt6`。
- **搵唔到 swig**：`sudo apt install swig`，或將 swig 放喺 `tools/swig/`。
- **`qsanguosha_engine` link 到 Qt 之外嘅 target 而 FATAL_ERROR**：呢個係 design 嘅 allowlist gate，唔可以 hack 過去。
- **build 完跑唔起，話搵唔到 lua**：行 `--target deploy-server`，或手動 `cp -r lua <exe_dir>/lua`。
- **Deterministic 對比**：用 `--seed` 令 `QT_HASH_SEED=0`，配合相同 package set／AI 喺 Windows／Linux 產生相同 hash。
