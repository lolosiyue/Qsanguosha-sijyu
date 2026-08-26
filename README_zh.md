# 太陽神三國殺-v2 (QSanguosha-v2)

[English](./README.md) | 中文版

本项目是一個基於 C++ 和 Qt 框架的開源三國殺克隆版。定位類似於 **Minecraft 的整合包 (Modpack)**，重心在於海量 AI 擴展的大雜燴亂鬥。

## 🛠️ 建置環境

- **語言標準**：C++17
- **Qt**：6.11.1 (`msvc2022_64`)
- **編譯器**：MSVC 2026 x64（VS 2026 v145 toolchain）
- **建置系統**：CMake 4.2+

```powershell
$env:QTDIR = 'H:\Qt6111\6.11.1\msvc2022_64'
cmake --preset vs2026-x64
cmake --build --preset release
```

或使用統一腳本：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-cmake.ps1 -Configuration Release
```

### 🐧 Linux（無頭伺服器）

Linux 本階段只建置 **無頭伺服器**（`qsanguosha_server`），冇 GUI、冇 FMOD、冇 X11 依賴，只連結 `Qt6::Core` 同 `Qt6::Network`。

```bash
sudo apt install -y build-essential cmake ninja-build qt6-base-dev swig

# GCC
cmake -S . -B build-linux-gcc -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_COMPILER=/usr/bin/c++
cmake --build build-linux-gcc
cmake --build build-linux-gcc --target deploy-server

# 或者用 Clang
cmake -S . -B build-linux-clang -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_COMPILER=/usr/bin/clang++
cmake --build build-linux-clang
```

執行伺服器，可選 `--game-mode`、`--seed`、`--autotest-log`：

```bash
./qsanguosha_server [--game-mode 10p] [--seed 12345] [--autotest-log /tmp/autotest.log]
```

CTest：

```bash
ctest --test-dir build-linux-gcc --output-on-failure
```

完整指南見：[`docs/linux-development-environment.md`](docs/linux-development-environment.md)。
Docker 無頭伺服器用法見：[`docs/docker-server.md`](docs/docker-server.md)。

## 🚀 核心特性

### 🖥️ 技術演進

- **64位架構**: 全面轉向 64 位，提升內存處理上限。
- **UI 與引擎解耦**: 徹底重構 Mutex 邏輯，解決內存地址閃退頑疾。
- **GPU 渲染**: 轉向 `QOpenGLWidget` 利用硬體加速提升動畫流暢度。

### ⚔️ 玩法與機制

- **國戰概念移植**: 移植部分國戰武將至身份模式，並平衡勢力限制。
- **軍令系統**: 完整實現發令、摸牌及軍令效果結算邏輯。
- **高級戰場機制**: 引入 **圍攻/隊列** 空間位置博弈及 **調虎離山** 邏輯重構。

### 🧠 智能 AI

- **加權目標選擇**: 根據動態威脅分數（Threat Score）選擇最佳目標。
- **情境感知策略**: 包含合縱判斷及根據主公選將動態調整身份策略。

## 📜 致謝與聲明

- **版權尊重**: 若您是原作者且不希望作品被包含，請告知，將立即刪除。
- **穩定性提示**: 作為“大雜燴”版本，閃退頻率可能較高，建議高配置硬體游玩。
