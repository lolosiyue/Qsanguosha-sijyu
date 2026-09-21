# Google Sheets 安裝與操作

架構與資料契約見[設計文件](google-sheets-client.md)；版本測量見[驗證記錄](reports/google-sheets-20260913-16.md)。

## 房間布局與共用戰報

`QSAN Actions` 是房間畫面，保留現有 D–F 欄選擇契約；更新原有 Apps Script
檔案後，首次更新牌桌會轉換這張專用工作表。`QSAN Room` 仍用於連線設定。

| 區域 | 內容 |
|---|---|
| 外圍座位 | 本人在下方，其餘按座位順序環型排列；保留陣亡席位，顯示武將、體力、手牌數、身分、階段、裝備與判定區 |
| 中央 | 僅呈現 PlaceTable 處理中的牌、現時行動／response prompt、預檢結果 |
| 本人區域 | 可見手牌的名稱／花色／點數及技能摘要；下方為原生提供的可操作候選清單 |
| 右側 | 最新戰報在上；完整保留的最近 200 條仍見 QSAN Log |
| 選擇 | D 欄勾選，E 欄指定順序，F 欄處理觀星／角色；重排布局時保留同一互動的草稿，換互動則重新產生候選 |

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
時套用新版；對局內查詢的實测狀態見[驗證記錄](reports/google-sheets-20260913-16.md)。

布局使用既有 Apps Script Spreadsheet 服務的
[Range 合併／寫入](https://developers.google.com/apps-script/reference/spreadsheet/range)與
[Sheet 欄寬／列高設定](https://developers.google.com/apps-script/reference/spreadsheet/sheet)。

## 組成

| 元件 | 職責 |
|---|---|
| `apps-script/Locale.gs` | 固定 Sheets 文案的穩定英文鍵與簡中表；遊戲文本一般使用簡中 |
| `apps-script/Client.gs` | 玩家憑證、HTTPS 請求、指令恢復與操作入口 |
| `apps-script/Table.gs`／`Draft.gs` | 工作表呈現、排序選擇與七種標準回覆結構 |
| `apps-script/Sidebar.html` | 配對、更新與連線控制；卡牌與目標在工作表選取 |
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
初次載入全部擴展可能需要時間，`--startup-timeout` 預設 180 秒，是產品啟動
限制，並非代理自行執行長時間驗證的許可。

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

儲存並重新開啟 Sheets，使用 QSanGuosha 選單建立專用工作表及開啟連線控制。
完成 Google 首次授權，輸入 HTTPS 服務網址與主機顯示的一次性配對碼。
配對碼預設有效 5 分鐘。網址變更或複製文件後重新配對，不把憑證填入儲存格。
安裝不需要把 Apps Script 發佈為公開 Web App，也不需要建立新的遊戲帳號。

舊版試作的 `Code.gs` 不與這三個 `.gs` 並存；新專案預設的空白 `Code.gs`
可刪除或保持完全空白。專用工作表以 developer metadata 識別，遇到別人建立的
同名頁籤會拒絕覆寫，請自行改名後再建立。既有專用房間設定不會在更新時重設。

| 操作 | 儲存格方式 |
|---|---|
| 開房／加入／聊天 | `QSAN Room` 填玩家、頭像、主機、埠、AI 人數與聊天文字，再按側邊欄 |
| 選牌／選項／技能 | `QSAN Actions` 的 D 欄勾選；技能先按預檢，以取得宣告或後續候選 |
| 目標次序／重複目標 | E 欄填 `1`、`2`；同一目標可填 `1,3`，保留重複與順序 |
| 觀星 | E 欄填順序，F 欄選 `top`／`bottom`；完整提交所有牌的分區 |
| 遺計 | 勾選一組牌與一名接收者，按預檢／提交；依原生互動繼續後續分配 |
| 角色分配 | F 欄選原生提供的角色 ID |
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

`exit.json` 記錄 native exit code、是否強制停止及清理錯誤；`native.log` 為私人
診斷。清理驗收同時核對 helper、埠與 GAME_OVER。
程式不刪除整個 runtime 或診斷目錄；刪除歷史診斷應由主機管理者決定。
