-- Value, damage and target helper consumers use only visible facade values.
local function skill(name, instance)
    return {name = name, instance_id = instance or 1, invalid = false}
end

local function card(id, name, class_name, kinds, fields)
    local value = {id = id, effective_id = id, name = name, class_name = class_name,
        number = 7, suit = sgs.Card_Heart, red = true, black = false,
        kind_of = kinds or {class_name, "Card"}}
    for key, item in pairs(fields or {}) do value[key] = item end
    return value
end

local function player(name, fields)
    local value = {object_name = name, hp = 3, max_hp = 4, handcard_count = 1,
        max_cards = 3, hujia = 0, skills = {}, equips = {}, judging_area = {},
        known_cards = {}, hand_visible = true, public_marks = {}, alive = true}
    for key, item in pairs(fields or {}) do value[key] = item end
    return value
end

local weapon = card(1, "hook_weapon", "HookWeapon", {"HookWeapon", "Weapon", "EquipCard", "Card"},
    {weapon_range = 2})
local armor = card(2, "hook_armor", "HookArmor", {"HookArmor", "Armor", "EquipCard", "Card"})
local slash = card(3, "slash", "Slash", {"Slash", "BasicCard", "Card"})
local request = {viewer = "self", kind = "activate", world_view = {
    self = player("self", {skills = {skill("value_hook", 1), skill("value_hook", 2),
        skill("later_hook", 3)}, hand_cards = {weapon, armor, slash}, equips = {armor}}),
    players = {player("friend", {hp = 1, handcard_count = 0}),
        player("enemy", {hp = 1, handcard_count = 2})},
    player_order = {"self", "friend", "enemy"},
    alive_player_order = {"self", "friend", "enemy"},
    distances = {self = {friend = 1, enemy = 2}},
    hand_cards = {weapon, armor, slash},
    mode_policy = {managed = true, relations = {
        self = {friend = "friend", enemy = "enemy"}}}}}

local ai = assert(SmartAIView.new(request))
local friend = assert(ai.room:findPlayerByObjectName("friend"))
local enemy = assert(ai.room:findPlayerByObjectName("enemy"))
local calls, later_calls = 0, 0
sgs.ai_card_priority.value_hook = function(self, offered, value)
    assert(self == ai and AIValue.isCard(offered))
    calls = calls + 1
    return value == 0 and 0 or 0.25
end
sgs.ai_card_priority.later_hook = function(self, offered, value)
    assert(self == ai and AIValue.isCard(offered) and value > 4)
    later_calls = later_calls + 1
    return 0.5
end
sgs.ai_suit_priority.value_hook = "heart|spade|club|diamond"
assert(ai:getUsePriority(CardView.new(slash)) > 4.7)
assert(calls == 1 and later_calls == 1)
sgs.ai_card_priority.value_hook = nil
sgs.ai_card_priority.later_hook = nil
sgs.ai_suit_priority.value_hook = nil

sgs.ai_skill_defense.value_hook = 2
assert(ai:getDefense(ai.player) >= 5)
sgs.ai_skill_defense.value_hook = nil
local baseline_calls, target_calls = 0, 0
sgs.ai_weapon_value.hook_weapon = function(self, target, owner)
    assert(self == ai and owner == ai.player)
    if target == nil then baseline_calls = baseline_calls + 1; return 2 end
    assert(target == enemy)
    target_calls = target_calls + 1
    return 3
end
sgs.ai_slash_weaponfilter.hook_weapon = function(self, target, owner)
    assert(self == ai and target == enemy and owner == ai.player)
    return true
end
sgs.ai_armor_value.value_hook = 1
local weapon_value, in_range = ai:evaluateWeapon(CardView.new(weapon))
assert(weapon_value == 6 and in_range == true)
assert(baseline_calls == 1 and target_calls == 1)
assert(ai:evaluateWeapon(CardView.new(weapon), ai.player, enemy) == 6)
assert(baseline_calls == 2 and target_calls == 2)
sgs.ai_armor_value.hook_armor = function(owner, self, offered)
    assert(self == ai and owner == ai.player and offered:objectName() == "hook_armor")
    return 3
end
assert(ai:evaluateArmor(CardView.new(armor)) == 4.1)
assert(ai:evaluateArmor() == 4.1)
sgs.ai_weapon_value.hook_weapon = nil
sgs.ai_slash_weaponfilter.hook_weapon = nil
sgs.ai_armor_value.value_hook = nil
sgs.ai_armor_value.hook_armor = nil

local draws = ai:findPlayerToDraw(false, 1, true)
assert(draws and draws[1] == friend)
local discards = ai:findPlayerToDiscard("h", false, nil, AIList.new({enemy}))
assert(discards and discards[1] == enemy)
local damage = ai:findPlayerToDamage(1, ai.player, "N", AIList.new({friend, enemy}), 0,
    CardView.new(slash))
assert(damage and damage[1] == enemy)
assert(ai:findBestDamageTarget(1, "N", 0, CardView.new(slash)) == enemy)

-- Each case receives fresh facade indexes; changing snapshot fields after
-- construction would bypass the production visibility/method contract.
local function copy(value)
    if type(value) ~= "table" then return value end
    local result = {}
    for key, item in pairs(value) do result[key] = copy(item) end
    return result
end
local function fresh(change)
    local next_request = copy(request)
    change(next_request.world_view)
    return assert(SmartAIView.new(next_request))
end

local outside = fresh(function(world) world.distances.self.enemy = 3 end)
local outside_value, outside_range = outside:evaluateWeapon(CardView.new(weapon))
assert(outside_value == 0 and outside_range == false)
local no_distance = fresh(function(world) world.distances = nil end)
assert(no_distance:evaluateWeapon(CardView.new(weapon)) == nil)
assert(no_distance:sortByUseValue(AIList.new({CardView.new(slash), CardView.new(weapon)})) == nil)
local no_range = copy(weapon)
no_range.weapon_range = nil
assert(ai:evaluateWeapon(CardView.new(no_range)) == nil)
local no_equips = fresh(function(world) world.self.equips = nil end)
assert(no_equips:evaluateArmor() == nil)
local unknown_priority = fresh(function(world) world.self.skills = nil end)
sgs.ai_card_priority.value_hook = function() return 0 end
assert(unknown_priority:sortByUsePriority(AIList.new({CardView.new(slash)})) == nil)
sgs.ai_card_priority.value_hook = nil

local seen_nature
sgs.ai_ajustdamage_from.value_hook = function(self, from, to, offered, nature)
    assert(self == ai and from == ai.player and to == enemy)
    assert(offered:getClassName() == "Slash")
    seen_nature = nature
    return 0
end
local enemies = AIList.new({enemy})
assert(#ai:findPlayerToDamage(3, ai.player, "F", enemies, 60, CardView.new(slash)) == 1)
assert(seen_nature == "F")
assert(#ai:findPlayerToDamage(1, ai.player, "T", enemies, 60, CardView.new(slash)) == 0)
assert(seen_nature == "T")
assert(#ai:findPlayerToDamage(0, ai.player, "N", enemies, 0, CardView.new(slash)) == 0)
assert(ai:findPlayerToDamage(1, ai.player, "invalid", enemies, 0, CardView.new(slash)) == nil)
sgs.ai_ajustdamage_from.value_hook = nil

local unknown_defense = fresh(function(world)
    world.players[2].skills = {skill("unknown_defense")}
end)
sgs.ai_skill_defense.unknown_defense = function() return "unsupported" end
assert(unknown_defense:findPlayerToDamage(1, nil, "N", nil, 0, CardView.new(slash)) == nil)
sgs.ai_skill_defense.unknown_defense = nil

assert(ai:canDraw(friend) == true)
local blocked_draw = fresh(function(world)
    world.players[1].skills = {skill("sfofl_suiqu")}
end)
assert(blocked_draw:canDraw(blocked_draw.room:findPlayerByObjectName("friend")) == false)
assert(#blocked_draw:findPlayerToDraw(false, 1, true) == 0)
for _, name in ipairs({"manjuan", "zishu"}) do
    local inactive = fresh(function(world)
        world.players[1].skills = {skill(name)}
        world.players[1].phase = sgs.Player_NotActive
    end)
    assert(inactive:canDraw(inactive.room:findPlayerByObjectName("friend")) == false)
    local unknown_phase = fresh(function(world) world.players[1].skills = {skill(name)} end)
    assert(unknown_phase:canDraw(unknown_phase.room:findPlayerByObjectName("friend")) == nil)
end
local unknown_zhafu = fresh(function(world)
    world.players[2].skills = {skill("zhafu")}
    world.players[1].phase = 5
end)
assert(unknown_zhafu:canDraw(unknown_zhafu.room:findPlayerByObjectName("friend")) == nil)
local outside_zhafu = fresh(function(world)
    world.players[2].skills = {skill("zhafu")}
    world.players[1].phase = 2
end)
assert(outside_zhafu:canDraw(outside_zhafu.room:findPlayerByObjectName("friend")) == true)

local peach = CardView.new(card(13, "peach", "Peach", {"Peach", "BasicCard", "Card"}))
local normal_keep = ai:getKeepValue(peach)
sgs.ai_NeedPeach[ai.player:objectName()] = 2
assert(ai:getKeepValue(peach) == normal_keep + 4, "Peach reserve must affect decision cost")
sgs.ai_NeedPeach[ai.player:objectName()] = function() return math.huge end
assert(ai:getKeepValue(peach) == nil, "invalid reserve cannot become a usable card value")
sgs.ai_NeedPeach[ai.player:objectName()] = nil

return true
