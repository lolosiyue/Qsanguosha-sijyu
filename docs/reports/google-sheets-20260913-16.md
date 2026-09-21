# Google Sheets 驗證記錄 — 2026-09-13 至 16

操作流程見[安裝指南](../google-sheets-setup.md)。以下結果按各次測量版本記錄。

## 2026-09-15 完整對局

狀態：2026-09-15 已完成一局真實儲存格操作的 05P（真人 1＋AI 4、無託管），第五輪
GAME_OVER、主公＋忠臣勝；原生 exit 0、無強制終止，程序與埠清理通過。
五個 Apps Script 檔案已安裝到使用者的測試文件。此結果證明單局可玩，仍有文字／取消呈現
缺口及多人、逐類互動、CI／交付包待驗；缺口清單見 [設計文件](../google-sheets-client.md)。
需求與驗收基準亦見同一份設計文件。

## 2026-09-16 房間布局

Sheets bridge／TUI／TUI 測試目標增量建置成功，前端測試
27/27 通過，TUI `card-text` 與 `log-text` 均通過（各少於 60 秒）。初次前端測試
發現的戰報末列邊界已修正。
此版後續已完成線上部署及一局 05P 託管對局，包含 GAME_OVER、主公＋忠臣勝、
正常退出與清理；版面仍有待修問題，見[設計文件缺口清單](../google-sheets-client.md)。

### 說明、圖片與座位詳情

Sheets 增量建置 exit 0、前端 30/30 通過；三個改動的 Apps Script 檔案已更新線上，
並讀回核對一致。新版選單及側欄提示已載入，對局內查詢仍待驗收。

## 2026-09-15 共用規則與隱私修復

絕途的 `sgs.Self` 缺失、無懈可擊錦囊名稱及 E4 移牌 ID 遮蔽已修復並通過相關 focused
驗證；Sheets bridge/helper 已重新建置與部署 runtime。絕途合法單牌可確認，同花色重複選牌仍被拒絕。
E3 的原生獨立修復已有 focused 證據；**修復後真人 Sheets 完整對局及正常關閉已通過**。

## 2026-09-15 前一局真人驗證（已被取代）

該局在第二輪「絕途」保留牌預檢受阻、正常關閉再現 E3（exit 86）；兩項根因其後
均已修復並提交（見上節）。逐輪驗證原始記錄位於
`builds/google-sheets-qa/`。

## 2026-09-13 歷史驗證檢查點

| Gate | 目前狀態／下一步 |
|---|---|
| Native Sheets target／完整 Debug | PASS；含三個原生 IPC／interaction／view focused executable |
| 初版完整 CTest | 55/55 PASS，1,694.63 秒；早於後續原生修正，不能視為最新修改的完整驗證 |
| Gateway focused contracts | PASS：12 tests；`python -m unittest discover -s google-sheets/tests -p test_gateway.py` |
| 草稿／指令恢復 focused | PASS：18 tests，包含勾選格式遷移與失敗會話恢復 |
| 原生 UI state／牌堆修正 | 增量建置與 ClientCore／規則生命週期 2/2 PASS；完整對局及最新 full-suite 未完成 |
| 真實 Apps Script | 已安裝五檔並重新載入核對；授權、配對、更新、開房、準備、勾選選將／預檢／提交及玩家詳情已驗證 |
| 出牌中正常關閉 | 其後已修復：E3 例外解構根因修正後，exit 0、正常退出與清理均通過 |
| 28 類互動 | 來源映射不是逐類操作通過證據；仍未逐類驗收 |
| 真人完整 05P | 其後已通過：2026-09-15／16 各完成一局 GAME_OVER、勝方與正常清理 |
| 多人／CI／交付包 | NOT RUN；須分開驗收 |

建置與測試日誌位於 `builds/google-sheets-qa/validation-20260912-01/`。

2026-09-13 原生缺陷調查暫停，測試與臨時服務已停止。
該批原生缺陷（E1–E5）其後已全部修復並提交；證據目錄保留於 `builds/`。


## 初版來源基線（2026-09-12）

初版原生與完整 Debug 建置、55/55 CTest、12 個 gateway 及 13 個前端 focused 測試通過；配對、開房、準備與選將候選已操作，當時完整對局仍在驗收。後續結果依上列日期記錄。

## 2026-09-16 缺陷與未驗項目

原生缺陷（E1 效果提示誤授技能、E2 命名牌堆同步遺漏、E3 關閉例外解構、
E4 移牌 ID 遮蔽、E5 提示代號）均已修復並提交。2026-09-15 真人 05P 完成
第五輪 GAME_OVER、主公＋忠臣勝、原生 exit 0 與程序／埠清理；2026-09-16
線上部署後再完成一局 05P 託管完整對局。逐輪驗證報告已依文檔清理政策移除，
原始證據仍保留於 `builds/google-sheets-qa/`。

下列缺口仍需另行驗收或修復，未被任何單局 PASS 覆蓋：

| 缺口 | 說明 |
|---|---|
| `ask_peach` 提示 | 未含救援對象與需求，仍只有通用提示 |
| 內部名稱顯示 | 部分技能／參數化選項與標記仍顯示 `sfofl_young_eight_diagram`、`shisuan`、`damaged`/`dismark`、`not_active` 等；未提供翻譯的擴展沿用 TUI fallback |
| 取消呈現不一致 | 附體類選人（玄武暗魂）呈現可取消但原生不允許，根因未定位 |
| 勝方顯示 | `winner_tokens` 結構欄位未轉為易讀中文 |
| 房間版面 | 陣亡後座位號重複、多席行動箭頭誤標；託管後中央可能殘留已回應的選將提示；窄視窗需縮放才能完整顯示 A:P |
| Apps Script 更新速度 | 分區寫入偏慢，更新中可見前後快照混合 |
| 原生快照警告 | `player.tags.NullifyingEffect (CardEffectStruct)` 無法無損 JSON 儲存；未擴大原生除錯 |
| 對局內詳情查詢 | 新版座位／圖片詳情在真實對局內尚未驗收 |
| Web 關閉路徑 | E3 共用引擎修復僅 Sheets 實測；Web 入口未重測 |
| 完整 gate | 28 類逐項真人互動、多人文件隔離、遠端 CI 與交付包均未驗 |
