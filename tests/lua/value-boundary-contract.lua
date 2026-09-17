-- Pure-value contract fixtures. Run in a fresh Lua state; no gameplay is started.
dofile("lua/ai/value-boundary.lua")

-- Without the facade layer nothing is a proxy, so every native branch is untouched.
assert(aiValueKind({}) == nil and aiValueKind("x") == nil and aiValueKind(nil) == nil)
assert(aiRejectValueView("entry", {}) == false and aiRejectValueView("entry", 7) == false)
assert(aiCardId(7) == 7 and aiCardId("7") == 7 and aiCardId(0) == 0)
assert(aiCardId(-1) == nil and aiCardId({}) == nil and aiCardId(nil) == nil)
assert(aiSkillKey("alpha") == "alpha" and aiSkillKey("alpha", 4) == "alpha#4")
assert(aiSkillKey("alpha", 0) == "alpha" and aiSkillKey("alpha", "4") == "alpha")
assert(aiSkillKey(7) == nil and aiSkillKey({}) == nil and aiSkillKey(nil) == nil)

-- The isolated facade layer is the only thing that turns tables into proxies.
local kinds = setmetatable({}, {__mode = "k"})
AIValue = {kind = function(value) return kinds[value] end}
local function proxy(kind, view)
    kinds[view] = kind
    return view
end
local card = proxy("card", {getEffectiveId = function() return 12 end})
local skill = proxy("skill", {objectName = function() return "alpha" end,
    getInstanceId = function() return 4 end})
local player = proxy("player", {objectName = function() return "owner" end})
assert(aiValueKind(card) == "card" and aiValueKind(player) == "player")
assert(aiValueKind(skill) == "skill" and aiValueKind({}) == nil)

-- A card proxy has an identity; a player proxy is not a card and is never an ID.
assert(aiCardId(card) == 12 and aiCardId(player) == nil)
assert(aiSkillKey(skill) == "alpha#4" and aiSkillKey(skill, 0) == "alpha")
assert(aiSkillKey(card) == nil and aiSkillKey(player) == nil)

-- An unsupported entry says which entry and which proxy; it never answers "nothing".
local ok, message = pcall(aiRejectValueView, "getKnownCards", player)
assert(not ok and string.find(message, "getKnownCards", 1, true))
assert(string.find(message, "player", 1, true))
assert(not pcall(aiRejectValueView, "aiUseCard", card))
assert(not pcall(aiRejectValueView, "getPlayerSkillList", skill))
assert(aiRejectValueView("isCard", {}) == false)
AIValue = nil
return true
