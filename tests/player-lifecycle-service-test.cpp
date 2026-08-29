#include "engine-bootstrap.h"
#include "ai.h"
#include "card-movement-service.h"
#include "event-dispatcher.h"
#include "json.h"
#include "player-lifecycle-service.h"
#include "protocol.h"
#include "room-notifier.h"
#include "room-roster.h"
#include "room.h"
#include "roomthread.h"
#include "serverplayer.h"
#include "skill-runtime-coordinator.h"

#include <QCoreApplication>
#include <QDebug>

using namespace QSanProtocol;

struct PlayerLifecycleServiceTestAccess
{
    static ServerPlayer *addPlayer(Room &room, const QString &objectName)
    {
        ServerPlayer *player = new ServerPlayer(&room);
        player->setObjectName(objectName);
        room.m_roster->add(player);
        return player;
    }

    static void resetAlive(Room &room)
    {
        room.m_roster->resetAliveToPlayers();
    }

    static void attachOrdinaryRuntime(Room &room, ServerPlayer *player)
    {
        player->setAlive(true);
        player->setRemoved(false);
        TrustAI *ai = new TrustAI(player);
        ai->setParent(player);
        player->setAI(ai);
        room.thread = new RoomThread(&room);
    }

    static RoomRoster &roster(Room &room)
    {
        return *room.m_roster;
    }

    static PlayerLifecycleService makeService(Room &room, EventDispatcher &dispatcher)
    {
        return PlayerLifecycleService(room, *room.m_roster, *room.m_skillRuntime,
                                      *room.m_cardMovement, *room.m_notifier, dispatcher);
    }

    static PlayerLifecycleService &service(Room &room)
    {
        return *room.m_playerLifecycle;
    }

    static void appendDynamicPlayer(PlayerLifecycleService &service, ServerPlayer *player)
    {
        service.m_dynamicPlayers.append(player);
    }
};

namespace {

class RecordingEventDispatcher : public EventDispatcher
{
public:
    bool dispatch(TriggerEvent event, ServerPlayer *target, QVariant &data) override
    {
        Q_UNUSED(data);
        events << event;
        targets << target;
        if (throwOnBury && event == BuryVictim)
            throw thrownEvent;
        return cancelledEvents.contains(event);
    }

    void registerTriggerSkill(const TriggerSkill *skill) override
    {
        registeredSkills << skill;
    }

    QList<TriggerEvent> events;
    QList<ServerPlayer *> targets;
    QList<TriggerEvent> cancelledEvents;
    QList<const TriggerSkill *> registeredSkills;
    bool throwOnBury = false;
    TriggerEvent thrownEvent = TurnBroken;
};

struct PacketRecord
{
    CommandType command;
    QVariant body;
};

class PacketRecorder
{
public:
    void watch(ServerPlayer *player)
    {
        QObject::connect(player, &ServerPlayer::message_ready, player,
                         [this](const QByteArray &message) {
            Packet packet;
            if (!packet.parse(message)) {
                parseFailed = true;
                return;
            }
            records << PacketRecord{packet.getCommandType(), packet.getMessageBody()};
        });
    }

    void clear()
    {
        records.clear();
        parseFailed = false;
    }

    int indexOf(CommandType command, int from = 0) const
    {
        for (int i = from; i < records.length(); ++i) {
            if (records[i].command == command)
                return i;
        }
        return -1;
    }

    QList<PacketRecord> records;
    bool parseFailed = false;
};

static bool expect(bool condition, const char *context)
{
    if (condition)
        return true;
    qCritical() << "player lifecycle service test failed:" << context;
    return false;
}

static bool serviceConstructionDoesNotDispatch()
{
    Room room(nullptr, QStringLiteral("03_1v2"));
    RecordingEventDispatcher dispatcher;
    {
        PlayerLifecycleService service =
            PlayerLifecycleServiceTestAccess::makeService(room, dispatcher);
        Q_UNUSED(service);
    }

    return expect(dispatcher.events.isEmpty(),
                  "service construction and destruction do not dispatch events")
        && expect(dispatcher.registeredSkills.isEmpty(),
                  "service construction and destruction do not register trigger skills");
}

static bool roomFacadeAddsRoomOwnedRobot()
{
    Room room(nullptr, QStringLiteral("03_1v2"));
    ServerPlayer *player = room.addAIPlayer();
    const QList<ServerPlayer *> players = room.getPlayers();

    return expect(player != nullptr, "addAIPlayer returns a player")
        && expect(player->parent() == &room, "Room remains the robot QObject owner")
        && expect(player->getState() == QStringLiteral("robot"),
                  "addAIPlayer initializes the robot state")
        && expect(players.count(player) == 1, "robot appears once in the canonical roster")
        && expect(room.getAlivePlayers().isEmpty(),
                  "adding a robot does not implicitly change alive-roster membership");
}

static bool roomFacadeOwnsPendingSummonQueue()
{
    Room room(nullptr, QStringLiteral("03_1v2"));
    ServerPlayer *before = PlayerLifecycleServiceTestAccess::addPlayer(room,
                                                                        QStringLiteral("before"));
    ServerPlayer *after = PlayerLifecycleServiceTestAccess::addPlayer(room,
                                                                       QStringLiteral("after"));

    if (!expect(!room.hasPendingSummons(), "new room starts with no pending summon"))
        return false;

    room.requestSummonBetween(before, after, QStringLiteral("caocao"));
    return expect(room.hasPendingSummons(),
                  "Room facade forwards a pending summon into lifecycle-owned state")
        && expect(room.getPlayers() == (QList<ServerPlayer *>() << before << after),
                  "queuing a summon does not mutate the roster before the event boundary");
}

static bool roomFacadePreservesRestTransitions()
{
    Room room(nullptr, QStringLiteral("03_1v2"));
    ServerPlayer *softRest = PlayerLifecycleServiceTestAccess::addPlayer(room,
                                                                          QStringLiteral("soft"));
    ServerPlayer *directRest = PlayerLifecycleServiceTestAccess::addPlayer(room,
                                                                            QStringLiteral("direct"));
    softRest->setAlive(true);
    directRest->setAlive(true);
    PlayerLifecycleServiceTestAccess::resetAlive(room);

    room.restPlayer(softRest, QStringLiteral("soft-rest"));
    if (!expect(room.isRest(softRest), "soft rest sets the RestPlayer property")
        || !expect(softRest->isAlive(), "soft rest preserves alive state")
        || !expect(room.getAlivePlayers().contains(softRest),
                   "soft rest preserves alive-roster membership"))
        return false;

    room.directRestPlayer(directRest, QStringLiteral("direct-rest"));
    if (!expect(room.isRest(directRest), "direct rest sets the RestPlayer property")
        || !expect(!directRest->isAlive(), "direct rest clears alive state")
        || !expect(!room.getAlivePlayers().contains(directRest),
                   "direct rest removes the player from the alive roster"))
        return false;

    RecordingEventDispatcher dispatcher;
    PlayerLifecycleService service =
        PlayerLifecycleServiceTestAccess::makeService(room, dispatcher);
    service.unrestPlayer(directRest, false, false);
    return expect(!room.isRest(directRest), "unrest clears the RestPlayer property")
        && expect(directRest->isAlive(), "unrest revives a directly rested player")
        && expect(room.getAlivePlayers().contains(directRest),
                  "unrest restores alive-roster membership")
        && expect(dispatcher.events == (QList<TriggerEvent>() << Revive << Revived),
                  "unrest keeps the revive event order");
}

static bool dispatcherCancellationKeepsRevivePreState()
{
    Room room(nullptr, QStringLiteral("03_1v2"));
    ServerPlayer *player = PlayerLifecycleServiceTestAccess::addPlayer(room,
                                                                        QStringLiteral("cancelled"));
    player->setAlive(false);
    PlayerLifecycleServiceTestAccess::roster(room).rebuildAlive();

    RecordingEventDispatcher dispatcher;
    dispatcher.cancelledEvents << Revive;
    PlayerLifecycleService service =
        PlayerLifecycleServiceTestAccess::makeService(room, dispatcher);
    service.revivePlayer(player, false, true, false);

    return expect(!player->isAlive(), "cancelled Revive keeps the player dead")
        && expect(!room.getAlivePlayers().contains(player),
                  "cancelled Revive keeps the player outside the alive roster")
        && expect(dispatcher.events == (QList<TriggerEvent>() << Revive),
                  "cancelled Revive does not dispatch the post-revive event");
}

static bool killCancellationKeepsLegacyPreMutations()
{
    Room room(nullptr, QStringLiteral("03_1v2"));
    ServerPlayer *victim = PlayerLifecycleServiceTestAccess::addPlayer(
        room, QStringLiteral("victim"));
    ServerPlayer *survivor = PlayerLifecycleServiceTestAccess::addPlayer(
        room, QStringLiteral("survivor"));
    victim->setAlive(true);
    survivor->setAlive(true);
    survivor->setState(QStringLiteral("online"));
    PlayerLifecycleServiceTestAccess::resetAlive(room);

    RecordingEventDispatcher dispatcher;
    dispatcher.cancelledEvents << BeforeGameOverJudge;
    PlayerLifecycleService service =
        PlayerLifecycleServiceTestAccess::makeService(room, dispatcher);
    service.killPlayer(victim, nullptr, nullptr);

    return expect(!victim->isAlive(),
                  "cancelled BeforeGameOverJudge keeps the pre-event dead state")
        && expect(!room.getAlivePlayers().contains(victim),
                  "cancelled BeforeGameOverJudge keeps the pre-event roster removal")
        && expect(room.getAlivePlayers() == (QList<ServerPlayer *>() << survivor),
                  "cancelled death leaves the survivor as the only alive player")
        && expect(dispatcher.events
                      == (QList<TriggerEvent>() << BeforeGameOverJudge),
                  "cancelled death emits no later lifecycle events")
        && expect(dispatcher.targets == (QList<ServerPlayer *>() << victim),
                  "BeforeGameOverJudge targets the victim");
}

static bool killLifecycleKeepsEventTargetOrder()
{
    Room room(nullptr, QStringLiteral("03_1v2"));
    ServerPlayer *victim = PlayerLifecycleServiceTestAccess::addPlayer(
        room, QStringLiteral("victim"));
    ServerPlayer *survivor = PlayerLifecycleServiceTestAccess::addPlayer(
        room, QStringLiteral("survivor"));
    victim->setAlive(true);
    survivor->setAlive(true);
    survivor->setState(QStringLiteral("online"));
    PlayerLifecycleServiceTestAccess::resetAlive(room);

    RecordingEventDispatcher dispatcher;
    PlayerLifecycleService service =
        PlayerLifecycleServiceTestAccess::makeService(room, dispatcher);
    service.killPlayer(victim, nullptr, nullptr);

    const QList<TriggerEvent> expectedEvents = QList<TriggerEvent>()
        << BeforeGameOverJudge << GameOverJudge << Death << Death << BuryVictim;
    const QList<ServerPlayer *> expectedTargets = QList<ServerPlayer *>()
        << victim << victim << victim << survivor << victim;
    if (!expect(dispatcher.events == expectedEvents,
                "full death keeps Before/GameOver/Death/Bury event order")
        || !expect(dispatcher.targets == expectedTargets,
                   "full death keeps per-event target order"))
        return false;

    Room exceptionRoom(nullptr, QStringLiteral("03_1v2"));
    ServerPlayer *exceptionVictim = PlayerLifecycleServiceTestAccess::addPlayer(
        exceptionRoom, QStringLiteral("exception-victim"));
    ServerPlayer *exceptionSurvivor = PlayerLifecycleServiceTestAccess::addPlayer(
        exceptionRoom, QStringLiteral("exception-survivor"));
    exceptionVictim->setAlive(true);
    exceptionSurvivor->setAlive(true);
    exceptionSurvivor->setState(QStringLiteral("online"));
    exceptionVictim->setMark(QStringLiteral("wujieNoRewardAndPunish-Keep"), 1);
    PlayerLifecycleServiceTestAccess::resetAlive(exceptionRoom);

    RecordingEventDispatcher exceptionDispatcher;
    exceptionDispatcher.throwOnBury = true;
    exceptionDispatcher.thrownEvent = TurnBroken;
    PlayerLifecycleService exceptionService =
        PlayerLifecycleServiceTestAccess::makeService(exceptionRoom, exceptionDispatcher);
    exceptionService.killPlayer(exceptionVictim, nullptr, nullptr);
    return expect(exceptionVictim->getMark(
                      QStringLiteral("wujieNoRewardAndPunish-Keep")) == 0,
                  "TurnBroken from BuryVictim keeps the legacy cleanup");
}

static bool heroCancellationStopsMutation()
{
    Room room(nullptr, QStringLiteral("03_1v2"));
    ServerPlayer *player = PlayerLifecycleServiceTestAccess::addPlayer(
        room, QStringLiteral("hero"));
    player->setGeneralName(QStringLiteral("caocao"));
    player->setGeneral2Name(QStringLiteral("guanyu"));

    RecordingEventDispatcher dispatcher;
    dispatcher.cancelledEvents << GeneralChange;
    PlayerLifecycleService service =
        PlayerLifecycleServiceTestAccess::makeService(room, dispatcher);
    service.changeHero(player, QStringLiteral("sunquan"), false, false,
                       false, false, -1);
    return expect(player->getGeneralName() == QStringLiteral("caocao"),
                  "cancelled GeneralChange leaves the primary general unchanged")
        && expect(dispatcher.events == (QList<TriggerEvent>() << GeneralChange),
                  "cancelled GeneralChange emits no GeneralChanged event");
}

static bool primaryAndSecondaryGeneralMutationStaySeparated()
{
    Room room(nullptr, QStringLiteral("03_1v2"));
    ServerPlayer *player = PlayerLifecycleServiceTestAccess::addPlayer(
        room, QStringLiteral("general"));
    PlayerLifecycleServiceTestAccess::attachOrdinaryRuntime(room, player);
    player->setGeneralName(QStringLiteral("caocao"));
    player->setGeneral2Name(QStringLiteral("guanyu"));

    RecordingEventDispatcher dispatcher;
    PlayerLifecycleService service =
        PlayerLifecycleServiceTestAccess::makeService(room, dispatcher);
    service.changePlayerGeneral(player, QStringLiteral("liubei"));
    service.changePlayerGeneral2(player, QStringLiteral("zhangfei"));
    return expect(player->getGeneralName() == QStringLiteral("liubei"),
                  "primary general mutation is service-owned")
        && expect(player->getGeneral2Name() == QStringLiteral("zhangfei"),
                  "secondary general mutation is service-owned");
}

static bool signupKeepsHumanPacketOrderAndRobotState()
{
    Room robotRoom(nullptr, QStringLiteral("03_1v2"));
    ServerPlayer *robot = robotRoom.addAIPlayer();
    robotRoom.signup(robot, QStringLiteral("Robot"), QStringLiteral("caocao"), true);
    if (!expect(!robot->objectName().isEmpty(), "robot signup assigns an object name")
        || !expect(robot->screenName() == QStringLiteral("Robot"),
                   "robot signup preserves the screen name")
        || !expect(robot->property("avatar").toString() == QStringLiteral("caocao"),
                   "robot signup stores the avatar"))
        return false;

    Room humanRoom(nullptr, QStringLiteral("03_1v2"));
    ServerPlayer *human = humanRoom.addSocket(nullptr);
    PacketRecorder recorder;
    recorder.watch(human);
    humanRoom.signup(human, QStringLiteral("Human"), QStringLiteral("liubei"), false);

    const int speakIndex = recorder.indexOf(S_COMMAND_SPEAK);
    const int delayIndex = recorder.indexOf(S_COMMAND_NETWORK_DELAY_TEST);
    return expect(!recorder.parseFailed, "human signup packets parse")
        && expect(human->isOwner(), "first human signup becomes Room owner")
        && expect(speakIndex >= 0 && delayIndex > speakIndex,
                  "human greeting precedes the network-delay probe");
}

static bool marshalReplaysDynamicPlayerInProtocolOrder()
{
    Room room(nullptr, QStringLiteral("03_1v2"));
    ServerPlayer *receiver = PlayerLifecycleServiceTestAccess::addPlayer(
        room, QStringLiteral("receiver"));
    ServerPlayer *controlled = PlayerLifecycleServiceTestAccess::addPlayer(
        room, QStringLiteral("controlled"));
    ServerPlayer *dynamic = PlayerLifecycleServiceTestAccess::addPlayer(
        room, QStringLiteral("dynamic"));
    receiver->setGeneralName(QStringLiteral("caocao"));
    controlled->setGeneralName(QStringLiteral("liubei"));
    dynamic->setGeneralName(QStringLiteral("guanyu"));
    room.setPlayerController(controlled, receiver);

    PlayerLifecycleService &service = PlayerLifecycleServiceTestAccess::service(room);
    PlayerLifecycleServiceTestAccess::appendDynamicPlayer(service, dynamic);
    PacketRecorder recorder;
    recorder.watch(receiver);
    service.marshal(receiver);

    const int arrangeIndex = recorder.indexOf(S_COMMAND_ARRANGE_SEATS);
    const int dynamicIndex = recorder.indexOf(S_COMMAND_ADD_PLAYER_DYNAMIC);
    const int startIndex = recorder.indexOf(S_COMMAND_START_IN_X_SECONDS);
    const int knownCardsIndex = recorder.indexOf(S_COMMAND_SET_KNOWN_CARDS);
    const int contextIndex = recorder.indexOf(S_COMMAND_SWITCH_CONTEXT);
    if (!expect(!recorder.parseFailed, "marshal packets parse")
        || !expect(arrangeIndex >= 0 && dynamicIndex > arrangeIndex
                       && startIndex > dynamicIndex,
                   "marshal orders ARRANGE before dynamic replay before countdown")
        || !expect(knownCardsIndex > startIndex && contextIndex > knownCardsIndex,
                   "marshal restores controlled hand knowledge before context switch"))
        return false;

    recorder.clear();
    service.reconnect(receiver, nullptr);
    return expect(receiver->getState() == QStringLiteral("online"),
                  "reconnect sets online state before marshal")
        && expect(recorder.indexOf(S_COMMAND_ARRANGE_SEATS) >= 0,
                  "reconnect replays the marshal protocol");
}

}

int runPlayerLifecycleServiceTests(int argc, char **argv)
{
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "engine initialization failed:" << error;
        return 1;
    }

    const QString selectedCase = argc == 3 && QString::fromLatin1(argv[1]) == QStringLiteral("--case")
        ? QString::fromLatin1(argv[2]) : QString();
    const auto shouldRun = [&selectedCase](const char *name) {
        return selectedCase.isEmpty() || selectedCase == QString::fromLatin1(name);
    };

    if (shouldRun("construction") && !serviceConstructionDoesNotDispatch())
        return 2;
    if (shouldRun("add-ai") && !roomFacadeAddsRoomOwnedRobot())
        return 3;
    if (shouldRun("pending-summon") && !roomFacadeOwnsPendingSummonQueue())
        return 4;
    if (shouldRun("rest-unrest") && !roomFacadePreservesRestTransitions())
        return 5;
    if (shouldRun("revive-cancel") && !dispatcherCancellationKeepsRevivePreState())
        return 6;
    if (shouldRun("kill-cancel") && !killCancellationKeepsLegacyPreMutations())
        return 7;
    if (shouldRun("kill-full") && !killLifecycleKeepsEventTargetOrder())
        return 8;
    if (shouldRun("hero-cancel") && !heroCancellationStopsMutation())
        return 9;
    if (shouldRun("general-mutation") && !primaryAndSecondaryGeneralMutationStaySeparated())
        return 10;
    if (shouldRun("signup") && !signupKeepsHumanPacketOrderAndRobotState())
        return 11;
    if (shouldRun("marshal-reconnect") && !marshalReplaysDynamicPlayerInProtocolOrder())
        return 12;

    if (!selectedCase.isEmpty()
        && selectedCase != QStringLiteral("construction")
        && selectedCase != QStringLiteral("add-ai")
        && selectedCase != QStringLiteral("pending-summon")
        && selectedCase != QStringLiteral("rest-unrest")
        && selectedCase != QStringLiteral("revive-cancel")
        && selectedCase != QStringLiteral("kill-cancel")
        && selectedCase != QStringLiteral("kill-full")
        && selectedCase != QStringLiteral("hero-cancel")
        && selectedCase != QStringLiteral("general-mutation")
        && selectedCase != QStringLiteral("signup")
        && selectedCase != QStringLiteral("marshal-reconnect"))
        return 64;

    qInfo() << "player lifecycle service behavior passed";
    return 0;
}
