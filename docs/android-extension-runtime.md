# Android 擴展實體目錄

沿用 `TODO/human` 的隨包部署方式：APK 附送 Lua／AI／擴展，缺檔才釋出，
保留已有擴展腳本。核心 Lua 基線隨 APK 升級，規則見下表。首次聲畫另用 ZIP 匯入，後續沿用已安裝媒體；個別圖片缺檔不擋局。

## 實體目錄與版本

| 項目 | 行為 |
|---|---|
| APK 來源 | `lua/`、`extensions/`、`lang/`、QML、皮膚設定、基本字型及翻譯，以 Qt resources 放在 `:/assets/`；外部擴展副本不納入主倉庫。 |
| 舊版釋出 | `<AppDataLocation>/runtime` 在首次安裝、APK 資源變更及失敗回復時只補缺檔，已有同名檔不覆寫。CP1 留下的使用者修改另保留於初始內容快照。 |
| 隨包原版 | `content/baseline` 從 APK 保存原版，與使用者修改分開；APK 新增檔及宣告以 revision 日誌追加。 |
| 核心 Lua 升級 | `lua/config.lua`、`lua/sanguosha.lua`、`lua/utilities.lua`、`lua/sgs_ex.lua`、`lua/lib/json.lua`、`lua/ai/mode-ai.lua` 的 baseline 隨 APK revision 更新。bootstrap migration version 3 會換上含 `registerStandardModeAI` 的 mode AI；version 2 修復相同資源 revision 下的舊部署。成功後重組新快照，舊快照及已捕捉的使用者覆蓋保留，聲畫沿用原 blob。啟動失敗仍先走回復介面，不自動跳過。 |
| Engine 來源 | `content/versions/<id>/runtime` 保留完整相對路徑；Lua／規則是版本獨立實體檔，聲畫引用私有 blob。建立 Engine 前設定 runtime root，既有 `image/`／`audio/`／`font/` 消費端不變。 |
| 套用變更 | 完成解壓與版本組合後寫 pending；重新啟動才切換 active，上一 active 保留為 previous。不做資源雜湊或啟動聲畫全掃描。 |
| 未變動媒體 | Android／POSIX 快照使用受中繼資料約束的符號連結；完整媒體目錄直接引用同一 blob，混合來源目錄逐檔引用。禁止硬連結失敗後靜默複製整包聲畫。 |
| 使用者資料 | `<AppDataLocation>/userdata` 保存設定、紀錄及學習資料，與內容快照分開。 |
| 診斷路徑 | 既有明確 `--asset-root`／`QSAN_ASSET_ROOT` 保留優先權。 |

## Android 啟動政策（使用者修訂）

**Android 不再雜湊資源、不逐檔掃描聲畫，也不因圖片缺檔擋住開局。**

| 情境 | 現行處理 |
|---|---|
| 資源匯入 | 正常解壓與路徑／大小處理；不計算或比對 SHA-256，舊 manifest 的 hash 欄位不參與判定。 |
| 已安裝資源的日常啟動 | 讀取版本日誌與實際必需的規則設定，直接沿用媒體；不逐檔掃描、雜湊或核對 APK 媒體清單。 |
| APK 新增圖片 | 不因缺少新增圖片禁止進入首頁／開局，不要求重匯完整媒體包。 |
| Lua／UI 更新 | 依 APK 內建 revision 識別是否需要補缺及切換版本；不在裝置上計算資源 hash，聲畫沿用同一 blob。 |
| 舊 startup_validation | 移除舊收據；錯誤／缺失收據不再觸發資源全掃描。 |
| 一次性舊目錄遷移 | 直接比較檔案內容以辨識需保留的使用者修改，不使用雜湊；日常啟動不執行此比較。 |

ZIP 解壓器的 CRC／格式解析仍屬檔案讀取。私有目錄連結只做少量目標路徑檢查，
不讀取媒體 payload。首次尚未安裝媒體仍顯示匯入入口；已安裝媒體缺個別圖片不再擋局。
Web／網路規則身分與聲畫資源是不同契約，本次只移除 Android 資源校驗。

修訂後的 APK 已移除舊收據：資源準備不計算媒體雜湊，缺個別圖片不再擋住連線。
先前 ARM translation 的有聲連線在開局前發生 AudioTrack SIGSEGV，沒有 GAME_OVER；
當時 server 正常退出、連接埠釋放，未重試。2026-10-03 的 ARMv7／LDPlayer 單機
`03_1v2` 以 `NULL` 音訊完成自然結局與正常退出；這不代表有聲路徑已修復。
環境、操作與證據見 [ARMv7 操作與問題處理](android-build.md#android-armv7-reuse)。

<a id="android-boot-attempt-recovery"></a>
### 啟動未完成回復：不要把按鈕文字當作媒體缺失

2026-10-03 雷電14／Android14 啟動時，內容準備已結束，`content/state.json` 同時有
`active_media_ready=true` 與 `boot_attempt=true`；畫面顯示「加入完整聲畫 ZIP」按鈕。
後者是資源管理／回復介面的既有入口，不能單憑文字判定缺少 ZIP，也不能因而重新匯入完整媒體。
media-ready 標記僅表示管理器狀態，並不證明全部媒體 payload 或有聲播放已驗收。

`boot_attempt` 是上次內容啟動未完成的保護標記；App／VM 中途退出可能留下它，原因需另查。
建立 Engine 與 QA controller 前會先執行內容準備及回復；`--asset-root` 不會跳過這個啟動入口。
先確認序號確實是指定 VM、App 是否處於前景及本輪退出／VM 日誌，保留首次錯誤。
本次 VBox 只證明 host 發出 `powerDown`，未定位 caller，不能宣稱是遊戲原生崩潰。

若使用者選擇只重試同一內容，**移除 `boot_attempt` 是一次有界的人工回復例外，需有該次授權**；
不作為日常啟動步驟，不改「失敗先進回復介面」政策。本次已經取得使用者同意並成功執行：

1. App 不在運行時，以指定序號的 `run-as org.qsanguosha.game` 讀取 `files/content/state.json`。
   保存原檔及提案，只移除 `boot_attempt`，保留其他所有欄位。
2. 提案具體列出 active、previous、媒體狀態均不變，未切回版本、未停用擴展、未重匯 ZIP、未清資料。
3. 套用前重新讀取狀態，須與已核准提案的原始狀態完全相符；若有變動就停止，不覆寫新狀態。
4. 以 App 自身 UID、私有暫存檔及同目錄原子替換寫入；不用 root，不手動刪 baseline／blobs／versions。
5. 讀回 JSON，確認差異只有該標記，保存 before／after；然後正常啟動一次。
6. 正常啟動可能依既有流程重新合成 active snapshot，這不代表手動回復時改寫了 active。
   保存啟動後狀態，不強行恢復舊 UUID 或另外固定舊 runtime。
7. 同一阻擋或 VM offline 再發生，保存證據並停止；新的引擎修復、清資料、下載或反覆完整局須另界定範圍。

具體批准差異、before／after 及正常 05P 結果見
[本次回復與驗收報告](../builds/android-10p-20261003-011a/summary.md)。可重用的裝置／連線／
完整局流程見 [雷電14 操作](android-build.md#android-14-ldplayer-reuse)。本次保留聲畫與擴展，
自然完成 05P 並正常退出；不外推為有聲或手動 GUI 驗收。

## 匯入／更新效能修正

固定單一環境以 [Android 建置文件](android-build.md#android-daily-environment) 為準。
先前 AVD 的完整啟動曾逾時，首次完整匯入的新耗時未在該環境重測，不能將局部時間
當作總耗時；下節另記 ARMv7／LDPlayer 的大檔完整匯入結果，不混用兩個環境的時間。

| 瓶頸 | 新行為 |
|---|---|
| 每解一檔，線性比對全部 ZIP entries | 讀中央目錄時建立 offset 索引；查找不再隨檔數平方增長，重複 offset／遭修改的 entry 拒收 |
| 可 seek 的來源仍先複製整份 ZIP | 直接使用來源 descriptor；只有不可 seek 的 provider 使用受限私有 spool |
| 每檔寫完再開檔讀取 SHA-256 | 現行政策移除：不計算／比對媒體 SHA-256 |
| 每檔遍歷相同父目錄、每檔同步磁碟 | 同一受鎖定操作內快取已核對的真實父目錄；Android 的全新 staging 檔整批 `syncfs` 後才發布 |
| 更新 Lua／APK 基線重複複製聲畫 | 新 snapshot 只建立媒體引用；完整媒體通常只需 image／audio／font 三個目錄連結，payload 寫入一次 |

32 位元 Android 的 ZIP reader 對超過 2 GiB 的普通檔案使用 `fstat64`／`pread64`，
避免已編譯 Qt runtime 的 32 位元檔案位移截斷。來源與私有 spool 均使用此讀取路徑，
descriptor 仍由原 QFile 持有；其他 ABI、小檔案及串流 provider 沿用原流程。
匯入大小限制、路徑／中央目錄／CRC 驗證與取消流程維持相同規則。

符號連結只由 store 建立：完整根目錄必須全部來自同一已啟用完整媒體包；混合來源
使用精確逐檔引用。載入時核對 package version、固定 blob 位置、相對檔名及真實
來源父目錄；ZIP 自帶連結、Lua 連結、未宣告的目錄連結或越界指向仍拒收。
取消／ZIP 解壓錯誤不發布暫存目錄；SHA-256 不再檢查。Android API 28 起的 `syncfs` 在批次檔案
落盤後才允許 metadata／blob 發布，失敗回報錯誤，不改用不安全的直接覆寫。

舊實體快照可讀；Android 首次升級用現有 blob 重建引用，不清 App 資料、不要求重匯 ZIP。
active／pending／previous 及其引用 blob 沿用既有 GC 與回復規則；舊版實體副本按正常
版本保留週期淘汰，不手動清除。Windows fixture 保留實體複製；共享連結的完整契約
需要 POSIX fixture 驗證。AVD 只確認實際目錄引用與快照建立。

回歸來源涵蓋：seek／串流 provider、索引、reader 重用、取消、忽略舊雜湊欄位、
移除舊收據、APK 更新後缺少個別圖片仍可沿用媒體、
Lua／baseline 更新與 rollback 共用同一媒體、混合媒體啟停、偽造連結及 GC 不追入引用目錄。
日誌分開記錄 archive／payload／snapshot 毫秒數，以及 snapshot 實際複製的 bytes。

<a id="android-32bit-zip-footer"></a>
### ARMv7 大檔 ZIP footer 失敗

2026-10-03 使用完整 2,754,338,324-byte 媒體 ZIP，首次 SAF 匯入在 footer 階段回報
`cannot read ZIP footer`。實際 Qt ARMv7 runtime 使用 `lseek`／`fseek`／`fstat`；
已編譯函式庫的 32 位元檔案位移無法靠 App 加編譯巨集補救。
Android 的 32 位元 ABI 限制見 [Android 官方說明](https://android.googlesource.com/platform/bionic/%2Bshow/master/docs/32-bit-abi.md)。

修正位於 `src/core/android-zip-reader.cpp/.h`：只在 Android／32 位元、普通檔案且大於
2 GiB 時使用借用 descriptor 的 `QIODevice`，由 `fstat64` 取得長度、`pread64` 以獨立
64 位元游標讀取；不接管 descriptor，也不改變原 QFile 的游標。直接來源與私有 spool
均套用；其他 ABI、小檔案、不可 seek provider 的既有流程及全部 ZIP 邊界檢查保留。
修正後增量建置、同憑證覆蓋更新，成功匯入同一完整 ZIP 並重啟開局；沒有清 App 資料、
換媒體包或修改第三方 Qt。第一份失敗與修正後證據見
[本次報告](../builds/android-armv7-10p-20261003/summary.md)。

| 下次看到的現象 | 判斷順序 |
|---|---|
| 32 位元、大於 2 GiB、footer 讀取失敗 | 先確認待測 APK 是否含此 reader 修正及實際 ABI／來源長度，保留錯誤；不要先推定 ZIP 壞掉、重打包或重建工具鏈 |
| 小檔案、其他 ABI 或其他格式錯誤 | 不直接套用本次根因；沿原中央目錄／CRC／路徑／大小錯誤分類，以現有證據判斷 |
| 100% 後仍在準備 | 解壓百分比不等於 active 已切換；等待完整暫存／發布成功再重啟，不能反覆匯入 |
| 已完成匯入，只更新 APK | 保留 App 媒體，沿用 `install -r`；不重匯、不掃描或雜湊聲畫 |

## 整包管理

| 操作 | 首版行為 |
|---|---|
| 匯入單一 Lua | 放入 `extensions/`；以輸入的整包名稱管理。 |
| 匯入 ZIP | 保留 extensions、對應 Lua／AI、翻譯及聲畫目錄，拒絕核心設定覆蓋、穿越及碰撞。 |
| 替換 | 同名整包保留位置；隨包原版保留供還原。 |
| 停用 | 整包的規則、AI、翻譯及媒體均不進入有效 runtime。 |
| 移除 | 使用者覆蓋包還原隨包原版；隨包項目移除等同停用。 |
| 順序 | 原有順序固定，新項目預設按名稱排序，可調整並匯出。 |
| APK 升級衝突 | 保留舊內容並進入管理／回復介面；可調整衝突或一次還原隨包擴展並保留媒體。 |
| 啟動失敗 | 下次先進回復介面，可停用本次匯入或切回上一版本。 |

可信 Lua 保留完整 `io`、`os`、`package` 能力；匯入介面說明程式碼權限，ZIP 檢查不等同沙箱。

有效 `runtime-content.json` 使用 schema 2／`declared-v2`，在載入 Lua 前合併宣告，不改寫
隨包 `config.lua`。規則宣告順序、依賴與 Lua 內容參與身份校驗；AI、翻譯及媒體保留角色界線。
原生、Web／WASM 及伺服器均同步此身份，Protocol V2 不變。

## 已知限制

共享連結的完整契約仍需 POSIX 專用 fixture 驗證，Windows fixture 不涵蓋 UNIX 分支；
有聲完整對局與 AudioTrack 修復未涵蓋在本路徑；已通過的 ARMv7／LDPlayer 靜音單機局
另見上方操作與證據。既有 GC 仍同步執行。

舊 APK 的 AudioTrack
SIGSEGV 與桌面 client access violation 保持獨立；本頁的效能修改不宣稱修復它們。
