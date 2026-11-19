# Android TV 側載版

Android TV 沿用同一 Qt 客戶端、套件 `org.qsanguosha.game` 與大屏模式。
最低 Android 9／API 28；ARM64 與 ARMv7 分別產生 APK，依裝置 ABI 安裝。
建置環境及一般 Android 更新流程見 [Android 建置](android-build.md)。

## 啟動與操作

TV launcher 可透過 `LEANBACK_LAUNCHER` 啟動遊戲；Manifest 提供 TV 橫幅，
並把 touchscreen／faketouch 設為非必要。手機與平板繼續使用原來的觸控行為。
偵測到 Android 的 leanback 或 television feature 時，自動啟用既有大屏配置及遙控器輸入。

| 畫面 | 方向鍵 | OK | 返回 |
|---|---|---|---|
| 首頁、設定、彈窗 | 移動焦點；清單上下捲動 | 執行、開啟選項或文字輸入 | 關閉彈窗、返回首頁；首頁再返回則退出 |
| 牌桌 | 上下換卡牌／目標／技能／操作群組；左右換選項 | 選取目前項目 | 取消可取消的選擇 |
| 操作群組 | 左右選確認、結束回合、詳情、遊戲選單 | 執行顯示的指令 | 保留既有強制回覆限制 |
| 等待開局 | 上或 OK 開啟遊戲選單 | 選單可填入電腦並開始遊戲 | 開啟遊戲選單 |

名稱、伺服器地址等 QWidget／QML 欄位共用原有螢幕文字編輯器；
只用方向鍵及 OK 即可輸入、刪除、確認或取消。密碼預覽保持遮蔽。
設定頁提供「資源與擴充套件」入口；對局選單也可開啟資源管理及返回首頁。
Android 系統檔案選擇器保留 Storage Access Framework 授權流程，
其遙控器支援取決於裝置提供的檔案選擇器。聲畫資源沿用現有選用匯入機制。

## 建置可安裝的 Release APK

重用 `H:\qsan-android-x86_64` 的 Qt 6.11.1、NDK、JDK、FreeType source 與 Gradle cache。
首次 ARM64 需要同版 `android_arm64_v8a` Qt kit；`build-android.ps1` 會建立對應 FreeType 靜態庫。
`-SignRelease` 使用本機既有持久簽章設定，讓 Release 與目前開發 APK 可覆蓋更新；
私鑰與密碼不進倉庫。若缺少有效設定，建置會停止，不會悄悄換簽章。

```powershell
./tools/build-android.ps1 -Configuration Release -Abi arm64-v8a `
  -ToolchainRoot H:/qsan-android-x86_64 `
  -SdkRoot "$env:LOCALAPPDATA/Android/Sdk" -SignRelease -PreserveDeviceContent
./tools/build-android.ps1 -Configuration Release -Abi armeabi-v7a `
  -ToolchainRoot H:/qsan-android-x86_64 `
  -SdkRoot "$env:LOCALAPPDATA/Android/Sdk" -SignRelease -PreserveDeviceContent
```

`-PreserveDeviceContent` 保留使用者已安裝的規則／媒體資料；一般開發版預設內容同步仍維持原行為。
輸出位於各 ABI preset 的 `cmake/android-app/android-build/build/outputs/apk/release/`。
以 `apksigner verify` 確認簽章後，使用 `adb -s <serial> install --streaming -r <apk>` 側載。
簽章不符時停止；不可為了更新而卸載或清除 App 資料。

## 模擬器遙控器診斷

沿用既有雷電14及 x86_64 Debug cache，不建立新 AVD。普通 Android 模擬器可用
`--android-tv` 強制本次啟用 TV 配置；這個選項不寫入裝置設定。
正式電視依 feature 自動啟用，不需要啟動參數。

```powershell
adb -s <serial> shell am start -W -n org.qsanguosha.game/org.qtproject.qt.android.bindings.QtActivity `
  --es applicationArguments '"--android-tv --controller-remote-only --controller-trace=/data/user/0/org.qsanguosha.game/files/tv-controller.jsonl"'
python tools/android/remote_driver.py observe --serial <serial> --output <evidence-directory>
python tools/android/remote_driver.py press --key down --serial <serial> --output <evidence-directory>
```

`remote_driver.py` 讀取既有控制器可見模型，只送 Android D-pad、OK、Back 系統按鍵，
不寫遊戲回覆。`play` 沿用桌面診斷工具的基本房間操作策略，單次最多 1800 秒；
只在授權的單局驗收使用。不得以托管取代遙控器玩家操作。
首頁短驗收、完整對局／結果畫面、正常返回與退出，以及實體 TV 遙控器相容性分別記錄。

## 共用輸入契約

`ControllerService` 以平台來源選擇桌面 SDL 或 Android 按鍵後端，
共用 `ControllerAction`、`ControllerRouter`、`DesktopGamePresentation` 和 `ClientCore`。
Android 不連結 SDL、不建立另一套選牌或協議回覆流程。
按鍵切入背景時清除 held state 並遞增 device epoch；重複確認鍵不穿透新彈窗，
方向長按才允許重複移動。控制器在首次資源／回復畫面前啟動。
