# Google Sheets 房間版面線上部署與託管驗收（2026-09-16）

## 範圍

使用者明確要求更新既有線上 SGS 文件，並以 trust 執行完整對局。本輪沿用工作表的
`05p`、1 個 Sheets 玩家＋4 AI、無操作時限設定，只開一局，設 30 分鐘單局期限。
這是託管驗收，不代表新版房間頁的真人選牌／預檢／提交全部通過。

## 部署與對局資料

| 項目 | 值 |
|---|---|
| 工作樹 | `L:\finaldebug\QSanguosha-v2`，`debug`，含未提交來源差異 |
| 線上文件 | [SGS](https://docs.google.com/spreadsheets/d/1HxZg4VfwLK7uMoqx69chX-0WoCCdDbqxs6aJ_d0NmUg/edit) |
| Apps Script | `Client.gs`、`Table.gs`、`Draft.gs`、`Sidebar.html` 已儲存，逐檔讀回與本地一致 |
| 原生產物 | 本輪檢查點已成功建置的 Sheets bridge／ExcelServer；另完成 Debug runtime 部署 |
| 素材 | 沿用前一日完整擴展 runtime，未改動外部 Lua |
| 模式／Seed | `05p`／`6555328003384258695` |
| Sheets 玩家 | `sgs1`，2 號位，SK神夏侯惇，反賊 |
| 開始 | 伺服器 `2026-09-15T16:45:28.795Z GAME_STARTED mode=05p` |
| 證據目錄 | `builds/google-sheets-qa/room-trust-20260916/` |

## 現場觀察

- 四個檔案經既有 Apps Script 編輯器更新及讀回核對；原版本備份與比對結果在
  `apps-script-deployment.json`、`*.before.gs`／`*.before.html`。
- 開房後首次 trust 因舊會話序號回報 `stale_session`。刷新同一會話後重送成功，
  隨後自行選將、出殺及裝備樓船；沒有重開房間或用投降結束。
- 真實 `QSAN Actions` 已顯示環型座位、本人手牌／裝備、中央處理區及右側戰報。
  `room-active.tsv` 例如包含 `贯穿射击[♦6]`，以及中文出牌、摸牌、傷害與回復紀錄。
- `SGS-live.xlsx`、`room-live.png` 保存現場畫面／儲存格內容。

## 可讀性及驗收限制

| 觀察 | 影響 |
|---|---|
| Apps Script 分區寫入偏慢 | 更新中可看到前後快照混合；側欄操作在請求期間停用 |
| 託管後曾保留選將提示與候選 | 中央提示不能視為當下仍需人工回應的可靠指示 |
| 陣亡後座位號重複、多席顯示行動箭頭 | 最終房間有兩個 2 號位及兩個 3 號位；原始座位及目前行動者標示仍須修正 |
| `not_active` 及擴展標記仍有內部名稱 | 共用 TUI 戰報改善主要事件，但不是全部文字翻譯完成 |
| 勝方仍顯示結構欄位 | 可由 `winner_tokens` 判斷勝方，但尚未轉成簡潔中文 |
| 窄視窗＋側欄須縮至 50% 才能容納 A:P | 完整版面可見，但字體偏小；100% 需要水平捲動 |
| 原生回合快照警告 | `player.sgs4.tags.NullifyingEffect (CardEffectStruct)` 無法無損 JSON 儲存；保留日誌，未擴大原生除錯 |

## 最終結果

**PASS：線上部署及本輪唯一一局 05P 託管完整對局，含正常退出與清理。版面可讀性仍有上列問題。**

| Gate | 結果／證據 |
|---|---|
| 完整對局 | 伺服器 `2026-09-15T16:49:17.090Z GAME_OVER mode=05p winner=lord+loyalist` |
| 線上結局 | 第二輪，`QSAN Board!G2=GAME_OVER`，`C4` 的 `winner_tokens=lord,loyalist`；主公＋忠臣勝 |
| Sheets 玩家 | SK神夏侯惇第二輪陣亡；全程使用使用者指定的 trust，未重開、未投降 |
| 最終匯出 | `SGS-final.xlsx`、`board-final.tsv`、`room-final.tsv`、`room-final.png` |
| 正常關閉 | 側欄「已正常關閉會話」，`session-closed.png`；伺服器 `16:51:12.717Z shutdown_complete` |
| 原生退出 | slot `exit.json`：`native_exit_code=0`、`clean_native_exit=true`、`forced_termination=false`、`cleanup_error=null` |
| 程序清理 | Bridge 55624、Server 35616、Gateway 55800、cloudflared 56468 均無殘留；Gateway 在原生退出後 Ctrl-C，exit 1 |
| 埠釋放 | 7051、11051、11052、8766、20241 均無 listener，見 `cleanup.json` |
| 時間 | 00:41:25 提出開局，00:45:28 開始遊戲，00:49:17 結束，00:51:41 清理完成（香港時間），未超過 30 分鐘期限 |

`exit.json` 的 `full_game_acceptance=NOT_RUN` 是 gateway 不自行判斷 GUI 對局的保守欄位，
未改寫它；本次完整對局結論由伺服器 GAME_OVER、真實工作表及清理證據共同支持。
服務與臨時 tunnel 已停止；線上腳本／房間保留，縮放還原 100%，側欄關閉。
多人文件隔離、真人操作、其他模式／客戶端及遠端 CI 未於本輪執行；未 commit／push。

建置及 focused 結果另見 [房間布局與 TUI 文字檢查點](google-sheets-room-validation-20260916.md)。
