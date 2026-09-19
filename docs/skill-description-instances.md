# 局中技能說明 v2：實例、狀態、使用情況與附加效果

第一批將既有實例資料接到桌面主／副將頭像 tooltip；沒有新增網路欄位，也不在顯示時執行 Lua 規則。原有正文、失效灰字、Oracle、相關技能與死亡後原生技能說明保留。

## 第一批顯示契約

| 資料 | 呈現與邊界 |
|---|---|
| 實例 | 以目前 Player 的技能名稱＋instanceID 查詢；畫面簡寫 `#ID`，技術資料保留 owner／skill／instanceID |
| 正文 | 同名技能依最終正文分組；相同正文與 Oracle 只顯示一次，下方各列實例資料；不同正文分開 |
| 來源 | 原生／後天獲得／附加技能／關聯附屬，僅實際雙將按 bindHead 加主將／副將；parentRef 僅列已記錄身分，不推定效果原因 |
| 私有 state | `getSkillDescription(viewer)` 僅在 viewer 指標等於 holder 時顯示該本地副本已收到的資料；原無參數 API 保留且不顯示私有 state |
| 修正 | 公開 correctState 與 amountOverride 放次要技術區；amountOverride 明標絕對值，不推定摸牌／傷害等語意 |
| 技術資料 | HTML 跳脫身分與 JSON state，避免資料變成富文字標記 |
| 失效 | 保留既有技能名稱層級的有效性快取與灰字；不將其宣稱為逐實例權威狀態，不標示「現在可發動」 |
| 未持有相關技能 | 保留青色參考說明，不生成實例身分、state 或修正資料 |
| 不可見實例 | 不列正文或技術資料；沒有實例時的死亡／legacy 說明不虛構實例 |

## 文字替換優先序

| 步驟 | 規則 |
|---|---|
| 1 | 保留原本模式描述 `:skill_p` → 一般描述 `:skill` 的回退 |
| 2 | 非空 `changeTranslationskill#ID` 優先，否則回退非空 `changeTranslationskill`，否則用步驟 1；單字元仍作翻譯尾碼，較長字串作完整正文 |
| 3 | 先合併全技能 swap 與單實例 swap；相同 key 由單實例覆蓋，再按 key 字典序替換；避免全技能值先吃掉單實例的佔位符 |
| 4 | 保留原有限定技圖示、類型色彩與花色格式化 |

無 instanceID 的 `Skill::getDescription()` 仍只讀全技能覆寫。真正移除實例時，同時清除該實例的文字屬性及 swap；全技能覆寫保留。`clearSkillInstances()` 也供 client 投影／快照重建使用，因此保留另行同步的文字資料；tooltip 只列目前實際存在的實例，不從殘留文字推定持有或 state。

## 第一批刷新與驗證

Player 的 skill_set_changed／skill_state_changed 以 queued unique connection 更新兩個頭像 tooltip，待 upsert／parent／bind 資料寫入完成才讀取。description swap 與 changeTranslation 動態屬性也發出狀態更新；hover 仍重新取得最新本地資料。

| 檢查 | 目前狀態 |
|---|---|
| 定向靜態審查與 `git diff --check` | 通過；不代表編譯／執行期驗收 |
| `qsanguosha_core_tests --suite skill-description` | 2026-09-20 通過，退出碼 0、38.9 秒、未超時且程序已退出；涵蓋優先序、正文分組、持有者隔離、私有遮蔽、HTML 跳脫、來源、重設、移除、快照、不可見實例與死亡說明 |
| configure／core／GUI 編譯 | 2026-09-20 獲授權後通過；CMake 4.3.1、VS 2026 v145、Qt 6.11.1，Debug 的 `qsanguosha_core_tests` 與 `QSanguosha` 均成功 |
| GUI hover、換將、斷線重連與清潔退出 | 未執行 |

本次 focused executable 設 60 秒硬上限並配置同版本 Qt Debug PATH，使用隔離的 user-data 目錄；未執行本地 CTest。記錄位於 `builds/skill-tooltip-validation/`：`configure.log`、`build.log`、`gui-build.log` 與 `focused-result.json`。

獨立 worktree 的執行期內容由乾淨、與 upstream 一致的外部 extensions `main` 複製 280 份受追蹤 Lua 腳本，逐檔 SHA-256 核對，來源版本及映射清單保留在上述記錄目錄。原 L／H 環境未修改。

GUI 首次建置因 `home_assets.qrc` 引用六個被 Git ignore 的 SVG 缺件而失敗；從原 L 工作區補入 qrc 明列的 sun／moon／home／generals／cards／replays 圖示，逐檔核對後只重建 GUI target，成功產出 `debug/QSanguosha.exe`。這些本地素材仍未納入 Git；乾淨 worktree 要重現建置須提供同等素材。編譯保留既有程式警告及 FreeType 缺少 PDB 的連結警告，沒有修改第三方庫。

## 第二批實作

| 功能 | 契約 |
|---|---|
| 權威 usage | 由伺服器 SkillRuntimeCoordinator 使用既有 getUsageHolder/getUsageTagKey/getMaxUsageLimit 和 UsageReservationLedger 產出；範圍為輪／回合／階段／整局，預留與已提交分列 |
| 完整身分 | PlayerUIStateMessage 的 player_name + skill#instanceID；計數器本身使用實際 holder + usage mark，從不只用 instanceID |
| 共用計數 | 相同 counter 與上限的數字在 tooltip 僅列一次，其餘實例指向該列；不同動態上限保留各自上限並標示共用 |
| 無通用／自訂限制 | None／空摘要不輸出使用情況或原始 usage；Custom 未提供計數不输出使用情況、不猜數字。明確的 mark/state adapter 或作者提供的私有摘要可補齊數字 |
| 逐實例有效性 | 伺服器計算 isSkillInvalid(skill,id)，客戶端只讀 skillValidity。相同正文全部失效才整段灰字；失效實例的資料亦灰字，不另列有效／失效狀態，沒有「現在可發動」標籤 |
| 可讀 state | 靜態翻譯 metadata 指定標題、player/players 類型及列舉值；其餘原始值仍在技術區。已接使命狀態、破圍目標、衛明目標、探幽角色與限定使用情況 |
| 修改 | 有 amount 語意名稱才顯示 base → override；override 是絕對值。Room 數值修改與 correctState 修改 API 同時記錄當時來源，來源不由最終數值反推 |
| 附加效果 | 失效記錄、卡牌限制及技能明確登記的描述另列「目前受到的效果」。探幽已接入來源 ref、作用對象、標記結束條件；不授予技能所有權 |
| 私密性 | usage、原始 state、修改來源與預設效果只給持有者（沿用既有合法控制者的接收範圍）。公開效果須明確宣告 publicEffect；forObserver 移除私人欄位，定向發送不在選好資料後再擴張接收者 |
| 刷新 | 私有 state、技能集、mark、phase、general、卡牌限制等變化只標記 dirty；RoomThread 在事件收尾與互動請求前刷新。預留／釋放／提交也標 dirty。既有完整 UI-state 刷新合併執行，避免重複計算 |
| 生命週期 | UI-state 每次整份取代，缺少新欄位視為空（相容舊伺服器／錄影）；移除／死亡不再產出舊實例。效果 activeMark 歸零時移除描述，之後重用同名 mark 不復活舊來源 |

摘要的上下文是該持有者與實例的當前無目標狀態；顯示的是計次資料，不是選牌、目標、cost 或特定事件的發動判定。規則仍以真正執行時的完整 SkillContext 結算。UI hover 不呼叫新增的規則／Lua 查詢。

### 技能作者 metadata

使用現有翻譯表，不需新增 Lua callback：

```lua
["@skill_name.state.targets"] = "已記錄目標",
["@skill_name.state.targets.type"] = "players", -- 單個角色用 player
["@skill_name.state.choice"] = "選擇結果",
["@skill_name.state.choice.value.draw"] = "摸牌",
["@skill_name.correct.offset"] = "距離修正",
["@skill_name.amount"] = "摸牌張數",
["@reason_name.effect"] = "這項卡牌限制的可讀說明",
```

`@skill-state.shiming_status` 是核心使命契約的共用名稱。所有新增名稱和值都作 HTML 跳脫，不能注入富文字。

對由固定 mark 或單一 instance state 計次的 legacy/custom 技能，可明確宣告（mark 與 state 二選一）：

```lua
skill:setProperty("DescriptionUsageMark", sgs.QVariant("exact_rule_counter"))
-- 或 skill:setProperty("DescriptionUsageState", sgs.QVariant("exact_state_key"))
skill:setProperty("DescriptionUsageLimit", sgs.QVariant(1))
skill:setProperty("DescriptionUsageScope", sgs.QVariant("本局"))
skill:setProperty("DescriptionUsageLabel", sgs.QVariant("限定效果"))
-- 只有規則本身確實有預留 mark 才提供 DescriptionUsageReservedMark。
```

複雜自訂技能可在自己的 private state `__description_usage` 提供 map：`used`、`limit`、可選 `reserved`、`scope_label`、`label`、`counter`；只接受整數計數。counter 會再加 holder 身分，作者負責在同一筆規則變動／重設更新此摘要；没有摘要就不顯示數字。核心保留 `__description_amount` 與 `__description_correct` 作已知修改來源；直接設定新數值及重設會清除不再成立的來源。

### 外部效果描述

C++ 與 SWIG/Lua 共用：

```cpp
room->setSkillEffectDescription(target, "author-scoped-id", "@effect.description",
    sourceRef, "@effect.expiry", "actual_effect_mark", false);
room->removeSkillEffectDescription(target, "author-scoped-id");
```

- id 以作者／來源／實例命名，避免同一目標的不同效果互相覆蓋。
- sourceRef 有提供時必須對應當時存在的來源實例；來源消失不推定其既有效果已終止。
- activeMark 必須已大於零；清零會退役描述。沒有 mark 的效果由技能在真正結束時呼叫 remove，不建立另一套規則計時器。
- expiry 是明確的可讀結束條件，空值就不顯示；不從 mark 名或最終數值推測。
- publicEffect 預設 false；只有效果與来源本來就公開時才宣告 true。
- 記錄放在既有 server map tag `SkillEffectDescriptions`，只經接收者過濾的 PlayerUIState 送出，不廣播原始 tag。

### 第二批檢查點

已補既有 `--suite skill-description`：真正 server usage holder、共用計數、reserve/release/commit、四種範圍 reset、動態上限、自訂 adapter、逐實例失效、JSON 往返、私有／公開過濾、可讀 state、HTML 跳脫、UI 不執行 usage 規則、效果清除與 mark 再利用、失去共享來源及舊協定覆蓋。

| 第二批驗證 | 結果 |
|---|---|
| `git diff --check` | 通過 |
| core／GUI 增量建置 | 通過；SWIG 綁定已重新產生並編譯。記錄：`second-final-build.log`、測試 fixture 修正後的 `second-fixture-build.log` |
| `qsanguosha_core_tests --suite skill-description` | 通過，退出碼 0、33.756 秒、未超時，程序已退出。記錄：`second-focused-retry1-result.json` 與對應 stdout／stderr |
| GUI | 新版已啟動供使用者人工驗收；不宣稱已完成 hover／對局驗收 |
| CTest／完整對局／跨平台 | 未執行 |

首次新增 server 情境測試缺少 AI 事件接收器，在既有 `RoomThread::dispatchTrigger` 的 `getSmartAI()->filterEvent` 發生空指標存取；只修正測試 fixture 的必要初始化，再重建 core target、重跑同一個 60 秒內 focused suite。沒有修改該引擎事件邏輯，亦没有把 BasicAI 測試接收器當作 SmartAI 對局驗收。首次失敗記錄、dump 與修正前本地 PDB 位址對應保留在 `second-focused-result.json`、`second-focused-crash.dmp`、`second-focused-symbol.txt`。

以上記錄均在本 worktree 的 `builds/skill-tooltip-validation/`。既有未登記語意的 state、舊式自訂計數與效果仍需由技能作者提供 metadata；本功能不會把缺少資料視為零、不會虛構来源／期限。


## GUI 回饋修訂（2026-09-20）

- 省略有效／失效狀態列，沿用灰字；相同正文有不同有效性時，僅將失效實例資料灰化。
- 單將省略主將／副將標籤；無實際 usage 計數與無 state 記錄時，不產生空白摘要。None usage 亦不列技術資料。
- 固定介面文案使用英文 Qt 翻譯鍵，中文置於 `builds/sanguosha.ts`；技能作者的可讀 state／效果 metadata 沿用技能翻譯表。
- 他人修改技能時，修改與已有來源列在該技能下；角色承受效果另列底部「目前受到的效果」，只列已記錄的來源、對象及結束條件，不推測舊技能未登記的來源。
- GUI 由使用者手動驗收，不操作 Computer Use。

本批修訂驗證：GUI／core 增量建置及 Qt 翻譯編譯通過；skill-description 專項測試退出碼 0，34.3 秒。TS XML／翻譯覆蓋／佔位符與 diff check 通過。未執行 CTest／完整對局，GUI 人工驗收待使用者確認。記錄見 builds/skill-tooltip-validation/gui-feedback-*。
