# QSanguosha-v2

[中文版](./README_zh.md) | English

An open-source clone of the popular board game _Sanguosha_, built with C++ and the Qt framework (Qt 6.5.3). This project is positioned as a **modpack-style distribution**, focusing on a "mishmash" of extensive content and AI expansion chaos.

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

## 🛠️ Build Instructions

### Prerequisites

- **Qt**: 6.5.3 (msvc2022_64)
- **Compiler**: MSVC 2022 (Visual Studio 2022)
- **Qt/MSBuild Toolset**: 3.5.0.0

### Build Steps

```powershell
# Using the release build script
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-release.ps1

# Or build the .pro file directly with qmake
# (requires Qt bin directory in PATH)
qmake QSanguosha.pro
nmake release
```

### Output

- Executable: `release\QSanguosha.exe`
- Library: `release\QSanguosha.lib`

---

_For more details, see the [Credits & Disclaimer](#-credits--disclaimer) section in the full document._
