# Android arm64 APK 建置、封裝與模擬器部署

本頁是目前 Android `arm64-v8a` 工作流程：建立 APK、產生外部聲畫 ZIP、以 Android 系統檔案選擇器（Storage Access Framework，SAF）匯入，以及靜態稽核。手機與實機不在本輪範圍；裝置驗證限 Android Emulator。

## 固定工具鏈與目錄

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

從倉庫根目錄執行。腳本會暫時設定 Android、Java、Qt 及獨立 Gradle cache，完成後還原 PowerShell 環境；亦會先建立 FreeType Android static dependency。

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

### Runtime descriptor 與內容身份

Android APK 的 `runtime-content-base.json` 必須由 CMake 產生的 filtered `bundled-lua.txt` 送入 `tools/android/create-runtime-descriptor.py`。descriptor 使用 `schema_version: 2`／`profile: declared-v2`，以 `lua/config.lua` 的 `extension_names` 順序為來源；它會把 APK 實際包含的 `lua/chat_config.lua` 與 `lua/lib/sqlite3.lua` 宣告為支援 libraries，並把 AI 路徑留在 server-owned policy 外。不要手動補 descriptor、修改 `config.lua`，或關閉 rules-content identity gate。

產生 descriptor 時，若 `extensions/` 含未被 `config.lua` 宣告的腳本，或宣告的 required file 不在 filtered APK input，工具必須失敗；這能避免 `ServerHello` 以 `rules_content_unsupported` 拒絕同版內容。修改 descriptor 生成邏輯後，可執行五個快速回歸案例（不啟動 Lua、server 或 CTest）：

```powershell
python tools/android/test-runtime-descriptor.py
```

本輪結果：5 tests passed。這只證明宣告覆蓋、缺檔拒絕、未宣告 extension 拒絕、路徑穿越拒絕及重複套用穩定；APK 建置、ServerHello 實際連線與完整對局仍須另行驗收。

既有安裝遵守 missing-only，原有同名包宣告不會被新版 APK 靜默覆寫。因此舊測試版的無效宣告不能只靠 `install -r` 修好；需透過整包管理匯入有效宣告，或在隔離測試副本使用明確 `--asset-root`。後者只算診斷部署，不能當作正常升級驗收。

## 產生外部聲畫 ZIP

APK 只附基本字型、UI WAV 與必要 runtime 資源；完整媒體用實際封裝器產生：

```powershell
python tools/android/create-media-package.py . builds/android-release/qsan-media.zip
```

工具只收集 `image/`、`audio/`、`font/`，排除 symlink、`.git`、暫存/備份/執行期目錄與備份副檔名，使用 ZIP64/`ZIP_STORED`，並寫入 `qsan-media.json`（逐檔 size/SHA-256）。來源在封裝途中變更會失敗。只掃描而不寫 ZIP：

```powershell
python tools/android/create-media-package.py . builds/android-release/qsan-media.zip --inspect
python tools/android/create-media-package.py . builds/android-release/qsan-media.zip --manifest-only
```

## 模擬器安裝與 SAF 匯入

```powershell
adb devices -l
$serial = 'emulator-5580' # 換成 devices 清單內本輪選定的序號。
adb -s $serial install -r 'builds/android-arm64-debug/cmake/android-app/android-build/build/outputs/apk/debug/android-build-debug.apk'
adb -s $serial shell am start -W -n org.qsanguosha.game/org.qtproject.qt.android.bindings.QtActivity
```

完整聲畫必須走 App 內的 SAF：在資源管理按「匯入聲畫」、選取 `qsan-media.zip`、等待完整性檢查成功，再重新啟動 App 使 active version 生效。SAF 會串流到 App 私有 staging；`adb push` 到外部路徑不算完成匯入。取消、切背景或空間不足時應保留上一 active 版本。

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

## 驗證限制與故障分類

目前只驗證 Android Emulator。x86_64 模擬器執行 arm64 APK 時包含 ARM translation layer，不能代替 arm64 實機、Android 9/16 或 16 KB page-size 環境。最新 API 33 模擬器的已知音訊閃退位於 AAudio CFI callback under translation；目前證據不能把它歸因於某一個 OGG 檔案。遇到閃退須連同 `adb logcat`、ABI、映像及是否播放音效記錄，不能只憑閃退判定規則核心回歸。四個短 UI WAV 只降低 codec 依賴，不代表完整 OGG 已驗收。

本頁不包含 CTest、完整 05P、跨版本矩陣或手機驗收。相關契約見 [Android 首版功能](android-first-release.md)，目錄與版本切換見 [Android 擴展實體目錄](android-extension-runtime.md)。

## 歷史建置證據（不作目前驗收）

| 輪次 | 證據 | 限制 |
|---|---|---|
| CP1 | `builds/android-cp1/`；舊 Debug APK SHA-256 `00db9718f62766b0497742244b5e82948f240f70d4d15c03a62effee14ba67fc` | 舊 source/產物；未做手機、Release、完整對局驗收 |
| 首版重建 | `builds/android-v1-validation/`；複製 Debug APK `QSanguosha-Android-arm64-debug.apk` SHA-256 `0baed09ee133af17ed929b8178cf8edcf3c6a0ffa79c3ffbc4f5c4da345d3dea` | Android 13 x86_64 emulator 以 ARM translation 執行，4 KB page；不能代替 arm64 實機/16 KB；音訊輸出有限制 |
| 宣告修正後 Qt build10 | `QSanguosha-Android-arm64-debug-qt-10.apk`，363,550,018 bytes；SHA-256 `2749358d9e8166a0c2b8dee4570eae888b307ea24c4206e2fb9c5deb8490e262` | 建置、Debug 簽章、ELF／ZIP 16 KB 靜態稽核通過；不能沿用 build09 診斷 APK 的對局結果 |
| 無音訊診斷 build09 | `QSanguosha-Android-arm64-debug-null-audio.apk`；SHA-256 `f1edf1b55dec34bc3d6f65c8a42cc02e56444c382da236cdf08af65123a68a62` | 編譯選項 `QSAN_AUDIO_BACKEND=NULL`；只隔離 AAudio 問題，不能當正常音訊版本交付 |

歷史「建置通過」與「靜態對齊通過」只適用於各自 source hash、工具鏈及 APK；來源修改後必須重新建立與稽核。

本輪完整對局與清理結果見 `builds/android-v1-validation/android-gameplay-summary.md`；模擬器、診斷部署與正常 APK 的證據分列。以上 APK、log 與 audit 均位於 `builds/android-v1-validation/`，不是需要加入 Git 的來源檔。
