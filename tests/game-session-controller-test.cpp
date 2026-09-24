#include "engine-bootstrap.h"
#include "engine.h"
#include "engine-runtime-context.h"
#include "general.h"
#include "game-session-controller.h"
#include "room.h"
#include "room-test-access.h"
#include "server-info.h"
#include "settings.h"
#include "protocol/gameplay/simple-choice-payloads.h"
#include "protocol/gameplay/protocol-gameplay-payload-registry.h"

#include <QDebug>
#include <QScopeGuard>

struct GameSessionControllerTestAccess
{
    static bool transition(GameSessionController &controller,
                           GameSessionController::State next)
    {
        return controller.transitionTo(next);
    }

    static void finish(GameSessionController &controller)
    {
        if (controller.transitionTo(GameSessionController::State::Finished))
            controller.m_terminationCause = GameSessionController::TerminationCause::GameOver;
    }
};

namespace {

static bool expect(bool condition, const char *context)
{
    if (condition)
        return true;
    qCritical() << "game session controller test failed:" << context;
    return false;
}

static bool hegemonySeatAssignment()
{
    using namespace QSanProtocol;
    const bool hegemony = Config.EnableHegemony, second = Config.Enable2ndGeneral;
    const bool randomSeat = Config.RandomSeat, unlimited = Config.OperationNoLimit;
    const int timeout = Config.OperationTimeout;
    const QVariantMap overrides = Config.valueOverrides();
    auto restore = qScopeGuard([=]() {
        Config.EnableHegemony = hegemony;
        Config.Enable2ndGeneral = second;
        Config.RandomSeat = randomSeat;
        Config.OperationNoLimit = unlimited;
        Config.OperationTimeout = timeout;
        Config.setValueOverrides(overrides);
    });
    Config.EnableHegemony = true;
    Config.RandomSeat = false;
    Config.OperationNoLimit = false;
    Config.OperationTimeout = 1;
    const auto run = [&](const QStringList &names, const QStringList &seats,
                         bool enabled, bool cancel, bool reordered) {
        QVariantMap values = overrides;
        values.insert(QStringLiteral("FreeAssign"), enabled);
        Config.setValueOverrides(values);
        Room room(nullptr, QStringLiteral("04p"));
        EngineRuntimeContextScope engineScope(*Sanguosha, &room);
        QList<ServerPlayer *> players;
        for (const QString &name : {QStringLiteral("s1"), QStringLiteral("s2"), QStringLiteral("s3")})
            players << RoomTestAccess::addPlayer(room, name, QStringLiteral("online"));
        ServerPlayer *owner = players.first();
        owner->setOwner(true);
        int requests = 0;
        bool encoded = true;
        const auto connection = QObject::connect(owner, &ServerPlayer::message_ready, &room,
            [&](const QByteArray &frame) {
                ProtocolMessage request;
                if (!ProtocolCodecRouter().decode(frame, &request).success
                    || request.type != ProtocolMessageType::Request || request.command != S_COMMAND_CHOOSE_ROLE) return;
                ++requests;
                QVariantList wireNames, wireSeats;
                for (const QString &name : names) wireNames << name;
                for (const QString &seat : seats) wireSeats << seat;
                ProtocolMessage logical, reply;
                logical.type = ProtocolMessageType::Reply;
                logical.source = ProtocolEndpoint::Client;
                logical.destination = ProtocolEndpoint::Room;
                logical.command = S_COMMAND_CHOOSE_ROLE;
                logical.replyTo = request.messageId;
                logical.hasPayload = !cancel;
                if (!cancel) logical.payload = QVariantList{wireNames, wireSeats};
                encoded = ProtocolGameplayPayloadRegistry::encodeForWire(logical, &reply, nullptr);
                if (encoded) RoomTestAccess::dispatch(room, owner, reply);
            });
        GameSessionController controller(room);
        controller.prepareForStart();
        QObject::disconnect(connection);
        const QList<ServerPlayer *> expected = reordered
            ? QList<ServerPlayer *>{players[2], players[0], players[1]} : players;
        if (!expect(encoded && requests == (enabled ? 1 : 0), "FreeAssign gates the opening seat request")
            || !expect(room.getPlayers() == expected, "complete seat permutation applied atomically; invalid/cancel keeps order"))
            return false;
        for (int i = 0; i < expected.size(); ++i)
            if (!expect(expected[i]->getSeat() == i + 1 && !expected[i]->hasShownRole()
                        && expected[i]->getRole() == "renegade", "seat assignment leaves factions undecided and hidden"))
                return false;
        return true;
    };
    const QStringList names{"s1", "s2", "s3"};
    return run(names, {"2", "3", "1"}, true, false, true)
        && run(names, {"2", "2", "1"}, true, false, false)
        && run({"s1", "s1", "s3"}, {"2", "3", "1"}, true, false, false)
        && run({"s1", "s2", "unknown"}, {"2", "3", "1"}, true, false, false)
        && run({"s1", "s2"}, {"2", "1"}, true, false, false)
        && run(names, {"2", "3", "4"}, true, false, false)
        && run(names, {"2", "3", "1"}, true, true, false)
        && run(names, {"2", "3", "1"}, false, false, false);
}

static bool hegemonyFreeChoiceKeepsPairRules()
{
    General head(nullptr, "free_choice_test_head", "wei");
    General deputy(nullptr, "free_choice_test_deputy", "wei");
    General other(nullptr, "free_choice_test_other", "shu");
    General incompatible(nullptr, "free_choice_test_incompatible", "wu");
    General lord(nullptr, "free_choice_test_lord$", "wei");
    General hidden(nullptr, "free_choice_test_hidden", "wei", 4, true, true, true);
    General careerist(nullptr, "free_choice_test_careerist", "careerist");
    General convertedLord(nullptr, "heg_lord_free_choice_test$", "wei");
    other.setSubordinateKingdom("wei");
    return expect(head.canPairForHegemony(&deputy), "free pair accepts compatible registered generals")
        && expect(head.canPairForHegemony(&other), "free pair respects secondary kingdoms")
        && expect(!head.canPairForHegemony(&incompatible), "incompatible kingdoms rejected")
        && expect(careerist.canPairForHegemony(&deputy), "careerist may be head")
        && expect(!head.canPairForHegemony(&careerist), "careerist may not be deputy")
        && expect(!head.canPairForHegemony(&head), "same-name pair rejected")
        && expect(!head.canPairForHegemony(nullptr), "missing deputy rejected")
        && expect(!head.canPairForHegemony(&lord), "lord deputy rejected")
        && expect(!head.canPairForHegemony(&hidden), "hidden placeholder rejected")
        && expect(!convertedLord.canPairForHegemony(&deputy), "lord conversion remains outside opening choice");
}

static bool hegemonyCommitsOnePairPerSeat(bool freeChoose)
{
    const bool hegemony = Config.EnableHegemony;
    const bool second = Config.Enable2ndGeneral;
    const bool dedup = Config.GeneralVersionDedup;
    const bool oldFreeChoose = Config.FreeChoose;
    const ServerInfoStruct serverInfo = ServerInfo;
    const QVariantMap overrides = Config.valueOverrides();
    auto restore = qScopeGuard([=]() {
        Config.EnableHegemony = hegemony;
        Config.Enable2ndGeneral = second;
        Config.GeneralVersionDedup = dedup;
        Config.FreeChoose = oldFreeChoose;
        Config.setValueOverrides(overrides);
        ServerInfo = serverInfo;
    });
    Config.EnableHegemony = true;
    Config.Enable2ndGeneral = true;
    Config.GeneralVersionDedup = false;
    Config.FreeChoose = freeChoose;
    ServerInfo.EnableHegemony = true;
    ServerInfo.FreeChoose = freeChoose;
    ServerInfo.GameMode = QStringLiteral("04p");
    ServerInfo.BanPackages.clear();
    QVariantMap values = overrides;
    values.insert(QStringLiteral("Banlist/Hegemony"), QStringList());
    values.insert(QStringLiteral("Banlist/Roles"), QStringList());
    values.insert(QStringLiteral("HegemonyMaxChoice"), 7);
    Config.setValueOverrides(values);
    Room room(nullptr, QStringLiteral("04p"));
    EngineRuntimeContextScope engineScope(*Sanguosha, &room);
    GameSessionController controller(room);
    QList<ServerPlayer *> players;
    for (int seat = 0; seat < 2; ++seat)
        players << RoomTestAccess::addPlayer(room, QStringLiteral("pair-seat-%1").arg(seat), QStringLiteral("robot"));
    QList<QSanProtocol::ChooseGeneralRequestPayload> requests(2);
    QList<int> requestCounts{0, 0};
    QList<QMetaObject::Connection> connections;
    for (int seat = 0; seat < players.size(); ++seat) {
        connections << QObject::connect(players.at(seat), &ServerPlayer::message_ready, &room,
            [&, seat](const QByteArray &frame) {
                QSanProtocol::ProtocolMessage message;
                if (!QSanProtocol::ProtocolCodecRouter().decode(frame, &message).success
                    || message.type != QSanProtocol::ProtocolMessageType::Request
                    || message.command != QSanProtocol::S_COMMAND_CHOOSE_GENERAL) return;
                ++requestCounts[seat];
                QSanProtocol::ChooseGeneralRequestPayload::parseV2(message.payload, &requests[seat]);
                // Simulate a partial answer, and a registered but out-of-pool pair.
                const QString answer = seat == 0 ? requests[seat].candidates.value(0)
                    : requests[0].hegemonyPairs.value(0);
                players.at(seat)->setClientReply(answer);
                players.at(seat)->m_isClientResponseReady = true;
            });
    }
    auto disconnect = qScopeGuard([&]() {
        for (const auto &connection : connections) QObject::disconnect(connection);
    });
    controller.chooseGenerals();
    if (!expect(!controller.isTerminal(), "hegemony pair preparation succeeds")) return false;
    QSet<QString> reserved;
    QSet<QString> chosen;
    for (int seat = 0; seat < players.size(); ++seat) {
        ServerPlayer *player = players.at(seat);
        const auto &request = requests.at(seat);
        if (!expect(requestCounts.at(seat) == 1 && !request.hegemonyPairs.isEmpty(),
                    "exactly one pair request per seat")) return false;
        for (const QString &name : request.candidates) {
            if (!expect(!reserved.contains(name), "candidate pools are disjoint")) return false;
            reserved.insert(name);
        }
        const QString pair = player->getActualGeneral1Name() + "+" + player->getActualGeneral2Name();
        const QString expected = freeChoose && seat == 1 ? requests[0].hegemonyPairs.first() : request.hegemonyPairs.first();
        if (!expect(pair == expected, "FreeChoose accepts out-of-pool pairs; invalid/restricted replies use fallback")
            || !expect(player->getGeneralName() == "anjiang" && player->getGeneral2Name() == "anjiang",
                       "server public identities remain concealed")
            || !expect(!player->hasShownOneGeneral() && !player->hasShownRole(), "pair and role stay hidden")
            || !expect(player->getActualGeneral1()->compareKingdomsWith(player->getActualGeneral2())
                           .contains(player->getHegemonyKingdom()), "initial kingdom belongs to both generals"))
            return false;
        for (const QString &name : pair.split('+')) {
            if (!expect(freeChoose || !chosen.contains(name), "restricted-mode committed generals are unique")) return false;
            chosen.insert(name);
        }
    }
    return true;
}

static bool legalLifecycleReachesPlayingOnlyAfterGameReady()
{
    Room room(nullptr, QStringLiteral("02_1v1"));
    GameSessionController controller(room);

    if (!expect(controller.state() == GameSessionController::State::Waiting,
                "initial state is Waiting")
        || !expect(!controller.hasGameStarted(), "waiting is not started")
        || !expect(controller.requestStart(), "ready request enters Preparing")
        || !expect(controller.state() == GameSessionController::State::Preparing,
                   "state is Preparing")
        || !expect(!controller.requestStart(), "duplicate ready request is rejected")
        || !expect(GameSessionControllerTestAccess::transition(
                       controller, GameSessionController::State::Preparing),
                   "same-state transition is a no-op")
        || !expect(!GameSessionControllerTestAccess::transition(
                       controller, GameSessionController::State::Playing),
                   "Preparing cannot skip Initializing")
        || !expect(controller.state() == GameSessionController::State::Preparing,
                   "invalid transition keeps the current state")
        || !expect(GameSessionControllerTestAccess::transition(
                       controller, GameSessionController::State::Initializing),
                   "Preparing enters Initializing")
        || !expect(controller.hasGameStarted(), "Initializing keeps legacy started semantics")
        || !expect(!controller.isPlaying(), "Initializing is not Playing"))
        return false;

    controller.markGameReadyCompleted();
    return expect(controller.state() == GameSessionController::State::Playing,
                  "GameReady completion enters Playing")
        && expect(controller.isPlaying(), "Playing query becomes true");
}

static bool terminalStatesAreStickyAndKeepTheirCause()
{
    Room abortedRoom(nullptr, QStringLiteral("02_1v1"));
    GameSessionController aborted(abortedRoom);
    aborted.abort(GameSessionController::TerminationCause::Disconnected);
    if (!expect(aborted.state() == GameSessionController::State::Aborted,
                "abort enters Aborted")
        || !expect(aborted.terminationCause()
                       == GameSessionController::TerminationCause::Disconnected,
                   "abort records its cause")
        || !expect(aborted.isTerminal(), "Aborted is terminal")
        || !expect(!GameSessionControllerTestAccess::transition(
                       aborted, GameSessionController::State::Waiting),
                   "terminal state rejects later transitions"))
        return false;

    aborted.abort(GameSessionController::TerminationCause::Shutdown);
    if (!expect(aborted.terminationCause()
                    == GameSessionController::TerminationCause::Disconnected,
                "later abort does not overwrite the original cause"))
        return false;

    Room finishedRoom(nullptr, QStringLiteral("02_1v1"));
    GameSessionController finished(finishedRoom);
    GameSessionControllerTestAccess::finish(finished);
    return expect(finished.state() == GameSessionController::State::Finished,
                  "normal completion enters Finished")
        && expect(finished.terminationCause()
                      == GameSessionController::TerminationCause::GameOver,
                  "normal completion records GameOver")
        && expect(finished.isTerminal(), "Finished is terminal");
}

} // namespace

int runGameSessionControllerTests()
{
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "engine initialization failed:" << error;
        return 1;
    }

    return legalLifecycleReachesPlayingOnlyAfterGameReady()
            && terminalStatesAreStickyAndKeepTheirCause()
            && hegemonySeatAssignment()
            && hegemonyFreeChoiceKeepsPairRules()
            && hegemonyCommitsOnePairPerSeat(false)
            && hegemonyCommitsOnePairPerSeat(true)
        ? 0 : 2;
}
