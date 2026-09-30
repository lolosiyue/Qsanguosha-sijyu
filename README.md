# QSanguosha-v2

[繁體中文](README_zh.md) | English

QSanguosha-v2 is an open-source Sanguosha game built with C++ and Qt, with Lua extensions for cards, generals, skills and AI. It includes a desktop client, a dedicated server and alternative clients for terminal, browser and spreadsheet use.

## Quick start

For a packaged build, follow the guide for its platform and version. For a Windows source build, install the toolchain listed in the [Windows build guide](docs/windows-build.md), set the path to your Qt kit, and run from the repository root:

```powershell
$env:QTDIR = 'C:/Qt/6.11.1/msvc2022_64' # Replace with your installed kit.
cmake --preset vs2026-x64
cmake --build --preset release
```

The executable is `release/QSanguosha.exe`. Complete [runtime deployment and content setup](docs/windows-build.md#runtime-deployment) before starting it.

## Choose a guide

| Task | Guide |
| --- | --- |
| Build and run on Windows | [Windows](docs/windows-build.md) |
| Use a Linux package or build from source | [Packages](docs/linux-packaging.md), [development](docs/linux-development-environment.md) |
| Run a server in Docker | [Docker server](docs/docker-server.md) |
| Build and install on Android | [Android](docs/android-build.md) |
| Play in a terminal or browser | [TUI](docs/tui-client.md), [Web](docs/web-client.md), [Browser Solo](docs/browser-solo.md) |
| Use spreadsheet clients | [Excel](docs/excel-packaging.md), [Google Sheets](docs/google-sheets-client.md) |
| Verify a change (smoke tests, headless games) | [Verification](docs/linux-development-environment.md#8-自動化驗證), [autotest tools](tools/autotest/README.md) |
| Develop extensions or explore the engine | [Documentation index](docs/README.md) |

## Content outside this repository

The repository does not track the Lua AI and extensions, or the art, audio and fonts.

- `lua/ai/`, `extensions/` and `lua/luaoldenemy_lib.lua` come from the separate [extensions repository](https://github.com/lolosiyue/extensions) (`main`). `bash tools/ci/fetch-extensions.sh <repository-root>` fetches them; the Windows equivalent is `tools/ci/fetch-extensions.ps1`.
- `image/`, `audio/`, `font/` and `hero-skin/` are release assets. Without them the game still starts and reports missing optional assets; `QSanguosha --asset-report` lists what is missing.
- There are no unit tests or CTest in the repository (removed 2026-09-25); changes are verified with the smoke scripts and headless games linked above.

## Licenses and attribution

The repository retains [LICENSE](LICENSE) and [GNU GPL v3](gpl-3.0.txt). See the notices in individual source files and third-party components for their applicable terms. Artwork, audio and external extensions retain their authors' notices and licenses.
