# GUI 呈現與特效驗證 — 2026-09-20

適用於各證據目錄保存的 Windows Debug 來源與執行檔快照。[成本控制與驗收方法](../gui-client-performance.md)。

## 實測結果

2026-09-20 完成增量 Debug 建置及直接執行
相關 focused executable；未使用 CTest。

| 本次 gate | 結果 |
| --- | --- |
| 呈現模型契約 | PASS：43 項，exit 0。 |
| 操作面板契約 | PASS：6 項，exit 0；第一次因缺 Qt offscreen plugin 路徑在啟動前失敗，補正同版本插件路徑後通過。 |
| 特效政策／完成回呼契約 | PASS：76 項，exit 0。 |
| GUI 主程式建置 | PASS：技能／對話框型別及回傳值修正已在目前工作樹，Debug GUI 與三個 focused target 增量建置 exit 0。 |
| Windows GUI Full／Reduced／None 煙霧 | PASS：修正 smoke 退出生命週期後，三種 profile 各通過七個階段並自行 exit 0。Debug 啟動期限設為 45 秒，每次程序約 28–29 秒結束。 |
| 啟動逾時清理 | PASS：保留 25 秒期限，main_window 階段逾時，回傳預期失敗碼 6 並自行退出；不把逾時改報成功。 |
| 一般 GUI 啟動／關閉 | PASS：未帶 smoke 旗標，Full 首頁 ready 後送 WM_CLOSE；程序自行 exit 0。未做畫面／操作人工驗收。 |
| 有效 Spine 快取／50 人效能／完整對局／CI | NOT RUN；上述獨立契約不能代替產品整合與流暢度驗收。 |

GUI 第一次建置遇到舊 engine stub 的 `QDialog` 型別宣告缺失，已在 wind／ol／mobile
標頭補上不透明前置宣告；一次續建仍於 mountain 的 `SkillDialogInfo` 回傳值及
sp／tenyear2 的 `SkillDeclarationCandidate/Reason` 不完整型別失敗。
詳細原始錯誤、外掛彈窗截圖、程序退出、binary hash 與測試日誌見
`builds/gui-performance-acceptance-20260920/report.md`。後續修正加入有效 `SkillDialogInfo` 回傳值與 `skill-declaration.h` include，
該輪成功建置並重跑三組契約，證據見 `builds/gui-performance-followup-20260920/report.md`。
原本 Full／None 尚未開始特效案例便超過 25 秒。後續原生除錯確認，這套 Debug 環境在視窗
建立前已用約 23–24 秒；45 秒對照可完成特效案例，不能把原逾時歸因於 Spine 或 GIF。

另修正 effects smoke 專用的退出生命週期：Qt 日誌攔截器必須在靜態正規表示式銷毀前還原；
所有提前返回分支也須解除 Engine 的視窗 parent，釋放視窗，再釋放 QApplication。
修正前的 native 快照候選位址指向 Qt TLS 清理警告再次進入已逾生命週期的分類器；
此分析是 stack-address scan，並非完整 unwind。修正後三種 profile 與逾時路徑均完成程序退出。
舊程序曾有退出碼但仍鎖住 exe；不能只靠退出碼或 .NET 的「not running」判定清理完成，
清理結果以 Windows 程序 handle 的終止訊號確認。完整報告見 `builds/native-gui-debug-20260920/report.md`。
本次沒有縮短引擎／設定初始化耗時，也不把 synthetic GIF／損壞 Spine 案例當作有效 Spine 資源驗收。
工作區同時有其他來源變更，不能把本次混合工作樹建置當作隔離基準。
