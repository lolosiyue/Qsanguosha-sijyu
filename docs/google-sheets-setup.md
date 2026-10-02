# Google Sheets 安裝與操作

架構與資料契約見[設計文件](google-sheets-client.md)。
再次使用前先看[已知問題與下次沿用流程](#已知問題與下次沿用流程)，沿用已完成的腳本與工作表更新。

## 房間布局與共用戰報

### 簡體中文介面與舊表升級

固定文案集中在 `Locale.gs`，工作表分頁顯示為「QSAN 操作／牌桌／房间／目录／详情／战报」。
程式仍使用既有英文分頁識別碼，查詢與回覆不使用翻譯後的名稱作協定值。
更新五個 Apps Script 來源（包含 `Locale.gs`）後，執行一次 `setupWorkbook` 或選單
「三国杀 → 创建专用工作表」即可在既有專用工作表套用新標籤；連線設定、聊天、
模式、識別碼與互動草稿保留，無須重新開局。

| 顯示 | 原生值 |
|---|---|
| 选项／卡牌／玩家／技能 | option／card／player／skill |
| 是／否 | true／false |
| 牌堆顶／牌堆底 | top／bottom |
| 主公／忠臣／反贼／内奸 | lord／loyalist／rebel／renegade |

儲存格顯示上述詞語的簡體版本，草稿解析亦相容舊英文值。勾選欄仍使用真正的布林核取方塊。
武將、技能、卡牌與規則正文沿用原生 `zh_CN` 翻譯；橋接程式更新後會分開翻譯 `(lord)` 標記，
缺少說明時保持空白，避免把 `:查找鍵` 當成技能描述。玩家名稱、作者名稱與識別碼屬內容資料。

`QSAN Actions` 是房間畫面，保留現有 D–F 欄選擇契約；更新原有 Apps Script
檔案後，首次更新牌桌會轉換這張專用工作表。`QSAN Room` 仍用於連線設定。

| 區域 | 內容 |
|---|---|
| 外圍座位 | 本人在下方，其餘按座位順序環型排列；保留陣亡席位，顯示武將、體力、手牌數、身分、階段、裝備與判定區 |
| 中央 | 僅呈現 PlaceTable 處理中的牌、現時行動／response prompt、預檢結果 |
| 本人區域 | 可見手牌的名稱／花色／點數及技能摘要；下方為原生提供的可操作候選清單 |
| 右側 | 最新戰報在上；完整保留的最近 200 條仍見 QSAN Log |
| 選擇 | D 欄勾選，E 欄指定順序，F 欄以簡體中文選擇觀星位置／身份；重排布局時保留同一互動的草稿，換互動則重新產生候選 |
| 配色 | 座位標題：藍＝自己、橙＝行動中、灰＝陣亡；候選列以條件格式標示已勾選（淺藍底）與不可用（灰字）；連線狀態以中文顯示 |

戰報直接共用由 TUI 抽出的 `client-log-formatter` 文本路徑，包含武將名、牌面、
技能及遊戲事件；略過動畫／表情／技能氣泡事件。橋接亦接上 TUI 原有的移牌、
體力與仁區補充戰報。階段與互動名稱使用 `TUICommon.lua`，不另建 Sheets 翻譯表。
缺失的上游翻譯仍按 TUI 的 fallback 顯示，缺少的擴展詞條使用原鍵名。

### 說明、圖片與座位詳情

| 功能 | 操作／來源 |
|---|---|
| 完整說明 | 卡牌／技能讀原生 `description`，相容既有 `detail`；選將與武將排序讀武將完整技能描述，不查 `:武將ID` |
| 圖片 | 清單保留 D–F 操作欄，原 K 欄留空；素材識別碼不寫入可見資料。按詳情後從已授權回覆載入圖片至側欄 |
| 座位詳情 | 在 QSAN Actions 選取一個座位的標題或內容，再按側欄「查詢詳情」；結果與圖片直接出現在側欄，完整文字亦寫入 QSAN Details |
| 標記 | 「詳情可查」可由該座位直接查閱完整標記；標記鍵名維持原樣，沒有新增翻譯 |

座位查詢使用畫面對應的玩家 ID，包含陣亡席位；選到中央處理區、空白或跨越
多個區域時會提示重新選取。查詢仍需要有效會話；已關閉對局只能查看先前保存的詳情。
側欄保留最近一次主動查詢的內容，自動輪詢不會切換正在閱讀的詳情。
缺少原生描述時保持空白，不捏造規則文字。已關閉會話的舊牌桌資料會在下次連線刷新
時套用新版。

布局使用既有 Apps Script Spreadsheet 服務的
[Range 合併／寫入](https://developers.google.com/apps-script/reference/spreadsheet/range)與
[Sheet 欄寬／列高設定](https://developers.google.com/apps-script/reference/spreadsheet/sheet)。

## 組成

| 元件 | 職責 |
|---|---|
| `apps-script/Locale.gs` | 全部固定 Sheets 文案（含側邊欄）的穩定英文鍵與大陸簡中表；`qsanFormat_` 處理 `%1` 佔位；遊戲文本來自原生不在此翻譯 |
| `apps-script/Client.gs` | 玩家憑證、HTTPS 請求、指令恢復與操作入口 |
| `apps-script/Table.gs`／`Draft.gs` | 工作表呈現、排序選擇與七種標準回覆結構 |
| `apps-script/Sidebar.html` | HtmlService 模板；分區（連接／房間／對局操作／查詢／更新）的配對、更新與連線控制；卡牌與目標在工作表選取 |
| `gateway.py` | 一次性配對、每位玩家獨立路由、已授權圖片與有界程序清理 |
| `src/sheets/sheets-main.cpp` | 以私有 stdin 監護原生 ClientCore／ExcelBridge |
| `QSanguoshaExcelServer.exe` | 共用的原生 C++／Lua／AI 遊戲 helper |

不在 Apps Script 重寫技能規則。28 類互動沿用原生預檢，QML 自訂互動仍明確
不支援。全擴展按引擎與房間設定載入，不另設 Sheets 相容性白名單。
每位玩家必須使用自己的文件；自己的手牌會出現在該文件，勿把文件分享給對手。

## 主機準備

以下是目前 Windows x64／Qt6 建置方式。
現階段只支援 Windows x64／Qt6；`QSAN_BUILD_SHEETS` 預設為 OFF，依賴
`QSAN_BUILD_EXCEL=ON`。新 target 和既有 helper 放在相同的 `excel-debug`／
`excel-release`，不啟動或依賴 Excel／Office。

```powershell
cmake --preset vs2026-x64 -DQSAN_BUILD_EXCEL=ON -DQSAN_BUILD_SHEETS=ON
cmake --build --preset debug --target qsanguosha_sheets_bridge --parallel 8
powershell -NoProfile -ExecutionPolicy Bypass -File google-sheets/deploy-runtime.ps1 -Configuration Debug
```

使用 Python 3.11+。原生程式需要與目前來源相符的 Qt runtime、Lua、擴展、AI、
翻譯與素材。`deploy-runtime.ps1` 使用同版 windeployqt 部署 bridge／helper 的 Qt
依賴及已配置的 FMOD DLL；本次已確認不含 Qt PATH 仍可啟動 bridge。
`deploy-server` 不能代替 Qt DLL 部署。
Excel runtime 素材可重用，但 Excel 測試結果不替代 Sheets 驗證。

```powershell
# 在倉庫根目錄執行；asset-root 必須是已準備的宣告內容執行期。
python google-sheets/prepare-runtime.py --asset-root . --destination builds/sheets-runtime
python google-sheets/gateway.py --bridge excel-debug/QSanguoshaSheetsBridge.exe --asset-root builds/sheets-runtime --state-root builds --port 8766
```

`--state-root` 必須是已存在的目錄。Gateway 在其下建立新的 UUID 目錄並收緊
Windows ACL；每個玩家再有自己的 config/data/log，原生 token 只經 stdin
傳遞，不放命令列或 ready file。請勿把主機 console 中顯示的配對碼貼進公開日誌。
初次載入全部擴展可能需要時間。Gateway 的 `--startup-timeout` 預設 180 秒，
用於等待 bridge 啟動；開房 helper 的 `LocalServerController` 另有固定 180 秒
Initializing 期限，改前者不會延長後者。這些產品限制不代表代理已獲長時間驗證許可。

開發工作樹可能含未宣告的 Lua 暫存檔，不能直接用作規則身分驗證的執行期。
`prepare-runtime.py` 沿用既有 `tools/package-web-solo.py` 準備完整宣告內容，
再補齊 AI／圖片，目的目錄須不存在或為空；原始 config bytes 與全部配置擴展均保留。
本次實際配置為 102 個擴展、117 個翻譯、165 個 AI Lua 與 18,233 張圖片。
原生匯出確認 312 個 package、1,195 張卡與 225 個規則內容檔。
這項部署整理不修改 config、不刪除外部工作檔，也不放寬原生身分驗證。

Gateway 僅監聽 `127.0.0.1`。將既有的、由主機管理者控制的 HTTPS 通道指向
`http://127.0.0.1:8766`；不要把原生 bridge 或 HTTP listener 直接公開到網際網路。
HTTPS 入口須保留 Authorization、X-QSan-Session 及 JSON body，不快取私人回覆，
並有合理的請求大小與連線限制。此套件不自動建立隧道、公開網址或修改防火牆。

Apps Script 的 [URL Fetch](https://developers.google.com/apps-script/reference/url-fetch/url-fetch-app)
請求出自 Google 網路，因此不能用玩家電腦的 localhost 作為 Sheets 服務網址。

## Sheets 安裝

本次已安裝到使用者指定的 SGS 文件；下列步驟供其他文件安裝使用。
在自己的 Google Sheets 中開啟「擴充功能 → Apps Script」，加入以下來源：

| Apps Script 檔案 | 倉庫來源 |
|---|---|
| `Locale.gs` | `apps-script/Locale.gs`；固定 Sheets 文案鍵值表 |
| `Client.gs` | `apps-script/Client.gs` |
| `Table.gs` | `apps-script/Table.gs` |
| `Draft.gs` | `apps-script/Draft.gs` |
| `Sidebar.html` | `apps-script/Sidebar.html` |
| `appsscript.json` | `apps-script/appsscript.json`；於專案設定啟用顯示資訊清單 |

儲存並重新開啟 Sheets，使用「三国杀」選單建立專用工作表及開啟連線控制。
完成 Google 首次授權，輸入 HTTPS 服務網址與主機顯示的一次性配對碼。
配對碼預設有效 5 分鐘。網址變更或複製文件後重新配對，不把憑證填入儲存格。
安裝不需要把 Apps Script 發佈為公開 Web App，也不需要建立新的遊戲帳號。

舊版試作的 `Code.gs` 不與這四個 `.gs` 並存；新專案預設的空白 `Code.gs`
可刪除或保持完全空白。專用工作表以 developer metadata 識別，遇到別人建立的
同名頁籤會拒絕覆寫，請自行改名後再建立。既有專用房間設定不會在更新時重設。

| 操作 | 儲存格方式 |
|---|---|
| 開房／加入／聊天 | `QSAN Room` 填玩家、頭像、主機、埠、AI 人數與聊天文字，再按側邊欄 |
| 選牌／選項／技能 | `QSAN Actions` 的 D 欄勾選；技能先按預檢，以取得宣告或後續候選 |
| 目標次序／重複目標 | E 欄填 `1`、`2`；同一目標可填 `1,3`，保留重複與順序 |
| 觀星 | E 欄填順序，F 欄選「牌堆顶」／「牌堆底」；完整提交所有牌的分區 |
| 遺計 | 勾選一組牌與一名接收者，按預檢／提交；依原生互動繼續後續分配 |
| 角色分配 | F 欄選原生提供的身份中文名稱；提交時轉回原角色 ID |
| 武將排列 | 勾選武將，E 欄填排列順序 |
| 詳情／圖片 | 在牌桌、候選或目錄選取一列，再查詢詳情；文字在 Details，圖片在側邊欄 |
| 網路回覆遺失 | 用「重試待確認指令」恢復原指令；不編新序號重送 |

目錄由 QSanGuosha 選單或側邊欄載入，房間設定按原生型別填寫。複合物件設定
標示「保留」並沿用原生預設；不讓玩家在儲存格輸入任意 JSON。圖片按需載入，
不使用需要公開私人圖片 URL 的 `IMAGE()` 公式。

首個驗收目標為 `05p`、一名真人加四名 AI，使用 `OperationNoLimit=true`。
Apps Script 採短請求及批次更新，背景／關閉側邊欄不視為立即離開；
Google [執行與服務配額](https://developers.google.com/apps-script/guides/services/quotas)
仍會限制更新頻率，不承諾固定即時延遲。

## 多玩家及清理

`--players N` 預先建立 N 個隔離的玩家 bridge（目前上限 8，每個有完整原生規則
執行期，因此須評估主機記憶體），每個配對碼只綁一位玩家。這個值是同時開放的
Sheets 玩家數，不是遊戲房間人數限制。
多人加入由主機預先設定的遊戲伺服器，使用重複的 `--allow-game HOST:PORT`
明確允許目的地；未設定時只允許 `127.0.0.1:9527`。配對權限不允許任意網路
連線或公開新 listener。私有 AI 房由玩家的 bridge 擁有並清理；加入既有多人
伺服器時，離開不會停止別人的伺服器。

開局後沒有真人操作倒數；服務失聯則獨立使用 `--idle-timeout`，預設 30 分鐘。
超過重連窗口或未兌換配對碼逾期，gateway 清理該 bridge。意外失去 gateway 的
stdin，原生 bridge 會停止並以失敗退出碼回報。明確離開使用 `/v1/shutdown`，
可重試讀回同一份清理結果；強制終止不算成功。每個 slot 關閉後不重用其憑證，
需要新一輪遊玩時重新啟動 gateway。

`exit.json` 記錄 gateway 管理的 bridge exit code、是否強制停止及清理錯誤；
`native.log` 為私人診斷。子 helper 的正常退出需另有證據，不能以 bridge exit=0
代替。清理驗收同時核對 helper、埠與 GAME_OVER。
程式不刪除整個 runtime 或診斷目錄；刪除歷史診斷應由主機管理者決定。

## 已知問題與下次沿用流程

### 2026-10-03 保存的狀態

| 項目 | 已知結果 | 下次起點 |
|---|---|---|
| 五檔 Apps Script 與既有 SGS 工作表 | 已線上更新；重新載入後逐檔回讀一致；固定文案、分頁、側欄與舊表標籤已簡體化，原有設定保留 | 先比對目前版本；一致且顯示正確時沿用，不重新貼檔或建置 |
| 武將 `(lord)` 名稱、描述與圖片查找 | `src/excel/excel-view.cpp` 已分離標記與武將翻譯鍵，原選項 ID 保留；本次未建置／部署此修正 | 仍需使用者許可的 bridge/helper 增量建置與部署，不能聲稱已生效 |
| Sheets 10P | 開房得到 `startup_timeout`，未進入選將、未送準備；完整局未完成 | 先處理已記錄的 helper 初始化期限；目前只准查原因，不自行修改或重跑 |
| 關閉與回收 | bridge exit=0，gateway／通道已停止，本輪程序及埠已回收；子 helper 正常退出未有獨立證據 | 保留這個驗收缺口；重新遊玩需要新會話與配對，不能重用已關閉的憑證 |

證據保存在倉庫根目錄下：

- `builds/google-sheets-qa/localization-zh-CN-20261003/`：`result.json`、`online-*` 五檔回讀、Room／Actions 前後 TSV、`sheets-zh-CN.jpg`。
- `builds/google-sheets-qa/acceptance-10p-20261003-001836/summary.md`：建置、開房失敗、只讀原因核對、退出與回收的分項結論；詳細日誌在同目錄。

`builds/` 是本機證據，不進版控；跨工作區應先確認它是否仍存在。舊 PASS 只適用當時的產物，來源或執行期改變後須重新判定受影響的檢查點。

### 症狀、原因與已採用的處理

| 症狀 | 本次查明的原因／限制 | 下次處理 |
|---|---|---|
| 本機文案已轉簡，線上仍顯示繁體 | 線上四個舊檔未更新，而且缺少 `Locale.gs` | 更新四個 `.gs` 與 `Sidebar.html` 共五檔；不只貼主腳本；資訊清單未變時不另改權限 |
| 點編輯器後 Ctrl+A／貼上無效果 | Monaco 的可存取 textarea 只有極小高度；點擊未取得輸入焦點，Ctrl+A 選到整個網頁 | 對已觀察到的編輯器 textbox 使用 locator `press('Control+a')` 取得焦點，再貼上；回讀確認真正的檔案內容 |
| 雲端圖示顯示已儲存，但來源不同 | 圖示只能證明目前內容已儲存，不能證明貼入成功；檔案切換與編輯器載入亦需核對 | 儲存後重新載入專案，逐檔 Ctrl+A／Ctrl+C 回讀；只正規化 CRLF/LF 後與本機比較，不忽略其他差異 |
| 舊表仍有 `option`、`true`、`top/bottom` 或繁體提示 | 更新腳本不會自動重寫所有既有儲存格；固定牌桌、手牌、預檢及右側戰報亦會保留舊字 | 更新後執行一次 `setupWorkbook`；它以 owner metadata 升級舊表，不重設設定與草稿。新版本仍由既有 renderer 更新 |
| 枚舉改成中文，舊驗證／解析仍讀英文（靜態發現的風險） | 顯示枚舉與協定值不能混用；舊驗證清單仍要求英文值；本次未以實局候選驗收 | 先清除受影響的舊驗證，再寫中文值並重設驗證；`Draft.gs` 把項目類型、身份、牌堆位置轉回原 ID，D 欄保持真正的布林核取方塊 |
| 說明顯示 `:lookup_key`，主公武將顯示 raw ID | 缺失描述不應當成正文；`(lord)` 被一起拿去查武將翻譯鍵 | 描述沿用原生 `description`／`detail`，缺失保持空白；主公標記在原生呈現層分開翻譯。不要另造遊戲翻譯表或修改回覆 ID |
| Chrome 重新連接後，既有分頁控制仍逾時 | 重新連接未必能恢復同一分頁；本次同一 Chrome 瀏覽器中新開相同 URL 的分頁可操作 | 先取得最新分頁清單；確認目標後重取 handle。仍失敗時在同一已選瀏覽器開一個目標 URL 分頁；不要沿用舊 tab ID 或反覆盲試 |
| 側欄 DOM 有中文，但截圖一度空白 | DOM 回讀不代表 iframe 已可見；本次完整視口截圖最終確認內容 | 等待可見載入結果後核對完整視口畫面；保留可辨認的截圖，不拿空白或工具內預覽充當完成證據 |
| 開房約 191 秒後逾時，Room worker 本身約 126 秒 | helper 的固定 180 秒期限包含前段初始化、規則／AI、交接、發布房間與 ready；progress 不刷新期限，到 Stopping 後不再接受 ready | 先看已保存的時間序列與狀態機；別只延長 gateway timeout、別直接歸因 Cloudflare、也別據此猜某個擴展是慢點 |

### 再次更新腳本與工作表

1. 先讀本節與 `memory/learned/web-client.md` 的 Sheets 條目，核對現有線上檔案及工作表。已匹配的檔案不用重新貼；本輪不需要原生變更時，不啟動建置、gateway、通道或對局。
2. 若需更新，先保存 `QSAN 房间` 的玩家、頭像、伺服器、埠、人數、聊天與房間設定；保存 `QSAN 操作` 的候選及 D–F 草稿。舊版分頁為 `QSAN Room`／`QSAN Actions`。保留公式、識別碼及已有資料，不以重建或清空全表代替升級。
3. 使用既有綁定的 Apps Script 專案，確認 `Locale.gs`、`Client.gs`、`Table.gs`、`Draft.gs`、`Sidebar.html` 都在。每次切檔先觀察編輯器內容，再取得真正的 textbox 焦點；貼入前核對選中檔，貼入後核對未儲存狀態與全文回讀。
4. 儲存完成後重新載入專案，逐檔複製全文與本機來源比較。編輯器回讀可用目前可見 textbox 的 `press('Control+a')`，接著複製；不要讀 Monaco 私有模型或只比對畫面可見數行。
5. 來源已核對後，只有需要升級既有儲存格時才執行一次 `setupWorkbook`。觀察執行記錄完成且無錯誤，再重新載入 Sheets，核對「三国杀」選單與六個專用分頁。這個升級動作不會建立遊戲房間。
6. 回讀房間設定、牌桌標籤、手牌／預檢／戰報及候選表頭，核對原輸入保留；打開側欄核對中文提示並保存截圖。若尚未有候選，不為了看 checkbox／枚舉而自行開局；候選提交行為仍另列待驗。
7. 分別報告來源／靜態、線上保存／工作表顯示、原生建置／部署、完整局／勝方、bridge／helper 退出與埠回收。更新腳本成功不等於原生修正已部署或 10P 完成。

若日後獲准恢復對局，Cloudflare 臨時通道的先前授權只適用已結束的單局，不當成永久授權。完整局仍須取得 `GAME_OVER`、勝方、客戶端／伺服器正常退出、無 orphan 與埠釋放；初始化再逾時時保存證據並遵守當輪停止條件。
