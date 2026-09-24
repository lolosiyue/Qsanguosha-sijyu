# Isolated AI 共用入口現況盤點

本表記錄共用入口的 consumer、覆蓋範圍與 native 缺口。契約詳見[共用層對照](isolated-ai-common-layer.md)，實跑結果見[2026-09-21 驗證紀錄](isolated-ai-common-layer.md#驗證狀態)。SmartAI 僅作原始語義參考，isolated 路徑不呼叫 SmartAI。

## 已有實際 consumer

| 共用入口 | source consumer | 現況 |
|---|---|---|
| `askForDiscard`、`doDisCard` | `ask-for-choice.lua` default、`decision-core.lua:doDisCard` | 純值候選、keep value、flags/obtain/n 已消費；pattern、完整 prohibition/effect/reason policy 仍可能 `NotCovered`。 |
| `needKongcheng`、`getBestHp`、`needToLoseHp` | `decision-core.lua`、`ai_need_kongcheng`、`ai_need_damaged` hooks | 基本 hand/HP/relation/hook 已接入；完整技能 effect、damage/recover context 仍受 DTO 限制。 |
| `cardNeed`、`getCardNeedPlayer` | `decision-core.lua`、`ai_cardneed` hooks | card need、sort、friend/player consumer 已接入；legacy active/not-active 規則及完整 give/draw objective 仍 partial。 |
| `needRetrial`、`getRetrialCardId` | `isolated/retrial.lua` | judge normalization、relation、outcome、reserve、candidate selection 有 consumer；完整 `cardEffect`、Lightning holder 與 reason-specific native hooks 仍未知。 |
| ask-for choice family | `ask-for-choice.lua` defaults/handlers | skill invoke、suit、choice、kingdom、discard、AG、chosen、Yiji、Guanxing、show/pindian 等已有純值 defaults 或 handler；逐技能策略與完整原版 tie-break 仍 partial。 |
| `askForCard`／nullification／response | `ask-for-choice.lua:respond_card`、C++ response conversion ticket | response defaults、候選完整性、V2 `card_spec` identity／成本／native 重驗及 show/pindian 限制已通過 `ai-common` 契約；其餘轉化技能與完整對局尚待驗證。 |
| `askForUseCard`／ResponseUse | `ask-for-use-card.lua`、response-family handlers | pattern/prompt registry、相容 ABI、Slash/Peach 與 response families 已有 consumer；完整 cardEffect、recast/method、所有 view-as scan 仍缺。 |
| value/target helpers | `decision-core.lua` | `ai_suit_priority`、`ai_card_priority`、`ai_chaofeng`、`ai_weapon_value`、`ai_armor_value`、`ai_skill_defense`、`ai_NeedPeach` 已被 value/keep/use/defense/threat 路徑讀取；`dynamic_value`、`card_damage_nature` 與 damage adjustment 也進入 damage 評估。 `findPlayerToDraw`、`findPlayerToDiscard`、`findPlayerToDamage`、`findBestDamageTarget` 已在目前 projected candidates 上提供 wrapper，不宣稱 native legality。 |

## source path 與架構狀態

| 項目 | 目前證據與界線 |
|---|---|
| standard mode loader | `room-runtime.cpp` 先載入 mode AI，再對非 custom mode 註冊 standard mode AI；isolated mode 使用 `modeAIUsesIsolatedEvents`。此路徑保留 new architecture 證據，不以 SmartAI fallback 當完成條件。 |
| event atomic pipeline | `ai-decision-coordinator.cpp` 將 native event 投影為 viewer-scoped value event，`ai-runtime.cpp:processEvent` 對 delta clone 後一次 commit；每個 observer 個別重建可見 world。native event／隱藏身份 privacy 與 Lua intention 契約已通過 `ai-common`；其他 legacy event table 仍待覆蓋。 |
| response conversion | C++ 以 authority ticket 列出實際 skill instance/conversion，Lua 回答 `card_spec`，native response 端再驗 identity、cost、selected cards 與 request kind。V1 view-as、未知成本與 show/pindian 仍保持 incomplete/拒絕。 |
| 複雜度與界限 | 成本模型與原始碼限制見[隔離層 bounded policy](isolated-ai-layer.md#bounded-policy)。 |

## 仍是 compatibility surface 或 native debt

| surface | 目前界線 |
|---|---|
| `ai_skill_use_func`、`ai_skill_carduse`、`ai_slash_prohibit` | 部分 callback 可經 value facade 使用；需要 native API 的個別 hook 尚未普遍覆蓋。 |
| `ai_skills`、`ai_view_as`、`ai_cardsview`、`ai_guhuo_card` | 不搬 legacy userdata 造牌 pipeline；由 authority conversion ticket 提供已授權的純值牌與成本。V1 轉化仍是 debt。 |
| intention／event callback tables | mode `onIntention` 與 event pipeline 是獨立 source path；尚未宣稱所有 legacy intention/filter table 已有 atomic viewer-scoped consumer。 |
| target revise／usage／nullification tables | 有 revise/value 的部分 consumer，但完整 distance、range、prohibited、usage limit、card effect 與 history DTO 仍缺。 |
| 逐武將 `ai_skill_*`、`useCardXxx` | 需要逐項純值 consumer 與 visible skill/mark/event DTO；目前維持 standalone `NotCovered`，不能稱任意武將 parity。 |

`findPlayerTo*` wrappers 的輸入是目前 projected candidates；它們不等同 native `canUse`、distance、range、prohibited 或完整 damage/effect search。未知實際 legality 時保持 unknown。

