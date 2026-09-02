#include "game-snapshot.h"
#include "game-rng.h"
#include "engine-bootstrap.h"
#include "engine.h"
#include "lua-runtime.h"
#include "player.h"
#include "protocol.h"
#include "recorder.h"
#include "replay/replay-codec.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "settings.h"
#include "takeover-scenario.h"

#include <algorithm>
#include <QCoreApplication>
#include <QCryptographicHash>
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
    QTextStream(stderr) << "[takeover] room restore\n";
    ok = takeoverRestoresRoomState() && ok;
    QTextStream(stderr) << "[takeover] complete\n";
    return ok ? 0 : 1;
}
