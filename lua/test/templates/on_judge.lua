-- ============================================================
-- TEMPLATE: on_judge 判定相關技能
-- Trigger events: AskForRetrial, FinishJudge, StartJudge
-- Common pattern: 判定牌生效前，你可以打出一张牌替换之...
-- ============================================================
-- Setup: 2 players, trigger a judgment (e.g. via Lightning)
-- ============================================================

local SKILL_NAME = "[REQUIRED: skill name]"
local TEST_NAME  = "[REQUIRED: test description]"

local runner = sgs.test.create(TEST_NAME)

runner:setup(function(t)
    local players = t:getPlayers()
    local p1 = players[1]   -- skill holder (retrial source)
    local p2 = players[2]   -- judgment target

    ROOM:handleAcquireDetachSkills(p1, SKILL_NAME, false)

    -- [OPTIONAL] Install Lightning on p2 to trigger judgment
    -- local lightning = ROOM:getCardFromPile("lightning")
    -- if lightning >= 0 then
    --     ROOM:moveCardTo(sgs.Sanguosha:getCard(lightning), p2,
    --         sgs.Player_PlaceDelayedTrick,
    --         sgs.CardMoveReason(sgs.CardMoveReason_S_REASON_TRANSFER, "test"))
    -- end
end)

runner:run(function(t)
    local players = t:getPlayers()
    local p1 = players[1]
    local p2 = players[2]

    -- Force skill invoke when asked to retrial
    t:registerOverride(p1, "skill_invoke", SKILL_NAME, sgs.QVariant(true))

    -- [SKILL-SPECIFIC] If skill asks which card to use for retrial:
    -- t:registerOverride(p1, "card", SKILL_NAME, sgs.QVariant(42))

    -- [SKILL-SPECIFIC] If skill has choices (e.g. exchange vs override):
    -- t:registerOverride(p1, "choice", SKILL_NAME, sgs.QVariant("exchange"))

    -- Skip play phases
    t:registerOverride(p1, "activate", "phase", sgs.QVariant("pass"))
    t:registerOverride(p2, "activate", "phase", sgs.QVariant("pass"))
end)

runner:assert(function(t)
    t:assertAlive(1)
    t:assertAlive(2)
    t:assertHasSkill(1, SKILL_NAME)

    -- [SKILL-SPECIFIC] Judgment was modified → check outcome:
    -- t:assertHp(2, 2)              -- Lightning dealt 3 damage (retrial failed to prevent)
    -- t:assertHp(2, 4)              -- Lightning avoided (retrial succeeded)
    -- t:assertHandcardCount(1, 3)   -- used a card for retrial → handcard decreased

    -- [SKILL-SPECIFIC] If skill obtained the judgment card:
    -- t:assertHandcardCount(1, 5)   -- gained the judgment card
end)

return runner
