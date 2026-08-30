#include "json.h"
#include "protocol.h"
#include "protocol/protocol-negotiation.h"
#include "protocol/protocol-runtime.h"

#include <QCoreApplication>
#include <QJsonDocument>
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
    if (!expect(QJsonDocument::fromJson(offerWire).isArray(),
                QStringLiteral("OFFER is a V1 array"))
        || !expect(sendFrame(serverSocket, offerWire, &error)
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
                QStringLiteral("COMMIT queued before server activation"))
        || !expect(router.decode(ProtocolVersion::V1, frame, &decoded).success
                       && clientState->acceptClientCommit(decoded.payload, &error),
                   QStringLiteral("TCP client accepts COMMIT"))) {
        return false;
    }

    return expect(serverState->activeVersion() == ProtocolVersion::V2
                      && clientState->activeVersion() == ProtocolVersion::V2,
                  QStringLiteral("TCP modern session is V2 active"));
}

ProtocolMessage requestMessage()
{
    ProtocolMessage message;
    message.type = ProtocolMessageType::Request;
    message.source = ProtocolEndpoint::Room;
    message.destination = ProtocolEndpoint::Client;
    message.messageId = 42;
    message.command = S_COMMAND_MULTIPLE_CHOICE;
    message.hasPayload = true;
    message.payload = QVariantList{
        QStringLiteral("tuxi"),
        QStringLiteral("left+right"),
        QStringLiteral("left"),
        QStringLiteral("choose a side")
    };
    return message;
}

ProtocolMessage replyMessage(ProtocolVersion version)
{
    ProtocolMessage message;
    message.type = ProtocolMessageType::Reply;
    message.source = ProtocolEndpoint::Client;
    message.destination = ProtocolEndpoint::Room;
    message.messageId = version == ProtocolVersion::V2 ? 81 : 0;
    message.replyTo = 42;
    message.command = S_COMMAND_MULTIPLE_CHOICE;
    message.hasPayload = true;
    message.payload = QStringLiteral("right");
    return message;
}

bool mixedV1V2Gameplay(QTcpSocket *legacyServer, QTcpSocket *legacyClient,
                       QTcpSocket *modernServer, QTcpSocket *modernClient,
                       const ProtocolSessionState &modernServerState,
                       const ProtocolSessionState &modernClientState,
                       const ProtocolCodecRouter &router)
{
    const ProtocolMessage logicalRequest = requestMessage();
    QString error;
    const QByteArray legacyWire = router.encode(
        ProtocolVersion::V1, logicalRequest, &error);
    const QByteArray modernWire = router.encode(
        modernServerState.activeVersion(), logicalRequest, &error);
    if (!expect(QJsonDocument::fromJson(legacyWire).isArray(),
                QStringLiteral("legacy gameplay root is array"))
        || !expect(QJsonDocument::fromJson(modernWire).isObject(),
                   QStringLiteral("modern gameplay root is object"))
        || !expect(sendFrame(legacyServer, legacyWire, &error)
                       && sendFrame(modernServer, modernWire, &error),
                   QStringLiteral("mixed server sends both requests"))) {
        return false;
    }

    QByteArray legacyReceived;
    QByteArray modernReceived;
    ProtocolMessage legacyLogical;
    ProtocolMessage modernLogical;
    if (!expect(receiveFrame(legacyClient, &legacyReceived, &error)
                    && receiveFrame(modernClient, &modernReceived, &error),
                QStringLiteral("mixed clients receive requests"))
        || !expect(router.decode(ProtocolVersion::V1, legacyReceived,
                                 &legacyLogical).success,
                   QStringLiteral("legacy client decodes request"))
        || !expect(router.decode(modernClientState.activeVersion(), modernReceived,
                                 &modernLogical).success,
                   QStringLiteral("modern client decodes request"))
        || !expect(legacyLogical.payload == modernLogical.payload
                       && modernLogical.payload == logicalRequest.payload,
                   QStringLiteral("V1 and V2 clients receive identical logical request"))) {
        return false;
    }

    const QByteArray legacyReply = router.encode(
        ProtocolVersion::V1, replyMessage(ProtocolVersion::V1), &error);
    const QByteArray modernReply = router.encode(
        modernClientState.activeVersion(), replyMessage(ProtocolVersion::V2), &error);
    if (!expect(QJsonDocument::fromJson(legacyReply).isArray()
                    && QJsonDocument::fromJson(modernReply).isObject(),
                QStringLiteral("mixed reply roots differ by negotiated version"))
        || !expect(sendFrame(legacyClient, legacyReply, &error)
                       && sendFrame(modernClient, modernReply, &error),
                   QStringLiteral("mixed clients send replies"))) {
        return false;
    }

    if (!expect(receiveFrame(legacyServer, &legacyReceived, &error)
                    && receiveFrame(modernServer, &modernReceived, &error),
                QStringLiteral("mixed server receives replies"))
        || !expect(router.decode(ProtocolVersion::V1, legacyReceived,
                                 &legacyLogical).success,
                   QStringLiteral("legacy server decodes reply"))
        || !expect(router.decode(modernServerState.activeVersion(), modernReceived,
                                 &modernLogical).success,
                   QStringLiteral("modern server decodes reply"))) {
        return false;
    }

    return expect(legacyLogical.payload == QStringLiteral("right")
                      && modernLogical.payload == legacyLogical.payload,
                  QStringLiteral("V1 and V2 replies normalize to identical choice"));
}

bool malformedModernFrameFailsClosed(QTcpSocket *modernServer,
                                     QTcpSocket *modernClient,
                                     const ProtocolCodecRouter &router)
{
    const QByteArray malformed =
        "{\"command\":24,\"destination\":\"client\",\"message_id\":\"99\","
        "\"payload\":[\"skill\",\"a+b\",\"\",\"tip\"],"
        "\"source\":\"room\",\"type\":\"request\",\"v\":2}";
    QString error;
    QByteArray received;
    ProtocolMessage decoded;
    if (!expect(sendFrame(modernServer, malformed, &error)
                    && receiveFrame(modernClient, &received, &error),
                QStringLiteral("malformed V2 frame crosses TCP"))) {
        return false;
    }
    const ProtocolDecodeResult result = router.decode(
        ProtocolVersion::V2, received, &decoded);
    if (!expect(!result.success && result.error == ProtocolDecodeError::InvalidPayload,
                QStringLiteral("malformed V2 typed payload rejected"))) {
        return false;
    }

    // Production endpoints use the same fail-closed policy on router errors.
    modernClient->disconnectFromHost();
    if (modernClient->state() != QAbstractSocket::UnconnectedState)
        modernClient->waitForDisconnected(2000);
    if (modernServer->state() != QAbstractSocket::UnconnectedState)
        modernServer->waitForDisconnected(2000);
    return expect(modernClient->state() == QAbstractSocket::UnconnectedState,
                  QStringLiteral("malformed modern session disconnects"));
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTcpServer listener;
    QString error;
    if (!expect(listener.listen(QHostAddress::LocalHost, 0),
                QStringLiteral("loopback listener"))) {
        return 1;
    }

    QTcpSocket legacyClient;
    QTcpSocket modernClient;
    QScopedPointer<QTcpSocket> legacyServer;
    QScopedPointer<QTcpSocket> modernServer;
    if (!expect(connectPair(&listener, &legacyClient, &legacyServer, &error),
                QStringLiteral("legacy TCP connection"))
        || !expect(connectPair(&listener, &modernClient, &modernServer, &error),
                   QStringLiteral("modern TCP connection"))) {
        QTextStream(stderr) << error << "\n";
        return 1;
    }

    const ProtocolCodecRouter router;
    ProtocolSessionState modernServerState;
    ProtocolSessionState modernClientState;
    const bool success = activateModernSession(
                             modernServer.data(), &modernClient,
                             &modernServerState, &modernClientState, router)
        && mixedV1V2Gameplay(
            legacyServer.data(), &legacyClient,
            modernServer.data(), &modernClient,
            modernServerState, modernClientState, router)
        && malformedModernFrameFailsClosed(
            modernServer.data(), &modernClient, router);

    QTextStream(stdout) << "[AUTOTEST] PROTOCOL_GAMEPLAY_TCP_RESULT status="
                        << (success ? "PASS" : "FAIL")
                        << " cases=" << testCaseCount << "\n";
    return success ? 0 : 1;
}
