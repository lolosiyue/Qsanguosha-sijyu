# GUI 呈現與特效效能

本文件說明 GUI 呈現與特效的成本控制及生命週期。
沿用 [大局原生 UI](process/large-room-ui-implementation.md)、[特效政策](linux-gui-effects-profiles.md)
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

## 驗收方法

[2026-09-20 驗證報告](reports/gui-client-performance-20260920.md)保存 focused 契約、建置失敗與修正、GUI 特效及退出測試的結果。

| 驗收項目 | 測量條件 |
| --- | --- |
| 呈現 | 純動畫不發布全場投影；滑鼠／鍵盤選牌、加減票、取消、技能切換、prompt、超時即時一致。 |
| 大局 | HP／手牌、角色死亡／移除、換座、回合標記、技能／標記／裝備詳情、候選排序、鎖定與細節重開。 |
| 同步 | 斷線重連／STATE_SYNC／錄影 seek 不顯示半份狀態，不接受上一代 request 的 intent。 |
| 動畫 | Full／Reduced／None、透明 Photo、GIF stop／恢復、皮膚切換、特效取消與完成回呼。 |
| 資源 | 同素材反覆演出、素材／皮膚失效、離房重入、viewport/context 重建與最後退出。 |
| 效能 | 相同 binary/config/素材/解析度/種子/SmartAI 設定比較幀時間 p95/p99、GUI thread CPU、private bytes、GPU 資源。 |

60 FPS 的 16.7 ms 幀預算與操作回饋 p95 < 100 ms 是量測目標；實測值記錄於各次報告。
GUI 與 AI/server 必須分開計量；不得以關閉 AI、關閉特效或換成 Release 的結果代替原情境。
保留既有戰報歷史；長局戰報的有界顯示與完整歷史載入是另行設計的工作。
