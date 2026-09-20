# Android 擴展實體目錄

2026-09-12；首版功能來源基線 `debug@9b7920c4469d42f40f8e4bcb66d15d8bcb6e727a`。

使用者指定沿用 `TODO/human` 的隨包部署方式：APK 附送 Lua／AI／擴展，缺檔才釋出，
保留已有擴展腳本。核心 Lua 基線隨 APK 升級，規則見下表。首次聲畫另用 ZIP 匯入，後續沿用已安裝媒體；個別圖片缺檔不擋局。

## 實體目錄與版本

| 項目 | 行為 |
|---|---|
| APK 來源 | `lua/`、`extensions/`、`lang/`、QML、皮膚設定、基本字型及翻譯，以 Qt resources 放在 `:/assets/`；外部擴展副本不納入主倉庫。 |
| 舊版釋出 | `<AppDataLocation>/runtime` 在首次安裝、APK 資源變更及失敗回復時只補缺檔，已有同名檔不覆寫。CP1 留下的使用者修改另保留於初始內容快照。 |
| 隨包原版 | `content/baseline` 從 APK 保存原版，與使用者修改分開；APK 新增檔及宣告以 revision 日誌追加。 |
| 核心 Lua 升級 | `lua/config.lua`、`lua/sanguosha.lua`、`lua/utilities.lua`、`lua/sgs_ex.lua`、`lua/lib/json.lua` 的 baseline 隨 APK revision 更新；bootstrap migration version 2 修復相同資源 revision 下的舊部署。成功後重組新快照，舊快照及已捕捉的使用者覆蓋保留，聲畫沿用原 blob。啟動失敗仍先走回復介面，不自動跳過。 |
| Engine 來源 | `content/versions/<id>/runtime` 保留完整相對路徑；Lua／規則是版本獨立實體檔，聲畫引用私有 blob。建立 Engine 前設定 runtime root，既有 `image/`／`audio/`／`font/` 消費端不變。 |
| 套用變更 | 完成解壓與版本組合後寫 pending；重新啟動才切換 active，上一 active 保留為 previous。不做資源雜湊或啟動聲畫全掃描。 |
| 未變動媒體 | Android／POSIX 快照使用受中繼資料約束的符號連結；完整媒體目錄直接引用同一 blob，混合來源目錄逐檔引用。禁止硬連結失敗後靜默複製整包聲畫。 |
| 使用者資料 | `<AppDataLocation>/userdata` 保存設定、紀錄及學習資料，與內容快照分開。 |
| 診斷路徑 | 既有明確 `--asset-root`／`QSAN_ASSET_ROOT` 保留優先權。 |

## Android 啟動政策（2026-09-16 使用者修訂）

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

以下效能紀錄保留歷史實測；其中串流 SHA／收據的描述已由上述政策取代。

最終修訂 APK 已增量建置、`install -r` 並實際執行：資源準備 **6,924 ms**，
`active_media_ready=true`，舊收據已移除，缺 8 張圖片不再擋住連線。
Android 連線 `03_1v2` 在開局前發生 AudioTrack SIGSEGV，沒有 GAME_OVER；
server 正常退出、連接埠釋放。未套用音訊替代方案或重試；證據在
`builds/android-content-performance-20260916/no-hash-run/summary.md`。

## 2026-09-16 匯入／更新效能修正（來源檢查點）

固定單一環境以 [Android 建置文件](android-build.md#本機唯一日常環境2026-09-16-起) 為準。
舊 APK 的完整媒體匯入約 17 分鐘，資源更新的 `prepareStartup` 實測 879,382 ms；
此數據只作修正前基線。新 APK 已建置，取得快照建立的局部實測；完整啟動仍逾時，
首次完整匯入未重測，詳見下方驗證紀錄，不能將局部時間當作總耗時。

| 瓶頸 | 新行為 |
|---|---|
| 每解一檔，線性比對全部 ZIP entries | 讀中央目錄時建立 offset 索引；查找不再隨檔數平方增長，重複 offset／遭修改的 entry 拒收 |
| 可 seek 的來源仍先複製整份 ZIP | 直接使用來源 descriptor；只有不可 seek 的 provider 使用受限私有 spool |
| 每檔寫完再開檔讀取 SHA-256 | 使用者後續要求移除：不計算／比對媒體 SHA-256 |
| 每檔遍歷相同父目錄、每檔同步磁碟 | 同一受鎖定操作內快取已核對的真實父目錄；Android 的全新 staging 檔整批 `syncfs` 後才發布 |
| 更新 Lua／APK 基線重複複製聲畫 | 新 snapshot 只建立媒體引用；完整媒體通常只需 image／audio／font 三個目錄連結，payload 寫入一次 |

符號連結只由 store 建立：完整根目錄必須全部來自同一已啟用完整媒體包；混合來源
使用精確逐檔引用。載入時核對 package version、固定 blob 位置、相對檔名及真實
來源父目錄；ZIP 自帶連結、Lua 連結、未宣告的目錄連結或越界指向仍拒收。
取消／ZIP 解壓錯誤不發布暫存目錄；SHA-256 不再檢查。Android API 28 起的 `syncfs` 在批次檔案
落盤後才允許 metadata／blob 發布，失敗回報錯誤，不改用不安全的直接覆寫。

舊實體快照可讀；Android 首次升級用現有 blob 重建引用，不清 App 資料、不要求重匯 ZIP。
active／pending／previous 及其引用 blob 沿用既有 GC 與回復規則；舊版實體副本按正常
版本保留週期淘汰，不手動清除。Windows fixture 保留實體複製；共享連結的完整契約
需要 POSIX fixture 驗證，本輪尚未執行；AVD 只確認實際目錄引用與快照建立。

最終回歸來源涵蓋：seek／串流 provider、索引、reader 重用、取消、忽略舊雜湊欄位、
移除舊收據、APK 更新後缺少個別圖片仍可沿用媒體、
Lua／baseline 更新與 rollback 共用同一媒體、混合媒體啟停、偽造連結及 GC 不追入引用目錄。
日誌分開記錄 archive／payload／snapshot 毫秒數，以及 snapshot 實際複製的 bytes。
本批只允許完成檢查點後的 targeted build／focused 與同一 AVD 資源路徑驗證，
不藉此重開完整對局或擴入 AudioTrack 原生崩潰修復。

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

## 驗證紀錄

2026-09-16 效能檢查點（證據：`builds/android-content-performance-20260916/`）：

| 項目 | 結果 |
|---|---|
| Windows 既有 host target 編譯／focused executable | PASS，20.01 秒；沒有執行 CTest |
| Android arm64 Debug APK | PASS；同一 CMake／Gradle cache，35 個 Gradle tasks 中 31 個 up-to-date |
| 同一 AVD `install -r` | PASS，14.11 秒；沒有重匯媒體或清 App 資料 |
| 舊媒體轉共享引用 | 實際新 runtime 的 image／audio／font 為 3 個符號連結，均指向原媒體 blob |
| 新快照建立 | 28,261 ms；實體複製 33,427,689 bytes 的規則／介面資源，媒體 payload 沒有複製 |
| 完整內容啟動 | TIMEOUT：60 秒內未完成；已停止 App，不能判定啟動／媒體完整性通過 |
| 首次完整 ZIP 匯入的新耗時 | NOT RUN；沒有為測速重匯 2.7 GB |
| POSIX 專用 fixture、完整對局、AudioTrack 修復 | NOT RUN；Windows fixture 不涵蓋 UNIX 分支 |

停止時 active 已切到新 snapshot，但 startup receipt 仍指向舊 active，表示完整啟動校驗
尚未提交；`active_media_ready` 的中途值不能當該次通過證據。這是舊設計的歷史紀錄，
後續版本已移除收據與聲畫掃描。主工作區當時新增 8 張圖片，APK inventory 比已匯入原包多 8 檔，
見 `media-inventory-drift.json`；這是獨立完整性差異，不得用重匯整包掩蓋效能結果。
既有 GC 仍同步執行，但本次停止時尚無證據確認卡在 GC；不把推測寫成根因。

完整契約及未執行矩陣見 [首版功能](android-first-release.md)。舊 APK 的 AudioTrack
SIGSEGV 與桌面 client access violation 保持獨立，這批效能修改不宣稱修復它們。
