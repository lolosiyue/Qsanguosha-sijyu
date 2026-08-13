#include "engine-bootstrap.h"
#include "engine.h"
#include "ai-runtime.h"
#include "general.h"
#include "lua-runtime.h"
#include "package.h"
#include "room.h"
#include "room-runtime.h"
#include "roomthread1v1.h"
#include "serverplayer.h"
#include "settings.h"

#include "lua.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QPointer>
#include <QSemaphore>
#include <QThread>

#include <memory>

struct RoomTestAccess
{
    static ServerPlayer *addOnlinePlayer(Room &room)
    {
        ServerPlayer *player = new ServerPlayer(&room);
        player->setObjectName(QStringLiteral("waiting-player"));
        player->setState(QStringLiteral("online"));
        player->drainAllLocks();
        player->releaseLock(ServerPlayer::SEMA_MUTEX);
        room.m_players << player;
        return player;
    }

    static ServerPlayer *addRobotPlayer(Room &room)
    {
        ServerPlayer *player = new ServerPlayer(&room);
        player->setObjectName(QStringLiteral("shadow-robot"));
        player->setState(QStringLiteral("robot"));
        room.m_players << player;
        return player;
    }

    static void attach1v1Thread(Room &room, RoomThread1v1 *thread)
    {
        room.thread_1v1 = thread;
    }

    static AIRequest makeRequest(Room &room, ServerPlayer *player,
                                 AIRequest::DecisionKind kind)
    {
        return room.makeAIRequest(player, kind, CardUseStruct::CARD_USE_REASON_PLAY,
                                  QString(), QString(), Card::MethodUse);
    }

    static bool decide(Room &room, ServerPlayer *player, const AIRequest &request,
                       CardUseStruct &use)
    {
        return room.decideAiAction(player, request, use);
    }

    static bool applyResult(Room &room, ServerPlayer *player, const AIRequest &request,
                            const AIResult &result, CardUseStruct &use)
    {
        return room.applyAIResult(player, request, result, use);
    }
};

class ScopedConfigValue
{
public:
    ScopedConfigValue(const QString &key, const QVariant &value)
        : m_key(key), m_existed(Config.contains(key)), m_previous(Config.value(key))
    {
        Config.setValue(key, value);
    }

    ~ScopedConfigValue()
    {
        if (m_existed)
            Config.setValue(m_key, m_previous);
        else
            Config.remove(m_key);
    }

private:
    QString m_key;
    bool m_existed;
    QVariant m_previous;
};

class InterruptibleRoomThread1v1 : public RoomThread1v1
{
public:
    InterruptibleRoomThread1v1(Room *room, ServerPlayer *player, QSemaphore &started)
        : RoomThread1v1(room), m_room(room), m_player(player), m_started(started)
    {
    }

protected:
    void run() override
    {
        m_started.release();
        m_room->getResult(m_player, 600000);
    }

private:
    Room *m_room;
    ServerPlayer *m_player;
    QSemaphore &m_started;
};

static LuaFunction createIncrementCallback(Room &room)
{
    LuaRuntime::Binding luaBinding(room.roomRuntime()->lua());
    EngineRuntimeContextScope contextScope(*Sanguosha, &room);
    if (Sanguosha->currentRoom() != &room)
        return LuaFunction();
    lua_State *L = room.getLuaState();
    if (!L || luaL_dostring(L, "return function(value) return value + 1 end") != 0)
        return LuaFunction();
    return LuaFunction(L, luaL_ref(L, LUA_REGISTRYINDEX));
}

static bool invokeIncrement(Room &room, const LuaFunction &callback, int value)
{
    LuaRuntime::Binding luaBinding(room.roomRuntime()->lua());
    EngineRuntimeContextScope contextScope(*Sanguosha, &room);
    if (Sanguosha->currentRoom() != &room)
        return false;
    lua_State *L = room.getLuaState();
    if (!callback.push(L))
        return false;
    lua_pushinteger(L, value);
    if (lua_pcall(L, 1, 1, 0) != 0)
        return false;
    const bool valid = lua_tointeger(L, -1) == value + 1;
    lua_pop(L, 1);
    return valid;
}

static bool installDefinitionFixture(Room &room)
{
    Package *package = new Package(QStringLiteral("runtime_isolation"), Package::CardPack);
    new General(package, QStringLiteral("runtime_isolation_general"), QStringLiteral("wei"));
    Card *card = Sanguosha->cloneCard(QStringLiteral("slash"));
    if (!card) {
        delete package;
        return false;
    }
    card->setObjectName(QStringLiteral("runtime_isolation_card"));
    card->setParent(package);
    room.roomRuntime()->addPackage(package);
    return true;
}

static bool definitionsAndGlobalsAreRoomLocal(Room &first, Room &second)
{
    const General *firstGeneral = nullptr;
    const General *secondGeneral = nullptr;
    Package *firstPackage = nullptr;
    Package *secondPackage = nullptr;
    QList<int> firstCardIds;
    QList<int> secondCardIds;

    {
        LuaRuntime::Binding luaBinding(first.roomRuntime()->lua());
        EngineRuntimeContextScope contextScope(*Sanguosha, &first);
        lua_State *L = first.getLuaState();
        lua_pushliteral(L, "first");
        lua_setglobal(L, "room_runtime_marker");
        if (!installDefinitionFixture(first))
            return false;
        firstGeneral = Sanguosha->getGeneral(QStringLiteral("runtime_isolation_general"));
        firstPackage = Sanguosha->getPackage(QStringLiteral("runtime_isolation"));
        if (!firstPackage)
            return false;
        Sanguosha->addTranslationEntry(QStringLiteral("runtime_a"), QStringLiteral("A1"));
        Sanguosha->addTranslationEntry(QStringLiteral("runtime_a"), QStringLiteral("A2"));
        Sanguosha->addTranslationEntry(QStringLiteral("runtime_b"), QStringLiteral("B"));
        if (Sanguosha->translate(QStringLiteral("runtime_a\\runtime_b")) != QStringLiteral("A2B")
            || Sanguosha->translate(QStringLiteral("runtime_a"), true) != QStringLiteral("A1"))
            return false;
        foreach (const Package *package, first.roomRuntime()->packages()) {
            foreach (Card *card, package->findChildren<Card *>()) {
                if (firstCardIds.contains(card->getId()) || Sanguosha->getEngineCard(card->getId()) != card)
                    return false;
                firstCardIds << card->getId();
            }
        }
    }
    {
        LuaRuntime::Binding luaBinding(second.roomRuntime()->lua());
        EngineRuntimeContextScope contextScope(*Sanguosha, &second);
        lua_State *L = second.getLuaState();
        lua_getglobal(L, "room_runtime_marker");
        const bool markerIsLocal = lua_isnil(L, -1);
        lua_pop(L, 1);
        if (!markerIsLocal)
            return false;
        if (!installDefinitionFixture(second))
            return false;
        secondGeneral = Sanguosha->getGeneral(QStringLiteral("runtime_isolation_general"));
        secondPackage = Sanguosha->getPackage(QStringLiteral("runtime_isolation"));
        if (!secondPackage)
            return false;
        if (Sanguosha->translate(QStringLiteral("runtime_a")) != QStringLiteral("runtime_a"))
            return false;
        foreach (const Package *package, second.roomRuntime()->packages()) {
            foreach (Card *card, package->findChildren<Card *>()) {
                if (secondCardIds.contains(card->getId()) || Sanguosha->getEngineCard(card->getId()) != card)
                    return false;
                secondCardIds << card->getId();
            }
        }
    }

    return firstGeneral && secondGeneral && firstGeneral != secondGeneral
        && firstPackage && secondPackage && firstPackage != secondPackage
        && firstCardIds == secondCardIds;
}

static bool callbacksRunConcurrentlyInOwningRooms(Room &first, Room &second,
                                                  const LuaFunction &firstCallback,
                                                  const LuaFunction &secondCallback)
{
    QSemaphore ready;
    QSemaphore start;
    bool firstValid = false;
    bool secondValid = false;

    QThread *firstThread = QThread::create([&]() {
        ready.release();
        start.acquire();
        firstValid = Sanguosha->currentRoom() == nullptr;
        for (int i = 0; firstValid && i < 100; ++i)
            firstValid = invokeIncrement(first, firstCallback, i)
                && Sanguosha->currentRoom() == nullptr;
    });
    QThread *secondThread = QThread::create([&]() {
        ready.release();
        start.acquire();
        secondValid = Sanguosha->currentRoom() == nullptr;
        for (int i = 0; secondValid && i < 100; ++i)
            secondValid = invokeIncrement(second, secondCallback, i)
                && Sanguosha->currentRoom() == nullptr;
    });

    firstThread->start();
    secondThread->start();
    ready.acquire(2);
    start.release(2);
    firstThread->wait();
    secondThread->wait();
    delete firstThread;
    delete secondThread;
    return firstValid && secondValid;
}

static bool gameRuntimeDoesNotWaitForBootstrapMutex(Room &room)
{
    QSemaphore entered;
    QSemaphore completed;
    Sanguosha->getLuaMutex().lock();
    QThread *thread = QThread::create([&]() {
        LuaRuntime::Binding luaBinding(room.roomRuntime()->lua());
        EngineRuntimeContextScope contextScope(*Sanguosha, &room);
        entered.release();
        lua_State *state = room.roomRuntime()->lua().state();
        lua_getglobal(state, "sgs");
        lua_pop(state, 1);
        completed.release();
    });
    thread->start();
    entered.acquire();
    const bool bypassed = completed.tryAcquire(1, 2000);
    Sanguosha->getLuaMutex().unlock();
    thread->wait();
    delete thread;
    return bypassed;
}

static bool roomDestructionJoinsSpecializedWorker()
{
    QSemaphore started;
    Room *room = new Room(nullptr, QStringLiteral("02_1v1"));
    ServerPlayer *player = RoomTestAccess::addOnlinePlayer(*room);
    QPointer<RoomThread1v1> worker = new InterruptibleRoomThread1v1(room, player, started);
    RoomTestAccess::attach1v1Thread(*room, worker);
    worker->start();
    started.acquire();
    delete room;
    return worker.isNull();
}

static bool aiStatesAreIsolated(Room &first, Room &second)
{
    lua_State *firstGameState = first.roomRuntime()->lua().rawState();
    lua_State *secondGameState = second.roomRuntime()->lua().rawState();
    lua_State *firstAiState = first.roomRuntime()->ai().lua().rawState();
    lua_State *secondAiState = second.roomRuntime()->ai().lua().rawState();
    return firstGameState && secondGameState && firstAiState && secondAiState
        && firstGameState != firstAiState && secondGameState != secondAiState
        && firstAiState != secondAiState && firstAiState != secondGameState
        && secondAiState != firstGameState;
}

static bool aiSandboxBlocksHostLibraries(Room &room)
{
    LuaRuntime::Binding luaBinding(room.roomRuntime()->ai().lua());
    lua_State *L = room.roomRuntime()->ai().lua().state();
    const char *blockedGlobals[] = {
        "sgs", "io", "os", "package", "coroutine", "require", "dofile", "loadfile",
        "load", "loadstring", "collectgarbage", nullptr
    };
    for (const char **name = blockedGlobals; *name; ++name) {
        lua_getglobal(L, *name);
        const bool blocked = lua_isnil(L, -1);
        lua_pop(L, 1);
        if (!blocked)
            return false;
    }
    lua_getglobal(L, "ai_data");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    lua_getfield(L, -1, "read");
    const bool hasRead = lua_isfunction(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, -1, "write");
    const bool hasWrite = lua_isfunction(L, -1);
    lua_pop(L, 2);
    return hasRead && hasWrite;
}

static bool aiRoutesSelectExactDefaultAndFreeze()
{
    AiRouteRegistry routes;
    if (routes.routeFor(AIRequest::Activate) != AiRouteShadow
        || !routes.setCallbackRoute(QStringLiteral("ask_for_card"), QString(), AiRouteIsolated)
        || !routes.setCallbackRoute(QStringLiteral("ask_for_card"), QStringLiteral("special"), AiRouteShadow)
        || routes.routeFor(AIRequest::UseCard, QStringLiteral("ask_for_card"), QStringLiteral("special")) != AiRouteShadow
        || routes.routeFor(AIRequest::UseCard, QStringLiteral("ask_for_card"), QStringLiteral("ordinary")) != AiRouteIsolated) {
        return false;
    }
    routes.freeze();
    return !routes.setDecisionRoute(AIRequest::UseCard, AiRouteShadow)
        && !routes.setCallbackRoute(QStringLiteral("ask_for_card"), QStringLiteral("late"), AiRouteShadow)
        && routes.routeFor(AIRequest::UseCard, QStringLiteral("ask_for_card"), QStringLiteral("special")) == AiRouteShadow;
}

static bool isolatedInitializationIsBudgeted()
{
    Room room(nullptr, QStringLiteral("02_1v1"));
    room.roomRuntime()->ai().shutdown();
    ScopedConfigValue scripts(QStringLiteral("AiIsolatedScripts"),
                              QStringList({QStringLiteral("instruction-limit-test.lua")}));
    ScopedConfigValue budget(QStringLiteral("AiLuaInitializationInstructionBudget"), 10000);
    QElapsedTimer timer;
    timer.start();
    QString error;
    const bool initialized = room.roomRuntime()->ai().initialize(&error);
    return !initialized && timer.elapsed() < 5000
        && room.roomRuntime()->lua().rawState()
        && !room.roomRuntime()->ai().lua().rawState()
        && error.contains(QStringLiteral("instruction limit"));
}

static bool productionIsolatedScriptAndShadowAudit(Room &room)
{
    AIRequest probe;
    probe.kind = AIRequest::UseCard;
    probe.viewerObjectName = QStringLiteral("production-probe");
    const AIResult loadedHandler = room.roomRuntime()->ai().decideShadow(probe);
    if (!loadedHandler.handled || loadedHandler.kind != AIResult::Pass
        || !loadedHandler.errorCode.isEmpty())
        return false;

    ServerPlayer *player = RoomTestAccess::addRobotPlayer(room);
    std::unique_ptr<TrustAI> ai(new TrustAI(player));
    player->setAI(ai.get());

    const quint64 revision = room.roomRuntime()->stateRevision();
    const AIRequest first = RoomTestAccess::makeRequest(room, player, AIRequest::UseCard);
    const AIRequest second = RoomTestAccess::makeRequest(room, player, AIRequest::UseCard);
    if (first.stateRevision != revision || second.stateRevision != revision
        || room.roomRuntime()->stateRevision() != revision) {
        player->setAI(nullptr);
        return false;
    }

    CardUseStruct use;
    const int auditCount = room.roomRuntime()->ai().shadowAudits().size();
    const bool decided = RoomTestAccess::decide(room, player, second, use);
    player->setAI(nullptr);
    const QList<AiShadowAuditEntry> &audits = room.roomRuntime()->ai().shadowAudits();
    if (!decided || use.card || audits.size() != auditCount + 1)
        return false;
    const AiShadowAuditEntry &audit = audits.last();
    return audit.decisionId == second.decisionId
        && audit.callbackName == QStringLiteral("askForUseCard")
        && audit.officialResult.handled && audit.shadowResult.handled
        && !audit.differs;
}

static bool aiWorldViewIsScopedAndRevisioned()
{
    std::unique_ptr<Room> room(new Room(nullptr, QStringLiteral("02_1v1")));
    EngineRuntimeContextScope contextScope(*Sanguosha, room.get());
    room->roomRuntime()->state().reset();
    ServerPlayer *viewer = RoomTestAccess::addRobotPlayer(*room);
    viewer->setObjectName(QStringLiteral("world-viewer"));
    viewer->setSeat(1);
    viewer->setMaxHp(4);
    viewer->setHp(3);
    viewer->setPhase(Player::Play);

    ServerPlayer *other = RoomTestAccess::addRobotPlayer(*room);
    other->setObjectName(QStringLiteral("world-other"));
    other->setSeat(2);
    other->setMaxHp(4);
    other->setHp(4);
    room->setCurrent(viewer);

    if (Sanguosha->getCardCount() < 2)
        return false;
    viewer->addCard(0, Player::PlaceHand);
    other->addCard(1, Player::PlaceHand);
    room->setPlayerMark(other, QStringLiteral("public_mark"), 2);
    room->setPlayerMark(other, QStringLiteral("private_mark"), 3,
                        QList<ServerPlayer *>() << other);
    other->setMark(QStringLiteral("server_internal_mark"), 4);

    const quint64 revision = room->roomRuntime()->stateRevision();
    const AIRequest request = RoomTestAccess::makeRequest(*room, viewer, AIRequest::UseCard);
    if (room->roomRuntime()->stateRevision() != revision
        || request.stateRevision != revision || request.worldView.revision != revision
        || request.worldView.self.objectName != viewer->objectName()
        || request.worldView.currentPlayer != viewer->objectName()
        || request.worldView.currentPhase != int(Player::Play)
        || request.worldView.handCards.size() != 1
        || request.worldView.handCards.first().cardId != 0
        || request.worldView.players.size() != 1)
        return false;

    const AIPlayerView &otherView = request.worldView.players.first();
    if (otherView.objectName != other->objectName() || otherView.handcardCount != 1
        || otherView.publicMarks.value(QStringLiteral("public_mark")) != 2
        || otherView.publicMarks.contains(QStringLiteral("private_mark"))
        || otherView.publicMarks.contains(QStringLiteral("server_internal_mark")))
        return false;

    AIResult result;
    result.kind = AIResult::Pass;
    result.handled = true;
    result.decisionId = request.decisionId;
    result.stateRevision = request.stateRevision;
    CardUseStruct use;
    if (!RoomTestAccess::applyResult(*room, viewer, request, result, use))
        return false;

    const quint64 noOpRevision = room->roomRuntime()->stateRevision();
    room->setCurrent(viewer);
    room->setPlayerMark(other, QStringLiteral("public_mark"), 2);
    if (room->roomRuntime()->stateRevision() != noOpRevision)
        return false;

    viewer->setFlags(QStringLiteral("AI_TestScratch"));
    viewer->setFlags(QStringLiteral("-AI_TestScratch"));
    if (room->roomRuntime()->stateRevision() != noOpRevision)
        return false;

    viewer->setHp(viewer->getHp());
    if (room->roomRuntime()->stateRevision() != revision)
        return false;
    viewer->setHp(viewer->getHp() - 1);
    if (room->roomRuntime()->stateRevision() <= revision
        || RoomTestAccess::applyResult(*room, viewer, request, result, use))
        return false;

    const quint64 limitationRevision = room->roomRuntime()->stateRevision();
    viewer->setCardLimitation(QStringLiteral("use"), QStringLiteral("."),
                              QStringLiteral("ai-world-view-test"), false);
    if (room->roomRuntime()->stateRevision() <= limitationRevision)
        return false;

    const quint64 skillRevision = room->roomRuntime()->stateRevision();
    viewer->createSkillInstance(QStringLiteral("ai_world_view_test_skill"), SourceAcquired, true);
    if (room->roomRuntime()->stateRevision() <= skillRevision)
        return false;

    const AIRequest fresh = RoomTestAccess::makeRequest(*room, viewer, AIRequest::UseCard);
    result.decisionId = fresh.decisionId;
    result.stateRevision = fresh.stateRevision;
    return fresh.worldView.revision == fresh.stateRevision
        && RoomTestAccess::applyResult(*room, viewer, fresh, result, use);
}

static bool shadowAuditPayloadIsBounded(Room &room)
{
    AIRequest request;
    request.decisionId = 9001;
    AIResult official;
    official.handled = true;
    official.action.legacyCardString = QString(200000, QLatin1Char('x'));
    official.action.userString = QString(200000, QLatin1Char('y'));
    for (int index = 0; index < 5000; ++index)
        official.action.selectedCardIds << index;
    for (int index = 0; index < 500; ++index)
        official.action.selectedTargetNames << QString(1000, QLatin1Char('t'));
    AIResult shadow;
    room.roomRuntime()->ai().recordShadowAudit(request, QStringLiteral("askForUseCard"),
                                               QString(), official, shadow);
    const AiShadowAuditEntry &entry = room.roomRuntime()->ai().shadowAudits().last();
    return entry.officialResult.action.legacyCardString.size() < 128
        && entry.officialResult.action.userString.size() < 128
        && entry.officialResult.action.selectedCardIds.size() == 32
        && entry.officialResult.action.selectedTargetNames.size() == 32
        && entry.officialResult.action.selectedTargetNames.first().size() < 128;
}

static bool ownedAiProxyUsesValueLifetime()
{
    QPointer<Card> pointer = new ActiveSkillCard;
    {
        CardUseStruct first;
        first.setOwnedCard(pointer.data());
        {
            CardUseStruct copy = first;
            if (pointer.isNull() || copy.card != pointer.data())
                return false;
        }
        if (pointer.isNull())
            return false;
    }
    return pointer.isNull();
}

static bool loadAiShadowHandler(Room &room)
{
    LuaRuntime::Binding luaBinding(room.roomRuntime()->ai().lua());
    lua_State *L = room.roomRuntime()->ai().lua().state();
    return luaL_dostring(L,
        "ai_register_handler('use_card', function(request) "
        "if request.pattern == 'pass' then return { kind = 'pass' } end "
        "if request.pattern == 'world' then local world = request.world_view; "
        "if world and world.revision == request.state_revision "
        "and world.self.object_name == request.viewer "
        "and world.hand_cards[1].id == 7 and world.players[1].public_marks.ready == 2 "
        "then return { kind = 'pass' } end; return { kind = 'use_card' } end "
        "if request.pattern == 'structured' then return { kind = 'use_card' } end "
        "if request.pattern == 'huge' then return { kind = 'use_card', cards = {math.huge} } end "
        "if request.pattern == 'too_many' then local cards = {}; "
        "for i = 1, 2049 do cards[i] = i end; return { kind = 'use_card', cards = cards } end "
        "return { kind = 'use_card', card = '@test=4', cards = {4, 17}, "
        "targets = {'target_a', 'target_b'}, user_string = 'metadata' } end)") == 0;
}

static bool aiShadowParsesPassAndUseCard(Room &room)
{
    if (!loadAiShadowHandler(room))
        return false;

    AIRequest request;
    request.kind = AIRequest::UseCard;
    request.viewerObjectName = QStringLiteral("ai_player");
    request.pattern = QStringLiteral("pass");
    const AIResult pass = room.roomRuntime()->ai().decideShadow(request);
    if (!pass.handled || pass.kind != AIResult::Pass || !pass.errorCode.isEmpty())
        return false;

    request.stateRevision = 42;
    request.worldView.revision = 42;
    request.worldView.self.objectName = request.viewerObjectName;
    AICardView handCard;
    handCard.cardId = 7;
    request.worldView.handCards << handCard;
    AIPlayerView otherPlayer;
    otherPlayer.objectName = QStringLiteral("other");
    otherPlayer.publicMarks.insert(QStringLiteral("ready"), 2);
    request.worldView.players << otherPlayer;
    request.pattern = QStringLiteral("world");
    const AIResult world = room.roomRuntime()->ai().decideShadow(request);
    if (!world.handled || world.kind != AIResult::Pass || !world.errorCode.isEmpty())
        return false;

    request.pattern = QStringLiteral("use");
    const AIResult useCard = room.roomRuntime()->ai().decideShadow(request);
    if (!useCard.handled || useCard.kind != AIResult::UseCard || !useCard.errorCode.isEmpty()
        || useCard.action.legacyCardString != QStringLiteral("@test=4")
        || useCard.action.selectedCardIds != QList<int>({4, 17})
        || useCard.action.selectedTargetNames != QStringList({QStringLiteral("target_a"), QStringLiteral("target_b")})
        || useCard.action.userString != QStringLiteral("metadata"))
        return false;

    request.pattern = QStringLiteral("structured");
    request.hasSkillActionContext = true;
    request.skillActionContext.activationRef = SkillInstanceRef(
        QStringLiteral("owner"), SkillInstanceKey(QStringLiteral("skill"), 1));
    request.skillActionContext.sourceRef = request.skillActionContext.activationRef;
    const AIResult structured = room.roomRuntime()->ai().decideShadow(request);
    if (!structured.handled || structured.kind != AIResult::UseCard
        || !structured.errorCode.isEmpty() || !structured.action.hasSkillActionContext
        || structured.action.skillActionContext.activationRef
            != request.skillActionContext.activationRef)
        return false;

    request.pattern = QStringLiteral("huge");
    const AIResult invalidNumber = room.roomRuntime()->ai().decideShadow(request);
    if (invalidNumber.errorCode != QStringLiteral("AI_INVALID_RESULT"))
        return false;
    request.pattern = QStringLiteral("too_many");
    const AIResult oversized = room.roomRuntime()->ai().decideShadow(request);
    return oversized.errorCode == QStringLiteral("AI_INVALID_RESULT");
}

static bool aiInstructionLimitRebuildsRuntime(Room &room)
{
    const quint64 generation = room.roomRuntime()->ai().lua().generation();
    {
        LuaRuntime::Binding luaBinding(room.roomRuntime()->ai().lua());
        lua_State *L = room.roomRuntime()->ai().lua().state();
        if (luaL_dostring(L,
            "ai_register_handler('activate', function(request) while true do end end)") != 0)
            return false;
    }
    AIRequest request;
    request.kind = AIRequest::Activate;
    request.viewerObjectName = QStringLiteral("ai_player");
    const AIResult result = room.roomRuntime()->ai().decideShadow(request);
    return result.errorCode == QStringLiteral("AI_INSTRUCTION_LIMIT")
        && room.roomRuntime()->ai().lua().rawState()
        && room.roomRuntime()->ai().lua().generation() > generation;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "Engine bootstrap failed:" << error;
        return 1;
    }
    const QString bootstrapLuaPackages = Config.value(QStringLiteral("LuaPackages")).toString();
    if (!isolatedInitializationIsBudgeted()) {
        qCritical() << "Isolated AI initialization did not fail within its instruction budget";
        return 16;
    }
    ScopedConfigValue isolatedScripts(QStringLiteral("AiIsolatedScripts"),
                                      QStringList({QStringLiteral("pass-ai.lua")}));

    std::unique_ptr<Room> first(new Room(nullptr, QStringLiteral("02_1v1")));
    std::unique_ptr<Room> second(new Room(nullptr, QStringLiteral("02_1v1")));
    if (Config.value(QStringLiteral("LuaPackages")).toString() != bootstrapLuaPackages
        || !first->roomRuntime()->definitionsLoaded()
        || !second->roomRuntime()->definitionsLoaded()) {
        qCritical() << "Room definition loading mutated bootstrap configuration";
        return 2;
    }
    lua_State *bootstrapState = Sanguosha->getLuaState();
    lua_State *firstState = first->roomRuntime()->lua().rawState();
    lua_State *secondState = second->roomRuntime()->lua().rawState();
    if (!firstState || !secondState || firstState == secondState
        || firstState == bootstrapState || secondState == bootstrapState) {
        qCritical() << "Room Lua states are not independently owned";
        return 3;
    }

    if (!definitionsAndGlobalsAreRoomLocal(*first, *second)) {
        qCritical() << "Room definitions or Lua globals crossed the runtime boundary";
        return 4;
    }
    if (!aiStatesAreIsolated(*first, *second) || !aiSandboxBlocksHostLibraries(*first)
        || !aiSandboxBlocksHostLibraries(*second)) {
        qCritical() << "AI Lua states are not isolated from game states or sandboxed";
        return 11;
    }
    if (!aiRoutesSelectExactDefaultAndFreeze()) {
        qCritical() << "AI route registry did not preserve exact/default/frozen routing";
        return 12;
    }
    if (!aiWorldViewIsScopedAndRevisioned()) {
        qCritical() << "AI world view scope or state revision gate failed";
        return 17;
    }
    if (!productionIsolatedScriptAndShadowAudit(*first)
        || !shadowAuditPayloadIsBounded(*first) || !ownedAiProxyUsesValueLifetime()) {
        qCritical() << "Production isolated loading, shadow audit, or proxy ownership failed";
        return 15;
    }
    if (!aiShadowParsesPassAndUseCard(*second)) {
        qCritical() << "AI shadow result parsing failed";
        return 13;
    }
    if (!aiInstructionLimitRebuildsRuntime(*first)) {
        qCritical() << "AI instruction limit did not rebuild the isolated runtime";
        return 14;
    }
    const LuaFunction firstCallback = createIncrementCallback(*first);
    const LuaFunction secondCallback = createIncrementCallback(*second);
    if (!firstCallback || !secondCallback) {
        qCritical() << "Unable to create room-local callbacks";
        return 5;
    }
    {
        LuaRuntime::Binding luaBinding(second->roomRuntime()->lua());
        EngineRuntimeContextScope contextScope(*Sanguosha, second.get());
        if (firstCallback.push(second->getLuaState())) {
            qCritical() << "A callback crossed into another room runtime";
            return 6;
        }
        lua_pop(second->getLuaState(), 1);
    }

    if (!callbacksRunConcurrentlyInOwningRooms(*first, *second, firstCallback, secondCallback)) {
        qCritical() << "Concurrent room callbacks lost their owning runtime context";
        return 7;
    }
    if (!gameRuntimeDoesNotWaitForBootstrapMutex(*second)) {
        qCritical() << "A game runtime waited for the bootstrap Lua mutex";
        return 8;
    }
    if (!roomDestructionJoinsSpecializedWorker()) {
        qCritical() << "Room destruction left a specialized worker alive";
        return 9;
    }

    lua_State *firstAiState = first->roomRuntime()->ai().lua().rawState();
    first.reset();
    if (LuaRuntime::fromState(firstState) != nullptr || LuaRuntime::fromState(firstAiState) != nullptr
        || !invokeIncrement(*second, secondCallback, 41) || !aiShadowParsesPassAndUseCard(*second)) {
        qCritical() << "Destroying one room invalidated another room runtime";
        return 10;
    }

    second.reset();
    EngineBootstrap::shutdown();
    qInfo() << "room runtime isolation passed";
    return 0;
}
