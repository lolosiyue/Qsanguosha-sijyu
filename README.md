# QSanguosha-v2

[中文版](./README_zh.md) | English

An open-source clone of the popular board game _Sanguosha_, built with C++17 and Qt 6.11.1. This project is positioned as a **modpack-style distribution**, focusing on a "mishmash" of extensive content and AI expansion chaos.

## 🚀 Key Features

### 🖥️ Technical Evolution

- **64-bit Architecture**: Fully migrated for superior memory management.
- **Decoupled Engine & UI**: Resolved mutex deadlocks and memory address issues.
- **GPU Acceleration**: Switched to `QOpenGLWidget` for smoother Spine animations.
- **Memory Safety**: Migrated to `QPointer` to prevent dangling pointers.

### ✨ Visual & UI Enhancements

- **Real-time Handcard Limit**: Dynamic tracking with Red/Green buff/debuff indicators.
- **Spine Animation System**: OpenGL-based `SpineGlItem` for "out-of-frame" dynamic effects.
- **Universal Pile Viewer (F11)**: Property-driven tool supporting multiple skill-specific card piles.

### 🧠 Intelligent AI

- **Weighted Target Selection**: Probabilistic model based on dynamic threat scores.
- **Context-Aware Strategy**: Enhanced evaluation for alliances and card threats (`evaluateCardThreat`).

## 🛠️ Development Environment

- **Framework**: Qt 6.11.1 (`msvc2022_64`)
- **Compiler**: MSVC 2026 (VS 2026 v145 toolchain)
- **Build system**: CMake 4.2+
- **Generator**: Visual Studio 18 2026 x64

Set `QTDIR` to the Qt kit before using the presets:

```powershell
$env:QTDIR = 'H:\Qt6111\6.11.1\msvc2022_64'
cmake --preset vs2026-x64
cmake --build --preset release
```

Alternatively, use the PowerShell entry point:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-cmake.ps1 -Configuration Release
```

### 🐧 Linux (Playing the game)

Ready-made Linux builds need no development tools and no source tree. Take either:

```bash
# Portable bundle
tar --zstd -xf QSanguosha-<version>-linux-x86_64.tar.zst
cd QSanguosha-<version>-linux-x86_64
./QSanguosha                 # GUI
./qsanguosha-server          # dedicated server

# AppImage
chmod +x QSanguosha-<version>-x86_64.AppImage
./QSanguosha-<version>-x86_64.AppImage
```

Both carry their own Qt runtime, so no `LD_LIBRARY_PATH`, `QT_PLUGIN_PATH` or
`QML2_IMPORT_PATH` has to be set. Settings, replays and logs go to
`~/.config/QSanguosha.org` and `~/.local/share/QSanguosha`, never into the
package.

Large artwork and voice packs are **not** part of the download. The game runs
without them (placeholder visuals, no voice-over); point it at an external copy
with `--asset-root <path>` or `QSAN_ASSET_ROOT`. To see exactly what was found
and what is missing:

```bash
./QSanguosha --asset-report
```

There is no `.deb` yet: no current Ubuntu release ships Qt 6.11, which the GUI
requires. See [docs/linux-packaging.md](docs/linux-packaging.md) for the full
layout, the packaging pipeline and that decision.

### 🐧 Linux (Building from source)

A Linux build produces the **headless server** (`qsanguosha_server`) by default —
no GUI, no FMOD, no X11 dependency, linking just `Qt6::Core` and `Qt6::Network`.
Add `-DQSAN_BUILD_GUI=ON` for the GUI client; that needs Qt 6.11 or newer, which
is beyond what current distributions package.

```bash
sudo apt install -y build-essential cmake ninja-build qt6-base-dev qt6-5compat-dev swig

# GCC
cmake -S . -B build-linux-gcc -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_COMPILER=/usr/bin/c++
cmake --build build-linux-gcc
cmake --build build-linux-gcc --target deploy-server

# or Clang
cmake -S . -B build-linux-clang -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_COMPILER=/usr/bin/clang++
cmake --build build-linux-clang
```

Inspect the CLI and start a server with one-run overrides:

```bash
./qsanguosha_server --help
./qsanguosha_server --list-game-modes
cp docs/server.ini.example server.ini
./qsanguosha_server --config server.ini --check-config
./qsanguosha_server --config server.ini --port 9527 --game-mode 10p
./qsanguosha_server --config server.ini --bind-address 127.0.0.1 --port 0
./qsanguosha_server --config server.ini --log-level info --log-format json \
    --log-file /var/log/qsanguosha/server.log
```

The precedence is built-in defaults, normal QSettings, `--config` INI, then
explicit CLI overrides. Neither the INI overlay nor CLI overrides are written
back. Use `--print-config --json` to inspect the complete effective server
configuration before listening.
Port `0` requests an ephemeral port from the kernel; the startup line reports
the actual endpoint, for example `Listening on 127.0.0.1:43817`.

After the server starts, its terminal is an interactive administration console:

```text
server> status
server> players
server> rooms
server> say Server maintenance in 10 minutes
server> kick p001
server> shutdown
```

Run `help` in the console for the seven supported commands. `kick` requires the
exact ID shown by `players`. The console reads stdin without blocking the Qt
event loop; closing stdin disables console input but leaves the server running.

Linux installs also include `qsanguosha-server.service`. It keeps the process in
the foreground, sends `SIGTERM` for graceful shutdown, and uses
`Restart=on-failure`; the legacy daemon mode is not used. See the Linux guide
for installation and configuration commands.

CTest:

```bash
ctest --test-dir build-linux-gcc --output-on-failure
```

Linux CTest includes three real TCP integration levels, from connect/disconnect
through handshake/signup to a complete automated `02p` game and clean disposal.

See the full guide: [`docs/linux-development-environment.md`](docs/linux-development-environment.md).
For production-oriented container packaging, see
[`docs/docker-server.md`](docs/docker-server.md).

---

_For more details, see the [Credits & Disclaimer](#-credits--disclaimer) section in the full document._
