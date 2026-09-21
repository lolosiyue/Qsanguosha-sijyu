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
| 試算表客戶端 | [Excel](docs/excel-client.md)、[Google Sheets](docs/google-sheets-client.md) |
| 擴展開發與引擎參考 | [文件索引](docs/README.md) |

## 授權與歸屬

倉庫保留 [LICENSE](LICENSE) 與 [GNU GPL v3](gpl-3.0.txt) 原文；個別原始碼及第三方元件的適用條款見各自聲明。圖片、音效及外部擴展保留其作者的歸屬與授權。
