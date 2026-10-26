# 新技能的 Isolated AI 寫法

新技能分成「遊戲規則」與「AI 決策」兩份。規則執行與最終合法性由 native／gameplay VM 負責；isolated AI 只讀當前觀察者可見的純值快照並提出答案。

移植既有 SmartAI 時，按[移植遵循文件](isolated-ai-migration-playbook.md)建立逐分支策略帳；已知失敗與未驗項見[2026-10-03 驗收](isolated-ai-acceptance-20261003.md)。

## 檔案位置

| 內容 | 外部 extensions 權威倉庫 | L 工作樹 |
|---|---|---|
| 技能規則 | `extensions/<package>.lua` | `extensions/<package>.lua` |
| 新 AI | `ai/isolated/<package>-ai.lua` | `lua/ai/isolated/<package>-ai.lua` |

套件啟用時，loader 會找對應 `<package>-ai.lua`；個別套件不加入 `ai_isolated_core`。`AiIsolatedScripts` 會覆寫預設載入清單，須自行包含所需核心與套件腳本。新增 isolated 檔案須登記 `docs/ai-runtime-manifest.json`（套件 handler 標為非 required）。修改 L 端後逐檔同步回外部倉庫。

## 選擇入口

| 技能詢問／策略 | 新 AI 入口 |
|---|---|
| 是否發動 | `ai_skill_invoke[reason](self, options, request)` |
| 選項 | `ai_skill_choice[reason](self, options, request)` |
| 選玩家／棄牌 | `ai_skill_playerchosen[reason]`／`ai_skill_discard[reason]`，同一新 ABI |
| 回應出牌 | `ai_skill_use[pattern](self, prompt, request)` |
| 主動技能 | `ai_skill_activate[skill_name](self, request)` |
| 主動出牌候選 | `ai_fill_skill[skill_name](self, options, request)`，`options.inclusive` 保留候選收集時的原值 |
| 自訂卡牌策略 | `ai_card_use[card_name_or_class](self, card, use, request)` |
| 估值、保牌、傷害等偏好 | 先找共用 helper 與 `strategy-hooks.lua` 已有 consumer |

鍵值對應實際 request 的 reason／pattern／技能名（不一定是顯示名稱）。新 ABI 的 `options` 不是 QVariant；尚未遷移的 `sgs.ai_skill_*` 相容表是 legacy ABI，參數位置不可混用。

`ai_skill_use` 已統一為 `(self, prompt, request)`，不再提供 `sgs.ai_skill_use` 或 legacy 註冊 API。處理方式與牌模式讀取 `request.handling_method`／`request.pattern`，技能來源讀取 `request.skill_action`；來源必須與 authority 提供的行動逐欄相符。內部估算若原本沒有技能 request，維持省略，不拿 `self.request` 補另一個技能的授權。

`ai_skill_invoke` 全套件統一為 `(self, options, request)`，不再提供 `sgs.ai_skill_invoke`。reason 先查原值，再查把 `-` 換成 `_` 的鍵；一般模式依 Lua truthiness 判斷，Original 國戰只接受 `true`。移植回呼原本的空回傳明確轉為 `false`；原生回呼的 `nil` 仍表示未處理。unsupported 必須先傳遞，不能轉成發動或拒絕。

跨技能詢問使用原請求的 `options, request`；原本沒有資料時明確傳 `{ }, nil`。需要合成資料時，以 `self:inquiryInput("skill_invoke", reason, {damage=damage})` 取得兩個純值參數，作為呼叫的最後一個運算式。context 可含既有的玩家／牌 view、AIList 與具名結構；helper 轉成純值快照，設定 `context_scope="supplied"`，不借用世界 context、LastCardEffect 或行動票券，也不切換外層 `self.request`。callback 用 `self:getDecisionContext(request)` 讀這份資料；不得再建立帶 `toDamage`／`toCardUse` 方法的假 QVariant。

`ai_fill_skill` 與登記用的局部 `getTurnUseCard` 使用同一原生三參數 ABI，沒有 `sgs.ai_fill_skill` 別名。第二參數是 options，第三參數是該技能實際的 raw request；來源檢查使用 `AIValue.isSkillActionRequest(request)` 與 `request.skill_action`。候選收集器按當時 `#cards < 1` 設定 inclusive，保留 Card／Conversion／候選陣列／nil 的回傳、順序與既有權威驗證。跨技能推薦原本沒有 request 時仍傳 nil，不補外層另一技能的來源身分。

`ai_skill_choice` 全套件統一為 `(self, options, request)`，候選是純值陣列 `options.choices`；不再提供 `sgs.ai_skill_choice` 或其 legacy 分派。直接回傳選項字串；原生 handler 的 `nil` 仍表示未處理，明確要求共用預設可回 `{kind="choice_default"}`。此結果只由頂層 dispatcher 消費，不存進跨詢問記憶。既有 scalar 選項字串維持原值與最後寫入者優先序。

移植政策使用 `ai_choice_result(value, options)` 完成每個最外層回傳，包括空回傳：一般頂層只接受字串，其他值要求預設；Original 頂層把數字轉成字串，非文字或空值保留原空答案旗標。原生 handler 自有的 `nil`／空字串語義不套這層轉換。頂層 scope 是 `ordinary_top_level`／`original_top_level`；內部呼叫使用獨立 options 與中性的 scope，保留原值，不能繼承外層的結果邊界。舊政策的字串輸入在頂層缺 choices 時仍為 `""`，內部原本省略時仍為 nil。Huashen 的額外參數改從 `options.xiaode_choice` 讀取。

## 國戰 strict 契約

國戰的二十個公開 AI callback（`activate`、`askForUseCard`、`askForSkillInvoke`、`askForChoice`、
`askForSuit`、`askForKingdom`、`askForGeneral`、`askForDiscard`、`askForAG`、`askForCardChosen`、
`askForYiji`、`askForPlayerChosen`、`askForPlayersChosen`、`askForGuanxing`、`askForTriggerOrder`、
`askForCard`、`askForNullification`、`askForCardShow`、`askForPindian`、`askForSinglePeach`）一律走
`Isolated`；`LegacyDirect`／`LegacyAdapted` 設定不會覆寫這條路由。`runAnswer`、`decideResponse`、`decide` 遇到未覆蓋、拒答、過期或錯誤時直接拒絕，
不回 SmartAI。一般模式保留既有 legacy 相容路徑；國戰 `GameReady`、`Gongxin`、`filterEvent`
與 legacy 初始化不提供 SmartAI fallback，handler 應使用值型事件與 request。

國戰回 nil 時，`AI_HEGEMONY_NOT_COVERED` 另記該次 outcome、reason 與 key，與決策 ID 對照。
這些是有長度上限的純值診斷；不執行額外 Lua callback、不保存題目或牌面，也不把未覆蓋改算 runtime fault。

Original `askForCard` 的理由策略回傳已知無卡結果時，接續同一 isolated VM 的
`response_card_default` 合法候選規劃，不要求 TrustAI 預設答案或額外的舊策略收據。
已知完整合法候選可以作答；其他候選的 unknown 仍保留 coverage 診斷，不能算完整覆蓋。
沒有可行答案且仍有 unknown 時維持 unsupported；合法 optional pass 與強制回應分開處理。

`world.hegemony_facts` 是 native 依 viewer 產生的純值快照。`shown`／`known` 身份與技能必須
分開處理，並只使用已提供的 `public_friends`、來源／`slots`、viewer 可見的 `card_counts`
（手牌與 Wooden Ox）、公開牌 `catalog`／`pile`、owner history／recovery 與 `duanchang`；
不要由名字字串或他人快照推測隱藏資訊。

原生 V2 cost／pay／effect 回呼的詢問資料，只在該回呼存活期間借用，巢狀技能結束後還原。
`heg_zhente`、`heg_qiao`、`heg_shejian`、`heg_anyong` 的玩家型詢問可取得同技能、同詢問者的
當次用牌／傷害純值；`jinghe_skill` 的受贈者可取得同次有效執行的已選目標集合。
投影仍依具名白名單提供 `original25.tags`／`original26.tags`，不公開整份 SkillContext，
也不從其他技能的 frame、通用 CurrentUse／CurrentDamage tag 或選項文字推測缺少的資料。

無懈的 `context.original26_nullification` 只在原生連鎖能對上同次用牌時提供
`source="native_nullification_chain"`、`chain_known=true`、`root_effect`、`times` 與 `type`。
`times` 是此根效果已接受的無懈反制層數；`type` 是被反制的國無懈已實際選定的全勢力效果。
失敗用牌重問不增加層數，其他巢狀錦囊不承接父連鎖，缺少關聯或選擇憑據時欄位保持 unknown。
Original 策略用此根效果與當題正反向判斷；native 的實際詢問與付款仍指向當前那張牌。
donor 讀 `NullifyingTimes`／`NullificatonType` room tags 或 `targets` tag 的原版無懈分支，
isolated 端一律改讀本投影的 `times`／`type`／`targets`，不掃 room tag；
未被 native 見證時欄位缺即 unsupported，不回退 tag、不猜層數。

知己知彼的 `skillChoice` 事件只向選擇者附上 `details.original_known_both_target`。
此名稱須來自同一次 `askForChoice` 的玩家型資料，並與事件 payload、行動者、理由及答案相符；
事件 callback 從純值事件輸入讀取，不借用另一題的 decision context，也不公開所看見的將名。

`@@heg_Triblade` 的 `context.heg_triblade.targets` 只列當次武器回呼提供的目標；材料與來源仍由
技能 action 重驗。`@@heg_jushou!` 是 `askForCard`／MethodNone 的 DummyCard 選擇，使用本題
`card_conversions` 票據，不能套用舊 `@@heg_jushou_select!` 用牌字串。已出票的 `@@skill` 產物
由同題 `canActivate`／`createCard` 證明身份，響應驗證不再把 `@@skill` 當成普通牌型；一般牌型
與實體牌答案仍必須通過 pattern 檢查。

設備 ViewAs 只能從 `request.skill_actions` 選已提供的
`equipment_activation=true` 項目。這類 action 的 `instance=0` 是 native
`prepareEquipSource` 背書的 physical equipment 收據，`physical_equipment` 只含純值
`{holder, equipment, skill, card_id}`；回覆使用
`skill_action={skill=..., instance=0, owner=..., equipment=true}`。不要自造來源、實例或
設備 metadata；native 會以同一 source／physical ID 重驗 quota、costs、targets 與合法性。

V1 view-as 只有 native 已提供 typed projection 時才列為可作答；未投影的國戰 V1 保持
`NotCovered`／未完成，不退 SmartAI。驗證時要分開 **submission validation**（manifest、ABI、
靜態契約）與最終 **gameplay** 觸發；前者通過不代表後者已驗收。

已登記的純值 `ai_skill_invoke[reason]` 在一般模式與 Original 國戰皆優先處理頂層詢問。Original 內部詢問明確標記 `options.inquiry_scope="original_internal"`；原先僅供頂層使用的 handler 在此 scope 回 `nil`，保留原本的可見範圍。內部沒有 handler 且非 Frequent 技能時仍回 `nil`，維持 GameRule 的三態候選選擇；頂層才完成布林答案。技能別名直接登記同一原生回呼，保留 provider 與最後寫入者優先序。

## 例：受傷才選擇回血

假設技能 `demo_recover` 詢問「是否接受無代價回血」，`demo_reward` 提供 `recover`、`draw` 選項：

```lua
-- lua/ai/isolated/demo-ai.lua
local function visible_hp(self, key)
    local hp = type(self.player.getHp) == "function" and self.player:getHp()
    local max_hp = type(self.player.getMaxHp) == "function" and self.player:getMaxHp()
    if type(hp) ~= "number" or type(max_hp) ~= "number" then
        ai_unsupported(key .. " requires visible HP", key)  -- 缺資料保留未覆蓋，不猜成健康或受傷
    end
    return hp, max_hp
end

ai_skill_invoke.demo_recover = function(self, options, request)
    local hp, max_hp = visible_hp(self, "demo_recover")
    return hp < max_hp  -- true 發動；false 明確不發動
end

ai_skill_choice.demo_reward = function(self, options, request)
    local hp, max_hp = visible_hp(self, "demo_reward")
    local preferred = hp < max_hp and "recover" or "draw"
    for _, choice in ipairs(options.choices or {}) do  -- 只回 authority 實際提供的選項
        if choice == preferred then return choice end
    end
    ai_unsupported("demo_reward has no supported offered choice", "demo_reward")
end
```

## 主動／轉化技能

1. 先完成 gameplay 的 V2 技能規則與選牌／目標／成本驗證，參考 [V2 規範](active-skill-v2-refactor-plan.md)。
2. 確認 native request 已提供該技能實例的 conversion ticket；`self:getConversions()`、`self:getConversion(id)` 取得的是純值票。
3. 結果是既有牌族時，用 `self:tryUseCard(conversion)`／`self:aiUseCard(conversion)` 共用規劃；前者返回 `plan, status`（`planned`／`declined`／`unsupported`）。
4. 新牌族寫 `ai_card_use` 填入 `use.card` 與 `use.to`（`AIList`），目標從當次候選組合選取，只回有完整 authority 支持的計劃。
5. `card_spec` 正規化後，native 仍驗 conversion ID、activation／source owner 與 instance、成本、request kind、目標與 revision。不拼 legacy 牌字串、`cloneCard` 或 `Card_Parse`。

一般 V2 轉化有現成投影，不必為每個技能新增 host registry；未知成本或 V1 轉化仍可能 `NotCovered`。`hasIndependentAIConversion()` 是 C++ 的嚴格 opt-in，只用於固定數量、獨立選牌且不改變生成牌身份／目標規則的技能。

推演計劃統一用 `AIUsePlan.dummy(is_dummy, extra, current)` 建構：省略參數時為 dummy、額外目標數 `0`、空的現有目標列表；明確傳入 `false` 時保留正式規劃語意。每次建構都有獨立的 `to`／預設現有目標列表；傳入的 `current` 保留原列表及元素，不複製或轉換。套件若需把玩家轉成名稱，只保留轉換後呼叫共用建構的薄轉接。

`AIUsePlan.new()` 只建構一般計劃，不接受 initializer；傳入參數會報錯。不要用 `AIUsePlan.new({isDummy=true})`，也不要在套件重寫 dummy 欄位初始化。dummy 標記控制規劃流程的副作用；唯讀牌推演仍使用 `self:evaluateCard()`／省略第二參數的 `self:aiUseCard()`。

## 資料與回傳界線

| 情況 | 寫法／含義 |
|---|---|
| 有合法答案 | 回當次 offered choice、ID／target 或授權計劃 |
| 明確不發動 | invoke 回 `false`；其他 request 只在允許拒絕時回 `{kind="pass"}` |
| 未覆蓋 | `ai_unsupported(reason, key)`；不拿空集合或 0 代替 unknown |
| handler 回 nil | dispatcher 繼續其他已接通策略／default，最終仍無答案才是未覆蓋。值型詢問若 SmartAI 有同理由 hook（C++ 標 `options.context.legacy_hook`），共用 default 讓位給 SmartAI |
| 關係 | 用 `self:isFriend`／mode policy；未知不等於敵人，不硬編碼角色 |
| 可見資料 | `self.player`、`self.room` 是 value facade，不是 native Player／Room |
| 跨詢問記憶 | 只存純值、按 viewer 分區；不保留 facade、userdata 或他人私有資料 |

`findPlayerTo*` 只在 projected candidates 上工作，不自行證明 distance／range／prohibition 合法。不覆寫整個 `ai_decide`，也不呼叫 SmartAI。

## 驗證

驗收 gate 與證據要求見[移植遵循文件第 9 節](isolated-ai-migration-playbook.md#9-檢查點與驗收)；倉庫不新增測試套件或 fixture。行為驗收須實際觸發：有答案、明確拒絕、必要資料未知、非法候選、借用／多實例身份；V2 conversion 另驗 native 重建與拒絕。

更多純值 API 與既有契約見 [Lua AI 規範](lua-ai-spec.md)、[共用層](isolated-ai-common-layer.md)。
