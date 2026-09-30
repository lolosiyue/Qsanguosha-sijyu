# 太陽神三國殺-v2（QSanguosha-v2）

[English](README.md) | 繁體中文

QSanguosha-v2 是以 C++ 與 Qt 開發的開源三國殺遊戲，透過 Lua 擴充卡牌、武將、技能與 AI。專案包含桌面客戶端、獨立伺服器，以及終端、瀏覽器與試算表客戶端。

## 快速上手

使用發行包時，依照對應平台與版本的指南操作。從原始碼建置 Windows 版本，先安裝 [Windows 建置指南](docs/windows-build.md) 所列工具，在倉庫根目錄執行：

```powershell
$env:QTDIR = 'C:/Qt/6.11.1/msvc2022_64' # 改成已安裝的 Qt kit 路徑。
cmake --preset vs2026-x64
cmake --build --preset release
```

執行檔位於 `release/QSanguosha.exe`。啟動前完成[執行期部署與內容設定](docs/windows-build.md#runtime-deployment)。

## 操作指南

| 需求 | 文件 |
| --- | --- |
| Windows 建置與執行 | [Windows](docs/windows-build.md) |
| Linux 發行包與原始碼建置 | [封裝](docs/linux-packaging.md)、[開發環境](docs/linux-development-environment.md) |
| Docker 伺服器 | [Docker 部署](docs/docker-server.md) |
| Android 建置與安裝 | [Android](docs/android-build.md) |
| 終端與瀏覽器遊戲 | [TUI](docs/tui-client.md)、[Web](docs/web-client.md)、[Browser Solo](docs/browser-solo.md) |
| 試算表客戶端 | [Excel](docs/excel-packaging.md)、[Google Sheets](docs/google-sheets-client.md) |
| 驗證改動（smoke 與 headless 對局） | [驗證方式](docs/linux-development-environment.md#8-自動化驗證)、[autotest 工具](tools/autotest/README.md) |
| 擴展開發與引擎參考 | [文件索引](docs/README.md) |

## 倉庫外的內容

倉庫不追蹤 Lua AI、擴展，以及美術、音效與字型。

- `lua/ai/`、`extensions/` 與 `lua/luaoldenemy_lib.lua` 來自獨立的 [extensions 倉庫](https://github.com/lolosiyue/extensions)（`main`）。執行 `bash tools/ci/fetch-extensions.sh <倉庫根目錄>` 取得；Windows 對應腳本為 `tools/ci/fetch-extensions.ps1`。
- `image/`、`audio/`、`font/`、`hero-skin/` 屬於發行素材。缺少時遊戲仍可啟動並回報缺少的 optional 資產；`QSanguosha --asset-report` 會列出缺項。
- 倉庫內沒有單元測試與 CTest（2026-09-25 移除）；改動以上面連結的 smoke 腳本與 headless 對局驗證。

## 授權與歸屬

倉庫保留 [LICENSE](LICENSE) 與 [GNU GPL v3](gpl-3.0.txt) 原文；個別原始碼及第三方元件的適用條款見各自聲明。圖片、音效及外部擴展保留其作者的歸屬與授權。
