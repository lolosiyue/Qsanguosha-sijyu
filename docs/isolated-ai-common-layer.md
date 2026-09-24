# Isolated AI 共用層對照

本文 `lua/ai/` 路徑指部署檔案；版本與取得方式見[外部 Lua 來源](lua-ai-spec.md#外部-lua-來源)。

本文件是目前 standalone `lua/ai/isolated/` 純值 AI 架構的維護錨點。原版 SmartAI 只作行為參考；共用層接收 viewer-scoped 純值 (pure values)，提出值型答案，再由 C++ 權威端重驗。安全 fallback 只是故障保護，觸發即代表 isolated acceptance 失敗，不是完成證據。

## 原版入口與目前共用 API

| SmartAI 原版入口 | isolated 共用層 | 目前語意與邊界 |
|---|---|---|
| `SmartAI:askForUseCard` (`lua/ai/smart-ai.lua`) | `ai_register_handler("use_card", ...)`、`ai_skill_use`／`ai_skill_use_legacy`、`ai_card_response_use` (`lua/ai/isolated/ask-for-use-card.lua`) | 保留 skill → pattern → prompt、legacy ABI 與 compulsory `!`。ResponseUse 支援 Slash、Peach、Jink、ExNihilo、裝備、Duel、順拆、樂／兵與四張標準全域牌；逐候選檢查 legality、priority、支付 keep 與 ID，共用 `bindConversionCosts`。已知完整合法 action 可在同題仍有 unknown 時回答；沒有可行 action 且仍有 unknown 才記 NotCovered。 |
| `SmartAI:activate` → `getTurnUse`、`aiUseCard` (`lua/ai/smart-ai.lua`) | `ai_register_handler("activate", ...)`、`ai_card_use`、`AIUsePlan`、`SmartAIView:tryUseCard` (`lua/ai/isolated/decision-core.lua`) | 以候選牌、值型 card spec、target names、skill instance ticket 規劃；`AIUsePlan:toAnswer()` 只描述意圖，真正造牌／扣成本／提交留在 C++。 |
| `useCardByClassName`、`findPlayerToUseSlash`、`getBestTarget` | `planTargetSequence`、`relationTo`、`ai_card_use.<ClassName>`、`ai_card_effect` | 目標選擇使用權威端列出的 target combinations；managed mode 才能使用 friend/enemy relation。效果特殊規則以 class/name registry 擴展，不在核心增加武將分支。 |
| `askForChoice`／`askForCard`／`askForPlayerChosen` | `ask-for-choice.lua` 的 request handlers、candidate index 與 relation helpers | 被選 action 缺資料或不完整投影才維持 unsupported／NotCovered；同題其他 unknown 不得被當 pass，也不以 native userdata 回查。 |
| `doDisCard`、`poisonCards`、`getCardNeedPlayer`、`cardNeed` | `decision-core.lua`，由順拆、選牌、Yiji、Guanxing 預設消費 | 分友敵、手／裝／判定區與損失收益；給牌先處理救命／裝備，再呼叫 `ai_cardneed(to, card, self)`。只評估投影事實，不代替合法性檢查。 |
| `needKongcheng`、`getBestHp`、`needToLoseHp` | 手牌／血量共用規則與 `ai_getLeastHandcardNum_skill`、`ai_getBestHp_skill`、`ai_need_damaged`、damage adjustment hooks | 保留原版關鍵優先序、精確 callback ABI 與 false；傷害只屬公開基準加 hook 的策略估算，未等價移植全部原生傷害分支。另提供 `dodiscard`、`aiUsecard`、`needToloseHp` 相容拼法。 |
| `needRetrial`、`getRetrialCardId`、`getFinalRetrial` | `retrial.lua`、`getDecisionData():toJudge()` | C++ 投影 `JudgeStruct::isGood(card)` 結果；支援常見判定、交換、Lightning 保留、拆牌偏好與最低 keep。FilterSkill、連鎖傷害、Beige 花色效果、最終改判能力缺資料時保持 unknown。 |
| `ai_use_revises`／`ai_useto_revises`／`ai_target_revises`／`ai_used_revises` | `tryUseCard` 的同一推演流程 | 使用 skill instance 的可見能力，重複技能去重；target revise 只重排完整權威組合。AOE 使用 affected targets；`used` 不在 dummy 推演觸發。另接 `ai_skill_use_func`、`ai_skill_carduse`、`ai_slash_prohibit`。 |
| mode 身份／意圖 hooks | `mode-ai.lua` 的 `onIntention`／宣告式 `intentions`，每 Room＋viewer 的純值 state | intention 是模式 hook 的原子更新入口；隔離查詢唯讀，事件更新才改 state。legacy `ai_card_intention` 等表尚未自動接成此事件來源，不能以登記表存在宣稱已搬遷。 |

## Mode、身份與 privacy 契約

`mode-ai.lua` 先按身份 hook，再按 mode hook；身份 hook 回 `nil` 才採該層預設，`onIntention` 每事件只執行選定版本一次。isolated 端只收到 `AIWorldView`／`mode_policy` 的 viewer-visible 值：`isModeManaged()`、`relationTo(target)`、objective 與 role visibility 查詢不可讀取 Room、Player、Card userdata，也不可在查詢中變更 state。

身份推測 state 必須按 Room 與 viewer 分區；`role_shown`／個別授權優先於推測，未授權身份保持 unknown。`onIntention(context, fromId, toId, level, state)` 是唯一的意圖變更入口，必須以 atomic 純值更新及 revision 失效快取。不要把 `isFriend`／`isEnemy` 當成取得秘密身份的後門，也不要以全域 `sgs.ai_role` 或另一觀察者 closure 復用狀態。

## 結果、coverage 與 acceptance

`sgs.ai_skill_*` 保留舊 callback 位置參數；無前綴的 isolated `ai_skill_*` 使用 `(self, options, request)`（use-card 為 prompt 版本）。兩種 ABI 分開登記，不能把同名表合併。`strategy-hooks.lua` 在共用 modules 後、套件 scripts 前載入；明確指定 `AiIsolatedScripts` 且載入 `decision-core.lua` 的配置，須同時載入 `strategy-hooks.lua`。

預設詢問已包含：頻率驅動的 skill invoke、choice 排除 `benghuai`、suit 權重、general 可選項、discard、AG、card chosen、Yiji、player(s) chosen、實體回應／show／pindian／救桃／無懈。隨機選擇使用 runtime 受控 RNG。Guanxing 雙向排序目前只覆蓋自己摸牌前且沒有待判定的基線：按需求排序全放頂；只有明確 draw count 才分頂／底，不推測摸牌數。

`nil` 是 `unhandled`，表示 isolated 尚未處理；`AIUnsupported` 是 unsupported，代表 isolated defect/debt 並記錄原因；`{kind="pass"}` 是已處理的 pass。安全 fallback 可在故障時保住流程，但任何 `unhandled`／unsupported／error 觸發都使 isolated acceptance 失敗。`"."`／空字串經 normalization 也是 declined/pass，但 compulsory pattern (`!`) 不接受 declined／pass。這三者不可合併成「沒有出牌」。

每個共用 registry 透過 `ai_coverage.declare` 申報；未註冊、過期、資料不完整、未知 conversion 或未知 target strategy 記錄 bounded `NotCovered`／`Error` metadata，並視為 isolated acceptance debt。逐候選 unknown 不會取消同一 request 中已知完整合法 action；只有沒有可行 action 且仍有 unknown 才使該 request NotCovered。已知為空和列不完必須分開。

目前仍有 standalone debt：逐武將／逐技能 activation、conversion／view-as 的所有 pattern 與 cost 語意、`cardEffect` 依賴事件上下文、完整排序與牌值模型，以及需要 native effect／move／userdata 的 response。這些路徑是 `NotCovered`，不能把安全 fallback 或參考 SmartAI 行為當作 isolated 完成。

## 複雜度與擴展方式

| 工作 | 成本與限制 |
|---|---|
| Player／候選／conversion／選項索引 | 建立 O(n)，之後按 ID 平均 O(1) 查找；回傳副本保護索引來源。 |
| 單選最低 keep、AG、Pindian | 卡片 metadata 一次建立索引，候選單次掃描 O(n)。 |
| viewer 關係與距離投影 | 關係只呼叫 viewer 對各人的 hook；距離保留 viewer 的雙向邊，原生 distance 呼叫次數 O(n)。未投影的其他玩家配對維持 unknown；原生 hook 成本另計。 |
| 排序／多張 discard | 評分預計算，排序 O(n log n)；不宣稱全流程 O(n)。 |
| 目標組合／轉化成本 | 權威端有探測 budget，耗盡為 incomplete；固定上限 k 的獨立成本選擇 O(k n)，不枚舉手牌子集。 |
| 給牌需求 | 共用救命／裝備優先項先預分類；任意技能 `ai_cardneed` 仍須牌 × 適用 hook 比對，不能宣稱這部分為 O(n)。 |

時間推演 (Planning) 按 viewer 保存純值；target／cost 授權券同時綁定 decision ID 與 state revision，跨 request 的一般 intent 只在 revision 未變時保留。`ai_coverage.outcomes()` 提供結果分類計數，與 registry 的宣告覆蓋率分開；沒有對局量測就不能把宣告數當作實際覆蓋率。

`respond_card` 目前只接受實體牌答案：show／pindian 是實體限定問題；conversion 逐項檢查，已知完整合法的實體 action 可先回答，未知 conversion 另記錄。持有有效 V1 view-as 的 response 保守視為未知；合法轉化閃／桃不能被空實體候選吞掉。

新增共用策略時，優先新增純值 facade 欄位、明確 registry handler 與 coverage declaration：

1. 由 C++ 投影 viewer-visible、request-scoped、可重驗的資料及完整性旗標。
2. 在既有 class／skill registry 掛 handler；不要在核心寫角色或武將名稱硬編碼。
3. handler 只回 `AIUsePlan`／值型 result；未知資料回 `AIUnsupported`。牌策略不填 plan 代表該牌不使用；request handler 的 `nil` 是未處理，明確拒絕整題才回 `{kind="pass"}`。
4. 逐項評估 request 候選並保留 unknown 記錄；只要有已知完整合法 action 就交給 normalization，沒有 action 且仍有 unknown 才回 NotCovered。權威端仍重驗 pattern、method、ticket、target、成本與 revision。
5. 補 source fixture 與 coverage case；不要以單一成功案例代表全量 SmartAI parity。

## 驗證狀態

2026-09-21 已完成建置、契約與生命週期測量驗證。

相關設計邊界：[`docs/ai-identity-mode-decoupling-plan.md`](ai-identity-mode-decoupling-plan.md)、[`docs/smart-ai-adapter-dependency-audit.md`](smart-ai-adapter-dependency-audit.md)、[`docs/lua-ai-spec.md`](lua-ai-spec.md)。

## Scarlet 參考技能所需共用接口（2026-09-25）

新增 addHandPile／getAllPeachNum、draw 目標推薦與無牌普通傷害接口。具體覆蓋與未覆蓋分支、原版保留行為及靜態檢查點見 [Scarlet isolated AI](scarlet-isolated-ai-examples.md)。此批不是全套 SmartAI 傷害／推薦策略完成證據。
