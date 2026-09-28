-- Extract the production repair/target gate; native objects below are mocks.
-- lua tools/local/h50p_mobile_probe.lua /path/to/nMobileEffect.lua [events]
assert(arg[1], 'nMobileEffect.lua required')
local file = assert(io.open(arg[1]))
local source = file:read('*a'); file:close()
local first = assert(source:find('local n_mobile_skill_names', 1, true))
local last = assert(source:find('\nn_trig =', first, true))
local checks, scans, attachments = 0, 0, 0
local names = {'#n_trig', '#n_mobile_effect', '#n_mvpexperience'}
sgs = {qlist=function(list) return ipairs(list) end,
    Sanguosha={getBanPackages=function() return {} end}}
function table.contains(list, v)
    for _, x in ipairs(list) do if x == v then return true end end
    return false
end
local gate = assert((loadstring or load)(source:sub(first,last-1)
    .. '\nreturn n_mobile_target_ctx', '@'..arg[1]))()
local function newRoom(n)
    local room = {revision=1, players={}}
    function room:aiStateRevision() return tostring(self.revision) end
    function room:getAllPlayers(dead)
        assert(dead); scans = scans + 1
        return setmetatable(self.players, {__index={length=function(t) return #t end}})
    end
    function room:attachSkillToPlayer(p, skill)
        attachments = attachments + 1
        if self.fail then return end
        p.skills[skill] = true; self.revision = self.revision + 1
        if self.onAttach then self:onAttach(p, skill) end
    end
    for i=1,n do
        local p = {name='sgs'..i, skills={}}
        function p:objectName() return self.name end
        function p:getSkillInstanceIds(skill)
            checks = checks + 1
            return {isEmpty=function() return not self.skills[skill] end}
        end
        for _, name in ipairs(names) do p.skills[name] = true end
        room.players[i] = p
    end
    return room
end
local function record(room, target, owner)
    return gate(room, target, {owner=owner})
end
local r = newRoom(50)
local function repaired(p)
    for _, name in ipairs(names) do assert(p.skills[name], name) end
end
assert(record(r,r.players[1],r.players[1]))
assert(not record(r,r.players[1],r.players[2]))
assert(not record(r,nil,r.players[2]))
-- A late/swapped general is repaired even on another owner's record.
r.players[50].skills = {}; r.revision = r.revision + 1
assert(not record(r,r.players[50],r.players[1])); repaired(r.players[50])
assert(record(r,r.players[50],r.players[50]))
-- Removal while the same general remains, including a dead roster member.
r.players[2].skills[names[1]] = nil; r.revision = r.revision + 1
record(r,r.players[1],r.players[1]); repaired(r.players[2])
-- Failed attachment must not make a cache entry that suppresses retries.
r.players[3].skills = {}; r.revision = r.revision + 1; r.fail = true
record(r,r.players[1],r.players[1]); r.fail = false
record(r,r.players[1],r.players[1]); repaired(r.players[3])
-- Mutation of an already-scanned player during repair must force a rescan.
r.players[50].skills = {}; r.revision = r.revision + 1
r.onAttach = function(self)
    self.onAttach = nil; self.players[1].skills = {}; self.revision = self.revision + 1
end
record(r,r.players[1],r.players[1]); record(r,r.players[1],r.players[1]); repaired(r.players[1])
-- Separate rooms can have identical revisions; no cross-room cache reuse.
local other = newRoom(50); other.revision = r.revision; other.players[10].skills = {}
record(other,other.players[1],other.players[1]); repaired(other.players[10])
record(r,r.players[1],r.players[1])
-- Roster growth without a revision signal must still be observed.
local extra = newRoom(1).players[1]; extra.skills = {}; r.players[51] = extra
record(r,r.players[1],r.players[1]); repaired(extra)
-- Older engines without the revision API retain the complete repair path.
local legacy = newRoom(2); legacy.aiStateRevision = false
record(legacy,legacy.players[1],legacy.players[1]); legacy.players[2].skills = {}
record(legacy,legacy.players[1],legacy.players[1]); repaired(legacy.players[2])
-- Mirrors 50 owner records per event; target's mark writes invalidate once more.
r = newRoom(50)
local events = tonumber(arg[2]) or 1000
checks, scans, attachments = 0, 0, 0
local samples = 0
collectgarbage('collect')
debug.sethook(function() samples = samples + 1 end, '', 1000)
local started = os.clock()
local accepted = 0
for event=1,events do
    r.revision = r.revision + 1
    for _, owner in ipairs(r.players) do
        if record(r,r.players[25],owner) then
            accepted = accepted + 1
            r.revision = r.revision + 1
        end
    end
end
local elapsed = os.clock()-started
debug.sethook()
assert(accepted == events and attachments == 0)
print(string.format('PASS events=%d records=%d instance_checks=%d roster_reads=%d instruction_samples=%d cpu_seconds=%.6f',
    events,events*50,checks,scans,samples,elapsed))
