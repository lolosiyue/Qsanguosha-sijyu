-- Lua Test Runner for QSanguosha
-- Usage: QSanguosha.exe --lua-test lua/test/examples/test_xxx.lua
--
-- The test script has access to global 'ROOM' (the Room object).
-- It should create a runner via sgs.test.create(name),
-- configure it with :setup(), :run(), :assert(), and
-- return the runner table.
-- The runner's execute() method will be called by C++.

sgs.TestRunner = {}

function sgs.TestRunner:new(name)
    local t = {
        name = name,
        _setupFn = nil,
        _runFn = nil,
        _assertFn = nil,
        _results = {},
    }
    setmetatable(t, { __index = sgs.TestRunner })
    return t
end

function sgs.TestRunner:setup(fn)
    self._setupFn = fn
    return self
end

function sgs.TestRunner:run(fn)
    self._runFn = fn
    return self
end

function sgs.TestRunner:assert(fn)
    self._assertFn = fn
    return self
end

function sgs.TestRunner:getPlayers()
    local list = ROOM:getAllPlayers(true)
    local t = {}
    for _, p in sgs.qlist(list) do
        table.insert(t, p)
    end
    return t
end

function sgs.TestRunner:getPlayer(index)
    if type(index) == "number" then
        return self:getPlayers()[index]
    elseif type(index) == "string" then
        for _, p in ipairs(self:getPlayers()) do
            if p:getScreenName() == index or p:objectName() == index then
                return p
            end
        end
    end
    return nil
end

function sgs.TestRunner:registerOverride(player, queryType, key, answer)
    ROOM:registerTestOverride(player, queryType, key, answer)
end

function sgs.TestRunner:addResult(passed, msg)
    table.insert(self._results, { passed = passed, msg = msg })
end

function sgs.TestRunner:printResults()
    print("")
    print("--------------------------------------------------")
    print("Test: " .. self.name)
    print("--------------------------------------------------")
    local passed = 0
    local failed = 0
    for _, r in ipairs(self._results) do
        if r.passed then
            passed = passed + 1
            print("[PASS] " .. r.msg)
        else
            failed = failed + 1
            print("[FAIL] " .. r.msg)
        end
    end
    print("--------------------------------------------------")
    local total = passed + failed
    if total > 0 then
        print(string.format("Results: %d/%d passed", passed, total))
        if failed > 0 then
            print(string.format("        %d FAILED", failed))
        end
    else
        print("Results: no assertions")
    end
    print("==================================================")
end

function sgs.TestRunner:execute()
    print("")
    print("==================================================")
    print("Test: " .. self.name)
    print("==================================================")

    if self._setupFn then
        self._setupFn(self)
    end

    RUNNER_DO_ASSERTIONS = function()
        self:printResults()
    end

    if self._runFn then
        self._runFn(self)
    end

    ROOM:start()
end

function sgs.TestRunner:assertHp(playerRef, expected)
    local p = self:getPlayer(playerRef)
    if not p then
        self:addResult(false, "assertHp: player not found: " .. tostring(playerRef))
        return false
    end
    local actual = p:getHp()
    local name = p:getScreenName() or p:objectName()
    local passed = (actual == expected)
    self:addResult(passed,
        string.format("%s HP: expected %d, got %d", name, expected, actual))
    return passed
end

function sgs.TestRunner:assertAlive(playerRef)
    local p = self:getPlayer(playerRef)
    if not p then
        self:addResult(false, "assertAlive: player not found: " .. tostring(playerRef))
        return false
    end
    local name = p:getScreenName() or p:objectName()
    local passed = p:isAlive()
    self:addResult(passed, string.format("%s is %s", name, passed and "alive" or "DEAD"))
    return passed
end

function sgs.TestRunner:assertHasSkill(playerRef, skillName)
    local p = self:getPlayer(playerRef)
    if not p then
        self:addResult(false, "assertHasSkill: player not found: " .. tostring(playerRef))
        return false
    end
    local name = p:getScreenName() or p:objectName()
    local passed = p:hasSkill(skillName)
    self:addResult(passed,
        string.format("%s has skill %s: %s", name, skillName, tostring(passed)))
    return passed
end

function sgs.TestRunner:assertHandcardCount(playerRef, expected)
    local p = self:getPlayer(playerRef)
    if not p then
        self:addResult(false, "assertHandcardCount: player not found: " .. tostring(playerRef))
        return false
    end
    local name = p:getScreenName() or p:objectName()
    local actual = p:getHandcardNum()
    local passed = (actual == expected)
    self:addResult(passed,
        string.format("%s handcard count: expected %d, got %d", name, expected, actual))
    return passed
end

function sgs.TestRunner:assertMark(playerRef, markName, expected)
    local p = self:getPlayer(playerRef)
    if not p then
        self:addResult(false, "assertMark: player not found: " .. tostring(playerRef))
        return false
    end
    local name = p:getScreenName() or p:objectName()
    local actual = p:getMark(markName)
    local passed = (actual == expected)
    self:addResult(passed,
        string.format("%s mark '%s': expected %d, got %d", name, markName, expected, actual))
    return passed
end

function sgs.TestRunner:assertNotKongcheng(playerRef)
    local p = self:getPlayer(playerRef)
    if not p then
        self:addResult(false, "assertNotKongcheng: player not found: " .. tostring(playerRef))
        return false
    end
    local name = p:getScreenName() or p:objectName()
    local passed = not p:isKongcheng()
    self:addResult(passed, string.format("%s is %s", name, passed and "not empty-handed" or "empty-handed"))
    return passed
end

sgs.test = {}
function sgs.test.create(name)
    return sgs.TestRunner:new(name)
end

-- Template index: maps trigger events → template file
-- AI Agents: read the skill definition, match its events to find the right template
sgs.test.templates = {
    on_damage = {
        file   = "lua/test/templates/on_damage.lua",
        events = { "Damaged", "DamageInflicted", "DamageDone", "DamageComplete" },
        desc   = "受到/造成傷害時觸發"
    },
    on_phase = {
        file   = "lua/test/templates/on_phase.lua",
        events = { "EventPhaseStart", "EventPhaseChanging", "EventPhaseEnd" },
        desc   = "摸牌/出牌/棄牌階段觸發"
    },
    on_slash = {
        file   = "lua/test/templates/on_slash.lua",
        events = { "CardUsed", "PreCardUsed", "TargetChosen", "SlashEffect", "SlashHit" },
        desc   = "使用殺/被殺指定時觸發"
    },
    on_discard = {
        file   = "lua/test/templates/on_discard.lua",
        events = { "CardsMoveOneTime", "CardLost", "CardDiscarded" },
        desc   = "棄牌/失去牌時觸發"
    },
    on_card_effect = {
        file   = "lua/test/templates/on_card_effect.lua",
        events = { "CardEffect", "CardEffected", "PreCardEffect" },
        desc   = "卡牌效果相關（免疫、無懈、改判等）"
    },
    on_judge = {
        file   = "lua/test/templates/on_judge.lua",
        events = { "AskForRetrial", "FinishJudge", "StartJudge" },
        desc   = "判定相關（改判、改判定結果等）"
    },
    on_turn = {
        file   = "lua/test/templates/on_turn.lua",
        events = { "EventTurnStart", "EventPhaseStart", "GameStart" },
        desc   = "回合/階段開始時觸發"
    },
    on_view_as = {
        file   = "lua/test/templates/on_view_as.lua",
        events = { "CardUsed", "PreCardUsed" },
        desc   = "ViewAsSkill 轉換技（武聖、龍膽等）"
    },
}
