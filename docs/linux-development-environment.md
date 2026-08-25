# Linux Development Environment

Linux 本階段只交付 **無頭伺服器 (headless server)**，唔會 build GUI 客戶端。目標 output 係 `qsanguosha_server`，使用 `QCoreApplication`，唔需要 X11／Wayland、FMOD 或任何 GUI／Qt Widgets／Quick／Multimedia 依賴。

- Status: CMake／CI／三級 TCP Network Integration Complete（`debug` branch）
- Last Updated: 2026-08-26
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

## 7. 測試 (CTest)

`tests/` 目錄有 CTest。Configure 之後直接行：

```bash
cmake --build build-linux-gcc
ctest --test-dir build-linux-gcc --output-on-failure
```

Linux CTest 包括三級真實 TCP network integration：

1. Level 1：啟動 server、TCP connect／disconnect，確認 server 仍可回應 console，再以 SIGTERM 正常退出。
2. Level 2：完成 version／setup handshake、signup，從 `players` snapshot 確認 server 已識別玩家，再正常斷線。
3. Level 3：兩個 TCP client handshake／signup、填滿 `02p` room、開局後轉托管、完成自動對局、收到 game over、等待 room dispose，再驗證 SIGTERM clean exit 同 `CARD_LIFETIME_ZERO`。

三個測試使用獨立臨時 `XDG_CONFIG_HOME`、CLI `--port 0` 同固定 seed，從 `Listening on` 取得實際 port，標記為 `network;server;integration` 並強制 serial 執行。只跑 network suite：

```bash
ctest --test-dir build-linux-gcc --output-on-failure -L network
```

其餘測試包括 server CLI parser／help／version、INI validation／precedence、7-command console smoke、engine smoke、card-lifetime、player-decision-service、room-runtime-isolation、protocol messages、request-coordinator、room-roster、player-lifecycle-service、skill-runtime-coordinator、lua-runtime-isolation、extra-turn-scheduler 等。可配置 `-DBUILD_TESTING=OFF` 跳過。

## 8. GitHub Actions CI

`.github/workflows/linux-server-ci.yml` 會喺 Ubuntu 24.04 並行驗證 GCC 同 Clang：

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

## 9. 常見問題

- **搵唔到 Qt6**：確認裝咗 `qt6-base-dev`；啟用 CTest 時亦要裝 `qt6-5compat-dev`。亦可設定 `-DCMAKE_PREFIX_PATH=/path/to/qt6`。
- **搵唔到 swig**：`sudo apt install swig`，或將 swig 放喺 `tools/swig/`。
- **`qsanguosha_engine` link 到 Qt 之外嘅 target 而 FATAL_ERROR**：呢個係 design 嘅 allowlist gate，唔可以 hack 過去。
- **build 完跑唔起，話搵唔到 lua**：行 `--target deploy-server`，或手動 `cp -r lua <exe_dir>/lua`。
- **Deterministic 對比**：用 `--seed` 令 `QT_HASH_SEED=0`，配合相同 package set／AI 喺 Windows／Linux 產生相同 hash。
