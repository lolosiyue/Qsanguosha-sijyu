# 房間自適應版面實作與驗收

基線：`a2ad841`（2026-09-16）。本計畫只改 Qt 桌面／Android 的呈現與幾何，
不修改協議、遊戲規則或伺服器。XP 維持經典橫向；Web／TUI 不在本批改版。

## 已鎖定的產品決策

- 桌面與 Android 在各 profile 完成驗收後自動選擇版面。
- 直向空間足夠時保留座次環，不足時使用保持環序的席位帶。
- 點擊保留選取，長按或獨立詳情按鈕開啟 Inspector。
- 手牌先在可辨識／可點選下限內壓縮，再於手牌區水平捲動。
- 單手偏好為 None／Left／Right，預設 None；直向動作區仍固定底部。
- Android 以 APK、模擬器與姿態注入為交付門檻；真機另列，不能以模擬器代替。

## 交付順序

| 檢查點 | 工作 | 完成邊界 |
| --- | --- | --- |
| PR 1–2：橫向基線 | 純值 `RoomLayoutEngine`、現有 Photo fitting／八區排列／資訊欄幾何，以及 RoomScene／FitView 接入 | 原幾何一致性、增量建置與 focused 案例通過；GUI 截圖另列 |
| PR 3：共用呈現與 Inspector | 開放既有 DesktopGamePresentation 給多個即時視圖；加入 QWidget RoomOverlayHost／Inspector | 面板關閉時 Inspector 仍更新；原文字快照保持手動刷新；不洩漏隱藏資料 |
| PR 4：Compact | Portrait／Landscape、Dashboard 分區、席位帶、手牌局部捲動、Log／Chat 面板 | 旋轉／resize 不丟草稿；修訂交付基線後才解除 Android 橫向限制 |
| PR 5：單手模式 | 左右配置、合法目標快捷列 | 共用 GameActionModel／intent 路徑；補足多票語義；48dp 觸控目標 |
| PR 6：Medium／Expanded | 同一 Inspector 的按需側欄／常駐分欄 | 切換保留玩家、草稿、焦點；Log 與 Chat 分開 |
| PR 7：Foldable | Android WindowManager bridge、Book／Tabletop 與遮擋 | 姿態注入及模擬器通過；缺少資訊時正常使用一般 profile |

目前的預設生產版面仍為 LegacyLandscape。PR 3–7 已接入手動預覽入口，
尚未通過新一輪建置、GUI 或 Android 驗收，不能視為自動版面已正式交付。
每個檢查點先完成修改與靜態核對，再取得該檢查點建置／執行驗證的明確許可。
本文件不授權本地 CTest、長測、提交或推送。

### PR 1–2 原始碼檢查點（2026-09-16）

| 項目 | 狀態 |
| --- | --- |
| 純幾何與接入 | 已寫入來源；RoomScene 保留量測／物件副作用，FitView 明確傳入可用 viewport |
| 回歸案例 | `room-layout-engine` suite 已併入既有 `qsanguosha_core_tests`，直接執行 PASS（3.984 秒）；既有 `photo-layout-fit` PASS（0.344 秒） |
| 靜態比對 | 已核對座位順序、特殊模式、整數截斷與 Dashboard 量測順序 |
| 建置／focused | 已獲授權；增量 configure、Debug QSanguosha／qsanguosha_core_tests 及兩個直接 suite PASS |
| GUI／Android／完整對局／遠端 CI | NOT RUN |
| PR 3–7 | 下節記錄後續原始碼檢查點；上列 PR 1–2 的通過結果不適用於這批新修改 |

本次驗證基線為 `8b80520` 加未提交版面修改；來源在建置與 focused 執行期間 SHA-256
一致。完整證據存於 `builds/room-layout-checkpoint-20260916/validation.md`。
本地未執行 CTest。純幾何案例通過不能代替 GUI、Android 或完整對局驗收。

獲准後的限定驗證：以現有 VS 2026／Qt 6.11.1 preset 增量 configure，僅建置
`QSanguosha` 與 `qsanguosha_core_tests`，再使用同套 Qt PATH 直接執行
`--suite room-layout-engine` 和 `--suite photo-layout-fit`；每項 focused 執行以 60 秒為上限。
不清理建置樹，不執行本地 CTest，不啟動完整對局。

### PR 3–7 原始碼檢查點（2026-09-16）

| 階段 | 本批來源實作 |
| --- | --- |
| PR 3 | `DesktopGamePresentation` 以 QObject 生命週期追蹤多個即時消費者；`RoomOverlayHost` 提供同一個玩家詳情抽屜／側欄。文字快照仍只手動更新。Inspector 只讀 `GameViewState` 的 recipient 投影、已披露狀態、技能、標記及可見牌區 |
| PR 4 | 純幾何新增 Compact Portrait／Landscape、Medium、Expanded；Photo 使用未縮小 Small 與實際 UIScale，逐張核對矩形後才保留座次環，否則使用獨立席位帶。Interaction Zone 以持續存在的 QWidget 呈現身份、提示、手牌、技能及目標，Dashboard 繼續持有原草稿 |
| PR 5 | None／Left／Right 偏好存於 `UI/RoomHandedness`；固定底部操作鍵、合法目標及加減票使用原意圖路徑。按下時擷取 generation／revision／request，提交時重新驗證；虎牢重整倒數不作目標票數 |
| PR 6 | Expanded／Book 常駐同一 Inspector，其他尺寸按需開啟；玩家、原聊天草稿與持續存在的控制項不因切換 profile 重建。Log／Chat 為分開視圖，使用原 QTextDocument 與原聊天傳送入口 |
| PR 7 | WindowManager Java listener → JNI → Qt thread → FitView → 純幾何；處理 separating／occluding、零寬 crease、平攤雙屏、Book／Tabletop、小 pane 回退、失效回呼及鍵盤遮擋。缺少姿態時使用一般 profile |

新入口為 **View → Player Details**（Android overflow 同一 action），或牌桌左上角
選單。**Responsive preview** 只在明確開啟時啟用本批 profile；不持久化此開關。
Android 預覽期間允許旋轉，關閉後還原原先 orientation；Manifest 的正式橫向限制
仍保留，待 Compact GUI／APK 驗收後另行解除。XP 不編入 Overlay／WindowManager。

手牌按鈕以原模型的穩定 ID 同步選取，寬度在可讀下限內壓縮，再局部水平捲動；
互動內容可局部垂直捲動，确认／取消／結束動作列保持底部。觀星等需要排序的請求
繼續使用既有 Game Controls 的排序操作，不建立第二份排序草稿。原有專用技能對話框
仍保留，沒有新增規則或協議編碼。

預覽中的姿態可用 `RoomWindowPosture::inject()` 注入；桌面啟動前亦可設定
`QSAN_WINDOW_POSTURE=book,400,0,420,900,1,1`，格式為
`mode,left,top,right,bottom,separating,occluding`，bounds 是 **window-relative Qt 邏輯座標**。
FitView 才轉換為 viewport 座標並裁去系統安全區；DPR 只在 Android pixel→Qt 邊界換算一次。

| 本批 gate | 狀態 |
| --- | --- |
| 原始碼與靜態審查 | 完成來源整合；新增案例併入既有 room-layout-engine 與 client-core presentation 合約 |
| 新來源建置／focused executable | PASS：增量 configure、Debug 三個 target 與三項直接 focused 驗證；證據見下文 |
| Windows 原生 GUI 完整對局 | 未通過：首次開局的隱藏手牌空指標已於 5097690 修正；重跑 03_1v2 約 11 分 43 秒後由使用者要求停止，沒有 GAME_OVER／winner，關閉時另有 Qt6Qmld.dll 0xC0000005。server 正常退出、連線埠釋放；未啟用 Responsive preview |
| Android APK | PASS：Android 專用標頭改用 Qt6 的 qcoreapplication_platform.h 後，原生／Java／Debug APK 建置成功 |
| Android 媒體與裝置 | 取消資源雜湊與聲畫掃描後，沿用媒體的內容準備實測 6,924 ms，缺 8 張圖片不擋連線；03_1v2 仍在 GAME_STARTED 前 AudioTrack SIGSEGV，完整對局未通過。首次匯入新耗時未重測，見 android-extension-runtime.md |
| 姿態注入、XP、遠端 CI、真機 | NOT RUN |

本檢查點經使用者授權，已完成增量 configure／建置 `QSanguosha`、`qsanguosha_core_tests`、
`qsanguosha_game_presentation_tests`；直接執行 `room-layout-engine`（0.344 秒）、
`photo-layout-fit`（0.046 秒）及 `qsanguosha_game_presentation_tests`（0.157 秒），皆 exit 0。
建置發現並移除 Overlay 整數寬度的多餘 `qRound()`；修正後來源在重建至 focused 完成期間
SHA-256 一致。保留既有 FreeType 缺少 PDB 警告，未修改第三方程式庫。
基線為 `debug@8b80520` 加本批未提交來源，完整紀錄在
`builds/room-responsive-checkpoint-20260916/validation.md`。未執行本地 CTest；
不以這些結果代替 GUI 旋轉、單手、多票、焦點、重連或 Android 證據。

後續執行紀錄分別保留在 `builds/gui-complete-game-20260916-02/summary.md` 與
`builds/android-content-performance-20260916/no-hash-run/summary.md`。
PR3–7 授權的來源／建置／focused 檢查點已完成；Responsive preview 的互動與折疊姿態
驗收仍待完成，不把一般橫向對局或資源啟動成功記為該項通過。

## 架構邊界

- Layout Engine 僅接收 Qt Core 值型別與 skin 尺寸，不讀取 QGraphicsItem、Config、
  Client、ServerInfo 或平台物件；RoomScene 負責量測、套用及原有 UI 副作用。
- FitView 已透過 viewport margins 排除系統安全區與鍵盤，送入引擎的是可用 viewport。
  LegacyLandscape 不再次扣除 margins；邏輯場景的 skin clamp 與裝置材質縮放分開。
- Dashboard setWidth 後才量測頭像的 scene bounds，保留既有 UIScale 對資訊欄的影響。
  不以未縮放的 skin 高度取代這份實際量測。
- `GameViewState`／`GameActionModel`／`GameEventStream` 已存在。Inspector 是薄轉接，
  僅補合法可見欄位；不可再建立第二份狀態、選取草稿、規則引擎或回覆編碼器。
- 既有 DesktopGamePresentation 只在操作面板可見時排程刷新；PR 3 改為任一即時視圖
  可見時刷新及發布。所有操作保留 generation／revision／request ID 防護。
- PR 5 已補多票呈現與加減票 intent，沿用原選取草稿；呈現合約已通過，GUI 操作仍待驗收。

## 後續 profile 初始策略

使用扣除安全區、未扣鍵盤的 Qt 邏輯尺寸選擇骨架；鍵盤只調整可用區。

| Profile | 條件 |
| --- | --- |
| ExpandedSplit | 寬至少 1000、高至少 600；320 寬側欄與 16 間距後主區至少 640 寬 |
| Medium | 未符合 Expanded，短邊至少 600 |
| CompactPortrait | 其餘高大於寬的視窗 |
| CompactLandscape | 其餘橫向／正方形視窗 |

使用 24 邏輯像素切換緩衝；實際字體／UI 最小尺寸放不下時退回按需面板。
自適應座位最低使用未縮小 Small Photo，放不下才改席位帶；經典基線保留原 scaled Small。
Interaction Zone 初始目標為可用高度 38%，受內容最小尺寸與 400 邏輯像素上限約束；
鍵盤造成不足時保留動作／提示，其他區域局部捲動。

## 驗收矩陣與證據

- 場景尺寸：1280×720、940×530、844×390、390×844、430×932、768×1024、
  1024×768、1920×1080，以及門檻兩側、非零 origin、空 viewport。
- Regular：2、4、8、10、20 人；特殊模式僅使用合法人數，覆蓋各自身座位、開局前後。
- 現有 PhotoLayoutFit 案例的 940×530 是牌桌可用區，不能當作整個 viewport。
- 幾何基線誤差小於 1e-4；整數 widget 幾何、Photo tier、座次與原有取整一致。
- 避免陣列越界：零座位、無效自身座位、超過 20 人不套用無定義座位表。
- 後續另測多視圖同步、隱私、請求替換、超時、旋轉、長按、重連、控制角色切換、
  安全區／鍵盤、DPR／UIScale、鉸鏈及原有鍵盤／無障礙行為。
- 分別記錄實作、靜態檢查、建置、focused、GUI、完整對局、遠端 CI、真機。
  新來源未建置時不得引用 a2ad841 的舊測試結果作為通過證據。

完整對局使用 03_1v2 作代表，其他人數測版面／互動；需有 GAME_OVER、winner、
乾淨退出與資源釋放證據。額外原生引擎缺陷另列範圍，不擴入版面重構。
