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

---

_For more details, see the [Credits & Disclaimer](#-credits--disclaimer) section in the full document._
