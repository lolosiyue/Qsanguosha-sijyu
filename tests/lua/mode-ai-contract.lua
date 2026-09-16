-- Pure-value contract fixtures. Run in a fresh Lua state; no gameplay is started.
sgs = {}
dofile("lua/ai/mode-ai.lua")
local function player(id, role, visible)
    return {object_name=id, role=role, role_visible=visible, role_revealed=visible,
        controller=id, alive=true, public_marks={}}
end
local function world(mode, viewer)
    return {mode_id=mode, revision="1", custom_roles=true,
        self=player(viewer or "a", "role_a", true),
        players={player("b", "role_b", true), player("c", "role_c", true)}}
end
sgs.registerModeAI("fixture", {teams={allies={"role_a", "role_b"}, others={"role_c"}}})
local w = world("fixture")
local result = sgs.evaluateModeAI(w)
assert(result.managed and result.predictable)
assert(result.relations.a.b == "friend" and result.relations.a.c == "enemy")
assert(result.objectives.a == -3 and result.objectives.b < 0 and result.objectives.c > 0)

-- Even a malformed caller including a role string cannot bypass visibility.
w.players[1].role_visible = false
result = sgs.evaluateModeAI(w)
assert(result.relations.a.b == "unknown" and result.objectives.b == 0 and not result.predictable)
w.players[1].role_visible = true
sgs.registerModeAI("different", {teams={left={"role_a"}, right={"role_b", "role_c"}}})
assert(sgs.evaluateModeAI(world("different")).relations.a.b == "enemy")
assert(sgs.evaluateModeAI(world("fixture")).relations.a.b == "friend")

sgs.registerModeAI("directional", {
    relation=function(ctx, from, to)
        if from == "a" and to == "b" then return "neutral" end
        if from == "b" and to == "a" then return "enemy" end
    end,
    objective=function(ctx, to) if to == "c" then return 0/0 end end,
})
result = sgs.evaluateModeAI(world("directional"))
assert(result.relations.a.b == "neutral" and result.relations.b.a == "enemy")
assert(result.relations.a.c == "unknown" and result.objectives.c == 0)
sgs.registerModeAI("failure", {relation=function() error("fixture failure") end})
assert(sgs.evaluateModeAI(world("failure")).relations.a.b == "unknown")
assert(sgs.evaluateModeAI(world("missing")).managed)
local old = world("missing")
old.custom_roles = false
assert(not sgs.evaluateModeAI(old).managed)
assert(not pcall(sgs.registerModeAI, "bad", {teams={one={"x"}, two={"x"}}}))

-- Beliefs are viewer-local. This fixture uses a mutation solely to expose aliasing.
sgs.registerModeAI("mind", {gameProcess=function(ctx, state)
    state.count = (state.count or 0) + 1
    return state.count, "neutral"
end})
assert(sgs.evaluateModeAI(world("mind", "first")).game_process == 1)
assert(sgs.evaluateModeAI(world("mind", "second")).game_process == 1)
assert(sgs.evaluateModeAI(world("mind", "first")).game_process == 2)

-- Same mode, different observer identities; missing hooks inherit mode defaults.
sgs.registerModeAI("roles", {
    objective=function() return -2 end,
    gameProcess=function() return 7, "mode-default" end,
    roles={role_a={objective=function(ctx, target) return target == "b" and 3 or 0 end}},
})
w = world("roles")
result = sgs.evaluateModeAI(w)
assert(result.relations.a.b == "enemy" and result.objectives.b == 3)
assert(result.relations.a.c == "neutral" and result.objectives.c == 0)
assert(result.relations.b.a == "unknown") -- A's score does not expose B's perspective.
assert(result.game_process == 7 and result.process_label == "mode-default")
w.self.role = "role_b"
assert(sgs.evaluateModeAI(w).relations.a.b == "friend")
w.self.role = "role_a"
w.self.role_visible = false
assert(sgs.evaluateModeAI(w).relations.a.b == "friend") -- No hidden-role dispatch.
assert(not pcall(sgs.registerModeAI, "bad-roles", {roles={role_a={objective=42}}}))

-- Target matchups belong to the mode. Role-specific entries fall back only when absent.
local matchups = {role_b=-2, role_c=3}
sgs.registerModeAI("matchups", {objectiveByRole=matchups,
    roles={role_a={objectiveByRole={role_c=-4}}}})
matchups.role_b = 5 -- Registered maps are snapshots, not live mutable tables.
local matchup_world = world("matchups")
result = sgs.evaluateModeAI(matchup_world)
assert(result.objectives.b == -2 and result.objectives.c == -4)
assert(result.relations.a.b == "friend" and result.relations.a.c == "friend")
matchup_world.self.role = "role_b"
assert(sgs.evaluateModeAI(matchup_world).objectives.c == 3)
matchup_world.players[1].role_visible = false
result = sgs.evaluateModeAI(matchup_world)
assert(result.objectives.b == 0 and result.relations.a.b == "unknown")
matchup_world.players[1].role_visible = true
matchup_world.players[1].role = "unconfigured"
assert(sgs.evaluateModeAI(matchup_world).relations.a.b == "unknown")
sgs.registerModeAI("explicit-nil", {objectiveByRole={role_b=5},
    roles={role_a={objectiveByRole={role_b=function() return nil end, role_c=0}}}})
result = sgs.evaluateModeAI(world("explicit-nil"))
assert(result.relations.a.b == "unknown" and result.relations.a.c == "neutral")
assert(not pcall(sgs.registerModeAI, "bad-matchup", {objectiveByRole={role_b=6}}))

local process_calls = 0
sgs.registerModeAI("process-input", {
    gameProcess=function() return -3, "mode" end,
    objectiveByRole={role_b=function(ctx, target, state, process)
        assert(process.label == "role")
        return process.value
    end},
    roles={role_a={gameProcess=function()
        process_calls = process_calls + 1
        return 4, "role"
    end}},
})
result = sgs.evaluateModeAI(world("process-input"))
assert(process_calls == 1 and result.game_process == 4 and result.objectives.b == 4)

-- Explicit relation (including unknown), teams and controller links take precedence.
sgs.registerModeAI("priority", {
    teams={one={"role_a", "role_c"}},
    relation=function(ctx, from, to) if to == "b" then return "unknown" end end,
    objective=function() return 4 end,
})
result = sgs.evaluateModeAI(world("priority"))
assert(result.relations.a.b == "unknown" and result.relations.a.c == "friend")
assert(result.objectives.b == 4 and result.objectives.c == 4)
sgs.registerModeAI("bad-score", {objective=function() return 6 end})
result = sgs.evaluateModeAI(world("bad-score"))
assert(result.relations.a.b == "unknown" and result.objectives.b == 0)
sgs.registerModeAI("bad-relation", {relation=function() error("relation") end,
    objective=function() return 4 end})
assert(sgs.evaluateModeAI(world("bad-relation")).relations.a.b == "unknown")

local function remember(ctx, from, to, level, state)
    state.last = level
end
local remembered = {onIntention=remember, objective=function(ctx, target, state) return state.last end}
sgs.registerModeAI("events", {objective=function() return -2 end, roles={role_a=remembered}})
local first, second = world("events", "first"), world("events", "second")
sgs.updateModeAIIntention(first, "b", "first", 3)
assert(sgs.evaluateModeAI(first).objectives.b == 3)
assert(sgs.evaluateModeAI(second).objectives.b == 0)
second.self.role = "role_b"
sgs.updateModeAIIntention(second, "b", "second", 4)
assert(sgs.evaluateModeAI(second).objectives.b == -2)
sgs.registerModeAI("events", {roles={role_a=remembered}})
assert(sgs.evaluateModeAI(first).objectives.b == 0) -- Re-registration clears old beliefs.

-- Load the module in a separate Room-like environment; same mode/player IDs do not alias.
local separate = setmetatable({sgs={}}, {__index=_G})
local loader = assert(loadfile("lua/ai/mode-ai.lua"))
if setfenv then setfenv(loader, separate)
else loader = assert(loadfile("lua/ai/mode-ai.lua", "t", separate)) end
loader()
separate.sgs.registerModeAI("events", {roles={role_a=remembered}})
sgs.updateModeAIIntention(first, "b", "first", 2)
assert(sgs.evaluateModeAI(first).objectives.b == 2)
assert(separate.sgs.evaluateModeAI(first).objectives.b == 0)

-- Intention identity routing is registered data, including entirely new identities.
local evidence_rules = {
    ignoreActors={"lord"},
    rules={{target="role_d", update=function(ctx, event, values)
        values.evidence = (values.evidence or 0) + event.level
    end}},
    infer={
        {role="role_e", test=function(ctx, event, values) return (values.evidence or 0) >= 2 end},
        {role="role_f", test=function() return true end},
    },
}
local function evidence_score(ctx, target, state)
    local inferred = state.inferred_roles and state.inferred_roles[target]
    return inferred == "role_e" and 4 or inferred == "role_f" and -4 or 0
end
sgs.registerModeAI("rule-data", {intentions=evidence_rules, objective=evidence_score,
    roles={role_b={intentions={}}}})
-- Registration owns its definition; later edits require explicit re-registration.
evidence_rules.rules[1].target = "different"
local rule_world = world("rule-data")
rule_world.players[1].role = "role_d"
rule_world.players[2].role, rule_world.players[2].role_visible = "lord", false
sgs.updateModeAIIntention(rule_world, "c", "b", 2)
assert(sgs.evaluateModeAI(rule_world).objectives.c == 4) -- Hidden 'lord' does not suppress the event.
assert(sgs.evaluateModeAI(rule_world).relations.a.c == "enemy")
local hidden_target = world("rule-data", "hidden-target")
hidden_target.players[1].role, hidden_target.players[1].role_visible = "role_d", false
sgs.updateModeAIIntention(hidden_target, "c", "b", 2)
assert(sgs.evaluateModeAI(hidden_target).objectives.c == 0)
local override_world = world("rule-data", "override")
override_world.self.role, override_world.players[1].role = "role_b", "role_d"
sgs.updateModeAIIntention(override_world, "c", "b", 2)
assert(sgs.evaluateModeAI(override_world).objectives.c == 0) -- Empty role rules override mode rules.
local ignored_world = world("rule-data", "ignored")
ignored_world.players[1].role, ignored_world.players[2].role = "role_d", "lord"
sgs.updateModeAIIntention(ignored_world, "c", "b", 2)
assert(sgs.evaluateModeAI(ignored_world).objectives.c == 0)
sgs.registerModeAI("different-rules", {intentions={}, objective=evidence_score})
rule_world.mode_id = "different-rules"
sgs.updateModeAIIntention(rule_world, "c", "b", 2)
assert(sgs.evaluateModeAI(rule_world).objectives.c == 0)

-- Matching rules accumulate in declaration order; actor selectors use prior estimates.
sgs.registerModeAI("rule-order", {intentions={
    rules={
        {target="role_b", update=function(ctx, event, values) values.value = (values.value or 0) + 1 end},
        {actor="role_e", target="role_b", update=function(ctx, event, values) values.value = values.value * 2 end},
    },
    infer={{role="role_e", test=function() return true end}},
}, objective=function(ctx, target, state)
    return state.role_values and state.role_values[target] and state.role_values[target].value or 0
end})
local ordered = world("rule-order")
ordered.players[2].role_visible = false
sgs.updateModeAIIntention(ordered, "c", "b", 1)
assert(sgs.evaluateModeAI(ordered).objectives.c == 1)
sgs.updateModeAIIntention(ordered, "c", "b", 1)
assert(sgs.evaluateModeAI(ordered).objectives.c == 4)

-- Invalid rules fail at registration; callback failure does not commit partial evidence.
assert(not pcall(sgs.registerModeAI, "conflict", {onIntention=remember, intentions={}}))
assert(not pcall(sgs.registerModeAI, "bad-rule", {intentions={rules={{target="role_a", update=2}}}}))
assert(not pcall(sgs.registerModeAI, "sparse-rule", {intentions={rules={[2]={update=remember}}}}))
sgs.registerModeAI("broken-rules", {intentions={rules={{update=function(ctx, event, values)
    values.evidence = 4
    error("fixture rule failure")
end}}}, objective=function(ctx, target, state)
    return state.role_values and state.role_values[target] and state.role_values[target].evidence or 0
end})
local broken = world("broken-rules")
sgs.updateModeAIIntention(broken, "c", "b", 1)
assert(sgs.evaluateModeAI(broken).objectives.c == 0)
sgs.registerModeAI("invalid-inference", {intentions={
    rules={{update=function(ctx, event, values) values.evidence = 4 end}},
    infer={{role="role_e", test=function() return "yes" end}},
}, objective=evidence_score})
local invalid = world("invalid-inference")
invalid.players[2].role_visible = false
sgs.updateModeAIIntention(invalid, "c", "b", 1)
assert(sgs.evaluateModeAI(invalid).objectives.c == 0)
sgs.registerModeAI("nonfinite-evidence", {intentions={
    rules={{update=function(ctx, event, values) values.evidence = math.huge end}},
    infer={{role="role_e", test=function() return true end}},
}, objective=evidence_score})
invalid.mode_id = "nonfinite-evidence"
sgs.updateModeAIIntention(invalid, "c", "b", 1)
assert(sgs.evaluateModeAI(invalid).objectives.c == 0)

-- The optional MODE config supplies matchups; the role tactics know no identities.
local villager_mode = dofile("lua/ai/mode-policies/villager.lua")
sgs.registerModeAI("villagers", villager_mode)
w = world("villagers")
w.self.role, w.self.hp, w.self.handcard_count = "villager", 3, 3
w.players[1].role, w.players[1].hp, w.players[1].handcard_count = "villager", 1, 1
w.players[2].role = "renegade"
result = sgs.evaluateModeAI(w)
assert(result.objectives.b == 2 and result.objectives.c == 5)
assert(result.relations.a.b == "enemy" and result.relations.a.c == "enemy")
w.players[2].role_visible = false
assert(sgs.evaluateModeAI(w).objectives.c == 0)
sgs.updateModeAIIntention(w, "c", "a", 20)
assert(sgs.evaluateModeAI(w).objectives.c == 5) -- Attack on a known villager raises suspicion.
local other = world("villagers", "other")
other.self.role = "villager"
other.players[2].role, other.players[2].role_visible = "renegade", false
assert(sgs.evaluateModeAI(other).objectives.c == 0)
-- The same event with an unknown victim carries different evidence for this observer.
other.players[1].role, other.players[1].role_visible = "villager", false
sgs.updateModeAIIntention(other, "c", "b", 20)
assert(sgs.evaluateModeAI(other).objectives.c == 0)
w.players[2].role, w.players[2].role_visible = "loyalist", true
assert(sgs.evaluateModeAI(w).objectives.c ~= 5) -- Revealed facts override prior estimates.
w.players[1].role, w.players[1].hp = "lord", 4
w.players[2].role, w.players[2].hp = "rebel", 1
result = sgs.evaluateModeAI(w)
assert(result.objectives.b == -2 and result.objectives.c == 3)
w.players[2].hp = 7
result = sgs.evaluateModeAI(w)
assert(result.objectives.b == 3 and result.objectives.c == -1)

-- Add an entirely new target identity by configuring the mode only.
local extended = dofile("lua/ai/mode-policies/villager.lua")
extended.roles.villager.objectiveByRole.role_d = -2
extended.strengthByRole.role_d = {weight=-1}
sgs.registerModeAI("extended-villagers", extended)
local extended_world = world("extended-villagers")
extended_world.self.role, extended_world.self.hp = "villager", 3
extended_world.players[1].role, extended_world.players[1].hp = "lord", 4
extended_world.players[2].role, extended_world.players[2].hp = "role_d", 7
result = sgs.evaluateModeAI(extended_world)
assert(result.game_process == -3 and result.objectives.c == -2 and result.objectives.b == 3)
extended_world.mode_id = "villagers"
result = sgs.evaluateModeAI(extended_world)
assert(result.game_process == 7 and result.relations.a.c == "unknown")
extended_world.mode_id = "extended-villagers"
extended_world.players[2].role_visible = false
result = sgs.evaluateModeAI(extended_world)
assert(result.game_process == 7 and result.relations.a.c == "unknown")

-- A mode can replace the process hook without changing the villager tactic.
local changed_process = dofile("lua/ai/mode-policies/villager.lua")
changed_process.gameProcess = function() return -9, "rebel" end
sgs.registerModeAI("changed-process", changed_process)
extended_world.mode_id = "changed-process"
assert(sgs.evaluateModeAI(extended_world).objectives.b == 3)

-- The isolated facade consumes copied results, never calls a gameplay closure.
dofile("lua/ai/isolated-facades.lua")
w = world("fixture")
w.mode_policy = sgs.evaluateModeAI(w)
local ai = SmartAIView.new({viewer="a", world_view=w})
local ally, enemy = PlayerView.new(w.players[1]), PlayerView.new(w.players[2])
assert(ai:isFriend(ally) and ai:isEnemy(enemy) and ai:objectiveLevel(enemy) == 5)
assert(#ai:getFriends(nil, true) == 1 and #ai:getEnemies() == 1)
w.players[1].controller = "a"
sgs.registerModeAI("fixture", {relation=function() return "enemy" end,
    objective=function() return 5 end})
w.mode_policy = sgs.evaluateModeAI(w)
assert(w.mode_policy.relations.a.b == "friend" and w.mode_policy.objectives.b < 0)
return true
