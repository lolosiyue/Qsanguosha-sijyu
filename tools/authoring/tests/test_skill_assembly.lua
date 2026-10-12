-- Run from the repository root using Lua 5.4.
-- Real Lua factories + explicit native-boundary doubles; not a Room simulation.
local checks = 0
local function equal(actual, expected, message)
    assert(actual == expected, (message or "mismatch") .. ": " .. tostring(actual) .. " ~= " .. tostring(expected))
    checks = checks + 1
end
local function rejects(fn, expected)
    local ok, err = pcall(fn)
    assert(not ok and tostring(err):find(expected, 1, true), "expected error: " .. expected .. ", got " .. tostring(err))
    checks = checks + 1
end

sgs = {
    Skill_NotFrequent = 0, Skill_Compulsory = 2,
    Skill_Limit_None = 0, Skill_Limit_Turn = 2, Skill_Limit_Custom = 9,
    CorrectSkill_Primary = 0, CorrectSkill_System = 4,
    Damaged = 17, EventPhaseStart = 18,
}
local native = {}
function native:objectName() return self.name end
function native:setBaseAmount(n) self.base = n end
function native:getEffectiveAmount(ctx)
    if ctx.modified_amount ~= nil then return math.max(0, ctx.modified_amount) end
    return math.max(0, ctx.amount or self.base or 1)
end
function native:addEvent(event) self.events[#self.events + 1] = event end
for _, field in ipairs({"LimitScope", "MaxUsageLimit", "HistoryKey", "HolderSelector",
    "N", "ResponseOrUse", "ExpandPile", "TargetMode", "TargetEffectMode", "WillThrowSelectedCards"}) do
    native["set" .. field] = function(self, value) self[field] = value end
end
function native:setTargetTipRules(value) self.tip = value; return true end
local function constructor(kind)
    return function(name, frequency)
        return setmetatable({name = name, kind = kind, frequency = frequency, events = {}}, {__index = native})
    end
end
for _, kind in ipairs({"Trigger", "ViewAs", "Distance", "MaxCards", "TargetMod", "AttackRange"}) do
    sgs["Lua" .. kind .. "SkillV2"] = constructor(kind)
end
sgs.SkillList = setmetatable({append = function(self, item) self[#self + 1] = item end}, {
    __call = function(self) return setmetatable({}, {__index = self}) end,
})
local registered, registrationCount = {}, 0
sgs.Sanguosha = {
    getSkill = function(self, name) return registered[name] end,
    addSkills = function(self, skills)
        registrationCount = registrationCount + 1
        for _, skill in ipairs(skills) do registered[skill.name] = skill end
    end,
}
function sgs.SkillInstanceKey(name, id) return {skillName = name, instanceID = id} end
function sgs.SkillInstanceRef(owner, key) return {ownerObjectName = owner, key = key} end
sgs.qlist = ipairs
function sgs.Package(name)
    return {name = name, relations = {}, insertRelatedSkills = function(self, parent, child)
        self.relations[#self.relations + 1] = {parent, child}
    end}
end
function sgs.General(extension, name)
    local general = {skills = {}, addSkill = function(self, name) self.skills[#self.skills + 1] = name end}
    extension.general = general
    return general
end
function sgs.AddTranslationEntry(key, value)
    sgs.translations = sgs.translations or {}
    sgs.translations[key] = value
end
-- Target-tip encoding uses the real bundled encoder.
package.path = package.path .. ";./lua/lib/?.lua"
dofile("lua/sgs_ex.lua")
local Assembly = require("lua.skill_assembly")

local callback = function() return false end
local usageRef = function(self, ctx) return ctx.activationRef end
local eventList = {sgs.Damaged}
local spec = {name = "composite", effects = {
    {id = "hand", kind = "maxcards", base_amount = 2, correct_func = function() return true end},
    {id = "draw", kind = "trigger", events = eventList, base_amount = 1,
        on_effect = callback, limit_scope = sgs.Skill_Limit_Turn, max_usage_limit = 2,
        get_usage_ref = usageRef},
}}
local a = Assembly.new(spec)
eventList[1] = 999
spec.effects[2].base_amount = 999
local root = a:createSkill()
equal(root:objectName(), "composite", "trigger chosen over earlier correction")
equal(root.events[1], sgs.Damaged, "spec snapshots nested tables")
equal(root.base, 1)
equal(root.on_effect, callback)
equal(root.get_usage_ref, usageRef)
equal(root.LimitScope, sgs.Skill_Limit_Turn)
equal(root.MaxUsageLimit, 2)
equal(a:createSkill(), root, "creation idempotent")
equal(a:effectName("hand"), "#composite__hand")
rejects(function() a:addEffect("trigger", {id = "late"}) end, "cannot add effects")
local package = sgs.Package("test")
local general = sgs.General(package, "general")
a:register(package, general)
a:register(package, general)
equal(registrationCount, 1)
equal(#package.relations, 1)
equal(package.relations[1][1], "composite")
equal(package.relations[1][2], "#composite__hand")
equal(general.skills[1], "composite")
equal(#general.skills, 1)
rejects(function() a:register(sgs.Package("other")) end, "another package")
local duplicate = Assembly.create {name = "composite", effects = {{id = "other", kind = "trigger"}}}
rejects(function() duplicate:register(package) end, "already registered")
equal(registrationCount, 1, "collision checked before any registration")

local b = Assembly.new {name = "action"}
b:addEffect("trigger", {id = "refresh", events = sgs.EventPhaseStart})
b:addEffect("active", {id = "activate", base_amount = 3, history_key = "ActionCard",
    limit_scope = sgs.Skill_Limit_Turn, max_usage_limit = 1,
    get_usage_ref = usageRef, on_effect = callback})
equal(b:createSkill().kind, "ViewAs")
equal(b:effectName("refresh"), "#action__refresh")
equal(b:effectName("activate"), "action")
equal(b:createSkill().HistoryKey, "ActionCard")
equal(b:createSkill().get_usage_ref, usageRef)
equal(b:createSkill().on_effect, callback)
equal(b:createSkill().LimitScope, sgs.Skill_Limit_Turn)
local kinds = {"distance", "maxcards", "targetmod", "atkrange", "viewas"}
for _, kind in ipairs(kinds) do
    local c = Assembly.create {name = "only_" .. kind, effects = {{id = "main", kind = kind, base_amount = 4}}}
    equal(c:createSkill().base, 4)
    equal(c:effectName("main"), "only_" .. kind)
end

rejects(function() Assembly.new {name = "bad#1"} end, "requires an ASCII")
rejects(function() Assembly.create {name = "empty"} end, "at least one effect")
rejects(function() Assembly.new {name = "bad", effects = {[2] = {id = "x", kind = "trigger"}}} end, "dense array")
rejects(function() Assembly.new {name = "bad", effects = {foo = {id = "x", kind = "trigger"}}} end, "dense array")
rejects(function() Assembly.new {name = "bad", base_amount = 3} end, "unknown assembly field")
rejects(function() Assembly.new {name = "bad", effects = {{id = "x", kind = "legacy"}}} end, "unsupported")
rejects(function() Assembly.new {name = "bad", effects = {{id = "x", kind = "trigger"}, {id = "x", kind = "maxcards"}}} end, "duplicate effect")
rejects(function() Assembly.new {name = "bad", effects = {{id = "x", kind = "active"}, {id = "y", kind = "viewas"}}} end, "only one active")
for _, forbidden in ipairs({{name = "escape"}, {global = true}, {holder_selector = sgs.CorrectSkill_System}, {view_as_skill = {}}, {target_tip = callback}}) do
    forbidden.id, forbidden.kind = "x", "trigger"
    local ok = pcall(function() Assembly.new {name = "bad", effects = {forbidden}} end)
    equal(ok, false, "unsafe effect rejected")
end
rejects(function() a:effectName("unknown") end, "unknown effect")

-- Reference lookup uses a fixture of native direct-helper query results.
-- There is no mirrored acquire/detach dispatcher in this test.
local function owner(name)
    return {
        name = name, roots = {[11] = true, [22] = true},
        children = {[11] = {sgs.SkillInstanceKey("#composite__hand", 31)},
                    [22] = {sgs.SkillInstanceKey("#composite__hand", 42)}},
        objectName = function(self) return self.name end,
        hasSkillInstance = function(self, skill, id) return skill == "composite" and self.roots[id] == true end,
        getChildSkillInstanceKeys = function(self, key) return self.children[key.instanceID] or {} end,
    }
end
local alice, bob = owner("alice"), owner("bob")
local first = a:effectRef(alice, 11, "hand")
local second = a:effectRef(alice, 22, "hand")
equal(first.key.instanceID, 31)
equal(second.key.instanceID, 42)
equal(first.ownerObjectName, "alice")
equal(a:effectRef(bob, 11, "hand").ownerObjectName, "bob", "owner participates in identity")
equal(a:effectRef(alice, 11, "draw").key.instanceID, 11)
equal(a:effectRef(alice, 99, "hand"), nil)
equal(a:effectRef(nil, 11, "hand"), nil)
equal(a:effectRef(alice, 0, "hand"), nil)
equal(a:effectRef(alice, 1.5, "hand"), nil)
alice.roots[11] = nil -- native post-loss snapshot, even if stale helper rows remain
equal(a:effectRef(alice, 11, "hand"), nil)
equal(a:effectRef(alice, 22, "hand").key.instanceID, 42)
alice.children[22][2] = sgs.SkillInstanceKey("#composite__hand", 43)
equal(a:effectRef(alice, 22, "hand"), nil, "ambiguous relation fails closed")

-- Load the actual opt-in package and invoke its callbacks with distinct amounts.
local example = dofile("lua/skill_assembly_example.lua")
local draw = registered.assembly_resilience
local hand = registered["#assembly_resilience__hand_limit"]
equal(example.general.skills[1], "assembly_resilience")
equal(example.relations[1][2], "#assembly_resilience__hand_limit")
equal(draw.events[1], sgs.Damaged)
local player = {drawn = 0, isAlive = function() return true end,
    hasSkill = function(self, name) return name == "assembly_resilience" end,
    drawCards = function(self, n, reason) self.drawn = self.drawn + n; self.reason = reason end}
local name, decision = draw.can_trigger(draw, sgs.Damaged, {}, player, {})
equal(name, "assembly_resilience")
equal(decision, player)
equal(draw.can_trigger(draw, sgs.Damaged, {}, nil, {}), "")
draw.on_effect(draw, sgs.Damaged, {}, player, {instanceID = 11, amount = 2})
equal(player.drawn, 2, "first root upgraded")
draw.on_effect(draw, sgs.Damaged, {}, player, {instanceID = 22, amount = 1})
equal(player.drawn, 3, "second root keeps its own amount")
draw.on_effect(draw, sgs.Damaged, {}, player, {amount = 2, modified_amount = 0})
equal(player.drawn, 3, "single-resolution zero override respected")
equal(draw.base, 1, "shared base unchanged")
equal(hand.base, 1)
equal(hand.correct_func(hand, {}), true, "native aggregator reads this helper's current amount")
equal(player.reason, "assembly_resilience")
print("skill assembly: " .. checks .. " checks passed (Lua factories; native boundary doubles)")
