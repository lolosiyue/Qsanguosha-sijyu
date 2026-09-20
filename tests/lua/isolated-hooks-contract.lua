-- Strategy hook table identity, namespace and skill projection contract.
local function player(name)
    return {object_name = name, alive = true, skills = {
        {name = "first", instance_id = 1, invalid = false},
        {name = "first", instance_id = 2, invalid = false},
        {name = "off", instance_id = 3, invalid = true},
        {name = "zero", instance_id = 4, invalid = false}
    }, equips = {}, judging_area = {}, public_marks = {}}
end

assert(type(sgs) == "table" and type(sgs.ai_skill_choice) == "table")
assert(sgs.ai_skill_choice == sgs.ai_choice_legacy_registries.ai_skill_choice
    or sgs.ai_choice_legacy_registries.ai_skill_choice == nil)
local ai = assert(SmartAIView.new({viewer = "self", world_view = {
    self = player("self"), players = {}, player_order = {"self"}, alive_player_order = {"self"},
    hand_cards = {}}}))

assert(ai:getHooks("skill_choice") == sgs.ai_skill_choice)
assert(ai:getHooks("ai_skill_choice") == sgs.ai_skill_choice)
sgs.ai_skill_choice.__contract_scalar = false
assert(ai:callHook("skill_choice", "__contract_scalar") == false)
sgs.ai_skill_choice.__contract_callback = function(a, b) return a, b end
local a, b = ai:callHook("ai_skill_choice", "__contract_callback", 3, "x")
assert(a == 3 and b == "x")

local skill = ai.player
sgs.ai_target_revises.first = 0
sgs.ai_target_revises.zero = false
sgs.ai_target_revises.off = function() return true end
local hooks = ai:forSkillHooks("target_revises", skill)
assert(hooks:length() == 2 and hooks[1].key == "first" and hooks[1].value == 0)
assert(hooks[2].key == "zero" and hooks[2].value == false)
assert(sgs.ai_use_revises == ai:getHooks("use_revises"))
assert(sgs.ai_cardneed == ai:getHooks("cardneed"))
assert(ai:getHooks("retrial") == sgs.ai_retrial)
assert(sgs.ai_retrial ~= sgs.ai_retrial_intention)
assert(sgs.ai_skill_use == ai_skill_use_legacy and sgs.ai_skill_use ~= ai_skill_use)
assert(sgs.ai_skill_guanxing == ai_choice_legacy_registries.ai_skill_guanxing)
assert(sgs.ai_skill_singlepeach == ai_choice_legacy_registries.ai_skill_singlepeach)
assert(ai:getHooks("dynamic_value") == sgs.dynamic_value)
assert(type(sgs.dynamic_value.damage_card) == "table"
    and type(sgs.dynamic_value.control_card) == "table"
    and type(sgs.dynamic_value.benefit) == "table")
assert(type(sgs.masochism_skill) == "string" and type(sgs.need_kongcheng) == "string")
assert(type(sgs.DamageStruct_Fire) == "number")
assert(sgs.card_damage_nature.FireSlash == sgs.DamageStruct_Fire)

sgs.ai_skill_choice.__contract_scalar = nil
sgs.ai_skill_choice.__contract_callback = nil
sgs.ai_target_revises.first = nil
sgs.ai_target_revises.zero = nil
sgs.ai_target_revises.off = nil
return true
