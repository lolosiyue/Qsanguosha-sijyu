# M1：50 人 UI／焦點實作檢查點

日期：2026-09-19。依據 [UI 路線圖 §5](ui-roadmap.md#5-m1超大局50-人模式--限-p2) 與 [協議審計](large-room-ui-protocol-audit.md)。
本頁記錄來源實作與待驗收事項；不是完整 50 人玩法或對局驗收報告。首批僅完成 UI／協議支援；使用者指出尚缺模式後，已確認並補建 **`50p` 身份局模式**。第二檢查點 GUI／server／core 建置及模式 focused 驗證已通過，模式已編入新版執行檔；實際 GUI 操作與完整對局仍未驗收。

## 原生外觀修正（進行中）

使用者指出首版以另一套 QWidget 面板、文字按鈕及下拉選單取代原生角色框，外觀與操作不符合要求。**首版 UI 不予驗收**；以下第一／第二檢查點的結果僅對當時來源成立，不能當成修正後 GUI 的通過證據。

| 項目 | 目前狀態 |
| --- | --- |
| 移除替代介面 | 已移除大局專用 QWidget 總覽、結算面板與候選／票數下拉選單；原有輔助選單保留。 |
| 原生元件 | 恢復原生 Photo 顯示與點選，沿用既有座位瀏覽及 Dashboard；不重建牌張或選取草稿。增量建置已通過，仍屬中間實作。 |
| 原版戰報 | 修正 50 人布局把戰報預設隱藏及停用選單的回歸：橫向保留右側常駐 ClientLogBox，直向由選單開啟同一元件；不建立替代戰報。修正後驗證見下方。 |
| 雙焦點與大局導航 | 須以原生角色框接回；現階段未完成，不能以座位瀏覽代替完整 M1。 |
| 模式／協議 | 保留 `50p` 身份配置、活動結算堆疊、獨立回應焦點與 replay／重連狀態契約。 |
| 驗證 | 原生角色框版面 focused executable 通過，涵蓋不重疊與全席可達。GUI 重測失敗，不能把幾何測試當成實際對局驗收。 |

## 2026-09-20：進房／F8／原生焦點修正（來源檢查點）

> 下列「移動原生 Photo 至焦點位」已被使用者否決；其後已按 M1 重新接入獨立縮略列與結算投影，見下一節。先前建置只證明被否決版本可編譯，不適用於新來源。

使用者回報進房背景錯亂、拖動席位後才出現戰報、F8 會令戰報消失，以及看不到目前出牌／回應角色。先前角色框可見的回報不能代表這些操作已通過。

- 橫向大局改用原有 skin 計算背景、Dashboard、身份欄、ClientLogBox 及聊天欄；只在對手區配置席位瀏覽與兩個原生 Photo 焦點位置。
- 原先大局路徑未初始化 `m_logSizeWithChat`／`m_logSizeWithoutChat`，但 F8 及全機器人房的自動隱藏聊天仍使用它們。現在沿用原生 `applyLayout()` 初始化，F8 與選單聊天入口共用原有切換方法。
- 焦點由既有 GameViewState 的單人回應名單、最內層結算及當前回合角色投影，保留原生 Photo 的框線、倒數和選取；巡覽席位不強制跳頁。自己沿用 Dashboard，多人廣播等待不任選一人當作唯一回應者。
- 本批僅完成來源及靜態檢查，未建置、未啟動 GUI、未執行 focused executable／CTest／完整對局；實際外觀、進房立即顯示、F8 往返及角色焦點仍待驗收。
- 後續使用者授權「做一次，看看」：Debug `QSanguosha` 目標增量建置成功，已開啟新版供人工查看；尚未取得 50 人進房、F8 往返及角色焦點的人工驗收結果，未執行自動對局或 CTest。

## 2026-09-20：M1 原版風格修訂（來源檢查點，尚未建置）

使用者要求嚴格依照 UI 路線圖 §5.3–5.4，並貼近原版 UI。**禁止把當前回合玩家或主公抽離座位當作結算面板**；回合標記、回應焦點與活動結算需分開。

| 區域 | 本批來源行為 |
| --- | --- |
| 外觀／右欄 | 沿用角色圖片、`GraphicsBox` 對話框樣式、`QSanButton`、原有 Dashboard／背景／戰報／聊天；新呈現位於原生 graphics scene，不使用另一套 QWidget 清單取代牌桌。 |
| 全局縮略列 | 包括自己；按座次排序後以自己為環起點，死亡不改序。小圖僅畫體力、手牌、死亡、自己與回合／結算／回應標記；身份載入／換將以外，只有四類值變化才使縮略圖更新。候選／已選標記是獨立子圖元。 |
| 巡覽 | 滾輪、拖曳與方向鍵連續捲動；無自動翻頁。保留跳回當前焦點／自己，Tab／Shift+Tab 只巡覽當下事件相關角色。聊天代理元件與 Ctrl／Alt／Meta 組合鍵不攔截。 |
| 結算面板 | 由最內層活動結算的 affected／actor／source／targets 投影主次角色及關係句；回應名單獨立標註。不以回合玩家或最近戰報冒充結算。跟隨／鎖定狀態明示，鎖定時保留觀察對象，當前結算文字仍獨立更新。 |
| 目標選取 | 全局列不排序；候選列預設只看合法，提供全部、座次／距離／鄰近／合法性排序。滾輪或方向鍵循環合法目標，hover 預覽；點選候選／Enter 加票，Delete／已選角標／草稿列撤回。所有操作經既有 presentation intent 的 generation／revision／request 驗證。 |
| 詳細卡 | 點縮略位或焦點卡開啟原生樣式、可捲動的 L1 詳細卡；體力、手牌數／上限、狀態、距離、公開身份、裝備、判定、技能、標記與牌堆只讀 GameViewState 的接收者可見投影。 |
| 驗證邊界 | 已同步原有版面 fixture 的新幾何契約；只完成來源與靜態檢查。未 configure／建置／focused executable／GUI／CTest，未宣稱完整 M1 驗收。小尺寸可讀性、素材觀感、實際選取／鎖定／重連仍須下一檢查點驗證。 |

來源：`src/ui/large-room-overview.*`、`RoomScene::applyResponsiveLayout`、`RoomLayoutEngine::computeLargeRoom`。

使用者其後授權「建置和開啟」：本次 configure 與 Debug `QSanguosha` 目標增量建置成功，已開啟新版供人工查看。未執行 focused executable／CTest／自動完整對局；外觀與互動仍待使用者確認。

首次人工進房失敗：`QSanButton::setSize()` 的背景 pixmap 斷言被觸發。大局按鈕工廠對無圖片建構子先呼叫非零 `setSize()`，再呼叫 `setActionText()`，順序錯誤。已改為先設定文字按鈕模式再設定尺寸，保留原斷言；GUI 增量建置成功並重開，實際進房仍待確認。

## 選將順序修正（已建置，GUI 未通過）

使用者要求主公先選，其他玩家在主公完成後一起選剩餘武將。現有 `chooseGenerals()` 已先詢問主公，再分配剩餘候選並以 `doBroadcastRequest()` 批次發送；不同控制者的請求先送出、再收集回覆。同一控制者兼控多席仍沿用既有逐請求語義，不能以平行發送覆蓋該控制者的待答請求。

發現 `startGame()` 原本把「公開一席武將／體力」與「初始化該席 AI」交錯執行，耗時初始化會讓畫面呈現逐席亮出武將。現已拆成先公開完整名冊、再初始化 AI；沒有更改候選分配、主公優先、預選技能或選將答覆處理。`git diff --check` 與增量建置通過；不能宣稱已在 GUI 驗證。開局長時間沒有 `GAME_STARTED` 的效能原因仍未定位。

## 原生介面重測與戰報修正

- `builds/large-room-native-checkpoint-20260919/`：GUI／server／core 增量建置成功；直接執行 `qsanguosha_core_tests --suite room-layout-engine`，exit 0，未執行 CTest。
- 使用者要求停止舊局並重新 GUI 測試。舊 SmartAI 局沒有 GAME_OVER，停止後 client／server exit 3；無殘留程序，連接埠釋放。記錄在 `builds/large-room-live-20260919-bounded/`，不得視為完整對局通過。
- `builds/large-room-gui-retest-20260919/`：以 `--ai off`（TrustAI）隔離 GUI，並非 SmartAI 完整驗收。已回報 50 席、49 個 Photo 與 Dashboard；client 在選將階段後以 `STATUS_ACCESS_VIOLATION` 退出，沒有 client GAME_STARTED／GAME_OVER。Server 有 game start 記錄，關閉 exit 0，無殘留程序、TCP 釋放。
- 戰報修正檢查點：`builds/large-room-log-checkpoint-20260919/`。GUI／core 增量建置成功，版面 focused executable exit 0；涵蓋橫向常駐戰報與角色框／桌面／手牌區不重疊、直向開啟戰報不移動座位。尚未重啟 GUI 或完整對局。選將後崩潰需另外界定原生除錯範圍。

## 限定原生診斷（30 分鐘授權／一次修復後重測）

證據：`builds/large-room-crash-20260919/diagnosis.md`、`postfix/network-ui-smoke-50p-summary.json`。

| 項目 | 結果與限制 |
| --- | --- |
| 舊崩潰轉儲 | 確認 QString 經位址 0x20 存取違規。舊應用程式 PDB 已被後續建置取代；強制載入新 PDB 的 RoomState／updateStatus 路徑只作線索，不能當成匹配符號的確診堆疊。 |
| 最小修正 | `RoomScene::updateStatus` 在刷新技能按鈕時改讀 Client 自有 RoomState，避免 GAME_START 註冊前解參考空的 Engine 房間狀態。GUI 增量建置成功；原先偶發崩潰尚不能宣稱完全排除。 |
| 診斷重現 | 修正前已能抵達 GAME_STARTED，未重現原先崩潰；附加除錯器影響收尾，該次只保留診斷證據，不作驗收。 |
| 唯一修復後重測 | 50 席／49 Photo／Dashboard、GUI GAME_STARTED（69.8 秒）有紀錄；180 秒逾時，沒有 play_phase、GAME_OVER 或勝方。TrustAI 隔離配置，不代表 SmartAI 完整對局。 |
| 遲遲未出牌 | responder 連續接受 114 次換手牌。測試設定只有 EnableLuckCard=false，實際流程使用 LuckCardTimes（預設 -1 不限次）；已補 LuckCardTimes=0，Python AST／diff 檢查通過，未另開第二次重測。 |
| 畫面／退出 | 失敗截圖僅有暗色背景與選單／捲軸，不能證明原生角色框或戰報的實際可見性。逾時退出有 Qt 斷言；server 被終止，client 一度殘留後消失，TCP／WebSocket 最終釋放，乾淨退出仍 FAIL。 |
| 未完成 | 原生雙焦點、OS 滑鼠／鍵盤操作、實際右側戰報可見性、重連／seek、完整對局／勝方／正常退出。 |

## noluck 開局／退出修正檢查點（2026-09-20 更新）

針對 `builds/large-room-gui-noluck-20260919/` 的修正與未解決事項，見該目錄的 `correction.md`。原始日誌與 FAIL 結果保留。

| 項目 | 修正／限制 |
| --- | --- |
| GUI 退出 | 正常 GUI 主路徑與 network smoke 先完成結果記錄，再同步銷毀主視窗與 QApplication；Engine 脫離視窗父物件，保留原有程序生命週期。 |
| UI 析構順序 | 先卸載首頁 QML，再分離輔助文件視圖、銷毀 RoomScene，最後銷毀頁面／OpenGL viewport；不再依賴事件迴圈結束後的 deleteLater。皮膚工廠在場景之後釋放。 |
| OpenGL 線索 | 桌面啟用 AA_ShareOpenGLContexts，舊主路徑未銷毀 QApplication。Qt 共用 context／靜態 texture cache 的清理次序與斷言相符，但缺少匹配轉儲，仍須實測證明。 |
| 開局成本 | 已以匹配 PDB 採樣定位無接收者通知編碼、AI 關閉時仍建快照、事件表重掃、全場距離與標記後的重複 PlayerUIState 計算。保留規則效果、座次及操作時限，修正通知／快照前置判斷、事件候選查詢及介面更新合併。 |
| 首次出牌 | 以 first_play_request／first_play_reply 分別量測玩家自己的首回合與回應，不把 GAME_STARTED 或 AI 首張牌當成玩家已能出牌。最新逐次數據見 `builds/large-room-fix-20260919/report.md` 與 `comparison.json`。 |
| 收尾證據 | 多次 client timeout exit 11 未重現原來的 QML／OpenGL／QString 斷言。診斷定位停止後的標記／Lua 介面重算；最終 `ui-batch-release` 在原有 10 秒上限內以 0.811 秒 graceful exit 0，無 orphan、port released。 |
| 最新計時 | 同種子 Release：first_play_request 150.998 → 123.514 秒，first_play_reply 123.572 秒。全場首張牌約 30.5 秒；玩家自己的等待仍約兩分鐘。前 24 次出牌角色、目標與牌張順序一致。 |
| 驗證 | Debug／Release 增量建置及通知、AI 決策、牌表、事件派發、技能登錄、分代 GC、介面合併與操作前通知順序 focused 已通過。沒有執行 CTest。原始 FAIL 保留，完整對局／勝方、原生雙焦點及 OS 操作仍未驗收。 |

## 首版來源（UI 已被否決，僅保留歷史）

> 後續真實重測 `builds/large-room-gui-noluck-20260919/`：使用者已授權本次對話 GUI 重測不再逐次確認，並要求停止 Computer Use。此局 LuckCardTimes=0，沒有重複換牌；GUI GAME_STARTED 為 84.376 秒，但 300 秒內僅收到指派／選將兩種請求，未覆蓋 play_phase，也沒有 GAME_OVER。使用者現場確認角色框、手牌、右側戰報均可見；Windows 擷取工具失敗，不冒充畫面驗證。先前 OS Tab 操作能聚焦版面按鈕、會跳過座位捲軸，因此補上捲軸 StrongFocus；第一次連結遇 EXE 鎖定，程序退出後重新連結成功，新版鍵盤行為尚待驗證。逾時退出再次出現 Qt OpenGL／QString 斷言，使用者提供的截圖已存為 user-shutdown-assert.png；測試 client／server 最終均消失、TCP 7939 與 WebSocket 8058 釋放。退出仍 FAIL；完整對局、重連／seek 未完成。

| 區域 | 已接入的來源行為 |
| --- | --- |
| 桌面入口 | 其他玩家 20–49 人時，自動使用 `LargeRoom` profile；一般 20 人以下、Android／XP 不啟用此大局介面。桌面開房的「身份模式」新增 `50p`；Android／XP 選單不提供此模式。 |
| 模式規則 | `50p` 固定 50 席，1 主公／23 忠臣／25 反賊／1 內奸；沿用 `identity` 勝負與獎懲策略、原有主公福利及座次設定。明確保存完整身份字串，不存取僅支援至 20 人的舊身份表。牌堆與將池沿用房間設定；初始配比未經平衡驗證。 |
| 布局 | 原生 Dashboard 留在底部；縮略列、結算面板、桌面處理區分開。經典座次表仍保留上限保護。 |
| 縮略列 | 以自己為錨點保持座次環序，死亡不重排；沿用角色圖片，顯示體力、手牌數、回合、回應、結算及已選票數。持續保留同一批 widget，只有顯示值改變才更新文字／圖片／框線。 |
| 候選與草稿 | 合法候選預設過濾、可切全部；候選可按座次或距離排序，總覽不排序。滾輪／方向鍵巡覽，Enter／右鍵加票、Delete 撤回；獨立已選清單可操作離屏目標。全部 intent 回到現有 `DesktopGamePresentation` 驗證。 |
| 焦點 | 結算層由伺服器更新；檢視可預覽或鎖定；自動更新不呼叫鍵盤 `setFocus`。Tab／Shift+Tab 巡覽事件相關角色，Escape 回到候選控制項。 |
| 回應 | `MOVE_FOCUS` 名單、倒數與所屬結算 ID 獨立保存；空名單按既有語義表示存活者。結算層結束清掉該層等待狀態。單人 overload 保留真正的詢問 command。 |
| 方向 | schema 2 必須攜帶 bool `play_order_reversed`；schema 1 可讀，但方向標示未同步，不能當成權威正序。 |
| 特效 | 大局以 session override 關閉裝飾特效；不覆寫設定，離房恢復所選 profile。必要狀態以文字、票數與框線呈現。 |

## 活動結算契約

`S_COMMAND_RESOLUTION_STATE = 134`，schema 1，Room → Client notification，錄影保存。

| 欄位 | 語義 |
| --- | --- |
| `phase` | `begin`／`update`／`end`／`reset` |
| `frames` | 權威活動堆疊，外層至內層；每個通知完整取代，不是待客戶端累積的 push/pop。 |
| frame `id`／`parent_id` | 不經浮點轉換的正十進位字串；根層 parent 為空，其餘必須指向前一層。 |
| `kind` | `card`／`effect`／`damage`／`recover`／`judge`／`dying`／`skill` |
| `actor`／`source`／`affected`／`targets` | 分離行動者、來源、目前受影響者與已公開目標；缺少資訊時保持空值。 |
| `card_name` | 已由原有公開 provenance 披露的牌名；不含私人手牌、技能 instance 或詢問 body。 |

來源接點：`Room::useCard/cardEffect/damage/recover/judge/enterDying` 的作用域守衛；公開技能通知啟用 `RoomThread` 的延遲公開 scope。不對技能候選掃描廣播，也不公開私人戰報。公開 `#UseCard` 補入最終目標，`#NullificationDetails` 不用來猜結算父子關係。

作用域返回與例外離開時移除自身及子層；Game Start／Game Over 明確 reset。重連在 `STATE_SYNC end` 前送完整快照及可用的回應焦點，指定倒數扣除已經過時間。Replay seek 在重放前清掉活動狀態，舊錄影顯示資訊不可用。共用 reducer、桌面 callback、Web reducer 及 protocol flow matrix 一起更新；TUI／其他共用 ClientCore 消費端保存資料，不新增大局 UI。

## 第一檢查點：UI／協議驗證狀態（不含後補模式）

| Gate | 本批結果 |
| --- | --- |
| `git diff --check` | **PASS**：本批來源與文件。 |
| Web protocol ID／欄位靜態對照 | `node web/scripts/check-protocol-sync.mjs` 通過，111 commands／29 reply maps。 |
| 新增測試來源 | 21／30／50 人及 20 人邊界、超界與非桌面 opt-in；生命週期取代／重複送達／父層返回／reset；非法 parent、非字串 ID、額外私人欄位拒收；方向 schema 相容。 |
| C++ 增量建置 | **PASS**：Debug `QSanguosha`、`qsanguosha_core_tests`、`qsanguosha_protocol_tests`、`qsanguosha_game_presentation_tests`，同一次序列建置成功。 |
| 版面 focused executable | **PASS**：直接執行 `qsanguosha_core_tests --suite room-layout-engine`，exit 0。涵蓋 21／30／50 人與 20 人邊界、三種桌面尺寸、區域不重疊及超界拒絕。 |
| 協定 focused executable | **PASS**：`--suite flow-inventory --check artifacts/protocol-v2-flow-matrix.json`；146 flows、0 implicit passthrough、0 unclassified，產物與登錄逐位元組一致。 |
| 呈現 focused executable | **PASS**：直接執行 `qsanguosha_game_presentation_tests`，43 項 PASS、exit 0；涵蓋巢狀生命週期、重複通知、父層返回、回應焦點清除、非法欄位／ID／父子關係拒收及重連 reset 投影。 |
| Web 型別檢查／reducer 測試 | **PASS**：`tsc --noEmit`；`vitest run tests/reducer.test.ts`，16／16。 |
| 50 人模式／開房入口 | 第二檢查點建置與模式 focused 驗證 **PASS**；實際開房選單操作尚未驗收。 |
| GUI 50 人合成資料場景 | **NOT RUN**；可獨立驗證 UI，但不能替代真實模式。 |
| 50 人實際操作、重連／seek 播放、完整對局／GAME_OVER／乾淨退出 | **NOT RUN**：須先驗證新模式能開局，再製作真實對局與錄影驗收資料。 |
| 遠端 CI | **NOT RUN**。 |

本輪已授權增量建置 GUI 與受影響的 core／protocol／presentation targets，直接執行既有 suite 的 `room-layout-engine`、`flow-inventory` 及呈現契約，加 Web reducer 單檔檢查；使用者允許超過 60 秒。不使用本地 CTest；不得以這些結果代替 GUI 或完整對局。證據保存在 `builds/large-room-ui-checkpoint-20260919/`。

首次建置在 MSBuild FileTracker 初始化時遇到沙箱存取拒絕；相同目標取得必要權限後建置成功，沒有修改來源來繞過環境問題。建置保留既有武將 bool 比較及第三方 FreeType PDB 缺失警告。

## 第二檢查點：補建 `50p` 身份局

| 項目 | 來源狀態／驗證範圍 |
| --- | --- |
| 模式登錄 | `Engine` 登錄 50 席、完整身份配置並加入「身份模式」群組；現有開房選單與 server CLI 沿用登錄表。 |
| 模式 focused 案例 | **PASS**：`qsanguosha_core_tests --suite large-room-mode`，exit 0；覆蓋選單群組、角色數量、勝負／獎懲策略、49→50 席滿房、實際 `Room::assignRoles` 分配及只公開主公。未啟動 AI／對局。 |
| Runner 相容 | **PASS**：直接呼叫兩個既有測試函式，確認 `50p` fallback 經假子程序可通過，未登錄 `30p`／`49p`／`51p` 拒收；未啟動真實對局。本機沒有 pytest，因此不宣稱 pytest runner 通過。 |
| 建置 | **PASS**：`QSanguosha`、`qsanguosha_server`、`qsanguosha_core_tests`；測試私有方法存取修正後只重建 core。 |
| 真實 server 登錄 | **PASS**：新版 `debug/qsanguosha_server.exe --list-game-modes` exit 0；唯一 `50p` 列的人數為 50，顯示已確認的身份配比與實驗性標示。 |
| 靜態檢查 | **PASS**：本批 `git diff --check`、Python AST 語法解析。 |

使用者已授權第二檢查點，沿用可超過 60 秒的設定；未執行本地 CTest。證據保存在 `builds/large-room-mode-checkpoint-20260919/`：`build.log`、`build-core-retry.log`、`mode-contract.log`、`runner-direct-retry.log`、`server-modes.log`。

本次發現並修正：測試改經既有 `RoomTestAccess` 呼叫私有身份分配入口；runner 查詢模式清單時明確使用 UTF-8，避免 Windows CP950 解碼中文模式名稱失敗。修正後受影響檢查皆已重跑通過。

模式可開局、實際 UI 操作、完整對局與平衡性是不同結論；不得把 50 席身份分配測試視為完成一局。

尚待動態驗收：多層無懈、傷害轉移與瀕死求桃、技能取消／回合中斷、深層重連、重複及向後 seek、20→21 動態加入、多票草稿中自動焦點切換、高 DPI／窄窗／關特效的資訊完整性。外部擴展若跳過標準 Room／技能公開入口，其自訂結算不能假稱已被完整覆蓋。

2026-09-20：依使用者要求移除本輪新增的效能測試案例、診斷插樁與測試產物；正式 SmartAI、引擎與退出修正保留。測試已停止，沒有重開對局。

2026-09-20：依人工截圖回饋，縮略列與結算焦點改用原生 Photo fullskin，共用體力／手牌數繪製；補上水平捲軸、字寬排版與 builds/sanguosha.ts 翻譯。靜態檢查通過；本批建置進行中，GUI 外觀仍待人工驗收。
2026-09-20 本批檢查點：Debug QSanguosha 增量建置與 lrelease 通過；已開啟 GUI 供人工查看，未執行 CTest 或完整對局，50 人房內外觀尚未驗收。
