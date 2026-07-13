-- Example: Basic sanity test
-- Usage: QSanguosha.exe --lua-test lua/test/examples/test_basic.lua
--
-- Tests that the game starts and runs with 2 players.
-- The players are pre-created by C++ (test scenario with 2 sujiang).
-- The game ends after lord's first turn (extraOptions:singleTurn:lord).

local runner = sgs.test.create("基本環境測試")

runner:assert(function(t)
    t:assertAlive(1)
    t:assertAlive(2)
    t:assertHp(1, 4)
    t:assertHp(2, 4)
end)

return runner
