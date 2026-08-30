#include "engine-bootstrap.h"
#include "json.h"
#include "protocol.h"
#include "protocol/protocol-negotiation.h"
#include "protocol/protocol-runtime.h"
#include "room-test-access.h"
#include "room.h"
#include "serverplayer.h"
#include "settings.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QMetaObject>
#include <QScopedPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextStream>

using namespace QSanProtocol;

namespace
{
int testCaseCount = 0;

bool expect(bool condition, const QString &label)
{
    ++testCaseCount;
    if (condition)
        return true;
    QTextStream(stderr) << label << " failed\n";
    return false;
}

class ScopedOperationLimit
{
public:
    ScopedOperationLimit()
        : m_previous(Config.OperationNoLimit)
    {
        Config.OperationNoLimit = false;
    }

    ~ScopedOperationLimit()
    {
        Config.OperationNoLimit = m_previous;
    }

private:
    bool m_previous;
};

bool sendFrame(QTcpSocket *socket, const QByteArray &frame, QString *error)
{
    if (socket == nullptr || frame.isEmpty()) {
        *error = QStringLiteral("cannot send an empty frame");
        return false;
    }
    if (socket->write(frame + '\n') < 0 || !socket->waitForBytesWritten(2000)) {
        *error = socket->errorString();
        return false;
    }
    return true;
}

bool receiveFrame(QTcpSocket *socket, QByteArray *frame, QString *error)
{
    if (socket == nullptr || frame == nullptr) {
        *error = QStringLiteral("frame receiver is null");
        return false;
    }
    if (!socket->canReadLine() && !socket->waitForReadyRead(2000)) {
        *error = socket->errorString();
        return false;
    }
    QByteArray received = socket->readLine();
    if (!received.endsWith('\n')) {
        *error = QStringLiteral("received frame has no newline delimiter");
        return false;
    }
    received.chop(1);
    if (received.endsWith('\r'))
        received.chop(1);
    *frame = received;
    return true;
}

bool connectPair(QTcpServer *listener, QTcpSocket *client,
                 QScopedPointer<QTcpSocket> *serverSocket, QString *error)
{
    client->connectToHost(QHostAddress::LocalHost, listener->serverPort());
    if (!client->waitForConnected(2000)) {
        *error = client->errorString();
        return false;
    }
    if (!listener->hasPendingConnections()
        && !listener->waitForNewConnection(2000)) {
        *error = listener->errorString();
        return false;
    }
    serverSocket->reset(listener->nextPendingConnection());
    if (serverSocket->isNull()) {
        *error = QStringLiteral("listener returned no pending socket");
        return false;
    }
    return true;
}

ProtocolMessage controlMessage(ProtocolEndpoint source,
                               ProtocolEndpoint destination,
                               const QVariantMap &payload)
{
    ProtocolMessage message;
    message.type = ProtocolMessageType::Notification;
    message.source = source;
    message.destination = destination;
    message.command = S_COMMAND_PROTOCOL_SWITCH;
    message.hasPayload = true;
    message.payload = payload;
    return message;
}

bool activateModernSession(QTcpSocket *serverSocket, QTcpSocket *clientSocket,
                           ProtocolSessionState *serverState,
                           ProtocolSessionState *clientState,
                           const ProtocolCodecRouter &router)
{
    serverState->setPeerCapabilities(ProtocolNegotiation::localCapabilities());
    clientState->setPeerCapabilities(ProtocolNegotiation::localCapabilities());

    QVariantMap offer;
    QVariantMap ack;
    QVariantMap commit;
    QString error;
    QByteArray frame;
    ProtocolMessage decoded;
    if (!expect(serverState->beginServerSwitch(&offer, &error),
                QStringLiteral("TCP server creates OFFER"))) {
        return false;
    }

    const QByteArray offerWire = router.encode(
        ProtocolVersion::V1,
        controlMessage(ProtocolEndpoint::Room, ProtocolEndpoint::Client, offer),
        &error);
    if (!expect(sendFrame(serverSocket, offerWire, &error)
                    && receiveFrame(clientSocket, &frame, &error),
                QStringLiteral("OFFER crosses TCP"))
        || !expect(router.decode(ProtocolVersion::V1, frame, &decoded).success
                       && clientState->acceptClientOffer(decoded.payload, &ack, &error),
                   QStringLiteral("TCP client accepts OFFER"))) {
        return false;
    }

    const QByteArray ackWire = router.encode(
        ProtocolVersion::V1,
        controlMessage(ProtocolEndpoint::Client, ProtocolEndpoint::Room, ack),
        &error);
    if (!expect(sendFrame(clientSocket, ackWire, &error)
                    && receiveFrame(serverSocket, &frame, &error),
                QStringLiteral("ACK crosses TCP"))
        || !expect(router.decode(ProtocolVersion::V1, frame, &decoded).success
                       && serverState->acceptServerAck(decoded.payload, &commit, &error),
                   QStringLiteral("TCP server accepts ACK"))) {
        return false;
    }

    const QByteArray commitWire = router.encode(
        ProtocolVersion::V1,
        controlMessage(ProtocolEndpoint::Room, ProtocolEndpoint::Client, commit),
        &error);
    if (!expect(sendFrame(serverSocket, commitWire, &error)
                    && serverState->activateServerAfterCommit(&error)
                    && receiveFrame(clientSocket, &frame, &error),
                QStringLiteral("COMMIT crosses TCP before activation"))
        || !expect(router.decode(ProtocolVersion::V1, frame, &decoded).success
                       && clientState->acceptClientCommit(decoded.payload, &error),
                   QStringLiteral("TCP client accepts COMMIT"))) {
        return false;
    }

    return expect(serverState->activeVersion() == ProtocolVersion::V2
                      && clientState->activeVersion() == ProtocolVersion::V2,
                  QStringLiteral("TCP session reaches active V2"));
}

struct TcpConnection
{
    QTcpSocket client;
    QScopedPointer<QTcpSocket> server;
    ProtocolSessionState serverState;
    ProtocolSessionState clientState;
    ProtocolVersion version = ProtocolVersion::V1;
};

bool initializeConnection(QTcpServer *listener, ProtocolVersion version,
                          TcpConnection *connection,
                          const ProtocolCodecRouter &router, QString *error)
{
    connection->version = version;
    if (!connectPair(listener, &connection->client, &connection->server, error))
        return false;
    if (version == ProtocolVersion::V2
        && !activateModernSession(connection->server.data(), &connection->client,
                                  &connection->serverState, &connection->clientState,
                                  router)) {
        return false;
    }
    return true;
}

void attachProductionBoundary(Room &room, ServerPlayer *player,
                              TcpConnection *connection,
                              QString *transportError)
{
    if (connection->version == ProtocolVersion::V2)
        player->setProtocolSessionState(connection->serverState);

    QObject::connect(player, &ServerPlayer::message_ready, player,
                     [connection, transportError](const QByteArray &message) {
        if (!sendFrame(connection->server.data(), message, transportError))
            return;
    });
    QObject::connect(player, &ServerPlayer::request_got, &room,
                     [&room, player](const QString &raw,
                                     const ProtocolMessage &message) {
        RoomTestAccess::dispatch(room, player, message, raw);
    });
}

bool roundTripRequest(Room &room, ServerPlayer *player,
                      TcpConnection *connection, CommandType command,
                      const QVariant &requestPayload, bool requestHasPayload,
                      const QVariant &replyPayload, const QString &label,
                      const ProtocolCodecRouter &router)
{
    QString error;
    const QVariant argument = requestHasPayload ? requestPayload : QVariant();
    if (!expect(room.doRequest(player, command, argument, 0, false),
                label + QStringLiteral(" RequestCoordinator starts"))) {
        return false;
    }

    QByteArray requestWire;
    ProtocolMessage logicalRequest;
    if (!expect(receiveFrame(&connection->client, &requestWire, &error),
                label + QStringLiteral(" request crosses TCP"))
        || !expect(router.decode(connection->version, requestWire,
                                 &logicalRequest).success,
                   label + QStringLiteral(" client decodes request"))
        || !expect(logicalRequest.command == command
                       && logicalRequest.hasPayload == requestHasPayload
                       && logicalRequest.payload == requestPayload,
                   label + QStringLiteral(" request normalizes logically"))) {
        return false;
    }

    const QJsonDocument requestDocument = QJsonDocument::fromJson(requestWire);
    if (!expect(connection->version == ProtocolVersion::V2
                    ? requestDocument.isObject()
                        && requestDocument.object().value(QStringLiteral("payload")).isObject()
                    : requestDocument.isArray(),
                label + QStringLiteral(" request uses negotiated wire shape"))) {
        return false;
    }

    ProtocolMessage reply;
    reply.type = ProtocolMessageType::Reply;
    reply.source = ProtocolEndpoint::Client;
    reply.destination = ProtocolEndpoint::Room;
    reply.messageId = connection->version == ProtocolVersion::V2 ? 900 : 0;
    reply.replyTo = logicalRequest.messageId;
    reply.command = command;
    reply.hasPayload = true;
    reply.payload = replyPayload;
    const QByteArray replyWire = router.encode(connection->version, reply, &error);
    if (!expect(!replyWire.isEmpty()
                    && sendFrame(&connection->client, replyWire, &error),
                label + QStringLiteral(" reply crosses TCP"))) {
        return false;
    }

    QByteArray receivedReply;
    if (!expect(receiveFrame(connection->server.data(), &receivedReply, &error),
                label + QStringLiteral(" server receives reply"))
        || !expect(QMetaObject::invokeMethod(
                       player, "getMessage", Qt::DirectConnection,
                       Q_ARG(QByteArray, receivedReply)),
                   label + QStringLiteral(" production ServerPlayer decodes reply"))
        || !expect(room.getResult(player, 0)
                       && player->getClientReply() == replyPayload,
                   label + QStringLiteral(" RequestCoordinator completes"))) {
        return false;
    }

    const QJsonDocument replyDocument = QJsonDocument::fromJson(replyWire);
    return expect(connection->version == ProtocolVersion::V2
                      ? replyDocument.isObject()
                          && replyDocument.object().value(QStringLiteral("payload")).isObject()
                      : replyDocument.isArray(),
                  label + QStringLiteral(" reply uses negotiated wire shape"));
}

bool simpleChoicesUseTrueTcp(QTcpServer *listener,
                             const ProtocolCodecRouter &router)
{
    Room modernRoom(nullptr, QStringLiteral("02_1v1"));
    ServerPlayer *modernPlayer = RoomTestAccess::addPlayer(
        modernRoom, QStringLiteral("modern-player"), QStringLiteral("online"));
    TcpConnection modern;
    QString error;
    QString transportError;
    if (!initializeConnection(listener, ProtocolVersion::V2, &modern, router, &error))
        return false;
    attachProductionBoundary(modernRoom, modernPlayer, &modern, &transportError);

    if (!roundTripRequest(modernRoom, modernPlayer, &modern,
                          S_COMMAND_CHOOSE_GENERAL,
                          QVariantList{QStringLiteral("caocao"), QStringLiteral("liubei")},
                          true, QStringLiteral("liubei"),
                          QStringLiteral("V2 choose general"), router)
        || !roundTripRequest(modernRoom, modernPlayer, &modern,
                             S_COMMAND_CHOOSE_SUIT, QVariant(), false,
                             QStringLiteral("diamond"),
                             QStringLiteral("V2 choose suit"), router)
        || !roundTripRequest(modernRoom, modernPlayer, &modern,
                             S_COMMAND_CHOOSE_KINGDOM,
                             QVariantList{QStringLiteral("wei+shu")}, true,
                             QStringLiteral("shu"),
                             QStringLiteral("V2 choose kingdom"), router)
        || !roundTripRequest(modernRoom, modernPlayer, &modern,
                             S_COMMAND_CHOOSE_ORDER,
                             static_cast<int>(S_REASON_CHOOSE_ORDER_SELECT), true,
                             static_cast<int>(S_CAMP_COOL),
                             QStringLiteral("V2 choose order"), router)
        || !roundTripRequest(modernRoom, modernPlayer, &modern,
                             S_COMMAND_INVOKE_SKILL,
                             QVariantList{QStringLiteral("技能"),
                                          QStringLiteral("playerdata:modern-player")},
                             true, false,
                             QStringLiteral("V2 invoke skill"), router)
        || !roundTripRequest(modernRoom, modernPlayer, &modern,
                             S_COMMAND_SURRENDER, QStringLiteral("曹操"), true, true,
                             QStringLiteral("V2 surrender vote"), router)) {
        return false;
    }
    if (!expect(transportError.isEmpty(),
                QStringLiteral("V2 transport emitted no error"))) {
        return false;
    }

    Room legacyRoom(nullptr, QStringLiteral("02_1v1"));
    ServerPlayer *legacyPlayer = RoomTestAccess::addPlayer(
        legacyRoom, QStringLiteral("legacy-player"), QStringLiteral("online"));
    TcpConnection legacy;
    transportError.clear();
    if (!initializeConnection(listener, ProtocolVersion::V1, &legacy, router, &error))
        return false;
    attachProductionBoundary(legacyRoom, legacyPlayer, &legacy, &transportError);
    return roundTripRequest(legacyRoom, legacyPlayer, &legacy,
                            S_COMMAND_CHOOSE_GENERAL,
                            QVariantList{QStringLiteral("caocao"), QStringLiteral("liubei")},
                            true, QStringLiteral("caocao"),
                            QStringLiteral("V1 choose general"), router)
        && roundTripRequest(legacyRoom, legacyPlayer, &legacy,
                            S_COMMAND_CHOOSE_SUIT, QVariant(), false,
                            QStringLiteral("heart"),
                            QStringLiteral("V1 choose suit"), router)
        && roundTripRequest(legacyRoom, legacyPlayer, &legacy,
                            S_COMMAND_INVOKE_SKILL,
                            QVariantList{QStringLiteral("test_skill"), QString()}, true,
                            true, QStringLiteral("V1 invoke skill"), router)
        && expect(transportError.isEmpty(),
                  QStringLiteral("V1 transport emitted no error"));
}

bool mixedVersionSurrenderVote(QTcpServer *listener,
                               const ProtocolCodecRouter &router)
{
    Room room(nullptr, QStringLiteral("02_1v1"));
    ServerPlayer *legacyPlayer = RoomTestAccess::addPlayer(
        room, QStringLiteral("legacy-voter"), QStringLiteral("online"));
    ServerPlayer *modernPlayer = RoomTestAccess::addPlayer(
        room, QStringLiteral("modern-voter"), QStringLiteral("online"));
    TcpConnection legacy;
    TcpConnection modern;
    QString error;
    QString legacyTransportError;
    QString modernTransportError;
    if (!initializeConnection(listener, ProtocolVersion::V1, &legacy, router, &error)
        || !initializeConnection(listener, ProtocolVersion::V2, &modern, router, &error)) {
        return false;
    }
    attachProductionBoundary(room, legacyPlayer, &legacy, &legacyTransportError);
    attachProductionBoundary(room, modernPlayer, &modern, &modernTransportError);

    const QString initiator = QStringLiteral("caocao");
    if (!expect(room.doRequest(legacyPlayer, S_COMMAND_SURRENDER,
                               initiator, 0, false)
                    && room.doRequest(modernPlayer, S_COMMAND_SURRENDER,
                                      initiator, 0, false),
                QStringLiteral("mixed surrender RequestCoordinator starts both voters"))) {
        return false;
    }

    QByteArray legacyRequestWire;
    QByteArray modernRequestWire;
    ProtocolMessage legacyRequest;
    ProtocolMessage modernRequest;
    if (!expect(receiveFrame(&legacy.client, &legacyRequestWire, &error)
                    && receiveFrame(&modern.client, &modernRequestWire, &error),
                QStringLiteral("mixed surrender requests cross TCP"))
        || !expect(QJsonDocument::fromJson(legacyRequestWire).isArray()
                       && QJsonDocument::fromJson(modernRequestWire).isObject(),
                   QStringLiteral("mixed surrender request wire shapes differ"))
        || !expect(router.decode(ProtocolVersion::V1, legacyRequestWire,
                                 &legacyRequest).success
                       && router.decode(ProtocolVersion::V2, modernRequestWire,
                                        &modernRequest).success
                       && legacyRequest.payload == initiator
                       && modernRequest.payload == initiator,
                   QStringLiteral("mixed surrender requests normalize equally"))) {
        return false;
    }

    auto sendVote = [&router, &error](TcpConnection *connection,
                                     const ProtocolMessage &request,
                                     bool vote, quint64 messageId) {
        ProtocolMessage reply;
        reply.type = ProtocolMessageType::Reply;
        reply.source = ProtocolEndpoint::Client;
        reply.destination = ProtocolEndpoint::Room;
        reply.messageId = messageId;
        reply.replyTo = request.messageId;
        reply.command = S_COMMAND_SURRENDER;
        reply.hasPayload = true;
        reply.payload = vote;
        const QByteArray wire = router.encode(connection->version, reply, &error);
        return sendFrame(&connection->client, wire, &error);
    };
    if (!expect(sendVote(&legacy, legacyRequest, true, 0)
                    && sendVote(&modern, modernRequest, false, 901),
                QStringLiteral("mixed surrender replies cross TCP"))) {
        return false;
    }

    QByteArray legacyReplyWire;
    QByteArray modernReplyWire;
    if (!expect(receiveFrame(legacy.server.data(), &legacyReplyWire, &error)
                    && receiveFrame(modern.server.data(), &modernReplyWire, &error),
                QStringLiteral("mixed surrender server receives replies"))
        || !expect(QMetaObject::invokeMethod(
                       legacyPlayer, "getMessage", Qt::DirectConnection,
                       Q_ARG(QByteArray, legacyReplyWire))
                       && QMetaObject::invokeMethod(
                           modernPlayer, "getMessage", Qt::DirectConnection,
                           Q_ARG(QByteArray, modernReplyWire)),
                   QStringLiteral("mixed surrender production decode"))
        || !expect(room.getResult(legacyPlayer, 0)
                       && room.getResult(modernPlayer, 0)
                       && legacyPlayer->getClientReply().toBool()
                       && !modernPlayer->getClientReply().toBool(),
                   QStringLiteral("mixed surrender votes collected logically"))) {
        return false;
    }

    return expect(legacyTransportError.isEmpty() && modernTransportError.isEmpty(),
                  QStringLiteral("mixed surrender transport emitted no error"));
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        QTextStream(stderr) << "engine initialization failed: " << error << "\n";
        return 1;
    }

    ScopedOperationLimit operationLimit;
    QTcpServer listener;
    if (!expect(listener.listen(QHostAddress::LocalHost, 0),
                QStringLiteral("loopback listener"))) {
        return 1;
    }

    const ProtocolCodecRouter router;
    const bool success = simpleChoicesUseTrueTcp(&listener, router)
        && mixedVersionSurrenderVote(&listener, router);
    QTextStream(stdout) << "[AUTOTEST] PROTOCOL_SIMPLE_CHOICE_TCP_RESULT status="
                        << (success ? "PASS" : "FAIL")
                        << " cases=" << testCaseCount << "\n";
    return success ? 0 : 1;
}
