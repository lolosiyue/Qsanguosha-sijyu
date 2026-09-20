# Isolated AI 共用入口現況盤點

本表以目前工作樹的 source 與 2026-09-21 `ai-common` 契約執行結果為準，區分實際 consumer、已驗證的受限路徑，以及仍屬 native debt 的相容面。registry、facade 或安全 fallback 都不代表 standalone 完成；本表也不宣稱任意武將 AI parity。SmartAI 僅作原始語義參考，isolated 路徑不呼叫 SmartAI。

## 已有實際 consumer

| 共用入口 | source consumer | 現況 |
|---|---|---|
| `askForDiscard`、`doDisCard` | `ask-for-choice.lua` default、`decision-core.lua:doDisCard` | 純值候選、keep value、flags/obtain/n 已消費；pattern、完整 prohibition/effect/reason policy 仍可能 `NotCovered`。 |
| `needKongcheng`、`getBestHp`、`needToLoseHp` | `decision-core.lua`、`ai_need_kongcheng`、`ai_need_damaged` hooks | 基本 hand/HP/relation/hook 已接入；完整技能 effect、damage/recover context 仍受 DTO 限制。 |
| `cardNeed`、`getCardNeedPlayer` | `decision-core.lua`、`ai_cardneed` hooks | card need、sort、friend/player consumer 已接入；legacy active/not-active 規則及完整 give/draw objective 仍 partial。 |
| `needRetrial`、`getRetrialCardId` | `isolated/retrial.lua` | judge normalization、relation、outcome、reserve、candidate selection 有 consumer；完整 `cardEffect`、Lightning holder 與 reason-specific native hooks 仍未知。 |
| ask-for choice family | `ask-for-choice.lua` defaults/handlers | skill invoke、suit、choice、kingdom、discard、AG、chosen、Yiji、Guanxing、show/pindian 等已有純值 defaults 或 handler；逐技能策略與完整原版 tie-break 仍 partial。 |
| `askForCard`／nullification／response | `ask-for-choice.lua:respond_card`、C++ response conversion ticket | response defaults、候選完整性、V2 `card_spec` identity／成本／native 重驗及 show/pindian 限制已通過 `ai-common` 契約；不代表所有轉化技能或完整對局覆蓋。 |
| `askForUseCard`／ResponseUse | `ask-for-use-card.lua`、response-family handlers | pattern/prompt registry、相容 ABI、Slash/Peach 與 response families 已有 consumer；完整 cardEffect、recast/method、所有 view-as scan 仍缺。 |
| value/target helpers | `decision-core.lua` | `ai_suit_priority`、`ai_card_priority`、`ai_chaofeng`、`ai_weapon_value`、`ai_armor_value`、`ai_skill_defense`、`ai_NeedPeach` 已被 value/keep/use/defense/threat 路徑讀取；`dynamic_value`、`card_damage_nature` 與 damage adjustment 也進入 damage 評估。 `findPlayerToDraw`、`findPlayerToDiscard`、`findPlayerToDamage`、`findBestDamageTarget` 已在目前 projected candidates 上提供 wrapper，不宣稱 native legality。 |

## source path 與架構狀態

| 項目 | 目前證據與界線 |
|---|---|
| standard mode loader | `room-runtime.cpp` 先載入 mode AI，再對非 custom mode 註冊 standard mode AI；isolated mode 使用 `modeAIUsesIsolatedEvents`。此路徑保留 new architecture 證據，不以 SmartAI fallback 當完成條件。 |
| event atomic pipeline | `ai-decision-coordinator.cpp` 將 native event 投影為 viewer-scoped value event，`ai-runtime.cpp:processEvent` 對 delta clone 後一次 commit；每個 observer 個別重建可見 world。native event／隱藏身份 privacy 與 Lua intention 契約已通過 `ai-common`；仍不代表所有 legacy event table 覆蓋。 |
| response conversion | C++ 以 authority ticket 列出實際 skill instance/conversion，Lua 回答 `card_spec`，native response 端再驗 identity、cost、selected cards 與 request kind。V1 view-as、未知成本與 show/pindian 仍保持 incomplete/拒絕。 |
| 複雜度與界限 | state 是每 viewer scan，約 `O(n + visible state)`；候選排序約 `O(n log n)`；全 observer event 約 `O(v(n + visible state))`。target/candidate projection 共用 request budget 上限 16384，conversion probes 上限 512，單 request card conversions 上限 64，event log 上限 64；超限標為 incomplete，不捏造 legality。 |

## 仍是 compatibility surface 或 native debt

| surface | 目前界線 |
|---|---|
| `ai_skill_use_func`、`ai_skill_carduse`、`ai_slash_prohibit` | 部分 callback 可經 value facade 使用；需要 native API 的個別 hook 尚未普遍覆蓋。 |
| `ai_skills`、`ai_view_as`、`ai_cardsview`、`ai_guhuo_card` | 不搬 legacy userdata 造牌 pipeline；由 authority conversion ticket 提供已授權的純值牌與成本。V1 轉化仍是 debt。 |
| intention／event callback tables | mode `onIntention` 與 event pipeline 是獨立 source path；尚未宣稱所有 legacy intention/filter table 已有 atomic viewer-scoped consumer。 |
| target revise／usage／nullification tables | 有 revise/value 的部分 consumer，但完整 distance、range、prohibited、usage limit、card effect 與 history DTO 仍缺。 |
| 逐武將 `ai_skill_*`、`useCardXxx` | 需要逐項純值 consumer 與 visible skill/mark/event DTO；目前維持 standalone `NotCovered`，不能稱任意武將 parity。 |

`findPlayerTo*` wrappers 的輸入是目前 projected candidates；它們不等同 native `canUse`、distance、range、prohibited 或完整 damage/effect search。未知實際 legality 時保持 unknown。

2026-09-21 後續授權檢查點：正常 CMake SWIG 重新生成、Debug engine／server／runtime runner 編譯通過；完整 `--suite ai-common` exit 0（112.0 秒），`--suite card-lifetime` exit 0（407.3 秒）。命令、修正與日誌見 `builds/ai-common-completion-report.md`。未執行 CTest、GUI 或 gameplay；尚不能宣稱完整對局可取代 SmartAI。
