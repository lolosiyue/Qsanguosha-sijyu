#include "protocol.h"
#include "replay/replay-container.h"
#include "replay/replay-codec.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

using namespace QSanProtocol;
using namespace QSanReplay;

namespace
{
int assertionCount = 0;

bool expect(bool condition, const QString &label)
{
    ++assertionCount;
    if (condition)
        return true;
    QTextStream(stderr) << label << " failed\n";
    return false;
}

ProtocolMessage notification(quint64 id)
{
    ProtocolMessage message;
    message.type = ProtocolMessageType::Notification;
    message.source = ProtocolEndpoint::Room;
    message.destination = ProtocolEndpoint::Client;
    message.messageId = id;
    message.command = S_COMMAND_ADD_PLAYER;
    message.hasPayload = true;
    message.payload = QVariantMap{
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("player_name"), QStringLiteral("p1")},
        {QStringLiteral("screen_name"), QStringLiteral("Player")},
        {QStringLiteral("avatar"), QStringLiteral("caocao")}
    };
    return message;
}

ProtocolMessage request(quint64 id)
{
    ProtocolMessage message;
    message.type = ProtocolMessageType::Request;
    message.source = ProtocolEndpoint::Room;
    message.destination = ProtocolEndpoint::Client;
    message.messageId = id;
    message.command = S_COMMAND_MULTIPLE_CHOICE;
    message.hasPayload = true;
    message.payload = QVariantMap{
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("skill_name"), QStringLiteral("skill")},
        {QStringLiteral("options"),
         QVariantList{QStringLiteral("yes"), QStringLiteral("no")}},
        {QStringLiteral("disabled_options"), QVariantList{}},
        {QStringLiteral("tip"), QStringLiteral("prompt")}
    };
    return message;
}

ProtocolMessage reply(quint64 id, quint64 replyTo)
{
    ProtocolMessage message;
    message.type = ProtocolMessageType::Reply;
    message.source = ProtocolEndpoint::Client;
    message.destination = ProtocolEndpoint::Room;
    message.messageId = id;
    message.replyTo = replyTo;
    message.command = S_COMMAND_MULTIPLE_CHOICE;
    message.hasPayload = true;
    message.payload = QVariantMap{
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("choice"), QStringLiteral("yes")}
    };
    return message;
}

bool normalReplayContract()
{
    ReplayWriter writer(QStringLiteral("2026.08"), QStringLiteral("official"));
    QString error;
    if (!expect(writer.appendEvent(17, notification(1), &error),
                QStringLiteral("normal replay append"))) {
        QTextStream(stderr) << error << '\n';
        return false;
    }

    const QByteArray raw = writer.rawReplayData();
    const ReplayLoadResult load = ReplayReader().read(raw);
    if (!expect(load.success, QStringLiteral("normal replay read"))
        || !expect(load.header.formatVersion == ReplayFormatVersion::V2,
                   QStringLiteral("format V2"))
        || !expect(load.header.protocolVersion == ProtocolVersion::V2,
                   QStringLiteral("protocol V2"))
        || !expect(load.header.schemaVersion == 1,
                   QStringLiteral("header schema"))
        || !expect(load.header.format == QLatin1String("qsanguosha-replay"),
                   QStringLiteral("header format marker"))
        || !expect(load.header.gameVersion == QLatin1String("2026.08")
                       && load.header.modName == QLatin1String("official")
                       && !load.header.takeover,
                   QStringLiteral("header metadata"))
        || !expect(load.events.size() == 1
                       && load.events.constFirst().elapsedMs == 17
                       && load.events.constFirst().message.payload
                           == notification(1).payload,
                   QStringLiteral("logical event round trip"))) {
        return false;
    }

    const QList<QByteArray> lines = raw.split('\n');
    const QJsonDocument header = QJsonDocument::fromJson(lines.constFirst());
    const QJsonDocument event = QJsonDocument::fromJson(lines.at(1));
    return expect(header.isObject() && event.isObject(),
                  QStringLiteral("JSON Lines objects"))
        && expect(header.object().value(QStringLiteral("format")).toString()
                      == QLatin1String("qsanguosha-replay"),
                  QStringLiteral("JSON Lines format marker"))
        && expect(event.object().value(QStringLiteral("message")).isObject(),
                  QStringLiteral("embedded V2 message object"));
}

bool takeoverContract()
{
    ReplayWriter takeover(QStringLiteral("2026.08"), QStringLiteral("official"), true);
    QString error;
    if (!expect(takeover.appendEvent(20, request(10), &error),
                QStringLiteral("takeover request"))
        || !expect(takeover.appendEvent(21, reply(11, 10), &error),
                   QStringLiteral("takeover correlated reply"))) {
        return false;
    }

    const ReplayLoadResult load = ReplayReader().read(takeover.rawReplayData());
    if (!expect(load.success && load.header.takeover && load.events.size() == 2,
                QStringLiteral("takeover replay read"))
        || !expect(load.events.at(1).message.replyTo == 10,
                   QStringLiteral("takeover reply correlation"))) {
        return false;
    }

    ReplayWriter passive;
    return expect(!passive.appendEvent(1, reply(11, 10), &error),
                  QStringLiteral("passive replay rejects replies"));
}

bool strictRejectionContract()
{
    const ReplayLoadResult legacy = ReplayReader().read(
        QByteArrayLiteral("QSAN_REPLAY {\"format_version\":1,\"protocol_version\":1}\n"));
    if (!expect(!legacy.success && legacy.error == ReplayLoadError::InvalidHeader,
                QStringLiteral("legacy replay rejected"))) {
        return false;
    }

    QByteArray malformed = ReplayWriter::headerLine();
    malformed += QByteArrayLiteral(
        "\n{\"schema_version\":1,\"elapsed_ms\":\"1\",\"message\":[]}");
    const ReplayLoadResult invalid = ReplayReader().read(malformed);
    return expect(!invalid.success
                      && invalid.error == ReplayLoadError::InvalidTimelineEntry,
                  QStringLiteral("non-object replay message rejected"));
}

bool pngContainerContract()
{
    ReplayWriter writer(QStringLiteral("2026.08"), QStringLiteral("official"));
    QString error;
    if (!writer.appendEvent(1, notification(1), &error))
        return false;

    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("replay.png"));
    if (!expect(directory.isValid()
                    && ReplayContainer::encodePng(writer.rawReplayData()).save(path),
                QStringLiteral("PNG replay container save"))) {
        return false;
    }
    if (!expect(ReplayContainer::decodePng(path) == writer.rawReplayData(),
                QStringLiteral("PNG replay container round trip"))) {
        return false;
    }

    QImage ordinary(2, 2, QImage::Format_RGBA8888);
    ordinary.fill(Qt::red);
    const QString ordinaryPath = directory.filePath(QStringLiteral("ordinary.png"));
    if (!expect(ordinary.save(ordinaryPath)
                    && ReplayContainer::decodePng(ordinaryPath).isEmpty(),
                QStringLiteral("ordinary PNG rejected"))) {
        return false;
    }

    QImage corrupted = ReplayContainer::encodePng(writer.rawReplayData());
    corrupted.bits()[20] ^= 0x01;
    const QString corruptedPath = directory.filePath(QStringLiteral("corrupted.png"));
    return expect(corrupted.save(corruptedPath)
                      && ReplayContainer::decodePng(corruptedPath).isEmpty(),
                  QStringLiteral("corrupted PNG replay rejected"));
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const bool success = normalReplayContract()
        && takeoverContract()
        && strictRejectionContract()
        && pngContainerContract();
    QTextStream(stdout) << "[AUTOTEST] REPLAY_V2_RESULT status="
                        << (success ? "PASS" : "FAIL")
                        << " assertions=" << assertionCount << '\n';
    return success ? 0 : 1;
}
