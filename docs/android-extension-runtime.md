# Android 擴展實體目錄

2026-09-12；首版功能來源基線 `debug@9b7920c4469d42f40f8e4bcb66d15d8bcb6e727a`。

使用者指定沿用 `TODO/human` 的隨包部署方式：APK 附送 Lua／AI／擴展，缺檔才釋出，
保留已有腳本。完整聲畫另用 ZIP 匯入；所有遊戲入口須等待完整匯入成功。

## 實體目錄與版本

| 項目 | 行為 |
|---|---|
| APK 來源 | `lua/`、`extensions/`、`lang/`、QML、皮膚設定、基本字型及翻譯，以 Qt resources 放在 `:/assets/`；外部擴展副本不納入主倉庫。 |
| 舊版釋出 | `<AppDataLocation>/runtime` 在首次安裝、APK 資源變更及失敗回復時只補缺檔，已有同名檔不覆寫。CP1 留下的使用者修改另保留於初始內容快照。 |
| 隨包原版 | `content/baseline` 從 APK 保存原版，與使用者修改分開；APK 新增檔及宣告以 revision 日誌追加。 |
| Engine 來源 | `content/versions/<id>/runtime` 是包含核心、有效擴展及媒體的完整實體目錄；建立 Engine 前設定 runtime root。 |
| 套用變更 | 暫存版本完整檢查後寫 pending；重新啟動才切換 active，上一 active 保留為 previous。 |
| 未變動媒體 | 同檔案系統使用 hardlink，無法建立才複製；更新使用新檔替換，不改寫共用 inode。 |
| 使用者資料 | `<AppDataLocation>/userdata` 保存設定、紀錄及學習資料，與內容快照分開。 |
| 診斷路徑 | 既有明確 `--asset-root`／`QSAN_ASSET_ROOT` 保留優先權。 |

## 未變更啟動的快速路徑

建置時由實際 APK 資源名稱與內容、規則宣告及獨立媒體清單產生 SHA-256 revision；
啟動只讀取 `:/android-content-revision.txt`，不在裝置上重新雜湊整套 APK。

| 情境 | 檢查範圍 |
|---|---|
| 相同 APK／baseline／active，沒有 pending 或回復狀態 | 核對已驗證收據、中繼資料與宣告的 SHA-256、少量核心檔案；略過 APK 遍歷、baseline 逐檔檢查及完整媒體清單／大小檢查。 |
| 首次升級至此版本、缺少／失效收據、中繼資料或宣告改變 | 完整檢查 active；成功後原子寫入新收據。同一次啟動不重複驗證同一版本。 |
| APK revision 改變、缺失或不合法 | 重走隨包補缺／baseline 流程及完整驗證；舊 APK 沒有 revision 仍可運作。 |
| 匯入、整包變更、pending、失敗回復 | 保留原有完整檢查與版本切換；管理變更使舊收據失效。 |

快速路徑以私有內容透過匯入／管理操作變更為前提，不會每次偵測帶外刪改的所有媒體
或腳本；SHA-256 收據校驗的是中繼資料與宣告，並非每個 payload。失敗回復會重新檢查。
首次安裝、匯入及內容版本重建仍需處理全部相關檔案。啟動日誌的
`Android content startup: baseline=... elapsed_ms=...` 僅量測內容準備，不含 Engine／首頁載入。

2026-09-12 效能修改為來源檢查點，尚未建置新 APK 或取得同裝置前後啟動秒數。

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

本批是來源實作，尚未重新建置或執行測試。ZIP／整包／回復回歸案例已加入 Linux focused
來源，未在本地執行 CTest。完整契約及未執行矩陣見 [首版功能](android-first-release.md)；
先前 CP1 APK 的獨立建置證據見 [Android 建置](android-build.md)，不能代替本批功能驗收。
