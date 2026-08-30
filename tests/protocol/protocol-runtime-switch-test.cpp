#include "json.h"
#include "protocol.h"
#include "protocol/protocol-negotiation.h"
#include "protocol/protocol-runtime.h"
#include "protocol/protocol-v1-codec.h"
#include "protocol/protocol-v1-message-adapter.h"

#include <QTextStream>

#include <limits>

using namespace QSanProtocol;

namespace
{
bool expect(bool condition, const QString &label)
{
    if (condition)
        return true;
    QTextStream(stderr) << label << " failed\n";
    return false;
}

ProtocolMessage controlMessage(const QVariantMap &payload)
{
    Packet packet(S_SRC_ROOM | S_TYPE_NOTIFICATION | S_DEST_CLIENT,
                  S_COMMAND_PROTOCOL_SWITCH);
    packet.setMessageBody(payload);
    return protocolMessageFromV1Packet(packet);
}

QVariantMap switchPayload(const QString &phase, const QString &switchId,
                          int targetVersion = 2)
{
    QVariantMap payload;
    payload.insert(QStringLiteral("schema_version"), 1);
    payload.insert(QStringLiteral("phase"), phase);
    payload.insert(QStringLiteral("target_version"), targetVersion);
    payload.insert(QStringLiteral("switch_id"), switchId);
    return payload;
}

bool activationBarrier()
{
    ProtocolSessionState server;
    ProtocolSessionState client;
    server.setPeerCapabilities(ProtocolNegotiation::localCapabilities());
    client.setPeerCapabilities(ProtocolNegotiation::localCapabilities());

    QVariantMap offer;
    QVariantMap ack;
    QVariantMap commit;
    QString error;
    if (!expect(server.beginServerSwitch(&offer, &error), QStringLiteral("server offer"))
        || !expect(server.activeVersion() == ProtocolVersion::V1,
                   QStringLiteral("server remains V1 after offer"))
        || !expect(client.acceptClientOffer(offer, &ack, &error),
                   QStringLiteral("client ack"))
        || !expect(client.activeVersion() == ProtocolVersion::V1,
                   QStringLiteral("client remains V1 after ack"))
        || !expect(server.acceptServerAck(ack, &commit, &error),
                   QStringLiteral("server commit"))
        || !expect(server.activeVersion() == ProtocolVersion::V1,
                   QStringLiteral("server remains V1 before commit queue"))
        || !expect(server.activateServerAfterCommit(&error),
                   QStringLiteral("server activates after commit queue"))
        || !expect(client.acceptClientCommit(commit, &error),
                   QStringLiteral("client accepts commit"))) {
        return false;
    }
    return expect(server.activeVersion() == ProtocolVersion::V2
                      && client.activeVersion() == ProtocolVersion::V2,
                  QStringLiteral("both sides active V2"));
}

bool invalidTransitionsFailClosed()
{
    ProtocolSessionState legacy;
    QVariantMap payload;
    QString error;
    if (!expect(!legacy.beginServerSwitch(&payload, &error) && !error.isEmpty(),
                QStringLiteral("legacy cannot offer"))) {
        return false;
    }

    ProtocolSessionState server;
    ProtocolSessionState client;
    server.setPeerCapabilities(ProtocolNegotiation::localCapabilities());
    client.setPeerCapabilities(ProtocolNegotiation::localCapabilities());
    QVariantMap offer;
    QVariantMap ack;
    QVariantMap commit;
    if (!expect(!server.acceptServerAck(switchPayload(QStringLiteral("ack"),
                                                        QStringLiteral("1")),
                                        &commit, &error),
                QStringLiteral("ack before offer rejected"))
        || !expect(!client.acceptClientCommit(switchPayload(QStringLiteral("commit"),
                                                             QStringLiteral("1")),
                                              &error),
                   QStringLiteral("commit before offer rejected"))
        || !expect(server.beginServerSwitch(&offer, &error),
                   QStringLiteral("valid offer created"))
        || !expect(!server.beginServerSwitch(&offer, &error),
                   QStringLiteral("duplicate offer rejected"))
        || !expect(client.acceptClientOffer(offer, &ack, &error),
                   QStringLiteral("valid offer accepted"))
        || !expect(!client.acceptClientOffer(offer, &ack, &error),
                   QStringLiteral("duplicate received offer rejected"))) {
        return false;
    }

    QVariantMap wrongId = ack;
    wrongId.insert(QStringLiteral("switch_id"), QStringLiteral("2"));
    QVariantMap wrongTarget = ack;
    wrongTarget.insert(QStringLiteral("target_version"), 3);
    QVariantMap malformed = ack;
    malformed.remove(QStringLiteral("schema_version"));
    if (!expect(!server.acceptServerAck(wrongId, &commit, &error),
                QStringLiteral("mismatched switch id rejected"))
        || !expect(!server.acceptServerAck(wrongTarget, &commit, &error),
                   QStringLiteral("wrong target rejected"))
        || !expect(!server.acceptServerAck(malformed, &commit, &error),
                   QStringLiteral("malformed payload rejected"))
        || !expect(!server.activateServerAfterCommit(&error),
                   QStringLiteral("activation without valid ack rejected"))
        || !expect(server.acceptServerAck(ack, &commit, &error),
                   QStringLiteral("valid ack accepted"))
        || !expect(!server.acceptServerAck(ack, &commit, &error),
                   QStringLiteral("duplicate ack rejected"))
        || !expect(server.activateServerAfterCommit(&error),
                   QStringLiteral("valid server activation"))
        || !expect(client.acceptClientCommit(commit, &error),
                   QStringLiteral("valid commit accepted"))) {
        return false;
    }
    return expect(!client.acceptClientCommit(commit, &error),
                  QStringLiteral("duplicate commit rejected"));
}

bool codecRoutingAndIds()
{
    ProtocolCodecRouter router;
    ProtocolMessage message;
    message.type = ProtocolMessageType::Notification;
    message.source = ProtocolEndpoint::Room;
    message.destination = ProtocolEndpoint::Client;
    message.command = 77;
    message.messageId = 1;
    message.hasPayload = true;
    message.payload = QStringLiteral("中文 😀 café");

    QString error;
    const QByteArray v2 = router.encode(ProtocolVersion::V2, message, &error);
    ProtocolMessage decoded;
    const ProtocolDecodeResult correct = router.decode(ProtocolVersion::V2, v2, &decoded);
    const ProtocolDecodeResult wrong = router.decode(ProtocolVersion::V1, v2, &decoded);
    if (!expect(!v2.isEmpty() && correct.success && decoded.payload == message.payload,
                QStringLiteral("V2 UTF-8 route"))
        || !expect(!wrong.success, QStringLiteral("router does not auto-detect V2 as V1"))) {
        return false;
    }

    message.messageId = 0;
    const QByteArray v1 = router.encode(ProtocolVersion::V1, message, &error);
    ProtocolMessage v1Message = message;
    v1Message.version = ProtocolVersion::V1;
    const QByteArray legacyV1 = ProtocolV1Codec().encode(v1Message, &error);
    if (!expect(v1 == legacyV1, QStringLiteral("V1 router bytes unchanged"))
        || !expect(!router.decode(ProtocolVersion::V2, v1, &decoded).success,
                QStringLiteral("router does not auto-detect V1 as V2"))) {
        return false;
    }

    ProtocolMessage request = message;
    request.type = ProtocolMessageType::Request;
    request.messageId = quint64(std::numeric_limits<unsigned int>::max()) + 42;
    request.replyTo = 0;
    const QByteArray requestBytes = router.encode(ProtocolVersion::V2, request, &error);
    ProtocolMessage reply = message;
    reply.type = ProtocolMessageType::Reply;
    reply.messageId = 9;
    reply.replyTo = request.messageId;
    const QByteArray replyBytes = router.encode(ProtocolVersion::V2, reply, &error);
    if (!expect(!requestBytes.isEmpty() && !replyBytes.isEmpty()
                    && router.decode(ProtocolVersion::V2, replyBytes, &decoded).success
                    && decoded.replyTo == request.messageId,
                QStringLiteral("full-width V2 request reply correlation"))) {
        return false;
    }

    const QByteArray replay = router.encodeReplayV1(request, &error);
    if (!expect(router.decode(ProtocolVersion::V1, replay, &decoded).success
                    && decoded.version == ProtocolVersion::V1
                    && decoded.messageId == 0,
                QStringLiteral("V2 replay normalized to V1 logical packet"))) {
        return false;
    }

    ProtocolMessageIdGenerator first;
    ProtocolMessageIdGenerator second;
    return expect(first.next() == 1 && first.next() == 2 && second.next() == 1,
                  QStringLiteral("per-connection message ids"));
}

bool framingContract()
{
    ProtocolFrameBuffer frames;
    const QList<qsizetype> acceptedSizes = {1, 15999, 16000, 16001, 32768, 65535};
    for (qsizetype size : acceptedSizes) {
        const ProtocolFrameAppendResult accepted = frames.append(QByteArray(size, 'x') + '\n');
        if (!expect(accepted.success && accepted.frames.size() == 1
                        && accepted.frames.first().size() == size,
                    QStringLiteral("accepted frame size %1").arg(size))) {
            return false;
        }
    }

    ProtocolFrameAppendResult result = frames.append(
        QByteArray(ProtocolFrameBuffer::MaxFrameSize, 'x') + "\r\n");
    if (!expect(result.success && result.frames.size() == 1
                    && result.frames.first().size() == ProtocolFrameBuffer::MaxFrameSize,
                QStringLiteral("maximum CRLF frame accepted"))) {
        return false;
    }

    result = frames.append(
        QByteArray(ProtocolFrameBuffer::MaxFrameSize + 1, 'x'));
    if (!expect(!result.success, QStringLiteral("oversize unterminated frame rejected")))
        return false;

    result = frames.append(QByteArray(ProtocolFrameBuffer::MaxFrameSize + 1, 'x') + '\n');
    if (!expect(!result.success, QStringLiteral("oversize delimited frame rejected")))
        return false;

    result = frames.append(QByteArray("one\ntwo\n"));
    if (!expect(result.success && result.frames == QList<QByteArray>({"one", "two"}),
                QStringLiteral("multiple frames in one burst"))) {
        return false;
    }

    result = frames.append(QByteArray("part"));
    if (!expect(result.success && result.frames.isEmpty() && frames.bufferedSize() == 4,
                QStringLiteral("partial frame buffered"))) {
        return false;
    }
    result = frames.append(QStringLiteral("ial 中文 😀\r\n").toUtf8());
    return expect(result.success && result.frames.size() == 1
                      && QString::fromUtf8(result.frames.first())
                          == QStringLiteral("partial 中文 😀"),
                  QStringLiteral("partial CRLF UTF-8 frame"));
}

bool commitAndV2InSameBurst()
{
    ProtocolSessionState server;
    ProtocolSessionState client;
    server.setPeerCapabilities(ProtocolNegotiation::localCapabilities());
    client.setPeerCapabilities(ProtocolNegotiation::localCapabilities());
    QVariantMap offer;
    QVariantMap ack;
    QVariantMap commit;
    QString error;
    server.beginServerSwitch(&offer, &error);
    client.acceptClientOffer(offer, &ack, &error);
    server.acceptServerAck(ack, &commit, &error);
    server.activateServerAfterCommit(&error);

    ProtocolCodecRouter router;
    const QByteArray commitBytes = router.encode(
        ProtocolVersion::V1, controlMessage(commit), &error);
    ProtocolMessage next;
    next.type = ProtocolMessageType::Notification;
    next.source = ProtocolEndpoint::Room;
    next.destination = ProtocolEndpoint::Client;
    next.messageId = 1;
    next.command = S_COMMAND_SPEAK;
    next.hasPayload = true;
    next.payload = QStringLiteral("切換完成 😀");
    const QByteArray v2Bytes = router.encode(ProtocolVersion::V2, next, &error);

    ProtocolFrameBuffer frameBuffer;
    const ProtocolFrameAppendResult burst = frameBuffer.append(
        commitBytes + '\n' + v2Bytes + '\n');
    ProtocolMessage decoded;
    if (!expect(burst.success && burst.frames.size() == 2,
                QStringLiteral("commit plus V2 burst framing"))
        || !expect(router.decode(client.activeVersion(), burst.frames.at(0), &decoded).success,
                   QStringLiteral("commit decoded as V1"))
        || !expect(client.acceptClientCommit(decoded.payload, &error),
                   QStringLiteral("commit changes active codec"))
        || !expect(router.decode(client.activeVersion(), burst.frames.at(1), &decoded).success,
                   QStringLiteral("next burst frame decoded as V2"))) {
        return false;
    }
    return expect(decoded.payload == next.payload,
                  QStringLiteral("same-burst V2 Unicode payload"));
}
}

int main()
{
    return activationBarrier()
            && invalidTransitionsFailClosed()
            && codecRoutingAndIds()
            && framingContract()
            && commitAndV2InSameBurst()
        ? 0
        : 1;
}
