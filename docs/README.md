# 文檔索引（依類別）

`docs/` 目錄的分類目錄。**新增文檔時必須歸入下表對應類別並補一句話說明**；刪除或合併文檔時同步更新本索引與 `STRUCTURE.md`（統計）。

行號參照一律寫成「符號（現約 :NNN）」，以符號搜尋為準（行號會漂移）。

---

## 1. 技能系統（Skill V2／多實例）

| 文檔 | 說明 |
|------|------|
| [skill-instance-refactor-plan.md](skill-instance-refactor-plan.md) | 技能多實例（SkillInstance）模型的權威設計與 ticket 狀態 |
| [active-skill-v2-refactor-plan.md](active-skill-v2-refactor-plan.md) | ViewAsSkillV2 權威契約（usage ref、selection 邊界）；依賴上項 |
| [TriggerSkillV2系統說明.md](TriggerSkillV2系統說明.md) | TriggerSkillV2 觸發技能系統（SkillContext、V2 分表、排序） |
| [CorrectSkillV2功能與開發指南.md](CorrectSkillV2功能與開發指南.md) | 四類修正技能開發指南；§16.1 為 Room integration 驗證期望 |
| [engine-correct-skills.md](engine-correct-skills.md) | CorrectSkillV2 引擎側快參（類別、selector、snapshot 欄位） |
| [active-skill-v2-migration-guide.md](active-skill-v2-migration-guide.md) | 舊 ViewAsSkill 遷移至 V2 的規範 |
| [active-skill-v2-test-matrix.md](active-skill-v2-test-matrix.md) | ViewAsSkillV2 驗證矩陣與證據 |
| [skill-instance-callsite-audit.md](skill-instance-callsite-audit.md) | 存檔快照；SkillInstance 呼叫點人工審核清單（仍有未勾選待辦） |
| [safe-view-as-equip.md](safe-view-as-equip.md) | 手牌安全視為裝備的 C++ 範式與 Lua 端正確做法 |
| [preselection-meta-skill.md](preselection-meta-skill.md) | PreSelectionMetaSkill 六層接線說明 |
| [anytime-skill.md](anytime-skill.md) | AnytimeSkill 全鏈路（C++／Lua／protocol／client） |
| [新型技能重製文檔.md](新型技能重製文檔.md) | 技能重製總覽入口，指向各 V2 權威文檔 |

## 2. 卡牌、模式與對局規則

| 文檔 | 說明 |
|------|------|
| [CardLimitation系統說明.md](CardLimitation系統說明.md) | `setCardLimitation`／`isCardLimited` 卡牌限制系統 |
| [card-lifetime-ownership.md](card-lifetime-ownership.md) | Card lifetime manager 與 `OwnedCardPtr` 權責 ledger |
| [oracle-text-system.md](oracle-text-system.md) | Oracle 概念文字與 tooltip 機制 |
| [multi-equip-slot-guide.md](multi-equip-slot-guide.md) | 多欄裝備（`getOccupyLocations`）指南 |
| [extra-turn-scheduling.md](extra-turn-scheduling.md) | 額外回合排程機制 |
| [mini-scenario-editor.md](mini-scenario-editor.md) | `CustomAssignDialog` 多欄裝備場景編輯器 |
| [一人多控初版说明.md](一人多控初版说明.md) | 多操控房（`setPlayerController`／`SWITCH_CONTEXT`） |
| [中途召喚系統說明.md](中途召喚系統說明.md) | 對局中途加入玩家（`PlayerLifecycleService`） |
| [鏖戰模式說明.md](鏖戰模式說明.md) | 鏖戰（Melee）模式技能與 `E` 旗標 |

## 3. GUI 互動、皮膚與 UI 路線

| 文檔 | 說明 |
|------|------|
| [client-core-interaction-model.md](client-core-interaction-model.md) | 29 類 interaction 的 ClientCore 權威模型（request/reply 序列化、matrix gate） |
| [room-askfor-ui-matrix.md](room-askfor-ui-matrix.md) | Room askFor* 與 GUI widget 對照矩陣（skill_ui_runner 28 案例） |
| [ask-for-qml.md](ask-for-qml.md) | `askForQml` 通用 QML 互動鏈（結構化 payload、overlay 契約） |
| [guhuo-dialog-refactor.md](guhuo-dialog-refactor.md) | 蠱惑對話框薄 presenter 重構範式（第三階段未做） |
| [engine-gui-decoupling-implementation-plan.md](engine-gui-decoupling-implementation-plan.md) | Engine/GUI 解耦計畫（M1 殘餘：約 34 處 `getDialog()`） |
| [ui-roadmap.md](ui-roadmap.md) | 八項共通 UI 契約與 P1–P8 產品線路線圖 |
| [windows-gui-crash-handoff.md](windows-gui-crash-handoff.md) | Windows GUI 崩潰交接書（§4 六嫌疑點未修） |
| [hero-skin-guide.md](hero-skin-guide.md) | 皮膚系統完整文檔（資源查找、翻譯、Spine、GIF 動圖） |
| [dynamic-skin-guide.md](dynamic-skin-guide.md) | Spine 動態皮膚與 `skin=` lightbox 用法 |
| [Aura光環系統說明.md](Aura光環系統說明.md) | Aura（`lani`）光環系統與 `changeBGM`／`changeBackground` |

## 4. Protocol 與 Replay

| 文檔 | 說明 |
|------|------|
| [protocol-v2.md](protocol-v2.md) | Protocol V2 生產契約（145 flows、envelope、Retired designs 墓碑） |
| [protocol-message.md](protocol-message.md) | 訊息 codec／registry 邊界 |
| [replay-v2.md](replay-v2.md) | Replay V2 錄製／播放契約 |
| [replay-system-update.md](replay-system-update.md) | Replay 與 takeover 整合邊界摘要 |

## 5. Native rules 與 Web 客戶端

| 文檔 | 說明 |
|------|------|
| [rules-bundle-identity.md](rules-bundle-identity.md) | rules bundle identity 與 WebSocket admission gate |
| [production-rules-session.md](production-rules-session.md) | W1 production rules session 契約 |
| [native-rules-ingress.md](native-rules-ingress.md) | W3b `ClientRulesIngress` streaming API（本組最新契約） |
| [native-rules-fixtures.md](native-rules-fixtures.md) | fixtures 驗收 slice ①：native runner |
| [wasm-rules-fixtures.md](wasm-rules-fixtures.md) | fixtures 驗收 slice ②：Node WASM parity |
| [browser-rules-fixtures.md](browser-rules-fixtures.md) | fixtures 驗收 slice ③：browser Worker probe |
| [web-client.md](web-client.md) | Web compact client 使用說明（Run、行為、範圍） |
| [web-client-wasm-runtime.md](web-client-wasm-runtime.md) | WASM runtime 建置、產物與 W3b cutover 邊界 |
| [browser-solo.md](browser-solo.md) | 離線單人瀏覽器包（`QSAN_BUILD_WASM_SOLO`） |

## 6. TUI

| 文檔 | 說明 |
|------|------|
| [tui-client.md](tui-client.md) | TUI 使用說明（CLI、classic／board 模式、驗收證據；隨包安裝） |
| [tui-board-ui.md](tui-board-ui.md) | board 模式權威規格（60×18、幀緩衝、golden tests） |
| [tui-client-architecture.md](tui-client-architecture.md) | TUI 設計緣起與 ClientCore 解耦契約（已實作） |

## 7. 平台建置與部署

| 文檔 | 說明 |
|------|------|
| [cross-platform-modernization-plan.md](cross-platform-modernization-plan.md) | 跨平台現代化權威計畫（Qt 6.11.1／Lua 5.4.8 長期基線） |
| [linux-development-environment.md](linux-development-environment.md) | Linux 開發環境與 M0–M2B 里程碑 |
| [linux-packaging.md](linux-packaging.md) | Linux 打包（`.deb` deferred） |
| [linux-gui-effects-profiles.md](linux-gui-effects-profiles.md) | Linux GUI 特效 profiles 契約與 smoke |
| [docker-server.md](docker-server.md) | Docker dedicated server（compose、entrypoint、smoke） |
| [android-build.md](android-build.md) | Android 建置流程（preset、媒體管線） |
| [android-extension-runtime.md](android-extension-runtime.md) | Android 擴展執行期與內容啟動 |
| [android-first-release.md](android-first-release.md) | Android 首發驗收證據 |
| [windows-xp-legacy-build.md](windows-xp-legacy-build.md) | XP SP3／Win7 x86 legacy 產品（含 GUI/helper process boundary） |

## 8. Excel／Google Sheets 橋接

| 文檔 | 說明 |
|------|------|
| [excel-client.md](excel-client.md) | Excel 客戶端 release gates（VBA＋PowerShell 橋） |
| [excel-ipc.md](excel-ipc.md) | Excel 橋 wire contract |
| [excel-implementation-status.md](excel-implementation-status.md) | Excel 進度檢查點 |
| [excel-trial-readme.txt](excel-trial-readme.txt) | Excel portable 試用包終端使用者說明（附檔） |
| [google-sheets-client.md](google-sheets-client.md) | Google Sheets 客戶端（`QSAN_BUILD_SHEETS`；未追蹤新檔） |

## 9. Lua 擴展規範

| 文檔 | 說明 |
|------|------|
| [lua-ext-spec.md](lua-ext-spec.md) | Lua 擴展 API／`module()`、`bit32` 相容層規範 |
| [lua-ai-spec.md](lua-ai-spec.md) | SmartAI／`sgs.ai` 撰寫慣例 |

## 10. 移植候選（來自人間版）

| 文檔 | 說明 |
|------|------|
| [GameModeStruct_重構說明.md](GameModeStruct_重構說明.md) | `GameModeStruct` 值物件＋Engine 註冊 API＋`createMode{}` |
| [休整功能移植说明.md](休整功能移植说明.md) | 休整（rest）狀態與 Mowang 移植 |
| [反转出牌顺序与调虎离山逻辑说明.md](反转出牌顺序与调虎离山逻辑说明.md) | 出牌順序反轉與調虎離山 |
| [場景切換與語音動畫移植說明.md](場景切換與語音動畫移植說明.md) | `changeBackground`／`setLoopEmotion`／lightbox 場景切換 |
| [手牌篩選功能說明.md](手牌篩選功能說明.md) | Dashboard 手牌篩選容器 |

## 11. 開發流程（本地私有，`.git/info/exclude` 排除）

| 文檔 | 說明 |
|------|------|
| [testing-conventions.md](testing-conventions.md) | 測試組織與 `qsan_add_ctest()` 入口慣例（本地） |
| [武將稽核-2025.md](武將稽核-2025.md) | 2025 官方武將覆蓋稽核（本地工作文檔） |

## 12. 設計 spec（superpowers）

| 文檔 | 說明 |
|------|------|
| [superpowers/specs/2026-09-09-extension-compatibility-scope-design.md](superpowers/specs/2026-09-09-extension-compatibility-scope-design.md) | Extension compatibility scope 設計權威（declared manifest P1–P3 已落地；兩項開放：完整舊載入／新 manifest 的 registry 對照、manifest 追加排序守衛） |

---

**附檔**：`server.ini.example`（server 設定範例）。

**維護**：本索引由 2026-09-12 全量文檔稽核建立；當天已刪除 12 份存檔／過期文檔（清單見 git 歷史與 `STRUCTURE.md` 墓碑註記）。
