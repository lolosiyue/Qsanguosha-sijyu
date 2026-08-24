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

### 🐧 Linux (Headless Server)

On Linux the project builds the **headless server** (`qsanguosha_server`) only — no GUI, no FMOD, no X11 dependency. It links just `Qt6::Core` and `Qt6::Network`.

```bash
sudo apt install -y build-essential cmake ninja-build qt6-base-dev swig

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

Run the server, optional `--game-mode`, `--seed`, `--autotest-log`:

```bash
./qsanguosha_server [--game-mode 10p] [--seed 12345] [--autotest-log /tmp/autotest.log]
```

CTest:

```bash
ctest --test-dir build-linux-gcc --output-on-failure
```

See the full guide: [`docs/linux-development-environment.md`](docs/linux-development-environment.md).

---

_For more details, see the [Credits & Disclaimer](#-credits--disclaimer) section in the full document._
