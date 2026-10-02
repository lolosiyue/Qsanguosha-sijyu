# Android APK 建置、更新與驗收

本機日常使用原生 `x86_64` APK 與既有 API 33 模擬器；`arm64-v8a` 留作實機／發行建置參考。
本機日常驗收以覆蓋安裝、`adb logcat`／截圖收集，以及完整局與正常退出分開判定為準。
首次外部聲畫 ZIP／Storage Access Framework（SAF）匯入流程保留於本文後半，日常更新不需重做。
32 位元 ARMv7 的裝置選擇、模式設定與收尾，直接查閱
[ARMv7 操作與問題處理](#android-armv7-reuse)；不要重新準備工具鏈或媒體包。
使用者指定 Android 14／雷電14 時，改查 [雷電14 重用流程](#android-14-ldplayer-reuse)，
不啟動 API 33 AVD、不下載新的 system image；建置仍沿用同一 x86_64 cache。

**版本固定規則：Android 不再雜湊資源、不逐檔掃描聲畫，也不因圖片缺檔擋住開局。**
已安裝媒體在 APK 更新後繼續沿用；新增圖片不觸發全包重匯。
版本固定規則的執行期行為見 [Android 擴展執行期](android-extension-runtime.md)。

<a id="android-daily-environment"></a>
## 本機唯一日常環境

未指定其他裝置時，日常操作固定使用下面這套環境，保留 App 資料、媒體與建置快取。
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
5. 分別記錄首頁短驗收與 05p 完整局的證據。驗收工具不建置、不修改模式設定、不自動判定 GAME_OVER。

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

**已知耗時來源**：首次 ZIP 匯入的 100% 只表示解壓進度，後面仍會建立可用版本。
此 AVD 的 SELinux 拒絕硬連結（hard link），程式退回逐檔實體複製；新增 APK
基線資源也觸發第二輪版本複製，同樣會耗時並占用數 GB。[新資源流程的來源修正](android-extension-runtime.md#匯入更新效能修正)
已移除平方次數 ZIP 比對、可 seek 來源的 spool／重複雜湊讀取及媒體版本複製；
內容準備只建立媒體目錄引用並複製規則／介面。移除資源雜湊與聲畫掃描後，缺圖片
不再擋住連線；先前該 AVD 的有聲局因 AudioTrack 崩潰而未完成。該環境首次完整
匯入的新耗時未重測，不能用啟動秒數代替；ARMv7／LDPlayer 的靜音完整局另見下節。

首次匯入期間 Download ZIP、私有 spool、解壓 blob 與 runtime 版本可能同時存在，
12 GiB 分割區曾接近滿載。日常不要重複保留傳輸副本；匯入完成後清理本輪傳輸檔，
保留 H 碟原包與 App 私有資料。不要手動刪除 content store 的 baseline／blobs／versions。

此 AVD 是 API 33 x86_64／4 KiB pages；日常 APK 使用原生 x86_64。模擬器上的
ARM translation 執行不能代替 arm64 實機；兩者均不等同實機或折疊機驗收。

<a id="android-14-ldplayer-reuse"></a>
## Android 14／雷電14：重用與故障處理（2026-10-03）

本節適用於使用者指定既有雷電14的工作。優先重用既有安裝、App 資料、媒體及上方 x86_64
建置快取；「最新版 Android APK」不代表下載新 Android 映像。既有 API 33 AVD 不能當作
Android 14 驗收。以下位置是本次實測錨點；每次重新核對程序、VM、序號與 API，不沿用舊 PID。

| 項目 | 本次沿用值 |
|---|---|
| LDPlayer | `L:\LDPlayer\LDPlayer14`，版本 `14.0.9.2`，index `0` |
| VM 身分 | UUID `20160302-aaaa-aaaa-4882-000000000000`；本次 guest IP `172.16.1.15` |
| VBox 控制工具 | `C:\Program Files\ldplayer9box\VBoxManage.exe`；工具路徑含 9 不代表 guest 是 Android 9 |
| 獨立 ADB 通道 | 本次 `127.0.0.1:5591`，localhost 暫時 NAT 到已確認的 LD14 guest `5555` |
| 系統 | Android `14`／API `34`、x86_64、4 KiB pages |
| 本次 APK 建置 | `tools/build-android.ps1 -Configuration Debug -Abi x86_64 -AudioBackend NULL`；既有 `H:\qsan-android-x86_64` |
| 已完成證據 | [05P 報告](../builds/android-10p-20261003-011a/summary.md)、[機器結果](../builds/android-10p-20261003-011a/result.json) |

### 先辨認裝置，再建立通道

LD9 與 LD14 可同時有 index `0`、VM 名稱 `leidian0`。本次即使執行 LD14 的
`ldconsole adb --index 0`，預設 `5555` 仍連到 LD9／Android 9。所有命令必須使用明確的
`adb -s <序號>`，並以 guest `getprop` 證明版本；不能只憑 console 路徑、名稱或視窗標題。

```powershell
$ld14Console = 'L:\LDPlayer\LDPlayer14\ldconsole.exe'
$ld14VBox = 'C:\Program Files\ldplayer9box\VBoxManage.exe'
$ld14Adb = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
$ld14StartedByThisRun = $false # 只有本輪自行啟動的原停止實例才改為 true。
& $ld14Console list2
& $ld14VBox list runningvms
& $ld14Adb devices -l
```

先將 `list2` 的 GUI／VM PID 對照程序路徑與 `--startvm` UUID，確認是使用者指定的既有 LD14。
若它原本停止且本輪已授權啟動，只啟動此實例。再核對該 VM 的 guest IP；下例 IP 是本次值，
不能猜另一台 VM 的 IP 或重用其他工作的 NAT 規則。需要獨立通道時才執行：

```powershell
$ld14Vm = '20160302-aaaa-aaaa-4882-000000000000' # 已核對的 LD14 UUID。
$ld14GuestIp = '172.16.1.15' # 先核對這次仍是該 VM 的 guest IP。
$ld14Port = 5591
$ld14Serial = "127.0.0.1:$ld14Port"
$ld14Rule = 'qsan-05p-' + (Get-Date -Format 'yyyyMMdd-HHmmss')
if (Get-NetTCPConnection -LocalPort $ld14Port -State Listen -ErrorAction SilentlyContinue) {
    throw 'Port already used; inspect ownership instead of replacing the mapping.'
}
& $ld14VBox controlvm $ld14Vm natpf1 "$ld14Rule,tcp,127.0.0.1,$ld14Port,$ld14GuestIp,5555"
if ($LASTEXITCODE -ne 0) { throw 'Owned LD14 NAT setup failed.' }
& $ld14Adb connect $ld14Serial
if ($LASTEXITCODE -ne 0) { throw 'LD14 ADB connection failed.' }
& $ld14Adb -s $ld14Serial shell getprop ro.build.version.release
$ld14Api = (& $ld14Adb -s $ld14Serial shell getprop ro.build.version.sdk).Trim()
if ($ld14Api -ne '34') { throw 'Wrong Android target; do not install.' }
& $ld14Adb -s $ld14Serial shell getprop sys.boot_completed
& $ld14Adb -s $ld14Serial shell getprop ro.product.cpu.abilist
& $ld14Adb -s $ld14Serial shell getconf PAGE_SIZE
```

`controlvm ... natpf1` 是執行中的暫時規則；不改持久 VM 設定，不開放到外部網卡。
記住本輪 rule、序號、程序身分及模擬器原本是否啟動，正確記錄 `$ld14StartedByThisRun`，
供最後精確還原；不要重啟共用 ADB server。

### 建置、安裝與 server 版本對齊

1. 先核對來源與 dirty state，依上方固定命令在既有 cache 增量建置；沒有新來源時沿用已建 APK。
   建置仍需本輪授權與完成檢查點，不因讀到本節自動執行。
   本次已驗收的是 NULL 包，重現時明確傳入 `-AudioBackend NULL`，不依賴之後可能變更的預設值；
   其他音訊後端須另有驗收證據，不能沿用本次靜音結果。
2. 核對建置退出碼、新 APK 路徑及持久簽章。LD14 使用下例 streaming 覆蓋安裝，保留資料：

   ```powershell
   $ld14Apk = 'L:\finaldebug\QSanguosha-v2\builds\android-x86_64-debug\cmake\android-app\android-build\build\outputs\apk\debug\android-build-debug.apk'
   & $ld14Adb -s $ld14Serial install --streaming -r $ld14Apk
   if ($LASTEXITCODE -ne 0) { throw 'Install failed; preserve App data and inspect the error.' }
   & $ld14Adb -s $ld14Serial shell sync
   ```

3. 外部 Windows server 先保存本輪固定副本及其原生規則身分；不能一直指向其他工作也會更新的
   `debug/qsanguosha_server.exe`。核對 APK **實際打包的** native library 與固定 server 的
   CPP／bindings 身分，不只看 HEAD、CMake cache 或 APK 檔案時間。
4. 若連線拒絕 rules identity，區分原生 CPP／bindings 不同與有效 Lua／規則不同。
   規則、extensions、lang 及宣告只在隔離 server runtime 對齊 Android active runtime，
   保留裝置原內容；server AI 為 server-owned，使用本次建置來源的現行 AI，不能被裝置舊 AI 覆蓋。
   不停用身分檢查，不改外部 Lua 權威倉庫，不搬移、掃描或雜湊聲畫。

| 本次問題 | 判定與下次處理 |
|---|---|
| `--no-streaming` 安裝出現 restorecon／`INSTALL_FAILED_MEDIA_UNAVAILABLE` | streaming `install -r` 成功；不卸載、不清資料、不改 SELinux／root |
| Gradle 已 `BUILD SUCCESSFUL`，外層未退出 | 本輪 idle daemon 保留輸出管線；依 [daemon 處理](#android-armv7-reuse) 核對身分後只停本輪 daemon，等待真正退出碼，不重建 |
| APK 舊 CPP、共享 server 已被其他工作更新 | 固定 server、重新對齊原生身分；本次做一次必要 Android 增量更新，不以同一 HEAD 當一致證據 |
| server mirror 的舊 AI 在初始化時失敗 | 恢復本次 APK 建置來源的 server AI；規則／翻譯仍用已對齊副本，未開局不能算完整局 |
| 顯示「加入完整聲畫 ZIP」、`boot_attempt=true` | 先讀 [啟動未完成回復](android-extension-runtime.md#android-boot-attempt-recovery)，不因按鈕文字重新匯入 ZIP |
| ADB offline 且 VBox 有 host `powerDown` | 保存第一份 VM／App 日誌；caller 未知不判成 App crash，不反覆重開局或擴大引擎除錯 |

### 完整 05P 與正常收尾

本次是 Android GUI 連線 Windows server 加四席機器人；不是 Android 內建單機 server 驗收。
server 使用 `05p`、AI on、Cheat off、operation timeout 15 秒、AI delay 0，並保存 seed、設定及
`--autotest-log`。長對局需本輪明確授權；本次上限 30 分鐘不構成日後自動執行的授權。

server 初始化成功、規則身分相符後，沿用產品的 `--network-ui-smoke`，不用再建立另一套 GUI
回覆邏輯。參數說明見 [既有 native GUI 工具](linux-development-environment.md#--network-ui-smoke)。
下例埠須改為**本輪已啟動 server 的 TCP 埠**；`--websocket-port 0` 會自動分配 WebSocket 埠，
不是停用 WebSocket，收尾須從 server 日誌取得實際埠。

```powershell
$ld14GamePort = 3671 # 本次範例；先核對正在使用的 server TCP 埠。
$ld14RunId = Get-Date -Format 'yyyyMMdd-HHmmss'
$ld14RemoteResult = "/data/user/0/org.qsanguosha.game/files/android-05p-$ld14RunId.json"
& $ld14Adb -s $ld14Serial reverse "tcp:$ld14GamePort" "tcp:$ld14GamePort"
if ($LASTEXITCODE -ne 0) { throw 'Owned reverse mapping failed; do not launch.' }
$ld14Args = "-connect:127.0.0.1:$ld14GamePort --auto-robots --network-ui-smoke --network-ui-smoke-result $ld14RemoteResult --network-ui-smoke-timeout-ms 1800000 --network-ui-smoke-stall-ms 600000"
# 完整 applicationArguments 必須在遠端 shell 保持單一引數。
& $ld14Adb -s $ld14Serial shell am start -W `
  -n org.qsanguosha.game/org.qtproject.qt.android.bindings.QtActivity `
  --es applicationArguments "'$ld14Args'"
```

結果檔每輪使用新名稱，避免讀到舊 JSON。先收集本輪 logcat／server 原始日誌，再啟動；不要清空
crash buffer。啟動監看使用產品實際標記 **`[AUTOTEST] game start`**，結局是
**`[AUTOTEST] game over <winner>`**。本次監看器錯找大寫 `GAME_START`，在真正開局後仍觸發
啟動逾時並中止了一局；修正後才完成自然局。不能只用標記檔存在判定開局，也不能向 producer
日誌補寫自造標記。收到實際 game start 後停用開局前 deadline，保留對局總上限。

| 完成條件 | 必要證據 |
|---|---|
| 自然完整局 | client `game_started=true`、`game_over=true`、`ok=true`，與 server 原始開局／結局及 winner 相符 |
| GUI 回覆 | responder request／reply 與 UI actions；`trustee_engaged=false`，託管 fallback 不能冒充此 gate |
| client 正常退出 | 等待自行退出、`pidof` 為空；`dumpsys activity exit-info` 對應本輪 PID 的 `EXIT_SELF / status 0` |
| server 正常退出 | 正常 console `shutdown`、退出 0、生命週期最終歸零；不用強停代替 |
| 清理 | TCP／實際 WebSocket 埠釋放、collector／本輪程序無殘留、只移除自己的 reverse／NAT |

結果及退出資訊可透過指定序號讀取，保存本輪輸出，不讀其他實例的舊紀錄：

```powershell
& $ld14Adb -s $ld14Serial exec-out run-as org.qsanguosha.game cat "files/android-05p-$ld14RunId.json"
& $ld14Adb -s $ld14Serial shell dumpsys activity exit-info org.qsanguosha.game
& $ld14Adb -s $ld14Serial shell pidof org.qsanguosha.game
```

App 已自行退出、server 已正常關閉後，先保存 final content state 與退出證據，再依本輪保存的值清理：

```powershell
& $ld14Adb -s $ld14Serial reverse --remove "tcp:$ld14GamePort"
& $ld14VBox controlvm $ld14Vm natpf1 delete $ld14Rule
& $ld14Adb disconnect $ld14Serial
# 僅在確認仍是本輪啟動的 LD14、且它原本停止時，正常還原停止狀態。
if ($ld14StartedByThisRun) { & $ld14Console quit --index 0 }
```

reverse 已由本輪 runner 移除時不重複執行。LD14 原本已運行則保留，不關其他實例或全域 ADB。
最後核對本輪程序與三種埠，不以 `quit` 命令退出碼代替清理完成證據。

2026-10-03 的修正後 05P 自然完成，反賊勝，對局 319 秒；無託管，client `EXIT_SELF / 0`、
server 退出 0，清理通過。此前被監看器強停的局及未開局嘗試均不算 PASS。日誌仍有
`hegemony-ai.lua:450` 的 `cloneCard` nil、shuangren 拼點卡未找到及 snapshot JSON 有損序列化
警告；保留作待處理項，完整局通過不代表它們已修復。NULL 音訊、有 full effects 計數或既有
聲畫不能證明有聲、人工觸控、特定視覺效果或 ARM 實機通過。

<a id="android-armv7-reuse"></a>
## ARMv7 操作與問題處理（2026-10-03）

本節保存本次 32 位元建置與單機 `03_1v2` 的可重用流程，不取代上方 x86_64 日常環境。
本次經使用者授權改用既有 LDPlayer 9，保留所有 App 資料；下次先核對任務授權與裝置 ABI，
不因這筆歷史紀錄自動切換模擬器或啟用 root。

### 沿用位置與建置

| 用途 | 已使用的位置／值 |
|---|---|
| ARMv7 Qt kit | `H:\qsan-android-x86_64\qt\6.11.1\android_armv7`；共用根目錄名稱保留 x86_64，實際 kit 是 ARMv7 |
| ARMv7 Release cache | `builds/android-armv7-release`；`CMakeCache.txt` 在此根目錄，不在其 `cmake/` 子目錄 |
| 工具鏈 | 共用根 `H:\qsan-android-x86_64`、JDK 21、NDK `27.2.12479018`、既有 SDK；不另裝一套 |
| Release 原產物 | `builds/android-armv7-release/cmake/android-app/android-build/build/outputs/apk/release/android-build-release-unsigned.apk` |
| 已簽章修正 APK | `builds/android-armv7-10p-20261003/QSanguosha-armeabi-v7a-release-zip64-signed.apk` |
| LDPlayer 9 | `L:\phone\LDPlayer\LDPlayer9`；既有 index `0`，名稱「雷電模擬器L」 |
| 本次裝置 | Android 9／API 28、`zygote64_32`、`libnb.so` ARM translation；當時序號 `emulator-5554`，同一實例亦顯示為 `127.0.0.1:5555` |
| 實例設定 | `L:\phone\LDPlayer\LDPlayer9\vms\config\leidian0.config`；本次原狀為 ADB 關閉、root 停用 |
| 已匯入媒體 | 沿用 App 私有內容；原包仍是 `H:\qsan-validation\room-responsive-20260916\qsan-media.zip` |
| 一次性證據 | [完整驗收報告](../builds/android-armv7-10p-20261003/summary.md)；目錄的 10p 是原任務名稱，實際指定局改為 `03_1v2` |

裝置啟動後先用 `adb devices -l` 與 LDPlayer `list2` 確認序號對應的 index，所有 ADB 操作指定
`-s`。不要把別的工作正在使用的實例、重複 TCP 別名或 offline 項目當成新的待測裝置。

```powershell
$adb = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
$serial = 'emulator-5554' # 先確認它仍對應已授權的 LDPlayer index 0。
& $adb -s $serial shell getprop ro.product.cpu.abilist
& $adb -s $serial shell getprop ro.product.cpu.abilist32
& $adb -s $serial shell getprop ro.zygote
```

原 `Responsive_API_33` 只有 x86_64／arm64，`zygote64` 不支援 32 位元 App；
ARM64 translation 存在也不等於能跑 ARMv7。先做上面的只讀核對，ABI 不符即停止安裝，
不重建同一 APK、不清資料，也不另建 AVD。

已有建置授權且完成約定檢查點後，沿用此命令增量建置：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-android.ps1 `
  -Configuration Release -Abi armeabi-v7a `
  -ToolchainRoot H:\qsan-android-x86_64 `
  -SdkRoot "$env:LOCALAPPDATA\Android\Sdk" -AudioBackend NULL
if ($LASTEXITCODE -ne 0) { throw 'Build failed; do not install an older APK.' }
```

`-Abi` 使用 `armeabi-v7a`；`armv7` 是 kit／preset／cache 名稱，不能拿來當腳本的 ABI 參數。
核對 cache 的來源仍是 L、`ANDROID_ABI=armeabi-v7a`、`QSAN_AUDIO_BACKEND=NULL`，
保留增量 cache，不用 `--fresh` 或 `--clean-first`。沒有來源變更時可沿用已交付 APK，
不為讀文件再建置或重跑已完成的局。

本次 Release 原產物未簽章，另外以既有持久開發憑證簽章；沿用下方
「固定開發簽名與覆蓋更新」的本機設定，金鑰與密碼不寫入文檔或倉庫。
先以 `apksigner verify --verbose --print-certs` 核對原憑證，再 `install -r`；
簽章不符就停止，不卸載。本次 APK 的 90 個 `.so` 均為 `ELF32 / EM_ARM`，唯一 ABI 是
`armeabi-v7a`；檢查應涵蓋 APK 內所有 native libraries，不只主程式。

本次曾在 Gradle 顯示 `BUILD SUCCESSFUL`、APK 已產出後，外層命令仍等待輸出管線 EOF；
新啟動的 Gradle daemon 保留了管線。先保存建置日誌、核對本輪 APK 與程序命令列／啟動時間，
只處理能證明由本輪建立的 daemon，再等待外層退出碼。不能因此重新建置、清 cache、
將成功文字直接當退出碼，或停止全部 Java／其他工作共用的 daemon。

### 首次匯入與模式設定

| 遇到的問題 | 已確認處理／下次入口 |
|---|---|
| ARMv7 匯入 2.75 GB ZIP 報 `cannot read ZIP footer` | 使用已包含 32 位元大檔 reader 修正的 APK，見 [大檔 ZIP 故障分類](android-extension-runtime.md#android-32bit-zip-footer)；保存首次錯誤，不重打包或反覆傳輸同一媒體 |
| 匯入進度達 100% | 還要確認完整暫存／版本準備成功，再重啟使版本生效；只有 `adb push` 或解壓 100% 不算匯入完成。已完成匯入的日常 APK 更新不重匯 |
| 本次 APK 首頁固定 2P，沒有模式設定入口 | 先查本次待測版本是否已提供設定入口；只有仍缺入口且取得明確授權時才暫時 root 調整 App 設定。模式鍵是 `GameMode=03_1v2`，不是顯示名稱 `03_1V2` |
| `adb root` 不支援 production adbd | 不反覆嘗試；本次使用 LDPlayer 正式 `modify --index 0 --root 1` 後重啟，透過 `su` 僅存取 QSanguosha 私有檔案，操作後停用並驗證 |
| `su -c cat` 的輸出將 LF 轉為 CRLF | 二進位備份用 `exec-out` 讀取 Base64，再在主機解碼及以 bytes 儲存；不能把終端文字輸出直接當設定原檔 |
| Android 返回鍵意外觸發預設 2P | 首頁不以 BACK 當選單／退出操作；使用 `KEYCODE_MENU` 開啟原生選單。誤開局另記中止，不計指定完整局 |

需要暫改設定時，先正常關閉 App 並確認 PID 消失；先備份 LDPlayer 原設定檔，
再啟用 root。App 設定實際位置是
`/data/user/0/org.qsanguosha.game/files/config.ini`，不是 `userdata/` 或某個 runtime 快照內。
記錄原檔 bytes、UID、GID 與 mode；本次為 11420 bytes、`10062:10062:600`，這些數字不能
當下一次的固定值。先取得未經文字換行轉換的備份，再只改 `[General]` 的 `GameMode`，
若缺鍵則新增該鍵；寫回既有 inode，避免替換檔案造成 owner／權限改變。

可重用的原始位元讀取方式如下；命令僅在已授權的暫時 root 期間使用，ADB 輸出須以 bytes
捕捉，Base64 在主機解碼，不能經 PowerShell 的文字重導向保存原檔：

```text
adb -s <已確認序號> exec-out su -c "base64 /data/user/0/org.qsanguosha.game/files/config.ini"
```

寫回後以同一方法逐位元核對，並核對 owner／mode；重新啟動 App，先看首頁確實顯示
「3人局［斗地主］」，再點 QuickJoin。備份及還原在 App 關閉時做，避免 App 稍後覆寫設定。
本次的 `configure_mode_verified.py`／`restore_app_config.py` 是證據目錄中的一次性腳本，
包含固定序號、原 bytes 與 UID，不能直接當成下次通用 runner。

### 結局判定與還原順序

1. 開局前保存診斷日誌基線、實際模式／人數、ABI、音訊後端與預算。本次從開局起使用產品
   auto-robots／選將／Trustee，不在卡住後更改驗收方式。`--test-general=caocao` 不在候選時
   會正常選其他候選，核對 `chooseGeneral` 日誌；本次實際是 `heg_xugong`。
2. `client_autotest_diag.log` 可能在目前 active 的 `content/versions/<id>/runtime/`，
   另有 `userdata/` 日誌；查實際 active 路徑，不沿用舊版本 UUID。只看本次基線之後新增的
   `GAME_STARTED players=3`／`GAME_OVER`，再以勝負畫面確認勝方。
   `GAME_OVER victory=0` 是本機玩家未勝，不能把 `0` 當勝方 ID 或失敗的完整局。
3. 勝負對話框出現後，先保存結果、點「Return to main menu」。模態對話框未關閉時，
   背後 Game 選單不能用來退出。返回首頁後 `KEYCODE_MENU` → Game → Exit → 確認；
   `force-stop`、重啟模擬器或殺程序僅是異常清理，不能算正常退出。
4. 分開核對房間收尾與 App 退出：本次單機使用 loopback 9527／9528，返回首頁後埠及
   App UID 的 socket 已消失，正常退出後 PID 消失。下次從本輪 PID／UID 與 socket
   記錄識別歸屬，不拿其他 App 的 listener 當成孤兒。
5. App 退出後還原設定原 bytes、UID／GID／mode，只清理本輪新增的 Download ZIP、
   設定傳輸檔與 UI dump；保留已匯入媒體、H 碟原包與其他 App 資料。
6. 若原本 root 停用，`modify --index 0 --root 0` 後重啟，核對一般 ADB 為 shell UID、
   `su` 不存在且 App 沒有自動重開；只看 host 設定欄位不算 guest root 已停用。
   關閉本輪實例，確認停止後精確還原 LDPlayer 原設定，包括 ADB 開關。
   回收本輪 collector，不停止共用 ADB server，也不影響其他實例。

本次唯一指定局自然 `GAME_OVER`，地主孫尚香［國］勝，場內 12 分 40 秒；
正常回首頁／退出、埠釋放、App 設定與 LDPlayer 原設定還原均已核對。
原定 10P 已被使用者改為 `03_1v2`，不再補跑 10P；ARM translation、`NULL` 音訊與託管
不能代替實機、音訊或人工觸控驗收。逾時、崩潰或停止只保存第一份失敗證據，
新增修復／重開局仍遵守範圍與檢查點授權，不因完整測試授權無限重試。

## 固定工具鏈與目錄

### 共用遊戲呈現入口（尚未裝置驗收）

對局右上角「More actions」選單提供「遊戲狀態」與「遊戲操作面板」。兩者沿用
桌面的 `GameViewState`／`GameActionModel` 與 RoomScene 草稿，不另外實作 QML
回覆或規則。文字快照主動更新、可複製；操作面板使用標準 Widgets、整頁捲動及
至少 48 logical-pixel 觸控高度。關閉面板不取消請求，背景及同步未完成時停用操作。

支援與限制見 [共用呈現契約](client-core-interaction-model.md#other-client-adapters)。
Android 建置、裝置觸控／外接鍵盤與 TalkBack 尚未驗收；桌面測試結果
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

### ARM 32 位元（armeabi-v7a）

ARMv7 沿用 Qt 6.11.1 與現有 Android GUI，最低 Android 9／API 28；
不使用 XP／Qt 5 相容層。`android-armv7-debug`／`android-armv7-release`
及對應 `-apk` presets 使用獨立建置目錄，不覆寫日常 x86_64 cache。

在既有共用工具鏈補入同版 ARMv7 kit（不另建 SDK／JDK／Gradle cache）：

```powershell
python -m aqt install-qt all_os android 6.11.1 android_armv7 `
  -O H:/qsan-android-x86_64/qt `
  -m qtmultimedia qtwebsockets qtshadertools qt5compat
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-android.ps1 `
  -Configuration Release -Abi armeabi-v7a `
  -ToolchainRoot H:/qsan-android-x86_64 `
  -SdkRoot "$env:LOCALAPPDATA/Android/Sdk"
```

腳本會為 ARMv7 建立獨立 `freetype-armv7`，重用共用 FreeType 原始碼。
Release APK 簽章狀態依現有 Gradle 配置；未簽章產物不能直接安裝。
建置成功不代表 32 位元實機、記憶體峰值或完整對局驗收通過；
不要以 x86_64 模擬器驗收代替 ARMv7 裝置驗收。

### 通用建置入口

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

此腳本涵蓋宣告覆蓋、缺檔拒絕、未宣告 extension 拒絕、路徑穿越拒絕及重複套用穩定；APK 建置、ServerHello 實際連線與完整對局仍須另行驗收。

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

## Android 暫時靜音

Android Debug／Release preset、CMake Android 預設與 `tools/build-android.ps1`
均選擇既有 `NULL` 音訊後端，暫停音效、武將語音及 BGM。這是使用者同意的暫時
繞過方案；原有 Qt6 Multimedia 路徑會觸發 AAudio callback 崩潰，尚未證明已修復。
Qt Multimedia 仍供其他介面／影片功能使用。Windows 與 Linux 的音訊預設不變。

不需 FMOD SDK、不更換第三方庫、不刪除已匯入媒體。日常建置沿用上方固定工作樹、
建置快取及 AVD。若另行授權調查有聲版本，才用 `-AudioBackend QT`（或 CMake
`-DQSAN_AUDIO_BACKEND=QT`）重新啟用原音訊路徑。

驗收須區分「靜音 APK 建置／啟動／前後景成功」與「音訊缺陷修復」；前者不代表後者，
也不代表完整對局通過。

Android 啟動修復改用 Qt Quick `software` 後端及既有 raster 牌桌
viewport，避免模擬器上已觀察到的 OpenGL 破圖與前後景 EGL context 失效。
選擇在第一個 Quick window 建立前完成；Windows／Linux 保持原有 OpenGL 路徑。
這是相容性繞過，GPU shader 特效與影片顯示可能受限，不能當作完整視覺功能驗收。
軟體後端限制見 [Qt 官方文件](https://doc.qt.io/qt-6/qtquick-visualcanvas-adaptations-software.html)。

靜音＋software 的 Debug APK 首頁可顯示，「關於」對話框與已就緒首頁的前後景
恢復正常；首頁啟動時間偏長，尚未外推為啟動效能通過。
該次 Debug 首頁短驗收未涵蓋完整對局、實機、CI 及 GPU 特效；後續 ARMv7／LDPlayer
Release 靜音單機完整局見 [本次紀錄](#android-armv7-reuse)，不外推其他 gate。

## AAudio CFI 音訊橋接：已確認故障機制

由既有 AudioTrack 崩潰的二進位 tombstone，
已取得 callback、Qt guest、ndk_translation helper 及 fault shadow 的必要映射。
host AAudio 準備呼叫的 x86_64 stub 位於匿名 rwx 區域，內嵌目標分別指向
Qt Multimedia ARM64 程式碼及 libndk_translation.so；兩份 stub 去除 ASLR
立即數後一致。

同 BuildId libdl.so 的 __cfi_slowpath+29 是讀取 16-bit shadow 的指令；
兩次 fault 都精確落在不可讀的 [anon:cfi shadow]，尚未執行 callback 或
CFI 型別失敗處理。這已確認該映像／ARM 橋接路徑的 CFI 整合失效，
仍未定位 translator／linker 的具體實作錯誤，也未核實任何已修復版本。

二進位擷取仍有每筆 256 KiB 限制，但上述必要映射完整可見。保留 NULL 隔離。
WAV、音量零、Qt push mode 或單設 QT_MEDIA_BACKEND 都不能保證避開此回呼。

## 開局後主執行緒 0x58：隱藏手牌修正

NULL 音訊與 software／raster APK 的開局後崩潰，完整 SYSTEM_TOMBSTONE 的記憶體
指令與 native 符號相符：
`Player::addCard()` 呼叫空卡牌的 `Card::getId()`，讀取 `this + 0x58`。

當時 Android 工作樹漏帶主分支的隱藏手牌修正。開局收到的 `-1`
代表未知牌，只能增加手牌張數，不能放入實體 `Card *` 清單；同一筆手牌移動也
不能重複計入。此歷史紀錄供問題分類，涉及 `src/core/player.cpp`、
`src/client/client.cpp`、`src/client/clientplayer.cpp/.h`。目前直接從 L 建置，
先確認來源是否已含修正，不再複製到舊 Android 工作樹或只同步 UI／音訊檔案。

該次記錄沒有 APK 重建與開局回歸證據；不能用它判定新版仍未驗。後續 ARMv7 完整局
證據另見上方紀錄。這個空卡牌缺陷與前節的 AAudio CFI callback 崩潰不同，修復它
不代表恢復有聲。

## 驗證限制與故障分類

目前只驗證 Android Emulator。x86_64 模擬器執行 arm64 APK 時包含 ARM translation layer，不能代替 arm64 實機、Android 9/16 或 16 KB page-size 環境。最新 API 33 模擬器的已知音訊閃退位於 AAudio CFI callback under translation；目前證據不能把它歸因於某一個 OGG 檔案。遇到閃退須連同 `adb logcat`、ABI、映像及是否播放音效記錄，不能只憑閃退判定規則核心回歸。四個短 UI WAV 只降低 codec 依賴，不代表完整 OGG 已驗收。

本頁不執行 CTest、跨版本矩陣或手機驗收。目錄與版本切換見 [Android 擴展實體目錄](android-extension-runtime.md)。

