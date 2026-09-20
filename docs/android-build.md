# Android APK 建置、更新與驗收

本機日常使用原生 `x86_64` APK 與既有 API 33 模擬器；`arm64-v8a` 留作實機／發行建置參考。
日常驗收入口見 [Android 簡化驗收](android-acceptance.md)：覆蓋安裝、日誌／截圖收集、完整局與正常退出分開判定。
首次外部聲畫 ZIP／Storage Access Framework（SAF）匯入流程保留於本文後半，日常更新不需重做。

**版本固定規則：Android 不再雜湊資源、不逐檔掃描聲畫，也不因圖片缺檔擋住開局。**
已安裝媒體在 APK 更新後繼續沿用；新增圖片不觸發全包重匯。
詳細契約見 [Android 版本固定規則](android-first-release.md#android-版本固定規則2026-09-16)。

## 本機唯一日常環境（2026-09-20 核對）

**使用者要求：只用下面這一套，保留 App 資料、媒體與建置快取。**
本頁是日常 Android 操作的唯一入口；後文的首次安裝教學不代表每次建置都要重做。

| 用途 | 固定位置／值 |
|---|---|
| Android 建置來源 | `L:\finaldebug\QSanguosha-v2`；既有 cache 的 CMAKE_HOME_DIRECTORY 指向此處 |
| Debug 建置目錄 | `L:\finaldebug\QSanguosha-v2\builds\android-x86_64-debug`，junction 指向 `H:\qsan-android-x86_64\build-debug` |
| 共用工具鏈 | `H:\qsan-android-x86_64`；Qt target=`qt/6.11.1/android_x86_64`，Gradle cache=`gradle` |
| AVD home | `H:\qsan-validation\room-responsive-20260916\avd` |
| 唯一 AVD／序號 | `Responsive_API_33`／`emulator-5586`，沿用既有 12 GiB userdata |
| Emulator | `C:\Users\a3160\AppData\Local\Android\Sdk\emulator\emulator.exe` |
| SDK／ADB | `%LOCALAPPDATA%\Android\Sdk`／其 `platform-tools\adb.exe` |
| 媒體原包 | `H:\qsan-validation\room-responsive-20260916\qsan-media.zip` |
| App | `org.qsanguosha.game`，保持相同簽章，以 `install -r` 更新 |

路徑中的日期是既有名稱，**不得換成當天日期再建一份**。每次任務只另建小型日誌目錄，
不另建 AVD、Android source worktree、SDK、Gradle cache、APK build tree 或完整媒體副本。
其他歷史 Android 目錄不是備用日常入口，也不要因本規則自動刪除它們。

### 每次修改的固定順序

1. 在 L 工作區核對本次來源與 dirty state；不再複製到舊 Android 工作樹。
2. 完成授權檢查點後，在同一 cache 增量建置一次；不用 `--fresh`、`--clean-first`。
3. 重用 `emulator-5586`；未啟動只啟動 `Responsive_API_33`，不用 `-wipe-data`、新 AVD 或新媒體副本。
4. 正常關閉 App，再以驗收助手 `run --install` 執行 `install --no-streaming -r` 與 `sync`；簽章不符就停止，不能卸載或清除資料。
5. 選擇 [首頁短驗收／05p 完整局](android-acceptance.md)，各自收集證據。助手不建置、不修改模式設定、不自動判定 GAME_OVER。

固定建置命令（已有建置授權及完成檢查點時）：

```powershell
Set-Location L:\finaldebug\QSanguosha-v2
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-android.ps1 `
  -Configuration Debug -Abi x86_64 `
  -ToolchainRoot H:\qsan-android-x86_64 `
  -SdkRoot "$env:LOCALAPPDATA\Android\Sdk"
if ($LASTEXITCODE -ne 0) { throw 'Build failed; do not install an older APK.' }
```

此通用腳本會 reconfigure 既有 preset，不另建 cache；Qt／NDK／FreeType 路徑沿用固定工具鏈。
若 cache 設定未變、只需最快增量建置，可在 Android 環境已設定的 shell 執行
`cmake --build builds/android-x86_64-debug --target apk --parallel 8`。
不要拿桌面 Qt 或未設定 JAVA_HOME／GRADLE_USER_HOME 的 shell 直接套用。
若 PowerShell 把原生命令的 stderr warning 升格為 `NativeCommandError`，保存紀錄並核對實際退出碼，
不要清除建置樹；互動式使用上述腳本，避免另以 `$ErrorActionPreference='Stop'` 包住 `2>&1` 的外層管線。

既有 AVD 未啟動時，人工驗收可開啟可見視窗；已有序號就沿用，不另開實例：

```powershell
$adb = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
& $adb devices -l
# 確認沒有 emulator-5586 才執行以下區塊；offline 先診斷，不能開第二份。
$savedAvdHome = $env:ANDROID_AVD_HOME
try {
    $env:ANDROID_AVD_HOME = 'H:\qsan-validation\room-responsive-20260916\avd'
    if (!(Test-Path "$env:ANDROID_AVD_HOME\Responsive_API_33.ini")) {
        throw 'Existing AVD missing; do not create a replacement.'
    }
    if ((& $adb devices) -match '^emulator-5586\s') {
        throw 'Reuse the existing emulator or inspect its offline state.'
    }
    Start-Process "$env:LOCALAPPDATA\Android\Sdk\emulator\emulator.exe" `
      -ArgumentList '-avd Responsive_API_33 -port 5586 -no-snapshot -no-boot-anim -gpu swiftshader_indirect' `
      -WindowStyle Normal
} finally { $env:ANDROID_AVD_HOME = $savedAvdHome }
```

無畫面代理驗收才加 `-no-window` 並使用 `-WindowStyle Hidden`。本例不等待開機完成；
`python tools/android/acceptance.py check` 可確認序號在線。新助手不會自動啟動或關閉模擬器。

### 何時才需要媒體操作

| 變更 | 日常處理 |
|---|---|
| 只有 C++／UI，APK 資源名稱與內容不變 | 增量建置、`install -r`；沿用媒體與啟動快取 |
| Lua／翻譯／APK 內建資源變動 | 先對齊來源再建置；保留已匯入媒體，只處理必要的規則／UI 版本切換，不掃描或雜湊聲畫 |
| 使用者確實要更新媒體 | 更新媒體；APK 新增圖片不自動觸發重匯，也不因圖片缺檔擋局 |
| 資料被清除、媒體損壞、升級衝突 | 保存錯誤並判斷原因；不自動清除資料或重匯整包 |

**已觀察到的耗時原因**：2026-09-16 首次 ZIP 匯入 34,511 個檔案，約 17 分鐘；
100% 只表示解壓進度，後面仍會建立可用版本。此 AVD 的 SELinux 拒絕硬連結
（hard link），程式退回逐檔實體複製。新增 APK 基線資源也觸發了第二輪版本複製；
這不是重新選 ZIP 匯入，但同樣會耗時、占用數 GB；該次啟動內容準備實測 879,382 ms。
這些是舊 APK 數據。[新資源流程的來源修正](android-extension-runtime.md#2026-09-16-匯入更新效能修正來源檢查點)
已移除平方次數 ZIP 比對、可 seek 來源的 spool／重複雜湊讀取及媒體版本複製。
新 APK 已實測建立 3 個媒體目錄引用：快照 28,261 ms，只複製約 33 MB 規則／介面。
該次舊檢查在 60 秒停止。後續移除資源雜湊與聲畫掃描的 APK 已實測內容準備
6,924 ms，缺 8 張圖片未擋住連線；對局仍因 AudioTrack 崩潰而未完成。
首次完整匯入新耗時未重測，不能用啟動秒數代替。

首次匯入期間 Download ZIP、私有 spool、解壓 blob 與 runtime 版本可能同時存在，
12 GiB 分割區曾接近滿載。日常不要重複保留傳輸副本；匯入完成後清理本輪傳輸檔，
保留 H 碟原包與 App 私有資料。不要手動刪除 content store 的 baseline／blobs／versions。

此 AVD 是 API 33 x86_64／4 KiB pages；2026-09-20 日常 APK 已改用原生 x86_64。
本文 2026-09-16 的 arm64 APK／ARM translation 紀錄屬歷史證據，不能混作目前 ABI；兩者均不等同實機或折疊機驗收。

## 固定工具鏈與目錄

### 共用遊戲呈現入口（2026-09-16，尚未裝置驗收）

對局右上角「More actions」選單提供「遊戲狀態」與「遊戲操作面板」。兩者沿用
桌面的 `GameViewState`／`GameActionModel` 與 RoomScene 草稿，不另外實作 QML
回覆或規則。文字快照主動更新、可複製；操作面板使用標準 Widgets、整頁捲動及
至少 48 logical-pixel 觸控高度。關閉面板不取消請求，背景及同步未完成時停用操作。

支援與限制見 [共用呈現契約](client-core-interaction-model.md#other-client-adapters)。
此批 Android build、裝置觸控／外接鍵盤、TalkBack 均 **NOT RUN**；桌面測試結果
不能代替 Android gate。

### 建置預設值

以下是腳本與 CMake preset 的預設值。改用其他位置時，必須明確傳入參數或設定環境變數，勿混用不同 Qt／NDK 版本。

| 工具 | 版本 | 預設位置 |
|---|---|---|
| Qt Android target | 6.11.1、`android_arm64_v8a` | `builds/android-toolchain/qt/6.11.1/android_arm64_v8a` |
| Qt host tools | 6.11.1、MSVC | `H:\Qt6111\6.11.1\msvc2022_64` |
| Android NDK | 27.2.12479018 | `%LOCALAPPDATA%\Android\Sdk\ndk\27.2.12479018` |
| Android SDK | API 36（compile/target）、API 28（min） | `builds/android-toolchain\sdk` |
| SDK Build Tools | 36.0.0 | `<SdkRoot>\build-tools\36.0.0` |
| JDK | 21 | `builds/android-toolchain\jdk\<唯一含 bin\javac.exe 的目錄>` |
| Ninja | Qt 隨附 | `H:\Qt6111\Tools\Ninja\ninja.exe` |
| CMake | VS 2026 bundled CMake 4.2+ | `vswhere` 自動尋找，或 `-CMakeExe` |
| FreeType | 2.14.3 Android static | source=`builds/android-toolchain\src\freetype-2.14.3`；prefix=`builds/android-toolchain\freetype-arm64` |

首次準備 Qt target（已有相同版本 host tools）可使用：

```powershell
python -m aqt install-qt all_os android 6.11.1 android_arm64_v8a `
  -O builds/android-toolchain/qt `
  -m qtmultimedia qtwebsockets qtshadertools qt5compat
```

NDK、SDK API 36、Build Tools 36.0.0 與 JDK 21 必須先安裝到表內位置。`build-android.ps1` 會檢查 `javac`、`android.jar`、`zipalign`、Qt/NDK toolchain 及 FreeType source。

將 Android command-line tools 解壓成 `builds/android-toolchain/sdk/cmdline-tools/latest/bin/sdkmanager.bat`，JDK 21 解壓到上表 `jdk/` 下後，可用以下命令安裝固定 SDK／NDK（首次授權條款依 sdkmanager 提示處理）：

```powershell
$env:JAVA_HOME = (Resolve-Path 'builds/android-toolchain/jdk/jdk-21.0.12.1+1').Path
$sdkManager = (Resolve-Path 'builds/android-toolchain/sdk/cmdline-tools/latest/bin/sdkmanager.bat').Path
$sdkRoot = (Resolve-Path 'builds/android-toolchain/sdk').Path
& $sdkManager "--sdk_root=$sdkRoot" 'platform-tools' 'platforms;android-36' 'build-tools;36.0.0'
& $sdkManager "--sdk_root=$env:LOCALAPPDATA/Android/Sdk" 'ndk;27.2.12479018'
```

FreeType 原始碼需解壓為 `builds/android-toolchain/src/freetype-2.14.3/CMakeLists.txt`。本輪使用官方 `freetype-2.14.3.tar.xz`，SHA-256 為 `36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f`；腳本不下載依賴，也不改動 `include/`／`lib/`。

## 建置 APK

以下為通用建置參考；本機日常只執行上方「本機唯一日常環境」的固定命令。
腳本會暫時設定 Android、Java、Qt 及獨立 Gradle cache，完成後還原 PowerShell 環境；亦會先建立 FreeType Android static dependency。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-android.ps1 -Configuration Debug
```

覆寫工具鏈時使用 `-QtRoot`、`-QtHostPath`、`-SdkRoot`、`-NdkRoot`、`-JavaRoot`、`-CMakeExe`、`-NinjaExe`；例如：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-android.ps1 `
  -Configuration Debug `
  -QtRoot 'L:\finaldebug\QSanguosha-v2\builds\android-toolchain\qt\6.11.1\android_arm64_v8a' `
  -QtHostPath 'H:\Qt6111\6.11.1\msvc2022_64' `
  -SdkRoot 'L:\finaldebug\QSanguosha-v2\builds\android-toolchain\sdk' `
  -NdkRoot "$env:LOCALAPPDATA\Android\Sdk\ndk\27.2.12479018" `
  -JavaRoot 'L:\finaldebug\QSanguosha-v2\builds\android-toolchain\jdk\jdk-21.0.12.1+1' `
  -NinjaExe 'H:\Qt6111\Tools\Ninja\ninja.exe'
```

等價 preset 流程需先設定 `QSAN_ANDROID_QT_ROOT`、`QSAN_QT_HOST_PATH`、`QSAN_ANDROID_FREETYPE_ROOT`、`ANDROID_SDK_ROOT`、`ANDROID_NDK_ROOT`、`JAVA_HOME`，並把 Ninja 放在 PATH：

```powershell
$env:QSAN_ANDROID_QT_ROOT = 'L:\finaldebug\QSanguosha-v2\builds\android-toolchain\qt\6.11.1\android_arm64_v8a'
$env:QSAN_QT_HOST_PATH = 'H:\Qt6111\6.11.1\msvc2022_64'
$env:QSAN_ANDROID_FREETYPE_ROOT = 'L:\finaldebug\QSanguosha-v2\builds\android-toolchain\freetype-arm64'
$env:ANDROID_SDK_ROOT = 'L:\finaldebug\QSanguosha-v2\builds\android-toolchain\sdk'
$env:ANDROID_NDK_ROOT = "$env:LOCALAPPDATA\Android\Sdk\ndk\27.2.12479018"
$env:JAVA_HOME = 'L:\finaldebug\QSanguosha-v2\builds\android-toolchain\jdk\jdk-21.0.12.1+1'
$env:PATH = "$env:JAVA_HOME\bin;H:\Qt6111\Tools\Ninja;$env:PATH"
cmake --preset android-arm64-debug
cmake --build --preset android-arm64-debug-apk --parallel 8
```

Release 使用 `-Configuration Release` 或 `android-arm64-release`／`android-arm64-release-apk`。`android-arm64-<config>` 只建 native target；`-apk` 才執行 Qt deployment/Gradle `apk` target。Debug APK 固定輸出：

```text
builds/android-arm64-debug/cmake/android-app/android-build/build/outputs/apk/debug/android-build-debug.apk
```

Release 目錄是 `builds/android-arm64-release/cmake/android-app/android-build/build/outputs/apk/release/`；檔名以該次 Gradle 輸出為準。Debug 使用 debug keystore，適合開發安裝。Release 沒有倉庫內的發布 keystore、密碼或 signing configuration；Release 編譯成功不等於正式簽章或可上架，金鑰須由發布環境注入且不可入庫。

### 固定開發簽名與覆蓋更新

Debug APK 不再使用 Gradle 自動產生的金鑰。`resource/android/build.gradle` 固定讀取
`%USERPROFILE%/.qsanguosha/android-signing.properties`，亦可用
`QSAN_ANDROID_SIGNING_PROPERTIES` 指定其他本機設定檔。此規則涵蓋直接 CMake 建置與
`tools/build-android.ps1`，不受 `ANDROID_USER_HOME`、ABI 或 build directory 改變影響。

設定檔包含 `storeFile`（使用絕對路徑及 `/`）、`keyAlias`、`certificateSha256`；
現有開發金鑰沿用 Android debug 密碼，其他金鑰可於本機設定檔提供 `storePassword`／`keyPassword`。
`verifyPersistentDebugSigning` 在 Debug 簽名驗證前核對憑證 SHA-256；設定、金鑰缺失或指紋不符即失敗，
不得刪除設定、另產生金鑰或解除安裝來規避。

目前本機持久金鑰為 `%USERPROFILE%/.qsanguosha/signing/android-debug.keystore`，
備份於 `H:/qsan-signing/qsanguosha/`，均在倉庫與建置目錄外；金鑰與本機設定不可提交。
目前已安裝開發版憑證 SHA-256：
`d21d970234d27fb6e0325ff21785bca759ea5c085654f37fedea5ba3f98bd3a6`。
換機時搬移同一份金鑰與設定並修正 `storeFile`，不可重新生成。

後續保持套件名稱 `org.qsanguosha.game`、相同簽名與不倒退的版本，以
`adb -s emulator-5586 install -r <新 APK 路徑>` 更新，保留 App 資料。
遇到 `INSTALL_FAILED_UPDATE_INCOMPATIBLE` 應核對新舊憑證；不要自動解除安裝。
這是目前開發安裝的簽名延續，不是商店 Release 發布金鑰配置。

### Runtime descriptor 與內容身份

Android APK 的 `runtime-content-base.json` 必須由 CMake 產生的 filtered `bundled-lua.txt` 送入 `tools/android/create-runtime-descriptor.py`。descriptor 使用 `schema_version: 2`／`profile: declared-v2`，以 `lua/config.lua` 的 `extension_names` 順序為來源；它會把 APK 實際包含的 `lua/chat_config.lua` 與 `lua/lib/sqlite3.lua` 宣告為支援 libraries，並把 AI 路徑留在 server-owned policy 外。不要手動補 descriptor、修改 `config.lua`，或關閉 rules-content identity gate。

產生 descriptor 時，若 `extensions/` 含未被 `config.lua` 宣告的腳本，或宣告的 required file 不在 filtered APK input，工具必須失敗；這能避免 `ServerHello` 以 `rules_content_unsupported` 拒絕同版內容。修改 descriptor 生成邏輯後，可執行五個快速回歸案例（不啟動 Lua、server 或 CTest）：

```powershell
python tools/android/test-runtime-descriptor.py
```

本輪結果：5 tests passed。這只證明宣告覆蓋、缺檔拒絕、未宣告 extension 拒絕、路徑穿越拒絕及重複套用穩定；APK 建置、ServerHello 實際連線與完整對局仍須另行驗收。

既有安裝遵守 missing-only，原有同名包宣告不會被新版 APK 靜默覆寫。因此舊測試版的無效宣告不能只靠 `install -r` 修好；需透過整包管理匯入有效宣告，或在隔離測試副本使用明確 `--asset-root`。後者只算診斷部署，不能當作正常升級驗收。

## 首次部署或媒體確實變更時：產生外部聲畫 ZIP

APK 只附基本字型、UI WAV 與必要 runtime 資源；完整媒體用實際封裝器產生：

```powershell
python tools/android/create-media-package.py . builds/android-release/qsan-media.zip
```

工具只收集 `image/`、`audio/`、`font/`，排除 symlink、`.git`、暫存/備份/執行期目錄與備份副檔名，使用 ZIP64/`ZIP_STORED`，並寫入 `qsan-media.json`（逐檔 size/SHA-256）。來源在封裝途中變更會失敗。只掃描而不寫 ZIP：

```powershell
python tools/android/create-media-package.py . builds/android-release/qsan-media.zip --inspect
python tools/android/create-media-package.py . builds/android-release/qsan-media.zip --manifest-only
```

## 首次部署或媒體確實變更時：SAF 匯入

```powershell
& $adb devices -l
$serial = 'emulator-5586' # 本機固定 AVD；安裝路徑使用上方固定工作流的 $apk。
& $adb -s $serial install -r $apk
& $adb -s $serial shell am start -W -n org.qsanguosha.game/org.qtproject.qt.android.bindings.QtActivity
```

首次安裝媒體走 App 內的 SAF：在資源管理按「匯入聲畫」、選取 `qsan-media.zip`、等待解壓與暫存完成，再重新啟動 App 使 active version 生效。不做資源雜湊檢查；可 seek 的來源直接讀取，其他來源使用私有 staging spool。`adb push` 到外部路徑不算完成匯入。取消、切背景或空間不足時保留上一 active 版本；日常更新不重匯。

```powershell
adb -s $serial logcat -v threadtime | Tee-Object builds/android-emulator-logcat.txt
```

## APK 靜態稽核

以下只讀取既有產物：

```powershell
$apk = (Resolve-Path 'builds/android-arm64-debug/cmake/android-app/android-build/build/outputs/apk/debug/android-build-debug.apk').Path
$bt = (Resolve-Path 'builds/android-toolchain/sdk/build-tools/36.0.0').Path
Get-FileHash $apk -Algorithm SHA256
& "$bt\apksigner.bat" verify --verbose --print-certs $apk
& "$bt\zipalign.exe" -c -P 16 -v 4 $apk
& "$bt\aapt2.exe" dump badging $apk
tar -tf $apk | Select-String '(^|/)lib/|assets/|AndroidManifest.xml'
$readelf = Join-Path $env:LOCALAPPDATA 'Android\Sdk\ndk\27.2.12479018\toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-readelf.exe'
& $readelf -h -lW '<解壓後的 lib/arm64-v8a/某個.so>'
```

`apksigner` 證明簽章存在，`zipalign -P 16` 檢查 ZIP 對齊，`aapt2` 檢查 package/SDK metadata；這些不能證明可玩、音效輸出或 16 KB 裝置執行。

逐一檢查 APK 內每個 `.so` 的 ELF Machine 為 AArch64，所有 `LOAD` segment 的 Align 至少為 `0x4000`；只檢查主程式或 ELF header 不足以證明所有依賴符合 16 KB。`apksigner` 需先將上面的 JDK 21 設為該程序的 `JAVA_HOME`。

## Android 暫時靜音（2026-09-16）

Android Debug／Release preset、CMake Android 預設與 `tools/build-android.ps1`
均選擇既有 `NULL` 音訊後端，暫停音效、武將語音及 BGM。這是使用者同意的暫時
繞過方案；原有 Qt6 Multimedia 路徑會觸發 AAudio callback 崩潰，尚未證明已修復。
Qt Multimedia 仍供其他介面／影片功能使用。Windows 與 Linux 的音訊預設不變。

不需 FMOD SDK、不更換第三方庫、不刪除已匯入媒體。日常建置沿用上方固定工作樹、
建置快取及 AVD。若另行授權調查有聲版本，才用 `-AudioBackend QT`（或 CMake
`-DQSAN_AUDIO_BACKEND=QT`）重新啟用原音訊路徑。

驗收須區分「靜音 APK 建置／啟動／前後景成功」與「音訊缺陷修復」；前者不代表後者，
也不代表完整對局通過。

同日的 Android 啟動修復檢查點改用 Qt Quick `software` 後端及既有 raster 牌桌
viewport，避免模擬器上已觀察到的 OpenGL 破圖與前後景 EGL context 失效。
選擇在第一個 Quick window 建立前完成；Windows／Linux 保持原有 OpenGL 路徑。
這是相容性繞過，GPU shader 特效與影片顯示可能受限，不能當作完整視覺功能驗收。
軟體後端限制見 [Qt 官方文件](https://doc.qt.io/qt-6/qtquick-visualcanvas-adaptations-software.html)。

本次靜音＋software Debug APK 已增量建置（exit 0）、覆蓋安裝，確認首頁顯示、
「關於」對話框與已就緒首頁的前後景恢復（同一 PID，17.8 秒檢查無崩潰）。
首頁約 49 秒才就緒，第一次自動化 48 秒子預算逾時仍保留為失敗，不外推為啟動效能通過。
證據位於 `builds/android-silent-20260916/report.md`；完整对局、實機、CI 及 GPU 特效未驗收。

## AAudio CFI 音訊橋接：已確認故障機制（2026-09-16）

由既有兩宗 AudioTrack 崩潰（PID 7606／3856）的二進位 tombstone，
已取得 callback、Qt guest、ndk_translation helper 及 fault shadow 的必要映射。
host AAudio 準備呼叫的 x86_64 stub 位於匿名 rwx 區域，內嵌目標分別指向
Qt Multimedia ARM64 程式碼及 libndk_translation.so；兩份 stub 去除 ASLR
立即數後一致。

同 BuildId libdl.so 的 __cfi_slowpath+29 是讀取 16-bit shadow 的指令；
兩次 fault 都精確落在不可讀的 [anon:cfi shadow]，尚未執行 callback 或
CFI 型別失敗處理。這已確認該映像／ARM 橋接路徑的 CFI 整合失效，
仍未定位 translator／linker 的具體實作錯誤，也未核實任何已修復版本。

詳細映射、指令、擷取限制及後續驗證方向見
`builds/android-audio-investigation-20260916/cfi-boundary-confirmed.md`。
二進位擷取仍有每筆 256 KiB 限制，但上述必要映射完整可見。
本輪未建置、安裝或重新啟用音訊；保留 NULL 隔離。
WAV、音量零、Qt push mode 或單設 QT_MEDIA_BACKEND 都不能保證避開此回呼。

## 開局後主執行緒 0x58：隱藏手牌修正（2026-09-16）

`builds/android-complete-game-20260916-0952/` 的 PID 4915 崩潰使用 NULL 音訊與
software／raster APK。完整 SYSTEM_TOMBSTONE 的記憶體指令，與 APK 同 BuildId
`d1808843aa1fec0b24370989114ee5b943f8f6ee` 的 native 符號相符：
`Player::addCard()` 呼叫空卡牌的 `Card::getId()`，讀取 `this + 0x58`。

該 Android 固定工作樹漏帶主分支 `5097690` 的隱藏手牌修正。開局收到的 `-1`
代表未知牌，只能增加手牌張數，不能放入實體 `Card *` 清單；同一筆手牌移動也
不能重複計入。更新 APK 前須將該提交的 `src/core/player.cpp`、
`src/client/client.cpp`、`src/client/clientplayer.cpp/.h` 一起對齊到固定 Android
工作樹，不能只同步 UI／音訊檔案。保留其他工作樹差異，不作整樹覆蓋。

本輪已補入這組修正並通過 `git apply --check`／`git diff --check`；
APK 重建與開局回歸尚待執行。這個空卡牌缺陷與前節的 AAudio CFI callback
崩潰不同，修復它不代表恢復有聲。完整證據與驗證狀態見
`builds/android-mainthread-investigation-20260916/`。

## 驗證限制與故障分類

目前只驗證 Android Emulator。x86_64 模擬器執行 arm64 APK 時包含 ARM translation layer，不能代替 arm64 實機、Android 9/16 或 16 KB page-size 環境。最新 API 33 模擬器的已知音訊閃退位於 AAudio CFI callback under translation；目前證據不能把它歸因於某一個 OGG 檔案。遇到閃退須連同 `adb logcat`、ABI、映像及是否播放音效記錄，不能只憑閃退判定規則核心回歸。四個短 UI WAV 只降低 codec 依賴，不代表完整 OGG 已驗收。

本頁不執行 CTest、跨版本矩陣或手機驗收；05p 完整對局流程與最新結果見 [簡化驗收](android-acceptance.md)。相關契約見 [Android 首版功能](android-first-release.md)，目錄與版本切換見 [Android 擴展實體目錄](android-extension-runtime.md)。

## 歷史建置證據（不作目前驗收）

| 輪次 | 證據 | 限制 |
|---|---|---|
| CP1 | `builds/android-cp1/`；舊 Debug APK SHA-256 `00db9718f62766b0497742244b5e82948f240f70d4d15c03a62effee14ba67fc` | 舊 source/產物；未做手機、Release、完整對局驗收 |
| 首版重建 | `builds/android-v1-validation/`；複製 Debug APK `QSanguosha-Android-arm64-debug.apk` SHA-256 `0baed09ee133af17ed929b8178cf8edcf3c6a0ffa79c3ffbc4f5c4da345d3dea` | Android 13 x86_64 emulator 以 ARM translation 執行，4 KB page；不能代替 arm64 實機/16 KB；音訊輸出有限制 |
| 宣告修正後 Qt build10 | `QSanguosha-Android-arm64-debug-qt-10.apk`，363,550,018 bytes；SHA-256 `2749358d9e8166a0c2b8dee4570eae888b307ea24c4206e2fb9c5deb8490e262` | 建置、Debug 簽章、ELF／ZIP 16 KB 靜態稽核通過；不能沿用 build09 診斷 APK 的對局結果 |
| 無音訊診斷 build09 | `QSanguosha-Android-arm64-debug-null-audio.apk`；SHA-256 `f1edf1b55dec34bc3d6f65c8a42cc02e56444c382da236cdf08af65123a68a62` | 編譯選項 `QSAN_AUDIO_BACKEND=NULL`；只隔離 AAudio 問題，不能當正常音訊版本交付 |

歷史「建置通過」與「靜態對齊通過」只適用於各自 source hash、工具鏈及 APK；來源修改後必須重新建立與稽核。

本輪完整對局與清理結果見 `builds/android-v1-validation/android-gameplay-summary.md`；模擬器、診斷部署與正常 APK 的證據分列。以上 APK、log 與 audit 均位於 `builds/android-v1-validation/`，不是需要加入 Git 的來源檔。
