#include "protocol/protocol-v2-codec.h"

#include <QTextStream>
#include <QVariantList>
#include <QVariantMap>

#include <limits>

using namespace QSanProtocol;

namespace
{
int caseCount = 0;

bool expect(bool condition, const QString &label)
{
    ++caseCount;
    if (condition)
        return true;
    QTextStream(stderr) << label << " failed\n";
    return false;
}

ProtocolMessage request()
{
    ProtocolMessage message;
    message.type = ProtocolMessageType::Request;
    message.source = ProtocolEndpoint::Room;
    message.destination = ProtocolEndpoint::Client;
    message.messageId = std::numeric_limits<quint64>::max();
    message.command = 37;
    message.hasPayload = true;
    message.payload = QVariantMap{
        {QStringLiteral("schema_version"), 2},
        {QStringLiteral("choice"), QStringLiteral("typed")}};
    return message;
}

bool same(const ProtocolMessage &left, const ProtocolMessage &right)
{
    return left.version == right.version
        && left.type == right.type
        && left.source == right.source
        && left.destination == right.destination
        && left.messageId == right.messageId
        && left.replyTo == right.replyTo
        && left.command == right.command
        && left.hasPayload == right.hasPayload
        && left.payload == right.payload;
}

bool validRoundTrip()
{
    const ProtocolV2Codec codec;
    const ProtocolMessage source = request();
    QString error;
    const QByteArray wire = codec.encode(source, &error);
    ProtocolMessage decoded;
    const ProtocolDecodeResult result = codec.decode(wire, &decoded);
    return expect(codec.version() == ProtocolVersion::V2,
                  QStringLiteral("codec version"))
        && expect(!wire.isEmpty() && error.isEmpty(),
                  QStringLiteral("typed object encode"))
        && expect(wire.contains(
                      QByteArrayLiteral("\"message_id\":\"18446744073709551615\"")),
                  QStringLiteral("full quint64 canonical ID"))
        && expect(result.success && same(source, decoded),
                  QStringLiteral("typed object round trip"));
}

bool nestedStringListEncode()
{
    const ProtocolV2Codec codec;
    ProtocolMessage message = request();
    message.payload = QVariantMap{
        {QStringLiteral("schema_version"), 2},
        {QStringLiteral("players"), QStringList{
            QStringLiteral("sgs1"), QStringLiteral("sgs2")}}
    };
    QString error;
    const QByteArray wire = codec.encode(message, &error);
    return expect(!wire.isEmpty() && error.isEmpty()
                      && wire.contains(QByteArrayLiteral("\"players\":[\"sgs1\",\"sgs2\"]")),
                  QStringLiteral("nested QStringList encode"));
}

bool invalidPayloadEncode()
{
    const ProtocolV2Codec codec;
    const QList<QVariant> invalidPayloads{
        QVariant(), true, 7, QStringLiteral("scalar"),
        QVariantList{1, 2},
        QVariantMap(),
        QVariantMap{{QStringLiteral("schema_version"), 1.5}},
        QVariantMap{{QStringLiteral("schema_version"), 0}},
        QVariantMap{{QStringLiteral("schema_version"), -1}}
    };
    for (qsizetype index = 0; index < invalidPayloads.size(); ++index) {
        ProtocolMessage message = request();
        message.payload = invalidPayloads.at(index);
        QString error;
        if (!expect(codec.encode(message, &error).isEmpty() && !error.isEmpty(),
                    QStringLiteral("invalid payload encode %1").arg(index))) {
            return false;
        }
    }

    ProtocolMessage missing = request();
    missing.hasPayload = false;
    missing.payload = QVariant();
    QString error;
    return expect(codec.encode(missing, &error).isEmpty() && !error.isEmpty(),
                  QStringLiteral("missing payload encode"));
}

bool invalidPayloadDecode()
{
    const ProtocolV2Codec codec;
    const QList<QByteArray> invalidFrames{
        QByteArrayLiteral("{\"v\":2,\"type\":\"request\",\"source\":\"room\",\"destination\":\"client\",\"message_id\":\"1\",\"command\":37}"),
        QByteArrayLiteral("{\"v\":2,\"type\":\"request\",\"source\":\"room\",\"destination\":\"client\",\"message_id\":\"1\",\"command\":37,\"payload\":null}"),
        QByteArrayLiteral("{\"v\":2,\"type\":\"request\",\"source\":\"room\",\"destination\":\"client\",\"message_id\":\"1\",\"command\":37,\"payload\":[]}"),
        QByteArrayLiteral("{\"v\":2,\"type\":\"request\",\"source\":\"room\",\"destination\":\"client\",\"message_id\":\"1\",\"command\":37,\"payload\":{}}"),
        QByteArrayLiteral("{\"v\":2,\"type\":\"request\",\"source\":\"room\",\"destination\":\"client\",\"message_id\":\"1\",\"command\":37,\"payload\":{\"schema_version\":1.5}}"),
        QByteArrayLiteral("{\"v\":2,\"type\":\"request\",\"source\":\"room\",\"destination\":\"client\",\"message_id\":\"1\",\"command\":37,\"payload\":{\"schema_version\":0}}"),
        QByteArrayLiteral("{\"v\":2,\"type\":\"request\",\"source\":\"room\",\"destination\":\"client\",\"message_id\":\"1\",\"command\":37,\"payload\":{\"schema_version\":-1}}")
    };
    for (qsizetype index = 0; index < invalidFrames.size(); ++index) {
        ProtocolMessage output = request();
        const ProtocolMessage sentinel = output;
        const ProtocolDecodeResult result = codec.decode(invalidFrames.at(index), &output);
        if (!expect(!result.success
                        && result.error == ProtocolDecodeError::InvalidPayload
                        && same(output, sentinel),
                    QStringLiteral("invalid payload decode %1").arg(index))) {
            return false;
        }
    }
    return true;
}

bool envelopeAndCorrelation()
{
    const ProtocolV2Codec codec;
    ProtocolMessage reply = request();
    reply.type = ProtocolMessageType::Reply;
    reply.source = ProtocolEndpoint::Client;
    reply.destination = ProtocolEndpoint::Room;
    reply.replyTo = 42;
    QString error;
    const QByteArray wire = codec.encode(reply, &error);
    if (!expect(!wire.isEmpty()
                    && wire.contains(QByteArrayLiteral("\"reply_to\":\"42\"")),
                QStringLiteral("reply correlation encode"))) {
        return false;
    }

    reply.replyTo = 0;
    if (!expect(codec.encode(reply, &error).isEmpty(),
                QStringLiteral("uncorrelated reply rejected"))) {
        return false;
    }
    ProtocolMessage bad = request();
    bad.replyTo = 1;
    if (!expect(codec.encode(bad, &error).isEmpty(),
                QStringLiteral("request reply_to rejected"))) {
        return false;
    }
    bad = request();
    bad.version = static_cast<ProtocolVersion>(1);
    return expect(codec.encode(bad, &error).isEmpty(),
                  QStringLiteral("V1 message rejected"));
}
}

int main()
{
    const bool success = validRoundTrip()
        && nestedStringListEncode()
        && invalidPayloadEncode()
        && invalidPayloadDecode()
        && envelopeAndCorrelation();
    QTextStream(stdout) << "[AUTOTEST] PROTOCOL_V2_CONTRACT_RESULT status="
                        << (success ? "PASS" : "FAIL")
                        << " cases=" << caseCount << '\n';
    return success ? 0 : 1;
}
