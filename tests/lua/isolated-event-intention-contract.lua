-- Source-only contract: the runner loads bootstrap, facades and core modules first.
assert(type(ai_event) == "function")
local function player(name, skills)
    return {object_name = name, alive = true, hp = 3, max_hp = 4,
        skills = skills or {}, equips = {}, judging_area = {}, public_marks = {}, flags = {}}
end
local world = {self = player("viewer"), players = {player("source"), player("target")},
    player_order = {"viewer", "source", "target"},
    alive_player_order = {"viewer", "source", "target"}, hand_cards = {}, events = {}}
local function event(kind)
    return {kind = kind, trigger_event = sgs[kind] or -991, from = "source", to = "target",
        targets = {"target"}, card_name = "", card_class = "", card_skill = "",
        amount = 2, reason = "", details = {player = "target"}}
end
local function delta(result, level)
    assert(#result == 1 and result[1].from == "source" and result[1].to == "target"
        and result[1].level == level)
end
-- Missing choice registries and missing entries are both inert. A malformed
-- registered callback must still fail instead of being silently ignored.
for _, kind in ipairs({"skillChoice", "skillInvoke"}) do
    local choice = event("ChoiceMade")
    choice.reason = "unregistered_choice"
    choice.details.choice_kind, choice.details.answer = kind, "yes"
    local previous = sgs.ai_choicemade_filter[kind]
    sgs.ai_choicemade_filter[kind] = nil
    assert(#ai_event(world, choice) == 0)
    sgs.ai_choicemade_filter[kind] = {}
    assert(#ai_event(world, choice) == 0)
    sgs.ai_choicemade_filter[kind][choice.reason] = false
    assert(not pcall(ai_event, world, choice))
    sgs.ai_choicemade_filter[kind] = previous
end
local used = event("TargetSpecified")
used.card_name, used.card_class = "contract_card", "ContractCard"
used.details.card = {name = "contract_card", class_name = "ContractCard", id = -1,
    effective_id = -1, virtual_card = true, kind_of = {"Card"}}
sgs.ai_card_intention.ContractCard = 33
delta(ai_event(world, used), 33)
used.intention_suppressed = true
assert(#ai_event(world, used) == 0)
used.intention_suppressed = false

local calls = 0
sgs.ai_card_intention.ContractCard = function(self, card, from, targets)
    calls = calls + 1
    assert(self.player:objectName() == "viewer" and card:getClassName() == "ContractCard")
    assert(from:objectName() == "source" and targets[1]:objectName() == "target")
    sgs.updateIntentions(from, targets, 17)
end
delta(ai_event(world, used), 17)
assert(calls == 1)
local damage = event("DamageInflicted")
damage.card_name, damage.card_class = used.card_name, used.card_class
assert(#ai_event(world, damage) == 0) -- Card intention already owns this effect.
sgs.ai_card_intention.ContractCard = nil
delta(ai_event(world, damage), 80) -- Unregistered card damage still supplies evidence.
damage.card_name, damage.card_class = "", ""
damage.chain = true
delta(ai_event(world, damage), 40)
damage.chain = false
damage.reason = "contract_suppressed"
sgs.ai_damage_reason_suppress_intention.contract_suppressed = true
assert(#ai_event(world, damage) == 0)
sgs.ai_damage_reason_suppress_intention.contract_suppressed = nil
damage.reason = ""
world.players[1].flags = {"contract_flag"}
sgs.ai_damage_from_flag_intention.contract_flag = 7
delta(ai_event(world, damage), 14)
sgs.ai_damage_from_flag_intention.contract_flag = nil
world.players[1].flags = {}

-- Visible skill names are deduplicated across instances and players; hidden and
-- invalid skills cannot trigger a callback through this viewer's event snapshot.
world.self.skills = {{name = "contract_skill", invalid = false},
    {name = "contract_skill", invalid = false}, {name = "contract_invalid", invalid = true}}
world.players[1].skills = {{name = "contract_skill", invalid = false}}
local callback_calls = 0
sgs.ai_event_callback[sgs.HpRecover] = {
    contract_skill = function(self, actor, snapshot)
        callback_calls = callback_calls + 1
        assert(actor:objectName() == "target" and snapshot.kind == "HpRecover")
        return {{from = "source", to = "target", level = -12}}
    end,
    contract_hidden = function() error("hidden skill executed") end,
    contract_invalid = function() error("invalid skill executed") end
}
delta(ai_event(world, event("HpRecover")), -12)
assert(callback_calls == 1)
world.self.skills, world.players[1].skills = {}, {}
assert(#ai_event(world, event("HpRecover")) == 0)
sgs.ai_event_callback[sgs.HpRecover] = nil

-- A callback failure discards prior emissions and clears the active sink.
sgs.ai_card_intention.ContractCard = function(self, card, from, targets)
    sgs.updateIntentions(from, targets, 3)
    error("contract event failure")
end
assert(not pcall(ai_event, world, used))
assert(not pcall(sgs.updateIntention, "source", "target", 1))
sgs.ai_card_intention.ContractCard = 4
delta(ai_event(world, used), 4)
sgs.ai_card_intention.ContractCard = nil

ai_damage_intention.contract_damage = function() return 9 end
damage.reason = "contract_damage"
delta(ai_event(world, damage), 9)
for _, bad in ipairs({math.huge, -math.huge, 0 / 0}) do
    ai_damage_intention.contract_damage = function() return bad end
    assert(not pcall(ai_event, world, damage))
end
ai_damage_intention.contract_damage = function()
    return {{from = "foreign_viewer", to = "target", level = 1}}
end
assert(not pcall(ai_event, world, damage))
ai_damage_intention.contract_damage = function()
    return {{from = "source", to = "target", level = 1, extra = true}}
end
assert(not pcall(ai_event, world, damage))
ai_damage_intention.contract_damage = function()
    local result = {}
    for index = 1, 65 do result[index] = {from = "source", to = "target", level = 1} end
    return result
end
assert(not pcall(ai_event, world, damage))
ai_damage_intention.contract_damage = nil

-- ChoiceMade carries only public selections; no hidden card ID reconstruction.
local choice = event("ChoiceMade")
choice.reason, choice.details.choice_kind = "contract_choice", "playerChosen"
sgs.ai_playerchosen_intention.contract_choice = -7
delta(ai_event(world, choice), -7)
sgs.ai_playerchosen_intention.contract_choice = function(self, from, to)
    sgs.updateIntention(from, to, 8)
end
delta(ai_event(world, choice), 8)
sgs.ai_playerchosen_intention.contract_choice = nil
choice.targets = {"target", "viewer"}
sgs.ai_playerschosen_intention.contract_choice = function(self, from, names)
    assert(names == "target+viewer")
    sgs.updateIntention(from, self.player, -2)
end
local multiple = ai_event(world, choice)
assert(#multiple == 1 and multiple[1].to == "viewer" and multiple[1].level == -2)
sgs.ai_playerschosen_intention.contract_choice = nil
choice.targets, choice.details.choice_kind = {"target"}, "Yiji"
sgs.ai_Yiji_intention.contract_choice = function(self, snapshot)
    assert(snapshot.details.choice_kind == "Yiji" and snapshot.card_ids == nil)
    return {{from = snapshot.from, to = snapshot.targets[1], level = -4}}
end
delta(ai_event(world, choice), -4)
sgs.ai_Yiji_intention.contract_choice = nil

assert(sgs.ai_card_intention.Slash == 80 and sgs.ai_card_intention.Peach == -120
    and sgs.ai_card_intention.Duel == 66 and sgs.ai_card_intention.SupplyShortage == 120)

local other = {self = player("other"), players = {player("source"), player("target")},
    player_order = {"other", "source", "target"}, alive_player_order = {"other", "source", "target"}}
damage.reason = ""
delta(ai_event(other, damage), 80) -- No PlayerView or active sink leaks across viewers.
assert(sgs.ais == nil and SmartAI == nil)
return true
