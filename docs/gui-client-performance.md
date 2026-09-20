# GUI client：50 人與全特效效能檢查點

本文件記錄 GUI 呈現與特效的成本控制；不是 FPS、RAM、完整對局或乾淨退出的驗收報告。
沿用 [大局原生 UI](large-room-ui-implementation.md)、[特效政策](linux-gui-effects-profiles.md)
及 [圖片快取失效契約](package-modularity.md#native-image-cache-lifetime)。

## 資訊更新

| 來源 | 更新行為 |
| --- | --- |
| 動畫移動、透明度、純重繪 | 不經 `QGraphicsScene::changed` 重建 actions／全場 GameViewState。 |
| 卡牌、目標票數、技能選取及合法性 | 原有 draft 訊號與 `RoomScene::presentationDraftChanged` 排程一次事件迴圈末端的呈現更新。 |
| 接受的狀態／連線變化 | 將 recipient-scoped 投影標記為 dirty，下一次有消費者時重建。 |
| 錄影通知 | reducer 與 GUI callback 完成後發出狀態變更；不再依靠畫面重繪觸發。 |
| STATE_SYNC | 中間片段不發布、不接受舊呈現 intent；完成後才發布完整狀態。 |
| 草稿更新 | 重用同一份玩家／事件投影，只更新 action、prompt、request 與 presentation revision。 |

`ClientGameState` 仍是唯一狀態來源；快取只保留現有 `GameViewState` 的接收者可見值。
不從原生 Room／其他玩家手牌重建隱藏資訊，也不另建 reducer。每次 intent 派發前仍重讀
當前 action，檢查 generation／revision／request，保留原有超時與回應驗證。

大局座次只在名冊／座次／自己的環起點變動時重排；列內位置查詢使用座次表。
玩家資料變動只更新受影響縮略項；候選、票數與結算標記則依各自依賴更新。
關閉的詳細卡不格式化文字，重開時讀取最新投影。Photo 仍沿用原生頭像、HP 與手牌元件，
HP／手牌數變動不重新設定未變的頭像。

## 全特效與生命週期

房間人數不再強制把使用者的 `Full` 改成 `None`。CLI／使用者設定與各個特效開關仍由
`VisualEffectsPolicy` 統一控制；XP 的既有平台限制不變。

隱藏的原生 Photo 保留選取／合法性狀態，但其 GIF 播放應暫停；重新可見時僅恢復原本
要求播放的動畫。顯式 stop、換圖、換皮膚與 Reduced 首幀模式不能被布局更新意外重啟。
一次性特效仍須交付完成回呼，不能以不可見為由直接停止流程時鐘。

Spine 的解析資源重用與播放狀態分離；各演出保留獨立骨架、AnimationState 與裁切器。
快取只接納成功載入的資源，保留原有解析候選評分選擇，不能改成遇到第一個成功就接受。
解析資源的 LRU 最多保留 8 項，以每組素材解碼 RGBA 貼圖估算 64 MiB 為淘汰預算；
單項超過預算仍可播放，但不保留。解析器選擇另存最多 128 項，避免素材淘汰後再次全候選探測。
鍵包含素材根目錄、皮膚／套件 revision、atlas／skel 檔案身分、runtime hint 與 scale；
命中時另外核對 atlas 實際引用的圖片頁（含子目錄），不掃描整個圖片目錄。

同素材的 SkeletonData／Atlas 共用，貼圖按 exact OpenGL context 分開保存；context 銷毀
時釋放該 context 的貼圖，下一個 context 首次繪製時重新載入。快取淘汰與最後一個活動
物件釋放都經過 context 切換保護，不能用另一個 context 的 GL handle。
64 MiB 是快取的單份貼圖估算預算，**不是程序 RAM／VRAM 硬上限**：不包含骨架解析物件、
活動物件仍持有的已淘汰資源，以及多 context 的貼圖副本。
若 owner context 已失效且無法 makeCurrent，會清掉 CPU wrapper，避免以其他 context 刪除
同號 GL 資源；此異常分支的 GL 分配須等 context／share group 銷毀才回收，不宣稱即時釋放。
此降級依據為 [Qt 6.11.1 的 texture destroy 實作](https://github.com/qt/qtbase/blob/v6.11.1/src/opengl/qopengltexture.cpp#L143-L164)。

## 驗證邊界

2026-09-20 來源檢查點之後，使用者授權「建置 驗收」。已做增量 Debug 建置及直接執行
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
`builds/gui-performance-acceptance-20260920/report.md`。使用者接續授權修正、建置與 GUI
測試後，核對目前工作樹已含有效 `SkillDialogInfo` 回傳值與 `skill-declaration.h` include，
該輪成功建置並重跑三組契約，證據見 `builds/gui-performance-followup-20260920/report.md`。
原本 Full／None 尚未開始特效案例便超過 25 秒。後續原生除錯確認，這套 Debug 環境在視窗
建立前已用約 23–24 秒；45 秒對照可完成特效案例，不能把原逾時歸因於 Spine 或 GIF。

另修正 effects smoke 專用的退出生命週期：Qt 日誌攔截器必須在靜態正規表示式銷毀前還原；
所有提前返回分支也須解除 Engine 的視窗 parent，釋放視窗，再釋放 QApplication。
修正前的 native 快照候選位址指向 Qt TLS 清理警告再次進入已逾生命週期的分類器；
此分析是 stack-address scan，並非完整 unwind。修正後三種 profile 與逾時路徑均完成程序退出。
舊程序曾有退出碼但仍鎖住 exe；不能只靠退出碼或 .NET 的「not running」判定清理完成，
本輪以 Windows 程序 handle 的終止訊號確認。完整報告見 `builds/native-gui-debug-20260920/report.md`。
本次沒有縮短引擎／設定初始化耗時，也不把 synthetic GIF／損壞 Spine 案例當作有效 Spine 資源驗收。
工作區同時有其他來源變更，不能把本次混合工作樹建置當作隔離基準。

| 後續驗收 | 必須保留的條件 |
| --- | --- |
| 呈現 | 純動畫不發布全場投影；滑鼠／鍵盤選牌、加減票、取消、技能切換、prompt、超時即時一致。 |
| 大局 | HP／手牌、角色死亡／移除、換座、回合標記、技能／標記／裝備詳情、候選排序、鎖定與細節重開。 |
| 同步 | 斷線重連／STATE_SYNC／錄影 seek 不顯示半份狀態，不接受上一代 request 的 intent。 |
| 動畫 | Full／Reduced／None、透明 Photo、GIF stop／恢復、皮膚切換、特效取消與完成回呼。 |
| 資源 | 同素材反覆演出、素材／皮膚失效、離房重入、viewport/context 重建與最後退出。 |
| 效能 | 相同 binary/config/素材/解析度/種子/SmartAI 設定比較幀時間 p95/p99、GUI thread CPU、private bytes、GPU 資源。 |

60 FPS 的 16.7 ms 幀預算與操作回饋 p95 < 100 ms 是待量測目標，不是本批成果數字。
GUI 與 AI/server 必須分開計量；不得以關閉 AI、關閉特效或換成 Release 的結果代替原情境。
本批不截斷既有戰報歷史；長局戰報的有界顯示與完整歷史載入是另行設計的工作。
