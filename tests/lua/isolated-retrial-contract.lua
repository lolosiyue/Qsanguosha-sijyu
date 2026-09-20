-- Real sandbox/facade contract: all production modules are already loaded.
local function player(name)
    return {object_name=name, alive=true, dead=false, hp=3, max_hp=4,
        handcard_count=2, max_cards=3, chained=false, active_armor_name="",
        hujia=0, attack_range=1, equips={}, equip_slots={}, judging_area={},
        known_cards={}, skills={}, public_marks={}, hand_visible=true}
end
local function card(id, class_name)
    return {id=id, effective_id=id, name=string.lower(class_name), class_name=class_name,
        kind_of={class_name, "BasicCard", "Card"}, suit=0, number=7}
end
local cards = {card(1, "Slash"), card(2, "Jink"), card(3, "Slash")}
local world = {mode_id="retrial-contract", revision=1, self=player("ally"),
    players={player("foe"), player("neutral")},
    player_order={"ally", "foe", "neutral"}, alive_player_order={"ally", "foe", "neutral"},
    hand_cards=cards, mode_policy={managed=true, relations={
        ally={ally="friend", foe="enemy", neutral="neutral"}}, objectives={foe=5}}}
local request = {viewer="ally", kind="use_card", world_view=world,
    options={context={judge={reason="eight_diagram", who="ally", good=false,
        negative=true, card=card(1, "Slash"), outcomes_complete=true,
        outcome_by_id={["1"]=true, ["2"]=false, ["3"]=true}}}}}
local ai = assert(SmartAIView.new(request))
local ally, foe = ai.player, ai.room:findPlayerByObjectName("foe")
-- Selection valuation is varied through the real pure-value extension registries.
local old_slash, old_jink = ai_keep_value.Slash, ai_keep_value.Jink
ai_keep_value.Slash = function(_, value) return value:getEffectiveId() == 1 and 5 or 2 end
ai_keep_value.Jink = 1
local no_discard_preference = function() return false end
ai.doDisCard = no_discard_preference
local dto = ai:getDecisionData():toJudge()
assert(AIValue.isPlayer(dto.who) and AIValue.isCard(dto.card))
assert(dto:isGood(CardView.new(cards[2])) == false)
assert(dto:isBad(CardView.new(cards[2])) == true)
assert(dto.good == false and dto.negative == true)
local judge = {reason="eight_diagram", who="ally", good=false,
    outcome_by_id={["1"]=true, ["2"]=false, ["3"]=true}, outcomes_complete=true}
assert(ai:needRetrial(judge) == true)
assert(ai:getRetrialCardId(cards, judge) == 3)
ai.doDisCard = function(_, _, id) return id == 1 end
assert(ai:getRetrialCardId(cards, judge) == 1)
ai.doDisCard = no_discard_preference
judge.good = true
assert(ai:needRetrial(judge) == false)
judge.who = "foe"
assert(ai:needRetrial(judge) == true)
assert(ai:getRetrialCardId(cards, judge) == 2)
judge.good = false
assert(ai:needRetrial(judge) == false)
judge.who = "neutral"
assert(ai:needRetrial(judge) == nil)
assert(ai:getRetrialCardId(cards, judge) == nil)

-- Ordinary Lightning/Indulgence/Luoshen retain the shared baseline decisions.
judge.who, judge.good = "ally", false
for _, reason in ipairs({"lightning", "indulgence", "luoshen"}) do
    judge.reason = reason
    assert(ai:needRetrial(judge) == true)
end
ally.hasArmorEffect = function(_, name) return name == "SilverLion" end
judge.reason = "lightning"
assert(ai:needRetrial(judge) == false)
ally.hasArmorEffect = function() return false end
ally.isChained = function() return true end
assert(ai:needRetrial(judge) == nil)
ally.isChained = function() return false end

-- Boolean false overrides aliases; callback false is also a real answer.
judge.reason, judge.who = "custom", "ally"
sgs.ai_need_retrial.custom = false
sgs.ai_need_retrial_func.custom = function() error("wrong precedence") end
assert(ai:needRetrial(judge) == false)
sgs.ai_need_retrial.custom = function(_, dto, good, who)
    assert(dto.who == ally and who == ally and good == false)
    return false
end
assert(ai:needRetrial(judge) == false)
sgs.ai_need_retrial.custom, sgs.ai_need_retrial_func.custom = nil, nil

-- Normal favorable retrials beat cheaper exchange-only cards. An exchange must
-- preserve the current result if there is no favorable candidate.
judge.good = false
assert(ai:getRetrialCardId(cards, judge, false, true) == 3)
assert(ai:getRetrialCardId({cards[2]}, judge, false, true) == 2)
judge.good = true
assert(ai:getRetrialCardId({cards[2]}, judge, false, true) == -1)
assert(ai:getRetrialCardId({cards[2]}, judge, false, false) == -1)
judge.outcomes_complete = false
assert(ai:getRetrialCardId(cards, judge) == nil)
-- Candidate hooks preserve false; stored legacy booleans are not candidate maps.
sgs.ai_judgeGood.custom = function() return false end
assert(ai:getRetrialCardId(cards, judge, false, false) == -1)
sgs.ai_judgeGood.custom = nil
judge.outcomes_complete = true

-- Self-owned Peach needs an explicit protection decision; borrowed cards do not.
local peach = card(3, "Peach")
assert(ai:getRetrialCardId({peach}, judge) == nil)
assert(ai:getRetrialCardId({peach, cards[1]}, judge) == 1)
judge.peach_protected = true
assert(ai:getRetrialCardId({peach}, judge) == -1)
assert(ai:getRetrialCardId({peach}, judge, false) == 3)
judge.peach_protected = false
assert(ai:getRetrialCardId({peach}, judge) == 3)

-- Public lightning requires authority-projected filtered reserve cards.
ally.getJudgingArea = function() return {CardView.new({id=90, effective_id=90, name="lightning", class_name="Lightning", kind_of={"Lightning", "DelayedTrick", "Card"}})} end
assert(ai:getRetrialCardId(cards, judge) == nil)
judge.lightning_reserve_complete, judge.lightning_candidate_ids = true, {["1"]=true, ["3"]=true}
judge.lightning_holder_count = 1
assert(ai:getRetrialCardId(cards, judge) == 1)
ally.getJudgingArea = function() return {} end

-- Final wizard is the last capable actor in projected action order, not max enemy.
assert(ai:getFinalRetrial() == nil)
local projection = {retrial_candidates_complete=true, retrial_candidates={
    {player="foe", can_retrial=true}, {player="ally", can_retrial=true}}}
local final, wizard = ai:getFinalRetrial(nil, projection)
assert(final == 1 and wizard == ally)
projection.retrial_candidates = {}
assert(ai:getFinalRetrial(nil, projection) == 0)

-- Reason-specific hooks may cover complex effects but cannot return unoffered IDs.
sgs.ai_retrial.custom = function() return 99 end
assert(ai:getRetrialCardId(cards, judge) == nil)
sgs.ai_retrial.custom = function() return -1 end
assert(ai:getRetrialCardId(cards, judge) == -1)
sgs.ai_retrial.custom = nil
judge.reason = "beige"
assert(ai:needRetrial(judge) == true and ai:getRetrialCardId(cards, judge) == nil)
ai_keep_value.Slash, ai_keep_value.Jink = old_slash, old_jink
return true
