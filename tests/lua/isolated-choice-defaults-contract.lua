-- Shared non-play defaults use offered authority values and seeded choices;
-- missing optional effect policies never authorize fabricated cards or players.
local function player(name)
    return {object_name=name, alive=true, dead=false, hp=3, max_hp=4,
        handcard_count=0, hand_visible=true, hujia=0, wounded=true,
        max_cards=4, attack_range=1, phase=sgs.Player_NotActive, equips={}, judging_area={},
        skills={}, public_marks={}, known_cards={}}
end

local function request(kind, options)
    return {viewer="viewer", kind=kind, options=options,
        world_view={self=player("viewer"), players={player("other")},
            player_order={"viewer", "other"}, alive_player_order={"viewer", "other"},
            hand_cards={}, mode_policy={managed=true, relations={viewer={other="friend"}},
                objectives={}}}}
end

local random_choice = ai_decide(request("choice", {reason="", choices={"first", "second"}})).answer
assert(random_choice == "first" or random_choice == "second")
local random_suit = ai_decide(request("suit", {reason="", choices={"spade", "heart"},
    default_choice="heart"})).answer
assert(random_suit == "spade" or random_suit == "heart")
assert(ai_decide(request("choice", {reason="", choices={"benghuai", "ordinary"}})).answer == "ordinary")
assert(ai_decide(request("choice", {reason="", choices={"benghuai"}})).answer == "benghuai")
assert(ai_decide(request("choice", {reason="", choices={"first", "second"}, default_choice="second"})).answer == "second")
assert(ai_decide(request("choice", {reason="", choices={"benghuai", "ordinary"}, default_choice="benghuai"})).answer == "ordinary")
assert(ai_decide(request("skill_invoke", {reason="", context={skill_frequency=sgs.Skill_Frequent}})).answer == "yes")
assert(ai_decide(request("skill_invoke", {reason="", context={skill_frequency=-1}})).kind == "pass")
assert(ai_decide(request("skill_invoke", {reason=""})).kind == "pass")
ai_skill_invoke.invoke_priority_contract = function() return true end
assert(ai_decide(request("skill_invoke", {reason="invoke_priority_contract"})).answer == "yes")
assert(ai_decide(request("kingdom", {reason="", choices={"wei", "shu"}})).answer == "wei")
assert(ai_decide(request("general", {reason="", choices={"caocao", "liubei"},
    default_choice="liubei"})).answer == "liubei")
assert(ai_decide(request("trigger_order", {reason="", choices={"a"},
    optional=false})).answer == "a")

-- A reason handler has precedence over the shared default.
ai_skill_choice.priority_contract = function() return "second" end
assert(ai_decide(request("choice", {reason="priority_contract", choices={"first", "second"}})).answer == "second")

-- Mandatory single-player choices use the server's offered-candidate fallback.
local single = ai_decide(request("player_chosen", {reason="unknown", players={"viewer", "other"}}))
assert(single.targets[1] == "viewer" or single.targets[1] == "other")
assert(ai_decide(request("player_chosen", {reason="unknown", players={"other"}, optional=true})).kind == "pass")
assert(ai_decide(request("player_chosen", {reason="unknown", players={}})) == nil)
ai_skill_playerchosen.single_priority_contract = function() return "other" end
assert(ai_decide(request("player_chosen", {reason="single_priority_contract",
    players={"viewer", "other"}, optional=true})).targets[1] == "other")
assert(ai_decide(request("discard", {reason="unknown", card_ids={7}, candidates_complete=false,
    min_count=1, max_count=1, optional=false})) == nil)

-- Guanxing is a pure permutation when the authority supplied the whole set.
local guanxing = ai_decide(request("guanxing", {reason="guanxing", card_ids={1, 2, 3},
    default_choice="1"}))
assert(guanxing.cards[1] == 1 and guanxing.cards[3] == 3 and #guanxing.bottom_cards == 0)

-- Without metadata, an offered permutation remains valid; known utility ranks
-- cards without pretending to know the next beneficiary or a judgment outcome.
local unvalued = ai_decide(request("guanxing", {reason="guanxing", card_ids={1, 2}, default_choice="0"}))
assert(unvalued.cards[1] == 1 and unvalued.cards[2] == 2)
local reorder = request("guanxing", {reason="guanxing", card_ids={7,8}, default_choice="0",
    cards={{id=7, effective_id=7, name="slash", class_name="Slash", kind_of={"Slash", "BasicCard"}, number=1},
        {id=8, effective_id=8, name="peach", class_name="Peach", kind_of={"Peach", "BasicCard"}, number=2}}})
reorder.world_view.current_player = "viewer"
reorder.world_view.self.phase = sgs.Player_Start
local reordered = ai_decide(reorder)
assert(#reordered.cards == 2 and #reordered.bottom_cards == 0 and reordered.cards[1] == 8)
reorder.options.context = {draw_count=1}
local partitioned = ai_decide(reorder)
assert(partitioned.cards[1] == 8 and partitioned.bottom_cards[1] == 7)
reorder.world_view.players[1].judging_area = {{id=10, effective_id=10, name="lightning",
    class_name="Lightning", kind_of={"Lightning", "DelayedTrick"}}}
assert(ai_decide(reorder).cards[1] == 8, "a pending judgment alone does not invent outcome facts")
reorder.world_view.current_player = "other"
reorder.options.context = nil
local generic_order = ai_decide(reorder)
assert(generic_order.cards[1] == 8 and generic_order.cards[2] == 7)
reorder.world_view.players[1].phase = sgs.Player_Start
reorder.world_view.mode_policy.relations.viewer.other = "enemy"
assert(ai_decide(reorder).cards[1] == 7, "a known enemy draw prefers lower generic utility")
reorder.world_view.mode_policy.relations.viewer.other = "friend"
reorder.options.context = {judge={who="other", reason="test", outcomes_complete=true,
    outcome_by_id={["7"]=true, ["8"]=false}}}
assert(ai_decide(reorder).cards[1] == 7, "only authority-projected outcomes reserve a judgment card")
reorder.options.context.judge.outcomes_complete = false
assert(ai_decide(reorder).cards[1] == 8)
reorder.options.default_choice = "1"
assert(ai_decide(reorder).cards[1] == 8 and #ai_decide(reorder).bottom_cards == 0)
local trigger = ai_decide(request("trigger_order", {reason="", choices={"a", "b"}, optional=false}))
assert(trigger.answer == "a" or trigger.answer == "b")
assert(ai_decide(request("trigger_order", {reason="", choices={"a", "b"}, optional=true})).kind == "pass")
ai_skill_triggerorder.trigger_priority_contract = function() return "b" end
assert(ai_decide(request("trigger_order", {reason="trigger_priority_contract",
    choices={"a", "b"}, optional=true})).answer == "b")

-- Yiji first attempts known card-need policy; compulsory ordinary gifts still
-- choose one offered card/recipient, while optional unprofitable gifts decline.
local forced_gift = request("yiji", {reason="", card_ids={7, 8}, players={"other"},
    candidates_complete=true, optional=false, cards=reorder.options.cards})
forced_gift.world_view.mode_policy.relations.viewer.other = "enemy"
local gift = ai_decide(forced_gift)
assert(gift.targets[1] == "other" and #gift.cards == 1 and (gift.cards[1] == 7 or gift.cards[1] == 8))
forced_gift.options.optional = true
assert(ai_decide(forced_gift).kind == "pass")
forced_gift.options.optional = false
forced_gift.options.cards = nil
local unknown_gift = ai_decide(forced_gift)
assert(unknown_gift.targets[1] == "other" and (unknown_gift.cards[1] == 7 or unknown_gift.cards[1] == 8))
ai_skill_askforyiji.gift_priority_contract = function()
    return {kind="answer", cards={8}, targets={"other"}}
end
forced_gift.options.reason, forced_gift.options.optional = "gift_priority_contract", true
assert(ai_decide(forced_gift).cards[1] == 8)

-- A compulsory discard asks for exactly its minimum; an optional empty minimum is pass.
local discard = ai_decide(request("discard", {reason="", card_ids={7, 8},
    cards={{id=7, effective_id=7, name="slash", class_name="Slash", kind_of={"Slash"},
        number=1}, {id=8, effective_id=8, name="peach", class_name="Peach", kind_of={"Peach"},
        number=2}}, candidates_complete=true, min_count=1, max_count=2, optional=false}))
assert(discard.cards[1] ~= nil and #discard.cards == 1)

-- Empty physical response is a deliberate pass only when the authority says the
-- candidate set is complete; missing conversion enumeration stays NotCovered.
local empty_response = request("respond_card", {question="askForCard", reason="",
    card_ids={}, candidates_complete=true, optional=true})
empty_response.pattern = "jink"
empty_response.conversions_enumerated = true
assert(ai_decide(empty_response) ~= nil and ai_decide(empty_response).kind == "pass")
local unknown_response = request("respond_card", {question="askForCard", reason="",
    card_ids={}, candidates_complete=false, optional=true})
unknown_response.pattern = "jink"
unknown_response.conversions_enumerated = false
assert(ai_decide(unknown_response) == nil)
local conversion_response = request("respond_card", {question="askForCard", reason="",
    card_ids={7}, candidates_complete=false, optional=true,
    cards={{id=7, effective_id=7, name="jink", class_name="Jink",
        kind_of={"Jink", "BasicCard"}, number=2}}})
conversion_response.pattern = "jink"
conversion_response.conversions_enumerated = true
conversion_response.card_conversions = {{activation_skill="x"}}
ai_coverage.clearUncovered()
assert(ai_decide(conversion_response).cards[1] == 7)
assert(#ai_coverage.uncovered() == 1)

local virtual_response = request("respond_card", {question="askForCard", reason="",
    card_ids={9}, candidates_complete=true, optional=true,
    cards={{id=9, effective_id=9, virtual_card=true, subcards={11},
        name="jink", class_name="Jink", kind_of={"Jink"}}}})
virtual_response.pattern = "jink"
virtual_response.conversions_enumerated = true
assert(ai_decide(virtual_response) == nil)
local partly_known = request("respond_card", {question="askForCard", reason="",
    card_ids={6, 7}, candidates_complete=false, optional=true,
    cards={{id=7, effective_id=7, name="jink", class_name="Jink", number=2}}})
partly_known.pattern = "jink"
partly_known.conversions_enumerated = false
assert(ai_decide(partly_known).cards[1] == 7)
empty_response.pattern = "jink!"
assert(ai_decide(empty_response) == nil, "a compulsory empty response is not a legal pass")

-- Responses use the existing conversion ticket and actual owned payment cost;
-- the converted card's intrinsic keep value never substitutes for that cost.
local function response_conversion(id, subcards)
    return {conversion_id=id, name="jink", class_name="Jink", kind_of_names={"Jink", "BasicCard"},
        suit=1, number=2, activation_owner="viewer", activation_skill="response_contract",
        activation_instance=3, source_owner="other", source_skill="borrowed_response_contract",
        source_instance=5, activation_quota_available=true, source_quota_available=true,
        cost_count=0, subcards=subcards or {}, available=true, complete_coverage=true,
        target_fixed=true, feasible_with_no_target=true, legal_targets={}, target_combinations={{}}}
end
local function converted_request(conversions, hand)
    local value = request("respond_card", {question="askForCard", reason="",
        card_ids={}, cards={}, candidates_complete=true, optional=true})
    value.pattern, value.conversions_enumerated = "jink", true
    value.card_conversions = conversions
    value.world_view.hand_cards = hand or {}
    value.world_view.self.handcard_count = #(hand or {})
    return value
end
local payment_slash = {id=51, effective_id=51, name="slash", class_name="Slash",
    kind_of={"Slash", "BasicCard"}, number=3, suit=1}
local payment_peach = {id=52, effective_id=52, name="peach", class_name="Peach",
    kind_of={"Peach", "BasicCard"}, number=4, suit=2}
local free_response = converted_request({response_conversion(11)})
local free_answer = ai_decide(free_response)
assert(free_answer.kind == "answer" and free_answer.card_spec.conversion_id == 11)
assert(free_answer.card_id == nil and free_answer.cards == nil and free_answer.targets == nil)
assert(free_answer.card_spec.skill == "response_contract" and #free_answer.card_spec.subcards == 0)
free_response.pattern = "jink!"
assert(ai_decide(free_response).card_spec.conversion_id == 11)

local paid_response = converted_request({response_conversion(12, {51}), response_conversion(13, {52})},
    {payment_slash, payment_peach})
local paid_answer = ai_decide(paid_response)
assert(paid_answer.card_spec.conversion_id == 12 and paid_answer.card_spec.subcards[1] == 51)
paid_response.options.card_ids, paid_response.options.cards = {7}, conversion_response.options.cards
assert(ai_decide(paid_response).card_spec.conversion_id == 12, "cheaper conversion beats physical Jink")
paid_response.card_conversions = {response_conversion(13, {52})}
assert(ai_decide(paid_response).cards[1] == 7, "physical Jink beats an expensive conversion")

local selected_costs = response_conversion(14)
selected_costs.cost_count, selected_costs.eligible_subcards = 2, {52, 51, 53}
local second_slash = {id=53, effective_id=53, name="slash", class_name="Slash",
    kind_of={"Slash", "BasicCard"}, number=5, suit=1}
local selected_response = converted_request({selected_costs}, {payment_slash, payment_peach, second_slash})
local selected_answer = ai_decide(selected_response)
assert(selected_answer.card_spec.conversion_id == 14 and #selected_answer.card_spec.subcards == 2)
assert(selected_answer.card_spec.subcards[1] == 51 and selected_answer.card_spec.subcards[2] == 53)
assert(#selected_response.card_conversions[1].subcards == 0, "binding cannot mutate the authority ticket")

-- An unknown branch must not abort another legal branch, in either order.
local missing_payment = response_conversion(15, {99})
for _, conversions in ipairs({{missing_payment, response_conversion(11)}, {response_conversion(11), missing_payment}}) do
    local partial_response = converted_request(conversions)
    ai_coverage.clearUncovered()
    assert(ai_decide(partial_response).card_spec.conversion_id == 11)
    assert(#ai_coverage.uncovered() >= 1)
end
assert(ai_decide(converted_request({missing_payment})) == nil)
assert(ai_decide(converted_request({response_conversion(16, {51, 51})}, {payment_slash})) == nil)
local missing_quota = response_conversion(17)
missing_quota.activation_quota_available = nil
assert(ai_decide(converted_request({missing_quota, response_conversion(11)})).card_spec.conversion_id == 11)
assert(ai_decide(converted_request({missing_quota})) == nil)
local missing_identity = response_conversion(19)
missing_identity.number = nil
assert(ai_decide(converted_request({missing_identity, response_conversion(11)})).card_spec.conversion_id == 11)
assert(ai_decide(converted_request({missing_identity})) == nil)
local unavailable = response_conversion(18)
unavailable.available = false
assert(ai_decide(converted_request({unavailable})).kind == "pass")
local not_enumerated = converted_request({unavailable})
not_enumerated.conversions_enumerated = false
assert(ai_decide(not_enumerated) == nil)

-- Show and Pindian remain physical questions despite any conversion rows.
local show_physical = converted_request({response_conversion(11)})
show_physical.options.question = "askForCardShow"
assert(ai_decide(show_physical) == nil)
show_physical.options.question = "askForPindian"
assert(ai_decide(show_physical) == nil)
show_physical.options.card_ids, show_physical.options.cards = {9}, virtual_response.options.cards
show_physical.options.players = {"other"}
assert(ai_decide(show_physical) == nil, "a positive synthetic ID still cannot be a Pindian card")
show_physical.options.question = "askForCardShow"
assert(ai_decide(show_physical) == nil, "a positive synthetic ID still cannot be shown as physical")

-- Zone comes from the target view, not an invented zone field on card DTOs.
local weapon = {id=7, effective_id=7, name="crossbow", class_name="Crossbow",
    kind_of={"Crossbow", "Weapon", "EquipCard"}}
local indulgence = {id=8, effective_id=8, name="indulgence", class_name="Indulgence",
    kind_of={"Indulgence", "DelayedTrick", "TrickCard"}}
local chosen = request("card_chosen", {reason="", card_ids={7, 8}, candidates_complete=true,
    players={"other"}, cards={weapon, indulgence},
    context={who="other", flags="hej", method=0}})
chosen.world_view.players[1].equips = {weapon}
chosen.world_view.players[1].judging_area = {indulgence}
assert(ai_decide(chosen).cards[1] == 8)
local chosen_reversed = request("card_chosen", {reason="", card_ids={8, 7}, candidates_complete=true,
    players={"other"}, cards={indulgence, weapon}, context={who="other", flags="hej", method=0}})
chosen_reversed.world_view.players[1].equips = {weapon}
chosen_reversed.world_view.players[1].judging_area = {indulgence}
assert(ai_decide(chosen_reversed).cards[1] == 8)
local metadata_free_chosen = ai_decide(request("card_chosen", {reason="", card_ids={7, 8},
    candidates_complete=true}))
assert(metadata_free_chosen.cards[1] == 7 or metadata_free_chosen.cards[1] == 8)

-- Mandatory ordinary selections are legal even without friend/enemy policy.
-- Metadata changes ranking, never grants a new card ID outside this list.
local forced_remove = request("card_chosen", {reason="", card_ids={51, 52}, candidates_complete=true,
    cards={payment_slash, payment_peach}, context={who="other", flags="h", method=0}})
forced_remove.world_view.players[1].known_cards = {payment_slash, payment_peach}
forced_remove.world_view.players[1].handcard_count = 2
forced_remove.world_view.mode_policy.relations.viewer.other = "unknown"
assert(ai_decide(forced_remove).cards[1] == 51, "unknown relation minimizes known keep loss")
forced_remove.world_view.mode_policy.relations.viewer.other = "enemy"
assert(ai_decide(forced_remove).cards[1] == 52, "enemy removal prefers higher value")
forced_remove.world_view.mode_policy.relations.viewer.other = "friend"
assert(ai_decide(forced_remove).cards[1] == 51, "a compulsory unhelpful ally removal minimizes loss")
forced_remove.options.optional = true
assert(ai_decide(forced_remove).kind == "pass", "optional removal without a benefit may decline")
forced_remove.options.optional = false
forced_remove.options.candidates_complete = false
assert(ai_decide(forced_remove).cards[1] == 51, "another unknown candidate does not erase an offered legal ID")
ai_skill_cardchosen.remove_priority_contract = function() return 52 end
forced_remove.options.reason = "remove_priority_contract"
assert(ai_decide(forced_remove).cards[1] == 52)

local offered_without_metadata = request("card_chosen", {reason="", card_ids={91},
    candidates_complete=true, context={who="other", flags="h", method=0}})
offered_without_metadata.world_view.players[1].hand_visible = false
offered_without_metadata.world_view.players[1].handcard_count = 4
assert(ai_decide(offered_without_metadata).cards[1] == 91, "only an explicitly offered ID may be returned")
offered_without_metadata.options.card_ids = {}
offered_without_metadata.options.candidates_complete = false
assert(ai_decide(offered_without_metadata) == nil, "hidden hand count never generates candidate IDs")

local saved_lose_equip = SmartAIView.loseEquipEffect
SmartAIView.loseEquipEffect = function() return true end
local friendly_equipment = request("card_chosen", {reason="", card_ids={7, 51},
    cards={weapon, payment_slash}, candidates_complete=true,
    context={who="other", flags="he", method=0}})
friendly_equipment.world_view.players[1].equips = {weapon}
friendly_equipment.world_view.players[1].known_cards = {payment_slash}
friendly_equipment.world_view.players[1].handcard_count = 1
assert(ai_decide(friendly_equipment).cards[1] == 7, "beneficial equipment loss precedes generic low-loss selection")
SmartAIView.loseEquipEffect = saved_lose_equip

-- Unknown Lightning policy must not erase a known Indulgence choice, in either order.
local function judged_pair(first, second)
    local lightning = {id=first, effective_id=first, name="lightning",
        class_name="Lightning", kind_of={"Lightning", "DelayedTrick"}}
    local indulgence_card = {id=second, effective_id=second, name="indulgence",
        class_name="Indulgence", kind_of={"Indulgence", "DelayedTrick"}}
    local value = request("card_chosen", {reason="", card_ids={first, second},
        candidates_complete=true, players={"other"}, cards={lightning, indulgence_card},
        context={who="other", flags="j", method=0}})
    value.world_view.players[1].judging_area = {lightning, indulgence_card}
    return value
end
assert(ai_decide(judged_pair(41, 42)).cards[1] == 42)
assert(ai_decide(judged_pair(42, 41)).cards[1] == 41)
local forced_lightning = judged_pair(41, 42)
forced_lightning.options.card_ids = {41}
assert(ai_decide(forced_lightning).cards[1] == 41, "mandatory Lightning removal remains an offered legal choice")

-- Nullification still requires the trick identity and polarity, even when no
-- physical response exists. Unknown effect intent can deliberately conserve it.
local nullification = request("respond_card", {question="askForNullification", reason="nullification",
    card_ids={}, candidates_complete=true, optional=true,
    context={from="other", to="viewer", positive=true}})
nullification.conversions_enumerated = true
assert(ai_decide(nullification) == nil, "a missing trick is not a projected no-effect decision")
nullification.options.context.card = {name="duel", class_name="Duel", kind_of={"Duel", "TrickCard"}}
assert(ai_decide(nullification).kind == "pass")

-- Original legacy callback shapes and constant values do not receive new-style
-- options in place of the old count/choices/targets arguments.
local legacy = ai_choice_legacy_registries
legacy.ai_skill_invoke.constant_false_contract = false
assert(ai_decide(request("skill_invoke", {reason="constant_false_contract"})).kind == "pass")
legacy.ai_skill_choice.constant_choice_contract = "second"
assert(ai_decide(request("choice", {reason="constant_choice_contract", choices={"first", "second"}})).answer == "second")
legacy.ai_skill_discard.legacy_discard_contract = function(self, maximum, minimum, optional, equiped, pattern)
    assert(maximum == 2 and minimum == 1 and optional == false and equiped == true and pattern == ".")
    return {7}
end
assert(ai_decide(request("discard", {reason="legacy_discard_contract", card_ids={7,8},
    min_count=1, max_count=2, optional=false, context={include_equip=true}})).cards[1] == 7)
legacy.ai_skill_suit.numeric_suit_contract = 2
assert(ai_decide(request("suit", {reason="numeric_suit_contract", choices={"heart", "spade"}})).answer == "heart")

-- Multi-player default fills only the minimum, without inventing effect intent.
local multiple = ai_decide(request("players_chosen", {reason="", players={"viewer", "other"},
    min_count=1, max_count=2, optional=false}))
assert(#multiple.targets == 1 and (multiple.targets[1] == "viewer" or multiple.targets[1] == "other"))

-- New-style pattern policy takes precedence over prompt policy, while a nil
-- result allows the prompt callback to handle the request.
ai_skill_cardask.pattern_contract = function() return nil end
legacy.ai_skill_cardask.prompt_contract = function(self, data, pattern, target, target2, arg)
    assert(pattern == "pattern_contract" and target:objectName() == "other" and arg == "token")
    return 7
end
local cardask = request("respond_card", {question="askForCard", reason="prompt_contract", card_ids={7}})
cardask.pattern, cardask.prompt = "pattern_contract", "prompt_contract:other:viewer:token"
assert(ai_decide(cardask).cards[1] == 7)

-- ExNihilo benefits its target even though it inherits SingleTargetTrick;
-- an absent source is valid for the generic nullification policy.
local nullcard = {id=9, effective_id=9, name="nullification", class_name="Nullification",
    kind_of={"Nullification", "TrickCard"}, number=4}
local beneficial = request("respond_card", {question="askForNullification", reason="",
    card_ids={9}, cards={nullcard}, candidates_complete=true, optional=true,
    context={to="other", positive=true, card={name="ex_nihilo", class_name="ExNihilo",
        kind_of={"ExNihilo", "SingleTargetTrick", "TrickCard"}}}})
beneficial.conversions_enumerated = true
assert(ai_decide(beneficial).kind == "pass")
beneficial.world_view.mode_policy.relations.viewer.other = "enemy"
assert(ai_decide(beneficial).cards[1] == 9)

local function nullification_effect(name, class_name)
    local value = request("respond_card", {question="askForNullification", reason="",
        card_ids={9}, cards={nullcard}, candidates_complete=true, optional=true,
        context={from="viewer", to="other", positive=true,
            card={name=name, class_name=class_name, kind_of={class_name, "TrickCard"}}}})
    value.conversions_enumerated = true
    return value
end
local remove_judge = nullification_effect("snatch", "Snatch")
remove_judge.world_view.players[1].judging_area = {indulgence}
assert(ai_decide(remove_judge).kind == "pass", "removing an ally's detrimental judgment benefits that ally")
remove_judge.world_view.mode_policy.relations.viewer.other = "enemy"
assert(ai_decide(remove_judge).cards[1] == 9, "stop removing an enemy's only detrimental judgment")
remove_judge.options.context.positive = false
assert(ai_decide(remove_judge).kind == "pass")
local remove_hand = nullification_effect("dismantlement", "Dismantlement")
remove_hand.world_view.players[1].handcard_count = 2
assert(ai_decide(remove_hand).cards[1] == 9)

local chain = nullification_effect("iron_chain", "IronChain")
assert(ai_decide(chain).kind == "pass", "unknown chain state conserves Nullification")
chain.world_view.players[1].chained = false
assert(ai_decide(chain).cards[1] == 9)
chain.world_view.players[1].chained = true
assert(ai_decide(chain).kind == "pass")
chain.world_view.mode_policy.relations.viewer.other = "enemy"
assert(ai_decide(chain).cards[1] == 9, "unchaining an enemy is a benefit to stop")
chain.options.context.positive = false
assert(ai_decide(chain).kind == "pass")

local lightning_response = nullification_effect("lightning", "Lightning")
assert(ai_decide(lightning_response).cards[1] == 9)
lightning_response.world_view.mode_policy.relations.viewer.other = "enemy"
assert(ai_decide(lightning_response).kind == "pass")
local collateral_response = nullification_effect("collateral", "Collateral")
assert(ai_decide(collateral_response).kind == "pass", "no visible weapon pressure means no reason to spend")
collateral_response.world_view.players[1].equips = {weapon}
collateral_response.options.context.from = "other"
collateral_response.options.context.to = "viewer"
collateral_response.world_view.self.equips = {weapon}
collateral_response.world_view.mode_policy.relations.viewer.other = "enemy"
assert(ai_decide(collateral_response).cards[1] == 9)

local custom_response = nullification_effect("custom_trick_contract", "CustomTrickContract")
assert(ai_decide(custom_response).kind == "pass", "unclassified effect conserves a legal response")
sgs.dynamic_value.damage_card.CustomTrickContract = true
assert(ai_decide(custom_response).cards[1] == 9)
sgs.dynamic_value.damage_card.CustomTrickContract = nil
sgs.dynamic_value.benefit.CustomTrickContract = true
assert(ai_decide(custom_response).kind == "pass")
custom_response.world_view.mode_policy.relations.viewer.other = "enemy"
assert(ai_decide(custom_response).cards[1] == 9)
sgs.dynamic_value.benefit.CustomTrickContract = nil
custom_response.options.context.positive = nil
assert(ai_decide(custom_response) == nil, "missing polarity remains unsupported")
custom_response.options.context.positive = true
custom_response.options.context.card = {}
assert(ai_decide(custom_response) == nil, "missing identity must raise a typed unsupported signal")
local null_priority = nullification_effect("null_priority_contract", "NullPriorityContract")
sgs.dynamic_value.damage_card.NullPriorityContract = true
ai_nullification.NullPriorityContract = function() return {kind="pass"} end
assert(ai_decide(null_priority).kind == "pass", "a class effect hook precedes shared dynamic classification")
sgs.dynamic_value.damage_card.NullPriorityContract = nil

local pindian_defaults = request("respond_card", {question="askForPindian", reason="",
    card_ids={51, 52, 53}, cards={payment_slash,
        {id=52, effective_id=52, name="peach", class_name="Peach", kind_of={"Peach"}, number=5}, second_slash},
    players={"other"}, candidates_complete=true, optional=false})
pindian_defaults.world_view.mode_policy.relations.viewer.other = "neutral"
assert(ai_decide(pindian_defaults).cards[1] == 53, "neutral Pindian favors high number, then low keep")
pindian_defaults.world_view.mode_policy.relations.viewer.other = nil
assert(ai_decide(pindian_defaults).cards[1] == 53)
pindian_defaults.world_view.mode_policy.relations.viewer.other = "friend"
assert(ai_decide(pindian_defaults).cards[1] == 51, "a known friend may win the Pindian")

local rescue = request("respond_card", {question="askForSinglePeach", reason="",
    card_ids={52}, cards={payment_peach}, players={"other"}, candidates_complete=true, optional=true})
rescue.pattern, rescue.conversions_enumerated = "peach", true
rescue.world_view.mode_policy.relations.viewer.other = "enemy"
assert(ai_decide(rescue).kind == "pass")
rescue.world_view.mode_policy.relations.viewer.other = "neutral"
assert(ai_decide(rescue).kind == "pass")
rescue.world_view.mode_policy.relations.viewer.other = "friend"
assert(ai_decide(rescue).cards[1] == 52)
ai_skill_singlepeach.rescue_priority_contract = function() return 52 end
rescue.options.reason = "rescue_priority_contract"
rescue.world_view.mode_policy.relations.viewer.other = "enemy"
assert(ai_decide(rescue).cards[1] == 52, "a skill policy may rescue an enemy explicitly")

-- Judge-dependent callbacks receive the value-only typed data ABI.
legacy.ai_skill_invoke.judge_contract = function(self, data)
    local judge = data:toJudge()
    assert(judge.who:objectName() == "other" and judge.card:getEffectiveId() == 9)
    return judge:isGood() == true
end
assert(ai_decide(request("skill_invoke", {reason="judge_contract",
    context={judge={who="other", card=nullcard, good=true}}})).answer == "yes")

-- Unsupported signals from unrelated equipment hooks do not erase a known
-- harmful judgement. Its fixed removal preference requires no keep-value hook.
local saved_poison, saved_keep = SmartAIView.poisonCards, SmartAIView.getKeepValue
SmartAIView.poisonCards = function() ai_unsupported("equipment policy unknown", "equipment") end
SmartAIView.getKeepValue = function(self, card)
    if card:isKindOf("Indulgence") then ai_unsupported("judgement keep unknown", "keep") end
    return saved_keep(self, card)
end
assert(ai_decide(chosen).cards[1] == 8)
SmartAIView.poisonCards, SmartAIView.getKeepValue = saved_poison, saved_keep

return true
