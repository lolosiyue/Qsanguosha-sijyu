# 文件索引

依操作與開發主題查找文件。使用入口見 [README](../README_zh.md)。

操作指南描述用法，API 文件描述契約；欄位、預設值與限制以連結的原始碼符號為準。

**收錄範圍**：本目錄只放現行功能說明、操作指南與介面契約。計畫、路線圖、遷移進度、稽核報告、驗收證據與量測紀錄不進 `docs/`；這類過程紀錄留在 git 歷史與 `builds/`。

> 2026-09-25 起倉庫不含單元測試與 CTest（`720a8df`）。文檔中「原測試」「已移除」的字樣即指此；驗證方式見 [Linux 開發環境 §8](linux-development-environment.md#8-自動化驗證)。

## 1. 技能系統（Skill V2／多實例）

| 文檔 | 說明 |
|------|------|
| [SkillV2新機制說明.md](SkillV2新機制說明.md) | Skill V2 新機制總覽（TriggerSkillV2、ViewAsSkillV2、LimitScope、CardLimitation、實例狀態） |
| [TriggerSkillV2系統說明.md](TriggerSkillV2系統說明.md) | TriggerSkillV2 觸發技能系統（SkillContext、V2 分表、排序）＋技能多實例權威模型 |
| [active-skill-v2-refactor-plan.md](active-skill-v2-refactor-plan.md) | ViewAsSkillV2 權威契約（usage ref、selection 邊界）；依賴多實例模型 |
| [CorrectSkillV2功能與開發指南.md](CorrectSkillV2功能與開發指南.md) | 四類修正技能開發指南；§16.1 為 Room integration 驗證期望 |
| [engine-correct-skills.md](engine-correct-skills.md) | CorrectSkillV2 引擎側快參（類別、selector、snapshot 欄位） |
| [active-skill-v2-migration-guide.md](active-skill-v2-migration-guide.md) | 舊 ViewAsSkill 遷移至 V2 的規範 |
| [safe-view-as-equip.md](safe-view-as-equip.md) | 手牌安全視為裝備的 C++ 範式與 Lua 端正確做法 |
| [preselection-meta-skill.md](preselection-meta-skill.md) | PreSelectionMetaSkill 六層接線說明 |
| [anytime-skill.md](anytime-skill.md) | AnytimeSkill 全鏈路（C++／Lua／protocol／client） |
| [shiming-skill-instances.md](shiming-skill-instances.md) | 使命技能 `SkillInstanceRef` 契約與外部 Lua 遷移 |
| [新型技能重製文檔.md](新型技能重製文檔.md) | 技能重製總覽入口，指向各 V2 權威文檔 |
| [view-as-skill-v2-guhuo.md](view-as-skill-v2-guhuo.md) | 選牌視為技能範例 |
| [skill-description-instances.md](skill-description-instances.md) | 技能實例的局中說明與狀態 |

## 2. 卡牌、模式與對局規則

| 文檔 | 說明 |
|------|------|
| [CardLimitation系統說明.md](CardLimitation系統說明.md) | `setCardLimitation`／`isCardLimited` 卡牌限制系統 |
| [oracle-text-system.md](oracle-text-system.md) | Oracle 概念文字與 tooltip 機制 |
| [multi-equip-slot-guide.md](multi-equip-slot-guide.md) | 多欄裝備（`getOccupyLocations`）指南 |
| [extra-turn-scheduling.md](extra-turn-scheduling.md) | 額外回合排程機制 |
| [mini-scenario-editor.md](mini-scenario-editor.md) | `CustomAssignDialog` 多欄裝備場景編輯器 |
| [一人多控初版说明.md](一人多控初版说明.md) | 多操控房（`setPlayerController`／`SWITCH_CONTEXT`） |
| [中途召喚系統說明.md](中途召喚系統說明.md) | 對局中途加入玩家（`PlayerLifecycleService`） |
| [鏖戰模式說明.md](鏖戰模式說明.md) | 鏖戰（Melee）模式技能與 `E` 旗標 |
| [resolution-history.md](resolution-history.md) | 結算歷史查詢 API |
| [scenario-works.md](scenario-works.md) | Scene／Stage 場景作品 |

## 3. GUI 互動與皮膚

| 文檔 | 說明 |
|------|------|
| [client-core-interaction-model.md](client-core-interaction-model.md) | ClientCore 互動模型與 request/reply 序列化 |
| [ask-for-qml.md](ask-for-qml.md) | `askForQml` 通用 QML 互動鏈（結構化 payload、overlay 契約） |
| [hero-skin-guide.md](hero-skin-guide.md) | 皮膚系統完整文檔（資源查找、翻譯、Spine、GIF 動圖） |
| [dynamic-skin-guide.md](dynamic-skin-guide.md) | Spine 動態皮膚與 `skin=` lightbox 用法 |
| [Aura光環系統說明.md](Aura光環系統說明.md) | Aura（`lani`）光環系統與 `changeBGM`／`changeBackground` |
| [../DESIGN.md](../DESIGN.md) | 首頁與卡牌總覽設計規範 |

## 4. Protocol 與 Replay

| 文檔 | 說明 |
|------|------|
| [protocol-v2.md](protocol-v2.md) | Protocol V2 訊息契約與相容性決策 |
| [protocol-message.md](protocol-message.md) | 訊息 codec／registry 邊界 |
| [replay-v2.md](replay-v2.md) | Replay V2 錄製／播放與 takeover 契約 |

## 5. Native rules 與 Web 客戶端

| 文檔 | 說明 |
|------|------|
| [rules-bundle-identity.md](rules-bundle-identity.md) | rules bundle identity 與 WebSocket admission gate |
| [web-client.md](web-client.md) | Web compact client 使用說明（Run、行為、範圍） |
| [web-client-wasm-runtime.md](web-client-wasm-runtime.md) | WASM runtime 建置、產物與 `ClientRulesIngress` stream ABI |
| [browser-solo.md](browser-solo.md) | 離線單人瀏覽器包（`QSAN_BUILD_WASM_SOLO`） |

## 6. TUI

| 文檔 | 說明 |
|------|------|
| [tui-client.md](tui-client.md) | TUI 使用說明（CLI、classic／board 模式；隨包安裝） |
| [tui-board-ui.md](tui-board-ui.md) | board 模式權威規格（60×18、幀緩衝） |

## 7. 平台建置與部署

| 文檔 | 說明 |
|------|------|
| [windows-build.md](windows-build.md) | Windows x64 建置、Qt 部署與啟動 |
| [linux-development-environment.md](linux-development-environment.md) | Linux 開發環境、建置與執行 |
| [linux-packaging.md](linux-packaging.md) | Linux 打包（AppImage、`.deb` deferred） |
| [linux-gui-effects-profiles.md](linux-gui-effects-profiles.md) | Linux GUI 特效 profiles 契約 |
| [docker-server.md](docker-server.md) | Docker dedicated server（compose、entrypoint） |
| [android-build.md](android-build.md) | Android 建置流程（preset、媒體管線） |
| [android-extension-runtime.md](android-extension-runtime.md) | Android 擴展執行期與內容啟動 |
| [windows-xp-legacy-build.md](windows-xp-legacy-build.md) | XP SP3／Win7 x86 legacy 產品（含 GUI/helper process boundary） |

## 8. Excel／Google Sheets 橋接

| 文檔 | 說明 |
|------|------|
| [excel-ipc.md](excel-ipc.md) | Excel 橋 wire contract |
| [excel-packaging.md](excel-packaging.md) | Excel 封裝輸入、VBA 匯出、runtime-only 組裝與支援界線 |
| [excel-trial-readme.txt](excel-trial-readme.txt) | Excel portable 試用包終端使用者說明（附檔） |
| [google-sheets-setup.md](google-sheets-setup.md) | Google Sheets 主機準備、安裝與工作表操作 |
| [google-sheets-client.md](google-sheets-client.md) | Google Sheets 客戶端、房間布局及限制（`QSAN_BUILD_SHEETS`） |

## 9. Lua 擴展與 AI 規範

| 文檔 | 說明 |
|------|------|
| [lua-ext-spec.md](lua-ext-spec.md) | Lua 擴展 API／`module()`、`bit32` 相容層、模式註冊規範 |
| [lua-ai-spec.md](lua-ai-spec.md) | SmartAI／`sgs.ai` 撰寫慣例 |
| [isolated-ai-authoring-guide.md](isolated-ai-authoring-guide.md) | 隔離 AI 技能撰寫指南 |
| [isolated-ai-layer.md](isolated-ai-layer.md) | 隔離 AI 分層與資料契約 |
| [isolated-ai-common-layer.md](isolated-ai-common-layer.md) | 共用策略入口 |
| [isolated-ai-a-packages.md](isolated-ai-a-packages.md) | A 字頭套件的隔離 AI 入口 |
| [scarlet-isolated-ai-examples.md](scarlet-isolated-ai-examples.md) | Scarlet 參考技能的隔離 AI 實作 |
| [human-lua-v2-api.md](human-lua-v2-api.md) | HUMAN 擴展的 V2 Lua API（供擴展作者查核） |
| [package-modularity.md](package-modularity.md) | Lua 套件准入與生命週期 |

## 10. 人間版移植功能

| 文檔 | 說明 |
|------|------|
| [休整功能移植说明.md](休整功能移植说明.md) | 休整（rest）狀態與 Mowang 移植 |
| [反转出牌顺序与调虎离山逻辑说明.md](反转出牌顺序与调虎离山逻辑说明.md) | 出牌順序反轉與調虎離山 |
| [場景切換與語音動畫移植說明.md](場景切換與語音動畫移植說明.md) | `changeBackground`／`setLoopEmotion`／lightbox 場景切換 |
| [手牌篩選功能說明.md](手牌篩選功能說明.md) | Dashboard 手牌篩選容器 |

## 設定與參考

- [伺服器 INI 範例](server.ini.example)
- [AI runtime manifest](ai-runtime-manifest.json)（隔離 AI 載入順序；由 `tools/ai/check-ai-runtime-manifest.py` 檢查）
- [套件內容與清單](../packages/README.md)
