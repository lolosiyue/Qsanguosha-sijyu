-- PR 05 的牌族契約：決鬥與拆牌族的能力邊界。
-- 與 isolated-use-plan-contract.lua 一樣只需要 isolated-bootstrap／isolated-facades／
-- decision-core，不需要 Room、Engine 或任何一支武將 handler。
-- 這裡驗的是三件事：已知張數與估計張數分得開、每一族的 planned／declined／unsupported
-- 三態各自答得出來、以及拆牌族用自己的估值挑目標而不是照抄共用排序的第一名。
local function player(name, fields)
    local view = {object_name = name, alive = true, dead = false, handcard_count = 0,
        hp = 4, max_hp = 4, hujia = 0, max_cards = 4, attack_range = 1,
        equips = {}, judging_area = {}, public_marks = {}, skills = {},
        known_cards = {}, hand_visible = false}
    for key, value in pairs(fields or {}) do view[key] = value end
    return view
end

local function card(id, name, class_name, kinds)
    return {id = id, effective_id = id, name = name, class_name = class_name,
        number = 7, suit = sgs.Card_Club, red = false, black = true,
        kind_of = kinds}
end

local function slash(id) return card(id, "slash", "Slash", {"Slash", "BasicCard", "Card"}) end
local function peach(id) return card(id, "peach", "Peach", {"Peach", "BasicCard", "Card"}) end
local function duel(id) return card(id, "duel", "Duel", {"Duel", "TrickCard", "Card"}) end

local crossbow = card(30, "crossbow", "Crossbow", {"Crossbow", "Weapon", "EquipCard", "Card"})
-- Match the native Crossbow weapon range used by value evaluation.
crossbow.weapon_range = 1
local indulgence = card(31, "indulgence", "Indulgence",
    {"Indulgence", "DelayedTrick", "TrickCard", "Card"})

-- 三名敵人刻意讓共用排序與拆牌估值給出不同的第一名：
--   poor  血 1，所以共用排序把他排最前面（isWeak），但身上只有一張手牌。
--   rich  血滿，共用排序排在後面，但有一件裝備加一張手牌，拆起來划算。
--   bound 身上只剩判定區的樂不思蜀，拆掉是幫他解套，估值 0。
local function request(fields)
    fields = fields or {}
    local hand = fields.hand or {slash(7), slash(8), duel(20)}
    local viewer = player("viewer", {hp = fields.hp or 4, handcard_count = #hand})
    local world = {
        mode_id = "trick-families-fixture", revision = "1", current_player = "viewer",
        self = viewer,
        players = {
            player("poor", {hp = 1, handcard_count = 1}),
            player("rich", {hp = 4, handcard_count = 1, equips = {crossbow}}),
            player("bound", {hp = 4, handcard_count = 0, judging_area = {indulgence}})
        },
        player_order = {"viewer", "poor", "rich", "bound"},
        alive_player_order = {"viewer", "poor", "rich", "bound"},
        distances = {viewer = {poor = 1, rich = 2, bound = 1}},
        hand_cards = hand
    }
    if fields.unmanaged ~= true then
        world.mode_policy = {managed = true, objectives = {},
            relations = {viewer = {poor = "enemy", rich = "enemy", bound = "enemy"}}}
    end
    return {viewer = "viewer", kind = "activate", world_view = world,
        card_candidates = fields.candidates or {}}
end

local function candidate(card_id, targets)
    return {card_id = card_id, available = true, limited = false, jilei = false,
        target_fixed = false, feasible_with_no_target = false, complete_coverage = true,
        max_votes = {}, legal_targets = targets,
        target_combinations = (function() local rows = {}; for _, name in ipairs(targets) do rows[#rows+1] = {name} end; return rows end)()}
end

-- ---------------------------------------------------------------- 已知 vs 估計
do
    local ai = assert(SmartAIView.new(request()))
    local rich = assert(ai.room:findPlayerByObjectName("rich"))
    -- 自己的手牌是完全已知的：沒有暗牌，所以估計等於已知，不會被密度加料。
    assert(ai:getUnknownCardsNum(ai.player) == 0)
    assert(ai:getCardsNum("Slash", ai.player) == 2)
    assert(ai:estimateCardsNum("Slash", ai.player) == 2)
    -- 別人的暗牌是「沒看到」，不是「沒有」：已知張數照實回 0，估計張數大於 0。
    assert(ai:getCardsNum("Slash", rich) == 0)
    assert(ai:getUnknownCardsNum(rich) == 1)
    local estimate = ai:estimateCardsNum("Slash", rich)
    assert(estimate > 0 and estimate < 1)
    -- 沒有登記密度的牌族回 nil，不回一個看起來像已知的 0。
    assert(ai:estimateCardsNum("Dismantlement", rich) == nil)
end

do
    -- 手牌全開的人沒有暗牌可估，估計必須退回已知張數本身，不再乘密度。
    local base = request()
    base.world_view.players[2].known_cards = {slash(41)}
    base.world_view.players[2].hand_visible = true
    local ai = assert(SmartAIView.new(base))
    local rich = assert(ai.room:findPlayerByObjectName("rich"))
    assert(ai:getUnknownCardsNum(rich) == 0)
    assert(ai:estimateCardsNum("Slash", rich) == 1)
end

-- ---------------------------------------------------------------------- 決鬥
local duel_card = CardView.new(duel(20))

do
    -- 沒有候選、認不出敵友：兩種未覆蓋，理由不同，都不是 declined。
    local ai = assert(SmartAIView.new(request()))
    local plan, status = ai:tryUseCard(duel_card)
    assert(status == "unsupported" and plan.key == "Duel")
    assert(plan.reason == "this request carries no candidate for the Duel")

    ai = assert(SmartAIView.new(request({unmanaged = true,
        candidates = {candidate(20, {"poor", "rich"})}})))
    plan, status = ai:tryUseCard(duel_card)
    assert(status == "unsupported" and plan.key == "Duel")
    assert(plan.reason == "the mode policy does not describe relations")
end

do
    -- 手上兩張殺對上只有一張暗牌的敵人：估計贏得了，所以開。
    local ai = assert(SmartAIView.new(request({
        candidates = {candidate(20, {"poor", "rich"})}})))
    local plan, status = ai:tryUseCard(duel_card)
    assert(status == "planned" and plan.card == duel_card and plan.to:length() == 1)
end

do
    -- 同一張決鬥，手上沒有殺：有策略，而且策略說不打。這是 declined，不是未覆蓋。
    local ai = assert(SmartAIView.new(request({
        hand = {peach(9), duel(20)},
        candidates = {candidate(20, {"poor", "rich"})}})))
    local plan, status = ai:tryUseCard(duel_card)
    assert(status == "declined" and plan.card == nil)
end

do
    -- 餘裕規則：一張殺對上三張暗牌（估計 0.83）平常開得下去……
    local wide = request({hand = {slash(7), duel(20)},
        candidates = {candidate(20, {"poor"})}})
    wide.world_view.players[1].handcard_count = 3
    local ai = assert(SmartAIView.new(wide))
    local plan, status = ai:tryUseCard(duel_card)
    assert(status == "planned")
    -- ……但血只剩一點又沒有桃時，輸掉就是死，追平不夠，要有餘裕才開。
    local risky = request({hand = {slash(7), duel(20)},
        candidates = {candidate(20, {"poor"})}})
    risky.world_view.players[1].handcard_count = 3
    risky.world_view.self.hp = 1
    ai = assert(SmartAIView.new(risky))
    plan, status = ai:tryUseCard(duel_card)
    assert(status == "declined" and plan.card == nil)
    -- 手上有桃就挨得起，同一個局面又開得下去。
    local insured = request({hand = {slash(7), peach(9), duel(20)},
        candidates = {candidate(20, {"poor"})}})
    insured.world_view.players[1].handcard_count = 3
    insured.world_view.self.hp = 1
    ai = assert(SmartAIView.new(insured))
    plan, status = ai:tryUseCard(duel_card)
    assert(status == "planned")
end

-- ------------------------------------------------------------------ 拆牌族
local snatch_card = CardView.new(card(21, "snatch", "Snatch",
    {"Snatch", "SingleTargetTrick", "TrickCard", "Card"}))
local dismantle_card = CardView.new(card(22, "dismantlement", "Dismantlement",
    {"Dismantlement", "SingleTargetTrick", "TrickCard", "Card"}))

do
    local ai = assert(SmartAIView.new(request({
        candidates = {candidate(21, {"poor", "rich"}), candidate(22, {"poor", "rich"})}})))
    -- 共用排序把血最少的 poor 排在最前面：敵友與威脅仍然只有這一份排序說了算。
    local ranked = ai:rankTargets(AIList.new({"poor", "rich"}), "enemy")
    assert(ranked:first():objectName() == "poor")
    -- 但拆牌族在那份順序上套自己的估值，所以挑的是身上東西比較多的 rich。
    local plan, status = ai:tryUseCard(snatch_card)
    assert(status == "planned" and plan.to:first():objectName() == "rich")
    -- 兩張牌共用同一份估值，所以指同一個人；差別在 use value，不在挑誰。
    local other, other_status = ai:tryUseCard(dismantle_card)
    assert(other_status == "planned" and other.to:first():objectName() == "rich")
    assert(ai:getUseValue(snatch_card) > ai:getUseValue(dismantle_card))
end

do
    -- 身上只剩判定區的敵人估值是 0：拆掉樂不思蜀是幫他，所以不指他。
    -- 場上只剩這種人時是 declined——有策略，決定不打。
    local ai = assert(SmartAIView.new(request({
        candidates = {candidate(21, {"bound"}), candidate(22, {"bound"})}})))
    local plan, status = ai:tryUseCard(snatch_card)
    assert(status == "declined" and plan.card == nil)
    plan, status = ai:tryUseCard(dismantle_card)
    assert(status == "declined" and plan.card == nil)
end

do
    -- 候選裡一個敵人都沒有也是 declined，不是未覆蓋：權威端把話說完了。
    local ai = assert(SmartAIView.new(request({candidates = {candidate(21, {})}})))
    local plan, status = ai:tryUseCard(snatch_card)
    assert(status == "declined" and plan.card == nil)
end

do
    -- 未覆蓋的兩條路與決鬥相同，而且理由各自帶著自己的牌名。
    local ai = assert(SmartAIView.new(request()))
    local plan, status = ai:tryUseCard(snatch_card)
    assert(status == "unsupported" and plan.key == "Snatch")
    assert(plan.reason == "this request carries no candidate for the Snatch")
    plan, status = ai:tryUseCard(dismantle_card)
    assert(status == "unsupported" and plan.key == "Dismantlement")
    assert(plan.reason == "this request carries no candidate for the Dismantlement")

    ai = assert(SmartAIView.new(request({unmanaged = true,
        candidates = {candidate(22, {"rich"})}})))
    plan, status = ai:tryUseCard(dismantle_card)
    assert(status == "unsupported"
        and plan.reason == "the mode policy does not describe relations")
end

-- ------------------------------------------------------------------ 出牌順序
do
    -- 優先序照 legacy standard_cards-ai.lua 的相對順序：拆 > 順 > 決鬥 > 殺。
    local ai = assert(SmartAIView.new(request()))
    local ordered = ai:sortByUsePriority(AIList.new({
        CardView.new(slash(7)), duel_card, snatch_card, dismantle_card}))
    local names = {}
    for index, entry in ipairs(ordered) do names[index] = entry:getClassName() end
    assert(table.concat(names, ",") == "Dismantlement,Snatch,Duel,Slash")
end

-- 這一批的三支策略全部登記在同一張 ai_card_use 表上，沒有第二套入口。
assert(ai_coverage.covers("card_use", "Duel"))
assert(ai_coverage.covers("card_use", "Snatch"))
assert(ai_coverage.covers("card_use", "Dismantlement"))
-- 拆完之後「丟／拿哪一張」是另一個 card_chosen 詢問，這個 runtime 沒有接。
-- 過河拆橋刻意不指友軍的理由就是這一行：指了等於把結果押在自己沒有作答的決策上。
assert(not ai_coverage.covers("card_chosen"))
return true
