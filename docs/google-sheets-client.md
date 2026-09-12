# Google Sheets 完整對局前端

本文件記錄 2026-09-12 已確認需求。儲存格前端、配對 gateway 與獨立原生入口
已完成原生與完整 Debug 建置、55/55 CTest、12 個 gateway 及 13 個前端 focused 測試。
五個 Apps Script 檔案已安裝並經使用者完成 Google 授權；真實 Sheets 已完成配對、
開房、準備及選將候選呈現，完整對局與儲存格 UI 修正仍在驗收。
來源與安裝流程見 [Google Sheets README](../google-sheets/README.md)。

## 已確認需求

| 決策 | 已確認範圍 |
|---|---|
| 遊戲核心 | 沿用本專案 C++／Lua 引擎，在外部服務執行 |
| 首版主機 | 現有 Windows 電腦，透過 HTTPS 安全通道供 Sheets 連線；遊玩期間保持開機 |
| 介面 | 本輪確認改以儲存格呈現及選牌、選目標、技能與排序；側邊欄提供連線、更新與提交控制 |
| 玩家隔離 | 每位玩家各自一份 Sheets，只接收該玩家可見的資料 |
| 登入方式 | 貼上服務網址與一次性配對碼；配對後綁定玩家會話，不新增遊戲帳號系統 |
| 互動範圍 | 沿用 Excel 的 28 類互動；排除 `qml_interact`／`qsanguosha.qml` |
| 擴展範圍 | 載入全部擴展，沿用現有引擎載入與房間設定；不新增 Sheets 專用的武將／牌包白名單 |
| 不支援內容 | 使用者已取消開局前的相容性攔截；遇到未實作互動時明確回報，不偽造玩家回覆 |
| 首個對局驗收 | `05p` 五人身分局，一名真人操作、四名 AI；多人 Sheets 另列驗收 |
| 真人倒數 | 首個驗收局不設選將、出牌及技能回應倒數；連線故障仍須獨立處理 |
| 結局與清理 | 真人操作至 `GAME_OVER`、顯示勝方、正常關閉會話／所擁有的 helper；無孤兒程序、釋放本次埠 |

## 現有來源與重用邊界

| 來源 | 可重用契約／需要處理的差異 |
|---|---|
| [Excel IPC v1](excel-ipc.md) | 原生快照、增量事件、指令去重、互動版本檢查與結構化草稿；HTTP 僅監聽 loopback |
| [Excel 實作狀態](excel-implementation-status.md) | 既有橋接完成過一次託管對局；實際 Excel 操作與修正版完整對局仍未驗收，不能作為 Sheets 驗收證據 |
| `src/excel/excel-bridge.{h,cpp}` | `ClientCore`、`ClientLiveSession`、`ExcelInteractionAdapter` 與 `LocalServerController` 的串行會話 |
| `src/excel/excel-view.{h,cpp}` | 已過濾的玩家可見狀態與呈現列；圖片是本機絕對路徑，Sheets 必須另行映射 |
| `src/excel/excel-process-guard.{h,cpp}` | Excel 父程序身分與生命週期，不能直接作為 Sheets 服務的監護方式 |
| `excel/frontend-coverage.json` | 28 類呈現映射基線；不是逐技能內容相容性的證明 |
| [Web client](web-client.md) | 既有 WebSocket／Protocol V2 客戶端可參考互動呈現；其 WASM 規則部署不直接搬進 Apps Script |

## 擬採設計

```text
每位玩家的 Sheets 儲存格 + HTML 連線／更新側邊欄
  → google.script.run
  → Apps Script：玩家憑證、有限批次讀寫、HTTPS 請求
  → HTTPS 入口：配對與玩家會話路由
  → Windows 原生會話：ClientCore + 規則預檢 + 已過濾快照
  → 現有遊戲伺服器／Lua／AI
```

Apps Script 與側邊欄不重寫武將規則、不解碼第二套遊戲協議，也不自行猜測合法出牌。
HTTPS 通道後方的配對與會話路由來源為 `google-sheets/gateway.py`，僅監聽 loopback；
本次已經使用者授權部署臨時 Cloudflare HTTPS 通道及綁定式 Apps Script；
臨時通道測後關閉。既有 Excel loopback 入口不能直接對外公開。

唯讀盤點確認：首版可讓每個玩家對應一個原生 bridge／`ClientCore`，保留現有串行
語意；另加非 Excel 的啟動／監護入口。不得為 Sheets 放寬既有 Excel 入口對
`EXCEL.EXE`、父程序 handle／建立時間與私有 bootstrap 權限的檢查。
現有 Web launcher 是本機靜態檔案服務，亦不能直接替代配對與會話路由。

| 設計項目 | 實作要求 |
|---|---|
| 配對 | 主機產生隨機、有期限、單次使用的配對碼；原子兌換、限制失敗嘗試；配對碼不放 URL |
| 憑證 | 綁定服務端玩家會話；Apps Script 以該使用者的屬性儲存，避免寫入儲存格、共用腳本屬性或日誌；複製模板需重新配對 |
| 會話隔離 | 伺服器從憑證決定玩家身分；不相信儲存格、客戶端提供的席位或文件 ID 作為授權證據 |
| HTTPS | 驗證憑證、禁止攜帶憑證的跨主機重新導向；服務網址變更需重新配對 |
| 更新 | 側邊欄以短請求輪詢；單一更新請求在途、失敗退避、有界事件；不讓單次 Apps Script 執行持續整局 |
| 儲存格 | 僅在內容變更時批次更新；寫入文字時避免把玩家名稱、聊天或技能文字當作公式 |
| 指令 | 保留字串形式的 `id`、`generation`、`revision`、`request_id`、`sequence`；真人指令依序提交，網路重試保留相同指令與編號 |
| 草稿 | 卡牌次序、重複目標、技能實例與宣告均沿用原生草稿；預檢只對完全相同的草稿與互動版本有效 |
| 失效狀態 | 新互動、重新連線、過期回覆或未知結果使舊預檢失效；顯示錯誤，不偷偷啟用託管或偽造回覆 |
| 暗牌 | 未授權身分、暗牌、原始牌堆順序與原始協議不傳到 Sheets；隱藏分頁不是資料隔離 |
| 圖片 | 將已允許的素材轉為受控識別碼或受驗證的 HTTPS 素材；不洩漏 Windows 路徑，不讓客戶端讀任意主機檔案 |
| 重連 | 保留既有 reconnect／snapshot 契約；恢復目前狀態，不重播過時音效或重送已執行動作 |
| 關閉 | 明確離開才停止本人會話及其擁有的 helper；瀏覽器短暫背景化不直接等於程序死亡；意外斷線清理期限須與重連窗口一致 |

一次性配對僅取代遊戲帳號，不取代 Google 對 Apps Script 的首次授權。
每人一份文件也不代表可以把含本人暗牌的文件分享給其他玩家；服務端仍不得傳出他人的暗牌。

## 全擴展與不支援互動

使用者明確要求「全擴展，不做攔截不支援內容」，取代先前的開局前相容性攔截決策。
首版與首個 05P 均載入全部擴展，遵循既有引擎與房間設定，不新增 Sheets 專用的
內容核准清單、不因未完成前端相容性審核而禁止武將／牌包／模式。

先前排除 QML 介面的決策維持；取消內容攔截不等於新增 QML presenter。
若實際收到未支援互動，保留明確錯誤與失敗證據，不默默跳過或偽造回覆。
`excel/frontend-coverage.json` 的 28 類映射仍是互動基線，不是全部擴展可玩的證明。
記錄實際載入來源版本與 hash 是驗收溯源，不得把這份清單變成相容性准入限制。
完整對局結果僅證明本次實際走過的路徑，不推定每個擴展技能均已驗收。

## 檢查點與驗收

| 檢查點 | 交付內容 | 驗證邊界 |
|---|---|---|
| CP0 設計與來源盤點 | 本文件、已確認需求、共用邊界與全擴展範圍 | 文件與來源靜態讀取；實際載入版本尚待記錄 |
| CP1 配對與原生會話 | HTTPS 路由、一次性配對、原生監護、玩家隔離及 Apps Script 傳輸 | 完成來源後另取得該檢查點建置／focused 驗證許可 |
| CP2 Sheets 可操作牌桌 | 活頁表模板、Apps Script、側邊欄、28 類互動、圖片與詳情、重連／結局 | 真實 Google Sheets 授權、配對及逐類操作；不能以本機 JSON 驗證代替 |
| CP3 真人 05P | 載入全擴展並記錄版本，一名真人完整操作到勝負並正常清理 | 獨立完整對局授權；不以託管或純橋接局代替真人 Sheets 操作 |
| CP4 多人與交付 | 每人一份文件、多會話隔離、多人對局與可部署套件 | 多人、長時間／CI 驗證另列，不由首個 05P 推定通過 |

專案預設禁止本地 CTest 與超過 60 秒的大型測試。選定 05P 作驗收案例不等於授權
立即啟動完整對局；依 `AGENTS.md`，每個來源檢查點先完成並宣告，再取得相應驗證許可。
後續本輪使用者明確授權「建置、完整測試、解除 60 秒限制」，因此已執行建置、
完整 CTest、focused 測試及實際 Google 部署／對局驗收；未 commit 或 push。
28 類分成七種原生 response shape 實作；映射數量不代表 28 類真實 Sheets 操作通過。

| 本輪來源 | 邊界 |
|---|---|
| `src/sheets/sheets-main.cpp` | 私有 stdin 監護、不同於 Excel 的啟動入口、先釋放 bridge 再銷毀 Engine |
| `cmake/QSanguoshaSheets.cmake` | opt-in Windows Qt6 target，依賴 Excel 共用 helper，輸出至 excel-debug／excel-release |
| `google-sheets/gateway.py` | 單次配對、nonce 恢復、每玩家隔離、目的地允許清單、素材識別碼與有界關閉 |
| `google-sheets/apps-script/` | 文件／使用者憑證、持久 pending 指令、儲存格草稿、目錄、結局與詳情 |
| `google-sheets/tests/` | gateway 與純草稿／指令恢復 focused 已執行；native probe 與 Sheets UI 的證據分開 |

2026-09-12 驗收發現並處理兩個部署前提：Windows bridge 需部署同版 Qt／FMOD DLL，
不能用缺少 `Qt6Cored.dll` 的啟動失敗判定遊戲回歸；開發根目錄中未宣告 Lua 暫存檔
會觸發既有 `rules_content_unsupported`，須以既有 packager 整理宣告內容。
`google-sheets/prepare-runtime.py` 保留全部配置擴展及 config bytes，另補 AI／圖片，
不修改外部 Lua 或放寬身分驗證。本次完整內容為 102 個配置擴展、117 個翻譯、
165 個 AI Lua、18,233 張圖片；原生規則匯出為 312 個 package、1,195 張卡。

實際儲存格驗收另外修正 D 欄文字格式導致勾選不是布林值、長素材識別碼撐高列、
開局前體力顯示 undefined，以及內部標記過多等問題。候選採真正布林勾選，保留
原生識別碼供詳情查詢，牌桌只摘要標記。已結束但退出失敗的會話可釋放憑證，
保留錯誤提示；未知／仍執行中的關閉結果不清除憑證，也不偽造正常退出。

原生 `UPDATE_PLAYER_UI_STATE` 的 maxCards／距離／虛擬裝備欄位是效果提示，
可能含其他玩家或帶參數的技能文字，不能加入玩家實際擁有技能清單。修正 reducer
後仍保留提示與數值，技能擁有權沿用既有權威訊息；新增回歸案例已通過。

首版 gateway 可預開 1–8 個獨立 Sheets 玩家 slot；不是房間人數限制。
多真人使用主機允許的既有遊戲伺服器；Sheets 私有開房維持 loopback。
重連窗口預設 30 分鐘，配對碼期限 5 分鐘，slot 關閉後不重用。主機意外死亡、
強制終止或清理證據寫入失敗不算正常退出。退出碼也不能取代 GAME_OVER／helper／
無孤兒／埠釋放的完整驗收證據。

## 2026-09-13 收尾狀態

依使用者指示，原生缺陷留待專項處理；本輪停止延伸除錯與測試。前端已安裝，
完整 05P 仍受清儉牌堆／關閉缺陷阻塞，未取得 GAME_OVER 或勝方。詳見
[原生除錯報告](google-sheets-engine-debug-report-20260913.md)，其中分開列出初版
55/55 CTest、最新精準驗證、未完成 gate 與已保留的未提交原生修改。

## Google 官方限制參考

以下於 2026-09-12 查閱；限制可能調整，部署時需重新核對。

| 官方來源 | 本設計的用途 |
|---|---|
| [URL Fetch](https://developers.google.com/apps-script/reference/url-fetch/url-fetch-app) | 請求源自 Google，不能將玩家電腦的 localhost 作為 Apps Script 目標 |
| [HTML 與服務端通訊](https://developers.google.com/apps-script/guides/html/communication) | `google.script.run` 非同步且不保證完成次序，需要序列與過期回覆檢查 |
| [配額](https://developers.google.com/apps-script/guides/services/quotas) | 單次執行、UrlFetch 與 Properties 均有限額，需短請求、節流與批次更新；不承諾固定即時延遲 |
| [Properties](https://developers.google.com/apps-script/guides/properties) | 區分 user／script 屬性範圍，不把玩家憑證放進所有使用者共用的屬性 |
