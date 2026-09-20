-- This file is executed twice: once after the production Lua test bootstrap
-- pushes the real SWIG Room pointer as ROOM, and once by isolated AI runtime.
local rules_room = ROOM or R
if rules_room ~= nil then
    local event_id = assert(tonumber(rules_room:currentHistoryEventId()))
    local event = rules_room:historyEvent(event_id)
    assert(event.kind == "damage")
    assert(event.data.skill_name == "rules_probe")

    local moves = rules_room:queryHistoryMoves({from = "alice", to = "bob", player = "alice", limit = 10})
    assert(#moves.facts == 1 and moves.facts[1].kind == "move")
    assert(moves.facts[1].data.card_id == 17)
    local damage = rules_room:queryActualDamage({from = "alice", to = "bob", limit = 10})
    assert(#damage.facts == 1 and damage.facts[1].data.amount == 1)
    local facts = rules_room:queryHistoryFacts({kind = "actual_damage", skill_name = "rules_probe", limit = 10})
    assert(#facts.facts == 1)

    local ok = pcall(function() rules_room:queryHistoryMoves({unknown = 1}) end)
    assert(not ok)
    ok = pcall(function() rules_room:queryHistoryFacts({limit = {10}}) end)
    assert(not ok)
    local invalid_cursor = rules_room:queryHistoryMoves({from = "alice", after = "not-an-id"})
    assert(invalid_cursor.error == "invalid_query")

    -- SWIG returns a fresh primitive table. Mutating it must not rewrite the
    -- authoritative journal returned by a second query.
    moves.facts[1].data.from = "tampered"
    local reread = rules_room:queryHistoryMoves({from = "alice", to = "bob", limit = 10})
    assert(#reread.facts == 1 and reread.facts[1].data.from == "alice")
    event.data.skill_name = "tampered"
    assert(rules_room:historyEvent(event_id).data.skill_name == "rules_probe")
    resolution_history_probe_ok = true
    return
end

-- Isolated AI privacy contract: no Room pointer or authoritative history API is
-- installed in this runtime. This remains separate from the rules SWIG test.
local ai = assert(SmartAIView.new({
    viewer = "viewer",
    world_view = {
        mode_id = "history-contract",
        revision = "1",
        current_player = "viewer",
        player_order = {"viewer"},
        alive_player_order = {"viewer"},
        -- The self view is separate from the other-player list.
        players = {},
        self = {object_name = "viewer", alive = true, skills = {}, handcard_count = 1},
        hand_cards = {{id = 17, effective_id = 17, name = "slash",
            kind_of = {"Slash", "BasicCard"}}}
    }
}))
local room = ai.room
assert(room:getCurrent():objectName() == "viewer")
assert(room.getHistory == nil)
assert(room["query" .. "HistoryEvents"] == nil)
assert(room["query" .. "HistoryFacts"] == nil)
