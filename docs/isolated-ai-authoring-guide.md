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

Original 出牌規劃允許單張牌的 dummy 推演回傳 unknown 後，繼續評估其他候選；失敗的
dummy 提案會丟棄，回復該候選前的回合預測欄位，並保留既有策略觀察前綴的記憶語意。
這不是完整的策略交易：原始技能、防具事實查詢仍維持 unknown，不改成 false；策略估算另行標示。
有其他完整提案時仍走正式策略與原生付款／目標驗證，並記錄
`AI_HEGEMONY_PARTIAL_COVERAGE`；此標記不代表該提案已獲原生接受或完整覆蓋。
沒有完整提案且仍有 unknown 時不可回 pass。正式策略執行失敗、原生拒絕、一般 Lua 錯誤
皆終止該題，不因這項候選隔離改選另一張牌；一般模式維持原有流程。

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

零材料且不需宣言的原生 `ActiveSkillCard` 行動可附 `empty_selection={card,candidate}`。
牌面來自同一 action 的原生 factory，候選沿共用目標枚舉與預算產生；缺少投影或
`complete_coverage=false` 不能視為已知無目標。這是策略查詢資料，不是 card conversion
或可提交票據；正式答案仍指名原 action，並由原生重驗來源、成本與目標。
技能行動的目標依其原生 target contract 判斷，不額外套用普通牌的付款或禁止條件。
`fillSkillCards` 保留一般技能與標記的順序，再補入尚未處理的精確裝備 action；
零實例裝備由 physical/source 收據驗證，不作 V1 處理，也不依技能名稱合併不同來源。

原生轉交家族由必載的 `hegemony-ai.lua` 持有，共用既有卡牌需求分配。
`heg_transfer` 使用當題原生出票的單張手牌；`jingfan`／`heg_Breastplate` 使用零材料行動，
已裝備牌只作 gift 評估，不放進選牌成本。各自保留實際產品 class、activation/source
與 physical equipment 身分；只有原生完整候選中的友方可成為收受者。


V1 view-as 只有 native 已提供 typed projection 時才列為可作答；未投影的國戰 V1 保持
`NotCovered`／未完成，不退 SmartAI。驗證時要分開 **submission validation**（manifest、ABI、
靜態契約）與最終 **gameplay** 觸發；前者通過不代表後者已驗收。

已登記的純值 `ai_skill_invoke[reason]` 在一般模式與 Original 國戰皆優先處理頂層詢問。Original 內部詢問明確標記 `options.inquiry_scope="original_internal"`；原先僅供頂層使用的 handler 在此 scope 回 `nil`，保留原本的可見範圍。內部沒有 handler 且非 Frequent 技能時仍回 `nil`，維持 GameRule 的三態候選選擇；頂層才完成布林答案。技能別名直接登記同一原生回呼，保留 provider 與最後寫入者優先序。

Original 頂層發動預設讀取同一 `skill_invoke` request 的 `context.skill_frequency`，並核對
reason；`-1` 表示原生已知沒有此技能定義。內部跨技能詢問仍只查可見技能，不能借用外層題目的頻率。

Original 選花色沿用 isolated 策略回傳的原生枚舉，僅轉成當題提供的花色名稱。選牌策略回傳的
非負整數 ID 必須在本題 `card_ids` 中，再轉成 `{kind="answer", cards={id}}`；其他手牌隱藏
造成候選集合不完整，不會否定已提供的明牌 ID。隱藏牌隨機選擇仍使用原生提供的 opaque 收據。

改判的 `judge.filtered_cards[id]` 與 `outcome_by_id[id]` 來自同一原生唯讀計算，包含
`judgment_source_id`／`judgment_target`；策略用過濾後牌面估算，付款仍回覆原本 offered ID。
投影從印刷牌面開始，依原生次序保留最後一個匹配過濾結果，不串接前一個過濾輸出。
只有明確提供唯讀能力的 FilterSkill 可參與，未支援者或他人未完整公開的技能集合維持 unknown；
不呼叫 `filterCards()`、不重寫 WrappedCard、不執行遊戲事件。紅顏使用其既有純回呼產生新牌面。
閃電保留另按持有者提供有序 `lightning_reserve_rows`，不把不同持有者的過濾結果混成一組。

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

固定材料按原生 `canSelectCard` 的順序前綴出票；宣告牌名只取當次 `usableNames`，保存在票據的 `declared_name`，重建時再驗 `canDeclare`。票據成本採原生產物的實際 subcards，包含隱式全手牌。變動材料只採樣單張與合法增長前綴；512 次探查、64 張票或目標投影預算耗盡均保留已知票並標示未完整枚舉。不能把未枚舉當成沒有轉化，也不能從 AI 的自由字串新增宣告權限。

缺少 QVariant 的追加目標詢問沿用原生 `AiSkillContextScope` 投影 `context.use` 與 `extra_targets`；須匹配詢問座位、來源技能與事件，不能借用其他執行的舊資料。`_heg_kenshang` 比較整份替換名單，`wushuang_extra`／`heg_Halberd` 只評分原生候選。化身消耗詢問提供持有者自己的 `huashen_soul_skills`，策略優先保留不同的技能供應者。`naman_target`／`NamanUsedata` 與 exchange.shefu／`ShefuUsedata` 是舊消費端：目前原生 Naman 只有回應殺的 invoke／取得，Shefu 使用 `shefu_cancel`／`ShefuData` 與 `@@shefu`，沒有呼叫這兩個舊詢問。這不表示兩名武將不能出現在國戰選將池；外部另行發出舊詢問時仍為 unknown，不能偷偷回退 SmartAI。

觀星的 `willSkipPlayPhase` 使用 viewer 私有純值 shadow，綁定 `AIWorldLifecycle` 的原生旗標清除世代；新世代從完整的持有者旗標列表開始，缺少世代／列表仍拒答。Dummy 推演只寫暫存 shadow，退出時復原。階段移除估算只建立原材料名稱、花色、點數的效果假設，走 isolated 的錦囊效果判斷，不取得用牌權限。牌類計數只在同一 request 快取已驗明的技能名稱，每張材料仍逐一執行轉化規則。
Original 共用 view-as 查牌入口在轉化規則產出 spec 後，先用既有牌目錄核對產物的類別；不匹配者無須投影原生目標與規則。匹配者仍執行完整原生來源／材料估值並再次核對牌類；缺少目錄或類別資料仍拒未知。不快取技能條件、牌數或策略結果，既有每題 64 次估值限制不變。

國戰觀察記錄分成 `AI_HEGEMONY_ACCEPTED_CARD`（通過答案重建）與 `AI_HEGEMONY_OBSERVED`（原生事件）。後者保留事件／用牌歷史 ID、公開材料移動的 ID／區域／原因，以及用牌、回應、效果與結束階段；隱藏抽牌仍只有數量。驗收須另對照接受牌面、實際材料、目標順序與原生事件，不能單憑 accepted 記錄宣稱付款或效果通過。

原生 KnownBoth 已把看過的槽位存於觀察者的 `KnownBoth_<target>` tag，`known_both_seen` 只投影這份持有者知識的有無，不讀取目標的私有標記或未揭露身份。技能類別字串取本 isolated VM 的共用定義；所有 Slash 子類共用國戰 Slash 規劃。`transferable` 是當前可見牌面屬性，`printed_transferable` 是同一已知 ID 的原始牌面屬性，不能以 WrappedCard 猜測原始牌面。

原生固定材料與 UI 選牌不是同一份資料。票據保存實際產物 subcards，另以 native-only `activationCardIds` 重建；回覆不能修改任一份授權。直覺的牌頂火攻直接使用原生票據，策略不查未公開牌頂的牌面；`ZhijueCard` 首次動作無目標，`@@zhijue` 在新的原生請求中重新規劃普通卡目標。`AI_HEGEMONY_V1_AVAILABLE` 只記錄當次原生 `isAvailable()` 確認可用的 V1 owner／instance／reason，不把定義存量當作實局可用數。

實體 passthrough conversion 另有 `physical_product_id`／`virtual_card`，不把實體 ID 放入成本列表。驅驟後續 Slash 走相同牌族規劃與原生票據，不以 AG／通知中的牌面自行製造使用權限。知天的免費查看策略只在實際 AG 詢問後記住當次旗標清除世代，dummy 規劃不消耗這份記憶。

國戰原始防具、技能、外人關係與鎖定事實查詢缺少資料時仍傳遞 unknown／unsupported，不以 absence prior 補成事實、不攔截錯誤字串、不加專用 host 證明。公開牌面不能證明隱藏虛擬防具、技能或來源相關無效化不存在；公開同盟也不代表能讀取外人的策略關係矩陣。防具估值只呼叫已載入的 isolated 同名回呼。

`queryArmorEffect` 保留 nullable 的確定事實；策略用 `hasArmorEffect` 優先使用該結果。
僅對國戰外人缺少確定結果時，既有策略 wrapper 可以完整公開裝備區、存活及裝備欄狀態
估計實體防具是否匹配，附 `status="estimate"`、可見防具、目標／來源及未解規則的 basis，
同時記錄未覆蓋診斷。沒有可見匹配只得到 estimate false，不代表暗將虛擬防具不存在；
有可見匹配亦不證明私人無效化不存在。公開投影缺失及 owner 缺確定結果仍拒答。
此估計只影響策略選擇，不能代替原生目標、材料、票據或實際效果驗證。

同一策略 wrapper 可處理國戰外人的 `hasSkill`／`hasSkills` 最終 unknown：先完成原有
三值查詢，已知 true／false 不變；只有公開技能索引存在時，才以明示 estimate false
表示「策略尚未把未證實技能納入預測」。保留表達式、include_lose、viewer／target 及
`unobserved_skill_presence_not_proven` 診斷，不逐 term 提前估值，也不更改 raw provider。
這不證明隱藏技能不存在，更不能製造技能來源、instance 或 conversion 票據。

回血策略共用既有 `hegemony_can_recover`，不呼叫 PlayerView 未提供的 `canRecover`。
owner 讀取原生 `owner_can_recover`；foreign 以公開存活、受傷、移除狀態短路排除，
軍令標記沿用共用策略查詢與其估算紀錄，其餘可回血預測明示私人 `CannotRecover` 仍未知。
技能來源與標準卡牌的重複判斷已併入此流程。

`relationTo` 原始查詢仍不製造缺少的關係列。國戰策略的兩玩家 `isFriend`／`isEnemy`
在外人私有關係列未提供時，可使用已投影的公開同盟作 `friend` 估算；無公開同盟則保持
`unknown`，在雙方支援數量預測中均不計入。兩種情況都附上 `knowledge_estimates` 與
未覆蓋診斷，不推斷暗將勢力、不把非同盟改成敵人；viewer 自己的關係列及公開同盟投影缺失
仍拒答。此為使用者允許的未知資訊決策政策，不是補齊外人的實際策略知識。

Snatch／Dismantlement 的有限原生 provider 只保管當次計劃的隨機域、私有 handle 與候選選牌；Lua 不取得暗牌 ID／牌面。正式計劃須匹配原生已發出的實體候選或 conversion，dummy 假設不能寫入。只有通過用牌權威驗證的計劃才綁定原生牌面快照，再以同一 `use_card` 歷史事件讀取一次；用牌結束／換回合清除，錯誤退出清除計劃 handle。抽樣使用獨立 AI RNG，不推進玩法 RNG。

CardChosen 的 `options.context.random_card_choices` 與 Original 規劃 domain 提供 token、數量及 `visible_card_indices`（當前 viewer 已知 ID → 1-based 位置）；未揭露 ID 僅留在原生 domain。Lua 自行決定隨機位置及白銀獅子避讓，原生只驗收據與合法性，不執行牌名策略。`random_card_choice={token,index}` 只適用當次 CardChosen，不能混入普通 ID、目標或 card spec；原生再驗 token、範圍、當前區域及 discard/get 合法性。Original 的完整有序 domain 與一般合法 domain 分開，避免把過濾後位置當成原始位置。外部 agent 的對應值為 `action.randomCardChoice`，同樣只接受 token／index。

`isLastHandCard` 同時接受已知牌與 conversion，實體 passthrough 使用原生產物 ID，不以負的票號代替。已遷移的零材料技能直接綁定當次來源票據，避免唯讀假設遺失實例。Original `toUse` 的 nil／空列表有 viewer 私有純值記錄及 request stamp；非空列表只在當次 request 保留實際物件，後續未知不還原成空列表。外人未觀察到的技能維持未知，不再把未知改成技能不存在。

`QSAN_AI_UNSUPPORTED_TRACE=1` 可在啟動時開啟有界的 unsupported 來源堆疊；只記檔案、行號與函式，不記 locals 或詢問資料。一般執行不收集堆疊。

技能檢視只在同一 PlayerView／request 內快取不可變 DTO 及名稱排序結果；每次仍回傳獨立列表，不快取可用性、技能判斷或策略排序分數。追加目標數沿用原生候選中已知的 `extra_target_count`，缺少欄位仍是 unknown。`queryCardLimited` 是 nullable 知識查詢，原始 `isCardLimited` 的必填契約不變。

國戰防禦估值在單次呼叫中重用已驗證的公開 `shown_skills` 表，並先檢查公開必要條件，
避免評估不可能影響分數的防具分支；排序仍照常呼叫策略與供牌回呼，不保存策略分數。
知識估計的共用簽章為 target／skills／include_lose 使用固定欄位，免除重複鍵名排序，
仍比對所有純值欄位並保留每次實際估計的計數。

Original 供牌／牌數掃描先確認該技能有註冊對應 provider，再讀其當次可用性；
沒有 provider 的技能不會產生轉化，無須反覆做技能查詢。選酒輔助殺時，沿用本次
已選出且已綁定的酒通過後續門檻，不重新 fill 全部技能或另選不同材料／來源的酒。

Original 開始建構當次 turn-use 時，先把同一份新列表綁至 `self.toUse` 並記錄當次 presence，
才呼叫 fill callbacks；已用牌後清除的上一題 nil 不會再被當作當次列表傳給 `table.contains`。
這不把跨題 unknown 還原成空列表，也不變更 `table.contains` 對非列表參數的錯誤語義。
Original 的 `ai_damage_effect` 由既有共用 hook registry 初始化；原版只有空表宣告與迴圈，
沒有遺漏 callback writer，其他具真實回呼的 registry 仍沿用各套件註冊。

主公技查詢也沿用同一未知處理：已公開技能的 `lord_skill_effective` 仍為原生事實，
缺少此欄位時拒答；只有未觀察到該技能且技能集合不完整，才在國戰策略層標示為
未證實存在的估計。不能以普通 `hasSkill` 代替主公技有效性。

fill／card_use 建構同技能唯讀提案時，必須將本 callback 的 `request` 傳入既有
`parse_policy`／turn-conversion 流程，保留完整實例來源；不可使用外層 `self.request`
補來源。跨技能假想仍不借用當前技能的 request。`CandidateView:getLegalTargets()`
回傳玩家名稱字串；`canSlash` 直接比對名稱，只有完整候選集合才可用未命中證明非法。

國戰的 `evaluateOwnerViewAsCard` 經共用 `ai_evaluate_card(spec)` 取得原生唯讀資料。
規格包含當題 viewer／decision／revision、source_key、skill、可選 instance_id、name、
suit、number、subcards 及可選 can_recast；拒絕未知欄位、非整數與重複材料。source_key
只標示策略來源，不能授權產物；技能實例必須由 owner 當題快照及目前所有權共同確認，
同名多實例須明確指定。材料限 owner 當題可見手牌／裝備／開放牌堆，並核對目前區域。

裝備來源沿既有 `ViewAsSkillV2::prepareEquipSource` 核對實體裝備或有效的虛擬授予，
以 `instance_id=0` 表示裝備入口，不捏造正數技能實例。舊策略使用裝備 class 作為
`skill` 時，經 Engine 的裝備定義映射取得原生技能，保留產物原本的 skill 拼法；
不把 source_key 或任意大小寫轉換當作所有權。明示正數實例不存在／失效時仍拒答，
不改走裝備入口；零實例亦須原生來源憑據有效。結果仍只有唯讀估值，最終選定後
沿原有 conversion／skill-action 收據重新驗證與付款。`evaluateTurnConversion`
會將當題同 owner、同技能 callback 的精確實例傳給估值器，跨技能假想不借用該實例。

只 clone 普通目錄牌，不執行 SkillCard、付款或效果。未設定的花色／點數由 native Card
按材料解析；Lua 使用回傳的完整牌面、card_rules 與 candidate，不借用另一張牌的目標。
假想規則按 Activate／MethodUse 計算，外層 reason／pattern 暫存後還原；完整性、revision、
技能定義及旗標的穩定檢查不放寬。結果不帶可提交 candidate ID／conversion ticket。
既有完整目標組合支援 prefix 規劃；未完整投影仍不能提供全部組合，不新增技能專用 host。
最終計畫及 target intent 共用 `validateTargetSequence`：完整投影按已列序列驗證；
不完整的已發行實體牌候選可用 `ai_evaluate_card` 的 `operation="target_sequence"`
查詢一條已選序列。原生核對 viewer／decision／revision、candidate ID 與 card ID、
持有區域，再由同一目標投影器只走該序列，保留順序、禁止規則、可結束性、穩定性及
深度限制。結果只證明該序列是否合法，不改 `complete_coverage`，也不授予新候選。
查詢與牌估值共用下述 64 次／16384 額度；未知、過期及額度不足仍拒絕。轉化與技能
receipt 保留既有授權及成本路徑；最終提交仍執行原生目標驗證。

每個 decision 的查詢最多 64 次（失敗也計次），共享額外 16384 次目標列舉額度；此額度
與初始 makeRequest 的額度分開，其他規則掃描由查詢次數與玩家名單界限控制。Lua 封存
當題 owner／request／牌面／candidate，重取 candidate 時檢查；跨題或改寫後拒答，
toCardSpec 仍拒絕提交。非國戰維持原有投影流程。

國戰正常決策的 world.distances 使用既有完整有向矩陣與失效機制。聚合距離原已由
refreshDistanceCacheIfDirty 廣播，補齊外人間數值不公開距離技能來源；事件仍維持既有
lord scope，非國戰保留 compact viewer scope。此變更只補零 fix 的原生距離，不宣稱
非零 fix 假想或完整國戰已驗收。

`queryArmorEffect` 保留 nullable 原始事實：預設裝備欄條件下，無來源查詢的精確 false
可沿用至指定來源，因 MethodEffect 的來源條件只追加無效化限制；精確 true 加來源仍為
unknown，`need_area=false` 亦不可借用預設裝備欄投影。策略估計不改寫此原始事實。

`analeptic_static_available` 由原生對精確牌面呼叫 `Analeptic::IsAvailable(owner, card)`；保留 Residue 的 self 目標、限制與禁止判斷，不以 `Card::isAvailable` 或另一張牌的結果代替。持有者、當次取得／printed 推演及已發出的 conversion 可帶此值；未知時不輸出欄位。外部 JSON 對應 `analepticStaticAvailable`。

持有者的基本牌原型亦單獨投影同一 static 欄位；`sgs.Analeptic_IsAvailable` 及
原型 `CardView:isAnalepticAvailable` 讀此欄位，不借用一般 `available`。
foreign 與 event-only 不新增此私有投影，缺欄位仍拒答。

Original 的 `hegemony_source_query(player,"getLord")` 僅對 viewer 使用既有
`owner_hegemony_lord` 精確投影；空字串對應已知 nil，不與 Room 的身份主公混用。
卡牌策略的技能存在查詢直接走共用三值／估算邊界，不再次用 `areSkillsComplete`
否決明示估算；將領槽位、來源實例的完整性要求不變。

共用 `sgs.ai_nullification` ABI 為
`(self, trick, from, to, positive, null_num, keep)`：第六項是當題持有者策略取得的
無懈數量，第七項才是可選保留旗標。Original、普通 dispatcher 及轉呼叫均遵守；
不將 nil 補零、不把反制鏈層數當數量，也不讓 numeric count 落入 boolean keep。

`isLocked` 對 printed／acquired 牌可從既有精確 limitation map 按當題 use/response
reason 取值。raw `queryLocked` 保持 nullable；國戰外人缺少私有限制時，策略 wrapper
只在牌面與 reason 已知下使用明示「未觀察到限制」先驗，保留 coverage／knowledge
診斷；owner 缺投影仍拒答。

當題 `SlashEffectStruct` 以 `slash_effect_present` 與純值 `slash_effect` 投影；
其他 QVariant 型別明示 false，沿用原生預設轉換。後續空資料補充不得覆寫既有型別見證。
無懈根錦囊的 UseHistory 比對接受同一實體牌 wrapper／real-card，仍核對當次來源、
受影響目標及完整目標順序，不靠牌名或 ID 合併虛擬牌。

SinglePeach 使用本題瀕死玩家填入 `dying.who`；`current_damage_present` 僅描述
當前房間 CurrentDamageStruct tag。invalid tag 為已知不存在，有效且可驗證才為 true，
malformed／失效仍 unknown；不能當作 DyingStruct.damage 的替身或借用最近傷害事件。
Original 救援先核對本題目標，再讀此見證。

Original 棄牌／換牌策略接收共用 ABI 的 pattern，保留完整排序後只選本題符合者；
callback 答案仍須通過原生候選與數量驗證。實體牌的 `getTargetModResidue` 必須顯式
傳入評估玩家；無參數只適用已有 evaluation_rules 的推演牌。
技能 use callback 的輸出若為既有 seed／封存 readonly 計劃，先按可見材料計算
致死成本；只有材料會致死且救援不足時才要求精確 `willThrow`，缺資料仍拒答。
材料決定、materialize 及原生付款驗證照常執行，真卡保留 willThrow-first 順序。
Original 正式出牌的前置 gate 同樣只對 SkillCard seed／封存 readonly 使用既有
`canMethodUse`；真正卡牌及 readonly Basic／Trick 保留 handling-method／recast 判斷。
`skillCardFace` 使用既有 policy identity allocator 給每次策略物件配置比較 token，
在封存前寫入；選定後沿既有 identity 繼承保持同一物件語意。這不是原生卡牌／
conversion 授權，不能取代材料、來源實例、request stamp 或最終 authority 驗證。
CardChosen 按 `request.kind`、reason、目標及區域識別當題；不要求僅部分 Choice
入口提供的 `options.question`。順拆沿既有原生選牌記憶交接，暗牌仍只回 opaque receipt。
順拆記憶在 CardUsed callbacks 前綁定現有 use-card 歷史事件，CardFinished 後清理；
不提前一般 AI event recorder。CardChosen 的禁選清單與可取消性由服務明確傳入，
禁選 ID 留在原生；可見候選與 legal opaque domain 都排除禁選及不可取得／棄置者，
最終 authority 再核對。Original 只有明確 `-1` 才按當題可取消性 pass，否則選
完整 legal domain 的第一項；nil、unknown、非法值及空合法域不冒充合法答案。
`canDiscard(target, zones)` 保留原生「區域內至少一張可棄」答案；numeric ID
只讀 viewer 對可見實體牌的 `id:<id>` 精確投影，包含已知 false，不以區域
答案推導單牌合法性。未投影的暗牌 ID 或 foreign actor 維持 unknown。
共用假想牌查詢亦接受明確空 skill 的普通 catalog 牌面，但限零材料與 instance 0，
不設定技能來源、不產生付款能力；有技能的查詢仍驗 owner 實例與 root。
國戰 foreign `isCardLimited(card, method, is_handcard)` 在 raw 規則未知時，僅對
Use／Response 作「未觀測私有限制」的明示策略估計，記錄 method 與 is_handcard；
owner、其他模式／method 及缺牌面仍拒未知。`queryCardLimited` 保留 nullable，
foreign 估計不使用 owner 的假想規則作合法性證明。
救援的策略旗標一律沿既有 `getStrategyFlag`：owner 保持精確記憶，foreign 私人
苦肉旗標的 raw 值為 unknown，再由共用策略層明示 inactive prior 並保留 coverage。
不讀取其他 AI 的私人記憶，也不把這項估計寫回原生旗標。
`queryFlag` 捕捉最終 nullable provider（含 range 假設）；直接 `hasFlag` 沿既有
required wrapper 對未知拒答，避免其他策略把 Lua nil 默認為 false。

CardView 僅索引原始 flags／kind_of 的 membership，索引留在 facade 私有欄位，
不寫入封存 DTO；原始列表 reference 替換時重建。動態 scratch、known_flags、
using／kill_off、可見性及完整性判斷仍先執行。沒有快取策略分數或牌數，也不減少
callback 次數；原始列表缺失仍為 unknown，不視為空集合。

座次查詢沿用公開快照環：`aliveCount()` 預設排除 removed 玩家，傳 true 才包括；
removed 狀態缺失仍為 unknown。`hegemony_source_query(...,"getLastAlive",n)` 重用
既有 `getNextAlive(count-1)` 逐步前移，與原生修正後每步從前次結果呼叫
`getPreviousAlive(1)` 一致；非正步數保持原玩家，不另猜名冊或跳過未知座次。

Conversion 以既有 `AICardView producedCard` 作牌面的唯一來源，涵蓋處理方式、類型、花色、旗標與包括 MethodNone 的限制；不再另存牌名、類別、花色、點數、鎖定或酒的可用性副本。Lua 和外部 JSON 的既有牌面鍵由同一 DTO 輸出，提交亦讀同一份原生紀錄。`ownerRulesKnown` 與 `ownerStateKnown` 各自控制規則與實體持有狀態，已知轉化規則不因缺實體持有狀態而被省略。牌面資料不取代 conversion ID、實例、材料與 revision 的提交權威。手牌、裝備、判定區與可見牌列表同樣只快取該快照的牌身分與 DTO，每次回傳獨立列表；策略值與旗標查詢仍即時計算。Original helper 安裝與 provider 驗證共用一份名稱映射，沉水／HDrowning 指向同一已移植實作，不再疊加動態別名及重複封存名單。其餘已審核 wrapped provider 也由同一封存表供每題安裝及分派驗證，named 映射優先，不從 runtime self 接納新 provider。其他模式保留原有 provider。

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

技能 seed 的建立亦經 `evaluateTurnSkillCard` 與相同 request/source seal；保留
`strategy_seed` 與禁止直接提交的介面。只有已選定的計劃才可由既有
`materializeTurnSelectedCard` 綁定，零材料亦須通過原生來源、成本與目標驗證；
nested readonly Play 不可付款。任何 face／來源／當題身分改變仍拒答。
`mobile_st_standard` 的全部 17 個 SkillCard fill caller 顯式轉傳 callback request；
不能以外層 `self.request` 代替含技能實例的 request，也不把回應或跨技能假想
查詢批量改成當前技能來源。

國戰 `getFormation` 沿既有公開 friend 矩陣、`aliveCount(false)` 與座次環，
按原生順向／逆向邊界及順序建立陣形；缺資料拒答，不從未公開勢力猜聯盟。
陣形查詢統一使用此方法。
Original 的 willSkipPlayPhase／Kurou_toDie／AI_needCrossbow 使用既有
`hasStrategyPlayerFlag`／`setStrategyPlayerFlag` 與 owner-only epoch shadow；
實際 flag clear 後回到完整 owner flags。Guanxing 保留舊記憶鍵及 dummy 隔離；
Kurou 保留已執行 prefix（含 dummy／錯誤）的立即持久化；Collateral 仍僅正式
use 才寫。這些純策略值不產生付款能力，不讀其他 viewer 記憶；foreign 苦肉／
連弩 raw flag 保持 unknown。Crossbow 跨 viewer 協作仍需獨立驗證，不由此宣稱完成。

共用 scalar getter 以欄位＋required method 分開快取函式；nullable 與 required
不可共用錯誤契約，每次仍讀當前 `_view`。required 檢查合進既有 getter，保留
nil 拒答、false／0／空值及 unsupported 原物件重拋。共用多值呼叫仍保留
pack/unpack 的尾端 nil。Original 牌數判定只在單次呼叫內重用不可變玩家名稱；
presence、shown、技能有效性與 callback 仍按原順序即時判定，不快取策略結果。
估計診斷先按原有完整 scalar 身分去重，只有新增紀錄才複製 entry；新增時同步
索引，避免下一次再編碼同一筆。重複 count、255 筆容量與溢出摘要語意不變。
Original 使用價值、使用優先序及動態優先序共用既有使用價值排序入口，
採「同次排序固定評分」：只在既有 comparator 首次
訪問該牌物件時呼叫原評分函式；同次排序的後續比較重用該值，包含 0。
分數表隨該次排序結束或錯誤丟棄，不跨其他排序、dummy 或正式 activate 共用。
原評分公式與外層同分規則不變；每張不同武器仍執行原敵方排序及回呼。
這是明確的計算政策調整：回呼、RNG、策略寫入與診斷次數只記實際求值，
不保留舊比較器的重複副作用，同分武器選擇可能改變；不可宣稱 donor 逐次等價。
內部固定模式參數放在既有 cards／inverse／flags 之後；Original 原本忽略的
flags 仍忽略，不借用 ordinary 預掃描／序列化鍵規則。保牌逐步更新與 CardNeed
維持各自既有契約，沒有跨排序共用分數。


不可響應名單由既有 `cardNoResponseNames` 共用入口處理；原生依據是
`CardUseStruct.no_respond_list` 與 `_ALL_TARGETS`，不是 Card tag。
只有當前問題直接提供且通過 live refs 檢查的出牌，或座位／技能／事件均吻合的
原生詢問，才投影 `response_list_known` 與完整名單（含已知空表）。一般歷史 tag
與 CurrentUseStruct 補充不賦予此保證。原生 self.request 的 context.use CardView 綁定
viewer／decision／revision 私有見證；supplied context 或另一個傳入 request 不得取得。
名單不經牌名、實體 id、authority card、clone 或付款 receipt 借用。
未帶實際出牌見證的可見牌／轉化牌以空名單作明示規劃估計，保留 coverage 與
knowledge 診斷；過期或格式錯誤的見證拒答，不降為估計。此估計不聲稱未來技能
不會禁止響應，也不產生原生合法性或付款能力。

`getExpectedJinkNum(use)` 沿當題 `CardUseStruct` → `options.context.use` →
`getDecisionData():toCardUse()` 讀取 `expected_jink_num`。原生共用 `projectUse`
只對通過 live refs 檢查的實際出牌，從出牌者 `Jink_<card>` tag 按原始目標順序
取出 viewer 自身的需求；不傳其他目標的完整清單。零保留為不可出閃，多個自身
目標槽沿既有規則取最大值、遇零立即返回。缺槽、非數字或非法負值保持未知，
不以預設一張掩蓋缺資料。歷史 tag、`CurrentUseStruct` 補充與假想牌不因此取得
當題需求；原生已配對的借用詢問沿同一投影入口。Lua 的既有 getter、各套件回呼
與外部 JSON 使用同一欄位，不新增武器／技能專用分支。


連環目標判定共用唯一參數契約：

```lua
self:isGoodChainTarget(target, source, nature, damage, card)
```

`source` 只能是玩家或 nil，`card` 只能是可見牌／轉化牌或 nil；兩者不能放在
同一參數槽。`nature` 接受六種 DamageStruct enum，或 N/F/T/I/G/P；明確屬性與
牌可同時提供，不能把兩者縮成 `card or nature`。省略 source 仍指向自己。
普通策略保留「有 card、nature=nil 時用 N」與原本其餘 nil 行為；Original 保留
nature=nil 時 Fire 及既有 Slash 屬性／傷害推導。統一 ABI 不合併兩套評分算法。
所有直呼、字串 method-query 與 Original 別名均使用上述順序；不保留依第二參
型別切換的舊 ABI。完整 DamageStruct 的既有 `isGoodChainTarget_`／
`originalHegemonyChainTargetStruct` 入口保持單結構參數，不混入位置參數入口。


Owner exact-card 限制投影涵蓋 `HandlingMethod` 全域（None 至 Get），一般與
適用的 hand map 使用同一迴圈；Play 保留原生查詢結果，不改映射為 Use。
Original activate 以 dummy callback 的實際輸出重新辨別策略候選；只有 SkillCard
seed／sealed plan 或 strategy candidate 本身為已登錄 skill-action receipt 時，
使用既有 source availability。ordinary policy evaluation 不借底層 receipt 豁免
handling limits；最終付款、來源及目標仍由既有 materialize／native admission 驗證。
`isCardLimited` 缺值診斷附已可見 face 的 class／id／conversion／evaluation／seed／
receipt 及 method、hand、rule/map 類型；未知仍拒答，不把 available 推成 limited=false。


當題 typed context 的實際公開牌，於既有 runtime binding 內附 viewer 專屬
`evaluation_owner`／`evaluation_rules`，重用完整 native card rules。保留原牌的
id／printing／virtual 狀態，不設 evaluation_only、printed_evaluation，不新增
subcards、candidate、receipt 或付款權威。isLocked 沿當題 reason 查精確 limits，
不借投影時的 locked 快照。hand map 僅在實體牌確實仍在 viewer 手牌時提供。
公開事件、審計及通用歷史 tag 的 publicDecisionCard 預設不帶 viewer 私人規則；
當題 nullification 的頂層 card 與 typed effect 在相同 scope 使用同一投影。


普通 card piles 的公開 name/count 與內容 card_ids 是不同契約；純張數／空堆
判斷使用既有 `AIValue.callKnown(player,"getPileCount",name)`，缺整份投影仍拒答。
Original 國戰模組75個具名pile純數量運算與1個hand-pile空檢查統一遷移；
hand-pile使用既有getHandPileCount，共用估值／saveplayer兩處巢狀查詢亦改count；
原本要枚舉、contains、first或比較點數的getPile仍要求可見真實ID，不能用空表
或假ID替代。間接變數混用數量與ID者保留原契約；general piles不套用此規則。


`ai_card_use` 預設接收完整 plan（包含 AIList `to`）。Original donor 的 nullable
dummy 是明確 ABI 例外：既有安裝點把最終 exported function identity 記入
`AIUsePlan.original_nil_targets` 弱鍵表，不按 slot／skill／檔名繼承。只有該函式
且 Original admission 成立時保留 nil-to；轉呼 ordinary previous 時不適用。
後置 package 以不同函式替換相同 key，仍遵守 native full-plan ABI；包住
Original callback 的新 wrapper 必須在其安裝點明確保留這個 ABI。
共用 useSkillCard 僅對 native nil-to dummy 暫附 AIList，callback 成功、普通錯誤、
unsupported 或 instruction-limit 後都先還原 nil-to，再沿原錯誤處理。其他輸出、
觀察與記憶寫入保留，沒有重試／回滾；正式 plan、已有目標清單、method fallback
維持原物件及流程。這項標記只說明資料契約，不是技能來源或付款權限。

Original 的 findLeijiTarget／needLeiji 保留 player-or-nil 契約；共用 known-nil 表只認既有 Original provider 身分，nil 代表沒有合適目標，內部 unknown／unsupported 仍原樣傳遞。

Original askForCard 的單材料 $id 與原生 PlayerDecisionService 一致，還原為實體 ID 後必須命中本題 offered ID；多材料 DummyCard 仍不由字串取得集合付款權限。

國戰 heg_Halberd 的 V2 啟用沿用空材料 ActiveSkillCard 評估票；isolated 套件 fill／use 以現有殺策略判斷啟用時機，原生另發 slash 詢問決定材料與目標。不得把啟用票當殺或沿用外層目標付款。

敕令（ImperialOrder）遵循目前原生分段詢問：先 askForCard 棄裝備，再 choice 的 show/losehp，最後 GameRule_AskForGeneralShow 決定將位。choice 不重做付款，也不回傳舊版 show_head/show_deputy/dis_equip。

唯讀 owner range 作用域由 TargetModSkillQueryScope 疊加旗標讀值，不寫 Player 實際旗標、不廣播、不改 revision；與既有 hidden-instance／history 查詢可巢狀，原查詢限制不變。僅接受 InfinityAttackRange／slashNoDistanceLimit 的詞法假設，深度8，共享原牌估值64次／16384探測額度。現有 facade 取得同一原生牌投影及 canSlash／targetFilter；物理牌限當題擁有且可見的手牌／裝備，虛擬牌沿共用 evaluateCard 檢查來源與材料。這不是先前有限來源的實際旗標寫入API；四參數寫入模式仍拒絕，沒有默許來源寫入或付款權限。

國戰 throw_crossbow_skill／force_slash_skill 是套件自有靜態分類，由 isolated 的 sgs 表及既有 AIHegemony.facts 使用。Mountain 共用 hasSkills 消費端直接使用 facade 已確認的 true/false，只有 nil 進入未知處理，不能再用未投影的 skills_complete 欄位推翻已知答案。
