local runner = sgs.test.create("PlayerDecisionService Lua compatibility"):factoryOnly()
local evidencePath = os.getenv("QSAN_PLAYER_DECISION_EVIDENCE")

local function recordEvidence(marker)
    if not evidencePath then
        return
    end
    local file = assert(io.open(evidencePath, "a"))
    file:write(marker .. "\n")
    file:close()
end

recordEvidence("SCRIPT_LOADED")

runner:run(function(t)
    local player = t:getPlayers()[1]
    t:registerOverride(player, "activate", "phase", sgs.QVariant("pass"))
    t:registerOverride(player, "skill_invoke", "tuxi", sgs.QVariant(true))
    t:registerOverride(player, "choice", "tuxi", sgs.QVariant("left"))
    t:registerOverride(player, "player_chosen", "tuxi", sgs.QVariant("decision-player"))
    t:registerOverride(player, "card_chosen", "snatch", sgs.QVariant(0))
    t:registerOverride(player, "card", ".", sgs.QVariant(0))
    recordEvidence("SWIG_REGISTER_OVERRIDE_RETURNED")
    print("[PLAYER_DECISION_SERVICE] SWIG register override returned")
end)

runner:assert(function(t)
    t:assertAlive(1)
    t:assertAlive(2)
    t:addResult(true, "PlayerDecisionService SWIG Room facade remained callable")
    recordEvidence("ASSERTIONS_REGISTERED")
    print("[PLAYER_DECISION_SERVICE] assertions completed")
end)

return runner
