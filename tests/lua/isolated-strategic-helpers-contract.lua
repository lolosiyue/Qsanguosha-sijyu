-- Shared SmartAI helper contract: pure facade values, legacy return shapes, and
-- unknown-versus-known boundaries.  No native Room/Card/Skill userdata is used.
local function skill(name)
    return {name = name, instance_id = 1, invalid = false}
end

local function card(id, name, class_name, kinds)
    return {id = id, effective_id = id, name = name, class_name = class_name,
        kind_of = kinds or {class_name, "Card"}}
end

local peach = card(1, "peach", "Peach", {"Peach", "BasicCard", "Card"})
local jink = card(2, "jink", "Jink", {"Jink", "BasicCard", "Card"})
local slash = card(3, "slash", "Slash", {"Slash", "BasicCard", "Card"})
local enemy_card = card(9, "enemy_slash", "Slash", {"Slash", "BasicCard", "Card"})

local function player(name, fields)
    local value = {object_name = name, alive = true, dead = false, hp = 3, max_hp = 4,
        handcard_count = 0, hujia = 0, max_cards = 3, attack_range = 1, role = "loyalist",
        equips = {}, judging_area = {}, known_cards = {}, hand_visible = true,
        skills = {}, public_marks = {}}
    for key, item in pairs(fields or {}) do value[key] = item end
    return value
end

local request = {viewer = "viewer", kind = "activate", world_view = {
    mode_id = "helper-fixture", revision = "1", current_player = "viewer",
    self = player("viewer", {hp = 3, handcard_count = 3,
        skills = {skill("longhun")}, known_cards = {peach, jink, slash}}),
    players = {
        player("friend", {hp = 1, handcard_count = 1, known_cards = {jink},
            skills = {skill("helper_cardneed")}}),
        player("enemy", {hp = 3, handcard_count = 1, known_cards = {enemy_card},
            skills = {skill("kongcheng")}})
    },
    player_order = {"viewer", "friend", "enemy"},
    alive_player_order = {"viewer", "friend", "enemy"},
    hand_cards = {peach, jink, slash},
    mode_policy = {managed = true, objectives = {}, relations = {
        viewer = {friend = "friend", enemy = "enemy"}}}
}}

local ai = assert(SmartAIView.new(request))
local friend = assert(ai.room:findPlayerByObjectName("friend"))
local enemy = assert(ai.room:findPlayerByObjectName("enemy"))

assert(ai:needKongcheng(enemy) == true)
assert(ai:needKongcheng(friend, true) == false)
assert(ai:getBestHp(ai.player) == 1)
assert(ai:needToLoseHp(friend, ai.player, CardView.new(slash)) == false)
assert(ai.needToloseHp == ai.needToLoseHp)

assert(ai:doDisCard(enemy, "h") == true)
assert(ai:doDisCard(enemy, 9) == true)
local unknown = player("hidden", {handcard_count = 1, hand_visible = false, known_cards = nil})
local hidden_request = {viewer = "viewer", kind = "activate", world_view = {
    self = player("viewer"), players = {unknown}, player_order = {"viewer", "hidden"},
    alive_player_order = {"viewer", "hidden"}, hand_cards = {}}}
local hidden_ai = assert(SmartAIView.new(hidden_request))
local hidden = assert(hidden_ai.room:findPlayerByObjectName("hidden"))
assert(hidden_ai:doDisCard(hidden, 9) == nil)

local needed_card, needed_player = ai:getCardNeedPlayer(AIList.new({CardView.new(peach), CardView.new(jink)}), false)
assert(needed_card and needed_player and needed_player:objectName() == "friend")
assert(ai:getCardNeedPlayer(AIList.new({}), false) == nil)
-- Core getBestHp counts equipment as well as hand cards and preserves awakening.
local equipped = player("equipped", {handcard_count = 1,
    skills = {skill("longhun")}, equips = {slash, jink}})
assert(getBestHp(PlayerView.new(equipped)) == 1)
local awakening = player("awakening", {skills = {skill("quanji"), skill("zili")}})
assert(getBestHp(PlayerView.new(awakening)) == 3)
local overlapping = player("overlapping", {skills = {skill("ganlu"), skill("yinghun")}})
assert(getBestHp(PlayerView.new(overlapping)) == 3)
local loss = player("loss", {hp = 1, skills = {skill("shangshi")}})
assert(ai:getLeastHandcardNum(PlayerView.new(loss)) == 2)

-- Callback ABI is exact, false is a decision, duplicate active instances run once.
local hook_player = PlayerView.new(player("hook", {hp = 3,
    skills = {skill("helper_need"), {name = "helper_need", instance_id = 2, invalid = false}}}))
local calls = 0
sgs.ai_need_damaged.helper_need = function(self, source, target, used_card)
    assert(self == ai and source == ai.player and target == hook_player)
    assert(AIValue.isCard(used_card))
    calls = calls + 1
    return false
end
assert(ai:needToLoseHp(hook_player, ai.player, CardView.new(slash)) == false)
assert(calls == 1)
sgs.ai_need_damaged.helper_need = nil
sgs.ai_getBestHp_skill.helper_need = function(owner)
    assert(owner == hook_player)
    return 2
end
assert(ai:needToLoseHp(hook_player, ai.player, CardView.new(slash), true) == true)
hook_player._view.hp = 2
assert(ai:needToLoseHp(hook_player, ai.player, CardView.new(slash), true) == false)
assert(ai:needToLoseHp(hook_player, ai.player, CardView.new(slash), true, true) == true)
hook_player._view.hp = 1
assert(ai:needToLoseHp(hook_player, ai.player, CardView.new(slash)) == false)
sgs.ai_getBestHp_skill.helper_need = nil

-- A friendly ordinary hand card is not beneficial to strip just because it exists.
assert(ai:doDisCard(friend, 2) == false)
local discard_request = {viewer = "viewer", world_view = {
    self = player("viewer"), players = {
        player("friend", {hp = 2, equips = {card(21, "silver_lion", "SilverLion",
            {"SilverLion", "Armor", "EquipCard", "Card"})}})}, hand_cards = {},
    mode_policy = {managed = true, relations = {viewer = {friend = "friend"}}}}}
local discard_ai = assert(SmartAIView.new(discard_request))
assert(discard_ai:doDisCard(discard_ai.room:findPlayerByObjectName("friend"), 21) == true)

-- Cardneed is called only for registered recipient skills, after survival gifts.
sgs.ai_cardneed.helper_cardneed = function(target, offered, self)
    assert(self == ai and target == friend and AIValue.isCard(offered))
    return offered:isKindOf("Slash")
end
local need, recipient = ai:getCardNeedPlayer(AIList.new({CardView.new(slash)}), false)
assert(need and need:isKindOf("Slash") and recipient == friend)
sgs.ai_cardneed.helper_cardneed = nil

-- Four revise consumers retain positional ABI and actual false veto semantics.
local probe = card(31, "helper_probe", "HelperProbe", {"TrickCard", "Card"})
probe.target_fixed = false
local use_request = {viewer = "viewer", kind = "activate", world_view = {
    self = player("viewer", {skills = {skill("helper_use")}}),
    players = {player("target", {skills = {skill("helper_target")}})}, hand_cards = {probe},
    player_order = {"viewer", "target"}, alive_player_order = {"viewer", "target"},
    mode_policy = {managed = true, relations = {viewer = {target = "enemy"}}}},
    card_candidates = {{card_id = 31, candidate_id = 1, available = true, limited = false, target_fixed = false,
        complete_coverage = true, feasible_with_no_target = false,
        legal_targets = {"target"}, target_combinations = {{"target"}}}}}
local use_ai = assert(SmartAIView.new(use_request))
local probe_view, target_player = CardView.new(probe), use_ai.room:findPlayerByObjectName("target")
local strategy_calls, post_calls = 0, 0
ai_card_use.helper_probe = function(self, proposed, use)
    strategy_calls = strategy_calls + 1
    use.card, use.to = proposed, AIList.new({target_player})
end
sgs.ai_use_revises.helper_use = function(self, proposed, use)
    assert(self == use_ai and proposed == probe_view and type(use) == "table")
    return false
end
local plan, plan_status = use_ai:tryUseCard(probe_view)
assert(plan_status == "declined" and plan.card == nil and strategy_calls == 0)
sgs.ai_use_revises.helper_use = nil
sgs.ai_useto_revises.helper_target = function(self, proposed, use, target)
    assert(self == use_ai and proposed == probe_view and target == target_player)
    return false
end
plan, plan_status = use_ai:tryUseCard(probe_view)
assert(plan_status == "declined" and plan.card == nil and strategy_calls == 0)
sgs.ai_useto_revises.helper_target = nil
sgs.ai_target_revises.helper_target = function(target, proposed, self, use)
    assert(target == target_player and proposed == probe_view and self == use_ai)
    return false
end
sgs.ai_used_revises.helper_use = function(self, use)
    assert(self == use_ai and use.card == probe_view)
    post_calls = post_calls + 1
end
plan, plan_status = use_ai:tryUseCard(probe_view)
assert(plan_status == "planned" and strategy_calls == 1 and post_calls == 1)
assert(use_ai.aiUsecard == use_ai.aiUseCard)
sgs.ai_target_revises.helper_target = nil
sgs.ai_used_revises.helper_use = nil
ai_card_use.helper_probe = nil
-- Shared cardNeed drives a stable copy sort and does not mutate input ordering.
local need_cards = AIList.new({CardView.new(peach), CardView.new(slash)})
assert(ai:cardNeed(need_cards[1]) > ai:cardNeed(need_cards[2]))
local need_sorted = assert(ai:sortByCardNeed(need_cards))
assert(need_sorted[1]:isKindOf("Slash") and need_cards[1]:isKindOf("Peach"))
assert(ai:sortByCardNeed(need_cards, true)[1]:isKindOf("Peach"))
assert(ai:sortByCardNeed(need_cards, false, "j") == nil)

-- Actual Dismantlement planning can relieve a friend's harmful judgment.
local strip = card(41, "dismantlement", "Dismantlement", {"Dismantlement", "TrickCard", "Card"})
strip.target_fixed = false
local strip_request = {viewer = "viewer", kind = "activate", world_view = {
    self = player("viewer"), players = {player("friend", {judging_area = {
        card(42, "indulgence", "Indulgence", {"Indulgence", "DelayedTrick", "TrickCard", "Card"})}})},
    hand_cards = {strip}, player_order = {"viewer", "friend"},
    alive_player_order = {"viewer", "friend"},
    mode_policy = {managed = true, relations = {viewer = {friend = "friend"}}}},
    card_candidates = {{card_id = 41, candidate_id = 2, available = true, limited = false, target_fixed = false,
        complete_coverage = true, feasible_with_no_target = false,
        legal_targets = {"friend"}, target_combinations = {{"friend"}}}}}
local strip_ai = assert(SmartAIView.new(strip_request))
local strip_plan, strip_status = strip_ai:tryUseCard(CardView.new(strip))
assert(strip_status == "planned" and strip_plan.to[1]:objectName() == "friend")
assert(strip_ai:willUse(strip_ai.player, CardView.new(strip), false, false, true) == true)
assert(strip_ai:willUse(strip_ai.player, CardView.new(strip), true) == nil)

-- Implicit target revises consume the candidate's actual affected-target list.
local aoe = card(51, "helper_aoe", "HelperAOE", {"AOE", "TrickCard", "Card"})
aoe.target_fixed = true
use_request.world_view.hand_cards = {aoe}
use_request.card_candidates = {{card_id = 51, candidate_id = 3, available = true, limited = false, target_fixed = true,
    complete_coverage = true, feasible_with_no_target = true, legal_targets = {},
    target_combinations = {{}}, affected_targets = {"target"}}}
local aoe_ai = assert(SmartAIView.new(use_request))
local affected = 0
ai_card_use.helper_aoe = function(self, offered, use) use.card = offered end
sgs.ai_target_revises.helper_target = function(target, offered, self, use)
    assert(target:objectName() == "target" and self == aoe_ai and offered:isKindOf("AOE"))
    affected = affected + 1
    return false
end
local aoe_plan, aoe_status = aoe_ai:tryUseCard(CardView.new(aoe))
assert(aoe_status == "planned" and aoe_plan.card and affected == 1)
sgs.ai_target_revises.helper_target = nil
ai_card_use.helper_aoe = nil
-- Fixed cost annotations never turn an already bound ticket into a parameter request.
local fixed = ConversionView.new({conversion_id = 101, name = "slash", cost_count = 1, subcards = {3}})
assert(ai:bindConversionCosts(fixed, {}) == fixed)
local free = ConversionView.new({conversion_id = 102, name = "slash", cost_count = 0, subcards = {}})
assert(ai:bindConversionCosts(free, {}) == free)
local native_fixed = ConversionView.new({conversion_id = 105, name = "slash", cost_count = 0, subcards = {3}})
assert(ai:bindConversionCosts(native_fixed, {}) == native_fixed)
local incomplete_cost = ConversionView.new({conversion_id = 103, name = "slash", cost_count = 2, subcards = {}})
local cost_ok, cost_signal = AIUnsupported.capture(function() return ai:bindConversionCosts(incomplete_cost, {}) end)
assert(not cost_ok and AIUnsupported.is(cost_signal))
local parameterized = ConversionView.new({conversion_id = 104, name = "slash", cost_count = 2,
    subcards = {}, eligible_subcards = {1, 2, 2, 3}})
local scores, original_keep = {}, ai.getKeepValue
ai.getKeepValue = function(self, offered)
    local id = offered:getId()
    scores[id] = (scores[id] or 0) + 1
    return id
end
local bound = ai:bindConversionCosts(parameterized, {
    [1] = CardView.new(peach), [2] = CardView.new(jink), [3] = CardView.new(slash)})
assert(bound:getSubcards()[1] == 1 and bound:getSubcards()[2] == 2)
assert(scores[1] == 1 and scores[2] == 1 and scores[3] == 1)
assert(#parameterized:getSubcards() == 0)
ai.getKeepValue = original_keep

-- canUse consumes the native candidate-pool shape, restricted to current authority.
strip_request.card_candidates[1].available = true
strip_request.card_candidates[1].limited = false
local can_ai = assert(SmartAIView.new(strip_request))
local can_friend = can_ai.room:findPlayerByObjectName("friend")
assert(can_ai:canUse(CardView.new(strip), AIList.new({can_friend})) == true)
assert(can_ai:canUse(CardView.new(strip), AIList.new({can_ai.player})) == false)
assert(can_ai:canUse(CardView.new(strip), AIList.new({})) == false)
assert(can_ai:canUse(CardView.new(strip), AIList.new({can_friend}), can_friend) == nil)
-- Hook-only proposals are autonomous isolated decisions, not native SmartAI
-- stand-ins. The activate preflight must exercise callbacks without used hooks.
local function hook_only_request(extra)
    local offered = card(61, "helper_hook_only", "HelperHookOnly", {"TrickCard", "Card"})
    offered.target_fixed = true
    local req = {viewer = "viewer", kind = "activate", conversions_enumerated = true,
        card_conversions = {}, skill_actions = {}, world_view = {
            self = player("viewer", {skills = {skill("helper_hook_owner")}}),
            players = {}, hand_cards = {offered},
            player_order = {"viewer"}, alive_player_order = {"viewer"}},
        card_candidates = {{card_id = 61, candidate_id = 61, available = true, limited = false,
            target_fixed = true, feasible_with_no_target = true, complete_coverage = true,
            legal_targets = {}, target_combinations = {{}}}}}
    if extra then
        local unknown = card(62, "helper_unknown", "HelperUnknown", {"TrickCard", "Card"})
        unknown.target_fixed = true
        req.world_view.hand_cards[2] = unknown
        req.card_candidates[2] = {card_id = 62, candidate_id = 62, available = true, limited = false,
            target_fixed = true, feasible_with_no_target = true, complete_coverage = true,
            legal_targets = {}, target_combinations = {{}}}
    end
    return req, CardView.new(offered)
end
local used_hook_count = 0
sgs.ai_used_revises.helper_hook_owner = function(self, use)
    assert(not use.isDummy and use.card:objectName() == "helper_hook_only")
    used_hook_count = used_hook_count + 1
end
sgs.ai_skill_carduse.helper_hook_owner = function(self, offered, use)
    assert(AIValue.isPlayer(self.player) and AIValue.isCard(offered))
    if offered:objectName() ~= "helper_hook_only" then return nil end
    use.card = offered
    return true
end
local hook_req, hook_card = hook_only_request()
local hook_ai = assert(SmartAIView.new(hook_req))
local hook_plan, hook_status = hook_ai:tryUseCard(hook_card)
assert(hook_status == "planned" and hook_plan.card == hook_card and used_hook_count == 1)
used_hook_count = 0
local hook_answer = ai_decide(hook_only_request())
assert(hook_answer and hook_answer.kind == "use_card" and hook_answer.card_id == 61)
assert(used_hook_count == 1)

-- Unknown alternatives do not invalidate an actual authorized proposal.
used_hook_count = 0
hook_answer = ai_decide(hook_only_request(true))
assert(hook_answer and hook_answer.card_id == 61 and used_hook_count == 1)
local partial_request = hook_only_request()
partial_request.conversions_enumerated = false
used_hook_count = 0
hook_answer = ai_decide(partial_request)
assert(hook_answer and hook_answer.card_id == 61 and used_hook_count == 1)
-- Dummy plans never fire used revises, and malformed proposals never reach them.
used_hook_count = 0
local dummy = AIUsePlan.new()
dummy.isDummy = true
hook_plan, hook_status = hook_ai:tryUseCard(hook_card, dummy)
assert(hook_status == "planned" and used_hook_count == 0)
sgs.ai_skill_carduse.helper_hook_owner = function(self, offered, use)
    local unoffered = card(63, "unoffered", "Unoffered", {"TrickCard", "Card"})
    unoffered.target_fixed = true
    use.card = CardView.new(unoffered)
    return true
end
hook_plan, hook_status = hook_ai:tryUseCard(hook_card)
assert(hook_status == "unsupported" and used_hook_count == 0)

for _, result in ipairs({false, "nil"}) do
    sgs.ai_skill_carduse.helper_hook_owner = function()
        if result == false then return false end
        return nil
    end
    local req, offered = hook_only_request()
    local unknown_ai = assert(SmartAIView.new(req))
    local signal, status = unknown_ai:tryUseCard(offered)
    assert(status == "unsupported" and AIUnsupported.is(signal))
    assert(ai_decide(hook_only_request()) == nil)
end
sgs.ai_skill_carduse.helper_hook_owner = nil
local unrelated_calls = 0
sgs.ai_skill_carduse.helper_unrelated = function(self, offered, use)
    unrelated_calls = unrelated_calls + 1
    use.card = offered
    return true
end
assert(ai_decide(hook_only_request()) == nil and unrelated_calls == 0)
sgs.ai_skill_carduse.helper_unrelated = nil
-- Non-functions and affirmative callbacks without a proposal prove no coverage.
sgs.ai_skill_carduse.helper_hook_owner = true
assert(ai_decide(hook_only_request()) == nil)
sgs.ai_skill_carduse.helper_hook_owner = nil
sgs.ai_use_revises.helper_hook_owner = function() return true end
assert(ai_decide(hook_only_request()) == nil)
sgs.ai_use_revises.helper_hook_owner = function(self, offered, use)
    use.card = offered
    return true
end
used_hook_count = 0
hook_req, hook_card = hook_only_request()
hook_plan, hook_status = assert(SmartAIView.new(hook_req)):tryUseCard(hook_card)
assert(hook_status == "planned" and hook_plan.card == hook_card and used_hook_count == 1)
used_hook_count = 0
hook_answer = ai_decide(hook_only_request())
assert(hook_answer and hook_answer.card_id == 61 and used_hook_count == 1)
sgs.ai_use_revises.helper_hook_owner = nil
sgs.ai_used_revises.helper_hook_owner = nil
-- A complete known Slash remains selectable beside an unknown custom card or
-- incomplete conversion enumeration. Unknown-only requests must never pass.
local mixed = hook_only_request(true)
mixed.world_view.self.skills = {}
local mixed_slash = card(61, "slash", "Slash", {"Slash", "BasicCard", "Card"})
mixed_slash.target_fixed = false
mixed.world_view.hand_cards[1] = mixed_slash
mixed.world_view.players = {player("enemy")}
mixed.world_view.mode_policy = {managed = true, relations = {viewer = {enemy = "enemy"}}}
mixed.card_candidates[1].target_fixed = false
mixed.card_candidates[1].feasible_with_no_target = false
mixed.card_candidates[1].legal_targets = {"enemy"}
mixed.card_candidates[1].target_combinations = {{"enemy"}}
local mixed_answer = ai_decide(mixed)
assert(mixed_answer and mixed_answer.kind == "use_card" and mixed_answer.card_id == 61)
mixed.conversions_enumerated = false
mixed_answer = ai_decide(mixed)
assert(mixed_answer and mixed_answer.card_id == 61)
local only_unknown = hook_only_request()
only_unknown.conversions_enumerated = false
assert(ai_decide(only_unknown) == nil)
-- Replaced proposals are checked against the final card's availability and
-- limitations, and carry its ticket rather than the original candidate ticket.
local replacement_used = 0
sgs.ai_used_revises.helper_hook_owner = function() replacement_used = replacement_used + 1 end
sgs.ai_use_revises.helper_hook_owner = function(self, offered, use)
    use.card = self.player:getHandcards()[2]
    use.candidate_id = 999 -- A callback cannot reuse or invent the ticket.
    return true
end
for _, unavailable in ipairs({"unavailable", "limited"}) do
    local req, offered = hook_only_request(true)
    req.card_candidates[2].candidate_id = 962
    req.card_candidates[2].available = unavailable ~= "unavailable"
    req.card_candidates[2].limited = unavailable == "limited"
    replacement_used = 0
    local replacement_ai = assert(SmartAIView.new(req))
    local signal, status = replacement_ai:tryUseCard(offered)
    assert(status == "unsupported" and AIUnsupported.is(signal) and replacement_used == 0)
    ai_coverage.clearUncovered()
    assert(ai_decide(req) == nil and replacement_used == 0)
    assert(#ai_coverage.uncovered() == 1)
end
local replacement_request, replacement_card = hook_only_request(true)
replacement_request.card_candidates[2].candidate_id = 962
replacement_used = 0
local replacement_plan, replacement_status = assert(SmartAIView.new(replacement_request)):tryUseCard(replacement_card)
assert(replacement_status == "planned" and replacement_plan.card:getId() == 62)
assert(replacement_plan.candidate_id == 962 and replacement_used == 1)
replacement_used = 0
local replacement_answer = ai_decide(replacement_request)
assert(replacement_answer.card_id == 62 and replacement_answer.candidate_id == 962 and replacement_used == 1)

-- A conversion replacement owns a conversion ticket; no physical candidate ID
-- from the originally considered card is retained in the normalized answer.
local conversion_request = hook_only_request()
conversion_request.card_conversions = {{conversion_id = 771, name = "slash", class_name = "Slash",
    available = true, target_fixed = true, complete_coverage = true,
    feasible_with_no_target = true, legal_targets = {}, target_combinations = {{}},
    cost_count = 0, subcards = {}, suit = 0, number = 0, activation_skill = "helper_hook_owner",
    activation_instance = 1, activation_owner = "viewer", kind_of_names = {"Slash", "BasicCard", "Card"}}}
sgs.ai_use_revises.helper_hook_owner = function(self, offered, use)
    use.card = self:getConversion(771)
    use.candidate_id = 999
    return true
end
local conversion_answer = ai_decide(conversion_request)
assert(conversion_answer and conversion_answer.card_spec.conversion_id == 771)
assert(conversion_answer.candidate_id == nil and conversion_answer.card_id == nil)
local forged_conversion = {}
for key, value in pairs(conversion_request.card_conversions[1]) do forged_conversion[key] = value end
forged_conversion.subcards = {61} -- The real fixed ticket has no subcards.
sgs.ai_use_revises.helper_hook_owner = function(self, offered, use)
    use.card = ConversionView.new(forged_conversion)
    return true
end
replacement_used = 0
local forged_ai = assert(SmartAIView.new(conversion_request))
local forged_plan, forged_status = forged_ai:tryUseCard(forged_ai.player:getHandcards()[1])
assert(forged_status == "unsupported" and AIUnsupported.is(forged_plan) and replacement_used == 0)
forged_conversion.subcards = {}
forged_conversion.name = "peach"
forged_plan, forged_status = forged_ai:tryUseCard(forged_ai.player:getHandcards()[1])
assert(forged_status == "unsupported" and replacement_used == 0)

-- Record discovered gaps only on a successful partial decision. Failed requests
-- retain exactly the bootstrap's one NotCovered record.
sgs.ai_use_revises.helper_hook_owner = function(self, offered, use) use.card = offered return true end
local partial = hook_only_request()
partial.conversions_enumerated = false
ai_coverage.clearUncovered()
assert(ai_decide(partial).card_id == 61)
local gaps = ai_coverage.uncovered()
assert(#gaps == 1 and gaps[1].key == "conversion")
local unknown_skill_request = hook_only_request()
unknown_skill_request.skill_actions = {{activation_skill = "helper_unimplemented_skill", activation_instance = 9,
    activation_owner = "viewer", source_skill = "helper_unimplemented_skill", source_instance = 9, source_owner = "viewer"}}
ai_coverage.clearUncovered()
assert(ai_decide(unknown_skill_request).card_id == 61)
gaps = ai_coverage.uncovered()
assert(#gaps == 1 and gaps[1].key == "helper_unimplemented_skill")
sgs.ai_use_revises.helper_hook_owner = nil
sgs.ai_used_revises.helper_hook_owner = nil
ai_coverage.clearUncovered()
assert(ai_decide(hook_only_request()) == nil)
assert(#ai_coverage.uncovered() == 1)
-- Known unavailable conversions are not unknown actions. Their absent cost and
-- target metadata is irrelevant because authority has already excluded their use.
local inactive = hook_only_request()
inactive.world_view.hand_cards = {}
inactive.card_candidates = {}
inactive.card_conversions = {{conversion_id = 772, name = "slash", class_name = "Slash", available = false}}
ai_coverage.clearUncovered()
assert(ai_decide(inactive).kind == "pass")
assert(#ai_coverage.uncovered() == 0)
return true
