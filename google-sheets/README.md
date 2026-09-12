# Google Sheets 對局前端

狀態：原生 target 與完整 Debug 建置成功；本地 CTest 55/55 通過。
五個 Apps Script 檔案已安裝到使用者的測試文件，完成 Google 授權、HTTPS 配對、
原生開房及選將候選呈現。真實儲存格操作仍在驗收；不能把原生 probe 當作完整 Sheets 對局。
需求與驗收基準見 [設計文件](../docs/google-sheets-client.md)。

## 組成

| 元件 | 職責 |
|---|---|
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

以下是本次通過來源檢查點及使用者建置／完整測試授權後使用的建置方式。
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
可沿用已準備的 Excel runtime 素材，但既有 Excel 測試結果不代表 Sheets 通過。

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
診斷。這些只能證明所記錄的程序結果，仍須額外核對 helper、埠與 GAME_OVER。
程式不刪除整個 runtime 或診斷目錄；刪除歷史診斷應由主機管理者決定。

## 驗證檢查點

| Gate | 目前狀態／下一步 |
|---|---|
| Native Sheets target／完整 Debug | PASS；含三個原生 IPC／interaction／view focused executable |
| 初版完整 CTest | 55/55 PASS，1,694.63 秒；早於後續原生修正，不能視為最新修改的完整驗證 |
| Gateway focused contracts | PASS：12 tests；`python -m unittest discover -s google-sheets/tests -p test_gateway.py` |
| 草稿／指令恢復 focused | PASS：18 tests，包含勾選格式遷移與失敗會話恢復 |
| 原生 UI state／牌堆修正 | 增量建置與 ClientCore／規則生命週期 2/2 PASS；完整對局及最新 full-suite 未完成 |
| 真實 Apps Script | 已安裝五檔並重新載入核對；授權、配對、更新、開房、準備、勾選選將／預檢／提交及玩家詳情已驗證 |
| 出牌中正常關閉 | FAIL：已到 play_card，active bridge exit 86；無殘留且埠已釋放，仍不算正常退出 |
| 28 類互動 | 來源映射不是逐類操作通過證據 |
| 真人完整 05P | BLOCKED：清儉牌堆／進行中關閉缺陷；未取得 GAME_OVER 或勝方，依使用者指示停止原生除錯 |
| 多人／CI／交付包 | NOT RUN；須分開驗收 |

本次使用者明確授權建置、完整測試並解除 60 秒限制；不改變其他任務的預設政策。
建置與測試日誌位於 `builds/google-sheets-qa/validation-20260912-01/`。

2026-09-13 使用者要求原生缺陷留待專項處理，已停止延伸除錯、測試與臨時服務。
問題、證據、未提交修改與後續確認邊界見 [原生除錯報告](../docs/google-sheets-engine-debug-report-20260913.md)。

