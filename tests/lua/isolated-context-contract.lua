-- Snapshot indexes and cross-request planning intent contract.
-- Loaded after isolated-bootstrap.lua and isolated-facades.lua by the Lua
-- contract harness; this file is intentionally source-only in this checkpoint.
local function player(name, alive)
    return {object_name = name, alive = alive, dead = not alive, skills = {},
        equips = {}, judging_area = {}, public_marks = {}, known_cards = {}}
end

local function request(revision, decision_id)
    return {viewer = "self", decision_id = decision_id or "decision-" .. revision,
        state_revision = revision, world_view = {
        self = player("self", true), players = {player("dead", false), player("ally", true)},
        player_order = {"self", "dead", "ally"}, alive_player_order = {"self", "ally"},
        hand_cards = {{id = 7, effective_id = 7, name = "slash"}}
    }, card_candidates = {{card_id = 7, candidate_id = 41, target_fixed = false,
            feasible_with_no_target = false, complete_coverage = true,
            legal_targets = {"ally"}, target_combinations = {{"ally"}}}},
        card_conversions = {
            {conversion_id = 9, name = "slash", subcards = {7}, cost_count = 1,
                activation_owner = "self", activation_skill = "foo", activation_instance = 2,
                source_owner = "self", source_skill = "foo", source_instance = 2},
            {conversion_id = 10, name = "slash", subcards = {}, cost_count = 2,
                eligible_subcards = {7, 8, 9}, activation_owner = "self",
                activation_skill = "foo", activation_instance = 2,
                source_owner = "self", source_skill = "foo", source_instance = 2}},
        skill_actions = {{activation_owner = "self", activation_skill = "foo",
            activation_instance = 2, source_owner = "self", source_skill = "foo",
            source_instance = 2}},
        options = {cards = {{id = 12, effective_id = 12, name = "shown"}}}}
end

local first = request("r1")
local ai = assert(SmartAIView.new(first))
assert(ai.room:findPlayerByObjectName("ally") ~= nil)
assert(ai.room:findPlayerByObjectName("dead") == nil)
assert(ai.room:findPlayerByObjectName("dead", true) ~= nil)
assert(ai:getCardCandidate(7):getCandidateId() == 41)
-- A partial projection must not invent a ticket or remove its query method.
local unticketed = request("unticketed")
unticketed.card_candidates[1].candidate_id = nil
assert(SmartAIView.new(unticketed):getCardCandidate(7):getCandidateId() == nil)
assert(ai:getConversion(9):getName() == "slash")
assert(ai:getSkillAction("foo", 2):getActivationInstanceId() == 2)
assert(ai:getChoiceCard(12):getId() == 12)
assert(AIValue.isList(ai:getSkillActions()) and #ai:getSkillActions() == 1)

-- Absent or malformed projections are unknown; a present empty array is known empty.
local missing = request("missing")
missing.skill_actions = nil
local missing_ai = assert(SmartAIView.new(missing))
assert(missing_ai:getSkillActions() == nil)
assert(missing_ai:getSkillAction("foo") == nil)
assert(missing_ai:getSkillAction("foo", 2) == nil)
local malformed = request("malformed")
malformed.skill_actions = {unexpected = true}
local malformed_ai = assert(SmartAIView.new(malformed))
assert(malformed_ai:getSkillActions() == nil)
assert(malformed_ai:getSkillAction("foo", 2) == nil)
local empty = request("empty")
empty.skill_actions = {}
local empty_ai = assert(SmartAIView.new(empty))
assert(AIValue.isList(empty_ai:getSkillActions()) and #empty_ai:getSkillActions() == 0)
assert(empty_ai:getSkillAction("foo", 2) == nil)

-- Ordinary returned values and lists must not alias later indexed reads.
local candidates = ai:getCardCandidates()
candidates[1]._view.card_id = 99
candidates[1]._view.legal_targets[1] = "polluted"
candidates:removeOne(candidates[1])
assert(#ai:getCardCandidates() == 1)
assert(ai:getCardCandidate(7):getCardId() == 7)
assert(ai:getCardCandidate(7):getLegalTargets()[1] == "ally")
assert(ai:getCardCandidate(99) == nil)
local conversion = ai:getConversions()[1]
conversion._view.name = "polluted"
conversion._view.subcards[1] = 99
assert(ai:getConversion(9):getName() == "slash")
assert(ai:getConversion(9):getSubcards()[1] == 7)
local action = ai:getSkillActions()[1]
action._view.activation_instance = 99
assert(ai:getSkillAction("foo", 2):getActivationInstanceId() == 2)
assert(ai:getSkillAction("foo", 99) == nil)
local choice = ai:getChoiceCards()[1]
choice._view.id = 99
assert(ai:getChoiceCard(12):getId() == 12 and ai:getChoiceCard(99) == nil)

assert(ai:planIntent("target", {candidate_id = 41, targets = {"ally"}}))
local target, status = ai:getPlannedIntent("target")
assert(status == "fresh" and target.candidate_id == 41)
target.targets[1] = "polluted"
assert(ai:getPlannedIntent("target").targets[1] == "ally")
local valid_target, valid_status = ai:revalidatePlannedIntent("target")
assert(valid_status == "fresh" and valid_target.targets[1] == "ally")

-- Membership alone cannot prove a complete legal combination or a finishable prefix.
for _, targets in ipairs({{}, {"ally", "ally"}, {"dead"}}) do
    assert(ai:planIntent("target", {candidate_id = 41, targets = targets}))
    local value, result = ai:revalidatePlannedIntent("target")
    assert(value == nil and result == "invalidated")
end
local incomplete = request("incomplete")
incomplete.card_candidates[1].complete_coverage = false
local incomplete_ai = assert(SmartAIView.new(incomplete))
local value, result = incomplete_ai:validatePlannedIntent("target",
    {candidate_id = 41, targets = {"ally"}})
assert(value == nil and result == "unknown")

-- Fixed costs compare the projected subcards; parameterized costs enforce count,
-- eligibility, and uniqueness without requiring a manual validity override.
assert(ai:planIntent("cost", {conversion_id = 9, subcards = {7}}))
local cost, cost_status = ai:revalidatePlannedIntent("cost")
assert(cost_status == "fresh" and cost.subcards[1] == 7)
assert(ai:planIntent("cost", {conversion_id = 9, subcards = {8}}))
cost, cost_status = ai:revalidatePlannedIntent("cost")
assert(cost == nil and cost_status == "invalidated")
assert(ai:planIntent("cost", {conversion_id = 10, subcards = {7, 9}}))
cost, cost_status = ai:revalidatePlannedIntent("cost")
assert(cost_status == "fresh" and #cost.subcards == 2)
for _, ids in ipairs({{7}, {7, 7}, {7, 99}}) do
    assert(ai:planIntent("cost", {conversion_id = 10, subcards = ids}))
    cost, cost_status = ai:revalidatePlannedIntent("cost")
    assert(cost == nil and cost_status == "invalidated")
end

local unknown_cost = request("unknown-cost")
unknown_cost.card_conversions[2].eligible_subcards = nil
local unknown_cost_ai = assert(SmartAIView.new(unknown_cost))
value, result = unknown_cost_ai:validatePlannedIntent("cost",
    {conversion_id = 10, subcards = {7, 9}})
assert(value == nil and result == "unknown")

-- The stale result is consumed once. Reused tickets in a different decision
-- remain stale even when no authoritative gameplay mutation changed revision.
assert(ai:planIntent("target", {candidate_id = 41, targets = {"ally"}}))
local next_ai = assert(SmartAIView.new(request("r2")))
local stale, stale_status = next_ai:getPlannedIntent("target")
assert(stale == nil and stale_status == "stale")
assert(select(2, next_ai:getPlannedIntent("target")) == "missing")
assert(ai:planIntent("target", {candidate_id = 41, targets = {"ally"}}))
local other_request = request("r1", "different-decision")
other_request.card_candidates[1].card_id = 8
local other_ai = assert(SmartAIView.new(other_request))
stale, stale_status = other_ai:getPlannedIntent("target")
assert(stale == nil and stale_status == "stale")
assert(not pcall(function() ai:planIntent("target", ai.player) end))

-- Registration coverage is separate from observed outcomes, and the counters
-- returned to a caller cannot mutate subsequent decision statistics.
ai_coverage.clearOutcomes()
ai_skill_choice.context_pass = function() return {kind = "pass"} end
ai_skill_choice.context_unknown = function() ai_unsupported("fixture", "context") end
ai_skill_choice.context_error = function() error("fixture error") end
for _, reason in ipairs({"context_pass", "context_unknown", "context_error"}) do
    local counted = request("counts")
    counted.kind, counted.options = "choice", {reason = reason}
    local ok = pcall(ai_decide, counted)
    assert(ok == (reason ~= "context_error"))
end
local counts = ai_coverage.outcomes()
assert(counts.choice.pass == 1 and counts.choice.unsupported == 1 and counts.choice.error == 1)
counts.choice.pass = 500
assert(ai_coverage.outcomes().choice.pass == 1)

-- Private phase/flag facts and typed decision data must preserve absence and false.
local contextual = request("context")
contextual.world_view.current_player = "ally"
contextual.world_view.self.flags = {"private_fixture"}
contextual.world_view.self.skipped_phases = {[3] = false, [4] = true}
contextual.world_view.self.active_armor_name = ""
contextual.options.context = {
    judge = {who = "ally", reason = "fixture", good = false, outcomes_complete = true,
        outcome_by_id = {["7"] = false}, card = {name = "slash", class_name = "Slash"}},
    damage = {from = "ally", to = "self", amount = 2},
    effect = {from = "ally", to = "self"}}
local contextual_ai = assert(SmartAIView.new(contextual))
assert(contextual_ai.player:hasFlag("private_fixture") == true)
assert(contextual_ai.player:hasFlag("absent") == false)
assert(contextual_ai.player:hasFlag("CurrentPlayer") == false)
local contextual_ally = contextual_ai.room:findPlayerByObjectName("ally")
assert(contextual_ally:hasFlag("CurrentPlayer") == true)
assert(contextual_ally:hasFlag("private_fixture") == nil)
assert(contextual_ai.player:isSkipped(3) == false and contextual_ai.player:isSkipped(4) == true)
assert(contextual_ally:isSkipped(4) == nil)
assert(contextual_ai.player:hasArmorEffect("SilverLion") == false)
assert(contextual_ally:hasArmorEffect("SilverLion") == nil)
local data = contextual_ai:getDecisionData()
assert(data:toJudge().who:objectName() == "ally")
assert(data:toJudge():isGood() == false)
assert(data:toJudge():isGood(CardView.new({effective_id = 7})) == false)
assert(data:toJudge():isBad(CardView.new({effective_id = 7})) == true)
assert(data:toDamage().damage == 2 and data:toDamage().to:objectName() == "self")
assert(data:toCardEffect().from:objectName() == "ally")
local judge_copy = data:toJudge()
judge_copy.outcome_by_id["7"] = true
assert(contextual_ai:getJudge():isGood(CardView.new({effective_id = 7})) == false)
assert(CardView.new({suit = sgs.Card_Heart}):getSuitString() == "heart")
assert(CardView.new({suit = sgs.Card_NoSuit}):getSuitString() == "no_suit")
assert(CardView.new({}):getSuitString() == nil)
return true
