# Linux packaging（Linux GUI M3）

本文件說明如何由一個 build tree 製作出愛好者可以直接下載執行的 Linux 版本，
以及每一步為何這樣做。目標很簡單：**不需要開發工具、不需要 source tree，
下載、解壓、雙擊即可遊玩。**

前置里程碑：M0 compile/link、M1 real startup、M2 playable network flow、
M2B-A multimedia、M2B-B effects profiles，全部已經在 `debug`。

---

## 1. 交付物

| 檔案 | 用途 |
| --- | --- |
| `QSanguosha-<version>-linux-x86_64.tar.zst` | 可攜包，解壓即用，含 GUI、dedicated server 與 TUI client |
| `QSanguosha-<version>-x86_64.AppImage` | 單檔案 AppImage，含 desktop entry 與圖示 |
| `build-info.json` | 版本、git SHA、Qt 版本、compiler、build type、資產清單版本、時間戳 |
| `SHA256SUMS` | 上面每個檔案的 SHA-256 |
| `deploy-report.json` | 打包了哪些 Qt library／plugin／QML module，以及哪些保留給系統 |
| `audit-*.json` | RPATH／開發機路徑／系統 library 稽核結果 |

一條指令完成所有工作：

```bash
python3 tools/packaging/build-linux-packages.py \
    --build-dir build/package --source-dir . \
    --qt-prefix "$QT_ROOT_DIR" --qt-version 6.11.1 \
    --output-dir dist --forbid "$HOME"
```

兩個交付物用**同一份** staging tree 打包出來。一個 bug 在其中一邊出現，就一定
在另一邊都出現 —— 沒有第二份「包內應該有什麼」的定義。

---

## 2. 安裝樹版面

`cmake --install` 遵循 GNUInstallDirs：

```text
<prefix>/bin/QSanguosha
<prefix>/bin/qsanguosha_server
<prefix>/bin/qsanguosha_tui                TUI client（QSAN_BUILD_TUI 預設 ON）
<prefix>/bin/qt.conf                       Qt 私有 plugin／QML 路徑
<prefix>/lib/qsanguosha/qt/lib/            私有 Qt runtime
<prefix>/lib/qsanguosha/qt/plugins/<type>/
<prefix>/lib/qsanguosha/qt/qml/<Module>/
<prefix>/lib/systemd/system/qsanguosha-server.service
<prefix>/share/qsanguosha/lua|extensions|lang|etc|qss|skins|ui-script
<prefix>/share/qsanguosha/translations/
<prefix>/share/qsanguosha/assets-manifest.json
<prefix>/share/applications/qsanguosha.desktop
<prefix>/share/icons/hicolor/{16..512}x*/apps/qsanguosha.png
<prefix>/share/icons/hicolor/scalable/apps/qsanguosha.svg
<prefix>/share/doc/QSanguosha/
```

四個 install component，令 GUI、server 與 TUI 可以各自出貨：

```text
qsan_data    lua / extensions / lang / etc —— 兩邊都要的規則資料
qsan_server  server binary、systemd unit、server 文件
qsan_tui     TUI binary、TUI 文件（docs/tui-client.md）
qsan_gui     GUI binary、介面資產、desktop entry、圖示
```

`QSAN_BUILD_GUI=OFF` 的 server-only 安裝**不會**帶任何 GUI 資產，亦不會
link 任何 GUI Qt library；packaging CI 每次都驗證這兩點。

資料目錄是「有才裝」：`install(DIRECTORY)` 遇到一個不存在的來源目錄會直接
fatal，而不是每個 build context 都具備全部內容 —— Docker server image 的
`.dockerignore` 就特意剔除 `qss/`、`skins/`、`ui-script/`，
`extensions/` 也是在 build 過程中 fetch 回來的。所以這些目錄藏在
`QSAN_DATA_DIRECTORIES` / `QSAN_GUI_DATA_DIRECTORIES` 後面，逐個
`if(EXISTS)` 檢查。

「應該有但沒有」不會悄悄溜走：資產清單會照樣將它列為 required，
`--asset-report` 傳回 exit 7，package smoke 紅燈。換句話說，缺漏是在一個
說得出是缺什麼的時刻報告，而不是在 install 中途拋出一句 CMake error。

`lang/` 的分類：缺翻譯**不會** crash（`sgs.GetFileNames()` 對一個不存在
的目錄傳回空 list，dedicated server 一直以來就是這樣運行），但一個顯示內部名
的 GUI 對玩家來說不算可用 —— 所以 GUI build 將它視為 required，其他情況 optional。

`DESTDIR` 有效（distro packaging 靠它），`--strip` 令 binary 由 ~420 MB 降到
~35 MB。Debug 與 Release 各自 install 到不同 prefix，不會互相污染。

### 私有 Qt 為何在 `lib/qsanguosha/qt/`

Qt 自己的 library、plugin、QML plugin 全部已經帶 `$ORIGIN`-relative RUNPATH，
而且假設 Qt 標準 prefix 版面：

```text
lib/libQt6Core.so.6                                  $ORIGIN
plugins/platforms/libqxcb.so                         $ORIGIN/../../lib
qml/QtQuick/libqtquick2plugin.so                     $ORIGIN/../../lib
qml/QtQuick/Controls/Basic/libqtquickcontrols2*.so   $ORIGIN/../../../../lib
```

只要私有 runtime 照抄這個相對版面，全部都自動找得到 —— **完全不需要
patchelf 改寫任何一個 Qt binary**。任何其他版面都要逐個 `.so` 改 RPATH。

我們自己的 binary 用：

```text
RUNPATH = $ORIGIN/../lib/qsanguosha/qt/lib
```

這個相對路徑由 CMake 用 `file(RELATIVE_PATH)` 由 `CMAKE_INSTALL_FULL_BINDIR`
與 `CMAKE_INSTALL_FULL_LIBDIR` 算出，所以整棵樹可以搬到任何地方。distro 式
安裝沒有這個目錄，loader 就直接使用系統 Qt —— 同一個 binary 兩邊都適用。

---

## 3. 執行期版面解析

M3 之前遊戲假設 `CWD == repository root`：engine 直接使用 `lua/config.lua`，
skin bank 直接使用 `image/...`。這個假設現在由**一個** resolver
（`src/core/runtime-paths.{h,cpp}`）取代，GUI 與 dedicated server 共用。

優先次序：

```text
1. --asset-root <path>          明確指定；不合格立即報錯，不會悄悄 fallback
2. QSAN_ASSET_ROOT              同上
3. <appDir>/../share/qsanguosha 安裝樹
4. <appDir>/share/qsanguosha    可攜／AppImage
5. 目前工作目錄                  開發樹（維持舊行為）
6. <appDir>/..                  由 build 輸出目錄找回 source tree
7. <appDir>                     平面部署目錄（exe 與 lua/ 同一層）
```

安裝／可攜版面**優先於** CWD：一個打包好的 binary 不應該因為使用者碰巧在
一個舊 source tree 裡開啟它，就去用那邊的資產。

一個目錄要有 `lua/config.lua` **與** `lua/sanguosha.lua` 才算數 —— engine
沒有這兩個檔就會在 constructor 裡 `exit(1)`，晚到的時候才發現已經太遲。

解析成功之後會 `QDir::setCurrent(assetRoot)` 一次。這是對現存大量相對路徑
call site 的過渡橋樑：目標由「使用者碰巧在哪裡開遊戲」變成「一個真正存在、
被驗證過的資產目錄」。**新 code 不應該再靠 CWD**，要用
`QSanRuntimePaths::assetPath()` / `userDataPath()` / `readablePath()`。

> 因為 CWD 會改，所以傳給遊戲的相對路徑（report、log、fixture）都會相對
> asset root。`tools/ci/*-smoke.sh` 已經一律轉為絕對路徑。

### 使用者資料

```text
設定        ~/.config/QSanguosha.org/QSanguosha.conf   （QSettings，一直如此）
replay      <userDataRoot>/record/
AI 學習資料  <userDataRoot>/lua/ai/data/AiData
自訂劇本     <userDataRoot>/etc/customScenes/
```

`userDataRoot` 規則：

* 打包的 root（CLI／env／安裝樹／可攜包）→ `~/.local/share/QSanguosha`。
  `/usr/share` 與 AppImage 的 squashfs 是唯讀，寫回去只會悄悄失敗。
* 開發樹（CWD／appDir）→ 就是 asset root 本身，**維持舊行為**，開發者慣用
  的 `record/` 不會突然搬走。Windows 部署目錄一樣走這條路，沒有 regression。
* `QSAN_USER_DATA_ROOT` 永遠蓋過以上兩者。

讀「使用者可以自訂、但亦有隨包附帶版本」的內容（自訂劇本）使用
`readablePath()`：user data 優先，找不到才回退到資產樹。

---

## 4. 資產策略

repository 與 clean CI 都沒有完整美術／音訊（見 AGENTS.md）。發佈出去的
**core runtime package 一樣沒有**：它帶齊規則、Lua、擴充、介面腳本，
但不帶幾 GB 立繪與語音。

`share/qsanguosha/assets-manifest.json`（schema 1）說清楚什麼是什麼：

```json
{
  "schema_version": 1,
  "game_version": "20251231",
  "asset_pack_version": "core-1",
  "required_paths": ["lua/config.lua", "lua/sanguosha.lua", "extensions", "..."],
  "optional_paths": ["image", "audio", "font", "hero-skin", "..."]
}
```

清單由 CMakeLists.txt 產生，所以它永遠描述**這個 build 實際裝了什麼**：
server-only 安裝不會聲稱自己有 GUI 資產。

* 缺 `required` → 該包已損壞，`--asset-report` 傳回 exit 7。
* 缺 `optional` → 正常情況。HomeScene 有 fallback，M1／M2B smoke 全部
  刻意在無素材環境下跑。**不可以 crash，packaging job 亦不可以因此失敗。**

診斷入口（愛好者報「開不了」時第一步就是跑這個）：

```bash
./QSanguosha --asset-report          # GUI，不需要 display
./qsanguosha-server --asset-report
```

用外部資產包：

```bash
./QSanguosha --asset-root /path/to/full/assets
QSAN_ASSET_ROOT=/path/to/full/assets ./QSanguosha
# 或者直接放入包內：
cp -r /path/to/{image,audio,font,hero-skin} share/qsanguosha/
```

Android `.qsanpack` 不在 M3 範圍；manifest 的 schema 刻意留有向那邊延伸的空間。

---

## 5. Qt deployment

Qt 6.11 沒有 generic-Linux 版的 `windeployqt`（`qt_generate_deploy_app_script`
不支援 desktop Linux，Qt release 亦沒有 `linuxdeployqt`），所以收集由
`tools/packaging/deploy-linux.py` 做。

規則：

* **只打包位於 Qt prefix 之中的 library。** 這條規則本身就令系統 glibc、
  libGL、libX11、libfreetype 留給主機 —— 打包了它們正是「在我機器上能跑、
  在你機器上開不了」的成因。Qt 自己 bundle 的 ICU 與 FFmpeg 在 prefix 之中，
  所以會一起帶走。留下的系統 library 會列在 `deploy-report.json`。
* **Plugin 要明確列出。** `ldd` 看不到 `dlopen`：platform（xcb／offscreen／
  minimal／wayland）、imageformats、iconengines、multimedia、TLS、
  networkinformation 全部是執行時才按名載入。
* **QML module 用 Qt 自己的 `qmlimportscanner`** 掃描 `qml/` 與 `ui-script/`，
  再加一張「執行時才決定」的清單（`QQuickStyle::setStyle("Basic")` 是一個
  runtime string，scanner 看不到）。用不到的 Controls style（Material、
  Imagine、Universal、FluentWinUI3）不帶，省下大約 10 MB。
* **不會複製**：header、`.a`、`.prl`、`.pc`、CMake 檔、debug info、Qt 工具。

`bin/qt.conf` 用相對 prefix 指向私有 runtime。沒有它，Qt 會去 build 當時寫死
的 Qt prefix 找 plugin —— 一個玩家機器上根本沒有的開發機路徑。

**不會** export `LD_LIBRARY_PATH`／`QT_PLUGIN_PATH`／`QML2_IMPORT_PATH`：
把一個私有 Qt 洩漏到環境會弄壞遊戲 launch 的任何其他程式。

### 稽核

`tools/packaging/audit-bundle.py` 在**成品**上面（不是 build tree）驗證三項：

1. 所有 RPATH／RUNPATH 都是 `$ORIGIN`-relative；
2. payload 裡沒有任何開發機路徑（build 目錄、Qt prefix、家目錄）；
3. 沒有 dev 檔案、沒有打包了主機的 glibc／libGL／libX11／libfreetype。

ELF 由 `tools/packaging/elfinfo.py` 自行解析（不靠 `readelf`／`ldd`），所以
稽核在哪部機器上跑都一樣，亦不會受主機 library 搜尋次序影響。

---

## 6. 可攜包

```bash
tar --zstd -xf QSanguosha-*.tar.zst
cd QSanguosha-*
./QSanguosha             # GUI
./qsanguosha-server      # dedicated server
```

**不需要**事先設定任何環境變數。頂層兩個 launcher 只做一件事：找回自己
所在目錄，然後 exec `bin/` 裡的真正 binary。

已驗證：在含空格的路徑、在 repository 以外、在任意 CWD 解壓都能運行。

---

## 7. AppImage

```bash
./QSanguosha-*.AppImage                       # 有 FUSE
./QSanguosha-*.AppImage --appimage-extract    # 沒有 FUSE（CI 走這條）
./squashfs-root/AppRun --ui-startup-smoke
```

AppImage 由 type2 runtime + squashfs 直接組裝，不經 `appimagetool` ——
appimagetool 自己也是一個 AppImage 而且要 FUSE，CI container 沒有。出來的
是一個正常 type2 AppImage，`--appimage-extract` 照用。

Payload 原封不動保留 install 版面在 `usr/` 下面，所以 resolver 由 `usr/bin`
就找到 `usr/share/qsanguosha`。`AppRun` 只是 exec `usr/bin/QSanguosha`。

type2 runtime 由 upstream 下載，sha256 在 workflow 中釘死。upstream 那個
rolling "continuous" release 重新 build 之後 job 會立即紅燈 —— 要人工驗證過
新 runtime 再更新，不可以悄悄接受一個未驗證過的二進位。

---

## 8. Desktop 整合

`packaging/linux/qsanguosha.desktop`，安裝到 `share/applications/`。
圖示安裝到 hicolor theme：8 個尺寸的 PNG（16–512）加一個 SVG。

Windows 的 `resource/icon/sgs.ico` 只有 32x32／16x16 8-bit，`sgs.icns` 裡
只有一張 JPEG 2000 —— 兩個都不可以做 Linux 圖示。Linux 一套由
`tools/packaging/make-linux-icons.py` 由同一份幾何定義同時輸出 SVG 與各尺寸
PNG（純標準庫，因為 build 機與 CI 都沒有 PIL／librsvg／ImageMagick）。

驗證：CI 跑真正的 `desktop-file-validate`；
`tools/packaging/validate-desktop-entry.py` 是本機用的同等檢查
（desktop-file-utils 不是每部開發機都有）。

---

## 9. Package smoke

**由成品測，不是由 build tree 測。** 這個分別就是 M3 存在的理由。

```bash
bash tools/ci/linux-package-smoke.sh <bundle-root> artifacts \
    --kind portable|appimage --platform xcb --no-xvfb
```

一次跑完：

```text
--asset-report（由 package 以外的目錄開啟）  版面／manifest／user data 分離
M1 startup                                  QApplication → HomeScene → 乾淨退出
M2B-A multimedia                            Qt audio backend、缺檔案降級
M2B-A video fallback                        影片缺失 → 靜態背景
M2B-B effects                               none / reduced / full 三個 profile
dedicated server                            --check-config --list-game-modes
TUI client                                  沒有 smoke 檢查 —— 未覆蓋
```

TUI 成品（`bin/qsanguosha_tui`）現在只有 build 驗證：`linux-package-ci.yml` 會
build `QSanguosha qsanguosha_server qsanguosha_tui` 三個 target，但
`linux-package-smoke.sh` 沒有任何 TUI 執行檢查。

M2 網絡對局**不在 CI 跑**（AGENTS.md「GUI runtime 不入 CI」：runner 沒有美術
資產，打完一局 client 與 server 會一起 SIGSEGV，與任何改動無關）。本機資產
齊全的環境下由 package 驗證：

```bash
PKG=/path/to/QSanguosha-<version>-linux-x86_64
cp -r /mnt/d/game/sgs/QSanguoshaFinal/{image,audio,font} "$PKG/share/qsanguosha/"
python3 tools/autotest/gui_network_smoke.py \
    --exe-root "$PKG" \
    --server-exe "$PKG/bin/qsanguosha_server" \
    --client-exe "$PKG/bin/QSanguosha" \
    --workdir "$PKG/share/qsanguosha" \
    --mode 02p --seed 20260828 --artifact-dir artifacts \
    --no-xvfb --platform xcb \
    --known-base-defect server-teardown-crash \
    --require-interactions choose_general
```

AppImage 可以用 `QSAN_ASSET_ROOT` 指向一個資產齊全的目錄跑同一條指令。

---

## 10. Packaging CI

`.github/workflows/linux-package-ci.yml`。整包 + 全套 smoke 要花上半小時，
所以**不是**每次改 source 都跑；觸發條件是 packaging 相關路徑、往
`debug`／`main` 的 PR、`push main`、`v*` tag，以及 `workflow_dispatch`。
`push debug` 不會在 PR gate 通過後再重複整包。

Action 全部釘 commit SHA，沿用 repository 現有政策。

---

## 11. `.deb`

```text
DEB STATUS: DEFERRED（M3.1）

理由:
  GUI 的 Qt baseline 是 6.11（CMakeLists.txt 的
  QSAN_QT_GUI_MINIMUM_VERSION 6.11 強制執行；GUI source 用到 Qt 6.11 的
  API）。沒有任何一個現行 Ubuntu series 的 archive 能提供 Qt >= 6.11，
  所以「.deb 依賴 distro Qt」（策略 A）今日根本做不到。

驗證證據（2026-08-28，Launchpad published sources，source_name=qt6-base）:
  noble    (24.04 LTS)  6.4.2+dfsg-21.1build5
  plucky   (25.04)      6.8.3+dfsg-0ubuntu2
  questing (25.10)      6.9.2+dfsg-1ubuntu1
  resolute (26.04 LTS)  6.10.2+dfsg-7
  本機 apt-cache policy qt6-base-dev → 6.10.2+dfsg-7（Ubuntu 26.04）
  對比 GUI 實際使用的 Qt：6.11.1

  也就是說 24.04（任務指定的目標）差 7 個 minor version，連最新 LTS 都仍然
  低於 baseline。一個聲稱支援 Ubuntu 24.04 但實際缺 Qt 6.11 的 deb，安裝
  完會直接開不了 —— 這種包比沒有 deb 更差，所以不會出。

  策略 B（deb 自己在 private lib directory bundle Qt runtime）技術上做得到：
  M3 已經建好那棵私有 Qt 樹（lib/qsanguosha/qt/，$ORIGIN RUNPATH，不需要
  patchelf），塞入一個 deb 只是多一層包裝。但這樣要在 deb 裡帶 ~124 MB
  Qt，違反 Debian policy 對 bundled library 的要求，永遠進不了官方 archive，
  而且功能上與已經交付的 portable bundle 完全重疊。要不要走這條路是產品決定，
  應該由 maintainer 決定，不應該在這個 PR 裡悄悄決定。

  策略 C（暫時只出 AppImage／portable）就是本 PR 實際採取的做法。

後續 M3.1:
  1. maintainer 選擇策略 B 或 C。
  2. 如果是 B：用同一棵 staging tree 出 deb（Depends 只寫系統 library，
     Qt 放在 /usr/lib/qsanguosha/qt/，binary RUNPATH 已經正確），加
     lintian + 一個 root-capable CI job 真正做 install → 執行 →
     purge 全循環才可以標 PASS。
  3. 如果是 C：在 README 講明 Linux 正式發佈就是 AppImage 與 portable。

這個決定不阻礙 portable bundle 與 AppImage，兩者在本 PR 已經完成並驗證。
```

---

## 12. Windows 不受影響

所有 install／RPATH／deployment 規則都在 `if(UNIX ...)` 裡。Windows 繼續
使用 `cmake/Deploy.cmake` 與 `windeployqt`，FMOD 部署、輸出目錄版面、
VS debugger working directory 全部沒有改動。

runtime resolver 在 Windows 的行為與以前一樣：`VS_DEBUGGER_WORKING_DIRECTORY`
是 source root，會由「目前工作目錄」這個候選命中；部署目錄（exe 與 lua/
同一層）由「application-dir」候選命中 —— 這個還順手修正了「由其他目錄開
exe 就找不到資產」的問題。
