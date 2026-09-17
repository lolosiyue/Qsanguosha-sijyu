-- Loaded by the native isolation suite inside the real AI sandbox.
-- Exercise the shared adapter without registering any skill decision.
local function player(name, alive)
    return {object_name=name, alive=alive, dead=not alive, handcard_count=1,
        equips={}, judging_area={}, public_marks={}, skills={}}
end

local function request()
    return {viewer="viewer", kind="use_card", world_view={
        mode_id="adapter-fixture", revision="7", current_player="other",
        self=player("viewer", true), players={player("other", true), player("dead", false)},
        player_order={"dead", "viewer", "other"}, alive_player_order={"viewer", "other"},
        hand_cards={{id=7, effective_id=7, name="slash", kind_of={"Slash", "BasicCard"}}},
        mode_policy={managed=true, relations={viewer={viewer="friend", other="enemy"}},
            objectives={viewer=-3, other=5}}
    }}
end

local function names(players)
    local result = {}
    for _, p in ipairs(players) do result[#result + 1] = p:objectName() end
    return table.concat(result, ",")
end

local first = request()
first.world_view.self.skills = {
    {name="alpha", instance_id=1, invalid=false},
    {name="beta", instance_id=2, invalid=false},
    {name="disabled", instance_id=3, invalid=true}
}
first.world_view.self.equips = {{id=8, effective_id=8, name="weapon"}}
local ai = assert(SmartAIView.new(first))
local room, owner = ai.room, ai.player
local other = room:getCurrent()
assert(getmetatable(room) == RoomView and room:getMode() == "adapter-fixture")
assert(room:findPlayerByObjectName("viewer") == owner)
assert(room:findPlayerByObjectName("other") == other)
assert(ai.friends[1] == owner and ai.enemies[1] == other)
assert(names(room:getPlayers()) == "dead,viewer,other")
assert(names(room:getAlivePlayers()) == "viewer,other")
assert(names(room:getAllPlayers()) == "other,viewer")
assert(names(room:getAllPlayers(true)) == "other,dead,viewer")
assert(names(room:getOtherPlayers(owner)) == "other")
assert(names(room:getOtherPlayers(owner, true)) == "other,dead")
assert(room:findPlayerByObjectName("dead") == nil)
assert(room:findPlayerByObjectName("dead", true):objectName() == "dead")
assert(room:findPlayerByObjectName("missing", true) == nil)

local players = room:getPlayers()
table.remove(players, 1)
assert(#room:getPlayers() == 3) -- Caller sorting/removal cannot change the snapshot order.
local cards = owner:getHandcards()
assert(#cards == 1 and cards[1]:isKindOf("Slash") and owner:handCards()[1] == 7)
cards[1]._view.id = 99
assert(owner:handCards()[1] == 7 and first.world_view.hand_cards[1].id == 7)
assert(#owner:getCards("he") == 2 and #owner:getCards("j") == 0)
assert(owner:getCards("hh")[1]:getId() == 7 and #owner:getCards("hh") == 1)
assert(owner:getCards("pile") == nil and owner:getCards(1) == nil)
assert(other:getHandcards() == nil and other:handCards() == nil)
assert(other:getCards("he") == nil and #other:getCards("e") == 0)
assert(PlayerView.new(player("standalone", true)):getHandcards() == nil)

-- A facade is never mistaken for an array, including method-shaped plain tables.
assert(type(owner) == "table" and AIValue.isPlayer(owner))
assert(AIValue.kind(room) == "room" and AIValue.kind(ai) == "ai")
assert(AIValue.isCard(cards[1]) and not AIValue.isPlayer(cards[1]))
assert(AIValue.isSkill(owner:getSkills()[1]))
assert(not AIValue.isList(owner) and not AIValue.isList(room) and not AIValue.isList(ai))
assert(not AIValue.isList(cards[1]) and not AIValue.isList(owner:getSkills()[1]))
assert(not AIValue.isPlayer({objectName=function() return "viewer" end}))
assert(not AIValue.isPlayer(setmetatable({}, PlayerView)))
assert(AIValue.kind(nil) == nil and AIValue.kind(7) == nil)
assert(AIValue.isList({}) and AIValue.isList({owner, other}))
assert(not AIValue.isList({[2]=other}) and not AIValue.isList({player=owner}))
assert(not pcall(AIList.new, owner) and not pcall(AIList.new, {[2]=other}))
assert(AIList.new(nil) == nil and sgs.QList2Table(nil) == nil)
assert(not pcall(sgs.qlist, nil) and not pcall(sgs.qlist, owner))
assert(not pcall(sgs.list, owner) and not pcall(sgs.QList2Table, owner))

-- QList indexing/iteration is zero-based; Lua indexing/iteration is one-based.
local roster = room:getPlayers()
assert(roster:length() == 3 and not roster:isEmpty())
assert(roster:at(0) == roster[1] and roster:at(1) == owner)
assert(roster:at(-1) == nil and roster:at(3) == nil and roster:at(0.5) == nil)
assert(roster:first() == roster[1] and roster:last() == other)
assert(roster:contains(owner) and not roster:contains(cards[1]))
local seen = 0
for index, value in sgs.qlist(roster) do
    assert(index == seen and value == roster[index + 1])
    seen = seen + 1
end
assert(seen == 3)
seen = 0
for index, value in sgs.list(roster) do
    seen = seen + 1
    assert(index == seen and value == roster[index])
end
assert(seen == 3)
local copy = sgs.QList2Table(roster)
assert(copy ~= roster and getmetatable(copy) == nil and copy[2] == owner)
table.sort(copy, function(a, b) return a:objectName() < b:objectName() end)
assert(names(room:getPlayers()) == "dead,viewer,other")
assert(roster:removeOne(owner) and not roster:removeOne(owner))
roster:append(owner)
assert(roster:last() == owner and names(room:getPlayers()) == "dead,viewer,other")
assert(not pcall(function() roster:append(nil) end))
local empty = AIList.new({})
assert(empty:isEmpty() and empty:length() == 0 and empty:first() == nil and empty:last() == nil)
for _ in sgs.qlist(empty) do error("empty collection yielded an element") end
local ids = owner:handCards()
assert(ids:contains(7) and ids:at(0) == 7)
assert(getmetatable(ai.friends) == AIList and getmetatable(ai.enemies) == AIList)
assert(getmetatable(owner:getSkills()) == AIList and getmetatable(owner:getEquips()) == AIList)
assert(getmetatable(owner:getJudgingArea()) == AIList and getmetatable(owner:getCards("he")) == AIList)

-- Collection edits and card/skill view edits cannot mutate input snapshots.
local equips, skills = owner:getEquips(), owner:getSkills()
equips[1]._view.id = 99
skills[1]._view.name = "changed"
assert(owner:getEquips()[1]:getId() == 8 and first.world_view.self.equips[1].id == 8)
assert(owner:getSkills()[1]:objectName() == "alpha" and owner:hasSkill("alpha"))
local unknown = PlayerView.new({object_name="unknown"})
assert(unknown:getEquips() == nil and unknown:getJudgingArea() == nil and unknown:getSkills() == nil)
assert(unknown:getCards("e") == nil and unknown:hasSkills("alpha") == nil)
local malformed = PlayerView.new({object_name="malformed", equips={{id=9}, false},
    judging_area={[2]={id=10}}, skills={named={name="alpha"}}})
assert(malformed:getEquips() == nil and malformed:getJudgingArea() == nil)
assert(malformed:getSkills() == nil and malformed:hasSkills("alpha") == nil)

-- The single-player overload returns boolean; the collection overload returns a player.
assert(ai:hasSkills("alpha") == true and ai:hasSkills("alpha", other) == false)
assert(ai:hasSkills("alpha+beta") == true and ai:hasSkills("missing|beta") == true)
assert(ai:hasSkills("alpha+missing") == false and ai:hasSkills("disabled") == false)
assert(ai:hasSkills("alpha#1") == true and ai:hasSkills("alpha#2") == false)
assert(ai:hasSkills("") == false and ai:hasSkills("alpha+") == false)
assert(ai:hasSkills("+alpha") == false and ai:hasSkills("alpha++beta") == false)
assert(ai:hasSkills("|alpha") == true)
assert(ai:hasSkills("alpha", {other, owner}) == owner)
assert(ai:hasSkills("alpha", room:getAlivePlayers()) == owner)
assert(ai:hasSkills("missing", room:getPlayers()) == nil and ai:hasSkills("alpha", {}) == nil)
assert(not pcall(function() ai:hasSkills("alpha", cards[1]) end))
assert(not pcall(function() ai:hasSkills("alpha", {owner, cards[1]}) end))

-- Identical player names in another decision/Room never reuse a prior facade.
local second = request()
second.world_view.hand_cards = {}
second.world_view.self.handcard_count = 0
second.world_view.current_player = "viewer"
local next_ai = assert(SmartAIView.new(second))
assert(next_ai.player ~= owner and next_ai.room ~= room)
assert(not room:getPlayers():contains(next_ai.player))
assert(next_ai.room:getCurrent() == next_ai.player and #next_ai.player:getHandcards() == 0)
assert(room:getCurrent() == other and #owner:getHandcards() == 1)

-- No current preserves the native RoomRoster fallback, including dead players.
local no_current = request()
no_current.world_view.current_player = ""
local no_current_room = SmartAIView.new(no_current).room
assert(no_current_room:getCurrent() == nil)
assert(names(no_current_room:getAllPlayers()) == "dead,viewer,other")

-- Old/incomplete snapshots do not silently invent ordering or private cards.
local incomplete = request()
incomplete.world_view.player_order = nil
incomplete.world_view.alive_player_order = nil
incomplete.world_view.hand_cards = nil
local partial = assert(SmartAIView.new(incomplete))
assert(partial.room:getPlayers() == nil and partial.room:getAllPlayers() == nil)
assert(partial.room:getAlivePlayers() == nil and partial.player:getHandcards() == nil)
assert(partial.room:getCurrent():objectName() == "other")
local duplicate = request()
duplicate.world_view.players[1].object_name = "viewer"
assert(SmartAIView.new(duplicate) == nil)
local wrong_viewer = request()
wrong_viewer.viewer = "other"
assert(SmartAIView.new(wrong_viewer) == nil)
local sparse = request()
sparse.world_view.players = {[2]=player("other", true)}
assert(SmartAIView.new(sparse) == nil)

-- Only value queries are exposed; native gameplay and VM globals stay absent.
assert(room.setPlayerMark == nil and room.useCard == nil and room.getTag == nil)
assert(owner.getRoom == nil and owner.setFlags == nil and owner.getTag == nil)
assert(owner.getPile == nil and cards[1].getRealCard == nil)
assert(sgs.Sanguosha == nil and global_room == nil and current_self == nil)

-- Card zones: unknown, partially visible and known-empty stay three different answers.
local zoned = request()
zoned.world_view.discard_pile = {{id = 20, effective_id = 20, name = "slash"}}
zoned.world_view.self.piles = {
    {name = "&stash", count = 2, open = true, hand_pile = true, card_ids = {31, 32}},
    {name = "secret", count = 1, open = false, hand_pile = false}
}
zoned.world_view.self.display_cards = {7}
zoned.world_view.self.equips = {{id = 8, effective_id = 8, name = "weapon"}}
zoned.world_view.players[1].known_cards = {{id = 40, effective_id = 40, name = "jink"}}
zoned.world_view.players[1].hand_visible = false
zoned.world_view.players[1].piles = {
    {name = "open", count = 1, open = true, hand_pile = false, card_ids = {41}}
}
zoned.world_view.players[2].known_cards = {}
zoned.world_view.players[2].hand_visible = true
local zone_ai = assert(SmartAIView.new(zoned))
local zone_room, me = zone_ai.room, zone_ai.player
local rival = zone_room:findPlayerByObjectName("other")
local dead_rival = zone_room:findPlayerByObjectName("dead", true)

assert(#zone_room:getDiscardPile() == 1 and zone_room:getDiscardPile()[1] == 20)
assert(zone_room:getDiscardCards()[1]:objectName() == "slash")
assert(#me:getKnownCards() == 1 and me:getKnownCards()[1]:getId() == 7)
assert(me:isHandVisible() == nil) -- The viewer's own view carries no flag.
assert(#rival:getKnownCards() == 1 and rival:getKnownCards()[1]:getId() == 40)
assert(rival:isHandVisible() == false)
-- A fully visible hand with nothing in it is known-empty, not unknown.
assert(#dead_rival:getKnownCards() == 0 and dead_rival:isHandVisible() == true)

local pile_names = me:getPileNames()
assert(#pile_names == 2 and pile_names[1] == "&stash" and pile_names[2] == "secret")
assert(#me:getPile("&stash") == 2 and me:getPile("&stash")[2] == 32)
assert(me:getPile("secret") == nil and me:getPileCount("secret") == 1)
assert(me:getPile("missing") == nil and me:getPileCount("missing") == nil)
assert(#me:getHandPile() == 2 and me:getHandPile()[1] == 31)
assert(me:getPileName(32) == "&stash" and me:getPileName(41) == nil)
assert(#me:getDisplayCards() == 1 and me:getDisplayCards()[1] == 7)
assert(rival:getDisplayCards() == nil and rival:getHandPile()[1] == nil)

-- Locations come from the snapshot only; an unseen id stays unknown.
assert(zone_room:getCardOwner(7) == me and zone_room:getCardPlace(7) == sgs.Player_PlaceHand)
assert(zone_room:getCardOwner(8) == me and zone_room:getCardPlace(8) == sgs.Player_PlaceEquip)
assert(zone_room:getCardOwner(40) == rival and zone_room:getCardPlace(40) == sgs.Player_PlaceHand)
assert(zone_room:getCardOwner(41) == rival and zone_room:getCardPlace(41) == sgs.Player_PlaceSpecial)
assert(zone_room:getCardPlace(20) == sgs.Player_DiscardPile and zone_room:getCardOwner(20) == nil)
assert(zone_room:getCardOwner(999) == nil and zone_room:getCardPlace(999) == nil)
assert(zone_room:isCardKnown(41) and not zone_room:isCardKnown(999))

-- Derived shared data: values, classification and slots, all from the snapshot.
local derived = request()
derived.world_view.self.max_cards = 4
derived.world_view.self.hujia = 2
derived.world_view.self.attack_range = 3
derived.world_view.self.gender = 1
derived.world_view.self.lord = true
derived.world_view.self.equip_slots = {[1] = 8, [3] = 9}
derived.world_view.self.equips = {{id = 8, effective_id = 8, name = "weapon",
    type_id = 3, handling_method = 1, virtual_card = false, target_fixed = true,
    damage_card = false, subcards = {}}}
derived.world_view.self.skills = {
    {name = "alpha", instance_id = 1, invalid = false, frequency = 2,
     skill_classes = {"LuaTriggerSkill", "TriggerSkill", "Skill"},
     lord_skill = true, attached_lord_skill = false, lord_skill_effective = true}
}
derived.world_view.hand_cards = {{id = 7, effective_id = 7, name = "slash",
    kind_of = {"Slash"}, subcards = {3, 4}, virtual_card = true, target_fixed = false}}
local derived_ai = assert(SmartAIView.new(derived))
local owner_view = derived_ai.player
assert(owner_view:getMaxCards() == 4 and owner_view:getHujia() == 2)
assert(owner_view:getAttackRange() == 3 and owner_view:getGender() == 1)
assert(owner_view:isLord() == true)
assert(owner_view:getEquip(1) == 8 and owner_view:getEquip(3) == 9)
assert(owner_view:getEquip(0) == nil and owner_view:hasEquip(0) == false)
assert(owner_view:hasEquip(1) == true and owner_view:hasEquip() == true)
local equip_card = owner_view:getEquips()[1]
assert(equip_card:getTypeId() == 3 and equip_card:getHandlingMethod() == 1)
assert(equip_card:targetFixed() == true and equip_card:isVirtualCard() == false)
assert(equip_card:isDamageCard() == false and #equip_card:getSubcards() == 0)
local hand_card = owner_view:getHandcards()[1]
assert(hand_card:isVirtualCard() == true and hand_card:subcardsLength() == 2)
assert(hand_card:getSubcards()[2] == 4)
local skill_view = owner_view:getSkills()[1]
assert(skill_view:getSkillClass() == "LuaTriggerSkill")
assert(skill_view:inherits("TriggerSkill") and skill_view:inherits("Skill"))
assert(skill_view:inherits("FilterSkill") == false)
assert(skill_view:getFrequency() == 2 and skill_view:isLordSkill() == true)
assert(skill_view:isLordSkillEffective() == true)
assert(skill_view:isAttachedLordSkill() == false)
-- Missing projections stay unknown instead of defaulting to a value.
local bare = PlayerView.new({object_name = "bare"})
assert(bare:getEquip(0) == nil and bare:hasEquip(0) == nil and bare.getMaxCards == nil)
assert(CardView.new({id = 1}):getSubcards() == nil)
assert(SkillView.new({name = "s"}):inherits("Skill") == nil)

-- Value-only string helpers behave like the gameplay ones.
assert(("a:b:c"):split(":")[2] == "b" and #("a:b"):split(":") == 2)
assert(("@@lianying"):startsWith("@@") and ("skill!"):endsWith("!"))
assert(("slash-jink"):contains("-") and not ("slash"):contains("+"))

-- Legal candidates and distances are answers the authority already computed.
local ruled = request()
ruled.world_view.distances = {viewer = {other = 1, dead = 2}, other = {viewer = 3}}
ruled.world_view.self.attack_range = 1
ruled.card_candidates = {
    {card_id = 7, available = true, limited = false, jilei = false,
     target_fixed = false, max_targets = 1, legal_targets = {"other"}},
    {card_id = 8, available = false, limited = true, jilei = true,
     target_fixed = true, max_targets = 0, legal_targets = {}}
}
local ruled_ai = assert(SmartAIView.new(ruled))
local ruled_room, ruled_me = ruled_ai.room, ruled_ai.player
local ruled_other = ruled_room:findPlayerByObjectName("other")
local candidates = ruled_ai:getCardCandidates()
assert(#candidates == 2 and AIValue.isCandidate(candidates[1]))
assert(not AIValue.isList(candidates[1]) and not AIValue.isCard(candidates[1]))
local playable = ruled_ai:getCardCandidate(7)
assert(playable:getCardId() == 7 and playable:isAvailable() == true)
assert(playable:isLimited() == false and playable:isJilei() == false)
assert(playable:targetFixed() == false and playable:getMaxTargets() == 1)
assert(playable:getLegalTargets()[1] == "other" and playable:canTarget("other"))
assert(playable:canTarget("dead") == false)
local blocked = ruled_ai:getCardCandidate(8)
assert(blocked:isAvailable() == false and blocked:isLimited() == true)
assert(#blocked:getLegalTargets() == 0 and blocked:canTarget("other") == false)
assert(ruled_ai:getCardCandidate(99) == nil)

assert(ruled_room:distanceTo(ruled_me, ruled_other) == 1)
assert(ruled_me:distanceTo(ruled_other) == 1 and ruled_me:distanceTo(ruled_me) == 0)
assert(ruled_other:distanceTo(ruled_me) == 3) -- Distance is directional.
assert(ruled_me:inMyAttackRange(ruled_other) == true)
assert(ruled_me:inMyAttackRange(ruled_room:findPlayerByObjectName("dead", true)) == false)
-- An unasked request carries no candidates, and a missing distance stays unknown.
local plain = assert(SmartAIView.new(request()))
assert(plain:getCardCandidates() == nil and plain:getCardCandidate(7) == nil)
assert(plain.room:distanceTo(plain.player, plain.room:getCurrent()) == nil)
assert(plain.player:inMyAttackRange(plain.room:getCurrent()) == nil)

-- Skill instance candidates: same-named instances stay apart and keep their source.
local acting = request()
acting.skill_actions = {
    {activation_owner = "viewer", activation_skill = "alpha", activation_instance = 1,
     source_owner = "viewer", source_skill = "alpha", source_instance = 1,
     activation_quota_available = true, source_quota_available = true},
    {activation_owner = "viewer", activation_skill = "alpha", activation_instance = 2,
     source_owner = "viewer", source_skill = "alpha", source_instance = 2,
     activation_quota_available = false, source_quota_available = true},
    {activation_owner = "viewer", activation_skill = "borrowed", activation_instance = 5,
     source_owner = "other", source_skill = "origin", source_instance = 3,
     activation_quota_available = true, source_quota_available = true}
}
acting.skill_action = acting.skill_actions[1]
local acting_ai = assert(SmartAIView.new(acting))
local actions = acting_ai:getSkillActions()
assert(#actions == 3 and AIValue.isSkillAction(actions[1]))
assert(actions[1]:getActivationInstanceId() == 1 and actions[2]:getActivationInstanceId() == 2)
assert(actions[1]:isActivationQuotaAvailable() and not actions[2]:isActivationQuotaAvailable())
assert(actions[1]:isValid() and actions[3]:isValid())
assert(actions[1]:isBorrowed() == false and actions[3]:isBorrowed() == true)
assert(actions[3]:getSourceOwner() == "other" and actions[3]:getSourceSkillName() == "origin")
assert(actions[3]:getSourceInstanceID() == 3)
-- Same name, different instance: the lookup must not collapse them.
assert(acting_ai:getSkillAction("alpha", 2):getActivationInstanceId() == 2)
assert(acting_ai:getSkillAction("alpha"):getActivationInstanceId() == 1)
assert(acting_ai:getSkillAction("missing") == nil)
assert(acting_ai:getSkillAction():getActivationInstanceId() == 1)
local answer = actions[3]:toAnswer()
assert(answer.skill == "borrowed" and answer.instance == 5 and answer.owner == "viewer")
local no_action = assert(SmartAIView.new(request()))
assert(no_action:getSkillActions() == nil and no_action:getSkillAction() == nil)
assert(no_action:getSkillAction("alpha") == nil)

-- Events arrive as ordered values; nothing native and no live "what is resolving now".
local evented = request()
evented.world_view.events = {
    {sequence = 1, revision = "7", trigger_event = 12, kind = "damage", from = "other",
     to = "viewer", card_name = "slash", reason = "", amount = 2, nature = 1,
     place = 0, good = false, targets = {}, card_ids = {5}},
    {sequence = 2, revision = "7", trigger_event = 20, kind = "card_move", from = "viewer",
     to = "other", card_name = "", reason = "", amount = 1, nature = 0,
     place = 3, good = false, targets = {}, card_ids = {}},
    {sequence = 3, revision = "8", trigger_event = 31, kind = "judge", from = "",
     to = "viewer", card_name = "peach", reason = "leiji", amount = 0, nature = 0,
     place = 0, good = true, targets = {"viewer"}, card_ids = {9}}
}
local evented_ai = assert(SmartAIView.new(evented))
local events = evented_ai:getEvents()
assert(#events == 3 and AIValue.isEvent(events[1]))
assert(events[1]:getKind() == "damage" and events[1]:getSequence() == 1)
assert(events[1]:getFrom() == "other" and events[1]:getTo() == "viewer")
assert(events[1]:getAmount() == 2 and events[1]:getNature() == 1)
assert(events[1]:getCardName() == "slash" and events[1]:getCardIds()[1] == 5)
assert(events[1]:getRevision() == "7" and events[1]:getTriggerEvent() == 12)
assert(events[2]:getKind() == "card_move" and #events[2]:getCardIds() == 0)
assert(events[3]:isGood() == true and events[3]:getReason() == "leiji")
assert(events[3]:getTargets()[1] == "viewer")
-- Filtering by kind keeps the order and the last one is the most recent.
local damages = evented_ai:getEvents("damage")
assert(#damages == 1 and damages[1]:getSequence() == 1)
assert(evented_ai:getLastEvent():getSequence() == 3)
assert(evented_ai:getLastEvent("card_move"):getSequence() == 2)
assert(#evented_ai:getEvents("dying") == 0 and evented_ai:getLastEvent("dying") == nil)
-- Editing a returned event cannot reach the snapshot.
events[1]._view.amount = 99
assert(evented_ai:getEvents()[1]:getAmount() == 2)
-- A snapshot without the projection says unknown, not "nothing happened".
local silent = assert(SmartAIView.new(request()))
assert(silent:getEvents() == nil and silent:getLastEvent() == nil)

-- Observer memory: plain values only, scoped to the viewer, gone when the VM is rebuilt.
ai_memory.clear()
local remembering = request()
remembering.state_revision = "7"
local memo_ai = assert(SmartAIView.new(remembering))
assert(memo_ai:recall("intent") == nil)
assert(memo_ai:remember("intent", {other = -3}) == true)
assert(memo_ai:recall("intent").other == -3)
-- The stored copy is independent of the caller's table.
local stored = memo_ai:recall("intent")
stored.other = 99
assert(memo_ai:recall("intent").other == -3)
memo_ai:rememberAt("guess", "rebel")
local guess, guess_revision = memo_ai:recallAt("guess")
assert(guess == "rebel" and guess_revision == "7")
-- Another observer in the same Room never sees it.
local other_request = request()
other_request.viewer = "other"
other_request.world_view.self = {object_name = "other", alive = true, dead = false,
    equips = {}, judging_area = {}, public_marks = {}, skills = {}}
other_request.world_view.players = {{object_name = "viewer", alive = true, dead = false,
    equips = {}, judging_area = {}, public_marks = {}, skills = {}}}
other_request.world_view.player_order = {"other", "viewer"}
other_request.world_view.alive_player_order = {"other", "viewer"}
other_request.world_view.current_player = "other"
local other_ai = assert(SmartAIView.new(other_request))
assert(other_ai:recall("intent") == nil)
assert(ai_memory.recall("viewer", "intent").other == -3)
-- Proxies, functions and oversized values never enter the memory.
assert(not pcall(function() memo_ai:remember("bad", memo_ai.player) end))
assert(not pcall(function() memo_ai:remember("bad", function() end) end))
assert(not pcall(function() memo_ai:remember("bad", memo_ai.room:getPlayers()) end))
assert(not pcall(function() memo_ai:remember(7, "value") end))
memo_ai:remember("intent", nil)
assert(memo_ai:recall("intent") == nil)
ai_memory.forget("viewer")
assert(memo_ai:recallAt("guess") == nil)

-- Mode and identity: rule relations and believed relations never substitute for each other.
local ruled_mode = request()
ruled_mode.state_revision = "9"
ruled_mode.world_view.self.kingdom = "wu"
ruled_mode.world_view.self.lord = true
ruled_mode.world_view.self.controller = "viewer"
ruled_mode.world_view.players[1].kingdom = "wu"
ruled_mode.world_view.players[1].controller = "viewer"
ruled_mode.world_view.players[2].controller = "dead"
local mode_ai = assert(SmartAIView.new(ruled_mode))
local mode_room, me2 = mode_ai.room, mode_ai.player
local ally = mode_room:findPlayerByObjectName("other")
assert(mode_ai:isModeManaged() == true and mode_ai:requireModePolicy() == true)
assert(mode_room:getLord() == me2)
assert(#mode_room:getLieges() == 1 and mode_room:getLieges()[1] == ally)
assert(#mode_room:getLieges("wu") == 1 and #mode_room:getLieges("shu") == 0)
assert(me2:isSameKingdom(ally) == true)
assert(ally:isControlledBy(me2) == true and me2:isControlledBy(ally) == false)
assert(mode_ai:sharesController(me2, ally) == true)
assert(mode_ai:sharesController(me2, mode_room:findPlayerByObjectName("dead", true)) == false)
-- A hidden kingdom is unknown, never "a different kingdom".
local hidden = request()
hidden.world_view.self.kingdom = "wu"
local hidden_ai = assert(SmartAIView.new(hidden))
assert(hidden_ai.player:isSameKingdom(hidden_ai.room:getCurrent()) == nil)
assert(hidden_ai.room:getLord() == nil)
-- An unmanaged mode says so instead of claiming neutrality.
local unmanaged = request()
unmanaged.world_view.mode_policy = {managed = false}
local unmanaged_ai = assert(SmartAIView.new(unmanaged))
assert(unmanaged_ai:isModeManaged() == false and unmanaged_ai:requireModePolicy() == nil)
assert(unmanaged_ai:relationTo(unmanaged_ai.room:getCurrent()) == nil)
assert(unmanaged_ai:isFriend(unmanaged_ai.room:getCurrent()) == nil)
assert(unmanaged_ai:getEnemies() == nil and unmanaged_ai:getFriends() == nil)
-- Belief is stored per observer with the revision it was formed at.
ai_memory.forget("viewer")
assert(mode_ai:believedRelationTo(ally) == nil)
mode_ai:believeRelation(ally, "enemy")
local believed, believed_at = mode_ai:believedRelationTo(ally)
assert(believed == "enemy" and believed_at == "9")
assert(mode_ai:relationTo(ally) == "enemy" or mode_ai:relationTo(ally) == "friend")
ai_memory.forget("viewer")

-- Legacy callback ABI: a value request view, scoped to the request's skill action.
local plain = request()
assert(AILegacyRequest.new(plain, nil) == nil)
local skill_request = request()
skill_request.decision_id = "12"
skill_request.state_revision = "7"
skill_request.pattern = "@@adapter!"
skill_request.prompt = "@adapter:viewer"
skill_request.handling_method = 2
skill_request.skill_action = {activation_owner = "viewer", activation_skill = "alpha",
    activation_instance = 4, source_owner = "viewer", source_skill = "beta",
    source_instance = 2, activation_quota_available = true, source_quota_available = false}
local skill_ai = assert(SmartAIView.new(skill_request))
local legacy = assert(AILegacyRequest.new(skill_request, skill_ai.room))
assert(legacy:isValid() and AIValue.kind(legacy) == "request" and not AIValue.isList(legacy))
assert(legacy:getDecisionId() == "12" and legacy:getStateRevision() == "7")
assert(legacy:getPattern() == "@@adapter!") -- The view keeps the native raw pattern.
assert(legacy:getPrompt() == "@adapter:viewer" and legacy:getHandlingMethod() == 2)
assert(legacy:getActivationOwner() == "viewer" and legacy:getActivationSkillName() == "alpha")
assert(legacy:getActivationInstanceId() == 4 and legacy:getSourceSkillName() == "beta")
assert(legacy:getSourceOwner() == "viewer" and legacy:getSourceInstanceID() == 2)
assert(legacy:isActivationQuotaAvailable() and not legacy:isSourceQuotaAvailable())
assert(legacy:getInitiator() == skill_ai.player) -- Identity projection, not a ServerPlayer.
-- Enums that the sandbox does not project stay absent instead of comparing against nil.
assert(legacy.getReason == nil and legacy.getDecisionKind == nil and legacy.getRoom == nil)
local invalid_instance = request()
invalid_instance.skill_action = {activation_owner = "viewer", activation_skill = "alpha",
    activation_instance = 0, source_owner = "viewer", source_skill = "beta",
    source_instance = 2}
assert(not AILegacyRequest.new(invalid_instance, nil):isValid())
local missing_source = request()
missing_source.skill_action = {activation_owner = "viewer", activation_skill = "alpha",
    activation_instance = 4, source_owner = "viewer", source_instance = 2}
assert(not AILegacyRequest.new(missing_source, nil):isValid())

-- Result conversion keeps unhandled, declined, pass and use_card apart.
local converted, status = AIResultValue.normalize(nil)
assert(converted == nil and status == "unhandled")
converted, status = AIResultValue.normalize(".")
assert(status == "declined" and converted.kind == "pass")
converted, status = AIResultValue.normalize("")
assert(status == "declined" and converted.kind == "pass")
converted, status = AIResultValue.normalize("@Card=.->other")
assert(status == "use_card" and converted.kind == "use_card" and converted.card == "@Card=.->other")
converted, status = AIResultValue.normalize({kind = "pass"})
assert(status == "pass" and converted.kind == "pass")
converted, status = AIResultValue.normalize({kind = "use_card", card = "slash",
    cards = {7}, targets = {"other"}, user_string = "wu"})
assert(status == "use_card" and converted.card == "slash" and converted.user_string == "wu")
assert(converted.cards[1] == 7 and converted.targets[1] == "other")
assert(AIResultValue.normalize({kind = "use_card"}).cards == nil) -- Missing is not empty.
local legacy_answer = {accepted = true, cards = {7}, targets = {"other"}}
converted, status = AIResultValue.normalize(legacy_answer)
assert(status == "use_card" and converted.kind == "use_card" and converted.accepted == nil)
assert(converted.cards[1] == 7 and converted.targets[1] == "other")
converted.cards[1] = 9
assert(legacy_answer.cards[1] == 7) -- The conversion copies the answer it was handed.
-- The documented V2 answer carries only cards/targets/user_string.
converted, status = AIResultValue.normalize({cards = {7}, targets = {"other"}, user_string = ""})
assert(status == "use_card" and converted.kind == "use_card" and converted.cards[1] == 7)
assert(converted.targets[1] == "other" and converted.user_string == "")
converted, status = AIResultValue.normalize({accepted = false})
assert(status == "pass" and converted.kind == "pass")
-- A malformed answer is an error, never a silent pass.
assert(not pcall(AIResultValue.normalize, true) and not pcall(AIResultValue.normalize, 7))
assert(not pcall(AIResultValue.normalize, {}) and not pcall(AIResultValue.normalize, {kind = "discard"}))
assert(not pcall(AIResultValue.normalize, {accepted = "yes"}))
assert(not pcall(AIResultValue.normalize, {user_string = 7}))
assert(not pcall(AIResultValue.normalize, {kind = "use_card", card = 7}))
assert(not pcall(AIResultValue.normalize, {kind = "use_card", user_string = {}}))
assert(not pcall(AIResultValue.normalize, {accepted = true, cards = {[2] = 7}}))
assert(not pcall(AIResultValue.normalize, {accepted = true, targets = {other = "x"}}))
-- A value card spec describes the card to build; the AI never builds one itself.
local specced, spec_status = AIResultValue.normalize({kind = "use_card",
    card_spec = {name = "slash", suit = 1, number = 5, skill = "longdan", subcards = {7}},
    targets = {"other"}}, "use_card")
assert(spec_status == "use_card" and specced.card == nil)
assert(specced.card_spec.name == "slash" and specced.card_spec.suit == 1)
assert(specced.card_spec.number == 5 and specced.card_spec.skill == "longdan")
assert(specced.card_spec.subcards[1] == 7 and specced.targets[1] == "other")
local minimal = AIResultValue.normalize({kind = "use_card", card_spec = {name = "jink"}})
assert(minimal.card_spec.name == "jink" and minimal.card_spec.suit == nil)
assert(minimal.card_spec.subcards == nil)
-- A malformed or double answer is an error, never a half-built card.
assert(not pcall(AIResultValue.normalize, {kind = "use_card", card_spec = {}}))
assert(not pcall(AIResultValue.normalize, {kind = "use_card", card_spec = {name = ""}}))
assert(not pcall(AIResultValue.normalize, {kind = "use_card", card_spec = "slash"}))
assert(not pcall(AIResultValue.normalize,
    {kind = "use_card", card = "slash:.", card_spec = {name = "slash"}}))
assert(not pcall(AIResultValue.normalize,
    {kind = "use_card", card_spec = {name = "slash", suit = "heart"}}))
assert(not pcall(AIResultValue.normalize,
    {kind = "use_card", card_spec = {name = "slash", subcards = {[2] = 7}}}))

-- Value-typed answers convert per decision kind; a card answer never leaks into them.
local answered, answer_status = AIResultValue.normalize("shu", "choice")
assert(answer_status == "answer" and answered.kind == "answer" and answered.answer == "shu")
assert(AIResultValue.normalize(nil, "choice") == nil)
assert(select(2, AIResultValue.normalize(nil, "choice")) == "unhandled")
answered, answer_status = AIResultValue.normalize("", "kingdom")
assert(answer_status == "declined" and answered.kind == "pass")
answered, answer_status = AIResultValue.normalize(true, "skill_invoke")
assert(answer_status == "answer" and answered.answer == "yes")
answered, answer_status = AIResultValue.normalize(false, "skill_invoke")
assert(answer_status == "declined" and answered.kind == "pass")
answered, answer_status = AIResultValue.normalize({kind = "pass"}, "suit")
assert(answer_status == "pass" and answered.kind == "pass")
answered, answer_status = AIResultValue.normalize({kind = "answer", answer = "spade"}, "suit")
assert(answer_status == "answer" and answered.answer == "spade")
-- Only a skill invoke may answer with a boolean, and an answer is never a card action.
assert(not pcall(AIResultValue.normalize, true, "choice"))
assert(not pcall(AIResultValue.normalize, 7, "choice"))
assert(not pcall(AIResultValue.normalize, {kind = "use_card", card = "slash"}, "choice"))
assert(not pcall(AIResultValue.normalize, {kind = "answer"}, "choice"))
assert(not pcall(AIResultValue.normalize, {kind = "answer", answer = ""}, "choice"))
-- The card kinds keep their own conversion: a bare string is still a card string.
answered, answer_status = AIResultValue.normalize("@Card=.", "use_card")
assert(answer_status == "use_card" and answered.card == "@Card=.")
-- Selections answer with ids or object names, per decision kind.
local picked, picked_status = AIResultValue.normalize(7, "amazing_grace")
assert(picked_status == "answer" and picked.cards[1] == 7 and picked.targets == nil)
picked, picked_status = AIResultValue.normalize({1, 2}, "discard")
assert(picked_status == "answer" and picked.cards[2] == 2)
picked, picked_status = AIResultValue.normalize("other", "player_chosen")
assert(picked_status == "answer" and picked.targets[1] == "other" and picked.cards == nil)
picked, picked_status = AIResultValue.normalize({"a", "b"}, "players_chosen")
assert(picked_status == "answer" and picked.targets[2] == "b")
picked, picked_status = AIResultValue.normalize(
    {kind = "answer", cards = {7}, targets = {"heir"}}, "yiji")
assert(picked_status == "answer" and picked.cards[1] == 7 and picked.targets[1] == "heir")
picked, picked_status = AIResultValue.normalize({kind = "pass"}, "discard")
assert(picked_status == "pass" and picked.kind == "pass")
assert(AIResultValue.normalize(nil, "discard") == nil)
-- A card response answers with one owned card id; C++ still checks the ownership.
picked, picked_status = AIResultValue.normalize(12, "respond_card")
assert(picked_status == "answer" and picked.cards[1] == 12)
assert(not pcall(AIResultValue.normalize, "peach", "respond_card"))

-- Guanxing keeps two ordered piles; the bottom one is never derived from the top.
picked, picked_status = AIResultValue.normalize(
    {kind = "answer", cards = {1, 2}, bottom_cards = {3}}, "guanxing")
assert(picked_status == "answer" and picked.cards[2] == 2 and picked.bottom_cards[1] == 3)
picked = AIResultValue.normalize({kind = "answer", bottom_cards = {3, 1}}, "guanxing")
assert(picked.cards == nil and picked.bottom_cards[2] == 1)
assert(not pcall(AIResultValue.normalize, {1, 2}, "guanxing"))
assert(not pcall(AIResultValue.normalize, {kind = "answer", bottom_cards = {"a"}}, "guanxing"))
-- The trigger order answers with the skill string the call site understands.
picked, picked_status = AIResultValue.normalize("qicai:other", "trigger_order")
assert(picked_status == "answer" and picked.answer == "qicai:other")

-- A kind never accepts the other kind's shape, and yiji needs both halves named.
assert(not pcall(AIResultValue.normalize, "name", "discard"))
assert(not pcall(AIResultValue.normalize, 7, "player_chosen"))
assert(not pcall(AIResultValue.normalize, 7, "yiji"))
assert(not pcall(AIResultValue.normalize, {"a"}, "yiji"))
assert(not pcall(AIResultValue.normalize, {1, "a"}, "discard"))
assert(not pcall(AIResultValue.normalize, {true}, "discard"))
assert(not pcall(AIResultValue.normalize, {[2] = 1}, "discard"))
assert(not pcall(AIResultValue.normalize, {kind = "answer"}, "discard"))
assert(not pcall(AIResultValue.normalize, {kind = "answer", cards = {"a"}}, "discard"))
assert(not pcall(AIResultValue.normalize, true, "discard"))
return true
