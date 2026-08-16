#include "engine-bootstrap.h"
#include "room-roster.h"
#include "room.h"
#include "serverplayer.h"

#include <QCoreApplication>
#include <QDebug>
#include <QPointer>

struct RoomTestAccess
{
    static ServerPlayer *addPlayer(Room &room, const QString &objectName)
    {
        ServerPlayer *player = new ServerPlayer(&room);
        player->setObjectName(objectName);
        room.m_roster->add(player);
        return player;
    }

    static RoomRoster &roster(Room &room)
    {
        return *room.m_roster;
    }

    static void resetAlive(Room &room)
    {
        room.m_roster->resetAliveToPlayers();
    }
};

class RosterFixture
{
public:
    RosterFixture()
        : room(nullptr, QStringLiteral("03_1v2"))
    {
    }

    ServerPlayer *add(const QString &objectName)
    {
        return RoomTestAccess::addPlayer(room, objectName);
    }

    RoomRoster &roster()
    {
        return RoomTestAccess::roster(room);
    }

    void resetAlive()
    {
        RoomTestAccess::resetAlive(room);
    }

    Room room;
};

static bool expect(bool condition, const char *context)
{
    if (condition)
        return true;
    qCritical() << "room roster test failed:" << context;
    return false;
}

static bool expectPlayers(const QList<ServerPlayer *> &actual,
                          const QList<ServerPlayer *> &expected,
                          const char *context)
{
    if (actual == expected)
        return true;
    QStringList actualNames;
    QStringList expectedNames;
    foreach (ServerPlayer *player, actual)
        actualNames << player->objectName();
    foreach (ServerPlayer *player, expected)
        expectedNames << player->objectName();
    qCritical() << "room roster test failed:" << context
                << "expected" << expectedNames << "got" << actualNames;
    return false;
}

static bool addAndCopiesStayIndependent()
{
    RosterFixture fixture;
    ServerPlayer *first = fixture.add(QStringLiteral("first"));
    ServerPlayer *second = fixture.add(QStringLiteral("second"));
    RoomRoster &roster = fixture.roster();

    if (!expectPlayers(roster.players(), QList<ServerPlayer *>() << first << second,
                       "add keeps canonical player order")
        || !expectPlayers(fixture.room.getPlayers(), QList<ServerPlayer *>() << first << second,
                          "Room getPlayers forwards to the roster")
        || !expect(first->parent() == &fixture.room && second->parent() == &fixture.room,
                   "Room keeps QObject ownership of roster players")
        || !expect(roster.aliveCount() == 0, "add does not add alive players"))
        return false;

    fixture.resetAlive();
    QList<ServerPlayer *> playerCopy = roster.players();
    QList<ServerPlayer *> aliveCopy = roster.alivePlayers();
    playerCopy.clear();
    aliveCopy.removeFirst();
    return expectPlayers(roster.players(), QList<ServerPlayer *>() << first << second,
                         "players returns a value copy")
        && expectPlayers(roster.alivePlayers(), QList<ServerPlayer *>() << first << second,
                         "alivePlayers returns a value copy")
        && expectPlayers(fixture.room.getAlivePlayers(),
                         QList<ServerPlayer *>() << first << second,
                         "Room getAlivePlayers forwards to the roster")
        && expect(roster.aliveCount() == 2 && fixture.room.alivePlayerCount() == 2,
                  "alive count forwards to the roster");
}

static bool orderedAndLookupQueriesKeepLegacyOrder()
{
    RosterFixture fixture;
    ServerPlayer *first = fixture.add(QStringLiteral("first"));
    ServerPlayer *second = fixture.add(QStringLiteral("second"));
    ServerPlayer *third = fixture.add(QStringLiteral("third"));
    ServerPlayer outsider(nullptr);
    first->setGeneralName(QStringLiteral("caocao"));
    second->setGeneralName(QStringLiteral("liubei"));
    third->setGeneralName(QStringLiteral("sunquan"));
    third->setAlive(false);
    fixture.roster().rebuildAlive();
    fixture.room.setCurrent(second);

    RoomRoster &roster = fixture.roster();
    if (!expectPlayers(roster.orderedFrom(second, false),
                       QList<ServerPlayer *>() << second << first,
                       "orderedFrom rotates and filters dead players")
        || !expectPlayers(roster.orderedFrom(second, true),
                          QList<ServerPlayer *>() << second << third << first,
                          "orderedFrom rotates all players")
        || !expectPlayers(fixture.room.getAllPlayers(false),
                          QList<ServerPlayer *>() << second << first,
                          "Room getAllPlayers forwards current-relative ordering")
        || !expectPlayers(roster.orderedFrom(nullptr, false),
                          QList<ServerPlayer *>() << first << second << third,
                          "null current keeps legacy unfiltered order")
        || !expectPlayers(roster.orderedFrom(&outsider, false),
                          QList<ServerPlayer *>() << first << second << third,
                          "outsider current keeps legacy unfiltered order")
        || !expect(roster.findByGeneral(QStringLiteral("liubei+caocao"), false) == first,
                   "general alternatives follow roster order")
        || !expect(roster.findByGeneral(QStringLiteral("sunquan"), false) == nullptr,
                   "dead general is absent from alive lookup")
        || !expect(fixture.room.findPlayer(QStringLiteral("liubei+caocao"), false) == first,
                   "Room general lookup forwards first-match semantics")
        || !expect(roster.findByObjectName(QStringLiteral("third"), second, true) == third,
                   "object name lookup is current-relative")
        || !expect(roster.findByObjectName(QStringLiteral("third"), second, false) == nullptr,
                   "object name lookup filters dead players"))
        return false;

    return expectPlayers(roster.otherPlayers(second, second, false),
                         QList<ServerPlayer *>() << first,
                         "otherPlayers preserves current-relative order");
}

static bool aliveMembershipOperationsPreserveSeatContracts()
{
    RosterFixture fixture;
    ServerPlayer *first = fixture.add(QStringLiteral("first"));
    ServerPlayer *second = fixture.add(QStringLiteral("second"));
    ServerPlayer *third = fixture.add(QStringLiteral("third"));
    ServerPlayer outsider(nullptr);
    RoomRoster &roster = fixture.roster();

    fixture.resetAlive();
    first->setSeat(1);
    second->setSeat(2);
    third->setSeat(3);
    second->setAlive(false);
    roster.removeAlive(second);
    if (!expectPlayers(roster.players(), QList<ServerPlayer *>() << first << second << third,
                       "removeAlive leaves canonical players unchanged")
        || !expectPlayers(roster.alivePlayers(), QList<ServerPlayer *>() << first << third,
                          "removeAlive removes the victim from the cached list")
        || !expect(second->getSeat() == 2, "removeAlive leaves victim seat unchanged")
        || !expect(third->getSeat() == 2, "removeAlive decrements later alive seats"))
        return false;

    roster.removeAlive(&outsider);
    if (!expect(first->getSeat() == 0 && third->getSeat() == 1,
                "outsider removeAlive keeps the legacy all-tail decrement"))
        return false;

    second->setSeat(42);
    roster.rebuildAlive();
    roster.reseatAlive();
    return expectPlayers(roster.alivePlayers(), QList<ServerPlayer *>() << first << third,
                         "rebuildAlive follows canonical alive order")
        && expect(first->getSeat() == 1 && third->getSeat() == 2,
                  "reseatAlive numbers alive players")
        && expect(second->getSeat() == 42, "reseatAlive leaves dead seats untouched");
}

static bool seatOrderOperationsRelinkPlayers()
{
    RosterFixture fixture;
    ServerPlayer *first = fixture.add(QStringLiteral("first"));
    ServerPlayer *second = fixture.add(QStringLiteral("second"));
    ServerPlayer *third = fixture.add(QStringLiteral("third"));
    RoomRoster &roster = fixture.roster();

    second->setAlive(false);
    roster.swapSeats(first, third);
    if (!expectPlayers(roster.players(), QList<ServerPlayer *>() << third << second << first,
                       "swapSeats swaps canonical order")
        || !expectPlayers(roster.alivePlayers(), QList<ServerPlayer *>() << third << first,
                          "swapSeats rebuilds alive order")
        || !expect(third->getSeat() == 1 && second->getSeat() == 0 && first->getSeat() == 2,
                   "swapSeats assigns alive and dead seats")
        || !expect(third->getPlayerSeat() == 1 && second->getPlayerSeat() == 2
                       && first->getPlayerSeat() == 3,
                   "swapSeats assigns canonical player seats")
        || !expect(third->getNext() == second && second->getNext() == first
                       && first->getNext() == third,
                   "swapSeats relinks normal play order"))
        return false;

    roster.reversePlayOrder();
    return expect(roster.isPlayOrderReversed(), "reversePlayOrder records direction")
        && expect(third->getNext() == first && second->getNext() == third
                      && first->getNext() == second,
                  "reversePlayOrder relinks the reverse ring");
}

static bool singlePlayerOrderKeepsLegacyNextBehavior()
{
    RosterFixture fixture;
    ServerPlayer *player = fixture.add(QStringLiteral("only"));

    player->setNext(nullptr);
    fixture.roster().swapSeats(player, player);
    if (!expect(player->getNext() == player,
                "single-player swap links the player to itself"))
        return false;

    player->setNext(nullptr);
    fixture.roster().reversePlayOrder();
    return expect(player->getNext() == nullptr,
                  "single-player reverse leaves the next pointer unchanged");
}

static bool adjustSeatsKeepsAliveCacheUntouched()
{
    RosterFixture fixture;
    ServerPlayer *first = fixture.add(QStringLiteral("first"));
    ServerPlayer *lord = fixture.add(QStringLiteral("lord"));
    ServerPlayer *third = fixture.add(QStringLiteral("third"));
    RoomRoster &roster = fixture.roster();

    lord->setRole(QStringLiteral("lord"));
    lord->setAlive(false);
    fixture.resetAlive();
    roster.adjustSeats(false);
    if (!expectPlayers(roster.players(), QList<ServerPlayer *>() << lord << third << first,
                       "adjustSeats rotates the lord to the first position")
        || !expectPlayers(roster.alivePlayers(), QList<ServerPlayer *>() << first << lord << third,
                          "adjustSeats does not rebuild the alive cache")
        || !expect(lord->getSeat() == 1 && third->getSeat() == 2 && first->getSeat() == 3,
                   "adjustSeats numbers the adjusted player order"))
        return false;

    roster.adjustSeats(true);
    return expectPlayers(roster.players(), QList<ServerPlayer *>() << lord << third << first,
                         "keepOriginalStart prevents lord rotation");
}

static bool pathsFollowRoomOwnedAliveRing()
{
    RosterFixture fixture;
    ServerPlayer *first = fixture.add(QStringLiteral("first"));
    ServerPlayer *second = fixture.add(QStringLiteral("second"));
    ServerPlayer *third = fixture.add(QStringLiteral("third"));
    ServerPlayer *fourth = fixture.add(QStringLiteral("fourth"));
    RoomRoster &roster = fixture.roster();

    fixture.resetAlive();
    roster.reversePlayOrder();
    roster.reversePlayOrder();
    if (!expectPlayers(roster.clockwisePath(first, third, false, false),
                       QList<ServerPlayer *>() << second,
                       "clockwise path follows the next-alive ring")
        || !expectPlayers(roster.counterclockwisePath(first, third, false, false),
                          QList<ServerPlayer *>() << fourth,
                          "counterclockwise path follows the reverse ring")
        || !expectPlayers(roster.pathBetween(first, third, true, true),
                          QList<ServerPlayer *>() << first << second << third,
                          "path tie keeps clockwise ordering"))
        return false;

    return expectPlayers(roster.clockwisePath(first, third, true, true),
                         QList<ServerPlayer *>() << first << second << third,
                         "clockwise path keeps requested endpoints")
        && expectPlayers(roster.counterclockwisePath(first, third, true, true),
                         QList<ServerPlayer *>() << third << fourth << first,
                         "counterclockwise path keeps requested endpoints");
}

static bool rosterNeverOwnsPlayers()
{
    ServerPlayer *player = new ServerPlayer(nullptr);
    QPointer<ServerPlayer> guard(player);
    {
        RoomRoster roster;
        roster.add(player);
        roster.resetAliveToPlayers();
    }
    const bool preserved = guard && player->parent() == nullptr;
    delete player;
    return expect(preserved && guard.isNull(), "RoomRoster does not delete or reparent players");
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "engine initialization failed:" << error;
        return 1;
    }

    if (!addAndCopiesStayIndependent())
        return 2;
    if (!orderedAndLookupQueriesKeepLegacyOrder())
        return 3;
    if (!aliveMembershipOperationsPreserveSeatContracts())
        return 4;
    if (!seatOrderOperationsRelinkPlayers())
        return 5;
    if (!singlePlayerOrderKeepsLegacyNextBehavior())
        return 6;
    if (!adjustSeatsKeepsAliveCacheUntouched())
        return 7;
    if (!pathsFollowRoomOwnedAliveRing())
        return 8;
    if (!rosterNeverOwnsPlayers())
        return 9;

    qInfo() << "room roster behavior passed";
    return 0;
}
