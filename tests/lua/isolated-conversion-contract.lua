-- PR 06 的轉化契約：card_spec、轉化候選與技能實例。
-- 與其他純 Lua 契約一樣只需要 isolated-bootstrap／isolated-facades／decision-core，
-- 不需要 Room、Engine 或任何一支武將 handler。
--
-- 這裡驗的是作者面的四件事：
--   1. 轉化走的是同一套出牌策略——ai_card_use.Slash 不知道自己手上這張殺是變出來的。
--   2. newCard 只「找出」已授權的轉化，找不到就是 nil，不是憑空造一張。
--   3. 授權是票。答案送的是 conversion_id，牌名與成本只是給權威端比對用的。
--   4. 「列不完」與「沒有」分開：conversions_enumerated = false 一律未覆蓋。
local function player(name, fields)
    local view = {object_name = name, alive = true, dead = false, handcard_count = 0,
        hp = 4, max_hp = 4, hujia = 0, max_cards = 4, attack_range = 1,
        equips = {}, judging_area = {}, public_marks = {}, skills = {},
        known_cards = {}, hand_visible = false}
    for key, value in pairs(fields or {}) do view[key] = value end
    return view
end

local function conversion(fields)
    local view = {conversion_id = fields.id, name = fields.name or "slash",
        class_name = fields.class_name or "Slash",
        kind_of_names = fields.kinds or {"Slash", "BasicCard", "Card"},
        suit = fields.suit or 1, number = fields.number or 7,
        activation_owner = "viewer",
        activation_skill = fields.skill or "conv_free",
        activation_instance = fields.instance or 1,
        source_owner = fields.source_owner or "viewer",
        source_skill = fields.source_skill or fields.skill or "conv_free",
        source_instance = fields.source_instance or fields.instance or 1,
        activation_quota_available = true, source_quota_available = true,
        -- Native cost_count=0 denotes an exact-subcards ticket.  Keep that
        -- value explicit so planned conversions pass cost-intent validation.
        cost_count = fields.cost_count or 0, subcards = fields.subcards or {},
        available = true, target_fixed = false, feasible_with_no_target = false,
        complete_coverage = fields.complete ~= false,
        max_votes = {}, legal_targets = fields.targets or {"weak", "strong"}}
    view.target_combinations = {}
    for _, name in ipairs(view.legal_targets) do view.target_combinations[#view.target_combinations + 1] = {name} end
    return view
end

local function physical_candidate(id, ticket, targets)
    targets = targets or {"weak"}
    local rows = {}
    for _, name in ipairs(targets) do rows[#rows + 1] = {name} end
    return {card_id = id, candidate_id = ticket, available = true, limited = false,
        complete_coverage = true, target_fixed = false,
        feasible_with_no_target = false, legal_targets = targets,
        target_combinations = rows}
end

local function request(fields)
    fields = fields or {}
    local world = {
        mode_id = "conversion-fixture", revision = "1", current_player = "viewer",
        self = player("viewer", {hp = 4, handcard_count = 0}),
        players = {
            player("weak", {hp = 1, handcard_count = 0, hand_visible = true}),
            player("strong", {hp = 5, handcard_count = 0, hand_visible = true})
        },
        player_order = {"viewer", "weak", "strong"},
        alive_player_order = {"viewer", "weak", "strong"},
        hand_cards = fields.hand or {}
    }
    if fields.unmanaged ~= true then
        world.mode_policy = {managed = true, objectives = {},
            relations = {viewer = {weak = "enemy", strong = "enemy"}}}
    end
    local built = {viewer = "viewer", kind = "activate", world_view = world,
        card_candidates = fields.candidates or {},
        card_conversions = fields.conversions or {},
        conversions_enumerated = fields.enumerated ~= false}
    if fields.skill_actions ~= nil then built.skill_actions = fields.skill_actions end
    return built
end

-- ------------------------------------------------------- 轉化就是一張可以打的牌
do
    local ai = assert(SmartAIView.new(request({
        conversions = {conversion({id = 1})}})))
    local view = assert(ai:getConversion(1))
    -- 身分來自權威端造出來的那張牌，不是作者宣稱的字串。
    assert(view:getName() == "slash")
    assert(view:getClassName() == "Slash")
    assert(view:isKindOf("Slash") == true)
    assert(view:isKindOf("Peach") == false)
    -- 合成 id 是負數，永遠撞不到實體牌，而且查得回同一筆轉化。
    -- 比的是票號不是物件：facade 每次查詢都重新包一個，這是既有設計（wrap_values
    -- 會 copy_value），CandidateView 也一樣，所以這裡不可以用 identity 比較。
    assert(view:getEffectiveId() == -1)
    assert(ai:getCardCandidate(-1):getConversionId() == view:getConversionId())
    -- 轉化同時答得出候選那幾個問題，所以策略不需要分辨這兩種東西。
    assert(view:canTarget("weak") == true)
    assert(view:canTarget("viewer") == false)
    assert(view:hasCompleteCoverage() == true)
    assert(view:needsATarget() == true)
    assert(view:isBorrowed() == false)
end

do
    -- 借用：activation 與 source 不同就是借來的，配額記在 source 上。
    local ai = assert(SmartAIView.new(request({
        conversions = {conversion({id = 1, skill = "conv_free", instance = 2,
            source_owner = "strong", source_skill = "conv_root", source_instance = 9})}})))
    local view = assert(ai:getConversion(1))
    assert(view:isBorrowed() == true)
    assert(view:getSourceOwner() == "strong")
    assert(view:getSourceInstanceID() == 9)
end

-- ------------------------------------------------- 同一套策略，沒有第二套演算法
do
    -- 這張殺是變出來的，但走的是 ai_card_use.Slash：打共用排序挑出來的那個敵人。
    local ai = assert(SmartAIView.new(request({
        conversions = {conversion({id = 1})}})))
    local view = assert(ai:getConversion(1))
    local plan, status = ai:tryUseCard(view)
    assert(status == "planned")
    assert(plan.card == view)
    assert(#plan.to == 1)
    assert(plan.to[1]:objectName() == "weak")
    -- 答案送的是票，不是牌名。牌名一起送只是給權威端比對。
    local answer = assert(plan:toAnswer())
    assert(answer.card_id == nil)
    assert(answer.card_spec ~= nil)
    assert(answer.card_spec.conversion_id == 1)
    assert(answer.card_spec.name == "slash")
    assert(answer.card_spec.skill == "conv_free")
    assert(answer.targets[1] == "weak")
end

do
    -- 成本會原樣送回去，權威端才比得出「用另一張牌付帳」這種偽造。
    local ai = assert(SmartAIView.new(request({
        conversions = {conversion({id = 4, subcards = {11}})}})))
    local view = assert(ai:getConversion(4))
    local answer = assert(select(1, ai:tryUseCard(view)):toAnswer())
    assert(#answer.card_spec.subcards == 1)
    assert(answer.card_spec.subcards[1] == 11)
end

do
    -- 沒有策略的轉化是未覆蓋，不是「決定不打」。
    local ai = assert(SmartAIView.new(request({
        conversions = {conversion({id = 1, name = "lightning", class_name = "Lightning",
            kinds = {"Lightning", "DelayedTrick", "TrickCard", "Card"}})}})))
    local view = assert(ai:getConversion(1))
    local plan, status = ai:tryUseCard(view)
    assert(status == "unsupported")
    assert(plan.reason == "no isolated strategy for this card")
end

-- ------------------------------------------------------- newCard 不製造授權
do
    local ai = assert(SmartAIView.new(request({
        conversions = {conversion({id = 1, skill = "conv_free", instance = 1}),
                       conversion({id = 2, skill = "conv_cost", instance = 3,
                           subcards = {11}})}})))
    -- 找得到：回的是權威端那一筆，不是新造的東西。
    local found = assert(ai:newCard("slash"))
    assert(found:getConversionId() == 1)
    -- 指名技能與實例時各自挑對。
    assert(ai:newCard("slash", {skill = "conv_cost"}):getConversionId() == 2)
    assert(ai:newCard("slash", {instance = 3}):getConversionId() == 2)
    assert(ai:newCard("slash", {subcards = {11}}):getConversionId() == 2)
    -- 找不到對應、完整且已授權的轉化能力時回 nil，不是 clone 一張照用。
    assert(ai:newCard("peach") == nil)
    assert(ai:newCard("slash", {skill = "conv_never"}) == nil)
    assert(ai:newCard("slash", {instance = 99}) == nil)
    assert(ai:newCard("slash", {subcards = {12}}) == nil)
    assert(ai:newCard("") == nil)
end

-- ------------------------------------------- 「列不完」與「沒有」是兩個答案
do
    local ai = assert(SmartAIView.new(request({conversions = {}})))
    assert(ai:hasEnumeratedConversions() == true)
    assert(#assert(ai:getConversions()) == 0)
end

do
    -- 列不完：整題未覆蓋。空清單在這裡不可以被讀成「這些技能變不出東西」。
    local ai = assert(SmartAIView.new(request({conversions = {}, enumerated = false})))
    assert(ai:hasEnumeratedConversions() == false)
    local ok, signal = pcall(function() return ai:planTurnUse() end)
    assert(ok == false)
    assert(AIUnsupported.is(signal))
    assert(signal.key == "conversion")
    assert(signal.reason == "available conversions are not fully enumerated")
end

do
    -- 已知實體牌已有完整 authority ticket 時，即使另一條轉化列舉未知，
    -- 仍可回答這個已知合法 action；未知只隨答案回傳，不能抹掉它。
    local ai = assert(SmartAIView.new(request({
        hand = {{id = 7, effective_id = 7, name = "slash", class_name = "Slash",
            kind_of = {"Slash", "BasicCard", "Card"}}},
        candidates = {physical_candidate(7, 7007)},
        conversions = {}, enumerated = false})))
    local answer, used, unknown = ai:planTurnUse()
    assert(answer and answer.kind == "use_card" and answer.card_id == 7)
    assert(answer.candidate_id == 7007 and used.card_id == 7)
    assert(AIUnsupported.is(unknown) and unknown.key == "conversion")
end

do
    -- 權威端說這張轉化的目標組合描述不完整時，規劃端不截斷後假裝完整。
    local ai = assert(SmartAIView.new(request({
        conversions = {conversion({id = 1, complete = false})}})))
    local ok, signal = pcall(function() return ai:planTurnUse() end)
    assert(ok == false)
    assert(AIUnsupported.is(signal))
    assert(signal.key == "Slash")
end

-- ------------------------------------------------- 技能覆蓋的第二條路
local function skill_action(skill, instance)
    return {activation_owner = "viewer", activation_skill = skill,
        activation_instance = instance, source_owner = "viewer",
        source_skill = skill, source_instance = instance,
        activation_quota_available = true, source_quota_available = true}
end

do
    -- 這個技能沒有 ai_skill_activate handler，但它的轉化已經列完而且變出來的殺有策略，
    -- 所以這一題仍然是完整覆蓋的——activate 規劃得出答案，不整題回退。
    assert(ai_skill_activate["conv_free"] == nil)
    ai_coverage.clearUncovered()
    local answer = assert(ai_decide(request({
        conversions = {conversion({id = 1, skill = "conv_free", instance = 1})},
        skill_actions = {skill_action("conv_free", 1)}})))
    assert(answer.kind == "use_card")
    assert(answer.card_id == nil)
    -- 票要活過 normalize，否則權威端收到的是一個不授權任何東西的 spec。
    assert(answer.card_spec.conversion_id == 1)
    assert(answer.card_spec.name == "slash")
    assert(answer.targets[1] == "weak")
    assert(#ai_coverage.uncovered() == 0)
end

do
    -- 同一個技能，轉化出來的牌沒有策略：那條路不算覆蓋，整題回退並留下原因。
    ai_coverage.clearUncovered()
    local answer = ai_decide(request({
        conversions = {conversion({id = 1, skill = "conv_free", instance = 1,
            name = "lightning", class_name = "Lightning",
            kinds = {"Lightning", "DelayedTrick", "TrickCard", "Card"}})},
        skill_actions = {skill_action("conv_free", 1)}}))
    assert(answer == nil)
    local recorded = ai_coverage.uncovered()
    assert(#recorded == 1)
    assert(recorded[1].key == "conv_free")
    ai_coverage.clearUncovered()
end

do
    -- 列不完就是未覆蓋，即使這個技能本身看起來無害。空清單在這裡不可以被讀成
    -- 「這個技能變不出東西」。
    ai_coverage.clearUncovered()
    local answer = ai_decide(request({conversions = {}, enumerated = false,
        skill_actions = {skill_action("conv_free", 1)}}))
    assert(answer == nil)
    assert(#ai_coverage.uncovered() == 1)
    ai_coverage.clearUncovered()
end

-- ------------------------------------------------------ 分支不互相污染
do
    -- 規劃 A 不可以把狀態留給 B。兩筆轉化各自規劃，第二筆看到的目標清單要與第一筆
    -- 無關——這是計畫 7.3 的「評估 A 不可污染 B」在本批能驗到的那一半。
    local ai = assert(SmartAIView.new(request({
        conversions = {conversion({id = 1, targets = {"weak", "strong"}}),
                       conversion({id = 2, targets = {"strong"}})}})))
    local first = assert(ai:getConversion(1))
    local second = assert(ai:getConversion(2))
    local first_plan = assert(select(1, ai:tryUseCard(first)))
    assert(first_plan.to[1]:objectName() == "weak")
    local second_plan = assert(select(1, ai:tryUseCard(second)))
    assert(#second_plan.to == 1)
    assert(second_plan.to[1]:objectName() == "strong")
    -- 第一筆的計劃沒有被第二筆改掉。
    assert(#first_plan.to == 1)
    assert(first_plan.to[1]:objectName() == "weak")
    -- 再規劃一次第一筆，答案與第一次相同：規劃沒有累積副作用。
    local again = assert(select(1, ai:tryUseCard(first)))
    assert(again.to[1]:objectName() == "weak")
end

return true
