#include "game-snapshot.h"
#include "game-rng.h"
#include "engine-bootstrap.h"
#include "engine.h"
#include "lua-runtime.h"
#include "player.h"
#include "protocol.h"
#include "recorder.h"
#include "replay-diagnostic-exporter.h"
#include "replay/replay-codec.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "settings.h"
#include "takeover-scenario.h"

#include <algorithm>
#include <functional>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

struct RoomTestAccess
{
    static ServerPlayer *addPlayer(Room &room, const QString &name,
                                   const QString &general, const QString &role)
    {
        ServerPlayer *player = new ServerPlayer(&room);
        player->setObjectName(name);
        player->setState(QStringLiteral("robot"));
        player->setGeneralName(general);
        player->setRole(role);
        const int seat = room.getPlayers().size() + 1;
        player->setSeat(seat);
        player->setPlayerSeat(seat);
        room.addPlayerToRoster(player);
        return player;
    }

    static void startVirtualGame(Room &room)
    {
        room._virtual = true;
        room.startGame();
    }
};

namespace {

bool expect(bool condition, const QString &label)
{
    if (condition)
        return true;
    QTextStream(stderr) << label << " failed\n";
    return false;
}

bool writeBytes(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QJsonObject readObject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    return error.error == QJsonParseError::NoError && document.isObject()
        ? document.object() : QJsonObject();
}

QByteArray sha256(const QByteArray &bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

quint16 readLe16(const QByteArray &bytes, qsizetype offset)
{
    return static_cast<quint16>(static_cast<unsigned char>(bytes.at(offset)))
        | (static_cast<quint16>(static_cast<unsigned char>(bytes.at(offset + 1))) << 8);
}

quint32 readLe32(const QByteArray &bytes, qsizetype offset)
{
    return static_cast<quint32>(readLe16(bytes, offset))
        | (static_cast<quint32>(readLe16(bytes, offset + 2)) << 16);
}

quint32 crc32(const QByteArray &bytes)
{
    quint32 crc = 0xffffffffU;
    for (char byte : bytes) {
        crc ^= static_cast<unsigned char>(byte);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

bool readStoreZip(const QString &path, QMap<QString, QByteArray> *entries)
{
    QFile file(path);
    if (!entries || !file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray bytes = file.readAll();
    const QByteArray endSignature("PK\x05\x06", 4);
    const qsizetype endOffset = bytes.lastIndexOf(endSignature);
    if (endOffset < 0 || endOffset + 22 > bytes.size())
        return false;

    const quint16 entryCount = readLe16(bytes, endOffset + 10);
    qsizetype centralOffset = readLe32(bytes, endOffset + 16);
    for (quint16 i = 0; i < entryCount; ++i) {
        if (centralOffset + 46 > bytes.size()
            || bytes.mid(centralOffset, 4) != QByteArray("PK\x01\x02", 4)
            || readLe16(bytes, centralOffset + 10) != 0)
            return false;
        const quint32 expectedCrc = readLe32(bytes, centralOffset + 16);
        const quint32 size = readLe32(bytes, centralOffset + 24);
        const quint16 nameLength = readLe16(bytes, centralOffset + 28);
        const quint16 extraLength = readLe16(bytes, centralOffset + 30);
        const quint16 commentLength = readLe16(bytes, centralOffset + 32);
        const quint32 localOffset = readLe32(bytes, centralOffset + 42);
        if (centralOffset + 46 + nameLength + extraLength + commentLength
                > bytes.size()
            || static_cast<qsizetype>(localOffset) + 30 > bytes.size()
            || bytes.mid(localOffset, 4) != QByteArray("PK\x03\x04", 4))
            return false;

        const QByteArray nameBytes = bytes.mid(centralOffset + 46, nameLength);
        const quint16 localNameLength = readLe16(bytes, localOffset + 26);
        const quint16 localExtraLength = readLe16(bytes, localOffset + 28);
        const qsizetype dataOffset = localOffset + 30
            + localNameLength + localExtraLength;
        if (dataOffset + size > bytes.size())
            return false;
        const QByteArray data = bytes.mid(dataOffset, size);
        if (crc32(data) != expectedCrc)
            return false;
        entries->insert(QString::fromUtf8(nameBytes), data);
        centralOffset += 46 + nameLength + extraLength + commentLength;
    }
    return true;
}

bool spinUntil(const std::function<bool()> &predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return predicate();
}

GlobalSnapshot validSnapshotState(quint64 turnSerial, int turnCount)
{
    GlobalSnapshot state{};
    state.turnCount = turnCount;
    state.roundCount = 1;
    state.turnSerial = turnSerial;
    state.currentPlayer = QStringLiteral("p1");
    state.currentPhase = QString::number(static_cast<int>(Player::NotActive));
    state.gameMode = QStringLiteral("02p");
    state.packages = QStringList{QStringLiteral("standard")};
    state.seatOrder = QStringList{QStringLiteral("p1"), QStringLiteral("p2")};
    state.drawPile = QList<int>{0};
    state.discardPile = QList<int>{1};
    state.catalogFingerprint.insert(QStringLiteral("fixture"), 1);
    state.catalogFingerprint.insert(QStringLiteral("cardCount"), 4);
    state.configFingerprint.insert(QStringLiteral("fixture"), 1);
    state.configFingerprint.insert(QStringLiteral("gameMode"), QStringLiteral("02p"));
    state.configFingerprint.insert(QStringLiteral("enableAI"), true);
    state.configFingerprint.insert(QStringLiteral("disableLua"), false);
    state.gameplayRng.algorithm = QString::number(GameRng::AlgorithmQsanRejectionV1);
    state.gameplayRng.seed = QStringLiteral("123");
    state.gameplayRng.drawCount = QStringLiteral("0");
    state.aiRng = state.gameplayRng;

    PlayerSnapshot first{};
    first.objectName = QStringLiteral("p1");
    first.general = QStringLiteral("caocao");
    first.hp = 3;
    first.maxhp = 4;
    first.alive = true;
    first.playerSeat = 1;
    first.seat = 1;
    first.handcards = QList<int>{2};
    first.marks.insert(QStringLiteral("damage_point"), 2);
    first.dynamicProperties.insert(
        QStringLiteral("test-property"), QStringLiteral("value"));

    PlayerSnapshot second{};
    second.objectName = QStringLiteral("p2");
    second.general = QStringLiteral("liubei");
    second.hp = 4;
    second.maxhp = 4;
    second.alive = true;
    second.playerSeat = 2;
    second.seat = 2;
    second.handcards = QList<int>{3};
    state.players = QList<PlayerSnapshot>{first, second};

    for (int id = 0; id < 4; ++id) {
        CardSnapshot card;
        card.id = id;
        card.objectName = QStringLiteral("slash");
        card.className = QStringLiteral("Slash");
        state.cards.append(card);
    }
    state.cardPlaces.insert(0, static_cast<int>(Player::DrawPile));
    state.cardPlaces.insert(1, static_cast<int>(Player::DiscardPile));
    state.cardPlaces.insert(2, static_cast<int>(Player::PlaceHand));
    state.cardPlaces.insert(3, static_cast<int>(Player::PlaceHand));
    state.cardOwners.insert(0, QString());
    state.cardOwners.insert(1, QString());
    state.cardOwners.insert(2, QStringLiteral("p1"));
    state.cardOwners.insert(3, QStringLiteral("p2"));
    return state;
}

bool snapshotRoundTripAndStrictSchema()
{
    QTemporaryDir directory;
    if (!expect(directory.isValid(), QStringLiteral("temporary directory")))
        return false;

    GlobalSnapshot state = validSnapshotState(9, 9);

    const QString path = directory.filePath(QStringLiteral("turn_009_turn.json"));
    GameSnapshot original;
    original.setState(state);
    original.setSnapshotType(QStringLiteral("turn"));
    original.setDescription(QStringLiteral("Turn 9"));
    if (!expect(original.save(path), QStringLiteral("atomic snapshot save")))
        return false;

    const QJsonObject root = readObject(path);
    bool ok = true;
    ok = expect(root.value(QStringLiteral("format")).toString()
                    == GameSnapshot::takeoverFormat(),
                QStringLiteral("strict takeover format")) && ok;
    ok = expect(root.value(QStringLiteral("schemaVersion")).toInt()
                    == GameSnapshot::TakeoverSchemaVersion,
                QStringLiteral("strict takeover schema version")) && ok;
    ok = expect(!root.contains(QStringLiteral("version")),
                QStringLiteral("legacy version key absent")) && ok;
    ok = expect(QFileInfo::exists(path), QStringLiteral("committed snapshot")) && ok;

    GameSnapshot restored;
    ok = expect(restored.load(path), QStringLiteral("snapshot load")) && ok;
    const GlobalSnapshot restoredState = restored.getState();
    ok = expect(restoredState.turnSerial == state.turnSerial
                    && restoredState.currentPlayer == state.currentPlayer
                    && restoredState.drawPile == state.drawPile
                    && restoredState.players.size() == 2
                    && restoredState.players.first().dynamicProperties
                           .value(QStringLiteral("test-property")).toString()
                        == QStringLiteral("value"),
                QStringLiteral("snapshot fields round-trip")) && ok;

    // The old partial snapshot contract is intentionally not loadable.
    const QString legacyPath = directory.filePath(QStringLiteral("legacy.json"));
    const QJsonObject legacy{{QStringLiteral("version"), 1},
                             {QStringLiteral("state"), QJsonObject()}};
    ok = expect(writeBytes(legacyPath,
                           QJsonDocument(legacy).toJson(QJsonDocument::Compact)),
                QStringLiteral("write legacy fixture")) && ok;
    GameSnapshot legacySnapshot;
    ok = expect(!legacySnapshot.load(legacyPath),
                QStringLiteral("legacy snapshot rejected")) && ok;
    return ok;
}

QByteArray validReplay()
{
    QSanReplay::ReplayWriter writer(QStringLiteral("test"), QStringLiteral("test"));
    QSanProtocol::ProtocolMessage message;
    message.type = QSanProtocol::ProtocolMessageType::Notification;
    message.source = QSanProtocol::ProtocolEndpoint::Room;
    message.destination = QSanProtocol::ProtocolEndpoint::Client;
    message.command = QSanProtocol::S_COMMAND_ADD_PLAYER;
    message.hasPayload = true;
    message.payload = QVariantMap{
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("player_name"), QStringLiteral("p1")},
        {QStringLiteral("screen_name"), QStringLiteral("Player")},
        {QStringLiteral("avatar"), QStringLiteral("caocao")}
    };
    QString error;
    if (!writer.appendEvent(1, message, &error))
        return QByteArray();

    message.command = QSanProtocol::S_COMMAND_SET_MARK;
    message.payload = QVariantMap{
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("player_name"), QStringLiteral("p1")},
        {QStringLiteral("mark_name"), QStringLiteral("Global_TurnCount")},
        {QStringLiteral("value"), 1}
    };
    return writer.appendEvent(2, message, &error)
        ? writer.rawReplayData() : QByteArray();
}

QByteArray barrierReplay()
{
    QSanReplay::ReplayWriter writer(QStringLiteral("test"),
                                    QStringLiteral("test"), false);
    QString error;
    QSanProtocol::ProtocolMessage message;
    message.type = QSanProtocol::ProtocolMessageType::Notification;
    message.source = QSanProtocol::ProtocolEndpoint::Room;
    message.destination = QSanProtocol::ProtocolEndpoint::Client;
    message.command = QSanProtocol::S_COMMAND_SET_MARK;
    message.hasPayload = true;
    message.payload = QVariantMap{
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("player_name"), QStringLiteral("p1")},
        {QStringLiteral("mark_name"), QStringLiteral("barrier")},
        {QStringLiteral("value"), 1}
    };
    if (!writer.appendEvent(0, message, &error))
        return {};
    message.payload = QVariantMap{
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("player_name"), QStringLiteral("p1")},
        {QStringLiteral("mark_name"), QStringLiteral("barrier")},
        {QStringLiteral("value"), 2}
    };
    return writer.appendEvent(1000, message, &error)
        ? writer.rawReplayData() : QByteArray();
}

bool replayStateCaptureBarrierIsEventAligned()
{
    QTemporaryDir directory;
    const QString replayPath = directory.filePath(QStringLiteral("barrier.txt"));
    if (!expect(directory.isValid() && writeBytes(replayPath, barrierReplay()),
                QStringLiteral("write barrier replay")))
        return false;

    bool ok = true;
    Replayer playing(nullptr, replayPath);
    QObject playingReceiver;
    int appliedPairIndex = -1;
    int pairIndexAtBoundary = -2;
    bool boundaryReached = false;
    quint64 playingRequestId = 0;
    QObject::connect(&playing, &Replayer::replayEventDispatched,
                     &playingReceiver,
                     [&](const QSanProtocol::ProtocolMessage &, int pairIndex,
                         qint64) {
        appliedPairIndex = pairIndex;
    }, Qt::QueuedConnection);
    QObject::connect(&playing, &Replayer::stateCaptureBoundaryReached,
                     &playingReceiver, [&](quint64 requestId) {
        pairIndexAtBoundary = appliedPairIndex;
        boundaryReached = true;
        Q_UNUSED(requestId);
    });
    playing.start();
    ok = expect(spinUntil([&]() { return appliedPairIndex == 0; }, 500),
                QStringLiteral("first replay event applied")) && ok;
    playingRequestId = playing.requestStateCaptureBoundary();
    ok = expect(playingRequestId != 0,
                QStringLiteral("playing capture requested")) && ok;
    ok = expect(spinUntil([&]() { return boundaryReached; }, 500),
                QStringLiteral("playing capture reached during delay")) && ok;
    ok = expect(pairIndexAtBoundary == 0,
                QStringLiteral("capture follows last applied event")) && ok;
    playing.toggle();
    playing.seekToPosition(1);
    ok = expect(!playing.isPlaying() && appliedPairIndex == 0,
                QStringLiteral("capture boundary blocks playback controls")) && ok;
    ok = expect(playing.releaseStateCaptureBoundary(playingRequestId)
                    && playing.isPlaying(),
                QStringLiteral("playing state restored after capture")) && ok;
    playing.stopAndWait(1000);

    Replayer paused(nullptr, replayPath);
    QObject pausedReceiver;
    bool pausedBoundaryReached = false;
    QObject::connect(&paused, &Replayer::stateCaptureBoundaryReached,
                     &pausedReceiver, [&](quint64 requestId) {
        pausedBoundaryReached = true;
        paused.releaseStateCaptureBoundary(requestId);
    });
    paused.start();
    paused.toggle();
    const quint64 pausedRequestId = paused.requestStateCaptureBoundary();
    ok = expect(pausedRequestId != 0,
                QStringLiteral("paused capture requested")) && ok;
    ok = expect(spinUntil([&]() { return pausedBoundaryReached; }, 500),
                QStringLiteral("paused capture reached")) && ok;
    ok = expect(!paused.isPlaying(),
                QStringLiteral("paused state restored after capture")) && ok;
    paused.stopAndWait(1000);

    Replayer seeking(nullptr, replayPath);
    QObject seekingReceiver;
    QList<int> seekAppliedPairIndexes;
    QList<int> seekElapsedSeconds;
    QObject::connect(&seeking, &Replayer::replayEventDispatched,
                     &seekingReceiver,
                     [&](const QSanProtocol::ProtocolMessage &, int pairIndex,
                         qint64) {
        seekAppliedPairIndexes.append(pairIndex);
    }, Qt::QueuedConnection);
    QObject::connect(&seeking, &Replayer::elasped,
                     &seekingReceiver,
                     [&](int seconds) {
        seekElapsedSeconds.append(seconds);
    }, Qt::QueuedConnection);
    seeking.start();
    ok = expect(spinUntil([&]() { return seekAppliedPairIndexes.contains(0); }, 500),
                QStringLiteral("seek fixture first event applied")) && ok;
    seeking.seekToPosition(0);
    ok = expect(spinUntil([&]() { return !seeking.isRunning(); }, 2500),
                QStringLiteral("seek worker reaches target end")) && ok;
    ok = expect(!seekAppliedPairIndexes.isEmpty()
                    && seekAppliedPairIndexes.constLast() == 1
                    && seekAppliedPairIndexes.count(1) == 1,
                QStringLiteral("playing seek dispatches target exactly once")) && ok;
    ok = expect(seekElapsedSeconds.count(1) == 1,
                QStringLiteral("playing seek suppresses stale elapsed time")) && ok;
    seeking.stopAndWait(1000);

    Replayer cancelled(nullptr, replayPath);
    QObject cancelledReceiver;
    int cancelledAppliedPairIndex = -1;
    QObject::connect(&cancelled, &Replayer::replayEventDispatched,
                     &cancelledReceiver,
                     [&](const QSanProtocol::ProtocolMessage &, int pairIndex,
                         qint64) {
        cancelledAppliedPairIndex = pairIndex;
    }, Qt::QueuedConnection);
    cancelled.start();
    ok = expect(spinUntil([&]() { return cancelledAppliedPairIndex == 0; }, 500),
                QStringLiteral("cancel fixture first event applied")) && ok;
    const quint64 cancelledRequestId = cancelled.requestStateCaptureBoundary();
    ok = expect(cancelledRequestId != 0
                    && cancelled.cancelStateCaptureBoundary(cancelledRequestId),
                QStringLiteral("capture request can be cancelled")) && ok;
    ok = expect(spinUntil([&]() { return cancelledAppliedPairIndex == 1; }, 1500),
                QStringLiteral("cancel restores playing state")) && ok;
    cancelled.stopAndWait(1000);
    return ok;
}

bool diagnosticExporterWritesCanonicalBundle()
{
    QTemporaryDir directory;
    if (!expect(directory.isValid(), QStringLiteral("bundle temporary directory")))
        return false;

    const QByteArray replayData = validReplay();
    const QString replayPath = directory.filePath(QStringLiteral("export-source.txt"));
    if (!expect(writeBytes(replayPath, replayData),
                QStringLiteral("write bundle replay")))
        return false;

    const QString snapshotDir = GameSnapshot::getSnapshotDir(replayPath);
    const QString snapshotPath = snapshotDir + QStringLiteral("/turn_001_turn.json");
    GameSnapshot snapshot;
    snapshot.setState(validSnapshotState(1, 0));
    snapshot.setReplayPath(replayPath);
    snapshot.setSnapshotType(QStringLiteral("turn"));
    if (!expect(snapshot.save(snapshotPath), QStringLiteral("write bundle snapshot")))
        return false;

    QFile snapshotFile(snapshotPath);
    if (!snapshotFile.open(QIODevice::ReadOnly))
        return false;
    const QByteArray snapshotData = snapshotFile.readAll();

    QJsonObject manifestEntry{
        {QStringLiteral("file"), QStringLiteral("turn_001_turn.json")},
        {QStringLiteral("sha256"), QString::fromLatin1(sha256(snapshotData))},
        {QStringLiteral("turnSerial"), QStringLiteral("1")},
        {QStringLiteral("playerName"), QStringLiteral("p1")},
        {QStringLiteral("playerTurnCount"), 1}
    };
    const QJsonObject manifest{
        {QStringLiteral("schema"), QStringLiteral("qsanguosha-takeover-manifest-v1")},
        {QStringLiteral("sessionId"), QStringLiteral("bundle-session")},
        {QStringLiteral("replaySha256"), QString::fromLatin1(sha256(replayData))},
        {QStringLiteral("snapshots"), QJsonArray{manifestEntry}}
    };
    const QByteArray manifestData = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    const QString manifestPath = snapshotDir + QStringLiteral("/manifest.json");
    if (!expect(writeBytes(manifestPath, manifestData),
                QStringLiteral("write bundle manifest")))
        return false;

    Replayer replayer(nullptr, replayPath);
    if (!expect(replayer.hasTakeoverSnapshots(),
                QStringLiteral("bundle source verified by Replayer")))
        return false;

    ReplayDiagnosticExportRequest request;
    request.replayPath = replayPath;
    request.manifestPath = replayer.getTakeoverManifestPath();
    request.snapshotPaths = replayer.getTakeoverSnapshotPaths();
    request.includeStateNow = true;
    request.stateNow = QJsonObject{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("restorable"), false},
        {QStringLiteral("alignment"), QStringLiteral("after-event")},
        {QStringLiteral("lastAppliedPairIndex"), 0},
        {QStringLiteral("elapsedMs"), 2},
        {QStringLiteral("clientCore"), QJsonObject{{QStringLiteral("fixture"), true}}}
    };
    request.includeDiagnostics = true;
    request.diagnostics = ReplayDiagnosticExporter::createDiagnostics(replayer);

    const ReplayDiagnosticExportResult overwriteResult =
        ReplayDiagnosticExporter::exportBundle(replayPath, request);
    bool ok = expect(!overwriteResult.success,
                     QStringLiteral("export refuses to overwrite replay source"));
    QFile replayAfterRefusal(replayPath);
    ok = expect(replayAfterRefusal.open(QIODevice::ReadOnly)
                    && replayAfterRefusal.readAll() == replayData,
                QStringLiteral("replay survives refused overwrite")) && ok;

    const QString unrelatedPath = directory.filePath(QStringLiteral("turn_999_turn.json"));
    ok = expect(writeBytes(unrelatedPath, QByteArrayLiteral("unrelated")),
                QStringLiteral("write unrelated snapshot candidate")) && ok;
    ReplayDiagnosticExportRequest outsideRequest = request;
    outsideRequest.snapshotPaths = QStringList{unrelatedPath};
    const ReplayDiagnosticExportResult outsideResult =
        ReplayDiagnosticExporter::exportBundle(
            directory.filePath(QStringLiteral("outside.qsgbug.zip")),
            outsideRequest);
    ok = expect(!outsideResult.success,
                QStringLiteral("export rejects snapshot outside sidecar")) && ok;

    const QString outputPath = directory.filePath(QStringLiteral("fixture.qsgbug.zip"));
    const ReplayDiagnosticExportResult result =
        ReplayDiagnosticExporter::exportBundle(outputPath, request);
    ok = expect(result.success, QStringLiteral("export diagnostic bundle")) && ok;

    QMap<QString, QByteArray> archive;
    ok = expect(readStoreZip(outputPath, &archive),
                QStringLiteral("read standard store-only ZIP")) && ok;
    const QStringList expectedPaths{
        QStringLiteral("bundle.json"),
        QStringLiteral("replay.txt"),
        QStringLiteral("replay.snapshots/manifest.json"),
        QStringLiteral("replay.snapshots/turn_001_turn.json"),
        QStringLiteral("state-now.json"),
        QStringLiteral("diagnostics.json")
    };
    for (const QString &path : expectedPaths)
        ok = expect(archive.contains(path), QStringLiteral("bundle contains %1").arg(path)) && ok;
    ok = expect(archive.value(QStringLiteral("replay.txt")) == replayData,
                QStringLiteral("bundle preserves replay bytes")) && ok;
    ok = expect(archive.value(QStringLiteral("replay.snapshots/manifest.json"))
                    == manifestData,
                QStringLiteral("bundle preserves manifest bytes")) && ok;
    ok = expect(archive.value(QStringLiteral("replay.snapshots/turn_001_turn.json"))
                    == snapshotData,
                QStringLiteral("bundle preserves snapshot bytes")) && ok;

    const QJsonObject bundle = QJsonDocument::fromJson(
        archive.value(QStringLiteral("bundle.json"))).object();
    ok = expect(bundle.value(QStringLiteral("schema")).toString()
                    == QStringLiteral("qsanguosha-bug-bundle-v1"),
                QStringLiteral("bundle schema")) && ok;
    const QJsonArray files = bundle.value(QStringLiteral("files")).toArray();
    ok = expect(files.size() == 5, QStringLiteral("bundle payload inventory")) && ok;
    for (const QJsonValue &value : files) {
        const QJsonObject item = value.toObject();
        const QString path = item.value(QStringLiteral("path")).toString();
        const QByteArray bytes = archive.value(path);
        ok = expect(item.value(QStringLiteral("size")).toInteger() == bytes.size(),
                    QStringLiteral("bundle size for %1").arg(path)) && ok;
        ok = expect(item.value(QStringLiteral("sha256")).toString().toLatin1()
                        == sha256(bytes),
                    QStringLiteral("bundle SHA-256 for %1").arg(path)) && ok;
    }
    ok = expect(!QJsonDocument(request.diagnostics).toJson().contains(
                    directory.path().toUtf8()),
                QStringLiteral("diagnostics omit absolute source path")) && ok;

    request.includeStateNow = false;
    request.stateNow = QJsonObject();
    request.stateNowOmission = QStringLiteral("fixture barrier timeout");
    const QString omissionPath = directory.filePath(QStringLiteral("omitted.qsgbug.zip"));
    const ReplayDiagnosticExportResult omissionResult =
        ReplayDiagnosticExporter::exportBundle(omissionPath, request);
    QMap<QString, QByteArray> omissionArchive;
    ok = expect(omissionResult.success
                    && readStoreZip(omissionPath, &omissionArchive),
                QStringLiteral("export bundle with omitted state")) && ok;
    ok = expect(!omissionArchive.contains(QStringLiteral("state-now.json")),
                QStringLiteral("omitted state is absent")) && ok;
    const QJsonObject omissionManifest = QJsonDocument::fromJson(
        omissionArchive.value(QStringLiteral("bundle.json"))).object();
    const QJsonArray omitted = omissionManifest.value(
        QStringLiteral("omitted")).toArray();
    ok = expect(omitted.size() == 1
                    && omitted.first().toObject().value(QStringLiteral("path")).toString()
                        == QStringLiteral("state-now.json")
                    && omitted.first().toObject().value(QStringLiteral("reason")).toString()
                        == QStringLiteral("fixture barrier timeout"),
                QStringLiteral("bundle records omission reason")) && ok;
    return ok;
}

bool manifestBindsReplayAndRejectsTamper()
{
    QTemporaryDir directory;
    if (!expect(directory.isValid(), QStringLiteral("manifest temporary directory")))
        return false;

    const QByteArray replayData = validReplay();
    const QString replayPath = directory.filePath(QStringLiteral("branch.replay.txt"));
    if (!expect(!replayData.isEmpty() && writeBytes(replayPath, replayData),
                QStringLiteral("write replay fixture")))
        return false;

    const QString snapshotDir = GameSnapshot::getSnapshotDir(replayPath);
    const QString snapshotPath = snapshotDir + QStringLiteral("/turn_001_turn.json");
    GlobalSnapshot state = validSnapshotState(1, 0);
    GameSnapshot snapshot;
    snapshot.setState(state);
    snapshot.setSnapshotType(QStringLiteral("turn"));
    if (!expect(snapshot.save(snapshotPath), QStringLiteral("write snapshot fixture")))
        return false;

    QJsonObject entry;
    entry.insert(QStringLiteral("file"), QStringLiteral("turn_001_turn.json"));
    // Read the committed bytes separately so the hash is exactly the manifest contract.
    QFile snapshotFile(snapshotPath);
    if (!expect(snapshotFile.open(QIODevice::ReadOnly),
                QStringLiteral("open snapshot fixture")))
        return false;
    const QByteArray snapshotData = snapshotFile.readAll();
    snapshotFile.close();
    entry.insert(QStringLiteral("sha256"), QString::fromLatin1(sha256(snapshotData)));
    entry.insert(QStringLiteral("turnSerial"), QStringLiteral("1"));
    entry.insert(QStringLiteral("playerName"), QStringLiteral("p1"));
    entry.insert(QStringLiteral("playerTurnCount"), 1);
    QJsonObject manifest{{QStringLiteral("schema"), QStringLiteral("qsanguosha-takeover-manifest-v1")},
                         {QStringLiteral("sessionId"), QStringLiteral("test-session")},
                         {QStringLiteral("replaySha256"), QString::fromLatin1(sha256(replayData))},
                         {QStringLiteral("snapshots"), QJsonArray{entry}}};
    const QString manifestPath = snapshotDir + QStringLiteral("/manifest.json");
    if (!expect(writeBytes(manifestPath,
                           QJsonDocument(manifest).toJson(QJsonDocument::Compact)),
                QStringLiteral("write manifest fixture")))
        return false;

    bool ok = true;
    Replayer verified(nullptr, replayPath);
    ok = expect(verified.hasTakeoverSnapshots(),
                QStringLiteral("verified manifest accepted")) && ok;

    // Per-snapshot hash tampering must invalidate the entire sidecar load.
    QFile tampered(snapshotPath);
    if (!expect(tampered.open(QIODevice::Append),
                QStringLiteral("open snapshot for tamper")))
        return false;
    tampered.write("tampered");
    tampered.close();
    Replayer rejectedSnapshot(nullptr, replayPath);
    ok = expect(!rejectedSnapshot.hasTakeoverSnapshots(),
                QStringLiteral("tampered snapshot rejected")) && ok;

    // Restore the snapshot, then tamper the replay binding itself.
    if (!expect(writeBytes(snapshotPath, snapshotData),
                QStringLiteral("restore snapshot fixture")))
        return false;
    QJsonObject replayTamperedManifest = manifest;
    replayTamperedManifest.insert(QStringLiteral("replaySha256"), QStringLiteral("00"));
    writeBytes(manifestPath,
               QJsonDocument(replayTamperedManifest).toJson(QJsonDocument::Compact));
    Replayer rejectedReplay(nullptr, replayPath);
    ok = expect(!rejectedReplay.hasTakeoverSnapshots(),
                QStringLiteral("tampered replay hash rejected")) && ok;
    return ok;
}

bool takeoverRestoresRoomState()
{
    QString bootstrapError;
    if (!expect(EngineBootstrap::initialize(false, &bootstrapError),
                QStringLiteral("engine bootstrap for takeover fixture")))
        return false;

    const bool oldEnableAI = Config.EnableAI;
    const bool oldDisableLua = Config.DisableLua;
    Config.EnableAI = true;
    Config.DisableLua = false;

    QTemporaryDir directory;
    if (!expect(directory.isValid(), QStringLiteral("restore temporary directory"))) {
        Config.EnableAI = oldEnableAI;
        Config.DisableLua = oldDisableLua;
        return false;
    }
    const QString snapshotPath = directory.filePath(QStringLiteral("turn_007_turn.json"));

    Room source(nullptr, QStringLiteral("02p"), GameSessionConfig(424242));
    ServerPlayer *sourceP1 = RoomTestAccess::addPlayer(
        source, QStringLiteral("p1"), QStringLiteral("caocao"), QStringLiteral("lord"));
    ServerPlayer *sourceP2 = RoomTestAccess::addPlayer(
        source, QStringLiteral("p2"), QStringLiteral("guanyu"), QStringLiteral("rebel"));
    RoomTestAccess::startVirtualGame(source);
    if (!expect(source.getPlayers().size() == 2 && source.getDrawPile().size() > 3,
                QStringLiteral("started source takeover fixture"))) {
        Config.EnableAI = oldEnableAI;
        Config.DisableLua = oldDisableLua;
        return false;
    }

    int handId = -1;
    QList<int> expectedDraw;
    GameRng::State expectedGameplayRng;
    {
        LuaRuntime::Binding luaBinding(source.roomRuntime()->lua());
        GameRng::Binding rngBinding(source.roomRuntime()->rng());
        EngineRuntimeContextScope contextScope(*Sanguosha, &source);

        source.setTag(QStringLiteral("TurnLengthCount"), 7);
        source.setTag(QStringLiteral("Round"), 3);
        source.setCurrent(sourceP1);
        sourceP1->setPhase(Player::NotActive);
        sourceP1->setMaxHp(4);
        sourceP1->setHp(2);
        // Fixture mutations happen after startGame() built the RoomThread. Use
        // the same restore guard as TakeoverRule so setup cannot dispatch skills.
        source.setRestoringTakeoverSnapshot(true);
        source.setPlayerMark(sourceP1, QStringLiteral("takeover_fixture_mark"), 6);
        sourceP1->setTag(QStringLiteral("takeover_fixture_tag"), QStringLiteral("kept"));

        handId = source.getDrawPile().first();
        source.getDrawPile().removeOne(handId);
        sourceP1->addCard(handId, Player::PlaceHand);
        source.setCardMapping(handId, sourceP1, Player::PlaceHand);
        source.setRestoringTakeoverSnapshot(false);
        expectedDraw = source.getDrawPile();
        if (expectedDraw.size() > 2)
            std::rotate(expectedDraw.begin(), expectedDraw.begin() + 2, expectedDraw.end());
        source.getDrawPile() = expectedDraw;

        expectedGameplayRng = source.roomRuntime()->rng().exportState();
        GameSnapshot snapshot(&source);
        snapshot.setSnapshotType(QStringLiteral("turn"));
        snapshot.setReplayPath(QStringLiteral("fixture.replay.txt"));
        const bool saved = snapshot.save(snapshotPath);
        if (!expect(saved, QStringLiteral("save full restore fixture: %1")
                               .arg(snapshot.getError()))) {
            Config.EnableAI = oldEnableAI;
            Config.DisableLua = oldDisableLua;
            return false;
        }
    }

    GameSessionConfig takeoverConfig(1);
    takeoverConfig.takeover = true;
    takeoverConfig.takeoverSnapshotPath = snapshotPath;
    takeoverConfig.takeoverSeatName = QStringLiteral("p1");
    Room target(nullptr, QStringLiteral("02p"), takeoverConfig);
    ServerPlayer *targetP1 = RoomTestAccess::addPlayer(
        target, QStringLiteral("runtime-human"), QStringLiteral("caocao"),
        QStringLiteral("lord"));
    ServerPlayer *targetP2 = RoomTestAccess::addPlayer(
        target, QStringLiteral("runtime-robot"), QStringLiteral("guanyu"),
        QStringLiteral("rebel"));
    // The fixture has no Server parent, so initialize with robot connections;
    // the real local-server path supplies the online socket registration.
    RoomTestAccess::startVirtualGame(target);
    targetP1->setState(QStringLiteral("online"));
    targetP2->setState(QStringLiteral("robot"));

    // A real GameReady dispatch runs inside the room's Lua, RNG and engine
    // bindings. The focused fixture must provide the same runtime context.
    LuaRuntime::Binding targetLuaBinding(target.roomRuntime()->lua());
    GameRng::Binding targetRngBinding(target.roomRuntime()->rng());
    EngineRuntimeContextScope targetContextScope(*Sanguosha, &target);

    bool ok = expect(target.isTakeoverReady(),
                     QStringLiteral("takeover room fixture ready: %1")
                         .arg(target.takeoverError()));
    QVariant data;
    if (ok) {
        const ScenarioRule *rule = target.takeoverScenario()->getRule();
        ok = expect(rule && rule->trigger(GameReady, &target, nullptr, data),
                    QStringLiteral("takeover GameReady restore")) && ok;
    }
    ok = expect(target.getCurrent() == targetP1,
                QStringLiteral("restored current player")) && ok;
    ok = expect(targetP1->getHp() == 2 && targetP1->getMaxHp() == 4,
                QStringLiteral("restored hp and maxhp")) && ok;
    ok = expect(targetP1->getMark(QStringLiteral("takeover_fixture_mark")) == 6,
                QStringLiteral("restored player mark")) && ok;
    ok = expect(targetP1->getTag(QStringLiteral("takeover_fixture_tag")).toString()
                    == QStringLiteral("kept"),
                QStringLiteral("restored player tag")) && ok;
    ok = expect(targetP1->handCards().contains(handId),
                QStringLiteral("restored hand card")) && ok;
    ok = expect(target.getDrawPile() == expectedDraw,
                QStringLiteral("restored draw pile order")) && ok;
    const GameRng::State restoredGameplayRng = target.roomRuntime()->rng().exportState();
    ok = expect(restoredGameplayRng.seed == expectedGameplayRng.seed
                    && restoredGameplayRng.drawCount == expectedGameplayRng.drawCount
                    && restoredGameplayRng.algorithm == expectedGameplayRng.algorithm,
                QStringLiteral("restored gameplay RNG state")) && ok;

    Config.EnableAI = oldEnableAI;
    Config.DisableLua = oldDisableLua;
    Q_UNUSED(sourceP2);
    Q_UNUSED(targetP2);
    return ok;
}

}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTextStream(stderr) << "[takeover] snapshot schema\n";
    bool ok = snapshotRoundTripAndStrictSchema();
    QTextStream(stderr) << "[takeover] manifest binding\n";
    ok = manifestBindsReplayAndRejectsTamper() && ok;
    QTextStream(stderr) << "[takeover] replay state capture barrier\n";
    ok = replayStateCaptureBarrierIsEventAligned() && ok;
    QTextStream(stderr) << "[takeover] diagnostic bundle export\n";
    ok = diagnosticExporterWritesCanonicalBundle() && ok;
    QTextStream(stderr) << "[takeover] room restore\n";
    ok = takeoverRestoresRoomState() && ok;
    QTextStream(stderr) << "[takeover] complete\n";
    return ok ? 0 : 1;
}
