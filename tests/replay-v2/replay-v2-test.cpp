#include "game-snapshot.h"
#include "protocol.h"
#include "protocol/protocol-runtime.h"
#include "protocol/protocol-v2-codec.h"
#include "recorder.h"
#include "replay-game-state.h"
#include "replay-index.h"
#include "replay/replay-codec.h"

#if QSAN_REPLAY_V2_TEST_TAKEOVER
#include "client.h"
#include "replay-takeover.h"
#endif

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTextStream>

#include <limits>

// Reuse PR19's exact 29-interaction inventory without copying its schemas.
#define main qsanProtocolAllInteractionPayloadFixtureMain
#include "../protocol/all-interaction-payloads-test.cpp"
#undef main

using namespace QSanProtocol;
using namespace QSanReplay;

#if QSAN_REPLAY_V2_TEST_TAKEOVER
Client *ClientInstance = nullptr;

ClientPlayer *Client::getPlayer(const QString &)
{
    return nullptr;
}

void Client::processReplayMessage(const ProtocolMessage &)
{
}
#endif

namespace
{
int replayCaseCount = 0;

bool replayExpect(bool condition, const QString &label)
{
    ++replayCaseCount;
    if (condition)
        return true;
    QTextStream(stderr) << label << " failed\n";
    return false;
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();
    return file.readAll();
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

ProtocolMessage notification(int command, quint64 messageId = 0,
                             const QVariant &payload = QVariant(),
                             bool hasPayload = false)
{
    ProtocolMessage message;
    message.type = ProtocolMessageType::Notification;
    message.source = ProtocolEndpoint::Room;
    message.destination = ProtocolEndpoint::Client;
    message.messageId = messageId;
    message.command = command;
    message.hasPayload = hasPayload;
    message.payload = payload;
    return message;
}

bool semanticallyEqual(const ProtocolMessage &left, const ProtocolMessage &right)
{
    return left.type == right.type
        && left.source == right.source
        && left.destination == right.destination
        && left.messageId == right.messageId
        && left.replyTo == right.replyTo
        && left.command == right.command
        && left.hasPayload == right.hasPayload
        && (!left.hasPayload || left.payload == right.payload);
}

bool goldenAndBasicRoundTrip()
{
    ReplayWriter writer;
    const ProtocolMessage message = notification(
        S_COMMAND_SPEAK, 7,
        QVariantList{QStringLiteral("p1"), QStringLiteral("hello")}, true);
    QString error;
    if (!replayExpect(writer.appendEvent(0, message, &error),
                      QStringLiteral("golden append"))) {
        return false;
    }

    const QByteArray golden = readFile(
        QStringLiteral(QSAN_TEST_ROOT_PATH "/tests/replay-v2/fixtures/replay-v2-golden.txt"));
    const QByteArray raw = writer.rawReplayData();
    ReplayLoadResult load = ReplayReader().read(raw);
    bool ok = replayExpect(raw == golden, QStringLiteral("exact Replay V2 golden"));
    ok = replayExpect(load.success && load.events.size() == 1,
                      QStringLiteral("golden read")) && ok;
    ok = replayExpect(load.header.formatVersion == ReplayFormatVersion::V2
                          && load.header.protocolVersion == ProtocolVersion::V2,
                      QStringLiteral("golden header versions")) && ok;
    ok = replayExpect(semanticallyEqual(load.events.first().message, message),
                      QStringLiteral("golden semantic round trip")) && ok;

    const QByteArray headerOnly = ReplayWriter::headerLine() + '\n';
    load = ReplayReader().read(headerOnly);
    ok = replayExpect(load.success && load.events.isEmpty(),
                      QStringLiteral("header-only replay")) && ok;
    load = ReplayReader().read(QByteArrayLiteral(
        "QSAN_REPLAY {\"format_version\":2,\"future\":\"ignored\",\"protocol_version\":2}\n"));
    ok = replayExpect(load.success && load.events.isEmpty(),
                      QStringLiteral("unknown optional header key ignored")) && ok;

    ReplayWriter unicodeWriter;
    const ProtocolMessage unicode = notification(
        S_COMMAND_SPEAK, 8,
        QVariantList{QStringLiteral("玩家"), QStringLiteral("重播 ✓")}, true);
    ok = replayExpect(unicodeWriter.appendEvent(1, unicode, &error),
                      QStringLiteral("Unicode append")) && ok;
    load = ReplayReader().read(unicodeWriter.rawReplayData());
    ok = replayExpect(load.success
                          && load.events.first().message.payload == unicode.payload,
                      QStringLiteral("Unicode round trip")) && ok;

    ProtocolMessage explicitNull = notification(
        S_COMMAND_SPEAK, 9, QVariant(), true);
    ReplayWriter nullWriter;
    ok = replayExpect(nullWriter.appendEvent(0, explicitNull, &error),
                      QStringLiteral("explicit null append")) && ok;
    load = ReplayReader().read(nullWriter.rawReplayData());
    ok = replayExpect(load.success && load.events.first().message.hasPayload
                          && load.events.first().message.payload.isNull(),
                      QStringLiteral("explicit null distinct from missing payload")) && ok;

    ReplayWriter missingWriter;
    ok = replayExpect(missingWriter.appendEvent(
                          0, notification(S_COMMAND_SPEAK, 10), &error),
                      QStringLiteral("missing payload append")) && ok;
    load = ReplayReader().read(missingWriter.rawReplayData());
    ok = replayExpect(load.success
                          && !load.events.first().message.hasPayload,
                      QStringLiteral("missing payload remains absent")) && ok;
    return ok;
}

bool failureIs(const QByteArray &data, ReplayLoadError expected,
               const QString &label)
{
    const ReplayLoadResult load = ReplayReader().read(data);
    return replayExpect(!load.success && load.error == expected
                            && load.events.isEmpty(), label);
}

bool strictFailureMatrix()
{
    bool ok = true;
    const QList<QByteArray> invalidHeaders{
        QByteArrayLiteral("QSAN_REPLAY\n"),
        QByteArrayLiteral("QSAN_REPLAY {}\n"),
        QByteArrayLiteral("QSAN_REPLAY []\n"),
        QByteArrayLiteral("QSAN_REPLAY {\"format_version\":\"2\",\"protocol_version\":2}\n"),
        QByteArrayLiteral("QSAN_REPLAY {\"format_version\":2}\n"),
        QByteArrayLiteral("QSAN_REPLAY {\"protocol_version\":2}\n"),
        QByteArrayLiteral("QSAN_REPLAY not-json\n")
    };
    for (qsizetype index = 0; index < invalidHeaders.size(); ++index) {
        ok = failureIs(invalidHeaders.at(index), ReplayLoadError::InvalidHeader,
                       QStringLiteral("invalid header %1").arg(index)) && ok;
    }

    ok = failureIs(
        QByteArrayLiteral("QSAN_REPLAY {\"format_version\":99,\"protocol_version\":2}\n"),
        ReplayLoadError::UnsupportedFormatVersion,
        QStringLiteral("unsupported format")) && ok;
    ok = failureIs(
        QByteArrayLiteral("QSAN_REPLAY {\"format_version\":0,\"protocol_version\":2}\n"),
        ReplayLoadError::UnsupportedFormatVersion,
        QStringLiteral("obsolete format")) && ok;
    ok = failureIs(
        QByteArrayLiteral("QSAN_REPLAY {\"format_version\":2,\"protocol_version\":99}\n"),
        ReplayLoadError::UnsupportedProtocolVersion,
        QStringLiteral("unsupported protocol")) && ok;
    ok = failureIs(QByteArray(), ReplayLoadError::EmptyInput,
                   QStringLiteral("empty input")) && ok;
    ok = failureIs(QByteArrayLiteral(" \r\n\t\n"), ReplayLoadError::EmptyInput,
                   QStringLiteral("blank input")) && ok;
    ok = failureIs(QByteArrayLiteral("QSAN_REPLAY ") + QByteArray(5000, 'x'),
                   ReplayLoadError::PacketTooLarge,
                   QStringLiteral("bounded header")) && ok;

    ReplayWriter writer;
    QString error;
    const ProtocolMessage first = notification(S_COMMAND_SPEAK, 1);
    const ProtocolMessage second = notification(S_COMMAND_SPEAK, 2);
    writer.appendEvent(10, first, &error);
    writer.appendEvent(20, second, &error);
    QByteArray decreasing = writer.rawReplayData();
    decreasing.replace("20 {", "5 {");
    ok = failureIs(decreasing, ReplayLoadError::InvalidElapsedTime,
                   QStringLiteral("decreasing elapsed")) && ok;

    const QByteArray prefix = ReplayWriter::headerLine() + '\n';
    ok = failureIs(prefix + QByteArrayLiteral("-1 {}\n"),
                   ReplayLoadError::InvalidElapsedTime,
                   QStringLiteral("negative elapsed")) && ok;
    ok = failureIs(prefix + QByteArrayLiteral("x {}\n"),
                   ReplayLoadError::InvalidElapsedTime,
                   QStringLiteral("nonnumeric elapsed")) && ok;
    ok = failureIs(prefix + QByteArrayLiteral("0\n"),
                   ReplayLoadError::InvalidTimelineEntry,
                   QStringLiteral("missing message")) && ok;
    ok = failureIs(prefix + QByteArrayLiteral("0 [0,0,1044,64]\n"),
                   ReplayLoadError::ProtocolDecodeFailure,
                   QStringLiteral("V1 event rejected inside V2")) && ok;
    ok = failureIs(prefix + QByteArrayLiteral("0 {bad-json}\n"),
                   ReplayLoadError::ProtocolDecodeFailure,
                   QStringLiteral("malformed V2 JSON")) && ok;

    ProtocolCodecRouter router;
    QByteArray invalidTyped = router.encode(
        ProtocolVersion::V2, requestCases().first().logical, &error);
    const bool replacedSchema = invalidTyped.contains("\"schema_version\":1");
    invalidTyped.replace("\"schema_version\":1", "\"schema_version\":99");
    ok = replayExpect(replacedSchema, QStringLiteral("typed schema mutation fixture")) && ok;
    ok = failureIs(prefix + QByteArrayLiteral("0 ") + invalidTyped + '\n',
                   ReplayLoadError::ProtocolDecodeFailure,
                   QStringLiteral("invalid typed V2 payload")) && ok;

    QByteArray partial = writer.rawReplayData();
    partial += QByteArrayLiteral("30 [0,0,1044,64]\n");
    ok = failureIs(partial, ReplayLoadError::ProtocolDecodeFailure,
                   QStringLiteral("transactional no-partial-load")) && ok;

    ReplayLoadResult crlf = ReplayReader().read(
        writer.rawReplayData().replace("\n", "\r\n"));
    ok = replayExpect(crlf.success && crlf.events.size() == 2,
                      QStringLiteral("CRLF tolerance")) && ok;
    ReplayLoadResult blanks = ReplayReader().read(
        QByteArrayLiteral("\n") + writer.rawReplayData() + QByteArrayLiteral("\n\n"));
    ok = replayExpect(blanks.success && blanks.events.size() == 2,
                      QStringLiteral("blank-line tolerance")) && ok;
    return ok;
}

bool identityAndTransportIndependence()
{
    bool ok = true;
    QString error;

    ProtocolMessage high = notification(S_COMMAND_SPEAK, Q_UINT64_C(4294967296));
    high.hasPayload = true;
    high.payload = QStringLiteral("high");
    ProtocolMessage reply;
    reply.type = ProtocolMessageType::Reply;
    reply.source = ProtocolEndpoint::Client;
    reply.destination = ProtocolEndpoint::Room;
    reply.messageId = std::numeric_limits<quint64>::max();
    reply.replyTo = std::numeric_limits<quint64>::max();
    reply.command = S_COMMAND_SPEAK;
    reply.hasPayload = true;
    reply.payload = QStringLiteral("max");

    ReplayWriter writer;
    ok = replayExpect(writer.appendEvent(0, high, &error)
                          && writer.appendEvent(1, reply, &error),
                      QStringLiteral("full quint64 append")) && ok;
    ReplayLoadResult load = ReplayReader().read(writer.rawReplayData());
    ok = replayExpect(load.success && load.events.size() == 2
                          && load.events.at(0).message.messageId == Q_UINT64_C(4294967296)
                          && load.events.at(1).message.messageId
                              == std::numeric_limits<quint64>::max()
                          && load.events.at(1).message.replyTo
                              == std::numeric_limits<quint64>::max(),
                      QStringLiteral("full quint64 round trip")) && ok;

    ProtocolMessage zero = notification(S_COMMAND_SPEAK);
    ReplayWriter firstReplay;
    ReplayWriter secondReplay;
    firstReplay.appendEvent(0, zero, &error);
    secondReplay.appendEvent(0, zero, &error);
    const ReplayLoadResult firstLoad = ReplayReader().read(firstReplay.rawReplayData());
    const ReplayLoadResult secondLoad = ReplayReader().read(secondReplay.rawReplayData());
    ok = replayExpect(zero.messageId == 0
                          && firstLoad.events.first().message.messageId == 1
                          && secondLoad.events.first().message.messageId == 1,
                      QStringLiteral("per-replay local IDs do not mutate input")) && ok;

    ReplayWriter collisionWriter;
    collisionWriter.appendEvent(0, notification(S_COMMAND_SPEAK, 1), &error);
    collisionWriter.appendEvent(1, zero, &error);
    load = ReplayReader().read(collisionWriter.rawReplayData());
    ok = replayExpect(load.success && load.events.at(1).message.messageId == 2,
                      QStringLiteral("local ID skips preserved collision")) && ok;

    ProtocolMessage fromV1 = notification(S_COMMAND_SPEAK, 12,
                                          QStringLiteral("same"), true);
    ProtocolMessage fromV2 = fromV1;
    fromV1.version = ProtocolVersion::V1;
    fromV2.version = ProtocolVersion::V2;
    ReplayWriter v1Source;
    ReplayWriter v2Source;
    v1Source.appendEvent(5, fromV1, &error);
    v2Source.appendEvent(5, fromV2, &error);
    ok = replayExpect(v1Source.rawReplayData() == v2Source.rawReplayData(),
                      QStringLiteral("network version independent replay")) && ok;

    ProtocolMessage protocolSwitch = notification(S_COMMAND_PROTOCOL_SWITCH, 13);
    ReplayWriter switchWriter;
    ok = replayExpect(switchWriter.appendEvent(0, protocolSwitch, &error)
                          && switchWriter.eventRecords().isEmpty(),
                      QStringLiteral("protocol switch is not recorded")) && ok;
    return ok;
}

bool allInteractionsRoundTrip()
{
    const QList<FlowCase> requests = requestCases();
    const QList<FlowCase> replies = replyCases();
    if (!replayExpect(requests.size() == 29 && replies.size() == 29,
                      QStringLiteral("PR19 29/29 inventory reused"))) {
        return false;
    }

    ReplayWriter writer;
    QString error;
    qint64 elapsed = 0;
    for (const FlowCase &flow : requests) {
        if (!replayExpect(writer.appendEvent(elapsed++, flow.logical, &error),
                          flow.name + QStringLiteral(" replay request append"))) {
            return false;
        }
    }
    for (const FlowCase &flow : replies) {
        if (!replayExpect(writer.appendEvent(elapsed++, flow.logical, &error),
                          flow.name + QStringLiteral(" replay reply append"))) {
            return false;
        }
    }

    const ReplayLoadResult load = ReplayReader().read(writer.rawReplayData());
    if (!replayExpect(load.success && load.events.size() == 58,
                      QStringLiteral("29 request and reply mappings decoded"))) {
        return false;
    }
    for (int index = 0; index < requests.size(); ++index) {
        if (!replayExpect(semanticallyEqual(
                              load.events.at(index).message,
                              requests.at(index).logical),
                          requests.at(index).name + QStringLiteral(" replay request parity"))) {
            return false;
        }
    }
    for (int index = 0; index < replies.size(); ++index) {
        if (!replayExpect(semanticallyEqual(
                              load.events.at(requests.size() + index).message,
                              replies.at(index).logical),
                          replies.at(index).name + QStringLiteral(" replay reply parity"))) {
            return false;
        }
    }
    return true;
}

bool frameBoundary()
{
    ProtocolCodecRouter router;
    ProtocolMessage message = notification(S_COMMAND_SPEAK, 77);
    message.hasPayload = true;
    QString error;
    int low = 0;
    int high = ProtocolV2Codec::MaxPacketSize;
    int best = -1;
    while (low <= high) {
        const int middle = low + (high - low) / 2;
        message.payload = QString(middle, QLatin1Char('x'));
        const QByteArray encoded = router.encode(ProtocolVersion::V2, message, &error);
        if (!encoded.isEmpty()) {
            best = middle;
            low = middle + 1;
        } else {
            high = middle - 1;
        }
    }
    if (!replayExpect(best >= 0, QStringLiteral("find maximum legal frame")))
        return false;

    message.payload = QString(best, QLatin1Char('x'));
    const QByteArray maximum = router.encode(ProtocolVersion::V2, message, &error);
    ReplayWriter writer;
    bool ok = replayExpect(maximum.size() == ProtocolV2Codec::MaxPacketSize
                               && writer.appendEvent(0, message, &error),
                           QStringLiteral("maximum legal V2 frame"));
    message.payload = QString(best + 1, QLatin1Char('x'));
    ok = replayExpect(router.encode(ProtocolVersion::V2, message, &error).isEmpty(),
                      QStringLiteral("oversize V2 frame rejected")) && ok;
    return ok;
}

bool legacyAndIndexParity()
{
    const QByteArray legacyBytes = readFile(
        QStringLiteral(QSAN_TEST_ROOT_PATH "/tests/replay-v2/fixtures/legacy-v1.txt"));
    const ReplayLoadResult legacy = ReplayReader().read(legacyBytes);
    bool ok = replayExpect(legacy.success && legacy.events.size() == 2
                               && legacy.header.formatVersion == ReplayFormatVersion::LegacyV1
                               && legacy.header.protocolVersion == ProtocolVersion::V1,
                           QStringLiteral("headerless Legacy V1 fixture"));
    if (!legacy.success)
        return false;

    ReplayWriter writer;
    QString error;
    for (const ReplayEvent &event : legacy.events)
        writer.appendEvent(event.elapsedMs, event.message, &error);
    const ReplayLoadResult v2 = ReplayReader().read(writer.rawReplayData());
    ok = replayExpect(v2.success && v2.events.size() == legacy.events.size(),
                      QStringLiteral("Legacy V1 semantic conversion to V2")) && ok;

    ReplayIndex legacyIndex;
    ReplayIndex v2Index;
    legacyIndex.buildIndex(legacy.events);
    v2Index.buildIndex(v2.events);
    ok = replayExpect(legacyIndex.getNodeCount() == v2Index.getNodeCount()
                          && legacyIndex.getNodeCount() == 1,
                      QStringLiteral("V1/V2 ReplayIndex node count parity")) && ok;
    if (legacyIndex.getNodeCount() == 1 && v2Index.getNodeCount() == 1) {
        const ReplayNode legacyNode = legacyIndex.getNode(0);
        const ReplayNode v2Node = v2Index.getNode(0);
        ok = replayExpect(legacyNode.type == v2Node.type
                              && legacyNode.elapsed == v2Node.elapsed
                              && legacyNode.pairIndex == v2Node.pairIndex,
                          QStringLiteral("V1/V2 ReplayIndex node parity")) && ok;
    }
    return ok;
}

bool containersPlaybackSnapshotAndTakeover()
{
    QTemporaryDir temporary;
    if (!replayExpect(temporary.isValid(), QStringLiteral("temporary replay directory")))
        return false;

    ProtocolMessage turn = notification(
        S_COMMAND_SET_MARK, 0,
        QVariantList{QStringLiteral("p1"), QStringLiteral("Global_TurnCount"), 1}, true);
    ProtocolMessage speak = notification(
        S_COMMAND_SPEAK, 0,
        QVariantList{QStringLiteral("p1"), QStringLiteral("hello")}, true);

    Recorder recorder;
    QString error;
    bool ok = replayExpect(recorder.recordMessage(turn, &error)
                               && recorder.recordMessage(speak, &error),
                           QStringLiteral("Recorder logical messages"));
    const QString textPath = temporary.filePath(QStringLiteral("record.txt"));
    const QString pngPath = temporary.filePath(QStringLiteral("record.png"));
    ok = replayExpect(recorder.save(textPath), QStringLiteral("Replay V2 TXT save")) && ok;
    ok = replayExpect(recorder.save(pngPath), QStringLiteral("Replay V2 PNG save")) && ok;
    ok = replayExpect(readFile(textPath).startsWith(ReplayWriter::headerLine()),
                      QStringLiteral("TXT contains V2 header")) && ok;
    ok = replayExpect(Recorder::PNG2TXT(pngPath).startsWith(ReplayWriter::headerLine()),
                      QStringLiteral("PNG contains V2 after decode")) && ok;

    const QString snapshotDir = GameSnapshot::getSnapshotDir(textPath);
    QDir().mkpath(snapshotDir);
    GameSnapshot snapshot;
    GlobalSnapshot snapshotState{};
    snapshotState.turnCount = 1;
    snapshot.setState(snapshotState);
    snapshot.setTurnCount(1);
    snapshot.setSnapshotType(QStringLiteral("turn"));
    snapshot.setReplayPath(textPath);
    const QString snapshotPath = snapshotDir + QLatin1Char('/')
        + GameSnapshot::generateSnapshotFilename(1, QStringLiteral("turn"));
    ok = replayExpect(snapshot.save(snapshotPath), QStringLiteral("snapshot save")) && ok;

    Replayer textReplayer(nullptr, textPath);
    Replayer pngReplayer(nullptr, pngPath);
    ok = replayExpect(textReplayer.isValid()
                          && textReplayer.formatVersion() == ReplayFormatVersion::V2,
                      QStringLiteral("V2 TXT Replayer load")) && ok;
    ok = replayExpect(pngReplayer.isValid()
                          && pngReplayer.formatVersion() == ReplayFormatVersion::V2,
                      QStringLiteral("V2 PNG Replayer load")) && ok;

    ReplayWriter longTimeline;
    ok = replayExpect(longTimeline.appendEvent(
                          std::numeric_limits<qint64>::max(), speak, &error),
                      QStringLiteral("qint64 elapsed append")) && ok;
    const QString longTimelinePath = temporary.filePath(QStringLiteral("long-timeline.txt"));
    ok = replayExpect(writeFile(longTimelinePath, longTimeline.rawReplayData()),
                      QStringLiteral("qint64 elapsed fixture save")) && ok;
    Replayer longTimelineReplayer(nullptr, longTimelinePath);
    int uiElapsed = -1;
    QObject::connect(&longTimelineReplayer, &Replayer::elasped,
                     [&uiElapsed](int seconds) { uiElapsed = seconds; });
    longTimelineReplayer.seekToPosition(0);
    ok = replayExpect(longTimelineReplayer.isValid()
                          && longTimelineReplayer.events().first().elapsedMs
                              == std::numeric_limits<qint64>::max()
                          && longTimelineReplayer.getDuration()
                              == std::numeric_limits<int>::max()
                          && uiElapsed == std::numeric_limits<int>::max(),
                      QStringLiteral("qint64 elapsed and checked UI boundary")) && ok;

    ok = replayExpect(textReplayer.getIndex() != nullptr
                          && textReplayer.getIndex()->getNodeCount() == 1
                          && textReplayer.getSnapshot(0) != nullptr,
                      QStringLiteral("Replay V2 timeline snapshot")) && ok;

    QList<ProtocolMessage> sought;
    QObject::connect(&textReplayer, &Replayer::command_parsed,
                     [&sought](const ProtocolMessage &message) { sought.append(message); });
    textReplayer.seekToPosition(1);
    ok = replayExpect(sought.size() == 2
                          && sought.at(0).command == S_COMMAND_SET_MARK
                          && sought.at(1).command == S_COMMAND_SPEAK,
                      QStringLiteral("ProtocolMessage seek playback")) && ok;

#if QSAN_REPLAY_V2_TEST_TAKEOVER
    ReplayTakeoverManager takeover(&textReplayer);
    takeover.setTakeoverTarget(QStringLiteral("p1"));
    takeover.enableTakeover();
    textReplayer.seekToPosition(1);
    const QString branchPath = temporary.filePath(QStringLiteral("branch.txt"));
    takeover.saveNewReplay(branchPath);
    takeover.disableTakeover();
    const ReplayLoadResult branch = ReplayReader().read(readFile(branchPath));
    ok = replayExpect(branch.success && branch.events.size() == 2
                          && branch.header.formatVersion == ReplayFormatVersion::V2,
                      QStringLiteral("takeover branch saves Replay V2")) && ok;
#endif

    const QByteArray legacyBytes = readFile(
        QStringLiteral(QSAN_TEST_ROOT_PATH "/tests/replay-v2/fixtures/legacy-v1.txt"));
    const QString legacyTextPath = temporary.filePath(QStringLiteral("legacy.txt"));
    const QString legacyPngPath = temporary.filePath(QStringLiteral("legacy.png"));
    writeFile(legacyTextPath, legacyBytes);
    Recorder::TXT2PNG(legacyBytes).save(legacyPngPath);
    Replayer legacyText(nullptr, legacyTextPath);
    Replayer legacyPng(nullptr, legacyPngPath);
    ok = replayExpect(legacyText.isValid()
                          && legacyText.formatVersion() == ReplayFormatVersion::LegacyV1,
                      QStringLiteral("Legacy V1 TXT Replayer")) && ok;
    ok = replayExpect(legacyPng.isValid()
                          && legacyPng.formatVersion() == ReplayFormatVersion::LegacyV1,
                      QStringLiteral("Legacy V1 PNG Replayer")) && ok;
    return ok;
}

bool productionSourceContract()
{
    int replayV1ProductionCalls = 0;
    const QRegularExpression replayV1Call(
        QStringLiteral("\\bencodeReplayV1\\s*\\("));
    QDirIterator iterator(QStringLiteral(QSAN_TEST_ROOT_PATH "/src"),
                          QStringList{QStringLiteral("*.cpp"), QStringLiteral("*.h")},
                          QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        const QString normalizedPath = QDir::fromNativeSeparators(path);
        if (normalizedPath.endsWith(QStringLiteral("/protocol/protocol-runtime.cpp"))
            || normalizedPath.endsWith(QStringLiteral("/protocol/protocol-runtime.h"))) {
            continue;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QString source = QString::fromUtf8(file.readAll());
        auto match = replayV1Call.globalMatch(source);
        while (match.hasNext()) {
            match.next();
            ++replayV1ProductionCalls;
        }
    }
    return replayExpect(replayV1ProductionCalls == 0,
                        QStringLiteral("production encodeReplayV1 call count is zero"));
}
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    const bool success = goldenAndBasicRoundTrip()
        && strictFailureMatrix()
        && identityAndTransportIndependence()
        && allInteractionsRoundTrip()
        && frameBoundary()
        && legacyAndIndexParity()
        && containersPlaybackSnapshotAndTakeover()
        && productionSourceContract();
    QTextStream(stdout) << "[AUTOTEST] REPLAY_V2_RESULT status="
                        << (success ? "PASS" : "FAIL")
                        << " cases=" << replayCaseCount << "\n";
    return success ? 0 : 1;
}
