-- Run with lua5.1 or lua5.4; extracts real SmartAI code (no 'continue' rewrite).
-- Usage: lua tools/local/h50p_relationship_probe.lua /path/to/smart-ai.lua [rounds]
local path = assert(arg[1], 'smart-ai.lua path required')
local f = assert(io.open(path)); local source = f:read('*a'); f:close()
local rounds = tonumber(arg[2]) or 20
local calls = 0
SmartAI = {}
-- Same objectName comparison as lua/utilities.lua; count the SWIG boundary calls.
function table.contains(t, value, by_name)
    for _, item in ipairs(t) do
        if by_name and item:objectName() == value:objectName()
            or not by_name and item == value then return true end
    end
    return false
end
local start = source:find('local ai_player_list_positions', 1, true)
    or assert(source:find('function SmartAI:isFriend(other,another)', 1, true))
local finish = assert(source:find('function SmartAI:getFriends(player,no_self)', start, true))
assert((loadstring or load)(source:sub(start, finish - 1), '@'..path))()
local players, by_name = {}, {}
for i = 1, 50 do
    local p = {name = 'sgs'..i, controller = ''}
    function p:objectName() calls = calls + 1; return self.name end
    function p:getTag() local value = self.controller; return {toString=function() return value end} end
    function p:removeTag() self.controller = '' end
    players[i], by_name[p.name] = p, p
end
local room = {findPlayerByObjectName=function(_, name) return by_name[name] end}
local ais = {}
for i, p in ipairs(players) do
    local ai = setmetatable({player=p, room=room, friends={}, enemies={}}, {__index=SmartAI})
    for j, other in ipairs(players) do
        local list = i % 2 == j % 2 and ai.friends or ai.enemies
        list[#list+1] = other
    end
    ais[i] = ai
end
-- Exercise actual wrappers as used by fallback, including controller semantics.
local function check(ai)
    for _, p in ipairs(players) do
        local linked = ai:isDualControlLinked(ai.player, p)
        assert(ai:isFriend(p) == (linked or table.contains(ai.friends, p, true)))
        assert(ai:isEnemy(p) == (not linked and table.contains(ai.enemies, p, true)))
    end
end
local ai = ais[1]
check(ai)
-- Same table, same length replacement; sorting; removal; append; list replacement.
ai.friends[2] = players[2]; check(ai)
table.sort(ai.friends, function(a,b) return a.name > b.name end); check(ai)
table.remove(ai.friends, 3); check(ai)
ai.friends[#ai.friends+1] = players[3]; check(ai)
ai.friends = {players[1], players[4]}; check(ai)
players[1].controller = players[2].name; check(ai)
players[2].controller = players[3].name; check(ai)
players[3].controller = players[1].name
assert(ai:getActualController(players[1]) == players[1]); assert(players[1].controller == '')
for _, p in ipairs(players) do p.controller = '' end
ai.friends = {}
for i=1,50,2 do ai.friends[#ai.friends+1] = players[i] end
for _, a in ipairs(ais) do check(a) end
-- Deterministic dense-list mutation stress, including same-length substitutions
-- and separate player wrappers with equal objectName (SWIG identity is irrelevant).
local state = 17
for step=1,300 do
    state = (state * 73 + 19) % 997
    local i = state % #ai.friends + 1
    ai.friends[i] = players[state % 50 + 1]
    check(ai)
    if step % 7 == 0 then
        local first = table.remove(ai.friends, 1)
        ai.friends[#ai.friends+1] = first
        check(ai)
    end
end
local alias = setmetatable({name=players[4].name, controller=''}, {__index=players[4]})
ai.friends = {alias}
assert(ai:isFriend(players[4]))
ai.friends[1] = players[6]
assert(not ai:isFriend(players[4]))
ai.friends = {}
assert(not ai:isFriend(players[4]))
for i=1,50,2 do ai.friends[#ai.friends+1] = players[i] end
collectgarbage('collect'); calls = 0
local before = os.clock()
local yes = 0
for _=1,rounds do
    for _, a in ipairs(ais) do
        for _, p in ipairs(players) do
            if a:isFriend(p) then yes = yes + 1 end
            if a:isEnemy(p) then yes = yes + 1 end
        end
    end
end
assert(yes == rounds * 2500)
print(string.format('PASS n=50 rounds=%d queries=%d cpu_seconds=%.6f objectName_calls=%d', rounds, rounds*5000, os.clock()-before, calls))
