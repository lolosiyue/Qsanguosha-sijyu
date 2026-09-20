-- ResponseUse common planner contract.  This fixture is source-only: it supplies
-- authority rows and verifies physical cards, conversion tickets, target planning,
-- compulsory patterns, and unsupported coverage boundaries.
local function player(name, fields)
    local value = {object_name = name, alive = true, dead = false, hp = 4,
        max_hp = 4, handcard_count = 0, wounded = false, hujia = 0,
        max_cards = 4, attack_range = 1, hand_visible = true, equips = {},
        judging_area = {}, known_cards = {}, skills = {}, public_marks = {}}
    for key, item in pairs(fields or {}) do value[key] = item end
    return value
end

local function card(id, name, class_name)
    return {id = id, effective_id = id, name = name, class_name = class_name,
        kind_of = {class_name, "BasicCard", "Card"}, suit = 0, number = 7}
end

local function candidate(id, rows, targets)
    return {card_id = id, candidate_id = id, available = true, limited = false,
        jilei = false, target_fixed = #rows == 1 and #rows[1] == 0,
        feasible_with_no_target = #rows == 1 and #rows[1] == 0,
        complete_coverage = true, legal_targets = targets,
        target_combinations = rows}
end

local function request(pattern, hand_cards, candidates, conversions)
    -- Every native conversion row includes the produced card's suit and number.
    for _, conversion in ipairs(conversions or {}) do
        if conversion.suit == nil then conversion.suit = sgs.Card_NoSuit end
        if conversion.number == nil then conversion.number = 0 end
    end
    local world = {mode_id = "response-use-contract", revision = 1,
        current_player = "viewer", self = player("viewer", {handcard_count = #hand_cards}),
        players = {player("enemy", {hp = 1, handcard_count = 1}),
            player("friend", {hp = 3, handcard_count = 1})},
        player_order = {"viewer", "enemy", "friend"},
        alive_player_order = {"viewer", "enemy", "friend"},
        hand_cards = hand_cards, discard_pile = {},
        mode_policy = {managed = true, objectives = {enemy = 5, friend = -2},
            relations = {viewer = {enemy = "enemy", friend = "friend"}}}}
    return {viewer = "viewer", kind = "use_card", reason = 0x12,
        pattern = pattern, prompt = "response", handling_method = sgs.Card_MethodUse,
        world_view = world, card_candidates = candidates,
        card_conversions = conversions or {}, conversions_enumerated = true}
end

-- An unhandled skill probe must not consume its viewer's ordinary physical card.
local skill_probe = request("slash", {card(9, "slash", "Slash")},
    {candidate(9, {{"enemy"}}, {"enemy"})})
skill_probe.skill_action = {activation_skill = "unhandled", activation_instance = 1,
    source_skill = "unhandled", source_instance = 1}
assert(ai_decide(skill_probe) == nil,
    "an unrelated physical card answered an unhandled skill probe")

local slash_request = request("slash", {card(1, "slash", "Slash")},
    {candidate(1, {{"enemy"}}, {"enemy"})})
local slash_result = assert(ai_decide(slash_request))
assert(slash_result.kind == "use_card" and slash_result.card_id == 1,
    "physical Slash response did not return its offered card")
assert(slash_result.targets and slash_result.targets[1] == "enemy",
    "ResponseUse Slash did not use the shared target planner")

-- A compulsory pattern keeps the raw registry key semantics, but generic planning
-- still strips the marker for the card-family handler.
local compulsory = request("slash!", {card(2, "slash", "Slash")},
    {candidate(2, {{"enemy"}}, {"enemy"})})
local compulsory_result = assert(ai_decide(compulsory))
assert(compulsory_result.card_id == 2, "compulsory Slash lost its response answer")

-- Peach may answer only a response-use Peach request while the viewer is wounded.
local peach = request("peach", {card(3, "peach", "Peach")},
    {candidate(3, {{}}, {})})
peach.world_view.self.hp, peach.world_view.self.wounded = 2, true
local peach_result = assert(ai_decide(peach))
assert(peach_result.card_id == 3 and peach_result.targets == nil,
    "wounded self Peach response was not selected")
local healthy = request("peach", {card(4, "peach", "Peach")},
    {candidate(4, {{}}, {})})
assert(ai_decide(healthy) == nil, "healthy Peach was treated as a response answer")

-- A converted Slash is returned as its authority-owned conversion ticket and keeps
-- its target combination.  The AI never fabricates a skill/card specification.
local converted = request("slash", {card(10, "jink", "Jink")}, {}, {
    {conversion_id = 7, name = "slash", class_name = "Slash", available = true,
        suit = sgs.Card_NoSuit, number = 0,
        activation_owner = "viewer", activation_skill = "convert", activation_instance = 2,
        source_owner = "viewer", source_skill = "convert", source_instance = 2,
        subcards = {10}, cost_count = 0, eligible_subcards = {},
        complete_coverage = true, target_fixed = false,
        feasible_with_no_target = false, legal_targets = {"enemy"},
        target_combinations = {{"enemy"}}}
})
local converted_result = assert(ai_decide(converted))
assert(converted_result.card_spec and converted_result.card_spec.conversion_id == 7,
    "converted response did not preserve the authority ticket")
assert(converted_result.targets and converted_result.targets[1] == "enemy",
    "converted Slash response lost its target")
converted.skill_action = {activation_owner = "viewer", activation_skill = "convert",
    activation_instance = 2, source_owner = "other", source_skill = "convert", source_instance = 2}
assert(ai_decide(converted) == nil, "a borrowed source owner mismatch answered the skill probe")
converted.skill_action.source_owner = "viewer"
assert(ai_decide(converted).card_spec.conversion_id == 7,
    "a fully matching activation/source identity lost its conversion")

-- Partial conversion enumeration is never interpreted as an empty set.
local incomplete = request("slash", {card(5, "slash", "Slash")},
    {candidate(5, {{"enemy"}}, {"enemy"})})
incomplete.conversions_enumerated = false
ai_coverage.clearUncovered()
local incomplete_result = assert(ai_decide(incomplete),
    "known legal physical response was blocked by unknown conversions")
assert(incomplete_result.card_id == 5,
    "known legal physical response did not win over unknown conversions")
local incomplete_uncovered = ai_coverage.uncovered()
assert(#incomplete_uncovered == 1 and incomplete_uncovered[1].key == "conversion",
    "partial conversion coverage was not audited")

-- An unknown target row does not erase a separate known legal Slash action.
local mixed_targets = request("slash", {card(40, "slash", "Slash"),
    card(41, "slash", "Slash")}, {
    candidate(40, {{"enemy"}}, {"enemy"}), candidate(41, {{"enemy"}}, {"enemy"})})
mixed_targets.card_candidates[1].complete_coverage = false
ai_coverage.clearUncovered()
local mixed_result = assert(ai_decide(mixed_targets))
assert(mixed_result.card_id == 41, "known Slash was hidden by unknown target coverage")
assert(#ai_coverage.uncovered() >= 1, "unknown target coverage was not recorded")

-- A broken conversion-cost row is unknown, while the known physical Slash remains usable.
local broken_cost = request("slash", {card(42, "slash", "Slash")},
    {candidate(42, {{"enemy"}}, {"enemy"})}, {
    {conversion_id=99, name="slash", class_name="Slash", available=true,
        activation_owner="viewer", activation_skill="broken", activation_instance=1,
        source_owner="viewer", source_skill="broken", source_instance=1,
        cost_count=1, eligible_subcards={999}, subcards={}, complete_coverage=true,
        target_fixed=false, feasible_with_no_target=false,
        legal_targets={"enemy"}, target_combinations={{"enemy"}}}})
ai_coverage.clearUncovered()
local broken_result = assert(ai_decide(broken_cost))
assert(broken_result.card_id == 42, "known Slash was hidden by broken conversion cost")
assert(#ai_coverage.uncovered() >= 1, "broken conversion cost was not recorded")

-- If the only offered candidate is unknown and no known action can answer, remain NotCovered.
local only_unknown = request("slash", {card(43, "slash", "Slash")},
    {candidate(43, {{"enemy"}}, {"enemy"})})
only_unknown.card_candidates[1].complete_coverage = false
ai_coverage.clearUncovered()
assert(ai_decide(only_unknown) == nil, "only unknown response candidate became a pass")
assert(#ai_coverage.uncovered() >= 1, "only unknown response candidate was not audited")

-- A known available response card without a registered family strategy is
-- unsupported; it must not become pass or an invented card string.
local unknown = request("mystery", {card(6, "mystery", "MysteryResponse")},
    {candidate(6, {{}}, {})})
ai_coverage.clearUncovered()
assert(ai_decide(unknown) == nil, "unknown response family was answered")
local uncovered = ai_coverage.uncovered()
assert(#uncovered == 1 and uncovered[1].kind == "use_card",
    "unknown response family was not audited as uncovered")

-- Extension authors can add a class-keyed response strategy without changing the
-- dispatcher or weakening the authority candidate/ticket checks.
ai_register_response_use_handler("ContractResponse", function(_, response_card)
    local plan = AIUsePlan.new()
    plan.card = response_card
    return plan
end)
local custom = request("contractresponse", {card(8, "contract_response", "ContractResponse")},
    {candidate(8, {{}}, {})})
local custom_result = assert(ai_decide(custom))
assert(custom_result.card_id == 8, "class-keyed response strategy was not dispatched")

-- Generic matching follows authority availability, including expression patterns.
local expression = request("Slash|red|.|hand", {card(20, "fire_slash", "FireSlash")},
    {candidate(20, {{"enemy"}}, {"enemy"})})
expression.world_view.hand_cards[1].kind_of = {"FireSlash", "Slash", "BasicCard", "Card"}
assert(ai_decide(expression).card_id == 20)

-- Competing copies consume the cheaper keep value, regardless of input order.
local old_slash_keep = ai_keep_value.Slash
ai_keep_value.Slash = function(_, value) return value:getEffectiveId() == 21 and 8 or 1 end
local competing = request("slash", {card(21, "slash", "Slash"), card(22, "slash", "Slash")},
    {candidate(21, {{"enemy"}}, {"enemy"}), candidate(22, {{"enemy"}}, {"enemy"})})
assert(ai_decide(competing).card_id == 22)
ai_keep_value.Slash = old_slash_keep

-- ExNihilo/Duel use response-native planning without changing the request to Play.
local draw = request("ex_nihilo", {card(23, "ex_nihilo", "ExNihilo")},
    {candidate(23, {{}}, {})})
assert(ai_decide(draw).card_id == 23 and draw.reason == 0x12)
local duel = request("duel", {card(24, "duel", "Duel")},
    {candidate(24, {{"enemy"}}, {"enemy"})})
assert(ai_decide(duel).targets[1] == "enemy")

-- The legacy alias ABI retains priority over generic matching.
sgs.ai_skill_use["contract-explicit"] = function(_, prompt, method, pattern)
    assert(prompt == "response" and method == sgs.Card_MethodUse and pattern == "contract-explicit")
    return "."
end
local explicit = request("contract-explicit", {card(25, "slash", "Slash")},
    {candidate(25, {{"enemy"}}, {"enemy"})})
local declined = ai_decide(explicit)
assert(declined == nil or declined.kind == "pass")

-- Independent fixed-count conversion costs share the play planner's binding rule.
local parameterized = request("slash", {card(30, "peach", "Peach"),
    card(31, "jink", "Jink"), card(32, "slash", "Slash")}, {}, {
    {conversion_id=8, name="slash", class_name="Slash", available=true,
        activation_owner="viewer", activation_skill="pair", activation_instance=1,
        source_owner="viewer", source_skill="pair", source_instance=1,
        cost_count=2, eligible_subcards={30,31,32}, subcards={},
        complete_coverage=true, target_fixed=false, feasible_with_no_target=false,
        legal_targets={"enemy"}, target_combinations={{"enemy"}}}
})
local bound = assert(ai_decide(parameterized))
assert(bound.card_spec.conversion_id == 8 and #bound.card_spec.subcards == 2)
assert(bound.card_spec.subcards[1] == 32 and bound.card_spec.subcards[2] == 31,
    "response conversion did not preserve the lowest-keep independent costs")

-- A known empty affected roster is a declined use, not missing projection.
local global = request("savage_assault", {card(33, "savage_assault", "SavageAssault")},
    {candidate(33, {{}}, {})})
global.card_candidates[1].affected_targets = {}
ai_coverage.clearUncovered()
assert(ai_decide(global) == nil)
assert(#ai_coverage.uncovered() == 0)
global.card_candidates[1].affected_targets = nil
assert(ai_decide(global) == nil and #ai_coverage.uncovered() == 1)

-- Missing hand projection must not hide a separately known equipment cost.
local equip_cost = request("slash", {}, {}, {{conversion_id=81, name="slash", class_name="Slash",
    available=true, activation_owner="viewer", activation_skill="equip_convert", activation_instance=1,
    source_owner="viewer", source_skill="equip_convert", source_instance=1,
    subcards={82}, cost_count=0, complete_coverage=true, target_fixed=false,
    feasible_with_no_target=false, legal_targets={"enemy"}, target_combinations={{"enemy"}}}})
equip_cost.world_view.hand_cards = nil
equip_cost.world_view.self.hand_visible = false
equip_cost.world_view.self.equips = {card(82, "crossbow", "Crossbow")}
local equip_answer = ai_decide(equip_cost)
assert(equip_answer and equip_answer.card_spec.conversion_id == 81)
assert(equip_answer.card_spec.subcards[1] == 82)

return true
