#include "ai-decision-coordinator.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "room-roster.h"
#include "runtime-paths.h"
#include "serverplayer.h"
#include "settings.h"
#include "skill-set-generation.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <cstdio>
#include <cstdlib>

static void check(bool ok, const char *what)
{
    if (!ok) { fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}

class CountingMaxCards : public MaxCardsSkill {
public:
    CountingMaxCards() : MaxCardsSkill("h50p_worldview_counter") {}
    int getExtra(const Player *player) const override
    {
        ++calls;
        if (mutate) {
            mutate = false;
            const_cast<Player *>(player)->setMark("h50p_mid_fill", 1);
        }
        return player->getMark("h50p_extra");
    }
    mutable int calls = 0;
    mutable bool mutate = false;
};

class CountingDistance : public DistanceSkill {
public:
    CountingDistance() : DistanceSkill("h50p_worldview_distance") {}
    int getCorrect(const Player *, const Player *) const override { ++calls; return 0; }
    mutable int calls = 0;
};

struct RoomTestAccess {
    static AiDecisionCoordinator &ai(Room &room) { return *room.m_aiDecisions; }
    static void add(Room &room, ServerPlayer *player) { room.addPlayerToRoster(player); }
    static void removeAlive(Room &room, ServerPlayer *player) { room.m_roster->removeAlive(player); }
    static void ready(Room &room) { room.m_roster->resetAliveToPlayers(); }
    static void invalidate(Room &room) { ai(room).m_publicBoardValid = false; }
    static AIWorldView uncached(Room &room, ServerPlayer *viewer)
    {
        auto &coordinator = ai(room);
        return coordinator.projectWorldView(coordinator.buildPublicBoard(), viewer, true, false);
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QString error;
    check(QSanRuntimePaths::resolve(app.arguments(), &error), qPrintable(error));
    Engine engine;
    Config.EnableAI = true;
    // Native 50-seat fixture: no room gameplay/AI VM or extension Lua callbacks. The counter
    // measures actual full board evaluations, not a model of the memo algorithm.
    auto *room = new Room(nullptr, "50p", GameSessionConfig(26092920),
                          Room::RuntimeInitializationPolicy::Deferred);
    auto *counter = new CountingMaxCards;
    auto *distance = new CountingDistance;
    room->roomRuntime()->addSkills({counter, distance});
    QList<ServerPlayer *> players;
    for (int i = 0; i < 50; ++i) {
        auto *player = new ServerPlayer(room);
        player->setObjectName(QString("h50p_%1").arg(i));
        player->setSeat(i + 1);
        player->setMaxHp(4);
        player->setHp(4);
        player->setRole("rebel");
        RoomTestAccess::add(*room, player);
        players << player;
    }
    RoomTestAccess::ready(*room);
    auto &ai = RoomTestAccess::ai(*room);
    auto request = [&](ServerPlayer *player) {
        return ai.makeRequest(player, AIRequest::TriggerOrder,
            CardUseStruct::CARD_USE_REASON_UNKNOWN, {}, {}, Card::MethodNone).worldView;
    };
    QElapsedTimer timer;
    timer.start();
    for (auto *player : players) RoomTestAccess::uncached(*room, player);
    const qint64 beforeNs = timer.nsecsElapsed();
    const int beforeCalls = counter->calls;
    const int beforeDistances = distance->calls;
    counter->calls = 0;
    distance->calls = 0;
    timer.restart();
    for (auto *player : players) {
        const auto world = request(player);
        check(world.self.objectName == player->objectName(), "projection belongs to viewer");
        check(world.players.size() == 49, "all other seats retained");
        check(world.distanceScope == "none" && world.distances.isEmpty(), "TriggerOrder omits geometry");
    }
    const qint64 afterNs = timer.nsecsElapsed();
    check(beforeCalls == 2500 && counter->calls == 50, "50 full boards become one");
    printf("50 TriggerOrder requests: maxCards calls %d -> %d; wall_ms %.3f -> %.3f\n",
        beforeCalls, counter->calls, beforeNs / 1e6, afterNs / 1e6);

    check(beforeDistances > 0 && distance->calls == 0, "TriggerOrder avoids distance callbacks");
    printf("distance callbacks %d -> %d\n", beforeDistances, distance->calls);
    const auto choice = ai.makeRequest(players.first(), AIRequest::Choice,
        CardUseStruct::CARD_USE_REASON_UNKNOWN, {}, {}, Card::MethodNone);
    check(choice.worldView.distanceScope == "viewer" && distance->calls > 0,
        "other decisions retain geometry");
    const int initialMaxCards = request(players.first()).self.maxCards;
    players.first()->setMark("h50p_extra", 2);
    const auto updated = request(players.first());
    check(counter->calls == 100 && updated.self.maxCards == initialMaxCards + 2, "revision invalidates and refreshes facts");
    request(players.last());
    check(counter->calls == 100, "another viewer reuses public facts");
    SkillSet::bump();
    request(players.first());
    check(counter->calls == 150, "skill generation invalidates");
    ai.recordEvent(GameReady, players.first(), QVariant());
    request(players.first());
    check(counter->calls == 150, "events reuse unchanged public facts");
    const auto eventWorld = ai.makeRequest(players.first(), AIRequest::Choice,
        CardUseStruct::CARD_USE_REASON_UNKNOWN, {}, {}, Card::MethodNone).worldView;
    check(eventWorld.events.size() == 1 && eventWorld.events.first().triggerEvent == GameReady,
        "event log projects live across a public-board cache hit");
    RoomTestAccess::invalidate(*room);
    counter->mutate = true;
    request(players.first());
    request(players.first());
    check(counter->calls == 250, "mid-fill mutation cannot publish a reusable board");
    request(players.last());
    check(counter->calls == 250, "stable refill is reusable");
    players.first()->setFlags("h50p_private");
    check(request(players.first()).self.privateFlags.contains("h50p_private"), "private projection stays live");
    const auto other = request(players.last());
    for (const auto &player : other.players)
        check(player.privateFlags.isEmpty(), "private flags never cross viewer boundary");
    // Definition registration must invalidate even without a player skill change.
    class ExtraMaxCards : public MaxCardsSkill {
    public:
        ExtraMaxCards() : MaxCardsSkill("h50p_added_definition") {}
        int getExtra(const Player *) const override { return 1; }
    };
    const int beforeDefinition = counter->calls;
    room->roomRuntime()->addSkills({new ExtraMaxCards});
    check(request(players.first()).self.maxCards == initialMaxCards + 3
        && counter->calls == beforeDefinition + 50, "definition change invalidates");
    auto *newPlayer = new ServerPlayer(room);
    newPlayer->setObjectName("h50p_added_seat");
    newPlayer->setMaxHp(4);
    newPlayer->setHp(4);
    RoomTestAccess::add(*room, newPlayer);
    check(request(players.first()).players.size() == 50, "roster growth is reflected");
    RoomTestAccess::removeAlive(*room, players.last());
    check(request(players.first()).alivePlayerOrder.size() == 49,
        "alive roster invalidates even without a state signal");
    puts("PASS: revision, generation, definition, roster, event, mid-fill and viewer isolation guards");
    fflush(stdout);
    // Like the distance probe, this tests native snapshots, not worker teardown.
    std::_Exit(0);
}
