#include "game-state-contract.h"

#include <QCoreApplication>
#include <QDebug>
#include <cstdlib>
#include <stdexcept>
#include <type_traits>

using namespace GameState;

namespace {
int checks = 0;
QString error;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    qCritical() << "CHECK failed:" << #condition << "line" << __LINE__ << error; \
    std::abort(); } } while (false)

static_assert(!std::is_copy_constructible<WorldStore>::value, "world owner cannot be copied");
static_assert(!std::is_move_constructible<WorldStore>::value, "world owner must have stable identity");
struct Faults {
    bool throwPrepare = false;
    bool rejectPrepare = false;
    bool rejectValidation = false;
    bool changeScope = false;
    int calls = 0;
    std::function<void()> duringPrepare;
};

ProviderRegistry providers(const std::shared_ptr<Faults> &faults)
{
    ProviderRegistry registry;
    ProviderContract skill;
    skill.id = "fixture.skill";
    skill.version = 1;
    skill.audit = "Test-only guard skill: immutable code; every counter and target is managed state.";
    skill.skillDefinitions = {"fixture.guard"};
    skill.prepare = [faults](WorldState &world, QString *) {
        ++faults->calls;
        if (faults->duringPrepare) faults->duringPrepare();
        if (faults->changeScope) world.turn.turnScopeId = "forged-other-turn";
        if (faults->throwPrepare || faults->rejectPrepare) {
            world.players["p1"].hp = 0;
            world.roomTags["partially_restored"] = true;
            if (faults->throwPrepare) throw std::runtime_error("injected after mutation");
            return false;
        }
        return true;
    };
    skill.validate = [faults](const WorldState &, QString *) { return !faults->rejectValidation; };
    CHECK(registry.registerProvider(skill, &error));
    ProviderContract ai;
    ai.id = "fixture.ai";
    ai.version = 2;
    ai.audit = "Test-only AI: remembered target and confidence are managed values, no mutable upvalues.";
    CHECK(registry.registerProvider(ai, &error));
    CHECK(registry.registerNativePackage("fixture", "1", "Only these test value transitions; no clock/audio/global state.", &error));
    return registry;
}

WorldState initial()
{
    WorldState state;
    state.rootGameId = "match";
    state.worldId = "logical-room";
    state.completeDomains = requiredDomains();
    state.nativePackages.insert("fixture", "1");
    PlayerState a; a.id = "p1"; a.hp = 3; a.maxHp = 4; a.armor = 2;
    a.marks.insert("guard", 1); a.usageHistory.insert("slash", 2);
    a.tags.insert("target", reference("player", "p2"));
    a.properties.insert("general", "fixture.hero");
    PlayerState b; b.id = "p2"; b.hp = 2; b.maxHp = 3; b.armor = 1;
    b.faceUp = false; b.chained = true;
    state.players.insert(a.id, a); state.players.insert(b.id, b);
    state.seatOrder = {"p1", "p2"};
    for (int i = 1; i <= 12; ++i) {
        CardState card; card.id = QString::number(i); card.definition = "fixture.slash";
        card.suit = i % 4; card.number = i;
        card.state = {{"flag", "visible"}, {"owner", reference("player", "p1")}};
        state.cards.insert(card.id, card);
    }
    state.zones = {
        {"draw", ZoneKind::Draw, {}, {}, {"3", "1", "2"}},
        {"discard", ZoneKind::Discard, {}, {}, {"4"}},
        {"table", ZoneKind::Table, {}, {}, {"5"}},
        {"void", ZoneKind::Void, {}, {}, {"12"}},
        {"p1.hand", ZoneKind::Hand, "p1", {}, {"7", "6"}},
        {"p1.equip", ZoneKind::Equip, "p1", {}, {"8"}},
        {"p1.judge", ZoneKind::Judge, "p1", {}, {"9"}},
        {"p1.pile", ZoneKind::Private, "p1", "guard", {"11", "10"}},
        {"p2.hand", ZoneKind::Hand, "p2", {}, {}},
        {"p2.equip", ZoneKind::Equip, "p2", {}, {}},
        {"p2.judge", ZoneKind::Judge, "p2", {}, {}}
    };
    SkillState skill; skill.id = "p1.guard.1"; skill.definition = "fixture.guard";
    skill.ownerId = "p1"; skill.providerId = "fixture.skill";
    skill.state = {{"uses", 2}, {"target", reference("player", "p2")}};
    skill.correctState = {{"amount", 3}};
    state.skills.insert(skill.id, skill);
    state.roomTags = {{"card", reference("card", "7")}, {"skill", reference("skill", skill.id)}};
    state.turn.playerId = "p1"; state.turn.phase = "RoundStart";
    state.turn.turnScopeId = "turn-1"; state.turn.roundScopeId = "round-1";
    GameRng rng; rng.seed(12345); rng.bounded(13); rng.bounded(6);
    state.gameplayRng = rng.exportState();
    rng.seed(999); rng.bounded(3); state.aiRng = rng.exportState();
    state.history.events = {{"1", {}, "round", {{"player", reference("player", "p1")}}},
                            {"2", "1", "turn", {{"skill", reference("skill", skill.id)}}}};
    state.history.activeEventIds = {"1", "2"}; state.history.nextEventId = 3;
    state.providers.insert("fixture.skill", {1, {{"enabled", true}}});
    state.providers.insert("fixture.ai", {2, {{"target", reference("player", "p2")}, {"confidence", 0.75}}});
    return state;
}

void validationTests()
{
    auto faults = std::make_shared<Faults>();
    const auto registry = providers(faults);
    const auto base = initial();
    CHECK(validateWorld(base, registry, &error));
    auto reject = [&](const std::function<void(WorldState &)> &change) {
        WorldState bad = base; change(bad);
        CHECK(!validateWorld(bad, registry, &error));
    };
    reject([](WorldState &s) { ++s.version; });
    reject([](WorldState &s) { s.completeDomains.removeLast(); });
    reject([](WorldState &s) { s.providers.remove("fixture.ai"); });
    reject([](WorldState &s) { ++s.providers["fixture.ai"].version; });
    reject([](WorldState &s) { s.nativePackages.insert("standard", "NosRende unaudited clock"); });
    reject([](WorldState &s) { s.unsupportedCapabilities << "unmanaged closure upvalue"; });
    reject([](WorldState &s) { s.players["p1"].tags["bad"] = reference("player", "other-world-player"); });
    reject([](WorldState &s) { s.roomTags["bad"] = reference("object", "p1"); });
    reject([](WorldState &s) { s.roomTags["bad"] = QVariantMap{{"$ref", QVariantMap{{"kind", 1}, {"id", "p1"}}}}; });
    reject([](WorldState &s) { s.roomTags["bad"] = QVariant::fromValue(static_cast<void *>(&s)); });
    reject([](WorldState &s) { s.zones[1].cards << "3"; });
    reject([](WorldState &s) { s.zones[0].cards.removeLast(); });
    reject([](WorldState &s) { s.zones.removeLast(); });
    reject([](WorldState &s) { s.zones[4].name = "not-a-private-pile"; });
    reject([](WorldState &s) { s.turn.phase = "Unrecognized"; });
    reject([](WorldState &s) { s.skills["p1.guard.1"].parentId = "p1.guard.1"; });
    reject([](WorldState &s) { s.skills["p1.guard.1"].definition = "unaudited.skill"; });
    reject([](WorldState &s) { s.history.events[0].parentId = "2"; });
    reject([](WorldState &s) { s.history.nextEventId = 2; });
    reject([](WorldState &s) { s.history.events[0].id = "01"; });
    reject([](WorldState &s) { s.gameplayRng.algorithm = 700; });
    reject([](WorldState &s) { s.turn.continuations << "native nested stack"; });
    reject([](WorldState &s) { s.turn.pendingExtraTurns << QVariantMap{{"player", "p2"}}; });
    reject([](WorldState &s) { s.turn.pendingRequests << "request"; });
    reject([](WorldState &s) { s.turn.timers << "timer"; });
    // Coverage/version/ref preflight rejects before invoking any restore callback.
    auto bad = base; bad.providers.remove("fixture.ai");
    const int before = faults->calls;
    CHECK(!WorldStore::create(bad, registry, &error));
    CHECK(faults->calls == before);
    ProviderRegistry unsupported;
    ProviderContract raw; raw.id = "legacy"; raw.version = 1; raw.audit = "Known unsupported";
    raw.unsupportedCapabilities = {"userdata", "table alias", "metatable", "mutable closure"};
    CHECK(unsupported.registerProvider(raw, &error));
    auto unknown = base; unknown.nativePackages.clear(); unknown.skills.clear(); unknown.roomTags.clear();
    unknown.history.events.clear(); unknown.history.activeEventIds.clear();
    unknown.providers.clear(); unknown.providers.insert("legacy", {1, {}});
    CHECK(!validateWorld(unknown, unsupported, &error));
}

void restoreTests()
{
    auto faults = std::make_shared<Faults>();
    const auto registry = providers(faults);
    auto store = WorldStore::create(initial(), registry, &error);
    CHECK(store);
    const auto original = store->capture();
    const auto round1 = store->checkpoint(GameTimeline::AnchorKind::FullRound, "round-1", {}, &error);
    const auto turn1 = store->checkpoint(GameTimeline::AnchorKind::PlayerTurn, "turn-1", "p1", &error);
    CHECK(!round1.id.isEmpty() && !turn1.id.isEmpty());
    const auto beforeDuplicate = store->capture();
    const auto duplicateRevision = store->revision();
    CHECK(store->checkpointUpdate([](WorldState &s, QString *) { s.players["p1"].hp = 1; return true; },
        GameTimeline::AnchorKind::PlayerTurn, "turn-1", "p1", &error).id.isEmpty());
    CHECK(store->capture() == beforeDuplicate && store->revision() == duplicateRevision);
    const auto anchorsBeforePair = store->timeline().anchors().size();
    CHECK(store->checkpointUpdate([](WorldState &s, QString *) {
        s.turn.roundScopeId = "detached-round"; return true;
    }, GameTimeline::AnchorKind::PlayerTurn, "turn-1", "p1", &error, true).id.isEmpty());
    CHECK(store->capture() == beforeDuplicate && store->revision() == duplicateRevision);
    CHECK(store->timeline().anchors().size() == anchorsBeforePair); // The first of a paired anchor never leaks.
    CHECK(store->update([](WorldState &s, QString *) {
        s.players["p1"].hp = 1; s.players["p1"].armor = 0; s.players["p1"].marks["guard"] = 0;
        s.players["p1"].usageHistory["slash"] = 9;
        s.players["p1"].tags.clear(); s.players["p2"].faceUp = true;
        s.zones[0].cards.swapItemsAt(0, 1);
        s.skills["p1.guard.1"].state["uses"] = 3;
        s.skills["p1.guard.1"].correctState["amount"] = 6;
        s.providers["fixture.ai"].state["confidence"] = 0.1;
        s.roomTags.clear(); s.turn.playerId = "p2"; s.turn.phase = "Play";
        s.turn.turnScopeId = "turn-2";
        GameRng rng; if (!rng.restore(s.gameplayRng)) return false;
        rng.generate(); s.gameplayRng = rng.exportState();
        s.history.events.append({"3", "2", "damage", {{"amount", 2}}});
        ++s.history.nextEventId;
        return true;
    }, &error));
    CHECK(store->checkpoint(GameTimeline::AnchorKind::PlayerTurn, "turn-2", "p2", &error).id.isEmpty());
    CHECK(store->update([](WorldState &s, QString *) { s.turn.phase = "RoundStart"; return true; }, &error));
    CHECK(store->checkpoint(static_cast<GameTimeline::AnchorKind>(99), "turn-2", "p2", &error).id.isEmpty());
    CHECK(store->checkpoint(GameTimeline::AnchorKind::FullRound, "round-1", "p2", &error).id.isEmpty());
    const auto turn2 = store->checkpoint(GameTimeline::AnchorKind::PlayerTurn, "turn-2", "p2", &error);
    CHECK(!turn2.id.isEmpty());
    CHECK(store->timeline().previousPlayerTurn().id == turn1.id);
    CHECK(store->update([](WorldState &s, QString *) { s.turn.turnScopeId = "extra-1"; s.turn.extraTurn = true; return true; }, &error));
    CHECK(!store->checkpoint(GameTimeline::AnchorKind::ExtraTurn, "extra-1", "p2", &error).id.isEmpty());
    CHECK(store->checkpoint(GameTimeline::AnchorKind::FullRound, "round-1", {}, &error).id.isEmpty());
    CHECK(store->timeline().previousPlayerTurn().id == turn1.id);
    CHECK(store->update([](WorldState &s, QString *) { s.turn.extraTurn = false; s.turn.roundScopeId = "round-9"; s.turn.turnScopeId = "turn-3"; return true; }, &error));
    const auto round2 = store->checkpoint(GameTimeline::AnchorKind::FullRound, "round-9", {}, &error);
    CHECK(!round2.id.isEmpty());
    CHECK(store->timeline().previousFullRound().id == round1.id); // Explicit scope, not seat count/round arithmetic.
    const auto changed = store->capture();
    const auto generation = store->timeline().generation();
    faults->changeScope = true;
    CHECK(!store->prepareRestore(turn1.id, &error));
    CHECK(store->capture() == changed && store->timeline().generation() == generation);
    faults->changeScope = false;
    faults->throwPrepare = true;
    CHECK(!store->prepareRestore(turn1.id, &error));
    CHECK(store->capture() == changed && store->timeline().generation() == generation);
    faults->throwPrepare = false; faults->rejectPrepare = true;
    CHECK(!store->prepareRestore(turn1.id, &error));
    CHECK(store->capture() == changed && store->timeline().generation() == generation);
    faults->rejectPrepare = false; faults->rejectValidation = true;
    CHECK(!store->prepareRestore(turn1.id, &error));
    CHECK(store->capture() == changed && store->timeline().generation() == generation);
    faults->rejectValidation = false;
    auto candidate = store->prepareRestore(turn1.id, &error);
    CHECK(candidate);
    CHECK(store->capture() == changed); // Preparation never publishes.
    auto other = WorldStore::create(initial(), registry, &error);
    CHECK(other);
    CHECK(!other->publish(std::move(*candidate), &error));
    CHECK(other->timeline().generation() == 0);
    const auto oldRequest = store->timeline().issueRequest("p1", "choose-card");
    CHECK(!store->publish(std::move(*candidate), &error)); // Timeline changed during prepare.
    candidate = store->prepareRestore(turn1.id, &error);
    CHECK(candidate && store->publish(std::move(*candidate), &error));
    CHECK(!store->publish(std::move(*candidate), &error)); // One shot.
    CHECK(store->timeline().generation() == 1 && store->timeline().rootGameId() == "match");
    CHECK(!store->timeline().acceptDecision(oldRequest, {{"card", "1"}}, "human", &error));
    CHECK(!store->timeline().acceptTimeout(oldRequest, {}, &error));
    const auto &restored = store->state();
    CHECK(restored.players["p1"].hp == original->players["p1"].hp && restored.players["p1"].armor == 2);
    CHECK(restored.players["p1"].marks == original->players["p1"].marks);
    CHECK(restored.players["p1"].usageHistory == original->players["p1"].usageHistory);
    CHECK(restored.players["p1"].tags == original->players["p1"].tags && !restored.players["p2"].faceUp);
    for (qsizetype i = 0; i < original->zones.size(); ++i) CHECK(restored.zones[i].cards == original->zones[i].cards);
    CHECK(restored.skills["p1.guard.1"].state == original->skills["p1.guard.1"].state);
    CHECK(restored.skills["p1.guard.1"].correctState == original->skills["p1.guard.1"].correctState);
    CHECK(restored.roomTags == original->roomTags && restored.turn.phase == "RoundStart");
    CHECK(restored.providers["fixture.ai"].state == original->providers["fixture.ai"].state);
    CHECK(restored.history.events.size() == 2 && restored.history.nextEventId == 3);
    CHECK(restored.history.activeEventIds == original->history.activeEventIds);
    GameRng expected, actual;
    CHECK(expected.restore(original->gameplayRng) && actual.restore(restored.gameplayRng));
    for (int i = 0; i < 12; ++i) CHECK(expected.bounded(17) == actual.bounded(17));
    CHECK(expected.restore(original->aiRng) && actual.restore(restored.aiRng));
    CHECK(expected.generate() == actual.generate());
    CHECK(!store->prepareRestore(round2.id, &error)); // Abandoned future ancestry.
    candidate = store->prepareRestore(round1.id, &error);
    CHECK(candidate && store->publish(std::move(*candidate), &error));
    CHECK(store->timeline().generation() == 2 && store->timeline().branchId() == "2");
    candidate = store->prepareRestore(round1.id, &error);
    CHECK(candidate && store->publish(std::move(*candidate), &error));
    CHECK(store->timeline().generation() == 3);
    candidate = store->prepareRestore(round1.id, &error);
    CHECK(store->update([](WorldState &s, QString *) { ++s.players["p1"].armor; return true; }, &error));
    CHECK(!store->publish(std::move(*candidate), &error));
    CHECK(store->timeline().generation() == 3 && store->state().players["p1"].armor == 3);
    GameTimeline::RequestToken duringPreparation;
    faults->duringPrepare = [&] { duringPreparation = store->timeline().issueRequest("p1", "intervening-request"); };
    candidate = store->prepareRestore(round1.id, &error);
    faults->duringPrepare = {};
    CHECK(candidate && !store->publish(std::move(*candidate), &error));
    CHECK(store->timeline().isPending(duringPreparation) && store->timeline().generation() == 3);
}

void pairedAnchorTests()
{
    auto store = WorldStore::create(initial(), providers(std::make_shared<Faults>()), &error);
    CHECK(store);
    const auto turn = store->checkpointUpdate({}, GameTimeline::AnchorKind::PlayerTurn,
        "turn-1", "p1", &error, true);
    CHECK(!turn.id.isEmpty());
    const auto round = store->timeline().anchors().first();
    CHECK(round.kind == GameTimeline::AnchorKind::FullRound);
    CHECK(store->checkpointState(round.id) == store->checkpointState(turn.id));
    CHECK(store->update([](WorldState &s, QString *) {
        s.turn.turnScopeId = "turn-2"; s.turn.playerId = "p2"; return true;
    }, &error));
    const auto future = store->checkpoint(GameTimeline::AnchorKind::PlayerTurn, "turn-2", "p2", &error);
    CHECK(!future.id.isEmpty());
    auto candidate = store->prepareRestore(round.id, &error);
    CHECK(candidate && store->publish(std::move(*candidate), &error));
    CHECK(!store->timeline().anchor(turn.id).id.isEmpty());
    CHECK(store->timeline().anchor(future.id).id.isEmpty());
    CHECK(store->timeline().anchors().size() == 2);
}

void checkpointRetentionTests()
{
    auto faults = std::make_shared<Faults>();
    const auto registry = providers(faults);
    WorldStore::CheckpointRetention retention;
    retention.playerTurns = 2;
    retention.fullRounds = 3;
    retention.otherAnchors = 2;
    auto store = WorldStore::create(initial(), registry, retention, &error);
    CHECK(store);
    auto findAnchor = [&](GameTimeline::AnchorKind kind, const QString &scope) {
        for (const auto &anchor : store->timeline().anchors())
            if (anchor.kind == kind && anchor.scopeId == scope) return anchor;
        return GameTimeline::Anchor{};
    };

    auto addTurn = [&](const QString &turn, const QString &player, const QString &round,
                       bool beginsRound) {
        CHECK(store->update([&](WorldState &state, QString *) {
            state.turn.phase = QStringLiteral("RoundStart");
            state.turn.turnScopeId = turn;
            state.turn.playerId = player;
            state.turn.roundScopeId = round;
            state.turn.extraTurn = false;
            return true;
        }, &error));
        return store->checkpointUpdate({}, GameTimeline::AnchorKind::PlayerTurn,
            turn, player, &error, beginsRound);
    };

    const auto turn1 = addTurn(QStringLiteral("turn-1"), QStringLiteral("p1"), QStringLiteral("round-1"), true);
    CHECK(!turn1.id.isEmpty());
    const auto round1 = findAnchor(GameTimeline::AnchorKind::FullRound, QStringLiteral("round-1"));
    const auto turn2 = addTurn(QStringLiteral("turn-2"), QStringLiteral("p2"), QStringLiteral("round-1"), false);
    const auto turn3 = addTurn(QStringLiteral("turn-3"), QStringLiteral("p1"), QStringLiteral("round-2"), true);
    const auto round2 = findAnchor(GameTimeline::AnchorKind::FullRound, QStringLiteral("round-2"));
    const auto turn4 = addTurn(QStringLiteral("turn-4"), QStringLiteral("p2"), QStringLiteral("round-2"), false);
    const auto turn5 = addTurn(QStringLiteral("turn-5"), QStringLiteral("p1"), QStringLiteral("round-3"), true);
    const auto round3 = findAnchor(GameTimeline::AnchorKind::FullRound, QStringLiteral("round-3"));
    const auto turn6 = addTurn(QStringLiteral("turn-6"), QStringLiteral("p2"), QStringLiteral("round-3"), false);
    CHECK(!turn2.id.isEmpty() && !turn4.id.isEmpty() && !turn5.id.isEmpty() && !turn6.id.isEmpty());
    CHECK(!round1.id.isEmpty() && !round2.id.isEmpty() && !round3.id.isEmpty());

    // The oldest round remains independently retained although its turn is
    // outside the two-turn window. Its paired turn is metadata only, not a
    // selectable player-turn restore target.
    CHECK(store->timeline().anchors().size() == 7);
    CHECK(store->checkpointState(round1.id));
    CHECK(store->checkpointState(turn1.id));
    CHECK(!store->prepareRestore(turn1.id, &error));
    CHECK(store->timeline().previousPlayerTurn().id == turn5.id);
    CHECK(store->timeline().previousFullRound().id == round2.id);
    CHECK(!store->checkpointState(turn2.id));
    CHECK(store->checkpointState(turn3.id)); // Retained only as round-2's shared-state alias.
    CHECK(!store->prepareRestore(turn3.id, &error)); // Outside the independent turn window.
    CHECK(!store->checkpointState(turn4.id));

    auto candidate = store->prepareRestore(round1.id, &error);
    CHECK(candidate);
    CHECK(store->publish(std::move(*candidate), &error));
    CHECK(store->timeline().generation() == 1);
    CHECK(store->timeline().anchor(turn1.id).id == turn1.id); // Paired alias survives round restore.
    CHECK(!store->prepareRestore(turn1.id, &error));
    CHECK(!store->checkpointState(turn6.id)); // Successful restore releases abandoned future states.

    const auto branchTurn = addTurn(QStringLiteral("branch-turn-1"), QStringLiteral("p2"),
        QStringLiteral("round-1"), false);
    CHECK(!branchTurn.id.isEmpty());
    CHECK(branchTurn.id.toULongLong() > turn6.id.toULongLong()); // Restore never reuses checkpoint IDs.
    CHECK(store->timeline().previousPlayerTurn().id == turn1.id);

    // Repeated restores preserve the checkpoint while generation increases;
    // a later branch remains bounded and uses fresh IDs.
    candidate = store->prepareRestore(round1.id, &error);
    CHECK(candidate && store->publish(std::move(*candidate), &error));
    CHECK(store->timeline().generation() == 2);
    const auto afterRepeatedRestore = addTurn(QStringLiteral("branch-turn-2"), QStringLiteral("p2"),
        QStringLiteral("round-1"), false);
    CHECK(!afterRepeatedRestore.id.isEmpty());
    CHECK(afterRepeatedRestore.id.toULongLong() > branchTurn.id.toULongLong());

    WorldStore::CheckpointRetention invalid = retention;
    invalid.playerTurns = 0;
    CHECK(!WorldStore::create(initial(), registry, invalid, &error));
}

void sharedTimelineTests()
{
    auto faults = std::make_shared<Faults>();
    const auto registry = providers(faults);
    const auto world = initial();
    auto authoritative = std::make_shared<GameTimeline>(world.rootGameId, world.worldId);
    auto store = WorldStore::create(world, registry, &error, authoritative);
    CHECK(store);
    CHECK(&store->timeline() == authoritative.get());
    CHECK(authoritative->generation() == 0 && authoritative->revision() == 0);

    const auto round = store->checkpoint(GameTimeline::AnchorKind::FullRound, "round-1", {}, &error);
    CHECK(!round.id.isEmpty());
    CHECK(&store->timeline() == authoritative.get());
    auto candidate = store->prepareRestore(round.id, &error);
    CHECK(candidate && store->publish(std::move(*candidate), &error));
    CHECK(&store->timeline() == authoritative.get()); // Publish swaps contents, never the shared object.
    CHECK(authoritative->generation() == 1 && store->timeline().generation() == 1);

    auto standalone = WorldStore::create(world, registry, &error);
    CHECK(standalone);
    CHECK(&standalone->timeline() != authoritative.get()); // Legacy core creation remains independent.
    CHECK(standalone->timeline().generation() == 0);

    // Rejected attachments must be checked before provider hooks and must not
    // alter identity, journal, anchors, counters, or pending-request state.
    const auto expectRejectedUnchanged = [&](const std::shared_ptr<GameTimeline> &timeline) {
        const auto root = timeline->rootGameId();
        const auto worldId = timeline->worldId();
        const auto branch = timeline->branchId();
        const auto generation = timeline->generation();
        const auto revision = timeline->revision();
        const auto records = timeline->records().size();
        const auto anchors = timeline->anchors().size();
        const int prepareCalls = faults->calls;
        CHECK(!WorldStore::create(world, registry, &error, timeline));
        CHECK(faults->calls == prepareCalls);
        CHECK(timeline->rootGameId() == root && timeline->worldId() == worldId);
        CHECK(timeline->branchId() == branch && timeline->generation() == generation);
        CHECK(timeline->revision() == revision);
        CHECK(timeline->records().size() == records && timeline->anchors().size() == anchors);
    };

    expectRejectedUnchanged(std::make_shared<GameTimeline>("other-match", world.worldId));
    expectRejectedUnchanged(std::make_shared<GameTimeline>(world.rootGameId, "other-world"));

    auto dirtyAnchor = std::make_shared<GameTimeline>(world.rootGameId, world.worldId);
    const auto dirty = dirtyAnchor->beginAnchor(GameTimeline::AnchorKind::FullRound, "round-1", {}, &error);
    CHECK(!dirty.id.isEmpty() && !dirtyAnchor->records().isEmpty() && !dirtyAnchor->anchors().isEmpty());
    expectRejectedUnchanged(dirtyAnchor);

    auto pending = std::make_shared<GameTimeline>(world.rootGameId, world.worldId);
    const auto token = pending->issueRequest("p1", "choose-card");
    CHECK(pending->isPending(token));
    expectRejectedUnchanged(pending);
    CHECK(pending->isPending(token)); // Failed attachment did not consume a live request.
}

void journalTests()
{
    GameTimeline one("same-root", "same-world"), two("same-root", "same-world");
    CHECK(one.beginAnchor(static_cast<GameTimeline::AnchorKind>(-1), "bad", "p1", &error).id.isEmpty());
    CHECK(one.beginAnchor(GameTimeline::AnchorKind::FullRound, "bad", "p1", &error).id.isEmpty());
    auto token = one.issueRequest("p1", "choose");
    auto other = two.issueRequest("p1", "choose");
    CHECK(token.serial == other.serial && token.generation == other.generation);
    CHECK(!two.acceptDecision(token, {}, "human", &error));
    CHECK(one.isPending(token) && two.isPending(other));
    CHECK(!one.acceptDecision(token, {{"pointer", QVariant::fromValue(static_cast<void *>(&one))}}, "human", &error));
    CHECK(one.isPending(token));
    CHECK(one.acceptDecision(token, {{"selected", "7"}}, "human", &error));
    CHECK(!one.acceptTimeout(token, {}, &error));
    auto timeout = one.issueRequest("p2", "choose");
    CHECK(one.acceptTimeout(timeout, {{"selected", "pass"}}, &error));
    CHECK(!one.acceptDecision(timeout, {}, "human", &error));
    CHECK(one.recordNondeterminism("clock.fixture", {{"milliseconds", "1234567"}}, &error));
    CHECK(one.records().size() == 3);
    CHECK(one.records()[0].kind == "accepted_decision" && one.records()[0].data["command"] == "choose");
    CHECK(one.records()[1].data["source"] == "timeout" && one.records()[2].kind == "nondeterminism");
    const auto anchor = one.beginAnchor(GameTimeline::AnchorKind::FullRound, "scope-A", {}, &error);
    const auto pending = one.issueRequest("p1", "choose");
    auto next = one.prepareRestore(anchor.id, &error);
    CHECK(next && one.isPending(pending) && one.generation() == 0);
    CHECK(next->generation() == 1 && !next->isPending(pending));
    CHECK(next->records().size() == 5 && next->records().last().kind == "restore");
    CHECK(one.generation() == 0 && one.isPending(pending)); // Only WorldStore can publish a timeline.
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    validationTests(); restoreTests(); pairedAnchorTests(); checkpointRetentionTests(); sharedTimelineTests(); journalTests();
    qInfo() << "game-state-contract:" << checks << "checks passed";
    return 0;
}
