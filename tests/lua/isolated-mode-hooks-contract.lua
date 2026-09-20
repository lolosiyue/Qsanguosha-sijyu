-- Static contract fixture for the isolated mode-hook boundary.
-- The focused Lua runner loads this file in a fresh state; no gameplay objects
-- or Engine callbacks are required here.
sgs = {}
dofile("lua/ai/mode-ai.lua")

local function player(id, role, visible)
    return {object_name=id, role=role, role_visible=visible, alive=true,
        controller=id, public_marks={}}
end

local function world(viewer_only)
    return {mode_id="isolated-hooks", custom_roles=true, viewer_only=viewer_only,
        self=player("a", "role_a", true),
        players={player("b", "role_b", true), player("c", "role_c", false)}}
end

-- A viewer request has one O(n) row and declares its compact scope.  Hidden
-- hidden identities stay hidden, but a supplied objective may classify hostility.
sgs.registerModeAI("isolated-hooks", {
    teams={left={"role_a", "role_b"}, right={"role_c"}},
    objective=function(_, target)
        return target == "b" and -2 or 3
    end,
})
local compact = sgs.evaluateModeAI(world(true), true)
assert(compact.relation_scope == "viewer")
assert(compact.relations.a and not compact.relations.b)
assert(compact.error == nil)
assert(compact.relations.a.b == "friend" and compact.relations.a.c == "enemy")
sgs.registerModeAI("unmapped-hidden", {teams={left={"role_a"}, right={"role_c"}}})
local hidden = world(true)
hidden.mode_id = "unmapped-hidden"
assert(sgs.evaluateModeAI(hidden, true).relations.a.c == "unknown")

-- The legacy call still emits the complete relation matrix.
local full = sgs.evaluateModeAI(world(false))
assert(full.relation_scope == "full" and full.relations.b and full.relations.c)
assert(full.error == nil)

-- Query state is read-only, and compact evaluation visits each target once.
local relation_calls, objective_state
sgs.registerModeAI("query-isolation", {
    gameProcess=function(_, state) state.leak = 9 end,
    objective=function(_, _, state) objective_state = state.leak; return 0 end,
    relation=function(_, from, to)
        relation_calls = (relation_calls or 0) + 1
        return from == "a" and to == "b" and "friend" or nil
    end,
})
local isolated = world(true)
isolated.mode_id = "query-isolation"
local isolated_result = sgs.evaluateModeAI(isolated, true)
assert(isolated_result.error == nil and relation_calls == 2 and objective_state == nil)

-- Hook failures are observable classifications and never become guessed allies.
sgs.registerModeAI("isolated-hooks", {
    relation=function() error("private hook failure") end,
})
local failed = sgs.evaluateModeAI(world(true))
assert(failed.error == "relation_hook" and failed.relations.a.b == "unknown")

-- A failed custom intention must not commit its partial state.
local attempts = 0
sgs.registerModeAI("atomic-intention", {
    onIntention=function(_, _, _, level, state)
        attempts = attempts + 1
        state.evidence = level
        error("reject")
    end,
    objective=function(_, _, state)
        return state.evidence
    end,
})
local atomic = world(true)
atomic.mode_id = "atomic-intention"
assert(sgs.updateModeAIIntention(atomic, "b", "a", 4) == "intention_hook")
assert(attempts == 1 and sgs.evaluateModeAI(atomic).objectives.b == 0)

-- Success returns no error; rejected non-finite state never replaces committed evidence.
sgs.registerModeAI("atomic-intention", {
    onIntention=function(_, _, _, level, state)
        state.evidence = level == 5 and math.huge or level
    end,
    objective=function(_, _, state) return state.evidence end,
})
assert(sgs.updateModeAIIntention(atomic, "b", "a", 2) == nil)
assert(sgs.evaluateModeAI(atomic, true).objectives.b == 2)
assert(sgs.updateModeAIIntention(atomic, "b", "a", 5) == "intention_state")
assert(sgs.evaluateModeAI(atomic, true).objectives.b == 2)
-- Multiple deltas from one event share a single commit. The first delta must
-- disappear too when a later hook rejects the event.
sgs.registerModeAI("atomic-batch", {
    onIntention=function(_, _, _, level, state)
        if level == 9 then error("reject later delta") end
        state.evidence = (state.evidence or 0) + level
    end,
    objective=function(_, _, state) return state.evidence or 0 end,
})
local batch = world(true)
batch.mode_id = "atomic-batch"
assert(sgs.updateModeAIIntentions(batch, {{from="b", to="a", level=1},
    {from="c", to="a", level=2}}) == nil)
assert(sgs.evaluateModeAI(batch, true).objectives.b == 3)
assert(sgs.updateModeAIIntentions(batch, {{from="b", to="a", level=1},
    {from="c", to="a", level=9}}) == "intention_hook")
assert(sgs.evaluateModeAI(batch, true).objectives.b == 3)
assert(sgs.updateModeAIIntentions(batch, {[2]={from="b", to="a", level=1}}) == "intention_input")
assert(sgs.updateModeAIIntentions(batch, {{from="b", to="a", level=1, hidden="secret"}}) == "intention_input")
assert(sgs.updateModeAIIntentions(batch, {{from="missing", to="a", level=1}}) == "intention_input")
assert(sgs.updateModeAIIntentions(batch, {{from="b", to="a", level=math.huge}}) == "intention_input")
assert(sgs.evaluateModeAI(batch, true).objectives.b == 3)
-- Preparing may exhaust a host budget after returning its token. The mind stays
-- unchanged until the host explicitly commits with its instruction hook stopped.
local prepared = sgs.prepareModeAIIntentions(batch, {{from="b", to="a", level=1}})
assert(type(prepared) == "table" and next(prepared) == nil)
assert(sgs.evaluateModeAI(batch, true).objectives.b == 3)
assert(sgs.commitModeAIIntentions({shadow={evidence=5}}) == "intention_prepared")
assert(sgs.commitModeAIIntentions(prepared) == nil)
assert(sgs.evaluateModeAI(batch, true).objectives.b == 4)
assert(sgs.commitModeAIIntentions(prepared) == "intention_prepared")

local stale = sgs.prepareModeAIIntentions(batch, {{from="b", to="a", level=1}})
sgs.registerModeAI("change-generation", {})
assert(sgs.commitModeAIIntentions(stale) == "intention_stale")
assert(sgs.evaluateModeAI(batch, true).objectives.b == 4)
local abandoned = sgs.prepareModeAIIntentions(batch, {{from="b", to="a", level=1}})
assert(sgs.prepareModeAIIntentions(batch, {{from="b", to="a", level=9}}) == "intention_hook")
assert(sgs.commitModeAIIntentions(abandoned) == "intention_prepared")
assert(sgs.evaluateModeAI(batch, true).objectives.b == 4)

local captured_shadow
sgs.registerModeAI("prepared-reference", {
    onIntention=function(_, _, _, level, state) state.evidence = level; captured_shadow = state end,
    objective=function(_, _, state) return state.evidence or 0 end,
})
local detached = world(true)
detached.mode_id = "prepared-reference"
local detached_token = sgs.prepareModeAIIntentions(detached, {{from="b", to="a", level=2}})
captured_shadow.evidence = 5
detached_token.shadow = {evidence=5} -- Token fields are never used as commit data.
assert(sgs.commitModeAIIntentions(detached_token) == nil)
assert(sgs.evaluateModeAI(detached, true).objectives.b == 2)
sgs.registerModeAI("non-finite-query", {
    objective=function() return math.huge end,
    gameProcess=function() return 0 / 0 end,
})
local invalid = world(true)
invalid.mode_id = "non-finite-query"
local invalid_result = sgs.evaluateModeAI(invalid, true)
assert(invalid_result.objectives.b == 0 and invalid_result.game_process == 0)
assert(invalid_result.relations.a.b == "unknown")

-- Native startup explicitly admits built-in identity modes. No SmartAI globals
-- or native players are present in this fixture.
assert(sgs.registerStandardModeAI("05p", true, false))
local identity = {mode_id="05p", custom_roles=false,
    self=player("lord", "lord", true),
    players={player("hidden", "rebel", false), player("ally", "loyalist", true)},
    alive_player_order={"lord", "hidden", "ally"}}
identity.self.hp, identity.self.handcard_count = 4, 4
identity.players[1].hp, identity.players[1].handcard_count = 3, 3
identity.players[2].hp, identity.players[2].handcard_count = 4, 3
local first = sgs.evaluateModeAI(identity, true)
assert(first.managed and first.relations.lord.ally == "friend")
assert(first.relations.lord.hidden == "unknown")
-- Poisoning an invisible role cannot change the decision or the inferred mind.
identity.players[1].role = "loyalist"
assert(sgs.evaluateModeAI(identity, true).relations.lord.hidden == "unknown")
assert(sgs.updateModeAIIntention(identity, "hidden", "lord", 40) == nil)
assert(sgs.evaluateModeAI(identity, true).relations.lord.hidden == "enemy")

local other_viewer = {mode_id="05p", custom_roles=false,
    self=player("ally", "loyalist", true),
    players={player("lord", "lord", true), player("hidden", "rebel", false)},
    alive_player_order={"lord", "hidden", "ally"}}
assert(sgs.evaluateModeAI(other_viewer, true).relations.ally.hidden == "unknown")
-- An authoritative reveal overrides the previous hostile inference immediately.
identity.players[1].role_visible = true
assert(sgs.evaluateModeAI(identity, true).relations.lord.hidden == "friend")

-- Equal renegade roles never create a shared side, and the last duel is hostile.
local renegade = {mode_id="05p", custom_roles=false,
    self=player("one", "renegade", true), players={player("two", "renegade", true)},
    alive_player_order={"one", "two"}}
assert(sgs.evaluateModeAI(renegade, true).relations.one.two == "enemy")

assert(sgs.registerStandardModeAI("03_1v2", false, false))
local fixed = {mode_id="03_1v2", custom_roles=false,
    self=player("farmer", "rebel", true),
    players={player("partner", "rebel", true), player("landlord", "lord", true)}}
local fixed_result = sgs.evaluateModeAI(fixed, true)
assert(fixed_result.relations.farmer.partner == "friend")
assert(fixed_result.relations.farmer.landlord == "enemy")
assert(not sgs.registerStandardModeAI("scenario-with-standard-roles", false, false))
assert(not sgs.registerStandardModeAI("08p", true, true))

-- The compatibility wrapper leaves explicitly legacy standard modes alone.
local legacy_room = {getMode=function() return "05p" end}
assert(sgs.modeAIEnabled(legacy_room) == false)
-- Author registration replaces the complete built-in policy, including its mind.
sgs.registerModeAI("05p", {relation=function() return "neutral" end})
assert(not sgs.registerStandardModeAI("05p", true, false))
assert(sgs.modeAIEnabled(legacy_room) == true)
assert(sgs.evaluateModeAI(identity, true).relations.lord.hidden == "neutral")

-- A mixed-route legacy filter must not submit a second intention after the
-- native event pipeline already committed it. These are Lua compatibility mocks.
sgs.registerModeAI("mixed-bridge", {
    onIntention=function(_, _, _, level, state) state.evidence = (state.evidence or 0) + level end,
    objective=function(_, _, state) return state.evidence or 0 end,
})
local bridge_world = world(true)
bridge_world.mode_id = "mixed-bridge"
local bridge_room = {getMode=function() return "mixed-bridge" end}
local bridge_from = {objectName=function() return "b" end}
local bridge_to = {objectName=function() return "a" end}
local refreshes, old_calls = 0, 0
local observer = {room=bridge_room, player=bridge_to,
    updatePlayers=function() refreshes = refreshes + 1 end}
sgs.ais, current_self, global_room = {observer}, observer, bridge_room
sgs.modeAIWorld = function()
    -- Match the native world's policy projection after each intention update.
    bridge_world.mode_policy = sgs.evaluateModeAI(bridge_world, true)
    return bridge_world
end
sgs.updateIntention = function() old_calls = old_calls + 1 end
sgs.installModeAI({})
assert(sgs.updateModeAIIntentions(bridge_world, {{from="b", to="a", level=1}}) == nil)
sgs.modeAIUsesIsolatedEvents = true
sgs.updateIntention(bridge_from, bridge_to, 1)
assert(sgs.evaluateModeAI(bridge_world, true).objectives.b == 1)
assert(refreshes == 1 and old_calls == 0)
sgs.modeAIUsesIsolatedEvents = false
sgs.updateIntention(bridge_from, bridge_to, 1)
assert(sgs.evaluateModeAI(bridge_world, true).objectives.b == 2)

return true
