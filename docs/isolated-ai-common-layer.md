# Isolated AI 共用層對照

本文 `lua/ai/` 路徑指部署檔案；版本與取得方式見[外部 Lua 來源](lua-ai-spec.md#外部-lua-來源)。

本文件是目前 standalone `lua/ai/isolated/` 純值 AI 架構的維護錨點。原版 SmartAI 只作行為參考；共用層接收 viewer-scoped 純值 (pure values)，提出值型答案，再由 C++ 權威端重驗。安全 fallback 只是故障保護；任何 `unhandled`／unsupported／error 觸發都使 isolated acceptance 失敗。

## Shared core ownership

兩個共用檔按責任分區，檔頭列出導航索引；分區保留既有 lexical scope 與註冊順序。新增方法先選唯一所屬檔案，另一檔透過 `self` 呼叫；不得用同名宣告或「先提供簡版、後覆寫完整版」決定策略。

| 所屬檔案 | 分區 | 責任與代表入口 |
|---|---|---|
| `decision-core.lua` | C01–C03 | 牌值、優先序、排序與防禦評分：`getKeepValue`、`getUsePriority`、`sort`、`getDefense`。 |
| `decision-core.lua` | C04–C06 | 出牌值型計劃、手牌／體力收益與傷害估值：`bindConversionCosts`、`aiUseCard`、`needKongcheng`、`ajustDamage`。 |
| `decision-core.lua` | C07–C08 | 棄牌損益、牌需與目標分配：`doDisCard`、`cardNeed`、`getCardNeedPlayer`、`findPlayerToDamage`。 |
| `decision-core.lua` | C09–C12 | 牌策略派發、修正／驗證、標準牌估值、回合規劃與付費無效決策：`tryUseCard`、`getAoeValue`、`getTurnUse`、`planTurnUse`。 |
| `smart-ai-functions.lua` | S01–S06 | 快照／unknown 契約、可見牌查詢、效果／殺判斷、階段預測、詢問與轉化適配。排序與估值呼叫 core 的唯一實作。 |
| `smart-ai-functions.lua` | S07–S14 | 主公／回復、生存／救援、技能／鐵索、觀測快取、選人、棄牌偏好、移動場上牌與技能 action 綁定。 |
| `smart-ai-functions.lua` | S15–S19 | 來源靜態 metadata 與預設 damage／priority／revision／nullification hooks。套件以 registry 擴充。 |
| `smart-ai-functions.lua` | S20–S22 | 具名來源適配、一般選牌策略、明確的原地排序與必要查詢包裝。 |

`isLordInDanger`、`isLordHealthy`、`getSourceLord` 集中在 S07。`isLordInDanger` 沿用正常 bootstrap 最後生效的版本：只查 `RoomView:getLord()` 的 projected lord，無主公回 false，主公體力小於 3 回 true；保留 `benghuai` 的原有體力處理與未知輸入拒絕。推定身份掃描不再作同名替代。`getSourceLord(player)` 保留來源演算法的同身份夥伴／自身規則，與房間主公查詢分開。

`tools/ai/check-ai-runtime-manifest.py` 同時檢查兩檔具名 `SmartAIView` 方法與 alias 的唯一所屬，以及上述主公查詢的所屬檔案；可加 `--shared-methods-only` 單獨檢查方法邊界。檢查涵蓋 `do` 區塊內宣告，忽略註解／字串；動態 required-query decorator 是既有 provider 的包裝，保持明確的末尾接線。這是靜態檢查，不代表對局或策略等價驗收。完整覆寫 `AiIsolatedScripts` 的配置仍須明列其使用的共用模組，不會自動補載。

## 原版入口與目前共用 API

| SmartAI 原版入口 | isolated 共用層 | 目前語意與邊界 |
|---|---|---|
| `SmartAI:askForUseCard` (`lua/ai/smart-ai.lua`) | `ai_register_handler("use_card", ...)`、`ai_skill_use`／`ai_skill_use_legacy`、`ai_card_response_use` (`lua/ai/isolated/ask-for-use-card.lua`) | 保留 skill → pattern → prompt、legacy ABI 與 compulsory `!`。ResponseUse 支援 Slash、Peach、Jink、ExNihilo、裝備、Duel、順拆、樂／兵與四張標準全域牌；逐候選檢查 legality、priority、支付 keep 與 ID，共用 `bindConversionCosts`。已知完整合法 action 可在同題仍有 unknown 時回答；沒有可行 action 且仍有 unknown 才記 NotCovered。 |
| `SmartAI:activate` → `getTurnUse`、`aiUseCard` (`lua/ai/smart-ai.lua`) | `ai_register_handler("activate", ...)`、`ai_card_use`、`AIUsePlan`、`SmartAIView:tryUseCard` (`lua/ai/isolated/decision-core.lua`) | 以候選牌、值型 card spec、target names、skill instance ticket 規劃；`AIUsePlan:toAnswer()` 只描述意圖，真正造牌／扣成本／提交留在 C++。 |
| `useCardByClassName`、`findPlayerToUseSlash`、`getBestTarget` | `planTargetSequence`、`relationTo`、`ai_card_use.<ClassName>`、`ai_card_effect` | 目標選擇使用權威端列出的 target combinations；managed mode 才能使用 friend/enemy relation。效果特殊規則以 class/name registry 擴展，不在核心增加武將分支。 |
| `askForChoice`／`askForCard`／`askForPlayerChosen` | `ask-for-choice.lua` 的 request handlers、candidate index 與 relation helpers | 被選 action 缺資料或不完整投影才維持 unsupported／NotCovered；同題其他 unknown 不得被當 pass，也不以 native userdata 回查。 |
| `doDisCard`、`poisonCards`、`getCardNeedPlayer`、`cardNeed` | `decision-core.lua`，由順拆、選牌、Yiji、Guanxing 預設消費 | 分友敵、手／裝／判定區與損失收益；給牌先處理救命／裝備，再呼叫 `ai_cardneed(to, card, self)`。只評估投影事實，不代替合法性檢查。 |
| `sort`／`sortBy*` 的原清單排序 | `smart-ai-functions.lua` 的 `sortInPlace`、`sortByKeepValueInPlace`、`sortByUseValueInPlace`、`sortByUsePriorityInPlace`、`sortByDynamicUsePriorityInPlace`、`sortByCardNeedInPlace` | 套件需要排序副作用時直接使用共用方法，轉送排序鍵、反向與 flags 並回傳原清單；一般 `sort`／`sortBy*` 仍回傳副本。寫回僅覆蓋結果前綴，保留既有轉接語意；`flags="j"` 的副本過濾不代表原清單尾端已刪除。自訂比較器、來源操作與外部持牌者隱私檢查仍由原策略負責。 |
| `needKongcheng`、`getBestHp`、`needToLoseHp` | 手牌／血量共用規則與 `ai_getLeastHandcardNum_skill`、`ai_getBestHp_skill`、`ai_need_damaged`、damage adjustment hooks | 保留原版關鍵優先序、精確 callback ABI 與 false；傷害只屬公開基準加 hook 的策略估算，未等價移植全部原生傷害分支。另提供 `dodiscard`、`aiUsecard`、`needToloseHp` 相容拼法。 |
| `needRetrial`、`getRetrialCardId`、`getFinalRetrial` | `retrial.lua`、`getDecisionData():toJudge()` | C++ 投影 `JudgeStruct::isGood(card)` 結果；支援常見判定、交換、Lightning 保留、拆牌偏好與最低 keep。FilterSkill、連鎖傷害、Beige 花色效果、最終改判能力缺資料時保持 unknown。 |
| `ai_use_revises`／`ai_useto_revises`／`ai_target_revises`／`ai_used_revises` | `tryUseCard` 的同一推演流程 | 使用 skill instance 的可見能力，重複技能去重；target revise 只重排完整權威組合。AOE 使用 affected targets；`used` 不在 dummy 推演觸發。另接 `ai_skill_use_func`、`ai_skill_carduse`、`ai_slash_prohibit`。 |
| mode 身份／意圖 hooks | `mode-ai.lua` 的 `onIntention`／宣告式 `intentions`，以及 `event-intention.lua` 的純值事件 consumers | 每 Room＋viewer 的 state 以原子 delta 更新，隔離查詢唯讀。已消費 card intention、player/Yiji/skill choice、damage 與 visible-skill callbacks；仍須逐項核對投影與 ABI，不能以登記表存在宣稱所有 legacy 策略已搬遷。 |

## Mode、身份與 privacy 契約

`mode-ai.lua` 先按身份 hook，再按 mode hook；身份 hook 回 `nil` 才採該層預設，`onIntention` 每事件只執行選定版本一次。isolated 端只收到 `AIWorldView`／`mode_policy` 的 viewer-visible 值：`isModeManaged()`、`relationTo(target)`、objective 與 role visibility 查詢不可讀取 Room、Player、Card userdata，也不可在查詢中變更 state。

身份推測 state 必須按 Room 與 viewer 分區；`role_shown`／個別授權優先於推測，未授權身份保持 unknown。`onIntention(context, fromId, toId, level, state)` 是唯一的意圖變更入口，必須以 atomic 純值更新及 revision 失效快取。不要把 `isFriend`／`isEnemy` 當成取得秘密身份的後門，也不要以全域 `sgs.ai_role` 或另一觀察者 closure 復用狀態。

## 結果、coverage 與 acceptance

`sgs.ai_skill_*` 保留舊 callback 位置參數；無前綴的 isolated `ai_skill_*` 使用 `(self, options, request)`（use-card 為 prompt 版本）。兩種 ABI 分開登記，不能把同名表合併。`strategy-hooks.lua` 在共用 modules 後、套件 scripts 前載入；明確指定 `AiIsolatedScripts` 且載入 `decision-core.lua` 的配置，須同時載入 `strategy-hooks.lua`。

預設詢問已包含：頻率驅動的 skill invoke、choice 排除 `benghuai`、suit 權重、general 可選項、discard、AG、card chosen、Yiji、player(s) chosen、實體回應／show／pindian／救桃／無懈。隨機選擇使用 runtime 受控 RNG。Guanxing 雙向排序目前只覆蓋自己摸牌前且沒有待判定的基線：按需求排序全放頂；只有明確 draw count 才分頂／底，不推測摸牌數。

`nil` 是 `unhandled`（isolated 尚未處理）；`AIUnsupported` 是 unsupported（isolated defect/debt，記錄原因）；`{kind="pass"}` 是已處理的 pass。`"."`／空字串經 normalization 也是 declined/pass，但 compulsory pattern (`!`) 不接受 declined／pass。三者不可合併成「沒有出牌」。

必要投影值統一用 `AIValue.requireKnown(value, key, reason?)`；套件內僅檢查 `nil` 後原樣回傳的 helper 可直接寫成 `local known = AIValue.requireKnown`，省去轉呼叫包裝。專屬診斷 helper 也委派至此守衛，保留原 coverage key，並以 `value == nil and (原診斷運算式) or nil` 傳入第三參數，避免已知值額外求值診斷。它會傳播既有 `AIUnsupported`，只將 `nil` 判為未知，保留 `false`、`0`、空字串與空表。允許合法查無結果的 API 不套此檢查；型別、完整性及合法性檢查仍須保留。

固定 coverage key 與診斷前綴用 `local guard = AIValue.requireKnownFor("key", "prefix")` 綁定；呼叫仍為 `guard(value, field)`，僅值為 nil 時組合原訊息。`value_known`、`damage_known`、`card_selection_known`、`pindian_known` 均採此方式保留原 key 與診斷，不再各自實作守衛。

每個共用 registry 透過 `ai_coverage.declare` 申報；未註冊、過期、資料不完整、未知 conversion 或未知 target strategy 記錄 bounded `NotCovered`／`Error` metadata，並視為 isolated acceptance debt。逐候選 unknown 不會取消同一 request 中已知完整合法 action；只有沒有可行 action 且仍有 unknown 才使該 request NotCovered。已知為空和列不完必須分開。

目前仍有 standalone debt：逐武將／逐技能 activation、conversion／view-as 的所有 pattern 與 cost 語意、`cardEffect` 依賴事件上下文、完整排序與牌值模型，以及需要 native effect／move／userdata 的 response，皆為 `NotCovered`。

## 複雜度與擴展方式

| 工作 | 成本與限制 |
|---|---|
| Player／候選／conversion／選項索引 | 建立 O(n)，之後按 ID 平均 O(1) 查找；回傳副本保護索引來源。 |
| 單選最低 keep、AG、Pindian | 卡片 metadata 一次建立索引，候選單次掃描 O(n)。 |
| viewer 關係與距離投影 | 關係只呼叫 viewer 對各人的 hook；距離保留 viewer 的雙向邊，原生 distance 呼叫次數 O(n)。未投影的其他玩家配對維持 unknown；原生 hook 成本另計。 |
| 排序／多張 discard | 評分預計算，排序 O(n log n)；不宣稱全流程 O(n)。 |
| 目標組合／轉化成本 | 權威端有探測 budget，耗盡為 incomplete；固定上限 k 的獨立成本選擇 O(k n)，不枚舉手牌子集。 |
| 給牌需求 | 共用救命／裝備優先項先預分類；任意技能 `ai_cardneed` 仍須牌 × 適用 hook 比對，不能宣稱這部分為 O(n)。 |

時間推演 (Planning) 按 viewer 保存純值；target／cost 授權券同時綁定 decision ID 與 state revision，跨 request 的一般 intent 只在 revision 未變時保留。`ai_coverage.outcomes()` 提供結果分類計數，與 registry 的宣告覆蓋率分開。

`respond_card` 接受實體牌答案及當次 authority 授權的 V2 conversion `card_spec`；show／pindian 仍是 offered 實體牌限定。conversion 逐項檢查，已知完整合法的 action 可先回答，未知 conversion 另記錄。未覆蓋 V1 view-as 的 response 保留 unknown；合法轉化閃／桃不能被空實體候選吞掉。

新增共用策略時，優先新增純值 facade 欄位、明確 registry handler 與 coverage declaration：

1. 由 C++ 投影 viewer-visible、request-scoped、可重驗的資料及完整性旗標。
2. 在既有 class／skill registry 掛 handler；不要在核心寫角色或武將名稱硬編碼。
3. handler 只回 `AIUsePlan`／值型 result；未知資料回 `AIUnsupported`。牌策略不填 plan 代表該牌不使用；request handler 的 `nil` 是未處理，明確拒絕整題才回 `{kind="pass"}`。
4. 逐項評估 request 候選並保留 unknown 記錄；只要有已知完整合法 action 就交給 normalization，沒有 action 且仍有 unknown 才回 NotCovered。權威端仍重驗 pattern、method、ticket、target、成本與 revision。
5. 補逐分支 coverage 帳與實際驗收證據（不新增測試套件／fixture）。原生路由、驗證拒絕與 fallback 缺少觀測時，獨立驗收記 BLOCKED。

相關設計邊界：[`docs/lua-ai-spec.md`](lua-ai-spec.md)。搬運既有策略時按[移植遵循文件](isolated-ai-migration-playbook.md)；以下函式清單不是等價性通過證明，驗收狀態見[2026-10-03 驗收](isolated-ai-acceptance-20261003.md)。

## Scarlet 參考技能所需共用接口

新增 addHandPile／getAllPeachNum、draw 目標推薦與無牌普通傷害接口；覆蓋與未覆蓋分支見 [Scarlet isolated AI](scarlet-isolated-ai-examples.md)。

`lua/ai/isolated/smart-ai-functions.lua` 補上套件會呼叫的 SmartAI 相等函式（可見牌選取、殺是否有效、命中、棄牌、找人出殺、跳過出牌階段、`getDefenseSlash`、`getWoundedFriend`、`getCardId`、`sortEnemies`、`updatePlayers`、`hasTuntianEffect`、`adjacentPlayers`、`isLordHealthy`，以及 `bignumber`／`slash` 牌需），未知投影回 nil。套件用 `sgs.append_skill_list`、`sgs.ai_slash_benefit`、`sgs.ai_valuable_card`、`sgs.ai_will_skip_play`、`sgs.ai_turn_over`、`sgs.ai_defense_slash`、`sgs.ai_hasTuntianEffect_skill`、`sgs.ai_jueqing_effect` 擴充，武將名不寫進這層。Scarlet、mcompetition、Shadow 的技能名單與策略分別在 `isolated/scarlet-ai.lua`、`isolated/mcompetition-ai.lua`、`isolated/shadow-ai.lua`；舊版同名檔的逐技能 callback 仍在 gameplay VM。字串 data、房間 tag、移動與玩家屬性沒有投影時，對應詢問維持 unsupported。
