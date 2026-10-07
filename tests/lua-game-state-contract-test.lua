local Contract = dofile("lua/game-state-contract.lua")

local function audit(author, scope)
    return {
        author = author,
        scope = scope,
        mode = "managed-only",
        scriptCoverage = "unknown",
        unmanagedClosures = "unsupported",
        mutableUpvalues = "unsupported",
        externalSideEffects = "unsupported"
    }
end

local function mustFail(value, message)
    local result, err = value()
    assert(result == nil or result == false, message or "expected operation to fail")
    assert(type(err) == "string" and #err > 0, "failure should explain its reason")
end

local manager = Contract.new()
assert(manager:registerManagedProvider({
    id = "lua.managed",
    version = 1,
    audit = audit("contract-test", "ordinary skill instance data only")
}))
assert(manager:registerManagedProvider({
    id = "ai.managed",
    version = 1,
    audit = audit("contract-test", "AI decision helper data only")
}))

local capabilities = assert(manager:capabilities())
assert(capabilities.coverage == "registered-providers-only")
assert(capabilities.wholeLuaState == false)
assert(capabilities.luaGlobals == "unknown")
assert(capabilities.unmanagedClosures == "unsupported")
assert(#capabilities.providers == 2)

local world = assert(manager:newWorld("logical-room-17"))
local sameRoomOtherRuntime = assert(manager:newWorld("logical-room-17"))
local otherRoom = assert(manager:newWorld("logical-room-18"))

local playerRef = assert(Contract.ref("player", "player:alice"))
local cardRef = assert(Contract.ref("card", "card:91"))
local skillRef = assert(Contract.ref("skill", "skill:contract-test"))
local skillPayload = {
    phase = "draw",
    uses = { 2, 5, 8 },
    source = playerRef,
    card = cardRef,
    skill = skillRef
}
assert(world:setManaged("lua.managed", "skill", "skill-instance:alice:foo", "payload", skillPayload))
assert(world:setManaged("ai.managed", "player", "player:alice", "plan", {
    target = playerRef,
    scores = { 0.25, 0.75 }
}))

-- Inputs and getter results are detached from provider-owned state.
skillPayload.phase = "mutated-after-write"
local exposed = assert(world:getManaged("lua.managed", "skill", "skill-instance:alice:foo", "payload"))
assert(exposed.phase == "draw")
exposed.uses[1] = 999
assert(world:getManaged("lua.managed", "skill", "skill-instance:alice:foo", "payload").uses[1] == 2)

local snapshot = assert(world:capture())
assert(snapshot.contract == "qsanguosha.managed-state")
assert(snapshot.schemaVersion == 1)
assert(snapshot.worldId == "logical-room-17")
assert(#snapshot.providers == 2)
assert(snapshot.providers[1].id == "ai.managed")
assert(snapshot.providers[2].id == "lua.managed")

-- Mutating an exported snapshot cannot mutate the active world.
snapshot.providers[2].state.owners.skill["skill-instance:alice:foo"].payload.phase = "snapshot mutation"
assert(world:getManaged("lua.managed", "skill", "skill-instance:alice:foo", "payload").phase == "draw")
snapshot = assert(world:capture())

local candidateIndex = {
    player = { ["player:alice"] = true },
    card = { ["card:91"] = true },
    skill = {
        ["skill:contract-test"] = true,
        ["skill-instance:alice:foo"] = true
    }
}

local aliasedProviders = assert(world:capture())
aliasedProviders.providers[2].state.owners = aliasedProviders.providers[1].state.owners
mustFail(function() return manager:prepareRestore(world, aliasedProviders, candidateIndex) end,
    "aliases shared across provider payloads must also be rejected")

local malformedOwners = assert(world:capture())
malformedOwners.providers[1].state.owners.player["player:alice"] = 17
mustFail(function() return manager:prepareRestore(world, malformedOwners, candidateIndex) end,
    "managed owner payloads must be objects")

local unresolvedOwner = assert(world:capture())
unresolvedOwner.providers[1].state.owners.player["player:ghost"] = { plan = { score = 1 } }
mustFail(function() return manager:prepareRestore(world, unresolvedOwner, candidateIndex) end,
    "stable owner IDs must resolve in the detached candidate")

mustFail(function()
    return world:setState("lua.managed", { owners = { player = { ["player:alice"] = 17 } } })
end, "setState must enforce the managed owner schema")
mustFail(function()
    return world:setManaged("lua.managed", "ai", "ai:alice", "plan", {})
end, "managed owner IDs must use typed player/card/skill identities")

-- All providers and stable references validate before a candidate is returned.
local unresolvedIndex = {
    player = {},
    card = { ["card:91"] = true },
    skill = { ["skill:contract-test"] = true }
}
mustFail(function() return manager:prepareRestore(world, snapshot, unresolvedIndex) end,
    "unresolved stable references must reject the candidate")

-- Preparing a valid candidate does not publish it; a failed cross-runtime
-- activation cannot affect either active world.
local candidate = assert(manager:prepareRestore(world, snapshot, candidateIndex))
local candidateView = assert(candidate:getManaged("lua.managed", "skill", "skill-instance:alice:foo", "payload"))
candidateView.phase = "mutated-candidate-view"
assert(candidate:getManaged("lua.managed", "skill", "skill-instance:alice:foo", "payload").phase == "draw")
mustFail(function() return manager:activate(sameRoomOtherRuntime, candidate) end,
    "candidate must be tied to its exact runtime world")
assert(sameRoomOtherRuntime:getManaged("lua.managed", "skill", "skill-instance:alice:foo", "payload") == nil)
assert(world:getManaged("lua.managed", "skill", "skill-instance:alice:foo", "payload").phase == "draw")

-- Failure during validation leaves current state unchanged.
assert(world:setManaged("lua.managed", "skill", "skill-instance:alice:foo", "payload", { phase = "live-before-failure" }))
local liveBeforeFailure = assert(world:getManaged("lua.managed", "skill", "skill-instance:alice:foo", "payload"))
mustFail(function() return manager:prepareRestore(world, snapshot, unresolvedIndex) end)
assert(world:getManaged("lua.managed", "skill", "skill-instance:alice:foo", "payload").phase == liveBeforeFailure.phase)

-- A valid restore replaces both provider namespaces in one activation.
candidate = assert(manager:prepareRestore(world, snapshot, candidateIndex))
assert(world:getManaged("lua.managed", "skill", "skill-instance:alice:foo", "payload").phase == "live-before-failure")
assert(manager:activate(world, candidate))
assert(world:getManaged("lua.managed", "skill", "skill-instance:alice:foo", "payload").phase == "draw")
assert(world:getManaged("ai.managed", "player", "player:alice", "plan").scores[2] == 0.75)

-- The same saved state can be restored repeatedly after new live changes.
assert(world:setManaged("ai.managed", "player", "player:alice", "plan", { target = playerRef, scores = { 9 } }))
candidate = assert(manager:prepareRestore(world, snapshot, candidateIndex))
assert(manager:activate(world, candidate))
assert(world:getManaged("ai.managed", "player", "player:alice", "plan").scores[2] == 0.75)

-- An intervening live write invalidates a previously prepared candidate.
candidate = assert(manager:prepareRestore(world, snapshot, candidateIndex))
assert(world:setManaged("lua.managed", "skill", "skill-instance:alice:foo", "phase", "later"))
mustFail(function() return manager:activate(world, candidate) end, "stale candidates must not overwrite later state")
assert(world:getManaged("lua.managed", "skill", "skill-instance:alice:foo", "phase") == "later")

-- Runtime instances are isolated even when they share a logical room id.
assert(sameRoomOtherRuntime:setManaged("lua.managed", "skill", "skill-instance:alice:foo", "phase", "other-runtime"))
assert(world:getManaged("lua.managed", "skill", "skill-instance:alice:foo", "phase") == "later")
mustFail(function() return manager:prepareRestore(otherRoom, snapshot, candidateIndex) end,
    "a different logical room cannot consume this snapshot")

-- Unsupported Lua values, cycles, aliases, metatables, and sparse arrays are
-- rejected before they can enter managed state.
mustFail(function()
    return world:setManaged("lua.managed", "skill", "skill-instance:x", "fn", function() end)
end, "functions and closure state are unsupported")
local cyclic = {}
cyclic.self = cyclic
mustFail(function() return world:setManaged("lua.managed", "skill", "skill-instance:x", "cycle", cyclic) end)
local shared = { value = 1 }
mustFail(function()
    return world:setManaged("lua.managed", "skill", "skill-instance:x", "alias", { left = shared, right = shared })
end)
mustFail(function()
    return world:setManaged("lua.managed", "skill", "skill-instance:x", "meta", setmetatable({}, {}))
end)
mustFail(function()
    return world:setManaged("lua.managed", "skill", "skill-instance:x", "sparse", { [1] = "a", [3] = "c" })
end)
mustFail(function()
    return world:setManaged("lua.managed", "skill", "skill-instance:x", "mixed", { [1] = "a", name = "b" })
end)
mustFail(function()
    return world:setManaged("lua.managed", "skill", "skill-instance:x", "nan", 0 / 0)
end)

-- Provider version, completeness, and audit metadata are part of the contract.
local missingProviderSnapshot = assert(world:capture())
table.remove(missingProviderSnapshot.providers, 1)
mustFail(function() return manager:prepareRestore(world, missingProviderSnapshot, candidateIndex) end)
local auditManager = Contract.new()
mustFail(function()
    return auditManager:registerManagedProvider({
        id = "unchecked",
        version = 1,
        audit = { author = "test", scope = "partial", mode = "managed-only" }
    })
end, "provider registration must not imply broad Lua coverage")

local olderManager = Contract.new()
assert(olderManager:registerManagedProvider({
    id = "lua.managed",
    version = 2,
    audit = audit("contract-test", "ordinary skill instance data only")
}))
assert(olderManager:registerManagedProvider({
    id = "ai.managed",
    version = 1,
    audit = audit("contract-test", "AI decision helper data only")
}))
local versionWorld = assert(olderManager:newWorld("logical-room-17"))
mustFail(function() return olderManager:prepareRestore(versionWorld, snapshot, candidateIndex) end,
    "provider version mismatch must reject the snapshot")

print("lua game state contract tests passed")
