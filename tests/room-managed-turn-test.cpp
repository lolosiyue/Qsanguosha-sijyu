#include "room-managed-state.h"
#include "room.h"
#include "roomthread.h"
#include "gamerule.h"
#include "serverplayer.h"
#include "room-roster.h"
#include "game-session-controller.h"
#include "engine.h"
#include "engine-bootstrap.h"
#include "runtime-paths.h"
#include "settings.h"
#include "ai.h"
#include "socket.h"
#include "request-coordinator.h"
#include "protocol/gameplay/protocol-gameplay-payload-registry.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <QtMath>
#include <lua.hpp>
#include <functional>
#include <stdexcept>

static int checks = 0;
static QString error;
#define CHECK(x) do { ++checks; if (!(x)) qFatal("CHECK %s:%d: %s (%s)", __FILE__, __LINE__, #x, qPrintable(error)); } while (false)

struct GameSessionControllerTestAccess {
    static void initializing(GameSessionController &session) {
        CHECK(session.requestStart());
        CHECK(session.transitionTo(GameSessionController::State::Initializing));
    }
};
struct RoomTestAccess {
    static RoomThread *start(Room &room) {
        room.thread = new RoomThread(&room);
        room.m_roster->resetAliveToPlayers();
        auto players = room.getPlayers();
        for (int i = 0; i < players.size(); ++i) {
            players[i]->setNext(players[(i + 1) % players.size()]);
            players[i]->setSeat(i + 1);
        }
        GameSessionControllerTestAccess::initializing(*room.m_gameSession);
        room.markGameReadyCompleted();
        room.beginNumericStateHistory();
        room.setCurrent(players.first());
        return room.thread;
    }
    static bool playing(const Room &room) { return room.isGamePlaying(); }
    static quint64 sendDirectionRequest(Room &room, ServerPlayer *player) {
        if (!room.m_requests->request(player, QSanProtocol::S_COMMAND_CHOOSE_DIRECTION,
                                      QVariant(), 0, false))
            return 0;
        return player->m_expectedReplyMessageId.load();
    }
    static bool deliverDirectionReply(Room &room, ServerPlayer *player,
                                      quint64 replyTo, const QString &direction) {
        QSanProtocol::ProtocolMessage logical;
        logical.type = QSanProtocol::ProtocolMessageType::Reply;
        logical.source = QSanProtocol::ProtocolEndpoint::Client;
        logical.destination = QSanProtocol::ProtocolEndpoint::Room;
        logical.command = QSanProtocol::S_COMMAND_CHOOSE_DIRECTION;
        logical.messageId = replyTo + 1000;
        logical.replyTo = replyTo;
        logical.hasPayload = true;
        logical.payload = direction;
        QSanProtocol::ProtocolMessage wire;
        if (!QSanProtocol::ProtocolGameplayPayloadRegistry::encodeForWire(
                logical, &wire, &error))
            return false;
        room.m_requests->processResponse(player, wire);
        return true;
    }
    static bool consumeDirectionResult(Room &room, ServerPlayer *player,
                                       time_t timeout = 0) {
        return room.m_requests->getResult(player, timeout);
    }
    static GameTimeline::RequestToken managedToken(Room &room, ServerPlayer *player) {
        QMutexLocker locker(&room.m_requests->m_mutex);
        return room.m_requests->m_managedRequests.value(player->objectName()).token;
    }
    static bool managedRestorePending(const RoomThread &thread) {
        return thread.m_managedRestorePending;
    }
    static QString managedRestoreAnchor(const RoomThread &thread) {
        return thread.m_managedRestoreAnchor;
    }
    static bool hasManagedPending(Room &room) {
        return room.m_requests->hasManagedPendingRequests();
    }
    static bool waitingForReply(const ServerPlayer *player) {
        return player->m_isWaitingReply;
    }
    static bool clientResponseReady(const ServerPlayer *player) {
        return player->m_isClientResponseReady;
    }
    static void commitRequestGeneration(Room &room, quint64 generation) {
        room.m_requests->commitManagedGeneration(generation);
    }
    static const GameTimeline &timeline(const Room &room) {
        return room.m_managedState->worldStore()->timeline();
    }
    static BattleStatistics::Match statisticsReceipt(const Room &room) {
        BattleStatistics::Match match;
        {
            QMutexLocker locker(&room.m_statisticsMutex);
            match = room.m_statisticsMatch;
        }
        match.history = room.resolutionHistory().snapshot();
        return match;
    }
    static void runManagedRequestChecks(Room &room, ServerPlayer *player);
};

#include "managed-request-checks.h"

void RoomTestAccess::runManagedRequestChecks(Room &room, ServerPlayer *player)
{
    checkManagedRequestLifecycle(room, player, [](bool condition, const char *label) {
        CHECK(condition && label[0] != '\0');
    });
}

namespace {
void inventoryCheckpoint(const Room &room, const GameState::WorldState &state,
                         GameTimeline::AnchorKind kind)
{
    QMap<QString, QString> nativeTypes;
    QStringList unsupported;
    const auto tags = [&](const QString &owner, const QVariantMap &values) {
        for (auto it = values.cbegin(); it != values.cend(); ++it)
            nativeTypes.insert(owner + "/" + it.key(), QString::fromLatin1(it.value().metaType().name()));
    };
    std::function<void(const QString &, const QVariant &)> validate;
    validate = [&](const QString &path, const QVariant &value) {
        if (value.userType() == QMetaType::QVariantMap && !value.toMap().contains("$ref")) {
            const auto map = value.toMap();
            for (auto it = map.cbegin(); it != map.cend(); ++it) validate(path + "/" + it.key(), it.value());
            return;
        }
        if (value.userType() == QMetaType::QVariantList) {
            const auto list = value.toList();
            for (qsizetype i = 0; i < list.size(); ++i) validate(path + "/" + QString::number(i), list[i]);
            return;
        }
        QString why;
        if (!GameState::validateValue(value, state, &why)) unsupported << path + ": " + why;
    };
    tags("room", room.getAllTags());
    for (const auto *player : room.getPlayers()) {
        tags(player->objectName(), player->getAllTags());
        for (const auto &name : player->dynamicPropertyNames())
            nativeTypes.insert(player->objectName() + "/property/" + QString::fromUtf8(name),
                QString::fromLatin1(player->property(name).metaType().name()));
    }
    for (auto it = state.cards.cbegin(); it != state.cards.cend(); ++it) {
        if (const auto *card = room.getCard(it.key().toInt())) {
            tags("card/" + it.key(), card->tag);
            tags("inner/" + it.key(), card->getRealCard()->tag);
        }
        validate("card/" + it.key(), it->state);
    }
    validate("room", state.roomTags);
    for (auto it = state.players.cbegin(); it != state.players.cend(); ++it) {
        validate("player/" + it.key() + "/tags", it->tags);
        validate("player/" + it.key() + "/properties", it->properties);
    }
    for (auto it = state.skills.cbegin(); it != state.skills.cend(); ++it) {
        validate("skill/" + it.key(), it->state);
        validate("skill/" + it.key() + "/correct", it->correctState);
    }
    for (auto it = state.providers.cbegin(); it != state.providers.cend(); ++it)
        validate("provider/" + it.key(), it->state);
    for (const auto &event : state.history.events) validate("history/" + event.id, event.data);
    qInfo() << "checkpoint_inventory" << int(kind) << "native types" << nativeTypes
            << "players" << state.players.keys() << "cards" << state.cards.keys()
            << "skills" << state.skills.keys() << "unsupported" << unsupported;
    CHECK(unsupported.isEmpty());
}

bool run(LuaRuntime &runtime, const QString &script)
{
    LuaRuntime::Binding binding(runtime, false);
    LuaRuntime::LuaInvocationScope invocation(runtime);
    auto *L = runtime.state();
    const int top = lua_gettop(L);
    const QByteArray bytes = script.toUtf8();
    bool ok = luaL_loadbuffer(L, bytes.constData(), size_t(bytes.size()), "managed-turn") == LUA_OK;
    if (ok) ok = LuaRuntime::protectedCall(L, 0, 0, 0) == LUA_OK;
    if (!ok) error = QString::fromUtf8(lua_tostring(L, -1));
    lua_settop(L, top);
    return ok;
}
class RecordingSocket final : public ClientSocket {
public:
    int sent = 0;
    void connectToHost() override {}
    void send(const QByteArray &) override { ++sent; }
    bool isConnected() const override { return true; }
    QString peerName() const override { return "managed-turn"; }
    QString peerAddress() const override { return "127.0.0.1"; }
    void disconnectFromHost() override {}
};

// Audited rule fixture: immutable native code, no mutable members/upvalues.
// It executes the real player phase loop, card movement/use, history and RNG.
class ManagedFixtureRule final : public TriggerSkill {
public:
    ManagedFixtureRule() : TriggerSkill("fixture.turn") { events << EventPhaseProceeding; global = true; }
    bool triggerable(const ServerPlayer *target) const override { return target != nullptr; }
    int getPriority(TriggerEvent) const override { return 100; }
    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override {
        if (player->getPhase() == Player::Draw) room->drawCards(player, 2, objectName());
        if (player->getPhase() == Player::Play) {
            if (player->isOnline()) RoomTestAccess::runManagedRequestChecks(*room, player);
            const QString id = player->objectName();
            CHECK(run(room->roomRuntime()->lua(), QString(
                "local a=sgs.ManagedState; local n=a.get('player','%1','plays') or 0; "
                "assert(a.set('player','%1','plays',n+1))").arg(id)));
            CHECK(run(room->roomRuntime()->ai().lua(), QString(
                "local a=sgs.ManagedState; local n=a.get('player','%1','confidence') or 0; "
                "assert(a.set('player','%1','confidence',n+1))").arg(id)));
            room->setPlayerMark(player, "fixture_roll", room->roomRuntime()->rng().bounded(100000));
            room->addPlayerMark(player, "fixture_plays");
            room->addPlayerMark(player, "@HuJia");
            for (const auto &instance : player->getSkillInstances()) {
                if (instance.skillName != objectName()) continue;
                player->setSkillInstanceState(objectName(), instance.instanceID,
                    {{"uses", player->getMark("fixture_plays")}});
                CHECK(player->setSkillInstanceCorrectStateValue(objectName(), instance.instanceID,
                    "amount", player->getMark("fixture_plays") + 1));
            }
            GameRng aiChoice;
            CHECK(aiChoice.restoreState(room->roomRuntime()->ai().exportRngState(), &error));
            const int aiRoll = aiChoice.bounded(100000);
            CHECK(room->roomRuntime()->ai().restoreRngState(aiChoice.exportState(), &error));
            CHECK(run(room->roomRuntime()->ai().lua(), QString(
                "assert(sgs.ManagedState.set('player','%1','roll',%2))").arg(id).arg(aiRoll)));
            player->setTag("fixture_target", GameState::reference("player", player->getNext()->objectName()));
            for (const auto *card : player->getHandcards()) {
                if (card->isKindOf("Peach") && player->isWounded()) {
                    CardUseStruct use; use.card = card; use.from = player; use.to << player;
                    room->useCard(use);
                    break;
                }
            }
            QVariant decision = QStringLiteral("skillChoice:fixture.turn:continue");
            room->getThread()->trigger(ChoiceMade, room, player, decision);
            if (room->getTag("fixture.request_restore").toBool()) {
                room->removeTag("fixture.request_restore");
                CHECK(room->getThread()->requestManagedRestore(GameTimeline::AnchorKind::PlayerTurn, &error));
            }
        }
        if (player->getPhase() == Player::Discard && !player->getHandcards().isEmpty())
            room->throwCard(player->getHandcards().first(), objectName(), player);
        return true; // This fixture owns the phase action; GameRule still owns the turn/round/phase lifecycle.
    }
};

QStringList ids(const QList<int> &cards)
{
    QStringList result; for (int card : cards) result << QString::number(card); return result;
}

struct StoredMatchProjection {
    bool found = false;
    quint64 generation = 0;
    QString branch;
    bool terminal = false;
    QByteArray history;
    int matchCount = 0;
};

StoredMatchProjection storedMatchProjection(const QString &path, const QString &root)
{
    StoredMatchProjection result;
    const QString connectionName = QUuid::createUuid().toString(QUuid::WithoutBraces);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(path);
        CHECK(db.open());
        QSqlQuery count(db);
        CHECK(count.exec(QStringLiteral("SELECT COUNT(*) FROM battle_matches")) && count.next());
        result.matchCount = count.value(0).toInt();
        count.finish();
        QSqlQuery query(db);
        query.prepare(QStringLiteral("SELECT generation,branch,terminal,history FROM battle_matches WHERE root=?"));
        query.addBindValue(root);
        CHECK(query.exec());
        if (query.next()) {
            result.found = true;
            result.generation = query.value(0).toULongLong();
            result.branch = query.value(1).toString();
            result.terminal = query.value(2).toBool();
            result.history = query.value(3).toByteArray();
        }
        query.finish();
        db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
    return result;
}

QByteArray compactHistory(const ResolutionHistorySnapshot &history)
{
    return QJsonDocument::fromVariant(history.serialize()).toJson(QJsonDocument::Compact);
}

QVariantMap metricTotals(const QVariantList &rows)
{
    QVariantMap totals;
    for (const auto &value : rows) {
        const auto metrics = value.toMap().value(QStringLiteral("metrics")).toMap();
        for (auto it = metrics.cbegin(); it != metrics.cend(); ++it)
            totals[it.key()] = totals.value(it.key()).toDouble() + it.value().toDouble();
    }
    return totals;
}

void checkRoomStatisticsProjection(Room &room, const QString &path, quint64 generation)
{
    const auto receipt = RoomTestAccess::statisticsReceipt(room);
    const auto history = room.resolutionHistory().snapshot();
    CHECK(receipt.rootMatchId == room.gameTimeline().rootGameId());
    CHECK(receipt.generation == generation && receipt.generation == room.gameTimeline().generation());
    CHECK(receipt.branchId == room.gameTimeline().branchId());
    CHECK(receipt.history.serialize() == history.serialize());

    const auto stored = storedMatchProjection(path, receipt.rootMatchId);
    CHECK(stored.found && stored.matchCount == 1);
    CHECK(stored.generation == generation && stored.branch == room.gameTimeline().branchId());
    CHECK(stored.terminal);
    CHECK(stored.history == compactHistory(history));

    QString summaryError;
    const auto summaries = BattleStatistics::readSummaries(path, QStringLiteral("excluded"), &summaryError);
    CHECK(summaryError.isEmpty() && !summaries.isEmpty());
    QVariantList summaryRows;
    for (const auto &value : summaries) {
        const auto summary = value.toMap();
        CHECK(summary.value(QStringLiteral("dataset")).toString() == QStringLiteral("excluded"));
        CHECK(summary.value(QStringLiteral("status")).toString() == QStringLiteral("isolated"));
        CHECK(summary.value(QStringLiteral("games")).toInt() == 1);
        summaryRows << summary;
    }
    const auto expected = metricTotals(BattleStatistics::analyze(receipt));
    const auto actual = metricTotals(summaryRows);
    for (const QString &key : {QStringLiteral("damage"), QStringLiteral("hp_damage"),
                               QStringLiteral("armor_damage"), QStringLiteral("received_damage"),
                               QStringLiteral("recovery"), QStringLiteral("support_recovery"),
                               QStringLiteral("card_uses"), QStringLiteral("slash_uses"),
                               QStringLiteral("completed_turns"), QStringLiteral("truncated_turns")})
        CHECK(qAbs(expected.value(key).toDouble() - actual.value(key).toDouble()) < 0.000001);
}

void checkInvalidatedProjection(Room &room, const QString &path, quint64 generation)
{
    const auto stored = storedMatchProjection(path, room.statisticsRootMatchId());
    CHECK(stored.found && stored.matchCount == 1);
    CHECK(stored.generation == generation && stored.branch == room.gameTimeline().branchId());
    CHECK(!stored.terminal && stored.history == QByteArray("{}"));
    QString summaryError;
    CHECK(BattleStatistics::readSummaries(path, QStringLiteral("excluded"), &summaryError).isEmpty());
    CHECK(summaryError.isEmpty());
}

QString statisticsTimelineLockPath(const QString &databasePath, const QString &root)
{
    const QByteArray digest = QCryptographicHash::hash(root.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QDir(databasePath + QStringLiteral(".timeline")).filePath(
        QString::fromLatin1(digest) + QStringLiteral(".json.lock"));
}

void runningGame()
{
    Room room(nullptr, "02p", GameSessionConfig(98765), Room::RuntimeInitializationPolicy::Deferred);
    EngineRuntimeContextScope engineScope(*Sanguosha, &room);
    auto &game = room.roomRuntime()->lua(); auto &ai = room.roomRuntime()->ai().lua();
    CHECK(game.initialize(&error) && ai.initialize(&error));
    LuaRuntime::Binding binding(game, false);
    GameRng::Binding rngBinding(room.roomRuntime()->rng());
    room.roomRuntime()->state().reset();
    auto *socket = new RecordingSocket;
    auto *p1 = room.addSocket(socket); p1->setObjectName("p1");
    auto *p2 = room.addAIPlayer(); p2->setObjectName("p2");
    for (auto *player : {p1, p2}) {
        player->setMaxHp(4); player->setHp(3); player->setPhase(Player::NotActive);
        player->setRole(player == p1 ? "lord" : "rebel");
        player->setAI(new TrustAI(player));
        const int instance = player->createSkillInstance("fixture.turn", SourceAcquired);
        player->setSkillInstanceState("fixture.turn", instance, {{"uses", 0}});
        CHECK(player->setSkillInstanceCorrectStateValue("fixture.turn", instance, "amount", 1));
    }
    room.getDrawPile().clear();
    // Only explicitly supported active card definitions enter this room's deck.
    for (const QString &name : {QStringLiteral("peach"), QStringLiteral("jink"), QStringLiteral("slash")}) {
        int count = 0;
        for (int id = 0; id < Sanguosha->getCardCount() && count < 8; ++id) {
            auto *card = room.getCard(id);
            if (card && card->objectName() == name
                && QString::fromLatin1(card->getRealCard()->metaObject()->className()).compare(name, Qt::CaseInsensitive) == 0) {
                room.getDrawPile() << id; room.setCardMapping(id, nullptr, Player::DrawPile); ++count;
            }
        }
    }
    CHECK(room.getDrawPile().size() >= 12);
    auto *thread = RoomTestAccess::start(room);
    GameRule rule(nullptr); ManagedFixtureRule fixture;
    thread->addTriggerSkill(&rule); thread->addTriggerSkill(&fixture);
    auto reject = std::make_shared<bool>(false);
    auto corruptNativePayload = std::make_shared<bool>(false);
    GameState::ProviderRegistry registry;
    GameState::ProviderContract skill;
    skill.id = "fixture.skill"; skill.version = 1; skill.skillDefinitions = {"fixture.turn"};
    skill.audit = "Immutable fixture phase rule; all persisted values are native fields or ManagedState, no mutable closure.";
    skill.prepare = [reject, corruptNativePayload](GameState::WorldState &state, QString *) {
        if (*reject) { state.players["p1"].hp = 0; throw std::runtime_error("detached provider failure"); }
        if (*corruptNativePayload) {
            for (auto it = state.roomTags.begin(); it != state.roomTags.end(); ++it) {
                if (!it.key().startsWith("UseHistory")) continue;
                auto wire = it.value().toMap();
                wire.insert("owner_use", QStringLiteral("true")); // Wrong scalar type must never coerce.
                it.value() = wire;
            }
        }
        return true;
    };
    CHECK(registry.registerProvider(skill, &error));
    GameState::ProviderContract aiProvider;
    aiProvider.id = "fixture.ai"; aiProvider.version = 1;
    aiProvider.audit = "TrustAI has no persistent decision memory; explicit fixture AI confidence lives only in ManagedState.";
    CHECK(registry.registerProvider(aiProvider, &error));
    CHECK(registry.registerNativePackage("fixture.normal", "1", "Normal GameRule, fixed roster, audited physical Slash/Jink/Peach and immutable phase fixture only", &error));
    CHECK(thread->enableManagedTurns(registry, &error));
    CHECK(thread->setManagedLuaProviders("fixture.skill", "fixture.ai"));
    room.beginBattleStatistics(QStringLiteral("managed_test_fixture"));
    const QString statisticsPath = BattleStatistics::defaultDatabasePath();
    const QString statisticsRoot = room.statisticsRootMatchId();
    CHECK(!statisticsRoot.isEmpty() && statisticsRoot == room.gameTimeline().rootGameId());
    CHECK(room.statisticsGeneration() == room.gameTimeline().generation());
    CHECK(!room.statisticsRestorePending());
    int commits = 0;
    int observerThrows = 0;
    QString expectedRestoreAnchor;
    bool throwAfterCommit = false;
    const auto *logicalRoom = &room; const auto *nativeRuntime = room.roomRuntime();
    const auto *gameVM = game.rawState(); const auto *aiVM = ai.rawState();
    thread->setTimelineCommitObserver([&](const GameTimeline &timeline, const GameTimeline::Anchor &anchor) {
        ++commits;
        CHECK(&room == logicalRoom && room.roomRuntime() == nativeRuntime);
        CHECK(game.rawState() == gameVM && ai.rawState() == aiVM);
        CHECK(room.getPlayers().first() == p1 && socket->isConnected());
        CHECK(timeline.generation() == quint64(commits));
        const auto &state = room.managedState().worldStore()->state();
        if (commits <= 2) inventoryCheckpoint(room, state, anchor.kind);
        CHECK(room.getCurrent()->objectName() == state.turn.playerId);
        CHECK(anchor.kind == GameTimeline::AnchorKind::PlayerTurn || anchor.kind == GameTimeline::AnchorKind::FullRound);
        if (!expectedRestoreAnchor.isEmpty()) CHECK(anchor.id == expectedRestoreAnchor);
        for (auto *player : {p1, p2}) {
            const auto saved = state.players.value(player->objectName());
            CHECK(player->getHp() == saved.hp && player->getMaxHp() == saved.maxHp);
            CHECK(player->getHujia() == saved.armor);
            CHECK(player->getMark("fixture_roll") == saved.marks.value("fixture_roll"));
        }
        for (const auto &zone : state.zones) {
            if (zone.kind == GameState::ZoneKind::Draw) CHECK(ids(room.getDrawPile()) == zone.cards);
            if (zone.kind == GameState::ZoneKind::Discard) CHECK(ids(room.getDiscardPile()) == zone.cards);
            if (zone.kind == GameState::ZoneKind::Hand) {
                QStringList hand; for (const auto *card : room.findPlayerByObjectName(zone.ownerId)->getHandcards()) hand << QString::number(card->getId());
                CHECK(hand == zone.cards);
            }
        }
        if (throwAfterCommit) {
            ++observerThrows;
            throw std::runtime_error("simulated statistics consumer failure after committed generation");
        }
        CHECK(room.roomRuntime()->rng().exportState().drawCount == state.gameplayRng.drawCount);
        CHECK(room.roomRuntime()->ai().exportRngState().drawCount == state.aiRng.drawCount);
        CHECK(room.resolutionHistory().snapshot().serialize()
            == state.providers.value("engine.native").state.value("history").toMap());
        for (auto *player : {p1, p2}) {
            for (const auto &instance : player->getSkillInstances()) {
                const QString id = RoomManagedState::skillId(player->objectName(), instance.skillName, instance.instanceID);
                CHECK(player->getSkillInstanceState(instance.skillName, instance.instanceID) == state.skills.value(id).state);
                CHECK(player->getSkillInstanceCorrectStateValue(instance.skillName, instance.instanceID, "amount")
                    == state.skills.value(id).correctState.value("amount"));
            }
            for (const auto &entry : {qMakePair(QStringLiteral("fixture.skill"), QStringLiteral("plays")),
                                      qMakePair(QStringLiteral("fixture.ai"), QStringLiteral("confidence"))}) {
                const auto saved = state.providers.value(entry.first).state.value("owners").toMap()
                    .value("player").toMap().value(player->objectName()).toMap().value(entry.second);
                const QString expected = saved.isValid() ? QString::number(saved.toInt()) : QStringLiteral("nil");
                CHECK(run(entry.first == "fixture.skill" ? game : ai,
                    QString("assert(sgs.ManagedState.get('player','%1','%2') == %3)")
                        .arg(player->objectName(), entry.second, expected)));
            }
        }
    });
    auto step = [&] { error.clear(); CHECK(thread->stepNormalTurn(&rule, &error)); CHECK(RoomTestAccess::playing(room)); };
    step(); CHECK(p1->getMark("fixture_plays") == 1 && room.getCurrent() == p2);
    step(); CHECK(p2->getMark("fixture_plays") == 1 && room.getCurrent() == p1);
    step(); CHECK(room.getTag("TurnLengthCount").toInt() == 2);
    const int replayedRoll = p1->getMark("fixture_roll");
    const auto replayedDraw = room.getDrawPile();
    const auto replayedDiscard = room.getDiscardPile();
    const auto replayedGameRng = room.roomRuntime()->rng().exportState();
    const auto replayedAiRng = room.roomRuntime()->ai().exportRngState();
    // Fixture-only terminal seam: persist the production terminal projection
    // while the native GameSession remains live so the test can exercise undo.
    room.markBattleStatisticsTerminal(QStringLiteral("p1"),
        int(GameSessionController::TerminationCause::GameOver));
    room.freezeBattleStatistics();
    BattleStatistics::waitForPendingWrites();
    checkRoomStatisticsProjection(room, statisticsPath, 0);
    CHECK(thread->requestManagedRestore(GameTimeline::AnchorKind::PlayerTurn, &error));
    step(); CHECK(commits == 1 && room.getCurrent() == p2); // Replay p1's preceding turn, then continue.
    CHECK(room.statisticsGeneration() == 1 && room.statisticsGeneration() == room.gameTimeline().generation());
    CHECK(!room.statisticsRestorePending());
    BattleStatistics::waitForPendingWrites();
    checkInvalidatedProjection(room, statisticsPath, 1);
    CHECK(p1->getMark("fixture_plays") == 2 && p2->getMark("fixture_plays") == 1);
    CHECK(p1->getMark("fixture_roll") == replayedRoll);
    CHECK(room.getDrawPile() == replayedDraw && room.getDiscardPile() == replayedDiscard);
    CHECK(room.roomRuntime()->rng().exportState().drawCount == replayedGameRng.drawCount);
    CHECK(room.roomRuntime()->ai().exportRngState().drawCount == replayedAiRng.drawCount);
    qInfo() << "previous_player_turn: restored and replayed real GameRule turn; generation 1";
    step();
    // This is the continued-play terminal seam for the corrected PlayerTurn
    // branch (T4). Save its effective history before the following round undo.
    room.markBattleStatisticsTerminal(QStringLiteral("p1"),
        int(GameSessionController::TerminationCause::GameOver));
    room.freezeBattleStatistics();
    BattleStatistics::waitForPendingWrites();
    checkRoomStatisticsProjection(room, statisticsPath, 1);
    const auto historyBeforeRoundRestore = room.resolutionHistory().snapshot();
    const auto beforeRoundSerialized = historyBeforeRoundRestore.serialize();
    const int beforeRoundEventCount = beforeRoundSerialized.value(QStringLiteral("events")).toList().size();
    const int beforeRoundFactCount = beforeRoundSerialized.value(QStringLiteral("facts")).toList().size();
    CHECK(thread->requestManagedRestore(GameTimeline::AnchorKind::FullRound, &error));
    step(); CHECK(commits == 2 && room.getCurrent() == p2);
    CHECK(room.statisticsGeneration() == 2 && room.statisticsGeneration() == room.gameTimeline().generation());
    CHECK(!room.statisticsRestorePending());
    BattleStatistics::waitForPendingWrites();
    checkInvalidatedProjection(room, statisticsPath, 2);
    const auto historyAfterRoundRestore = room.resolutionHistory().snapshot();
    const auto afterRoundSerialized = historyAfterRoundRestore.serialize();
    CHECK(afterRoundSerialized != beforeRoundSerialized);
    CHECK(afterRoundSerialized.value(QStringLiteral("events")).toList().size() < beforeRoundEventCount);
    CHECK(afterRoundSerialized.value(QStringLiteral("facts")).toList().size() < beforeRoundFactCount);
    // A fresh terminal projection at the corrected round generation contains
    // the restored T3 history and excludes T4's abandoned future facts.
    room.markBattleStatisticsTerminal(QStringLiteral("p1"),
        int(GameSessionController::TerminationCause::GameOver));
    room.freezeBattleStatistics();
    BattleStatistics::waitForPendingWrites();
    checkRoomStatisticsProjection(room, statisticsPath, 2);
    CHECK(room.getTag("TurnLengthCount").toInt() == 2);
    CHECK(p1->getMark("fixture_plays") == 2 && p2->getMark("fixture_plays") == 1);
    qInfo() << "full_round: restored and replayed real GameRule turn; generation 2";
    step(); step();
    const auto hp = p1->getHp(); const auto draw = room.getDrawPile();
    const auto beforeRejectedRestore = room.managedState().worldStore()->capture();
    const auto beforeRejectedRevision = room.managedState().worldStore()->revision();
    const auto beforeRejectedTimeline = room.managedState().worldStore()->timeline().revision();
    *reject = true;
    CHECK(thread->requestManagedRestore(GameTimeline::AnchorKind::PlayerTurn, &error));
    CHECK(!thread->stepNormalTurn(&rule, &error));
    CHECK(p1->getHp() == hp && room.getDrawPile() == draw && commits == 2);
    CHECK(room.managedState().worldStore()->capture() == beforeRejectedRestore);
    CHECK(room.managedState().worldStore()->revision() == beforeRejectedRevision);
    CHECK(room.managedState().worldStore()->timeline().revision() == beforeRejectedTimeline);
    *reject = false;
    *corruptNativePayload = true;
    CHECK(thread->requestManagedRestore(GameTimeline::AnchorKind::PlayerTurn, &error));
    CHECK(!thread->stepNormalTurn(&rule, &error));
    CHECK(room.managedState().worldStore()->capture() == beforeRejectedRestore);
    CHECK(room.managedState().worldStore()->revision() == beforeRejectedRevision);
    CHECK(room.managedState().worldStore()->timeline().revision() == beforeRejectedTimeline);
    CHECK(p1->getHp() == hp && room.getDrawPile() == draw && commits == 2);
    *corruptNativePayload = false;
    step();
    CHECK(thread->requestManagedRestore(GameTimeline::AnchorKind::PlayerTurn, &error));
    step(); CHECK(commits == 3);
    CHECK(room.statisticsGeneration() == 3 && room.statisticsGeneration() == room.gameTimeline().generation());
    CHECK(!room.statisticsRestorePending());
    BattleStatistics::waitForPendingWrites();
    checkInvalidatedProjection(room, statisticsPath, 3);
    for (auto it = room.managedState().worldStore()->timeline().anchors().crbegin();
         it != room.managedState().worldStore()->timeline().anchors().crend(); ++it) {
        if (it->kind == GameTimeline::AnchorKind::PlayerTurn) { expectedRestoreAnchor = it->id; break; }
    }
    room.setTag("fixture.request_restore", true);
    step(); CHECK(commits == 3); // Accepted inside a real phase; native stack must unwind first.
    throwAfterCommit = true;
    step(); CHECK(commits == 4); // Frozen previous-player target survives the deferred boundary.
    CHECK(room.statisticsGeneration() == 4 && room.statisticsGeneration() == room.gameTimeline().generation());
    CHECK(!room.statisticsRestorePending());
    BattleStatistics::waitForPendingWrites();
    checkInvalidatedProjection(room, statisticsPath, 4);
    CHECK(room.managedState().worldStore()->timeline().generation() == 4);
    const auto &records = room.managedState().worldStore()->timeline().records();
    bool accepted = false; for (const auto &record : records) if (record.kind == "accepted_decision") accepted = true;
    CHECK(accepted);
    CHECK(socket->sent > 0);

    // A restore accepted before an early executor precondition failure must be
    // consumed; the next ordinary turn must not unexpectedly publish it.
    const int commitsBeforeEarlyFailure = commits;
    const quint64 generationBeforeEarlyFailure = room.managedState().worldStore()->timeline().generation();
    error.clear();
    CHECK(thread->requestManagedRestore(GameTimeline::AnchorKind::PlayerTurn, &error));
    CHECK(RoomTestAccess::managedRestorePending(*thread));
    CHECK(!thread->stepNormalTurn(nullptr, &error));
    CHECK(!RoomTestAccess::managedRestorePending(*thread));
    CHECK(room.managedState().worldStore()->timeline().generation() == generationBeforeEarlyFailure);
    CHECK(commits == commitsBeforeEarlyFailure);
    step();
    CHECK(room.managedState().worldStore()->timeline().generation() == generationBeforeEarlyFailure);
    CHECK(commits == commitsBeforeEarlyFailure);

    // A second queued request must not replace the first accepted target.
    error.clear();
    CHECK(thread->requestManagedRestore(GameTimeline::AnchorKind::PlayerTurn, &error));
    const QString firstPendingAnchor = RoomTestAccess::managedRestoreAnchor(*thread);
    CHECK(!firstPendingAnchor.isEmpty());
    error.clear();
    CHECK(!thread->requestManagedRestore(GameTimeline::AnchorKind::FullRound, &error));
    CHECK(error.contains(QStringLiteral("already pending")));
    CHECK(RoomTestAccess::managedRestorePending(*thread));
    CHECK(RoomTestAccess::managedRestoreAnchor(*thread) == firstPendingAnchor);
    expectedRestoreAnchor = firstPendingAnchor;

    // Observer exceptions happen after publication. Restore-only must leave a
    // paused restored boundary that can be rewound again, then stepped forward.
    const int beforeRestoreOnly = commits;
    const int throwsBeforeRestoreOnly = observerThrows;
    CHECK(thread->stepNormalTurn(&rule, &error, false));
    CHECK(commits == beforeRestoreOnly + 1);
    CHECK(observerThrows == throwsBeforeRestoreOnly + 1);
    CHECK(room.managedState().worldStore()->timeline().generation() == quint64(commits));
    CHECK(room.statisticsGeneration() == room.gameTimeline().generation());
    CHECK(!room.statisticsRestorePending());
    BattleStatistics::waitForPendingWrites();
    checkInvalidatedProjection(room, statisticsPath, room.gameTimeline().generation());
    CHECK(!RoomTestAccess::managedRestorePending(*thread));
    const QString secondPendingAnchor = room.managedState().worldStore()->timeline().previousPlayerTurn().id;
    CHECK(!secondPendingAnchor.isEmpty());
    expectedRestoreAnchor = secondPendingAnchor;
    error.clear();
    CHECK(thread->requestManagedRestore(GameTimeline::AnchorKind::PlayerTurn, &error));
    CHECK(RoomTestAccess::managedRestoreAnchor(*thread) == secondPendingAnchor);
    const int throwsBeforeSecondRestoreOnly = observerThrows;
    CHECK(thread->stepNormalTurn(&rule, &error, false));
    CHECK(commits == beforeRestoreOnly + 2);
    CHECK(observerThrows == throwsBeforeSecondRestoreOnly + 1);
    CHECK(room.managedState().worldStore()->timeline().generation() == quint64(commits));
    CHECK(room.statisticsGeneration() == room.gameTimeline().generation());
    CHECK(!room.statisticsRestorePending());
    BattleStatistics::waitForPendingWrites();
    checkInvalidatedProjection(room, statisticsPath, room.gameTimeline().generation());

    ServerPlayer *restoredPlayer = room.getCurrent();
    const int restoredPlays = restoredPlayer->getMark("fixture_plays");
    const int otherPlays = restoredPlayer == p1 ? p2->getMark("fixture_plays") : p1->getMark("fixture_plays");
    const quint64 restoredGeneration = room.managedState().worldStore()->timeline().generation();
    step();
    CHECK(restoredPlayer->getMark("fixture_plays") == restoredPlays + 1);
    CHECK((restoredPlayer == p1 ? p2->getMark("fixture_plays") : p1->getMark("fixture_plays")) == otherPlays);
    CHECK(room.managedState().worldStore()->timeline().generation() == restoredGeneration);
    CHECK(commits == beforeRestoreOnly + 2);
    CHECK(observerThrows == throwsBeforeSecondRestoreOnly + 1);

    // Publish a real terminal projection, then hold the actual per-root journal
    // lock across another restore. The game generation advances, but statistics
    // remain pending and stale summaries are quarantined until an explicit retry.
    room.markBattleStatisticsTerminal(QStringLiteral("p1"),
        int(GameSessionController::TerminationCause::GameOver));
    room.freezeBattleStatistics();
    BattleStatistics::waitForPendingWrites();
    const quint64 savedGeneration = room.gameTimeline().generation();
    checkRoomStatisticsProjection(room, statisticsPath, savedGeneration);

    QLockFile journalLock(statisticsTimelineLockPath(statisticsPath, statisticsRoot));
    CHECK(journalLock.tryLock(0));
    error.clear();
    CHECK(thread->requestManagedRestore(GameTimeline::AnchorKind::PlayerTurn, &error));
    CHECK(thread->stepNormalTurn(&rule, &error, false));
    const quint64 failedNotificationGeneration = room.gameTimeline().generation();
    CHECK(failedNotificationGeneration == savedGeneration + 1);
    CHECK(room.statisticsGeneration() == failedNotificationGeneration);
    CHECK(room.statisticsRestorePending());
    BattleStatistics::waitForPendingWrites();
    const auto staleProjection = storedMatchProjection(statisticsPath, statisticsRoot);
    CHECK(staleProjection.found && staleProjection.generation == savedGeneration && staleProjection.terminal);
    QString quarantinedError;
    CHECK(BattleStatistics::readSummaries(statisticsPath, QStringLiteral("excluded"), &quarantinedError).isEmpty());
    CHECK(quarantinedError.isEmpty());

    journalLock.unlock();
    CHECK(room.retryStatisticsTimelineRestore());
    CHECK(room.statisticsGeneration() == failedNotificationGeneration);
    CHECK(!room.statisticsRestorePending());
    BattleStatistics::waitForPendingWrites();
    checkInvalidatedProjection(room, statisticsPath, failedNotificationGeneration);

    ServerPlayer *afterRetryPlayer = room.getCurrent();
    const int playsBeforeRetryStep = afterRetryPlayer->getMark("fixture_plays");
    step();
    CHECK(afterRetryPlayer->getMark("fixture_plays") == playsBeforeRetryStep + 1);
    CHECK(room.statisticsGeneration() == failedNotificationGeneration);
    CHECK(room.gameTimeline().generation() == failedNotificationGeneration);

    // The final saved bytes and metrics must describe only the current Room
    // receipt after retry; the abandoned future's history is not accumulated.
    room.markBattleStatisticsTerminal(QStringLiteral("p1"),
        int(GameSessionController::TerminationCause::GameOver));
    room.freezeBattleStatistics();
    BattleStatistics::waitForPendingWrites();
    checkRoomStatisticsProjection(room, statisticsPath, failedNotificationGeneration);
}
}

int main(int argc, char **argv)
{
    QTemporaryDir dataHome;
    CHECK(dataHome.isValid());
    qputenv("XDG_DATA_HOME", dataHome.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", QDir(dataHome.path()).filePath(QStringLiteral("config")).toUtf8());
    qputenv("XDG_CACHE_HOME", QDir(dataHome.path()).filePath(QStringLiteral("cache")).toUtf8());
    QCoreApplication app(argc, argv);
    CHECK(QFileInfo(BattleStatistics::defaultDatabasePath()).absoluteFilePath().startsWith(
        QFileInfo(dataHome.path()).absoluteFilePath()));
    CHECK(QSanRuntimePaths::resolve(app.arguments(), &error));
    CHECK(EngineBootstrap::initialize(false, &error));
    QObject::disconnect(&app, SIGNAL(aboutToQuit()), Sanguosha, SLOT(deleteLater()));
    Config.init(); Config.EnableAI = false; Config.Enable2ndGeneral = false; Config.EnableHegemony = false;
    Config.AIDelay = 0; Config.OriginAIDelay = 0;
    runningGame();
    BattleStatistics::waitForPendingWrites(); // Room has queued its lifecycle close in its destructor.
    EngineBootstrap::shutdown();
    qInfo() << "room-managed-turn:" << checks << "running GameRule checks passed";
}
