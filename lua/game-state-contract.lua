-- Managed Lua/AI state contract.
--
-- This module stores only explicitly managed, data-only state. It does not
-- serialize Lua environments, closures, upvalues, userdata, metatables, or
-- external effects. A successful prepareRestore therefore means that every
-- registered provider payload was validated; it does not mean that the whole
-- script runtime is restorable.

local GameStateContract = {}

GameStateContract.CONTRACT_ID = "qsanguosha.managed-state"
GameStateContract.SCHEMA_VERSION = 1

local ManagerMethods = {}
local WorldMethods = {}
local CandidateMethods = {}

local managerData = setmetatable({}, { __mode = "k" })
local worldData = setmetatable({}, { __mode = "k" })
local candidateData = setmetatable({}, { __mode = "k" })
local validateManagedState

local MAX_DEPTH = 128
local MAX_TABLE_ENTRIES = 100000

local function isInteger(value)
    return type(value) == "number" and value == math.floor(value)
end

local function isFiniteNumber(value)
    return type(value) == "number"
        and value == value
        and value ~= math.huge
        and value ~= -math.huge
end

local function isNonEmptyString(value, maxLength)
    return type(value) == "string"
        and #value > 0
        and #value <= (maxLength or 512)
end

local function fail(message)
    return nil, message
end

-- Clones the supported Lua value tree. `seen` is intentionally never cleared:
-- both cycles and aliases are rejected because the wire format is a tree.
local function copyValue(value, seen, depth, refCheck, path)
    local valueType = type(value)
    if valueType == "nil" then
        return fail(path .. ": nil is not a state value")
    elseif valueType == "boolean" or valueType == "string" then
        return value
    elseif valueType == "number" then
        if not isFiniteNumber(value) then
            return fail(path .. ": non-finite number")
        end
        return value
    elseif valueType ~= "table" then
        return fail(path .. ": unsupported Lua value type " .. valueType)
    end

    if depth > MAX_DEPTH then
        return fail(path .. ": maximum state nesting exceeded")
    end
    if getmetatable(value) ~= nil then
        return fail(path .. ": table metatables are unsupported")
    end
    if seen[value] then
        return fail(path .. ": table cycle or alias is unsupported")
    end
    seen[value] = true

    local count = 0
    local maxIndex = 0
    local hasStringKey = false
    local hasNumberKey = false
    local refValue = rawget(value, "$ref")
    for key in pairs(value) do
        count = count + 1
        if count > MAX_TABLE_ENTRIES then
            return fail(path .. ": too many table entries")
        end
        if key == "$ref" then
            -- Checked below as an exact typed reference descriptor.
        elseif type(key) == "string" then
            hasStringKey = true
        elseif type(key) == "number"
            and isInteger(key)
            and key >= 1 then
            hasNumberKey = true
            if key > maxIndex then
                maxIndex = key
            end
        else
            return fail(path .. ": table keys must be strings or dense array indices")
        end
        if hasStringKey and hasNumberKey then
            return fail(path .. ": mixed array and object table is unsupported")
        end
    end

    if refValue ~= nil then
        if count ~= 1 or type(refValue) ~= "table" then
            return fail(path .. ": malformed typed reference")
        end
        if getmetatable(refValue) ~= nil then
            return fail(path .. ": reference metatables are unsupported")
        end
        if seen[refValue] then
            return fail(path .. ": reference descriptor cycle or alias")
        end
        seen[refValue] = true
        local kind = rawget(refValue, "kind")
        local id = rawget(refValue, "id")
        local fields = 0
        for key in pairs(refValue) do
            fields = fields + 1
            if key ~= "kind" and key ~= "id" then
                return fail(path .. ": malformed typed reference fields")
            end
        end
        if fields ~= 2
            or (kind ~= "player" and kind ~= "card" and kind ~= "skill")
            or not isNonEmptyString(id) then
            return fail(path .. ": typed reference needs player/card/skill kind and stable id")
        end
        if refCheck then
            local ok, reason = refCheck(kind, id, path)
            if not ok then
                return fail(reason or (path .. ": unresolved " .. kind .. " reference " .. id))
            end
        end
        return { ["$ref"] = { kind = kind, id = id } }
    end

    local result = {}
    if hasNumberKey then
        if count ~= maxIndex then
            return fail(path .. ": sparse array is unsupported")
        end
        local index
        for index = 1, maxIndex do
            local item = rawget(value, index)
            if item == nil then
                return fail(path .. ": sparse array is unsupported")
            end
            local copied, err = copyValue(item, seen, depth + 1, refCheck,
                path .. "[" .. tostring(index) .. "]")
            if err then
                return nil, err
            end
            result[index] = copied
        end
    else
        for key, item in pairs(value) do
            local copied, err = copyValue(item, seen, depth + 1, refCheck,
                path .. "." .. key)
            if err then
                return nil, err
            end
            result[key] = copied
        end
    end
    return result
end

local function copyState(state, label, refCheck)
    if type(state) ~= "table" then
        return fail(label .. ": provider state must be an object table")
    end
    local copied, err = copyValue(state, {}, 0, refCheck, label)
    if err then
        return nil, err
    end
    -- Provider roots are objects, never arrays or scalar typed references.
    if rawget(copied, "$ref") ~= nil then
        return fail(label .. ": provider root must be an object")
    end
    for key in pairs(copied) do
        if type(key) ~= "string" then
            return fail(label .. ": provider root must be an object")
        end
    end
    return copied
end

local function copyAllStates(states, providers, refCheck)
    local copied = {}
    local providerIds = {}
    local id
    for id in pairs(providers) do
        providerIds[#providerIds + 1] = id
    end
    table.sort(providerIds)
    for _, providerId in ipairs(providerIds) do
        local value, err = copyState(states[providerId],
            "provider " .. providerId, refCheck)
        if err then
            return nil, err
        end
        copied[providerId] = value
    end
    return copied
end

local function validateAudit(audit)
    if type(audit) ~= "table" or getmetatable(audit) ~= nil then
        return fail("provider audit metadata is required")
    end
    if not isNonEmptyString(audit.author, 256)
        or not isNonEmptyString(audit.scope, 1024) then
        return fail("provider audit needs non-empty author and scope")
    end
    if audit.mode ~= "managed-only"
        or audit.scriptCoverage ~= "unknown"
        or audit.unmanagedClosures ~= "unsupported"
        or audit.mutableUpvalues ~= "unsupported"
        or audit.externalSideEffects ~= "unsupported" then
        return fail("provider audit must attest managed-only scope and declare global, closure/upvalue, and external state limits")
    end
    return true
end

local function validateProviderSpec(spec)
    if type(spec) ~= "table" or getmetatable(spec) ~= nil then
        return fail("provider declaration must be a plain table")
    end
    if not isNonEmptyString(spec.id, 128) then
        return fail("provider id must be a non-empty stable string")
    end
    if not isInteger(spec.version) or spec.version < 1 or spec.version > 2147483647 then
        return fail("provider version must be a positive 32-bit integer")
    end
    local auditOk, auditError = validateAudit(spec.audit)
    if not auditOk then
        return nil, auditError
    end
    return {
        id = spec.id,
        version = spec.version,
        managed = spec.managed == true,
        audit = {
            author = spec.audit.author,
            scope = spec.audit.scope,
            mode = spec.audit.mode,
            scriptCoverage = spec.audit.scriptCoverage,
            unmanagedClosures = spec.audit.unmanagedClosures,
            mutableUpvalues = spec.audit.mutableUpvalues,
            externalSideEffects = spec.audit.externalSideEffects
        }
    }
end

local function defaultState(provider)
    if provider.managed then
        return { owners = {} }
    end
    return {}
end

local function getManager(self)
    local data = managerData[self]
    if not data then
        return nil, "invalid game state contract manager"
    end
    return data
end

local function getWorld(self)
    local data = worldData[self]
    if not data then
        return nil, "invalid game state world"
    end
    return data
end

local function getCandidate(self)
    local data = candidateData[self]
    if not data then
        return nil, "invalid game state candidate"
    end
    return data
end

function GameStateContract.new()
    local manager = setmetatable({}, { __index = ManagerMethods })
    managerData[manager] = {
        providers = {},
        worlds = setmetatable({}, { __mode = "k" })
    }
    return manager
end

function GameStateContract.ref(kind, stableId)
    if kind ~= "player" and kind ~= "card" and kind ~= "skill" then
        return fail("typed reference kind must be player, card, or skill")
    end
    if not isNonEmptyString(stableId) then
        return fail("typed reference id must be a non-empty stable string")
    end
    return { ["$ref"] = { kind = kind, id = stableId } }
end

function ManagerMethods:registerProvider(spec)
    local data, managerError = getManager(self)
    if not data then
        return fail(managerError)
    end
    local provider, err = validateProviderSpec(spec)
    if err then
        return nil, err
    end
    if data.providers[provider.id] then
        return fail("provider already registered: " .. provider.id)
    end
    data.providers[provider.id] = provider
    local world
    for world in pairs(data.worlds) do
        local state = worldData[world]
        if state then
            state.states[provider.id] = defaultState(provider)
            state.revision = state.revision + 1
        end
    end
    return true
end

function ManagerMethods:registerManagedProvider(spec)
    if type(spec) ~= "table" or getmetatable(spec) ~= nil then
        return fail("provider declaration must be a plain table")
    end
    local copy = {
        id = spec.id,
        version = spec.version,
        audit = spec.audit,
        managed = true
    }
    return self:registerProvider(copy)
end

function ManagerMethods:newWorld(logicalRoomId)
    local manager, managerError = getManager(self)
    if not manager then
        return fail(managerError)
    end
    if not isNonEmptyString(logicalRoomId, 256) then
        return fail("logical room id must be a non-empty stable string")
    end
    local world = setmetatable({}, { __index = WorldMethods })
    local states = {}
    local providerId, provider
    for providerId, provider in pairs(manager.providers) do
        states[providerId] = defaultState(provider)
    end
    worldData[world] = {
        manager = self,
        logicalRoomId = logicalRoomId,
        states = states,
        revision = 0
    }
    manager.worlds[world] = true
    return world
end

function ManagerMethods:capabilities()
    local manager, managerError = getManager(self)
    if not manager then
        return fail(managerError)
    end
    local result = {
        contract = GameStateContract.CONTRACT_ID,
        schemaVersion = GameStateContract.SCHEMA_VERSION,
        coverage = "registered-providers-only",
        managedState = true,
        wholeLuaState = false,
        luaGlobals = "unknown",
        unmanagedClosures = "unsupported",
        mutableUpvalues = "unsupported",
        metatables = "unsupported",
        userdata = "unsupported",
        externalSideEffects = "unsupported",
        providers = {}
    }
    local ids = {}
    local providerId
    for providerId in pairs(manager.providers) do
        ids[#ids + 1] = providerId
    end
    table.sort(ids)
    for _, id in ipairs(ids) do
        local provider = manager.providers[id]
        result.providers[#result.providers + 1] = {
            id = provider.id,
            version = provider.version,
            managed = provider.managed,
            audit = {
                author = provider.audit.author,
                scope = provider.audit.scope,
                mode = provider.audit.mode,
                scriptCoverage = provider.audit.scriptCoverage,
                unmanagedClosures = provider.audit.unmanagedClosures,
                mutableUpvalues = provider.audit.mutableUpvalues,
                externalSideEffects = provider.audit.externalSideEffects
            }
        }
    end
    return result
end

local function checkProvider(world, providerId, requireManaged)
    local world, err = getWorld(world)
    if not world then
        return nil, err
    end
    if not isNonEmptyString(providerId, 128) then
        return nil, "provider id must be a non-empty stable string"
    end
    local manager = managerData[world.manager]
    local provider = manager and manager.providers[providerId]
    if not provider then
        return nil, "provider is not registered: " .. providerId
    end
    if requireManaged and not provider.managed then
        return nil, "provider does not expose shared managed state: " .. providerId
    end
    return world, provider
end

function WorldMethods:setState(providerId, state)
    local world, providerOrError = checkProvider(self, providerId, false)
    if not world then
        return fail(providerOrError)
    end
    local copied, err = copyState(state, "provider " .. providerId)
    if err then
        return nil, err
    end
    if providerOrError.managed then
        local managedOk, managedError = validateManagedState(copied, providerId)
        if not managedOk then
            return nil, managedError
        end
    end
    world.states[providerId] = copied
    world.revision = world.revision + 1
    return true
end

function WorldMethods:getState(providerId)
    local world, providerOrError = checkProvider(self, providerId, false)
    if not world then
        return fail(providerOrError)
    end
    return copyState(world.states[providerId], "provider " .. providerId)
end

local function copyManagedState(state, providerId)
    local copied, err = copyState(state, "provider " .. providerId)
    if err then
        return nil, err
    end
    local valid, validationError = validateManagedState(copied, providerId)
    if not valid then
        return nil, validationError
    end
    return copied
end

local function managedCoordinates(ownerKind, ownerId, key)
    if (ownerKind ~= "player" and ownerKind ~= "card" and ownerKind ~= "skill")
        or not isNonEmptyString(ownerId, 256)
        or not isNonEmptyString(key, 256) then
        return fail("managed owner must use player/card/skill and stable owner/key strings")
    end
    return true
end

validateManagedState = function(state, providerId, refCheck)
    local rootKey
    for rootKey in pairs(state) do
        if rootKey ~= "owners" then
            return fail("managed provider state has an unknown root field: " .. tostring(rootKey))
        end
    end
    if type(state.owners) ~= "table" then
        return fail("managed provider state is malformed: " .. providerId)
    end
    local ownerKind, ownerMap
    for ownerKind, ownerMap in pairs(state.owners) do
        if ownerKind ~= "player" and ownerKind ~= "card" and ownerKind ~= "skill" then
            return fail("managed provider contains an unknown owner kind")
        end
        if type(ownerMap) ~= "table" then
            return fail("managed owner map must be an object")
        end
        local ownerId, payload
        for ownerId, payload in pairs(ownerMap) do
            if not isNonEmptyString(ownerId, 256) or type(payload) ~= "table" then
                return fail("managed owner must map a stable id to an object")
            end
            if refCheck then
                local ok, reason = refCheck(ownerKind, ownerId,
                    "provider " .. providerId .. ".owners." .. ownerKind)
                if not ok then
                    return fail(reason or ("unresolved managed owner " .. ownerKind .. ":" .. ownerId))
                end
            end
            local key
            for key in pairs(payload) do
                if type(key) ~= "string" then
                    return fail("managed owner payload must be an object")
                end
            end
        end
    end
    return true
end

function WorldMethods:setManaged(providerId, ownerKind, ownerId, key, value)
    local world, providerOrError = checkProvider(self, providerId, true)
    if not world then
        return fail(providerOrError)
    end
    local coordinateOk, coordinateError = managedCoordinates(ownerKind, ownerId, key)
    if not coordinateOk then
        return nil, coordinateError
    end
    local copiedValue, valueError = copyValue(value, {}, 0, nil, "managed value")
    if valueError then
        return nil, valueError
    end
    local state, stateError = copyManagedState(world.states[providerId], providerId)
    if stateError then
        return nil, stateError
    end
    local ownerKinds = state.owners
    if ownerKinds[ownerKind] == nil then
        ownerKinds[ownerKind] = {}
    end
    local owners = ownerKinds[ownerKind]
    if type(owners[ownerId]) ~= "table" then
        owners[ownerId] = {}
    end
    owners[ownerId][key] = copiedValue
    world.states[providerId] = state
    world.revision = world.revision + 1
    return true
end

function WorldMethods:getManaged(providerId, ownerKind, ownerId, key)
    local world, providerOrError = checkProvider(self, providerId, true)
    if not world then
        return fail(providerOrError)
    end
    local coordinateOk, coordinateError = managedCoordinates(ownerKind, ownerId, key)
    if not coordinateOk then
        return nil, coordinateError
    end
    local owners = world.states[providerId].owners
    local value = owners[ownerKind]
        and owners[ownerKind][ownerId]
        and owners[ownerKind][ownerId][key]
    if value == nil then
        return nil
    end
    return copyValue(value, {}, 0, nil, "managed value")
end

function WorldMethods:removeManaged(providerId, ownerKind, ownerId, key)
    local world, providerOrError = checkProvider(self, providerId, true)
    if not world then
        return fail(providerOrError)
    end
    local coordinateOk, coordinateError = managedCoordinates(ownerKind, ownerId, key)
    if not coordinateOk then
        return nil, coordinateError
    end
    local state, stateError = copyManagedState(world.states[providerId], providerId)
    if stateError then
        return nil, stateError
    end
    local ownerKinds = state.owners
    local owners = ownerKinds[ownerKind]
    if not owners or not owners[ownerId] or owners[ownerId][key] == nil then
        return false
    end
    owners[ownerId][key] = nil
    if next(owners[ownerId]) == nil then
        owners[ownerId] = nil
    end
    if next(owners) == nil then
        ownerKinds[ownerKind] = nil
    end
    world.states[providerId] = state
    world.revision = world.revision + 1
    return true
end

function WorldMethods:capture()
    local world, err = getWorld(self)
    if not world then
        return fail(err)
    end
    local manager = managerData[world.manager]
    local providerIds = {}
    local providerId
    for providerId in pairs(manager.providers) do
        providerIds[#providerIds + 1] = providerId
    end
    table.sort(providerIds)
    local providers = {}
    for _, id in ipairs(providerIds) do
        local declaration = manager.providers[id]
        providers[#providers + 1] = {
            id = id,
            version = declaration.version,
            state = world.states[id]
        }
    end
    local rawSnapshot = {
        contract = GameStateContract.CONTRACT_ID,
        schemaVersion = GameStateContract.SCHEMA_VERSION,
        worldId = world.logicalRoomId,
        providers = providers
    }
    return copyValue(rawSnapshot, {}, 0, nil, "snapshot")
end

local function normalizeCandidateIndex(index)
    if type(index) ~= "table" or getmetatable(index) ~= nil then
        return fail("candidate reference index must be a plain table")
    end
    local copied, err = copyValue(index, {}, 0, nil, "candidate index")
    if err then
        return nil, err
    end
    local kind, entries
    for kind, entries in pairs(copied) do
        if kind ~= "player" and kind ~= "card" and kind ~= "skill" then
            return fail("candidate reference index contains an unknown kind")
        end
        if type(entries) ~= "table" then
            return fail("candidate reference index kind must map stable ids to true")
        end
        local id, present
        for id, present in pairs(entries) do
            if type(id) ~= "string" or not isNonEmptyString(id) or present ~= true then
                return fail("candidate reference index must map stable ids to true")
            end
        end
    end
    if type(copied.player) ~= "table"
        or type(copied.card) ~= "table"
        or type(copied.skill) ~= "table" then
        return fail("candidate reference index must provide player, card, and skill maps")
    end
    return copied
end

local function parseSnapshot(manager, world, snapshot, candidateIndex)
    local function resolveReference(kind, id, path)
        if candidateIndex[kind][id] == true then
            return true
        end
        return false, path .. ": unresolved candidate " .. kind .. " id " .. id
    end

    local copied, err = copyValue(snapshot, {}, 0, resolveReference, "snapshot")
    if err then
        return nil, err
    end
    if type(copied) ~= "table"
        or copied.contract ~= GameStateContract.CONTRACT_ID
        or copied.schemaVersion ~= GameStateContract.SCHEMA_VERSION then
        return fail("snapshot contract id or schema version is unsupported")
    end
    if copied.worldId ~= world.logicalRoomId then
        return fail("snapshot belongs to a different logical room")
    end
    local rootKey
    for rootKey in pairs(copied) do
        if rootKey ~= "contract" and rootKey ~= "schemaVersion"
            and rootKey ~= "worldId" and rootKey ~= "providers" then
            return fail("snapshot has an unknown top-level field: " .. tostring(rootKey))
        end
    end
    if type(copied.providers) ~= "table" then
        return fail("snapshot provider list is missing")
    end
    local entryCount = 0
    local maxIndex = 0
    local key
    for key in pairs(copied.providers) do
        if type(key) ~= "number" or not isInteger(key) or key < 1 then
            return fail("snapshot providers must be a dense array")
        end
        entryCount = entryCount + 1
        if key > maxIndex then
            maxIndex = key
        end
    end
    if entryCount ~= maxIndex then
        return fail("snapshot providers must be a dense array")
    end

    local expectedIds = {}
    local providerId
    for providerId in pairs(manager.providers) do
        expectedIds[#expectedIds + 1] = providerId
    end
    table.sort(expectedIds)
    if #expectedIds ~= entryCount then
        return fail("snapshot provider set is incomplete or has unknown providers")
    end

    local states = {}
    local index, entry, declaration
    for index = 1, entryCount do
        entry = copied.providers[index]
        if type(entry) ~= "table" then
            return fail("snapshot provider entry must be an object")
        end
        local field
        for field in pairs(entry) do
            if field ~= "id" and field ~= "version" and field ~= "state" then
                return fail("snapshot provider entry has an unknown field")
            end
        end
        providerId = expectedIds[index]
        if entry.id ~= providerId then
            return fail("snapshot providers must be sorted and complete; expected " .. providerId)
        end
        declaration = manager.providers[providerId]
        if entry.version ~= declaration.version then
            return fail("provider version mismatch for " .. providerId)
        end
        local providerState, stateError = copyState(entry.state,
            "provider " .. providerId, resolveReference)
        if stateError then
            return nil, stateError
        end
        if declaration.managed then
            local managedOk, managedError = validateManagedState(providerState,
                providerId, resolveReference)
            if not managedOk then
                return nil, managedError
            end
        end
        states[providerId] = providerState
    end
    return states
end

function ManagerMethods:prepareRestore(worldHandle, snapshot, candidateIndex)
    local manager, managerError = getManager(self)
    if not manager then
        return fail(managerError)
    end
    local world, worldError = getWorld(worldHandle)
    if not world then
        return fail(worldError)
    end
    if world.manager ~= self then
        return fail("world belongs to a different contract manager")
    end
    local index, indexError = normalizeCandidateIndex(candidateIndex)
    if indexError then
        return nil, indexError
    end
    local states, stateError = parseSnapshot(manager, world, snapshot, index)
    if stateError then
        return nil, stateError
    end
    local candidate = setmetatable({}, { __index = CandidateMethods })
    candidateData[candidate] = {
        manager = self,
        targetWorld = worldHandle,
        preparedAgainstRevision = world.revision,
        states = states
    }
    return candidate
end

function CandidateMethods:getState(providerId)
    local candidate, err = getCandidate(self)
    if not candidate then
        return fail(err)
    end
    local manager = managerData[candidate.manager]
    if not manager.providers[providerId] then
        return fail("provider is not registered: " .. tostring(providerId))
    end
    return copyState(candidate.states[providerId], "candidate provider " .. providerId)
end

function CandidateMethods:getManaged(providerId, ownerKind, ownerId, key)
    local candidate, err = getCandidate(self)
    if not candidate then
        return fail(err)
    end
    local provider = managerData[candidate.manager].providers[providerId]
    if not provider or not provider.managed then
        return fail("provider does not expose shared managed state: " .. tostring(providerId))
    end
    local coordinateOk, coordinateError = managedCoordinates(ownerKind, ownerId, key)
    if not coordinateOk then
        return nil, coordinateError
    end
    local owners = candidate.states[providerId].owners
    local value = owners[ownerKind]
        and owners[ownerKind][ownerId]
        and owners[ownerKind][ownerId][key]
    if value == nil then
        return nil
    end
    return copyValue(value, {}, 0, nil, "candidate managed value")
end

function ManagerMethods:activate(worldHandle, candidateHandle)
    local manager, managerError = getManager(self)
    if not manager then
        return fail(managerError)
    end
    local world, worldError = getWorld(worldHandle)
    if not world then
        return fail(worldError)
    end
    local candidate, candidateError = getCandidate(candidateHandle)
    if not candidate then
        return fail(candidateError)
    end
    if world.manager ~= self or candidate.manager ~= self then
        return fail("world or candidate belongs to a different contract manager")
    end
    if candidate.targetWorld ~= worldHandle then
        return fail("candidate is bound to a different runtime world")
    end
    if world.revision ~= candidate.preparedAgainstRevision then
        return fail("candidate is stale because the active world changed")
    end

    -- Build a complete replacement before touching active world state. No
    -- provider callback runs here, so validation or copying cannot partially
    -- mutate the live state.
    local newStates, copyError = copyAllStates(candidate.states, manager.providers)
    if copyError then
        return nil, copyError
    end
    world.states = newStates
    world.revision = world.revision + 1
    return true, world.revision
end

function WorldMethods:prepareRestore(snapshot, candidateIndex)
    local world, err = getWorld(self)
    if not world then
        return fail(err)
    end
    return world.manager:prepareRestore(self, snapshot, candidateIndex)
end

function WorldMethods:activate(candidate)
    local world, err = getWorld(self)
    if not world then
        return fail(err)
    end
    return world.manager:activate(self, candidate)
end

return GameStateContract
