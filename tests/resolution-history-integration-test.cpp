#include "engine-bootstrap.h"
#include "engine.h"
#include "game-snapshot.h"
#include "ai-runtime.h"
#include "lua-runtime.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "standard.h"
#include "maneuvering.h"

#include "lua.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QVariantList>

#include <cstdio>

namespace {

bool isolatedLuaContractRuns()
{
    Room room(nullptr, QStringLiteral("02_1v1"));
    LuaRuntime &runtime = room.roomRuntime()->ai().lua();
    if (!runtime.rawState()) {
        QString initError;
        if (!room.roomRuntime()->ai().initialize(&initError)) {
            std::fprintf(stderr, "isolated AI initialization failed: %s\n",
                         qPrintable(initError));
            return false;
        }
    }
    if (!runtime.rawState()) return false;
    LuaRuntime::Binding binding(runtime);
    const int top = lua_gettop(runtime.state());
    QString error;
    const bool loaded = runtime.loadScript(
        QStringLiteral("tests/lua/resolution-history-contract.lua"), &error);
    lua_settop(runtime.state(), top);
    if (!loaded) {
        std::fprintf(stderr, "isolated Lua contract failed: %s\n", qPrintable(error));
        return false;
    }
    return true;
}

bool rulesLuaRoomPointerContract(Room &room)
{
    EngineRuntimeContextScope contextScope(*Sanguosha, &room);
    const qint64 event = room.resolutionHistory().beginEvent(QStringLiteral("damage"),
        {{QStringLiteral("from"), QStringLiteral("alice")},
         {QStringLiteral("to"), QStringLiteral("bob")},
         {QStringLiteral("skill_name"), QStringLiteral("rules_probe")},
         {QStringLiteral("skill_owner"), QStringLiteral("alice")}});
    room.resolutionHistory().appendFact(event, QStringLiteral("move"),
        {{QStringLiteral("from"), QStringLiteral("alice")},
         {QStringLiteral("to"), QStringLiteral("bob")},
         {QStringLiteral("player"), QStringLiteral("alice")},
         {QStringLiteral("card_id"), 17}});
    room.resolutionHistory().appendFact(event, QStringLiteral("damage_component"),
        {{QStringLiteral("component"), QStringLiteral("hp")},
         {QStringLiteral("amount"), 1}});
    room.resolutionHistory().appendFact(event, QStringLiteral("actual_damage"),
        {{QStringLiteral("from"), QStringLiteral("alice")},
         {QStringLiteral("to"), QStringLiteral("bob")},
         {QStringLiteral("skill_name"), QStringLiteral("rules_probe")},
         {QStringLiteral("skill_owner"), QStringLiteral("alice")},
         {QStringLiteral("amount"), 1}});

    QFile scriptFile(QStringLiteral("tests/lua/resolution-history-contract.lua"));
    if (!scriptFile.open(QIODevice::ReadOnly)) return false;
    LuaRuntime::Binding binding(room.roomRuntime()->lua());
    room.initializeLuaTestEnvironment();
    lua_State *state = room.getLuaState();
    const QByteArray script = scriptFile.readAll();
    if (luaL_dostring(state, script.constData()) != LUA_OK) {
        std::fprintf(stderr, "rules Lua history contract failed: %s\n",
                     lua_tostring(state, -1));
        lua_pop(state, 1);
        return false;
    }
    lua_getglobal(state, "resolution_history_probe_ok");
    const bool passed = lua_toboolean(state, -1) != 0;
    lua_pop(state, 1);
    return passed;
}

bool cardHistoryProjectsNativeFacts(Room &room)
{
    ServerPlayer actor(&room), responder(&room);
    actor.setObjectName("history_actor");
    responder.setObjectName("history_responder");
    Slash slash(Card::Spade, 7);
    Jink jink(Card::Heart, 2);
    DummyCard skillCard;
    const QVariantMap card = room.historyCardSnapshot(&slash);
    auto &history = room.resolutionHistory();
    const qint64 turn = history.beginEvent("turn");
    const qint64 phase = history.beginEvent("phase", {{"phase", int(Player::Play)}});
    const auto use = [&](const QVariantMap &snapshot) {
        const QVariantMap data{{"from", actor.objectName()}, {"card", snapshot}};
        const qint64 id = history.beginEvent("use_card", data);
        history.appendFact(id, "use_card", data);
        return id;
    };
    // More than a query page; the same physical identity is reused each time.
    for (int i = 0; i < 101; ++i) history.finishEvent(use(card));
    history.finishEvent(use(room.historyCardSnapshot(&skillCard)));
    const auto respond = [&](bool isUse) {
        const QVariantMap data{{"from", responder.objectName()}, {"player", actor.objectName()},
            {"card", room.historyCardSnapshot(&jink)}, {"is_use", isUse}};
        const qint64 id = history.beginEvent("respond_card", data);
        history.appendFact(id, "respond_card", data);
        history.finishEvent(id);
    };
    respond(true);
    respond(false);
    if (room.countHistoryCards(&actor) != 102
        || room.countHistoryCards(&actor, "turn", "Slash") != 101
        || room.countHistoryCards(&actor, "turn", QString(), true) != 1
        || room.countHistoryCards(&responder) != 0) return false;

    history.finishEvent(phase);
    const qint64 nextPlay = history.beginEvent("phase", {{"phase", int(Player::Play)}});
    const qint64 outerUse = use(card);
    const auto damage = [&](const QVariantMap &damageCard, int amount) {
        const qint64 id = history.beginEvent("damage");
        history.appendFact(id, "damage_component", {{"component", "hp"}, {"amount", amount}});
        history.appendFact(id, "actual_damage", {{"from", actor.objectName()},
            {"to", responder.objectName()}, {"card", damageCard}});
        history.finishEvent(id);
    };
    damage(card, 2);
    damage({}, 1); // Reactive skill damage is not damage dealt by this card.
    const qint64 nestedUse = use(card);
    // Fan-style conversion changes the resolved event, not its accepted fact.
    FireSlash fire(Card::Spade, 7);
    CardUseStruct converted(&slash, &actor);
    converted.changeCard(&fire);
    damage(room.historyCardSnapshot(&fire), 3);
    history.finishEvent(nestedUse);
    const QVariantList damages = room.queryCardUseDamage().value("items").toList();
    if (damages.size() != 1 || damages.first().toMap().value("data").toMap().value("amount").toInt() != 2
        || room.queryCardUseDamage(nestedUse).value("items").toList().size() != 1
        || room.countHistoryCards(&actor, "turn", "FireSlash") != 0
        || room.countHistoryCards(&actor, "phase") != 2
        || room.countHistoryCards(&actor, "turn", QString(), false, true) != 104) return false;
    history.finishEvent(outerUse);
    // Extra turns get their own scope without leaking the suspended outer turn.
    const qint64 extraTurn = history.beginEvent("turn");
    if (room.countHistoryCards(&actor) != 0) return false;
    history.finishEvent(extraTurn);
    history.finishEvent(nextPlay);
    history.finishEvent(turn);
    if (room.countHistoryCards(&actor) != 0
        || room.countHistoryCards(&actor, "game") != 104) return false;
    // Older snapshots missing classification must remain unknown, not zero.
    const qint64 oldTurn = history.beginEvent("turn");
    QVariantMap oldCard = card;
    oldCard.remove("classes");
    history.finishEvent(use(oldCard));
    const bool unknown = room.countHistoryCards(&actor) == -1;
    history.finishEvent(oldTurn);
    return unknown;
}

bool roomWrappersExposeReadOnlyJournal()
{
    Room room(nullptr, QStringLiteral("02_1v1"));
    const qint64 event = room.resolutionHistory().beginEvent(QStringLiteral("turn"));
    room.resolutionHistory().appendFact(event, QStringLiteral("move"),
        {{QStringLiteral("from"), QStringLiteral("alice")},
         {QStringLiteral("to"), QStringLiteral("bob")},
         {QStringLiteral("player"), QStringLiteral("alice")}});
    const QVariantMap moves = room.queryHistoryMoves({{QStringLiteral("from"), QStringLiteral("alice")},
        {QStringLiteral("to"), QStringLiteral("bob")}, {QStringLiteral("limit"), 10}});
    const QVariantList facts = moves.value(QStringLiteral("facts")).toList();
    if (facts.size() != 1 || room.currentHistoryEventId() != event) return false;

    // Room's exposed query returns copied QVariant trees; mutating the caller
    // result cannot alter the authoritative rules journal.
    QVariantMap changed = facts.first().toMap();
    QVariantMap data = changed.value(QStringLiteral("data")).toMap();
    data.insert(QStringLiteral("from"), QStringLiteral("tampered"));
    changed.insert(QStringLiteral("data"), data);
    const QVariantMap reread = room.queryHistoryMoves({{QStringLiteral("from"), QStringLiteral("alice")},
        {QStringLiteral("limit"), 10}});
    if (reread.value(QStringLiteral("facts")).toList().size() != 1) return false;
    room.resolutionHistory().finishEvent(event);
    return cardHistoryProjectsNativeFacts(room);
}

bool snapshotNextIdAndUnknownAttribution()
{
    ResolutionHistoryService history;
    const qint64 event = history.beginEvent(QStringLiteral("damage"),
        {{QStringLiteral("skill_name"), QStringLiteral("legacy_skill")},
         {QStringLiteral("skill_owner"), QString()},
         {QStringLiteral("attribution_complete"), false}});
    const QVariantMap data = history.event(event).value(QStringLiteral("data")).toMap();
    if (!data.value(QStringLiteral("skill_owner")).toString().isEmpty()
        || data.value(QStringLiteral("attribution_complete")).toBool()) return false;
    history.finishEvent(event, QStringLiteral("completed"));
    const ResolutionHistorySnapshot snapshot = history.snapshot();
    ResolutionHistoryService restored;
    QString error;
    if (!restored.restore(snapshot, &error)) return false;
    const qint64 next = restored.appendFact(event, QStringLiteral("actual_damage"),
        {{QStringLiteral("amount"), 0}});
    return next > event;
}

bool snapshotWithoutHistoryIsIneligible()
{
    // A legacy/current-state-only snapshot must not become a takeover source by
    // silently treating the missing authoritative journal as an empty history.
    const GlobalSnapshot decoded = GlobalSnapshot::deserialize({
        {QStringLiteral("eligible"), true},
        {QStringLiteral("ineligibleReason"), QString()}});
    return !decoded.eligible && !decoded.ineligibleReason.isEmpty();
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        std::fprintf(stderr, "engine bootstrap failed: %s\n", qPrintable(error));
        return 1;
    }
    // Separate Room initialization costs so each focused case has a bounded budget.
    const QStringList args = application.arguments();
    const int selector = args.indexOf(QStringLiteral("--case"));
    const QString selected = selector >= 0 ? args.value(selector + 1) : QString();
    const QStringList cases{QStringLiteral("rules-lua"), QStringLiteral("isolated-lua"),
        QStringLiteral("room-wrappers"), QStringLiteral("snapshot"),
        QStringLiteral("snapshot-without-history")};
    if (selector >= 0 && !cases.contains(selected)) return 64;
    for (const QString &name : cases) {
        if (!selected.isEmpty() && selected != name) continue;
        std::fprintf(stderr, "RUN %s\n", qPrintable(name));
        std::fflush(stderr);
        bool passed = false;
        if (name == QStringLiteral("rules-lua")) {
            Room rulesRoom(nullptr, QStringLiteral("02_1v1"));
            passed = rulesLuaRoomPointerContract(rulesRoom);
        } else if (name == QStringLiteral("isolated-lua")) passed = isolatedLuaContractRuns();
        else if (name == QStringLiteral("room-wrappers")) passed = roomWrappersExposeReadOnlyJournal();
        else if (name == QStringLiteral("snapshot")) passed = snapshotNextIdAndUnknownAttribution();
        else passed = snapshotWithoutHistoryIsIneligible();
        std::fprintf(stderr, "%s %s\n", passed ? "PASS" : "FAIL", qPrintable(name));
        std::fflush(stderr);
        if (!passed) return 2;
    }
    return 0;
}
