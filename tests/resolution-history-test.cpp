#include "engine-bootstrap.h"
#include "engine.h"
#include "gamerule.h"
#include "room-test-access.h"
#include "room.h"
#include "roomthread.h"
#include "serverplayer.h"
#include "settings.h"
#include "skill.h"
#include "structs.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <cstdio>
#include <memory>

namespace {
void checkpoint(const char *name)
{
    std::fprintf(stderr, "history checkpoint: %s\n", name);
    std::fflush(stderr);
}

struct RoomFixture
{
    explicit RoomFixture(const QString &mode = QStringLiteral("02_1v1"))
        : room(nullptr, mode)
    {
        scope = std::make_unique<EngineRuntimeContextScope>(*Sanguosha, &room);
        checkpoint("room state reset begin");
        room.roomRuntime()->state().reset();
        checkpoint("room state reset end");
    }

    // Declare the scope first so the room is destroyed while it is still bound.
    std::unique_ptr<EngineRuntimeContextScope> scope;
    Room room;
};

#define CHECK_HISTORY(expr) do { if (!(expr)) { \
    std::fprintf(stderr, "history check failed at line %d: %s\n", __LINE__, #expr); return false; \
} } while (false)

class SyntheticLoss final : public TriggerSkillV2 {
public:
    SyntheticLoss() : TriggerSkillV2(QStringLiteral("test-history-loss"))
    // Trigger-order selection itself emits ChoiceMade; do not recursively
    // activate the fixture while the engine is still selecting this skill.
    { events << EventPhaseStart; frequency = Compulsory; }
    mutable bool done = false;
    ServerPlayer *owner = nullptr;
    ServerPlayer *victim = nullptr;
    QList<int> cards;
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override
    { TriggerList list; if (!done && owner) list[owner] << objectName(); return list; }
    bool trigger(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return false; }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &) const override
    {
        done = true;
        room->moveCardsAtomic(CardsMoveStruct(cards, victim, nullptr, Player::PlaceHand,
            Player::DiscardPile, CardMoveReason(CardMoveReason::S_REASON_DISMANTLE,
                owner->objectName(), objectName(), QString())), true);
        return false;
    }
};

class SyntheticRecovery final : public TriggerSkillV2 {
public:
    SyntheticRecovery() : TriggerSkillV2(QStringLiteral("test-history-recovery"))
    { events << EventPhaseStart; frequency = Compulsory; }
    mutable bool done = false;
    mutable bool complete = false;
    mutable QSet<int> recovered;
    ServerPlayer *owner = nullptr;
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override
    { TriggerList list; if (!done && owner) list[owner] << objectName(); return list; }
    bool trigger(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return false; }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &) const override
    {
        done = true;
        QVariantMap filter{{QStringLiteral("turn_id"), room->historyScopes().value(QStringLiteral("turn_id"))},
            {QStringLiteral("from"), owner->objectName()}, {QStringLiteral("limit"), 1}};
        QSet<int> eligible;
        do {
            const QVariantMap page = room->queryHistoryMoves(filter);
            complete = page.value(QStringLiteral("complete")).toBool()
                && page.value(QStringLiteral("attribution_complete")).toBool();
            if (!complete) return false;
            filter.insert(QStringLiteral("watermark"), page.value(QStringLiteral("watermark")));
            for (const QVariant &item : page.value(QStringLiteral("items")).toList()) {
                const QVariantMap data = item.toMap().value(QStringLiteral("data")).toMap();
                const QString source = data.value(QStringLiteral("skill_owner")).toString();
                if (!source.isEmpty() && source != owner->objectName())
                    eligible.insert(data.value(QStringLiteral("card_id")).toInt());
            }
            if (!page.value(QStringLiteral("has_more")).toBool()) break;
            filter.insert(QStringLiteral("after"), page.value(QStringLiteral("next_after")));
        } while (true);
        // Facts describe the past. Ownership/position must be checked again at activation.
        for (int card : eligible) {
            if (room->getCardPlace(card) != Player::DiscardPile) continue;
            room->obtainCard(owner, card, objectName());
            recovered.insert(card);
        }
        return false;
    }
};

ServerPlayer *addPlayer(Room &room, const QString &name)
{
    ServerPlayer *player = RoomTestAccess::addOrdinaryPlayer(room, name, true);
    player->setState(QStringLiteral("robot"));
    player->setGeneralName(QStringLiteral("caocao"));
    player->setMaxHp(6); player->setHp(6);
    return player;
}

bool lateAcquisitionUsesRealMoves()
{
    // Engine definition registries outlive each Room in this executable.
    static SyntheticLoss loss;
    static SyntheticRecovery recovery;
    Sanguosha->addSkills(QList<const Skill *>() << &loss << &recovery);
    RoomFixture fixture;
    Room &room = fixture.room;
    LuaRuntime::Binding luaBinding(room.roomRuntime()->lua());
    GameRng::Binding rngBinding(room.roomRuntime()->rng());
    RoomTestAccess::attachThread(room);
    ServerPlayer *owner = addPlayer(room, QStringLiteral("history-owner"));
    ServerPlayer *other = addPlayer(room, QStringLiteral("history-other"));
    RoomTestAccess::resetAlive(room); room.setCurrent(owner);
    checkpoint("late acquisition players ready");
    loss.owner = other; loss.victim = owner; recovery.owner = owner;
    loss.cards = room.getNCards(2);
    CHECK_HISTORY(loss.cards.size() == 2);
    checkpoint("initial movement begin");
    room.moveCardsAtomic(CardsMoveStruct(loss.cards, owner, Player::PlaceHand,
        CardMoveReason(CardMoveReason::S_REASON_DRAW, owner->objectName())), true);
    checkpoint("initial movement end");
    ResolutionHistoryEventGuard turn(room.resolutionHistory(), QStringLiteral("turn"),
        {{QStringLiteral("player"), owner->objectName()}});
    CHECK_HISTORY(room.acquireSkill(other, &loss, false, false, false) > 0);
    checkpoint("loss skill acquired");
    QVariant data = QStringLiteral("history-fixture");
    room.getThread()->trigger(EventPhaseStart, &room, other, data);
    checkpoint("loss skill triggered");
    CHECK_HISTORY(loss.done && room.getCardPlace(loss.cards[0]) == Player::DiscardPile);
    room.obtainCard(other, loss.cards[1]);
    // The recovery skill did not exist while either card was lost.
    CHECK_HISTORY(!owner->hasSkill(recovery.objectName()));
    CHECK_HISTORY(room.acquireSkill(owner, &recovery, false, false, false) > 0);
    checkpoint("recovery skill acquired");
    room.getThread()->trigger(EventPhaseStart, &room, owner, data);
    checkpoint("recovery skill triggered");
    CHECK_HISTORY(recovery.done && recovery.complete);
    CHECK_HISTORY(recovery.recovered == QSet<int>{loss.cards[0]});
    CHECK_HISTORY(room.getCardOwner(loss.cards[0]) == owner);
    CHECK_HISTORY(room.getCardOwner(loss.cards[1]) == other);
    return true;
}

class DamageProbe final : public TriggerSkill {
public:
    DamageProbe() : TriggerSkill(QStringLiteral("test-history-damage"))
    { events << DamageInflicted << HpChanged << Damaged << LostHujia; }
    bool prevent = false, abortHp = false, abortArmor = false;
    mutable bool hpVisible = false, damagedVisible = false, armorVisible = false;
    bool triggerable(ServerPlayer *, Room *, TriggerEvent, ServerPlayer *, QVariant) const override
    { return true; }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *target, QVariant &) const override
    {
        if (event == DamageInflicted) return prevent;
        const QVariantMap damage = room->historyParent(room->currentHistoryEventId(), QStringLiteral("damage"));
        if (damage.isEmpty()) return false;
        const QVariantList facts = room->queryActualDamage({{QStringLiteral("event_id"), damage.value(QStringLiteral("id"))},
            {QStringLiteral("to"), target->objectName()}}).value(QStringLiteral("items")).toList();
        const bool visible = facts.size() == 1 && damage.value(QStringLiteral("status")) == QStringLiteral("active");
        if (event == HpChanged) { hpVisible = visible; if (abortHp) throw TurnBroken; }
        if (event == Damaged) damagedVisible = visible;
        if (event == LostHujia) {
            armorVisible = visible && facts.first().toMap().value(QStringLiteral("data")).toMap()
                .value(QStringLiteral("hp_loss")).toInt() == 0;
            if (abortArmor) throw TurnBroken;
        }
        return false;
    }
};

bool realDamageCommitsBeforeCallbacks()
{
    DamageProbe probe;
    RoomFixture fixture;
    Room &room = fixture.room;
    LuaRuntime::Binding luaBinding(room.roomRuntime()->lua());
    GameRng::Binding rngBinding(room.roomRuntime()->rng());
    RoomTestAccess::attachThread(room);
    ServerPlayer *owner = addPlayer(room, QStringLiteral("damage-owner"));
    ServerPlayer *victim = addPlayer(room, QStringLiteral("damage-victim"));
    RoomTestAccess::resetAlive(room); room.setCurrent(owner);
    room.getThread()->addTriggerSkill(new GameRule(&room));
    room.getThread()->addTriggerSkill(&probe);
    probe.prevent = true;
    room.damage(DamageStruct(QStringLiteral("history-test"), owner, victim, 1));
    CHECK_HISTORY(victim->getHp() == 6);
    CHECK_HISTORY(room.queryActualDamage({}).value(QStringLiteral("items")).toList().isEmpty());
    probe.prevent = false;
    room.damage(DamageStruct(QStringLiteral("history-test"), owner, victim, 1));
    CHECK_HISTORY(victim->getHp() == 5 && probe.hpVisible && probe.damagedVisible);
    probe.abortHp = true; probe.hpVisible = false;
    bool interrupted = false;
    try { room.damage(DamageStruct(QStringLiteral("history-test"), owner, victim, 1)); }
    catch (TriggerEvent event) { interrupted = event == TurnBroken; }
    CHECK_HISTORY(interrupted && victim->getHp() == 4 && probe.hpVisible);
    CHECK_HISTORY(room.queryActualDamage({}).value(QStringLiteral("items")).toList().size() == 2);
    probe.abortHp = false; probe.abortArmor = true;
    room.setPlayerMark(victim, "@HuJia", 1);
    CHECK_HISTORY(victim->getHujia() == 1);
    interrupted = false;
    try { room.damage(DamageStruct(QStringLiteral("history-test"), owner, victim, 2)); }
    catch (TriggerEvent event) { interrupted = event == TurnBroken; }
    CHECK_HISTORY(interrupted && victim->getHp() == 4 && victim->getHujia() == 0 && probe.armorVisible);
    CHECK_HISTORY(room.queryActualDamage({}).value(QStringLiteral("items")).toList().size() == 3);
    const QVariantList events = room.queryHistoryEvents({{QStringLiteral("kind"), QStringLiteral("damage")}})
        .value(QStringLiteral("items")).toList();
    CHECK_HISTORY(events.last().toMap().value(QStringLiteral("outcome")) == QStringLiteral("aborted"));
    return true;
}

bool pileMovesRecordEachCardOnce()
{
    RoomFixture fixture;
    Room &room = fixture.room;
    LuaRuntime::Binding luaBinding(room.roomRuntime()->lua());
    GameRng::Binding rngBinding(room.roomRuntime()->rng());
    RoomTestAccess::attachThread(room);
    ServerPlayer *owner = addPlayer(room, QStringLiteral("pile-owner"));
    RoomTestAccess::resetAlive(room); room.setCurrent(owner);
    const QList<int> cards = room.getNCards(2);
    CHECK_HISTORY(cards.size() == 2);
    const auto draw = [&]() {
        room.moveCardsAtomic(CardsMoveStruct(cards, owner, Player::PlaceHand,
            CardMoveReason(CardMoveReason::S_REASON_DRAW, owner->objectName())), true);
    };
    draw();
    QVariant cursor = room.queryHistoryMoves({}).value(QStringLiteral("watermark"));
    room.moveCardsInToDrawpile(owner, cards, QStringLiteral("pile-fixture"), 1, true);
    CHECK_HISTORY(room.queryHistoryMoves({{QStringLiteral("after"), cursor}})
        .value(QStringLiteral("items")).toList().size() == 2);
    draw();
    cursor = room.queryHistoryMoves({}).value(QStringLiteral("watermark"));
    room.shuffleIntoDrawPile(owner, cards, QStringLiteral("pile-fixture"), true);
    CHECK_HISTORY(room.queryHistoryMoves({{QStringLiteral("after"), cursor}})
        .value(QStringLiteral("items")).toList().size() == 2);
    return true;
}

class NestedTurnProbe final : public TriggerSkill {
public:
    NestedTurnProbe() : TriggerSkill(QStringLiteral("test-history-nested-turn")) { events << TurnStart; }
    mutable QString outer, inner, restored;
    mutable bool entered = false;
    bool triggerable(ServerPlayer *, Room *, TriggerEvent, ServerPlayer *, QVariant) const override { return true; }
    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const override
    {
        if (entered) { inner = room->historyScopes().value(QStringLiteral("turn_id")).toString(); return false; }
        entered = true;
        outer = room->historyScopes().value(QStringLiteral("turn_id")).toString();
        QVariant nested;
        room->getThread()->trigger(TurnStart, room, nullptr, nested);
        restored = room->historyScopes().value(QStringLiteral("turn_id")).toString();
        return false;
    }
};

bool nestedTurnDispatchSeparatesScopes()
{
    NestedTurnProbe probe;
    RoomFixture fixture;
    Room &room = fixture.room;
    LuaRuntime::Binding luaBinding(room.roomRuntime()->lua());
    GameRng::Binding rngBinding(room.roomRuntime()->rng());
    RoomTestAccess::attachThread(room, &probe);
    QVariant data;
    room.getThread()->trigger(TurnStart, &room, nullptr, data);
    CHECK_HISTORY(!probe.outer.isEmpty() && probe.outer != "0" && probe.inner != probe.outer);
    CHECK_HISTORY(probe.inner != "0" && probe.restored == probe.outer);
    const QVariantList own = room.queryHistoryEvents({{QStringLiteral("kind"), QStringLiteral("turn")},
        {QStringLiteral("turn_id"), probe.outer}}).value(QStringLiteral("items")).toList();
    CHECK_HISTORY(own.size() == 1);
    return true;
}

bool paginationAndSnapshotContracts()
{
    ResolutionHistoryService history;
    const qint64 turn = history.beginEvent(QStringLiteral("turn"));
    const qint64 move = history.beginEvent(QStringLiteral("move_cards"));
    for (int i = 0; i < 1005; ++i)
        history.appendFact(move, QStringLiteral("move"), {{QStringLiteral("card_id"), i}});
    const QVariantMap first = history.queryFacts({{QStringLiteral("kind"), QStringLiteral("move")}, {QStringLiteral("limit"), 1000}});
    CHECK_HISTORY(first.value(QStringLiteral("items")).toList().size() == 1000 && first.value(QStringLiteral("has_more")).toBool());
    // A new fact between pages must not escape the caller's fixed watermark.
    history.appendFact(move, QStringLiteral("move"), {{QStringLiteral("card_id"), 2000}});
    const QVariantMap second = history.queryFacts({{QStringLiteral("kind"), QStringLiteral("move")},
        {QStringLiteral("after"), first.value(QStringLiteral("next_after"))},
        {QStringLiteral("watermark"), first.value(QStringLiteral("watermark"))}, {QStringLiteral("limit"), 1000}});
    CHECK_HISTORY(second.value(QStringLiteral("items")).toList().size() == 5);
    CHECK_HISTORY(history.queryFacts({{QStringLiteral("turn_id"), -1}}).contains(QStringLiteral("error")));
    history.finishEvent(move); history.finishEvent(turn);
    const ResolutionHistorySnapshot checkpoint = history.snapshot();
    const QVariantMap saved = checkpoint.serialize();
    const qint64 later = history.beginEvent(QStringLiteral("damage"));
    CHECK_HISTORY(checkpoint.serialize() == saved);
    ResolutionHistorySnapshot decoded;
    QString error;
    CHECK_HISTORY(ResolutionHistorySnapshot::deserialize(saved, &decoded, &error));
    const QVariantMap jsonRoundTrip = QJsonDocument::fromJson(
        QJsonDocument(QJsonObject::fromVariantMap(saved)).toJson()).object().toVariantMap();
    CHECK_HISTORY(ResolutionHistorySnapshot::deserialize(jsonRoundTrip, &decoded, &error));
    ResolutionHistoryService restored;
    CHECK_HISTORY(restored.restore(decoded, &error));
    CHECK_HISTORY(restored.beginEvent(QStringLiteral("damage")) == later);
    QVariantMap tampered = saved;
    tampered.insert(QStringLiteral("next_id"), QStringLiteral("1"));
    CHECK_HISTORY(!ResolutionHistorySnapshot::deserialize(tampered, &decoded));
    return true;
}

bool cleanupAndAttributionContracts()
{
    ResolutionHistoryService history;
    const qint64 turn = history.beginEvent(QStringLiteral("turn"));
    const qint64 round = history.beginRound();
    CHECK_HISTORY(history.event(turn).value(QStringLiteral("round_id")).toLongLong() == round);
    const qint64 phase = history.beginEvent(QStringLiteral("phase"));
    history.finishEvent(phase, QStringLiteral("aborted"));
    history.finishEvent(turn, QStringLiteral("aborted"));
    {
        ResolutionHistoryContextGuard cleanup(history, phase);
        ResolutionHistoryEventGuard move(history, QStringLiteral("move_cards"));
        history.appendFact(move.id(), QStringLiteral("move"),
            {{QStringLiteral("from"), QStringLiteral("alice")},
             {QStringLiteral("skill_owner"), QString()},
             {QStringLiteral("attribution_complete"), false}});
        CHECK_HISTORY(history.currentScopes().value(QStringLiteral("turn_id")).toLongLong() == turn);
        CHECK_HISTORY(!history.snapshot().isComplete());
    }
    const QVariantMap unknown = history.queryFacts({{QStringLiteral("kind"), QStringLiteral("move")},
        {QStringLiteral("skill_owner"), QStringLiteral("bob")}});
    CHECK_HISTORY(unknown.value(QStringLiteral("items")).toList().isEmpty());
    CHECK_HISTORY(!unknown.value(QStringLiteral("attribution_complete")).toBool());
    CHECK_HISTORY(history.event(phase).value(QStringLiteral("outcome")) == QStringLiteral("aborted"));
    ResolutionHistorySnapshot restored;
    CHECK_HISTORY(ResolutionHistorySnapshot::deserialize(history.snapshot().serialize(), &restored));
    CHECK_HISTORY(restored.remapPlayerIds({{QStringLiteral("alice"), QStringLiteral("seat1")}}));
    ResolutionHistoryService remapped;
    CHECK_HISTORY(remapped.restore(restored));
    CHECK_HISTORY(remapped.queryFacts({{QStringLiteral("from"), QStringLiteral("seat1")}})
        .value(QStringLiteral("items")).toList().size() == 1);
    // A missing RoundEnd closes only the old history lifecycle at the next boundary.
    history.beginRound();
    CHECK_HISTORY(history.event(round).value(QStringLiteral("outcome")) == QStringLiteral("interrupted"));
    CHECK_HISTORY(history.snapshot().isComplete());
    return true;
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        std::fprintf(stderr, "engine bootstrap failed: %s\n", qPrintable(error)); return 1;
    }
    const int oldDelay = Config.AIDelay;
    Config.AIDelay = 0;
    QString selectedCase;
    const QStringList args = application.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args.at(i) == QStringLiteral("--case") && i + 1 < args.size())
            selectedCase = args.at(++i);
        else if (args.at(i).startsWith(QStringLiteral("--case=")))
            selectedCase = args.at(i).mid(QStringLiteral("--case=").size());
    }
    const QSet<QString> caseNames{QStringLiteral("late-acquisition"), QStringLiteral("damage"),
        QStringLiteral("pile-moves"), QStringLiteral("nested-turn"), QStringLiteral("pagination"),
        QStringLiteral("cleanup")};
    if (!selectedCase.isEmpty() && !caseNames.contains(selectedCase)) {
        std::fprintf(stderr, "unknown --case: %s\n", qPrintable(selectedCase));
        return 64;
    }
    const auto runCase = [&](const QString &name, const auto &test) {
        if (!selectedCase.isEmpty() && selectedCase != name) return true;
        std::fprintf(stdout, "RUN %s\n", qPrintable(name));
        std::fflush(stdout);
        const bool result = test();
        std::fprintf(stdout, "%s %s\n", result ? "PASS" : "FAIL", qPrintable(name));
        std::fflush(stdout);
        return result;
    };
    const bool passed = runCase(QStringLiteral("late-acquisition"), lateAcquisitionUsesRealMoves)
        && runCase(QStringLiteral("damage"), realDamageCommitsBeforeCallbacks)
        && runCase(QStringLiteral("pile-moves"), pileMovesRecordEachCardOnce)
        && runCase(QStringLiteral("nested-turn"), nestedTurnDispatchSeparatesScopes)
        && runCase(QStringLiteral("pagination"), paginationAndSnapshotContracts)
        && runCase(QStringLiteral("cleanup"), cleanupAndAttributionContracts);
    Config.AIDelay = oldDelay;
    return passed ? 0 : 2;
}
