# Isolated AI 共用層對照

本文 `lua/ai/` 路徑指部署檔案；版本與取得方式見[外部 Lua 來源](lua-ai-spec.md#外部-lua-來源)。

本文件是目前 standalone `lua/ai/isolated/` 純值 AI 架構的維護錨點。原版 SmartAI 只作行為參考；共用層接收 viewer-scoped 純值 (pure values)，提出值型答案，再由 C++ 權威端重驗。安全 fallback 只是故障保護；任何 `unhandled`／unsupported／error 觸發都使 isolated acceptance 失敗。

## Shared core ownership

估值與查詢兩個共用檔按責任分區，檔頭列出導航索引；hook 契約、預設值政策及目標推薦另有唯一所屬模組。分區保留既有 lexical scope 與註冊順序。新增方法先選唯一所屬檔案，另一檔透過 `self` 呼叫；不得用同名宣告或「先提供簡版、後覆寫完整版」決定策略。

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

`tools/ai/check-ai-runtime-manifest.py` 同時檢查四個共用決策／查詢／hook 模組的具名 `SmartAIView` 方法與 alias 的唯一所屬，以及上述主公查詢的所屬檔案；可加 `--shared-methods-only` 單獨檢查方法邊界。檢查涵蓋 `do` 區塊內宣告，忽略註解／字串；動態 required-query decorator 是既有 provider 的包裝，保持明確的末尾接線。這是靜態檢查，不代表對局或策略等價驗收。完整覆寫 `AiIsolatedScripts` 的配置仍須明列其使用的共用模組，不會自動補載。

### Hook 模組邊界

| 模組 | 唯一責任 |
|---|---|
| `strategy-hooks.lua` | 綁定原有 `sgs` hook 表、別名與可見技能；按階段及順序呼叫同一表內的具名 callback。階段資料只存 hook key／rule／order，不存另一套政策函式。 |
| `hook-defaults.lua` | 原有技能分類與牌傷害屬性的定義資料；不再放手牌、體力、傷害或防禦的技能政策。 |
| 各套件的 `<package>-ai.lua` | 自己的技能政策、原有 `sgs.*[skill]` callback 及其階段資料。新增技能政策修改所屬套件，不集中加入共用 defaults。 |
| `target-recommendation.lua` | 目標推薦機制、評分、排序與來源守衛。 |
| `decision-core.lua` | 共用估值流程、數值查詢守衛、階段呼叫；保留一般牌型分派、角色／體力等共用算式。 |

必要模組的順序仍由 `isolated-bootstrap.lua` 的 `ai_isolated_core` 宣告。套件 handler 仍以啟用套件與同名 AI 檔探索；此次補上 `FCGod-ai.lua`、`ol_ccxh-ai.lua`，其餘沿用已有套件檔。明列 `AiIsolatedScripts` 的配置必須包含所需套件 provider，不能只載 defaults 便假設全部技能政策存在。

### 套件政策使用原有 sgs hook

前幾批集中在 defaults 的特例已分拆為 105 個套件內分支，放回 52 個 isolated 套件 AI 檔；五個原先直接在 core 登記的傷害 callback 也移回來源套件。舊表與正常 callback 的前置參數保持原樣：

| Hook | 原有參數 |
|---|---|
| `sgs.ai_ajustdamage_from[skill]`／`ai_ajustdamage_to[skill]` | `(self, from, to, card, nature)` |
| `sgs.ai_skill_defense[skill]` | `(self, player)`，保留數值常數的原有效果 |
| `sgs.ai_getLeastHandcardNum_skill[skill]` | `(self, player, least)` |
| `sgs.ai_getBestHp_skill[skill]` | `(owner)` |
| `sgs.ai_need_kongcheng[skill]` | `(self, player, keep)` |
| `sgs.ai_getOverflow_skill[skill]`／`ai_hasManjuanEffect_skill[skill]` | 沿用前批已存在的 isolated 擴充表，分別以 `(self, player, current)`／`(self, player, manjuan_only)` 接收一般呼叫 |

原回呼只靠加減值，無法表達屬性改寫、傷害封頂或原本較早發生的短路。因此階段呼叫會在原參數後附加當次純值 `context`；一般呼叫不帶 context，保留該 hook 原有回值。套件 wrapper 保存自己原有的 callback，只有 `sgs.isValueHookContext(context)` 確認的階段呼叫才執行新增分支。這不是回到 SmartAI 或 gameplay VM；所有 provider 均在 isolated VM。

`context` 含 `phase`、`rule`、`value`、`query`、`extra`；傷害 value 同時保存 `damage`／`nature`，其他入口沿用數字或布林值。damage ABI 的 `extra` 固定為 `{from, card, helpers}`，套件政策一律在 `extra[3]` 取 helpers；`cantDamageMore` 的 cap 查詢沒有牌，card 槽補 `nil` 不可省略。未知值守衛與可見標記、角色及卡牌觀測 helper 仍來自核心，不新增 native 物件查詢。回 `(value, terminal)` 的 `terminal=true` 保留原有早退，包含 terminal nil。

套件用 `sgs.declareValueHookStages(registryName, skill, declarations)` 只申報 `{phase, rule, order}`；核心用 `sgs.runValueHookStages(...)` 在執行時取得 `registry[skill]` 呼叫。相同 phase／skill／rule 的重複申報原位替換，排序不依賴套件載入次序。覆寫政策仍覆寫原來的具名 hook slot，沒有獨立的 `_ordered` callback 表。`sgs.getValueHookKeys` 只供 coverage 列舉，不代表策略驗收。

### 保留的順序與組合

| 入口 | 保留的行為 |
|---|---|
| 最低留牌 | 連營等同效替代保留一次合併的 `hasSkills` 可見性查詢，再進隊友補牌與傷逝；原 connections callback 順序不變。 |
| 空城偏好 | 分 keep、失牌收益前與失牌收益後；各知己變體及其他技能回自己的套件。 |
| 最佳體力 | 龍魂、魂姿與覺醒等保留首個命中優先。原五鍵 `pairs` 的列舉形狀僅保留為相容順序資料，甘露／英魂／秘計／血祭／豹變的條件與數值在所屬套件。 |
| Overflow／漫卷 | `isMax` 的提前回傳、viewer 手牌數來源、只讀一次 phase 及 `manjuan_only` 邊界保留。 |
| 傷害 | 免傷、屬性、原來源 hooks、卡牌修正、原受傷方 hooks、官渡事件、後置修正與鐵索傳導的先後不變。牌型分類仍屬共用流程，裸衣、忠義、喊勇等政策分回各包。 |
| 防禦刷新 | 一般分類與狀態算式仍共用；兩次誓仇加分及三種援護分回各包，保留查詢順序與每種援護只取首個符合者。 |

`getDamageValueContext()` 只向套件內原五參數傷害 callback 提供原本的 query／觀測 helper；既有 scalar callback 的數值及條件不改。

上述套件遷移之外，後續批次也將原 core 的生存與目標判斷接上同一 stage runner，見下節。語法與靜態接線比對不代替實局策略等價驗收；既有套件載入或語法阻擋需分開記錄。

### 生存、Slash 防禦與傷害目標

本批 184 條生存、Slash 防禦與傷害目標政策放回 60 個所屬套件 AI，並以同一 stage runner 分派。新增的 `h_momentum-ai.lua` 與 `sijyuoffline-ai.lua` 也是可選套件 handler；共用 defaults 不保存這些政策。八卦、延時錦囊等通用牌政策由既有的標準牌／軍爭模組提供；`CanUseAttackSkill` 的組合判斷保留在原來源 Touhou 套件。

同一組跨套件短路條件保留完整 callback，於涉及的套件以相同 key／phase／rule 宣告；重複宣告原位替換，因此 203 筆套件宣告只產生 184 條政策。未載入其 provider 的套件政策不執行；明列 `AiIsolatedScripts` 時也須包含需要的套件。完整載入相關 provider 時，仍依原先的數值順序與查詢順序執行。

| Registry | 入口與階段 | Stage callback ABI |
|---|---|---|
| `ai_skill_defense` | `isWeak` 的 `weakness_early`／`weakness_hp`；`cantbeHurt` 的 `retaliation` | `(self, target, context)` |
| `ai_need_damaged` | `canLoseHp` 的 hostile／weak／lethal 前後階段；`needToLoseHp` 的傷害前、Slash 武器前後、legacy 前後與最佳體力階段 | `(self, from, to, card, context)` |
| `ai_defense_slash` | `getDefenseSlash` 的八卦／已知閃、護駕、雙閃、大喝、潛襲、體力、目標、手牌、屬性及尾段政策 | `(self, target, observer, context)` |
| `ai_target_damage` | `isGoodTarget`、`canAttack`、`findPlayerToDamage`、`justDamage`、`isGoodChainTarget`、`CanUseAttackSkill` | `(self, target, context)` |

`ai_need_damaged` 與 `ai_defense_slash` 的原 connection callback 仍在 core 原位置，以原參數呼叫。單套件政策沿用技能 slot：wrapper 只處理精確匹配的 phase／rule，其他 stage 含 context 委派前一個 callback，普通呼叫則保留原 ABI；跨套件組合使用穩定的具名前綴 key。`getDefenseSlash` 的護駕閃數及共享絕情判斷放在當次 context；未載入絕情政策時允許受傷收益判斷。連鎖反傷同時傳回分數與 kill-lord 狀態；均不使用跨 request 全域快取。

同 phase 以 `order` 決定先後。回 `context.value` 表示繼續；回 `(nil, true)`、`(false, true)` 或 `(value, true)` 都是終止，不能以 `if value` 代替 terminal 判斷。既有合併 `hasSkills` 查詢、查詢早於技能條件的都督／技能狀態觀測、亂數消耗位置及遞迴 guard 保留。

例如套件可新增一項受傷收益否決，而不修改 `needToLoseHp`：

```lua
do
    local previous = sgs.ai_need_damaged.example
    sgs.ai_need_damaged.example = function(self, from, to, card, context)
        if sgs.isValueHookContext(context) then
            if context.phase == "need_hp_before_damage" and context.rule == "example" then
                if context.query(from, "hasSkill", "example") then return nil, true end
                return context.value
            end
            if type(previous) == "function" then return previous(self, from, to, card, context) end
            return context.value
        end
        if type(previous) == "function" then return previous(self, from, to, card) end
    end
    sgs.declareValueHookStages("ai_need_damaged", "example", {
        {phase="need_hp_before_damage", order=100, rule="example"},
    })
end
```

### 共用 query／q 契約

具名 `query`／`q` 查詢以 `AIValue.bindQuery` 宣告 `missing`、`known`、`known_nil`、`resolve` 或必要的資料完整性政策。純轉接的 `call`／retrial `q` 直接採函式別名。一般 connection、Yongdi、事件與套件查詢保持 strict nil；standard equipment 只允許原 provider 的已知 nil；Touhou 的無武器／無主公與 nullable provider 仍逐項核實，不因使用共用介面而擴大已知空值範圍。事件 helper 也統一在此傳播 `AIUnsupported`，不再讓它流入後續政策。

`GoodChaintoRecover` 與固定來源查詢的技能 false 仍要求完整技能投影；`isWeak` 保留外部隱匿狀態的明示估計。標記／旗標 remap 只用於原先有此契約的查詢，不加到 strict getter。這些是 observation policy，而不是另一層純 function-call 包裝。


## 原版入口與目前共用 API

| SmartAI 原版入口 | isolated 共用層 | 目前語意與邊界 |
|---|---|---|
| `SmartAI:askForUseCard` (`lua/ai/smart-ai.lua`) | `ai_register_handler("use_card", ...)`、原生 `ai_skill_use`、`ai_card_response_use` (`lua/ai/isolated/ask-for-use-card.lua`) | 全套件 use callback 統一 `(self, prompt, request)`；保留 skill → pattern → prompt 與 compulsory `!`。ResponseUse 支援 Slash、Peach、Jink、ExNihilo、裝備、Duel、順拆、樂／兵與四張標準全域牌；逐候選檢查 legality、priority、支付 keep 與 ID，共用 `bindConversionCosts`。已知完整合法 action 可在同題仍有 unknown 時回答；沒有可行 action 且仍有 unknown 才記 NotCovered。 |
| `SmartAI:activate` → `getTurnUse`、`aiUseCard` (`lua/ai/smart-ai.lua`) | `ai_register_handler("activate", ...)`、`ai_card_use`、`AIUsePlan`、`SmartAIView:tryUseCard` (`lua/ai/isolated/decision-core.lua`) | 以候選牌、值型 card spec、target names、skill instance ticket 規劃；`AIUsePlan:toAnswer()` 只描述意圖，真正造牌／扣成本／提交留在 C++。 |
| `useCardByClassName`、`findPlayerToUseSlash`、`getBestTarget` | `planTargetSequence`、`relationTo`、`ai_card_use.<ClassName>`、`ai_card_effect` | 目標選擇使用權威端列出的 target combinations；managed mode 才能使用 friend/enemy relation。效果特殊規則以 class/name registry 擴展，不在核心增加武將分支。 |
| `askForChoice`／`askForCard`／`askForPlayerChosen` | `ask-for-choice.lua` 的 request handlers、candidate index 與 relation helpers | 被選 action 缺資料或不完整投影才維持 unsupported／NotCovered；同題其他 unknown 不得被當 pass，也不以 native userdata 回查。 |
| `doDisCard`、`poisonCards`、`getCardNeedPlayer`、`cardNeed` | `decision-core.lua`，由順拆、選牌、Yiji、Guanxing 預設消費 | 分友敵、手／裝／判定區與損失收益；給牌先處理救命／裝備，再呼叫 `ai_cardneed(to, card, self)`。只評估投影事實，不代替合法性檢查。 |
| `sort`／`sortBy*` 的原清單排序 | `smart-ai-functions.lua` 的 `sortInPlace`、`sortByKeepValueInPlace`、`sortByUseValueInPlace`、`sortByUsePriorityInPlace`、`sortByDynamicUsePriorityInPlace`、`sortByCardNeedInPlace` | 套件需要排序副作用時直接使用共用方法，轉送排序鍵、反向與 flags 並回傳原清單；一般 `sort`／`sortBy*` 仍回傳副本。寫回僅覆蓋結果前綴，保留既有轉接語意；`flags="j"` 的副本過濾不代表原清單尾端已刪除。自訂比較器、來源操作與外部持牌者隱私檢查仍由原策略負責。 |
| `needKongcheng`、`getBestHp`、`needToLoseHp` | 手牌／血量共用規則與 `ai_getLeastHandcardNum_skill`、`ai_getBestHp_skill`、`ai_need_damaged`、damage adjustment hooks | 保留原版關鍵優先序、精確 callback ABI 與 false；傷害只屬公開基準加 hook 的策略估算，未等價移植全部原生傷害分支。另提供 `dodiscard`、`aiUsecard`、`needToloseHp` 相容拼法。 |
| `needRetrial`、`getRetrialCardId`、`getFinalRetrial` | `retrial.lua`、`getDecisionData():toJudge()` | C++ 投影 `JudgeStruct::isGood(card)` 結果；支援常見判定、交換、Lightning 保留、拆牌偏好與最低 keep。FilterSkill、連鎖傷害、Beige 花色效果、最終改判能力缺資料時保持 unknown。 |
| `ai_use_revises`／`ai_useto_revises`／`ai_target_revises`／`ai_used_revises` | `tryUseCard` 的同一推演流程 | 使用 skill instance 的可見能力，重複技能去重；target revise 只重排完整權威組合。AOE 使用 affected targets；`used` 不在 dummy 推演觸發。另接原生 `ai_card_use(self, card, use, request)`、`ai_skill_carduse`、`ai_slash_prohibit`。 |
| mode 身份／意圖 hooks | `mode-ai.lua` 的 `onIntention`／宣告式 `intentions`，以及 `event-intention.lua` 的純值事件 consumers | 每 Room＋viewer 的 state 以原子 delta 更新，隔離查詢唯讀。已消費 card intention、player/Yiji/skill choice、damage 與 visible-skill callbacks；仍須逐項核對投影與 ABI，不能以登記表存在宣稱所有 legacy 策略已搬遷。 |

### 牌型共用的目標推薦

轉化牌依轉出牌的名稱／類別進入既有 `ai_card_use` 策略，與實體牌共用選目標流程；不按轉化來源技能登記白名單，也不在技能檔另選一輪目標。

共用分類、持有者限制、評分器與排序位於 `lua/ai/isolated/target-recommendation.lua`；決策核心與套件牌型只消費這一份實作。

| 牌型 | 推薦接入方式 |
|---|---|
| Slash（含屬性殺） | 敵人主分支沿用 `getBestTargetOr`，保留合法性、額外目標及友方特殊優先序。 |
| Duel、Indulgence、SupplyShortage | 在牌型原有數值評分上加入 `targetRecommendAdjust`；Duel 原分數方向相反，先取負值。 |
| Snatch／Dismantlement、FireAttack、IronChain、Collateral | 在各牌型既有規則分支內按推薦修正排序，同分保留原順序；保留拆牌區域、火攻花色、鐵索解鎖／連鎖／重鑄及借刀持武器者／受殺者配對。Collateral 的推薦作用於持武器者，第二人仍按持有者的殺規則選取。 |
| 擴充牌的自由選人分支 | 套件在牌型策略建立 `targetRecommendScorer(card)`，把原有候選分組交給 `orderTargetsByRecommend(targets, scorer)`；例如出其不意、逐近棄遠、刮骨療毒、草木皆兵、射擊及可選他人的裝備。別名與子類沿用原牌型方法，不另加一層推薦。 |
| 配對牌與勢力組別 | 聲東擊西、調劑鹽梅分別按交出／獲得牌的角色建立推薦情境；合縱連橫、聯軍盛宴比較完整效果組別，組內否決會淘汰整組，不裁切固定受影響者。 |
| 自用、固定目標、全域或純回應牌 | 沿用各自牌型流程，不另外選取目標或裁切全域效果。 |

每個牌型／效果角色建立一個評分器，按目標快取加減分與 `false` 否決；每次回傳原始基準分加 delta，不把上次結果再加一次。【殺】的推薦首選、額外目標及備援共用同一評分器，所有 `add()` 分支都遵守否決。排序與估值可以重跑，但不累加先前計劃的推薦分。同分不新增亂數消耗，未投影資料仍傳遞 unsupported；推薦不取代權威端的目標組合與轉化票驗證。

`registerTargetRecommend` 限定目標自身技能：只消費 `owner` 與目標同人的掛鉤，避免場上另一名同技能持有者重複加分；跨玩家效果仍走原有 owner-aware／global 掛鉤，不按技能名全域去重。牌物件以 `isDamageCard()`、公開效果 subtype 與類別分類，套件可用既有 `registerSkillCardType(class_name, kind)` 補充牌型效果；分類同時適用實體牌與轉化牌，不用登記轉化來源技能。

新牌型仍須實作自身選目標策略並在候選分組使用上述 helper；固定座次決定的火燒連營、全域效果、純回應及重鑄空目標不改成自由選人，不能因為可轉化就宣稱已有策略覆蓋。

## Mode、身份與 privacy 契約

`mode-ai.lua` 先按身份 hook，再按 mode hook；身份 hook 回 `nil` 才採該層預設，`onIntention` 每事件只執行選定版本一次。isolated 端只收到 `AIWorldView`／`mode_policy` 的 viewer-visible 值：`isModeManaged()`、`relationTo(target)`、objective 與 role visibility 查詢不可讀取 Room、Player、Card userdata，也不可在查詢中變更 state。

身份推測 state 必須按 Room 與 viewer 分區；`role_shown`／個別授權優先於推測，未授權身份保持 unknown。`onIntention(context, fromId, toId, level, state)` 是唯一的意圖變更入口，必須以 atomic 純值更新及 revision 失效快取。不要把 `isFriend`／`isEnemy` 當成取得秘密身份的後門，也不要以全域 `sgs.ai_role` 或另一觀察者 closure 復用狀態。

## 結果、coverage 與 acceptance

尚未遷移的 `sgs.ai_skill_*` 保留舊 callback 位置參數；無前綴的 isolated `ai_skill_*` 使用 `(self, options, request)`。兩種 ABI 分開登記，不能把同名表合併。`ai_skill_use` 家族已完成全套件遷移，只使用 `(self, prompt, request)`；退役 `sgs.ai_skill_use`、legacy registry 與註冊 API。國戰 provider 的局部表、跨套件別名及狀態包裝均指向同一原生契約，保留原有策略與普通／Original 分流。三個 hook 模組依上述順序在共用 modules 後、套件 scripts 前載入；明確指定 `AiIsolatedScripts` 的配置須遵守相同依賴。

`tools/ai/check-ai-runtime-manifest.py --native-use-only` 可單獨核對本批來源契約，完整 manifest 檢查亦包含此閘；註解與字串不作登記。這是靜態介面檢查，不代表策略等價或對局驗收。`sgs.ai_skill_use_func` 亦已遷移至原生 `ai_card_use(self, card, use, request)`，以同一 `AIUsePlan` 填 `use.card`／`use.to`；保留卡種原有查找順序、provider scope、狀態包裝與別名。第四參數為該卡候選原先綁定的純值 request，省略的內部 request 不借外層身分。其他詢問家族仍須分批遷移；共用數值表與 `sgs.Card_*` 常數不屬於本批退役範圍。

invoke 已統一為 `ai_skill_invoke(self, options, request)`，同一靜態閘亦禁止舊表及舊函式參數。一般模式保留 truthiness，Original 保留 strict true；移植的空結果明確回 false，原生 nil 仍是未處理。Original 內部的 `inquiry_scope="original_internal"` 保留原先 handler 可見範圍及未命中時的三態結果。套件的 mode 局部表、共用別名、狀態 wrapper 與 ownership 檢查均使用原生 registry。

`SmartAIView:inquiryInput(kind, reason, context, options)` 只建立詢問用的純值 `options, request`，不附行動候選或票券、不修改外層 request。`context_scope="supplied"` 讓 `getDecisionContext(request)`／`getDecisionData(request)` 只使用指定資料，包含明確空 context；玩家／牌／清單依既有 view 型別轉回純值，未知資料仍 unsupported。Overseas 的 IntList 投影只提供原本已知的 length，不推測內容；既有未提供的 QVariant 轉換方法仍未覆蓋。

fill 統一到唯一 `ai_fill_skill` registry，局部 `getTurnUseCard` 與登記回呼均為 `(self, options, request)`；第二參數的 `inclusive` 保留原值，第三參數只傳該技能的 raw request。`fillSkillCards` 仍依當時候選數決定 inclusive，保留 Card／Conversion／陣列／nil、候選順序、instance admission、來源六欄匹配及 raw request 快取。mode 私有 provider、metatable 登記、狀態 wrapper 與跨技能引用同時使用原生表；未提供 request 的推薦仍明確傳 nil。靜態 ABI 閘亦檢查 fill 與具名 `getTurnUseCard`，共用數值表及常數仍不在退役範圍。

choice 統一到唯一 `ai_skill_choice(self, options, request)`，不再建立 legacy choice registry／`sgs` 別名。`options.choices` 是純值候選陣列；覆寫記憶優先、scalar lifecycle、別名與最後寫入者順序保留。移植政策的 `ai_choice_result` 將一般非字串結果標成 `choice_default`，由 dispatcher 直接完成預設；既有 native nil 保留未處理與 `legacy_hook` 缺覆蓋判定。Original 函式回傳數字先轉文字，空值／非文字保留原空答案旗標；scalar 非字串註冊值則維持直接預設。內部 scope 保留原始值與省略的 choices／data，不借外層結果邊界。既有 11 個 native handlers 的輸入缺欄位、nil／空字串及優先序保持原義。

native registry 的 `__pairs` 用於覆蓋盤點，不代表可擴大原先 proxy 的策略包裝範圍。OL 的 choice／invoke 原本不參與通用 state wrapper，保持不包裝；gaoda／sfoflCard／sgs10th 的 card-proposal loop 同樣不處理 choice／invoke。fill 原是 plain table，既有 fill／use／card_use 包裝仍保留。來源契約閘涵蓋以上五族，Lua 語法與 manifest 通過仍不代表玩法驗收。

預設詢問已包含：頻率驅動的 skill invoke、choice 排除 `benghuai`、suit 權重、general 可選項、discard、AG、card chosen、Yiji、player(s) chosen、實體回應／show／pindian／救桃／無懈。隨機選擇使用 runtime 受控 RNG。

### 標準諸葛亮／觀星 V2

武將註冊 ID 為 `zhugeliang`，技能為 `guanxing`／`kongcheng`，沒有 `nos_zhugeliang`。觀星規則已使用 `TriggerSkillV2`；isolated 的 `guanxing.lua` 以 `ai_skill_guanxing.guanxing(self, options, request)` 接收 native 統一的 `guanxing` reason，沿用當次可見牌 ID、方向與既有模式策略，回傳 `answer.cards`／`bottom_cards`。回傳前驗證兩堆恰好分割 offered IDs，禁止遺漏、重複、額外牌與方向不符；native 仍負責最終驗證。native 對觀星揭示的牌與五穀相同，投影 viewer 持有時的 acquired-card evaluation（residue／limits／play candidate），不含印刷牌評估；排序估值與自摸挑牌依此作答。

| 分支 | 策略與資料邊界 |
|---|---|
| 發動、空城 | 沿用頻率驅動的純值 invoke，以及 `needKongcheng` 的保牌／給牌 consumers；不另建同義策略。 |
| 雙向觀星 | 依當前角色尚未經過的階段排序：判定（僅判定階段前且未跳過）、摸牌（自己／友方給好牌，敵方給最差牌，關係不明則不規劃後續），再到下一位行動角色的判定；當前回合已過出牌階段時，再依兵糧結果安排下一位的摸牌。同名延時錦囊按判定位置分槽，避免重複分配牌。 |
| 下一位判定 | 只有確定 friend／enemy 才做有利／不利安排；unknown 保持未覆蓋，不視為敵方。 |
| 置頂／置底 | 置頂沿用雙向觀星的同一消費順序規劃，未規劃的牌接在其後；置底全放牌堆底。兩者均受當次方向與牌集合驗證。 |
| 後備武將控底 | 限 `02_1v1` 進入；一般模式不讀私有 `1v1Arrange`。1v1 未投影的後備武將資料仍是未覆蓋。 |
| 模式覆寫 | 透過現有 `self:askForGuanxing` 分派，保留 Original 國戰自己的排序。 |

此變更僅涉及 isolated AI；SmartAI 與原生規則未改。策略實作與靜態接線不代表實局觸發、策略強度或全部模式已驗收。

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

## 標準無雙的純值回應

標準呂布的 `wushuang` 是鎖定技，沒有額外的發動決策；AI 策略在殺／決鬥規劃及對手的回應詢問中消費。以下入口位於 `isolated/standard-ai.lua`、`standard_cards-ai.lua`，以無前綴 registry 接收當次純值 request。

| 原策略分支 | 純值入口與資料 | 保留的決策與邊界 |
|---|---|---|
| `@wushuang-slash-1` 首殺提示 | `ai_skill_cardask`；當次 effect、prompt 對手與 offered cards | 拒絕受傷／解怨／無言；殺不足兩張時保留單牌空城例外；明確回牌或 pass |
| `duel-slash`，含原生無雙追加殺 | `ai_skill_cardask` → `selectDuelSlash` → `cardResponseAnswer` | 保留覺醒、無言、武魂、雙殺階段旗標、仁德／節命、失血收益、憤勇、傷害禁忌與友敵分支；未知關係為 unsupported |
| 雙閃開始／後續提示 | `ai_skill_cardask` 或 `ai_skill_use` 的 `@multi-jink-start`／`@multi-jink` → `selectMultiJink` | 使用當次 prompt 的剩餘閃數；保留解怨、空城／古錠刀及變體分支，數量缺失或不是正整數為 unsupported |
| 一般閃策略 | `selectSlashJink(..., true)` | 直接使用純值 nullfilter；保留共用閃牌選擇及副作用，不經 positional callback |
| 實體牌／轉化答案 | `cardResponseAnswer`／`useCardResponseAnswer` | 實體牌使用可見牌 ID；評估用轉化須先綁定當次 receipt，再回 `card_spec`；不以 nil 隱藏轉化綁定失敗 |
| 主動殺／決鬥規劃 | 既有 `ai_card_use.Slash`／`ai_card_use.Duel` | 沿用既有純值牌型策略、無雙判斷與目標推薦，本次不重寫策略估值 |

決鬥交替應答時，`effect.from` 仍是原始出牌者；`duel-slash` handler 以當次 prompt 決定對手。雙閃的 use-card 詢問依既有公開 CardEffect 事件建立 request-local context，在還原外層 request 前完成選牌與轉化綁定。其他套件直接呼叫的 `sgs.ai_skill_cardask["duel-slash"]` 保留薄轉接，原生詢問由純值 registry 優先回答；無雙與雙閃入口不呼叫該轉接。

無雙首殺及決鬥回應以 `ajustDamage(對手, 本次回應者, 1, 當次決鬥牌) == 0` 判斷免傷，不在詢問 handler 列舉無言技能。`standard-ai.lua` 在既有傷害 registry 的 `before_nature` 階段登記 `wuyan` 錦囊免傷；絕情類替代效果由 `ajustDamage` 的既有前置流程處理，其負值結果不當作零傷害。憤勇分支仍使用共用 `hasJueqingEffect(對手, 本次回應者)`。

本次只改 isolated Lua。決鬥既有 `wushuangSlash` 階段旗標仍走 viewer 的策略值；原生 receipt 的剩餘回應數未在本次新增投影。這份策略帳表示入口與策略實作，不代表額外 amount、多實例及各分支的實局等價性已驗。

## Scarlet 參考技能所需共用接口

新增 addHandPile／getAllPeachNum、draw 目標推薦與無牌普通傷害接口；覆蓋與未覆蓋分支見 [Scarlet isolated AI](scarlet-isolated-ai-examples.md)。

`lua/ai/isolated/smart-ai-functions.lua` 補上套件會呼叫的 SmartAI 相等函式（可見牌選取、殺是否有效、命中、棄牌、找人出殺、跳過出牌階段、`getDefenseSlash`、`getWoundedFriend`、`getCardId`、`sortEnemies`、`updatePlayers`、`hasTuntianEffect`、`adjacentPlayers`、`isLordHealthy`，以及 `bignumber`／`slash` 牌需），未知投影回 nil。套件用 `sgs.append_skill_list`、`sgs.ai_slash_benefit`、`sgs.ai_valuable_card`、`sgs.ai_will_skip_play`、`sgs.ai_turn_over`、`sgs.ai_defense_slash`、`sgs.ai_hasTuntianEffect_skill`、`sgs.ai_jueqing_effect` 擴充，武將名不寫進這層。Scarlet、mcompetition、Shadow 的技能名單與策略分別在 `isolated/scarlet-ai.lua`、`isolated/mcompetition-ai.lua`、`isolated/shadow-ai.lua`；舊版同名檔的逐技能 callback 仍在 gameplay VM。字串 data、房間 tag、移動與玩家屬性沒有投影時，對應詢問維持 unsupported。
