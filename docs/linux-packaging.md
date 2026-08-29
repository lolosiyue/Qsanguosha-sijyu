# Linux packaging（Linux GUI M3）

呢份文件講點樣由一個 build tree 整出愛好者可以直接下載執行嘅 Linux 版本，
同埋點解每一步係咁做。目標好簡單：**唔需要開發工具、唔需要 source tree，
下載、解壓、雙擊就玩得到。**

前置里程碑：M0 compile/link、M1 real startup、M2 playable network flow、
M2B-A multimedia、M2B-B effects profiles，全部已經喺 `debug`。

---

## 1. 交付物

| 檔案 | 用途 |
| --- | --- |
| `QSanguosha-<version>-linux-x86_64.tar.zst` | 可攜包，解壓即用，含 GUI 同 dedicated server |
| `QSanguosha-<version>-x86_64.AppImage` | 單檔案 AppImage，含 desktop entry 同圖示 |
| `build-info.json` | 版本、git SHA、Qt 版本、compiler、build type、資產清單版本、時間戳 |
| `SHA256SUMS` | 上面每個檔案嘅 SHA-256 |
| `deploy-report.json` | 打包咗邊啲 Qt library／plugin／QML module，同埋邊啲留返畀系統 |
| `audit-*.json` | RPATH／開發機路徑／系統 library 稽核結果 |

一句指令做晒：

```bash
python3 tools/packaging/build-linux-packages.py \
    --build-dir build/package --source-dir . \
    --qt-prefix "$QT_ROOT_DIR" --qt-version 6.11.1 \
    --output-dir dist --forbid "$HOME"
```

兩個交付物用**同一份** staging tree 包出嚟。一個 bug 喺其中一邊出現，就一定
喺另一邊都出現 —— 冇第二份「包入面應該有乜」嘅定義。

---

## 2. 安裝樹版面

`cmake --install` 跟 GNUInstallDirs：

```text
<prefix>/bin/QSanguosha
<prefix>/bin/qsanguosha_server
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

三個 install component，令 GUI 同 server 可以各自出貨：

```text
qsan_data    lua / extensions / lang / etc —— 兩邊都要嘅規則資料
qsan_server  server binary、systemd unit、server 文件
qsan_gui     GUI binary、介面資產、desktop entry、圖示
```

`QSAN_BUILD_GUI=OFF` 嘅 server-only 安裝**唔會**帶任何 GUI 資產，亦唔會
link 任何 GUI Qt library；packaging CI 每次都驗呢兩點。

資料目錄係「有先裝」：`install(DIRECTORY)` 撞到一個唔存在嘅來源目錄會直接
fatal，而唔係每個 build context 都有齊嘢 —— Docker server image 嘅
`.dockerignore` 就特登剔走 `lang/`、`qss/`、`skins/`、`ui-script/`，
`extensions/` 亦係喺 build 入面 fetch 返嚟。所以呢啲目錄收埋喺
`QSAN_DATA_DIRECTORIES` / `QSAN_GUI_DATA_DIRECTORIES` 後面，逐個
`if(EXISTS)` 檢查。

「應該有但係冇」唔會靜靜咁溜走：資產清單會照樣列佢做 required，
`--asset-report` 回 exit 7，package smoke 紅燈。換句話講，缺嘢係喺一個
講得出係缺乜嘢嘅時刻報，而唔係喺 install 中途炸一句 CMake error。

`lang/` 嘅分類：缺翻譯**唔會** crash（`sgs.GetFileNames()` 對住一個唔存在
嘅目錄回空 list，dedicated server 一路以嚟就係咁行），但一個顯示內部名嘅
GUI 對玩家嚟講唔算能用 —— 所以 GUI build 當佢 required，其他情況 optional。

`DESTDIR` 有效（distro packaging 靠佢），`--strip` 令 binary 由 ~420 MB 跌到
~35 MB。Debug 同 Release 各自 install 去唔同 prefix，唔會互相污染。

### 私有 Qt 點解喺 `lib/qsanguosha/qt/`

Qt 自己嘅 library、plugin、QML plugin 全部已經帶 `$ORIGIN`-relative RUNPATH，
而且假設 Qt 標準 prefix 版面：

```text
lib/libQt6Core.so.6                                  $ORIGIN
plugins/platforms/libqxcb.so                         $ORIGIN/../../lib
qml/QtQuick/libqtquick2plugin.so                     $ORIGIN/../../lib
qml/QtQuick/Controls/Basic/libqtquickcontrols2*.so   $ORIGIN/../../../../lib
```

只要私有 runtime 照抄呢個相對版面，全部都自動搵得返 —— **完全唔需要
patchelf 改寫任何一個 Qt binary**。任何其他版面都要逐個 `.so` 改 RPATH。

我哋自己嘅 binary 用：

```text
RUNPATH = $ORIGIN/../lib/qsanguosha/qt/lib
```

呢個相對路徑由 CMake 用 `file(RELATIVE_PATH)` 由 `CMAKE_INSTALL_FULL_BINDIR`
同 `CMAKE_INSTALL_FULL_LIBDIR` 算出，所以整棵樹可以搬去任何地方。distro 式
安裝冇呢個目錄，loader 就直接落系統 Qt —— 同一個 binary 兩邊都啱。

---

## 3. 執行期版面解析

M3 之前遊戲假設 `CWD == repository root`：engine 直接 `lua/config.lua`，
skin bank 直接 `image/...`。呢個假設而家由**一個** resolver
（`src/core/runtime-paths.{h,cpp}`）取代，GUI 同 dedicated server 共用。

優先次序：

```text
1. --asset-root <path>          明確指定；唔合格即刻報錯，唔會靜靜 fallback
2. QSAN_ASSET_ROOT              同上
3. <appDir>/../share/qsanguosha 安裝樹
4. <appDir>/share/qsanguosha    可攜／AppImage
5. 目前工作目錄                  開發樹（維持舊行為）
6. <appDir>/..                  由 build 輸出目錄睇返 source tree
7. <appDir>                     平面部署目錄（exe 同 lua/ 同一層）
```

安裝／可攜版面**行先過** CWD：一個打包好嘅 binary 唔應該因為使用者碰巧喺
一個舊 source tree 入面開佢，就走去用嗰邊嘅資產。

一個目錄要有 `lua/config.lua` **同** `lua/sanguosha.lua` 先算數 —— engine
冇呢兩個檔就會喺 constructor 入面 `exit(1)`，遲到嗰陣先發現已經太夜。

解析成功之後會 `QDir::setCurrent(assetRoot)` 一次。呢個係對現存大量相對路徑
call site 嘅過渡橋樑：目標由「使用者碰巧喺邊度開遊戲」變成「一個真正存在、
被驗證過嘅資產目錄」。**新 code 唔應該再靠 CWD**，要用
`QSanRuntimePaths::assetPath()` / `userDataPath()` / `readablePath()`。

> 因為 CWD 會改，所以傳畀遊戲嘅相對路徑（report、log、fixture）都會相對
> asset root。`tools/ci/*-smoke.sh` 已經一律轉做絕對路徑。

### 使用者資料

```text
設定        ~/.config/QSanguosha.org/QSanguosha.conf   （QSettings，一直如此）
replay      <userDataRoot>/record/
AI 學習資料  <userDataRoot>/lua/ai/data/AiData
自訂劇本     <userDataRoot>/etc/customScenes/
```

`userDataRoot` 規則：

* 打包嘅 root（CLI／env／安裝樹／可攜包）→ `~/.local/share/QSanguosha`。
  `/usr/share` 同 AppImage 嘅 squashfs 係唯讀，寫返入去只會靜靜失敗。
* 開發樹（CWD／appDir）→ 就係 asset root 本身，**維持舊行為**，開發者慣用
  嘅 `record/` 唔會突然搬走。Windows 部署目錄一樣行呢條路，冇 regression。
* `QSAN_USER_DATA_ROOT` 永遠蓋過以上兩者。

讀「使用者可以自訂、但亦有隨包附帶版本」嘅內容（自訂劇本）行
`readablePath()`：user data 行先，搵唔到先返資產樹。

---

## 4. 資產策略

repository 同 clean CI 都冇完整美術／音訊（見 AGENTS.md）。發佈出去嘅
**core runtime package 一樣冇**：佢帶齊規則、Lua、擴充、介面腳本，
但唔帶幾 GB 立繪同語音。

`share/qsanguosha/assets-manifest.json`（schema 1）講清楚咩係咩：

```json
{
  "schema_version": 1,
  "game_version": "20251231",
  "asset_pack_version": "core-1",
  "required_paths": ["lua/config.lua", "lua/sanguosha.lua", "extensions", "..."],
  "optional_paths": ["image", "audio", "font", "hero-skin", "..."]
}
```

清單由 CMakeLists.txt 產生，所以佢永遠描述**呢個 build 實際裝咗乜**：
server-only 安裝唔會聲稱自己有 GUI 資產。

* 缺 `required` → 個包壞咗，`--asset-report` 回 exit 7。
* 缺 `optional` → 正常情況。HomeScene 有 fallback，M1／M2B smoke 全部
  刻意喺無素材環境下跑。**唔可以 crash，packaging job 亦唔可以因此失敗。**

診斷入口（愛好者報「開唔到」第一步就係跑呢個）：

```bash
./QSanguosha --asset-report          # GUI，唔需要 display
./qsanguosha-server --asset-report
```

用外部資產包：

```bash
./QSanguosha --asset-root /path/to/full/assets
QSAN_ASSET_ROOT=/path/to/full/assets ./QSanguosha
# 或者直接放入包入面：
cp -r /path/to/{image,audio,font,hero-skin} share/qsanguosha/
```

Android `.qsanpack` 唔喺 M3 範圍；manifest 嘅 schema 刻意留得可以向嗰邊延伸。

---

## 5. Qt deployment

Qt 6.11 冇 generic-Linux 版嘅 `windeployqt`（`qt_generate_deploy_app_script`
唔支援 desktop Linux，Qt release 亦冇 `linuxdeployqt`），所以收集由
`tools/packaging/deploy-linux.py` 做。

規則：

* **只打包住喺 Qt prefix 入面嘅 library。** 呢條規則本身就令系統 glibc、
  libGL、libX11、libfreetype 留返畀主機 —— 打包咗佢哋正正就係「喺我部機行到、
  喺你部機開唔到」嘅成因。Qt 自己 bundle 嘅 ICU 同 FFmpeg 喺 prefix 入面，
  所以會一齊帶走。留低嘅系統 library 會列喺 `deploy-report.json`。
* **Plugin 要明確列出。** `ldd` 睇唔到 `dlopen`：platform（xcb／offscreen／
  minimal／wayland）、imageformats、iconengines、multimedia、TLS、
  networkinformation 全部係執行時先按名載入。
* **QML module 用 Qt 自己嘅 `qmlimportscanner`** 掃 `qml/` 同 `ui-script/`，
  再加一張「執行時先決定」嘅清單（`QQuickStyle::setStyle("Basic")` 係一個
  runtime string，scanner 睇唔到）。用唔到嘅 Controls style（Material、
  Imagine、Universal、FluentWinUI3）唔帶，慳大約 10 MB。
* **唔會抄**：header、`.a`、`.prl`、`.pc`、CMake 檔、debug info、Qt 工具。

`bin/qt.conf` 用相對 prefix 指去私有 runtime。冇佢，Qt 會去 build 嗰陣燒死
嘅 Qt prefix 搵 plugin —— 一個玩家部機根本冇嘅開發機路徑。

**唔會** export `LD_LIBRARY_PATH`／`QT_PLUGIN_PATH`／`QML2_IMPORT_PATH`：
洩一個私有 Qt 出環境會整死遊戲 launch 嘅任何其他程式。

### 稽核

`tools/packaging/audit-bundle.py` 喺**成品**上面（唔係 build tree）驗三樣嘢：

1. 所有 RPATH／RUNPATH 都係 `$ORIGIN`-relative；
2. payload 入面冇任何開發機路徑（build 目錄、Qt prefix、家目錄）；
3. 冇 dev 檔案、冇打包咗主機嘅 glibc／libGL／libX11／libfreetype。

ELF 由 `tools/packaging/elfinfo.py` 自己解（唔靠 `readelf`／`ldd`），所以
稽核喺邊部機行都一樣，亦唔會受主機 library 搜尋次序影響。

---

## 6. 可攜包

```bash
tar --zstd -xf QSanguosha-*.tar.zst
cd QSanguosha-*
./QSanguosha             # GUI
./qsanguosha-server      # dedicated server
```

**唔需要**事先設定任何環境變數。頂層兩個 launcher 只做一件事：搵返自己
所在目錄，然後 exec `bin/` 入面嘅真正 binary。

已驗：喺含空格嘅路徑、喺 repository 以外、喺任意 CWD 解壓都行得到。

---

## 7. AppImage

```bash
./QSanguosha-*.AppImage                       # 有 FUSE
./QSanguosha-*.AppImage --appimage-extract    # 冇 FUSE（CI 行呢條）
./squashfs-root/AppRun --ui-startup-smoke
```

AppImage 由 type2 runtime + squashfs 直接砌，唔經 `appimagetool` ——
appimagetool 自己都係一個 AppImage 而且要 FUSE，CI container 冇。出嚟嘅
係一個正常 type2 AppImage，`--appimage-extract` 照用。

Payload 原封不動保留 install 版面喺 `usr/` 下面，所以 resolver 由 `usr/bin`
就搵到 `usr/share/qsanguosha`。`AppRun` 只係 exec `usr/bin/QSanguosha`。

type2 runtime 由 upstream 下載，sha256 喺 workflow 入面釘死。upstream 嗰個
rolling "continuous" release 重新 build 之後 job 會即刻紅燈 —— 要人手驗過
新 runtime 再更新，唔可以靜靜食一個未驗過嘅二進位。

---

## 8. Desktop 整合

`packaging/linux/qsanguosha.desktop`，裝去 `share/applications/`。
圖示裝去 hicolor theme：8 個尺寸嘅 PNG（16–512）加一個 SVG。

Windows 嘅 `resource/icon/sgs.ico` 得 32x32／16x16 8-bit，`sgs.icns` 入面
淨係一張 JPEG 2000 —— 兩個都唔可以做 Linux 圖示。Linux 一套由
`tools/packaging/make-linux-icons.py` 由同一份幾何定義同時出 SVG 同各尺寸
PNG（純標準庫，因為 build 機同 CI 都冇 PIL／librsvg／ImageMagick）。

驗證：CI 行真正嘅 `desktop-file-validate`；
`tools/packaging/validate-desktop-entry.py` 係本機用嘅同等檢查
（desktop-file-utils 唔係每部開發機都有）。

---

## 9. Package smoke

**由成品測，唔係由 build tree 測。** 呢個分別就係 M3 存在嘅理由。

```bash
bash tools/ci/linux-package-smoke.sh <bundle-root> artifacts \
    --kind portable|appimage --platform xcb --no-xvfb
```

一次過跑：

```text
--asset-report（由 package 以外嘅目錄開）  版面／manifest／user data 分離
M1 startup                                  QApplication → HomeScene → 乾淨退出
M2B-A multimedia                            Qt audio backend、缺檔案降級
M2B-A video fallback                        影片缺失 → 靜態背景
M2B-B effects                               none / reduced / full 三個 profile
dedicated server                            --check-config --list-game-modes
```

M2 網絡對局**唔喺 CI 跑**（AGENTS.md「GUI runtime 唔入 CI」：runner 冇美術
資產，打完一局 client 同 server 會一齊 SIGSEGV，同任何改動無關）。本機齊
資產環境下由 package 驗：

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

AppImage 可以用 `QSAN_ASSET_ROOT` 指去一個齊料嘅目錄跑同一條指令。

---

## 10. Packaging CI

`.github/workflows/linux-package-ci.yml`。整包 + 全套 smoke 要成半個鐘，
所以**唔係**每次改 source 都跑；觸發條件係 packaging 相關路徑、去
`debug`／`main` 嘅 PR、`push main`、`v*` tag，同 `workflow_dispatch`。
`push debug` 唔會喺 PR gate 通過後再重複整包。

Action 全部釘 commit SHA，沿用 repository 現有政策。

---

## 11. `.deb`

```text
DEB STATUS: DEFERRED（M3.1）

理由:
  GUI 嘅 Qt baseline 係 6.11（CMakeLists.txt 嘅
  QSAN_QT_GUI_MINIMUM_VERSION 6.11 強制執行；GUI source 用到 Qt 6.11 嘅
  API）。冇任何一個現行 Ubuntu series 嘅 archive 提供到 Qt >= 6.11，
  所以「.deb 依賴 distro Qt」（策略 A）今日根本做唔到。

驗證證據（2026-08-28，Launchpad published sources，source_name=qt6-base）:
  noble    (24.04 LTS)  6.4.2+dfsg-21.1build5
  plucky   (25.04)      6.8.3+dfsg-0ubuntu2
  questing (25.10)      6.9.2+dfsg-1ubuntu1
  resolute (26.04 LTS)  6.10.2+dfsg-7
  本機 apt-cache policy qt6-base-dev → 6.10.2+dfsg-7（Ubuntu 26.04）
  對比 GUI 實際使用嘅 Qt：6.11.1

  即係話 24.04（任務指定嘅目標）差 7 個 minor version，連最新 LTS 都仍然
  低過 baseline。一個聲稱支援 Ubuntu 24.04 但實際缺 Qt 6.11 嘅 deb，安裝
  完會直接開唔到 —— 呢種包比冇 deb 更差，所以唔會出。

  策略 B（deb 自己喺 private lib directory bundle Qt runtime）技術上做得到：
  M3 已經整好嗰棵私有 Qt 樹（lib/qsanguosha/qt/，$ORIGIN RUNPATH，唔需要
  patchelf），塞入一個 deb 只係多一層包裝。但係咁樣要喺 deb 入面帶 ~124 MB
  Qt，違反 Debian policy 對 bundled library 嘅要求，永遠入唔到官方 archive，
  而且功能上同已經交付嘅 portable bundle 完全重疊。要唔要行呢條路係產品決定，
  應該由 maintainer 揀，唔應該喺呢個 PR 靜靜咁決定咗。

  策略 C（暫時淨係出 AppImage／portable）就係本 PR 實際採取嘅做法。

後續 M3.1:
  1. maintainer 揀策略 B 定 C。
  2. 如果係 B：用同一棵 staging tree 出 deb（Depends 只寫系統 library，
     Qt 走 /usr/lib/qsanguosha/qt/，binary RUNPATH 已經啱），加
     lintian + 一個 root-capable CI job 真正做 install → 執行 →
     purge 全循環先可以標 PASS。
  3. 如果係 C：喺 README 講明 Linux 正式發佈就係 AppImage 同 portable。

呢個決定唔阻礙 portable bundle 同 AppImage，兩者喺本 PR 已經完成同驗證。
```

---

## 12. Windows 唔受影響

所有 install／RPATH／deployment 規則都喺 `if(UNIX ...)` 入面。Windows 繼續
行 `cmake/Deploy.cmake` 同 `windeployqt`，FMOD 部署、輸出目錄版面、
VS debugger working directory 全部冇改。

runtime resolver 喺 Windows 嘅行為同以前一樣：`VS_DEBUGGER_WORKING_DIRECTORY`
係 source root，會由「目前工作目錄」呢個候選命中；部署目錄（exe 同 lua/
同一層）由「application-dir」候選命中 —— 呢個仲順手修正咗「由其他目錄開
exe 就搵唔到資產」。
