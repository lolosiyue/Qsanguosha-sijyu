# Android 簡化驗收

日常使用同一套 x86_64 APK、`Responsive_API_33`／`emulator-5586` 與現有 App 資料。
建置環境見 [Android 建置與更新](android-build.md)。不要重建 AVD、清空 cache、重新匯入聲畫包或掃描媒體。

[`tools/android/acceptance.py`](../tools/android/acceptance.py) 將之前臨時 monitor 整理成可重用入口：

| 自動處理 | 保留人工／產品操作 |
|---|---|
| 固定序號、檢查既有程序、可選 `install --no-streaming -r`、安裝後 `sync` | 在首頁／設定確認 `05p` 五人局，按「快速加入」 |
| 啟動／附加 App、logcat、定期 PNG、PID、前後退出資訊、最後監聽埠 | 選將、出牌、切背景／返回、結算後返回主菜單及正常退出 |
| 保留每次 attempt 與 PID 變化／裝置斷線錯誤 | 根據勝敗表確認 GAME_OVER 與勝方；依本次 PID／時間核對正常退出 |

目前沒有可靠的 Android GUI 自動開局／GAME_OVER／正常退出測試入口；`--auto-robots` 是既有產品的機器人選項，
不包含完整無人驗收。驗收不使用固定點擊座標、不自動投降、不強制結束 App，也不把程式碼 0 當成遊戲 PASS。
這個版本先減少 ADB 與收證操作；若要全自動，下一個獨立檢查點應加入產品內的 opt-in 驗收入口，
從現有首頁控制器開局、記錄權威結局、沿用返回首頁／退出流程，避免另造遊戲規則或跳過 UI。

## 最少操作

前置：Python 3、SDK platform-tools；用既有 AVD 視窗操作 App。
從專案工作樹執行。啟動模擬器命令見建置文件；驗收工具不建立或關閉模擬器。

```powershell
python tools/android/acceptance.py check
```

`check` 只讀取序號、ABI、PID 及本機 APK 是否存在；不建置、不啟動 App。
離線或找不到序號就停下，勿選另一台裝置頂替。

### 首頁／更新短驗收

先正常關閉 App；APK 必須來自已成功完成的建置：

```powershell
python tools/android/acceptance.py run --install --seconds 60
```

這會保留 App 資料覆蓋安裝、啟動首頁，收集最多 60 秒的觀察證據。
安裝及 `am start -W` 的時間另計；這不是保證總 wall time 小於 60 秒的 focused executable。
首頁就緒後確認：無「QSanguosha」原生標題列、素材可見，切到背景再返回，最後在首頁按 Android 返回鍵正常退出。
若仍在載入或觀察時間不夠，工具只停止收集，不關閉 App；結果記為未完成並保留原因。

不需更新 APK 時省略 `--install`。觀察正在運行的 App 用 `--attach`，不改變既有局：

```powershell
python tools/android/acceptance.py run --attach --seconds 60
```

### 完整 05p 與退出驗收

先在 App 中將模式設為五人局並正常退出，記錄原模式，驗收完再還原。
工具不直接覆寫 `config.ini`；預設二人局不能當成五人局。
完整對局需有當輪長測授權；預設不重建 APK：

```powershell
python tools/android/acceptance.py run --auto-robots --seconds 1800 --interval 10
```

| 步驟 | 驗收內容 |
|---|---|
| 1. 首頁按快速加入 | 確認 `05p`／五席；記錄日誌中的 Game Seed。auto-robots 不會代按快速加入。 |
| 2. 開局 | dashboard 主視角圖、體力、技能、操作鈕與五位玩家可見；手動操作與產品託管分別記錄。 |
| 3. 切前後景 | Home 後返回原 App；PID 不變，仍能出牌。首頁前後景不能代替遊玩中前後景。 |
| 4. 自然結局 | 保存勝敗表與勝方；不投降、不注入勝負。牌桌存在、倒數停止或託管狀態都不等於 GAME_OVER。 |
| 5. 返回主菜單 | 先保存首頁截圖，確認同一 PID 仍存活；結算通過不等於這一步通過。 |
| 6. 正常退出 | 首頁按 Android 返回鍵；核對本次 PID 的 `EXIT_SELF status=0`、`exited cleanly (0)`、PID 消失及 9527/9528 釋放。 |

命令會印出唯一證據目錄。可以在另一個 PowerShell 保存關鍵畫面，無需另寫 screencap 腳本：

```powershell
# 換成 run 印出的實際目錄；先停留在相應畫面，再執行 capture。
$run = 'L:\finaldebug\QSanguosha-v2\builds\android-acceptance-<實際時間戳>'
python tools/android/acceptance.py capture --output $run --label game-started
python tools/android/acceptance.py capture --output $run --label game-over
python tools/android/acceptance.py capture --output $run --label returned-home
```

`capture` 只保存當前 PNG；檔名是人工標籤，不會宣告畫面真的符合該狀態。
本工具不傳送任意點擊／文字，也不讀取私有遊戲設定。需要 ADB 時，可指定 `--adb`／`--serial`；
使用其他 APK 才加 `--apk`，不要把不同 ABI 的結果混用。

## 讀取結果與收尾

| 檔案／狀態 | 意義 |
|---|---|
| `result.json` | 時間、PID 採樣、停止原因；有 `--install` 才包含本次安裝 APK 的單檔 SHA-256；沒有掃描媒體。 |
| `package.txt`／`device.txt` | 裝置與已安裝 package 的描述；沒有本次安裝證據時，不能用本機 APK 取代裝置版本。 |
| `logcat.txt` | 從本次收集開始保存 logcat，不先清除舊日誌；可能包含本機其他程序訊息，分享前檢查內容。 |
| `exit-before.txt`／`exit-after.txt` | Android 退出歷史；必須用本次 PID／時間配對，不能取任一舊 status 0。 |
| `screen-*.png`／人工 capture | 定期畫面、開局／結算／回首頁證據。 |
| `listeners-after.txt`／`pid-after.txt` | 收集結束時的狀態；崩潰也會釋放埠，所以不可獨立證明正常退出。 |
| `ended_reason=process exited`、程序 exit 0 | 只表示觀察到程序離開；崩潰同樣可能得到這個收集結果，還要人工核對退出資訊。 |
| `timeout`、中斷或錯誤、程序非零 | 保留現場，App 不會被 force-stop；完整局／正常退出不能列 PASS。 |

工具把 `game_over`／`clean_exit` 保持 `MANUAL_REVIEW_REQUIRED`；在同目錄另寫 `summary.md`，
分列建置、安裝、UI、實際模式／人數、結局／勝方、前後景、正常退出與尚未驗證項目。
如果收集時間到期但 App 仍運行，不能重新按快速加入或重裝；先判斷是否只是尚未結束。
需要繼續收尾觀察時用新目錄 `run --attach`，把兩個目錄串在報告中，不能抹掉原 timeout。
觀察到真實崩潰時停止重試，先保存堆疊；新增原生除錯範圍仍按 AGENTS.md 確認。

還原手動改過的模式／偏好。只關閉驗收程序擁有的模擬器，先正常退出 App，再執行：

```powershell
$adb = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
& $adb -s emulator-5586 shell sync
& $adb -s emulator-5586 emu kill
```

簽章不符時恢復既有固定金鑰，詳見 [固定開發簽名](android-build.md#固定開發簽名與覆蓋更新)，
不以 uninstall／`pm clear` 解決。若 APK 原檔正常而安裝副本損壞，保存第一份錯誤，
再以相同 APK 的 `--no-streaming -r` 與 `sync` 修復；不重匯整包素材。

## 2026-09-20 已有結果與界線

證據根：`builds/android-acceptance-20260920-135247/summary.md`（本機產物，不納入 Git）。

| 檢查點 | 結果 |
|---|---|
| socket／dashboard 修正版，x86_64、NULL audio、software/raster | 產品託管 05p 自然結局，第 10 輪，反賊勝；前後景同 PID；返回首頁及正常退出 status 0，埠釋放。 |
| dashboard 缺主視角圖 | 原因為整個 dashboard 排滿寬度後又以 1.25 倍放大；修正先保留縮放寬度，主將及操作鈕已在對局中可見。 |
| 返回主菜單 SIGSEGV | 批次收訊遇接收端銷毀 socket；加入生命週期守衛，五個 focused 案例及上述完整局通過。 |
| 最後移除 Android 標題列的 APK | 覆蓋安裝、首頁無標題及正常退出短驗證 PASS；該最終包未再跑完整局。 |
| 新增驗收助手 | Python AST、Markdown fence／本機連結及差異靜態檢查 PASS；新助手 ADB 實跑 NOT RUN。舊 monitor 的成功不代表新助手已在裝置跑過。 |

未覆蓋：實機／ARM64／音訊恢復、所有手動互動、選將窗過大、局部對手框裁切、缺字、空錄像匯出，
以及先前 QSettings 初始化失敗退出路徑。NULL audio 是當前診斷配置，不能宣稱音訊缺陷已修復。
