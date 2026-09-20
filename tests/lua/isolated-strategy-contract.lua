-- PR07 shared standard-family contract.  The fixture supplies only value projections:
-- authority rows decide legality, while family hooks decide pure target preference.
local function player(name, fields)
    local value = {object_name = name, alive = true, dead = false, hp = 4, max_hp = 4,
        handcard_count = 0, hujia = 0, max_cards = 4, attack_range = 1,
        equips = {}, equip_slots = {}, judging_area = {}, known_cards = {}, hand_visible = true,
        skills = {}, public_marks = {}}
    for key, item in pairs(fields or {}) do value[key] = item end
    value.wounded = value.hp < value.max_hp
    return value
end

local function card(id, name, class_name, kinds)
    return {id = id, effective_id = id, name = name, class_name = class_name,
        number = 7, suit = sgs.Card_Club, red = false, black = true,
        -- Crossbow's native constructor is Weapon(suit, number, 1).
        weapon_range = class_name == "Crossbow" and 1 or nil,
        kind_of = kinds or {class_name, "Card"}}
end

local function candidate(id, rows, targets, fixed, affected)
    local feasible = false
    for _, row in ipairs(rows or {}) do
        if #row == 0 then feasible = true end
    end
    return {card_id = id, available = true, limited = false, jilei = false,
        target_fixed = fixed == true,
        feasible_with_no_target = feasible,
        complete_coverage = true, legal_targets = targets or {},
        target_combinations = rows, affected_targets = affected}
end

local function request(cards, candidates)
    -- Native card metadata and its authority candidate agree on target shape.
    for index, entry in ipairs(candidates) do
        entry.candidate_id = index
        for _, projected in ipairs(cards) do
            if projected.effective_id == entry.card_id then
                projected.target_fixed = entry.target_fixed
            end
        end
    end
    local world = {mode_id = "isolated-family-fixture", revision = "1",
        current_player = "viewer", self = player("viewer", {hp = 3}),
        players = {
            player("enemy", {hp = 1, handcard_count = 2, chained = false}),
            player("friend", {hp = 2, handcard_count = 1, chained = true}),
            player("armed", {hp = 4, equips = {card(90, "crossbow", "Crossbow",
                {"Crossbow", "Weapon", "EquipCard", "Card"})}})
        },
        player_order = {"viewer", "enemy", "friend", "armed"},
        alive_player_order = {"viewer", "enemy", "friend", "armed"},
        distances = {viewer = {enemy = 1, friend = 2, armed = 1}},
        hand_cards = cards}
    world.mode_policy = {managed = true, objectives = {}, relations = {
        viewer = {enemy = "enemy", friend = "friend", armed = "enemy"}}}
    return {viewer = "viewer", kind = "activate", world_view = world,
        card_candidates = candidates, conversions_enumerated = true,
        card_conversions = {}}
end

local kinds = {"SavageAssault", "AOE", "TrickCard", "Card"}
local aoe = card(1, "savage_assault", "SavageAssault", kinds)
local ex = card(2, "ex_nihilo", "ExNihilo", {"ExNihilo", "TrickCard", "Card"})
local equip = card(3, "crossbow", "Crossbow", {"Crossbow", "Weapon", "EquipCard", "Card"})
equip.equip_slot = 0
local indulgence = card(4, "indulgence", "Indulgence", {"Indulgence", "DelayedTrick", "TrickCard", "Card"})
local chain = card(5, "iron_chain", "IronChain", {"IronChain", "TrickCard", "Card"})
local collateral = card(6, "collateral", "Collateral", {"Collateral", "TrickCard", "Card"})

local ai = assert(SmartAIView.new(request({aoe, ex, equip, indulgence, chain, collateral}, {
    candidate(1, {{}}, {}, true, {"enemy", "friend"}),
    candidate(2, {{}}, {}, true), candidate(3, {{}}, {}, true),
    candidate(4, {{"enemy"}, {"friend"}}, {"enemy", "friend"}),
    candidate(5, {{}, {"enemy", "friend"}}, {"enemy", "friend"}),
    candidate(6, {{"armed", "enemy"}, {"armed", "friend"}}, {"armed", "enemy", "friend"})
})))

local plan, status = ai:tryUseCard(CardView.new(equip))
assert(status == "planned" and plan.card:getClassName() == "Crossbow")
plan, status = ai:tryUseCard(CardView.new(ex))
assert(status == "planned" and plan.to:length() == 0)
plan, status = ai:tryUseCard(CardView.new(aoe))
assert(status == "planned" and plan.to:length() == 0
    and plan.to:isEmpty())
plan, status = ai:tryUseCard(CardView.new(indulgence))
assert(status == "planned" and plan.to:first():objectName() == "enemy")
local multi_delayed = request({indulgence}, {
    candidate(4, {{"enemy", "armed"}}, {"enemy", "armed"})})
local delayed_plan, delayed_status = SmartAIView.new(multi_delayed):tryUseCard(CardView.new(indulgence))
assert(delayed_status == "planned" and delayed_plan.to:length() == 2
    and delayed_plan.to[2]:objectName() == "armed")
plan, status = ai:tryUseCard(CardView.new(chain))
assert(status == "planned" and plan.to:length() == 2)
plan, status = ai:tryUseCard(CardView.new(collateral))
assert(status == "planned" and plan.to[1]:objectName() == "armed"
    and plan.to[2]:objectName() == "enemy")

-- A declared hook is value-only and can alter preference without adding a new strategy.
local old = ai_card_effect.IronChain
ai_card_effect.IronChain = function(_, _, target, relation)
    return relation == "friend" and (target:isWounded() and 11 or 0) or -10
end
plan, status = ai:tryUseCard(CardView.new(chain))
assert(status == "planned" and plan.to[1]:objectName() == "enemy"
    and plan.to[2]:objectName() == "friend")
ai_card_effect.IronChain = old

-- Recast feasibility must not erase a selected non-empty IronChain sequence.
local reversed = request({chain}, {candidate(5, {{}, {"enemy", "friend"}}, {"enemy", "friend"})})
reversed.world_view.players[1].chained = true
reversed.world_view.players[2].chained = false
local _, reversed_status = SmartAIView.new(reversed):tryUseCard(CardView.new(chain))
assert(reversed_status == "declined")
reversed.world_view.players[1].chained = nil
local _, missing_chain_status = SmartAIView.new(reversed):tryUseCard(CardView.new(chain))
assert(missing_chain_status == "unsupported")

-- Equipment slots contain IDs, while equips contains the public card DTOs.
local replacement = request({equip}, {candidate(3, {{}}, {}, true)})
local current = card(90, "fixture_weapon", "FixtureWeapon", {"Weapon", "EquipCard", "Card"})
current.equip_slot = 0
current.weapon_range = 1
replacement.world_view.self.equip_slots = {[1]=90}
replacement.world_view.self.equips = {current}
local old_crossbow, old_weapon = ai_use_value.Crossbow, ai_use_value.FixtureWeapon
ai_use_value.Crossbow, ai_use_value.FixtureWeapon = 1, 10
local _, replacement_status = SmartAIView.new(replacement):tryUseCard(CardView.new(equip))
assert(replacement_status == "declined")
ai_use_value.Crossbow, ai_use_value.FixtureWeapon = old_crossbow, old_weapon

-- Pure preference hooks must reject NaN and infinity rather than invent a plan.
for _, invalid in ipairs({math.huge, 0 / 0}) do
    ai_card_effect.IronChain = function() return invalid end
    local _, invalid_status = ai:tryUseCard(CardView.new(chain))
    assert(invalid_status == "unsupported")
end
ai_card_effect.IronChain = old

-- Missing rows/effect coverage remain unsupported; a known empty effect set is declined.
local saved_value = ai_use_value.Crossbow
ai_use_value.Crossbow = function() return math.huge end
local _, invalid_value_status = SmartAIView.new(replacement):tryUseCard(CardView.new(equip))
assert(invalid_value_status == "unsupported")
ai_use_value.Crossbow = saved_value
local unknown = request({aoe}, {candidate(1, nil, nil)})
local _, unknown_status = SmartAIView.new(unknown):tryUseCard(CardView.new(aoe))
assert(unknown_status == "unsupported")
local empty = request({aoe}, {candidate(1, {{}}, {}, true, {})})
local _, empty_status = SmartAIView.new(empty):tryUseCard(CardView.new(aoe))
assert(empty_status == "declined")
local no_effects = request({aoe}, {candidate(1, {{}}, {}, true)})
local _, no_effects_status = SmartAIView.new(no_effects):tryUseCard(CardView.new(aoe))
assert(no_effects_status == "unsupported")
local no_sequence = request({ex}, {candidate(2, {}, {}, true)})
local _, no_sequence_status = SmartAIView.new(no_sequence):tryUseCard(CardView.new(ex))
assert(no_sequence_status == "unsupported")
return true
