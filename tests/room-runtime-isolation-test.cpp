#include "engine-bootstrap.h"
#include "engine.h"
#include "ai-runtime.h"
#include "ai-decision-coordinator.h"
#include "game-rng.h"
#include "general.h"
#include "lua-runtime.h"
#include "package.h"
#include "card-movement-service.h"
#include "card-lifetime-manager.h"
#include "room.h"
#include "room-runtime.h"
#include "roomthread1v1.h"
#include "serverplayer.h"
#include "settings.h"
#include "skill-instance-utils.h"
#include "skill.h"
#include <algorithm>

#include "lua.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QEvent>
#include <QFile>
#include <QElapsedTimer>
#include <QMetaEnum>
#include <QJsonObject>
#include <QJsonDocument>
#include <QPointer>
#include <QSemaphore>
#include <QThread>

#include <memory>
#include <functional>

struct RoomTestAccess
{
    static ServerPlayer *addOnlinePlayer(Room &room)
    {
        ServerPlayer *player = new ServerPlayer(&room);
        player->setObjectName(QStringLiteral("waiting-player"));
        player->setState(QStringLiteral("online"));
        player->drainAllLocks();
        player->releaseLock(ServerPlayer::SEMA_MUTEX);
        room.addPlayerToRoster(player);
        return player;
    }

    static ServerPlayer *addRobotPlayer(Room &room)
    {
        ServerPlayer *player = new ServerPlayer(&room);
        player->setObjectName(QStringLiteral("shadow-robot"));
        player->setState(QStringLiteral("robot"));
        room.addPlayerToRoster(player);
        return player;
    }

    static void attach1v1Thread(Room &room, RoomThread1v1 *thread)
    {
        room.thread_1v1 = thread;
    }

    static QList<int> drawPile(const Room &room)
    {
        return room.m_cardMovement->drawPile();
    }

    static AIRequest makeRequest(Room &room, ServerPlayer *player,
                                 AIRequest::DecisionKind kind)
    {
        return room.makeAIRequest(player, kind, CardUseStruct::CARD_USE_REASON_PLAY,
                                  QString(), QString(), Card::MethodUse);
    }

    static AIRequest makeRequest(Room &room, ServerPlayer *player,
                                 AIRequest::DecisionKind kind,
                                 CardUseStruct::CardUseReason reason,
                                 const QString &pattern, Card::HandlingMethod method)
    {
        return room.makeAIRequest(player, kind, reason, pattern, QString(), method);
    }

    static bool decide(Room &room, ServerPlayer *player, const AIRequest &request,
                       CardUseStruct &use)
    {
        return room.decideAiAction(player, request, use);
    }

    static AIRequest makeResponseRequest(Room &room, ServerPlayer *player,
                                         const QString &question, const QString &pattern,
                                         Card::HandlingMethod method)
    {
        return room.m_aiDecisions->makeResponseRequest(player, question, pattern, pattern,
                                                       QString(), method);
    }

    static void projectDecisionContext(Room &room, ServerPlayer *viewer, const QVariant &data,
                                       AIRequest &request)
    {
        room.m_aiDecisions->projectDecisionContext(viewer, data, request);
    }

    static bool applyResult(Room &room, ServerPlayer *player, const AIRequest &request,
                            const AIResult &result, CardUseStruct &use)
    {
        return room.applyAIResult(player, request, result, use);
    }

    static const Card *responseCard(Room &room, ServerPlayer *player,
                                    const AIRequest &request, const AIResult &result)
    {
        return room.m_aiDecisions->responseCard(player, request, result);
    }

    static bool areCardTargetsLegal(Room &room, const CardUseStruct &use)
    {
        return room.areCardTargetsLegal(use);
    }

    static AI *cloneAI(Room &room, ServerPlayer *player)
    {
        return room.cloneAI(player);
    }

    static void recordAiEvent(Room &room, int event, ServerPlayer *actor, const QVariant &data)
    {
        room.m_aiDecisions->recordEvent(event, actor, data);
    }

    static AIWorldView eventWorld(Room &room, ServerPlayer *viewer)
    {
        return room.m_aiDecisions->buildWorldView(viewer, true, true);
    }

    // decideAi* 是 Room 的 private 介面，測試一律經 friend wrapper 進入。
    static bool decideAiSkillInvoke(Room &room, ServerPlayer *player, const QString &skillName,
                                    const QVariant &data, bool &invoked)
    {
        return room.decideAiSkillInvoke(player, skillName, data, invoked);
    }
    static bool decideAiChoice(Room &room, ServerPlayer *player, const QString &skillName,
                               const QString &choices, const QVariant &data, QString &answer)
    {
        return room.decideAiChoice(player, skillName, choices, data, answer);
    }
    static bool decideAiSuit(Room &room, ServerPlayer *player, const QString &reason,
                             Card::Suit &suit)
    {
        return room.decideAiSuit(player, reason, suit);
    }
    static bool decideAiKingdom(Room &room, ServerPlayer *player, const QString &reason,
                                const QStringList &kingdoms, QString &answer)
    {
        return room.decideAiKingdom(player, reason, kingdoms, answer);
    }
    static bool decideAiGeneral(Room &room, ServerPlayer *player, const QStringList &generals,
                                const QString &defaultChoice, const QString &reason,
                                QString &answer)
    {
        return room.decideAiGeneral(player, generals, defaultChoice, reason, answer);
    }
    static bool decideAiDiscard(Room &room, ServerPlayer *player, const QString &reason,
                                int discardNum, int minNum, bool optional, bool includeEquip,
                                const QString &pattern, const QList<int> &candidates,
                                QList<int> &cards)
    {
        return room.decideAiDiscard(player, reason, discardNum, minNum, optional, includeEquip,
                                    pattern, candidates, cards);
    }
    static bool decideAiAmazingGrace(Room &room, ServerPlayer *player,
                                     const QList<int> &cardIds, bool refusable,
                                     const QString &reason, int &cardId)
    {
        return room.decideAiAmazingGrace(player, cardIds, refusable, reason, cardId);
    }
    static bool decideAiCardChosen(Room &room, ServerPlayer *player, ServerPlayer *who,
                                   const QString &flags, const QString &reason,
                                   Card::HandlingMethod method, int &cardId)
    {
        return room.decideAiCardChosen(player, who, flags, reason, method, cardId);
    }
    static bool decideAiYiji(Room &room, ServerPlayer *player, const QList<int> &cards,
                             const QString &reason, const QList<ServerPlayer *> &candidates,
                             ServerPlayer *&target, int &cardId)
    {
        return room.decideAiYiji(player, cards, reason, candidates, target, cardId);
    }
    static bool decideAiPlayerChosen(Room &room, ServerPlayer *player,
                                     const QList<ServerPlayer *> &targets,
                                     const QString &reason, ServerPlayer *&choice)
    {
        return room.decideAiPlayerChosen(player, targets, reason, choice);
    }
    static bool decideAiPlayersChosen(Room &room, ServerPlayer *player,
                                      const QList<ServerPlayer *> &targets,
                                      const QString &reason, int maxNum, int minNum,
                                      QList<ServerPlayer *> &chosen)
    {
        return room.decideAiPlayersChosen(player, targets, reason, maxNum, minNum, chosen);
    }
    static const Card *decideAiSinglePeach(Room &room, ServerPlayer *player,
                                           ServerPlayer *dying)
    {
        return room.decideAiSinglePeach(player, dying);
    }
};

// Config.EnableAI is a plain member, not a settings key, so ScopedConfigValue cannot
// reach it. Every AI contract below asks the coordinator to describe or decide an AI
// question, and since fc1c09e the coordinator answers those only while AI is enabled.
class ScopedAiEnabled
{
public:
    explicit ScopedAiEnabled(bool enabled) : m_previous(Config.EnableAI)
    {
        Config.EnableAI = enabled;
    }

    ~ScopedAiEnabled() { Config.EnableAI = m_previous; }

private:
    bool m_previous;
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

static int gameLuaRandomAfterLocalSeed(Room &room, quint32 localSeed)
{
    GameRng::Binding rngBinding(room.roomRuntime()->rng());
    LuaRuntime::Binding luaBinding(room.roomRuntime()->lua());
    lua_State *L = room.roomRuntime()->lua().state();
    const QByteArray script = QStringLiteral("math.randomseed(%1); return math.random(1, 1000000)")
        .arg(localSeed).toLatin1();
    if (!L || luaL_dostring(L, script.constData()) != 0)
        return -1;
    const int value = int(lua_tointeger(L, -1));
    lua_pop(L, 1);
    return value;
}

static QByteArray gameLuaHashOrder(Room &room)
{
    GameRng::Binding rngBinding(room.roomRuntime()->rng());
    LuaRuntime::Binding luaBinding(room.roomRuntime()->lua());
    lua_State *L = room.roomRuntime()->lua().state();
    if (!L || luaL_dostring(L,
            "local t={alpha=1,beta=2,gamma=3,delta=4,epsilon=5,zeta=6}; "
            "local r={}; for k in pairs(t) do r[#r+1]=k end; return table.concat(r, ',')") != 0)
        return QByteArray();
    const QByteArray order(lua_tostring(L, -1));
    lua_pop(L, 1);
    return order;
}

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

// The sandbox sgs table is expected on top of the stack.
static bool aiSandboxExposesMetaEnum(lua_State *L, const QMetaObject &metaObject,
                                     const char *enumeratorName, const char *fieldPrefix)
{
    const int enumeratorIndex = metaObject.indexOfEnumerator(enumeratorName);
    if (enumeratorIndex < 0)
        return false;
    const QMetaEnum enumerator = metaObject.enumerator(enumeratorIndex);
    for (int index = 0; index < enumerator.keyCount(); ++index) {
        const QByteArray fieldName = QByteArray(fieldPrefix) + enumerator.key(index);
        lua_getfield(L, -1, fieldName.constData());
        const bool matches = lua_type(L, -1) == LUA_TNUMBER
            && lua_tointeger(L, -1) == enumerator.value(index);
        lua_pop(L, 1);
        if (!matches)
            return false;
    }
    return true;
}

static bool aiSandboxBlocksHostLibraries(Room &room)
{
    LuaRuntime::Binding luaBinding(room.roomRuntime()->ai().lua());
    lua_State *L = room.roomRuntime()->ai().lua().state();
    const char *blockedGlobals[] = {
        "io", "os", "package", "coroutine", "require", "dofile", "loadfile",
        "load", "loadstring", "collectgarbage", nullptr
    };
    for (const char **name = blockedGlobals; *name; ++name) {
        lua_getglobal(L, *name);
        const bool blocked = lua_isnil(L, -1);
        lua_pop(L, 1);
        if (!blocked)
            return false;
    }
    lua_getglobal(L, "sgs");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    if (!aiSandboxExposesMetaEnum(L, Player::staticMetaObject, "Phase", "Player_")
        || !aiSandboxExposesMetaEnum(L, Card::staticMetaObject, "HandlingMethod", "Card_")
        || !aiSandboxExposesMetaEnum(L, Player::staticMetaObject, "Place", "Player_")) {
        lua_pop(L, 1);
        return false;
    }
    lua_getfield(L, -1, "Sanguosha");
    const bool nativeSgsBlocked = lua_isnil(L, -1);
    lua_pop(L, 2);
    if (!nativeSgsBlocked)
        return false;
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
    // 2026-09-18: Shadow 機制移除後，未設定的 kind／callback 一律預設 Isolated。
    if (routes.routeFor(AIRequest::Activate) != AiRouteIsolated
        || routes.routeFor(AIRequest::UseCard) != AiRouteIsolated
        || !routes.setCallbackRoute(QStringLiteral("ask_for_card"), QString(), AiRouteLegacyDirect)
        || !routes.setCallbackRoute(QStringLiteral("ask_for_card"), QStringLiteral("special"), AiRouteLegacyAdapted)
        || routes.routeFor(AIRequest::UseCard, QStringLiteral("ask_for_card"), QStringLiteral("special")) != AiRouteLegacyAdapted
        || routes.routeFor(AIRequest::UseCard, QStringLiteral("ask_for_card"), QStringLiteral("ordinary")) != AiRouteLegacyDirect) {
        return false;
    }
    routes.freeze();
    return !routes.setDecisionRoute(AIRequest::UseCard, AiRouteLegacyAdapted)
        && !routes.setCallbackRoute(QStringLiteral("ask_for_card"), QStringLiteral("late"), AiRouteLegacyAdapted)
        && routes.routeFor(AIRequest::UseCard, QStringLiteral("ask_for_card"), QStringLiteral("special")) == AiRouteLegacyAdapted;
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

static bool sharedFacadesAreAvailableToAllDecisions()
{
    ScopedConfigValue scripts(QStringLiteral("AiIsolatedScripts"), QStringList());
    Room room(nullptr, QStringLiteral("02_1v1"));
    if (!room.roomRuntime()->ai().lua().rawState())
        return false;

    {
        LuaRuntime &runtime = room.roomRuntime()->ai().lua();
        LuaRuntime::Binding luaBinding(runtime);
        const int top = lua_gettop(runtime.state());
        QString adapterError;
        const bool loaded = runtime.loadScript(
            QStringLiteral("tests/lua/isolated-adapter-contract.lua"), &adapterError);
        lua_settop(runtime.state(), top);
        if (!loaded) {
            qCritical().noquote() << adapterError;
            return false;
        }
    }

    {
        LuaRuntime::Binding luaBinding(room.roomRuntime()->ai().lua());
        lua_State *L = room.roomRuntime()->ai().lua().state();
        if (luaL_dostring(L,
            "if ai_skill_use ~= nil then error('askForUseCard dispatcher was loaded') end; "
            "local function shared_facade_probe(self, request) "
            "local player = self.player; local equips = player:getEquips(); "
            "local skills = player:getSkills(); "
            "if getmetatable(self) ~= SmartAIView or getmetatable(player) ~= PlayerView "
            "or request ~= self.request or self.world ~= request.world_view "
            "or player:getHp() ~= 2 or #equips ~= 1 "
            "or getmetatable(self.room) ~= RoomView or self.room:getCurrent() ~= player "
            "or self.room:getPlayers()[1] ~= player or self.room:getAlivePlayers()[1] ~= player "
            "or #player:getHandcards() ~= 1 or not player:getHandcards()[1]:isKindOf('Slash') "
            "or getmetatable(equips[1]) ~= CardView or not equips[1]:isKindOf('Slash') "
            "or #skills ~= 1 or getmetatable(skills[1]) ~= SkillView "
            "or skills[1]:getStateValue('count') ~= 2 then return nil end; "
            "local state = skills[1]:getState(); state.count = 99; "
            "if skills[1]:getStateValue('count') ~= 2 then return nil end; "
            "return { kind = 'pass' } end; "
            "ai_register_handler('activate', shared_facade_probe); "
            "ai_register_handler('use_card', shared_facade_probe)") != 0) {
            lua_pop(L, 1);
            return false;
        }
    }

    AIRequest request;
    request.viewerObjectName = QStringLiteral("shared-facade-owner");
    request.worldView.self.objectName = request.viewerObjectName;
    request.worldView.self.hp = 2;
    request.worldView.self.alive = true;
    request.worldView.self.dead = false;
    request.worldView.currentPlayer = request.viewerObjectName;
    request.worldView.playerOrder << request.viewerObjectName;
    request.worldView.alivePlayerOrder << request.viewerObjectName;

    AICardView equip;
    equip.objectName = QStringLiteral("slash");
    equip.className = QStringLiteral("Slash");
    equip.kindOfNames << QStringLiteral("Slash") << QStringLiteral("BasicCard")
                      << QStringLiteral("Card");
    request.worldView.self.equips << equip;
    request.worldView.handCards << equip;

    AISkillView skill;
    skill.skillName = QStringLiteral("shared-facade-skill");
    skill.hasPrivateState = true;
    skill.state.insert(QStringLiteral("count"), 2);
    request.worldView.self.skills << skill;

    request.kind = AIRequest::Activate;
    const AIResult activate = room.roomRuntime()->ai().decideIsolated(request);
    request.kind = AIRequest::UseCard;
    const AIResult useCard = room.roomRuntime()->ai().decideIsolated(request);
    if (!activate.handled || activate.kind != AIResult::Pass
        || !activate.errorCode.isEmpty() || !useCard.handled
        || useCard.kind != AIResult::Pass || !useCard.errorCode.isEmpty()) {
        return false;
    }

    request.worldView.self.objectName = QStringLiteral("wrong-viewer");
    request.kind = AIRequest::Activate;
    const AIResult invalidActivate = room.roomRuntime()->ai().decideIsolated(request);
    request.kind = AIRequest::UseCard;
    const AIResult invalidUseCard = room.roomRuntime()->ai().decideIsolated(request);
    return !invalidActivate.handled && invalidActivate.errorCode.isEmpty()
        && !invalidUseCard.handled && invalidUseCard.errorCode.isEmpty();
}

static bool productionIsolatedScriptAndFallback(Room &room)
{
    AIRequest probe;
    probe.kind = AIRequest::UseCard;
    probe.viewerObjectName = QStringLiteral("production-probe");
    probe.prompt = QStringLiteral("production-prompt");
    probe.worldView.self.objectName = probe.viewerObjectName;
    probe.worldView.self.seat = 3;
    probe.worldView.self.hp = 2;
    probe.worldView.self.maxHp = 4;
    probe.worldView.self.handcardCount = 5;
    probe.worldView.self.phase = int(Player::Play);
    probe.worldView.self.alive = true;
    probe.worldView.self.dead = false;
    probe.worldView.self.removed = false;
    probe.worldView.self.kongcheng = false;
    probe.worldView.self.wounded = true;
    probe.worldView.self.faceUp = false;
    probe.worldView.self.chained = true;
    probe.worldView.self.kingdom = QStringLiteral("wu");
    probe.worldView.self.role = QStringLiteral("rebel");
    probe.worldView.self.generalName = QStringLiteral("luxun");
    probe.worldView.self.general2Name = QStringLiteral("sujiang");
    probe.worldView.self.publicMarks.insert(QStringLiteral("facade-mark"), 7);

    AICardView equip;
    equip.cardId = 17;
    equip.effectiveId = 3;
    equip.objectName = QStringLiteral("slash");
    equip.className = QStringLiteral("Slash");
    equip.suit = int(Card::Spade);
    equip.number = 9;
    equip.skillName = QStringLiteral("_facade");
    equip.black = true;
    equip.kindOfNames << QStringLiteral("Slash") << QStringLiteral("BasicCard")
                      << QStringLiteral("Card");
    probe.worldView.self.equips << equip;

    AICardView judgingCard;
    judgingCard.cardId = 18;
    judgingCard.effectiveId = 18;
    judgingCard.objectName = QStringLiteral("indulgence");
    judgingCard.className = QStringLiteral("Indulgence");
    judgingCard.suit = int(Card::Heart);
    judgingCard.number = 6;
    judgingCard.red = true;
    judgingCard.kindOfNames << QStringLiteral("Indulgence")
                            << QStringLiteral("DelayedTrick")
                            << QStringLiteral("TrickCard") << QStringLiteral("Card");
    probe.worldView.self.judgingArea << judgingCard;

    AISkillView visibleSkill;
    visibleSkill.skillName = QStringLiteral("lianying");
    visibleSkill.instanceId = 4;
    visibleSkill.source = int(SourceAcquired);
    visibleSkill.hasAmountOverride = true;
    visibleSkill.amount = 2;
    visibleSkill.hasPrivateState = true;
    visibleSkill.state.insert(QStringLiteral("count"), 2);
    visibleSkill.state.insert(QStringLiteral("nested"),
                              QJsonObject({{QStringLiteral("answer"), 42}}));
    visibleSkill.correctState.insert(QStringLiteral("bonus"), 1);
    probe.worldView.self.skills << visibleSkill;

    AISkillView invalidSkill;
    invalidSkill.skillName = QStringLiteral("lianying");
    invalidSkill.instanceId = 5;
    invalidSkill.source = int(SourceAcquired);
    invalidSkill.invalid = true;
    invalidSkill.correctState.insert(QStringLiteral("public"), 3);
    probe.worldView.self.skills << invalidSkill;
    probe.pattern = QStringLiteral("not-migrated");
    const AIResult loadedHandler = room.roomRuntime()->ai().decideIsolated(probe);
    if (loadedHandler.handled || !loadedHandler.errorCode.isEmpty())
        return false;

    {
        LuaRuntime::Binding luaBinding(room.roomRuntime()->ai().lua());
        lua_State *L = room.roomRuntime()->ai().lua().state();
        if (luaL_dostring(L,
            "ai_skill_use['facade-probe'] = function(self, prompt, request) "
            "local p = self.player; "
            "if getmetatable(self) ~= SmartAIView or getmetatable(p) ~= PlayerView then "
            "return { kind = 'use_card', card = '@facade=metatable' } end; "
            "local checks = {{'objectName', request.viewer}, {'getSeat', 3}, {'getHp', 2}, "
            "{'getMaxHp', 4}, {'getHandcardNum', 5}, {'getPhase', sgs.Player_Play}, "
            "{'isAlive', true}, {'isDead', false}, {'isRemoved', false}, "
            "{'isKongcheng', false}, {'isWounded', true}, {'faceUp', false}, "
            "{'isChained', true}, "
            "{'getKingdom', 'wu'}, {'getRole', 'rebel'}, {'getGeneralName', 'luxun'}, "
            "{'getGeneral2Name', 'sujiang'}}; "
            "for _, check in ipairs(checks) do local method = p[check[1]]; "
            "if type(method) ~= 'function' or method(p) ~= check[2] then "
            "return { kind = 'use_card', card = '@facade=' .. check[1] } end end; "
            "if p:getMark('facade-mark') ~= 7 then "
            "return { kind = 'use_card', card = '@facade=getMark' } end; "
            "if not p:hasSkill('lianying') or not p:hasSkill('lianying#4') "
            "or p:hasSkill('lianying#5') or p:hasSkill('missing') then "
            "return { kind = 'use_card', card = '@facade=hasSkill' } end; "
            "local equips = p:getEquips(); local card = equips[1]; "
            "if #equips ~= 1 or getmetatable(card) ~= CardView then "
            "return { kind = 'use_card', card = '@facade=getEquips' } end; "
            "local card_checks = {{'getId', 17}, {'getEffectiveId', 3}, "
            "{'objectName', 'slash'}, {'getClassName', 'Slash'}, "
            "{'getSuit', sgs.Card_Spade}, {'getNumber', 9}, "
            "{'getSkillName', '_facade'}, {'isRed', false}, {'isBlack', true}}; "
            "for _, check in ipairs(card_checks) do local method = card[check[1]]; "
            "if type(method) ~= 'function' or method(card) ~= check[2] then "
            "return { kind = 'use_card', card = '@facade=' .. check[1] } end end; "
            "if not card:isKindOf('Slash') or not card:isKindOf('BasicCard') "
            "or not card:isKindOf('Card') or card:isKindOf('TrickCard') then "
            "return { kind = 'use_card', card = '@facade=isKindOf' } end; "
            "local judging = p:getJudgingArea(); "
            "if #judging ~= 1 or getmetatable(judging[1]) ~= CardView "
            "or not judging[1]:isKindOf('DelayedTrick') or not judging[1]:isRed() then "
            "return { kind = 'use_card', card = '@facade=getJudgingArea' } end; "
            "local skills = p:getSkills(); local skill = skills[1]; "
            "if #skills ~= 2 or getmetatable(skill) ~= SkillView "
            "or skill:objectName() ~= 'lianying' or skill:getInstanceId() ~= 4 "
            "or skill:getSource() ~= 1 or skill:isInvalid() "
            "or not skill:hasAmountOverride() or skill:getAmount() ~= 2 then "
            "return { kind = 'use_card', card = '@facade=getSkills' } end; "
            "local private_state = skill:getState(); "
            "if private_state.count ~= 2 or private_state.nested.answer ~= 42 "
            "or skill:getCorrectStateValue('bonus') ~= 1 then "
            "return { kind = 'use_card', card = '@facade=getState' } end; "
            "private_state.count = 99; private_state.nested.answer = 0; "
            "if skill:getStateValue('count') ~= 2 "
            "or skill:getStateValue('nested').answer ~= 42 "
            "or skills[2]:getState() ~= nil "
            "or skills[2]:getCorrectStateValue('public') ~= 3 then "
            "return { kind = 'use_card', card = '@facade=state-copy' } end; "
            "for _, method_name in ipairs({'getGeneral', 'getRoom', 'getTag', "
            "'canSlash', 'setFlags', 'addMark'}) do if p[method_name] ~= nil then "
            "return { kind = 'use_card', card = '@facade=' .. method_name } end end; "
            "for _, method_name in ipairs({'getRealCard', 'setSkillName', 'deleteLater', 'getRoom'}) "
            "do if card[method_name] ~= nil then "
            "return { kind = 'use_card', card = '@facade=card-' .. method_name } end end; "
            "for _, method_name in ipairs({'setStateValue', 'setCorrectStateValue', "
            "'setAmount', 'getRoom'}) do if skill[method_name] ~= nil then "
            "return { kind = 'use_card', card = '@facade=skill-' .. method_name } end end; "
            "if self.world_view ~= request.world_view or prompt ~= request.prompt then "
            "return { kind = 'use_card', card = '@facade=request' } end; "
            "return { kind = 'pass' } end; "
            "ai_skill_use['shadow-match'] = function() "
            "return { kind = 'pass' } end; "
            "ai_skill_use['shadow-use'] = function() "
            "return { kind = 'use_card', card = '@shadow=.' } end; "
            "ai_skill_use['shadow-error'] = function() "
            "error('shadow handler failure') end; "
            "ai_register_use_card_skill_handler('shadow-skill', function() "
            "return { kind = 'pass' } end)") != 0) {
            lua_pop(L, 1);
            return false;
        }
    }

    probe.pattern = QStringLiteral("facade-probe");
    const AIResult facadeResult = room.roomRuntime()->ai().decideIsolated(probe);
    if (!facadeResult.handled || facadeResult.kind != AIResult::Pass
        || !facadeResult.errorCode.isEmpty()) {
        qCritical() << "Value facade probe failed"
                    << facadeResult.handled << facadeResult.kind
                    << facadeResult.errorCode << facadeResult.action.legacyCardString;
        return false;
    }

    AIRequest skillProbe = probe;
    skillProbe.pattern = QStringLiteral("shadow-use");
    skillProbe.hasSkillActionContext = true;
    skillProbe.skillActionContext.activationRef = SkillInstanceRef(
        QStringLiteral("shadow-robot"),
        SkillInstanceKey(QStringLiteral("shadow-skill"), 1));
    skillProbe.skillActionContext.sourceRef = skillProbe.skillActionContext.activationRef;
    const AIResult skillResult = room.roomRuntime()->ai().decideIsolated(skillProbe);
    if (!skillResult.handled || skillResult.kind != AIResult::Pass
        || !skillResult.errorCode.isEmpty())
        return false;

    ServerPlayer *player = RoomTestAccess::addRobotPlayer(room);
    std::unique_ptr<TrustAI> ai(new TrustAI(player));
    player->setAI(ai.get());

    const quint64 revision = room.roomRuntime()->stateRevision();
    const AIRequest first = RoomTestAccess::makeRequest(room, player, AIRequest::UseCard);
    AIRequest notCovered = RoomTestAccess::makeRequest(room, player, AIRequest::UseCard);
    if (first.stateRevision != revision || notCovered.stateRevision != revision
        || room.roomRuntime()->stateRevision() != revision) {
        player->setAI(nullptr);
        return false;
    }

    notCovered.pattern = QStringLiteral("not-migrated");
    CardUseStruct notCoveredUse;
    const bool notCoveredDecided = RoomTestAccess::decide(
        room, player, notCovered, notCoveredUse);

    AIRequest answered = RoomTestAccess::makeRequest(room, player, AIRequest::UseCard);
    answered.pattern = QStringLiteral("shadow-match");
    CardUseStruct answeredUse;
    const bool answeredDecided = RoomTestAccess::decide(room, player, answered, answeredUse);

    AIRequest rejected = RoomTestAccess::makeRequest(room, player, AIRequest::UseCard);
    rejected.pattern = QStringLiteral("shadow-use");
    CardUseStruct rejectedUse;
    const bool rejectedDecided = RoomTestAccess::decide(
        room, player, rejected, rejectedUse);

    AIRequest failing = RoomTestAccess::makeRequest(room, player, AIRequest::UseCard);
    failing.pattern = QStringLiteral("shadow-error");
    CardUseStruct failingUse;
    const bool failingDecided = RoomTestAccess::decide(room, player, failing, failingUse);
    player->setAI(nullptr);

    // 2026-09-18：UseCard 預設走 isolated——未覆蓋與錯誤的 request 回退 legacy（TrustAI
    // 一律 "."，視為 pass）；shadow-use 回傳引擎不認識的牌字串，在 applyResult 驗證關被拒答。
    return notCoveredDecided && answeredDecided && !rejectedDecided && failingDecided
        && !notCoveredUse.card && !answeredUse.card && !rejectedUse.card && !failingUse.card;
}

static AIRequest adapterRequest(const QString &pattern, const QString &prompt,
                                Card::HandlingMethod method)
{
    AIRequest request;
    request.kind = AIRequest::UseCard;
    request.viewerObjectName = QStringLiteral("adapter-owner");
    request.pattern = pattern;
    request.prompt = prompt;
    request.handlingMethod = method;
    request.worldView.self.objectName = request.viewerObjectName;
    request.worldView.self.alive = true;
    request.worldView.self.dead = false;
    request.worldView.currentPlayer = request.viewerObjectName;
    request.worldView.playerOrder << request.viewerObjectName;
    request.worldView.alivePlayerOrder << request.viewerObjectName;
    return request;
}

static bool legacyCallbackAbiAndResultConversion(Room &room)
{
    {
        LuaRuntime::Binding luaBinding(room.roomRuntime()->ai().lua());
        lua_State *L = room.roomRuntime()->ai().lua().state();
        // A legacy-style callback keeps method third, the stripped pattern fourth and
        // the skill-action request fifth; the new ABI keeps the request itself third.
        if (luaL_dostring(L,
            "ai_skill_use_legacy['legacy-abi'] = "
            "function(self, prompt, method, pattern, request) "
            "if prompt ~= 'legacy-prompt:extra' or method ~= sgs.Card_MethodResponse "
            "or pattern ~= 'legacy-abi' or request ~= nil then return '.' end; "
            "return '@legacy=' .. pattern end; "
            "ai_skill_use['new-abi'] = function(self, prompt, request) "
            "if type(request) ~= 'table' or request ~= self.request "
            "or prompt ~= request.prompt or request.pattern ~= 'new-abi' then return '.' end; "
            "return { kind = 'use_card', card = '@new=' .. request.pattern } end; "
            "ai_skill_use_legacy['@@compulsory!'] = function(self, prompt, method, pattern) "
            "if pattern ~= '@@compulsory' then return '@stripped=' .. tostring(pattern) end; "
            "return '.' end; "
            "ai_skill_use_legacy['optional-decline'] = function() return '.' end; "
            "ai_skill_use['fallback-prompt'] = function(self, prompt, request) "
            "return { kind = 'use_card', card = '@prompt=' .. request.pattern } end; "
            "ai_skill_use['skill-pattern'] = function() "
            "return { kind = 'use_card', card = '@pattern=lost' } end; "
            "ai_register_use_card_legacy_skill_handler('adapter-skill', "
            "function(self, prompt, method, pattern, request) "
            "if not request or not request:isValid() "
            "or request:getActivationSkillName() ~= 'adapter-skill' "
            "or request:getActivationInstanceId() ~= 1 "
            "or not request:isActivationQuotaAvailable() "
            "or request:getInitiator() ~= self.player then return '.' end; "
            "return { accepted = true, cards = { 7 }, "
            "targets = { self.player:objectName() } } end); "
            "ai_skill_use['invalid-answer'] = function() return 7 end") != 0) {
            lua_pop(L, 1);
            return false;
        }
        // One key, one ABI: the duplicate is refused, the standing handler is kept.
        if (luaL_dostring(L,
            "if pcall(function() ai_skill_use_legacy['new-abi'] = function() end end) "
            "or pcall(function() ai_skill_use['legacy-abi'] = function() end end) "
            "or pcall(ai_register_use_card_skill_handler, 'adapter-skill', function() end) "
            "then error('a conflicting registration was accepted') end; "
            "ai_skill_use['new-abi'] = ai_skill_use['new-abi']") != 0) {
            lua_pop(L, 1);
            return false;
        }
    }

    const AIResult legacyAbi = room.roomRuntime()->ai().decideIsolated(
        adapterRequest(QStringLiteral("legacy-abi"), QStringLiteral("legacy-prompt:extra"),
                       Card::MethodResponse));
    const AIResult newAbi = room.roomRuntime()->ai().decideIsolated(
        adapterRequest(QStringLiteral("new-abi"), QStringLiteral("new-prompt"),
                       Card::MethodUse));
    if (!legacyAbi.handled || legacyAbi.kind != AIResult::UseCard
        || legacyAbi.action.legacyCardString != QStringLiteral("@legacy=legacy-abi")
        || !newAbi.handled || newAbi.kind != AIResult::UseCard
        || newAbi.action.legacyCardString != QStringLiteral("@new=new-abi")) {
        qCritical() << "Callback ABI arguments were not routed to the registered shape"
                    << legacyAbi.action.legacyCardString << newAbi.action.legacyCardString;
        return false;
    }

    // A compulsory request refuses "." and keeps searching; an optional one accepts it.
    const AIResult compulsory = room.roomRuntime()->ai().decideIsolated(
        adapterRequest(QStringLiteral("@@compulsory!"),
                       QStringLiteral("fallback-prompt:target"), Card::MethodUse));
    const AIResult declined = room.roomRuntime()->ai().decideIsolated(
        adapterRequest(QStringLiteral("optional-decline"),
                       QStringLiteral("fallback-prompt:target"), Card::MethodUse));
    if (!compulsory.handled || compulsory.kind != AIResult::UseCard
        || compulsory.action.legacyCardString != QStringLiteral("@prompt=@@compulsory!")
        || !declined.handled || declined.kind != AIResult::Pass
        || !declined.errorCode.isEmpty()) {
        qCritical() << "Compulsory dispatch did not separate a refusal from a decision"
                    << compulsory.action.legacyCardString << int(declined.kind);
        return false;
    }

    // An unregistered request stays unhandled and a broken answer stays an error;
    // neither may arrive as a legal pass.
    const AIResult unhandled = room.roomRuntime()->ai().decideIsolated(
        adapterRequest(QStringLiteral("not-registered"), QStringLiteral("no-handler"),
                       Card::MethodUse));
    const AIResult invalid = room.roomRuntime()->ai().decideIsolated(
        adapterRequest(QStringLiteral("invalid-answer"), QStringLiteral("no-handler"),
                       Card::MethodUse));
    if (unhandled.handled || !unhandled.errorCode.isEmpty()
        || invalid.errorCode != QStringLiteral("AI_RUNTIME_ERROR")
        || invalid.kind != AIResult::Pass || invalid.handled) {
        qCritical() << "Unhandled and failing answers were not kept apart"
                    << unhandled.handled << invalid.errorCode << invalid.handled;
        return false;
    }

    // The skill tier wins over the pattern tier, and a legacy structured answer
    // becomes the value result.
    AIRequest skillRequest = adapterRequest(QStringLiteral("skill-pattern"),
                                            QStringLiteral("no-handler"), Card::MethodUse);
    skillRequest.hasSkillActionContext = true;
    skillRequest.skillActionContext.activationRef = SkillInstanceRef(
        skillRequest.viewerObjectName,
        SkillInstanceKey(QStringLiteral("adapter-skill"), 1));
    skillRequest.skillActionContext.sourceRef = skillRequest.skillActionContext.activationRef;
    skillRequest.skillActionContext.activationQuotaAvailable = true;
    skillRequest.skillActionContext.sourceQuotaAvailable = true;
    const AIResult skillResult = room.roomRuntime()->ai().decideIsolated(skillRequest);
    if (!skillResult.handled || skillResult.kind != AIResult::UseCard
        || !skillResult.action.legacyCardString.isEmpty()
        || skillResult.action.selectedCardIds != QList<int>({7})
        || skillResult.action.selectedTargetNames
            != QStringList({skillRequest.viewerObjectName})) {
        qCritical() << "Skill dispatch or legacy result conversion failed"
                    << skillResult.action.legacyCardString
                    << skillResult.action.selectedCardIds.size();
        return false;
    }
    return true;
}

static bool valueDecisionsRouteThroughTheIsolatedVm()
{
    // Routes are frozen at Room construction, so the configuration comes first.
    ScopedConfigValue scripts(QStringLiteral("AiIsolatedScripts"),
                              QStringList({QStringLiteral("ask-for-choice.lua")}));
    ScopedConfigValue isolatedCallbacks(QStringLiteral("AiIsolatedCallbacks"),
        QStringList({QStringLiteral("askForSkillInvoke"), QStringLiteral("askForChoice"),
                     QStringLiteral("askForSuit"), QStringLiteral("askForKingdom"),
                     QStringLiteral("askForGeneral"), QStringLiteral("askForDiscard"),
                     QStringLiteral("askForAG"), QStringLiteral("askForCardChosen"),
                     QStringLiteral("askForYiji"), QStringLiteral("askForPlayerChosen"),
                     QStringLiteral("askForPlayersChosen"),
                     QStringLiteral("askForSinglePeach")}));
    Room room(nullptr, QStringLiteral("02_1v1"));
    ServerPlayer *player = RoomTestAccess::addRobotPlayer(room);
    player->setObjectName(QStringLiteral("value-owner"));
    std::unique_ptr<TrustAI> ai(new TrustAI(player));
    player->setAI(ai.get());

    {
        LuaRuntime::Binding luaBinding(room.roomRuntime()->ai().lua());
        lua_State *L = room.roomRuntime()->ai().lua().state();
        if (luaL_dostring(L,
            // Every value handler receives (self, options, request): the candidates and
            // limits arrive as values, never as a QVariant.
            "ai_skill_invoke['yes-skill'] = function(self, options, request) "
            "if options.reason ~= 'yes-skill' or request.kind ~= 'skill_invoke' "
            "or #options.choices ~= 2 or options.choices[1] ~= 'yes' "
            "or options.default_choice ~= 'no' or not options.optional "
            "or getmetatable(self) ~= SmartAIView then return nil end; return true end; "
            "ai_skill_invoke['no-skill'] = function() return false end; "
            "ai_skill_choice['pick'] = function(self, options) "
            "if #options.choices ~= 3 or options.default_choice ~= nil then return nil end; "
            "return options.choices[2] end; "
            "ai_skill_choice['bad-answer'] = function() return 7 end; "
            "ai_skill_suit['suit-reason'] = function(self, options) "
            "return options.choices[3] end; "
            "ai_skill_kingdom['kingdom-reason'] = function(self, options) "
            "return { kind = 'answer', answer = options.choices[1] } end; "
            "ai_general_choice['general-reason'] = function(self, options) "
            "if options.default_choice ~= 'zhangfei' then return nil end; "
            "return 'guanyu' end") != 0) {
            lua_pop(L, 1);
            return false;
        }
    }

    bool invoked = false;
    if (!RoomTestAccess::decideAiSkillInvoke(room, player, QStringLiteral("yes-skill"), QVariant(), invoked)
        || !invoked)
        return false;
    if (!RoomTestAccess::decideAiSkillInvoke(room, player, QStringLiteral("no-skill"), QVariant(), invoked)
        || invoked)
        return false;
    // An unregistered reason falls back to the legacy AI instead of inventing an answer.
    if (!RoomTestAccess::decideAiSkillInvoke(room, player, QStringLiteral("unregistered"), QVariant(), invoked)
        || invoked)
        return false;

    QString answer;
    if (!RoomTestAccess::decideAiChoice(room, player, QStringLiteral("pick"), QStringLiteral("a+b+c"),
                             QVariant(), answer)
        || answer != QStringLiteral("b"))
        return false;
    // A broken handler is an error, so the legacy AI answers instead; the result is
    // still one of the candidates and never the malformed value.
    answer = QStringLiteral("untouched");
    if (!RoomTestAccess::decideAiChoice(room, player, QStringLiteral("bad-answer"), QStringLiteral("a+b"),
                             QVariant(), answer)
        || (answer != QStringLiteral("a") && answer != QStringLiteral("b")))
        return false;

    Card::Suit suit = Card::NoSuit;
    if (!RoomTestAccess::decideAiSuit(room, player, QStringLiteral("suit-reason"), suit)
        || suit != Card::AllSuits[2])
        return false;

    QString kingdom;
    if (!RoomTestAccess::decideAiKingdom(room, player, QStringLiteral("kingdom-reason"),
                              QStringList({QStringLiteral("wu"), QStringLiteral("shu")}), kingdom)
        || kingdom != QStringLiteral("wu"))
        return false;

    QString general;
    if (!RoomTestAccess::decideAiGeneral(room, player, QStringList({QStringLiteral("guanyu"),
                                                   QStringLiteral("zhangfei")}),
                              QStringLiteral("zhangfei"), QStringLiteral("general-reason"),
                              general)
        || general != QStringLiteral("guanyu"))
        return false;
    // Selections answer with ids and object names; the candidates come from the request.
    {
        LuaRuntime::Binding luaBinding(room.roomRuntime()->ai().lua());
        lua_State *L = room.roomRuntime()->ai().lua().state();
        if (luaL_dostring(L,
            "ai_skill_askforag['ag-reason'] = function(self, options) "
            "if #options.card_ids ~= 2 or not options.optional then return nil end; "
            "return options.card_ids[2] end; "
            "ai_skill_discard['discard-reason'] = function(self, options, request) "
            "if options.min_count ~= 1 or options.max_count ~= 2 "
            "or request.pattern ~= '.' or #options.card_ids ~= 3 then return nil end; "
            "return { options.card_ids[1], options.card_ids[3] } end; "
            "ai_skill_discard['bad-discard'] = function(self, options) return { 999 } end; "
            "ai_skill_playerchosen['player-reason'] = function(self, options) "
            "return options.players[2] end; "
            "ai_skill_playerschosen['players-reason'] = function(self, options) "
            "if options.max_count ~= 2 or options.min_count ~= 1 then return nil end; "
            "return { options.players[2], options.players[1] } end; "
            "ai_skill_askforyiji['yiji-reason'] = function(self, options) "
            "return { kind = 'answer', cards = { options.card_ids[1] }, "
            "targets = { options.players[1] } } end; "
            "ai_skill_cardchosen['card-chosen-reason'] = function(self, options) "
            "if #options.players ~= 1 or options.choices[1] ~= 'he' then return nil end; "
            "return 42 end") != 0) {
            lua_pop(L, 1);
            return false;
        }
    }

    ServerPlayer *other = RoomTestAccess::addRobotPlayer(room);
    other->setObjectName(QStringLiteral("value-other"));
    QList<ServerPlayer *> candidates;
    candidates << player << other;

    int cardId = -1;
    if (!RoomTestAccess::decideAiAmazingGrace(room, player, QList<int>({5, 9}), true,
                                   QStringLiteral("ag-reason"), cardId)
        || cardId != 9)
        return false;

    QList<int> discarded;
    if (!RoomTestAccess::decideAiDiscard(room, player, QStringLiteral("discard-reason"), 2, 1, true, false,
                              QStringLiteral("."), QList<int>({1, 2, 3}), discarded)
        || discarded != QList<int>({1, 3}))
        return false;
    // A card the question never offered is refused, so the call site keeps its own list.
    discarded.clear();
    if (RoomTestAccess::decideAiDiscard(room, player, QStringLiteral("bad-discard"), 2, 1, true, false,
                             QStringLiteral("."), QList<int>({1, 2, 3}), discarded)
        || !discarded.isEmpty())
        return false;

    ServerPlayer *chosen = nullptr;
    if (!RoomTestAccess::decideAiPlayerChosen(room, player, candidates, QStringLiteral("player-reason"), chosen)
        || chosen != other)
        return false;

    QList<ServerPlayer *> chosenList;
    if (!RoomTestAccess::decideAiPlayersChosen(room, player, candidates, QStringLiteral("players-reason"), 2, 1,
                                    chosenList)
        || chosenList != QList<ServerPlayer *>({other, player}))
        return false;

    ServerPlayer *receiver = nullptr;
    int yijiCard = -1;
    if (!RoomTestAccess::decideAiYiji(room, player, QList<int>({7, 8}), QStringLiteral("yiji-reason"),
                           candidates, receiver, yijiCard)
        || receiver != player || yijiCard != 7)
        return false;

    int chosenCard = -1;
    if (!RoomTestAccess::decideAiCardChosen(room, player, other, QStringLiteral("he"),
                                 QStringLiteral("card-chosen-reason"), Card::MethodDiscard,
                                 chosenCard)
        || chosenCard != 42)
        return false;

    // Card responses: the request names its question, and an id the player does not
    // hold is refused, so the legacy card (here: none) stands.
    {
        LuaRuntime::Binding luaBinding(room.roomRuntime()->ai().lua());
        lua_State *L = room.roomRuntime()->ai().lua().state();
        if (luaL_dostring(L,
            "respond_seen = ''; "
            "ai_skill_singlepeach['single_peach'] = function(self, options, request) "
            "respond_seen = table.concat({request.kind, options.question, options.reason, "
            "request.pattern, tostring(#options.card_ids), options.players[1]}, '|'); "
            "return 4242 end") != 0) {
            lua_pop(L, 1);
            return false;
        }
    }
    if (RoomTestAccess::decideAiSinglePeach(room, player, other) != nullptr)
        return false;
    {
        LuaRuntime::Binding luaBinding(room.roomRuntime()->ai().lua());
        lua_State *L = room.roomRuntime()->ai().lua().state();
        lua_getglobal(L, "respond_seen");
        const QString seen = QString::fromUtf8(lua_tostring(L, -1));
        lua_pop(L, 1);
        if (seen != QStringLiteral("respond_card|askForSinglePeach|single_peach|peach|0|value-other"))
            return false;
    }

    player->setAI(nullptr);
    return true;
}

static bool decisionCorePlansATurnFromCandidates()
{
    ScopedConfigValue scripts(QStringLiteral("AiIsolatedScripts"),
                              QStringList({QStringLiteral("decision-core.lua"), QStringLiteral("strategy-hooks.lua")}));
    Room room(nullptr, QStringLiteral("02_1v1"));
    if (!room.roomRuntime()->ai().lua().rawState())
        return false;

    AIRequest request;
    request.kind = AIRequest::Activate;
    // A complete question: this fixture carries no view-as skill, so "no
    // conversion" is its answer rather than something it failed to work out.
    request.conversionsEnumerated = true;
    request.viewerObjectName = QStringLiteral("core-owner");
    request.worldView.self.objectName = request.viewerObjectName;
    request.worldView.self.alive = true;
    request.worldView.self.dead = false;
    request.worldView.self.hp = 4;
    request.worldView.self.attackRange = 1;
    request.worldView.currentPlayer = request.viewerObjectName;
    request.worldView.playerOrder << request.viewerObjectName << QStringLiteral("core-enemy");
    request.worldView.alivePlayerOrder = request.worldView.playerOrder;

    AIPlayerView enemy;
    enemy.objectName = QStringLiteral("core-enemy");
    enemy.alive = true;
    enemy.dead = false;
    enemy.hp = 1;
    enemy.handcardCount = 0;
    enemy.handVisible = true;
    request.worldView.players << enemy;

    AICardView slash;
    slash.cardId = 7;
    slash.effectiveId = 7;
    slash.objectName = QStringLiteral("slash");
    slash.className = QStringLiteral("Slash");
    slash.kindOfNames << QStringLiteral("Slash") << QStringLiteral("BasicCard");
    AICardView peach;
    peach.cardId = 9;
    peach.effectiveId = 9;
    peach.objectName = QStringLiteral("peach");
    peach.className = QStringLiteral("Peach");
    peach.kindOfNames << QStringLiteral("Peach") << QStringLiteral("BasicCard");
    request.worldView.handCards << slash << peach;

    QJsonObject relations, ownRow, objectives;
    ownRow.insert(QStringLiteral("core-enemy"), QStringLiteral("enemy"));
    relations.insert(request.viewerObjectName, ownRow);
    objectives.insert(QStringLiteral("core-enemy"), 5);
    QJsonObject policy;
    policy.insert(QStringLiteral("managed"), true);
    policy.insert(QStringLiteral("relations"), relations);
    policy.insert(QStringLiteral("objectives"), objectives);
    request.worldView.modePolicy = policy;

    AICardCandidateView slashCandidate;
    slashCandidate.cardId = 7;
    slashCandidate.available = true;
    slashCandidate.completeCoverage = true;
    slashCandidate.legalTargets << QStringLiteral("core-enemy");
    for (const QString &name : slashCandidate.legalTargets)
        slashCandidate.targetCombinations << QStringList{name};
    AICardCandidateView peachCandidate;
    peachCandidate.cardId = 9;
    peachCandidate.available = false;
    request.cardCandidates << slashCandidate << peachCandidate;

    const AIResult planned = room.roomRuntime()->ai().decideIsolated(request);
    if (!planned.handled || planned.kind != AIResult::UseCard
        || planned.action.useCardId != 7
        || planned.action.selectedTargetNames != QStringList({QStringLiteral("core-enemy")})
        || !planned.errorCode.isEmpty()) {
        qCritical() << "Generic turn plan failed" << planned.handled << int(planned.kind)
                    << planned.action.useCardId << planned.errorCode;
        return false;
    }

    // Candidates that exist but are all unusable mean pass: the question was asked
    // and nothing legal came out of it.
    AIRequest blocked = request;
    blocked.cardCandidates.clear();
    AICardCandidateView unusable;
    unusable.cardId = 7;
    unusable.available = true;
    unusable.limited = true;
    blocked.cardCandidates << unusable;
    const AIResult passed = room.roomRuntime()->ai().decideIsolated(blocked);
    AIRequest empty = request;
    empty.cardCandidates.clear();
    const AIResult nothingLegal = room.roomRuntime()->ai().decideIsolated(empty);
    return passed.handled && passed.kind == AIResult::Pass && passed.errorCode.isEmpty()
        && nothingLegal.handled && nothingLegal.kind == AIResult::Pass;
}

// Find a physical card of the engine by its object name, so the candidate contract is
// checked against real rules rather than a fabricated card view.
static const Card *findEngineCard(const QString &objectName)
{
    for (int id = 0; id < Sanguosha->getCardCount(); ++id) {
        const Card *card = Sanguosha->getCard(id);
        if (card && card->objectName() == objectName && !card->isVirtualCard())
            return card;
    }
    return nullptr;
}

static const AICardCandidateView *candidateFor(const AIRequest &request, int cardId)
{
    for (int index = 0; index < request.cardCandidates.size(); ++index) {
        if (request.cardCandidates.at(index).cardId == cardId)
            return &request.cardCandidates.at(index);
    }
    return nullptr;
}

// A candidate answers the question that was actually asked. Play asks what may be
// played; a response asks what matches the pattern. Card::isAvailable answers only the
// first, so using it for both hides every legal response behind "not available".
// The same request must also say whether its target list is the whole story: a card
// whose combinations it cannot describe is not a card the planner may truncate.
static bool cardCandidatesDescribeTheQuestionAsked()
{
    std::unique_ptr<Room> room(new Room(nullptr, QStringLiteral("02_1v1")));
    EngineRuntimeContextScope contextScope(*Sanguosha, room.get());
    room->roomRuntime()->state().reset();
    ServerPlayer *viewer = RoomTestAccess::addRobotPlayer(*room);
    viewer->setObjectName(QStringLiteral("candidate-viewer"));
    viewer->setSeat(1);
    viewer->setMaxHp(4);
    viewer->setHp(4);
    viewer->setPhase(Player::Play);
    ServerPlayer *other = RoomTestAccess::addRobotPlayer(*room);
    other->setObjectName(QStringLiteral("candidate-other"));
    other->setSeat(2);
    other->setMaxHp(4);
    other->setHp(4);
    room->setCurrent(viewer);
    room->rebuildAlivePlayers();

    const Card *slash = findEngineCard(QStringLiteral("slash"));
    const Card *jink = findEngineCard(QStringLiteral("jink"));
    if (!slash || !jink)
        return false;
    viewer->addCard(slash->getId(), Player::PlaceHand);
    viewer->addCard(jink->getId(), Player::PlaceHand);

    const AIRequest play = RoomTestAccess::makeRequest(*room, viewer, AIRequest::Activate,
        CardUseStruct::CARD_USE_REASON_PLAY, QString(), Card::MethodUse);
    const AICardCandidateView *playSlash = candidateFor(play, slash->getEffectiveId());
    const AICardCandidateView *playJink = candidateFor(play, jink->getEffectiveId());
    if (!playSlash || !playJink) {
        qCritical() << "Play candidates were not built for the held cards";
        return false;
    }
    if (!playSlash->available || playJink->available) {
        qCritical() << "Play availability was wrong" << playSlash->available
                    << playJink->available;
        return false;
    }
    // A Slash is a single-target card here: the other player stands alone as a complete
    // action, and the viewer is not a legal target of their own Slash.
    if (!playSlash->completeCoverage
        || playSlash->legalTargets != QStringList({other->objectName()})
        || playSlash->feasibleWithNoTarget || !playSlash->maxVotes.isEmpty()) {
        qCritical() << "The Slash candidate did not describe a complete single target"
                    << playSlash->completeCoverage << playSlash->legalTargets
                    << playSlash->feasibleWithNoTarget;
        return false;
    }

    // The same two cards, asked as a response to a Slash: now the Jink is the legal
    // answer and the Slash is not, which is the exact inversion of the Play answer.
    const AIRequest response = RoomTestAccess::makeRequest(*room, viewer,
        AIRequest::RespondCard, CardUseStruct::CARD_USE_REASON_RESPONSE,
        QStringLiteral("jink"), Card::MethodResponse);
    const AICardCandidateView *respondSlash = candidateFor(response, slash->getEffectiveId());
    const AICardCandidateView *respondJink = candidateFor(response, jink->getEffectiveId());
    if (!respondSlash || !respondJink) {
        qCritical() << "Response candidates were not built for the held cards";
        return false;
    }
    if (!respondJink->available || respondSlash->available) {
        qCritical() << "A response was judged with the Play availability rule"
                    << respondJink->available << respondSlash->available;
        return false;
    }
    // The compulsory marker belongs to the dispatcher key, not to the pattern.
    const AIRequest compulsory = RoomTestAccess::makeRequest(*room, viewer,
        AIRequest::RespondCard, CardUseStruct::CARD_USE_REASON_RESPONSE,
        QStringLiteral("jink!"), Card::MethodResponse);
    const AICardCandidateView *compulsoryJink =
        candidateFor(compulsory, jink->getEffectiveId());
    if (!compulsoryJink || !compulsoryJink->available) {
        qCritical() << "A compulsory pattern hid its own legal answer";
        return false;
    }
    return true;
}

// Targets the AI picked are re-checked by the authority before anything happens. The
// value-typed card_id answer used to skip that: CardUseStruct::parse arms the check for
// the legacy string answer, and nothing armed it for the new path.
static bool illegalAiTargetsNeverReachGameplay()
{
    std::unique_ptr<Room> room(new Room(nullptr, QStringLiteral("02_1v1")));
    EngineRuntimeContextScope contextScope(*Sanguosha, room.get());
    room->roomRuntime()->state().reset();
    ServerPlayer *viewer = RoomTestAccess::addRobotPlayer(*room);
    viewer->setObjectName(QStringLiteral("gate-viewer"));
    viewer->setSeat(1);
    viewer->setMaxHp(4);
    viewer->setHp(4);
    viewer->setPhase(Player::Play);
    ServerPlayer *other = RoomTestAccess::addRobotPlayer(*room);
    other->setObjectName(QStringLiteral("gate-other"));
    other->setSeat(2);
    other->setMaxHp(4);
    other->setHp(4);
    room->setCurrent(viewer);
    room->rebuildAlivePlayers();

    const Card *slash = findEngineCard(QStringLiteral("slash"));
    if (!slash)
        return false;
    viewer->addCard(slash->getId(), Player::PlaceHand);

    const AIRequest request = RoomTestAccess::makeRequest(*room, viewer,
        AIRequest::Activate, CardUseStruct::CARD_USE_REASON_PLAY, QString(),
        Card::MethodUse);
    const AICardCandidateView *candidate = candidateFor(request, slash->getEffectiveId());
    if (!candidate || candidate->legalTargets.contains(viewer->objectName())) {
        qCritical() << "The candidate offered the viewer their own Slash";
        return false;
    }

    // An answer the candidate list never offered: a Slash aimed at oneself.
    AIResult forged;
    forged.kind = AIResult::UseCard;
    forged.handled = true;
    forged.decisionId = request.decisionId;
    forged.stateRevision = request.stateRevision;
    forged.action.useCardId = slash->getEffectiveId();
    forged.action.selectedTargetNames << viewer->objectName();

    CardUseStruct use;
    if (!RoomTestAccess::applyResult(*room, viewer, request, forged, use)) {
        qCritical() << "The owned card was rejected before the target check ran";
        return false;
    }
    if (!use.m_validateTargets) {
        qCritical() << "An AI-selected target reached gameplay without revalidation";
        return false;
    }
    if (RoomTestAccess::areCardTargetsLegal(*room, use)) {
        qCritical() << "A Slash aimed at its own user passed the target contract";
        return false;
    }
    // End to end: useCard refuses it, and it refuses before touching any game state.
    const int handBefore = viewer->getHandcardNum();
    if (room->useCard(use)) {
        qCritical() << "An illegal AI use was executed";
        return false;
    }
    if (viewer->getHandcardNum() != handBefore) {
        qCritical() << "A rejected AI use still moved cards";
        return false;
    }

    // The same answer with the target the authority did offer passes the contract.
    AIResult legal = forged;
    legal.action.selectedTargetNames = QStringList({other->objectName()});
    CardUseStruct legalUse;
    if (!RoomTestAccess::applyResult(*room, viewer, request, legal, legalUse)
        || !legalUse.m_validateTargets
        || !RoomTestAccess::areCardTargetsLegal(*room, legalUse)) {
        qCritical() << "The offered target was rejected by the target contract";
        return false;
    }

    return true;
}

// The author-facing API is plain Lua: sorting, the use plan and the unsupported signal
// need the sandbox and the shared core, but no Room state, no Engine and no package
// handler.  Loading it beside decision-core.lua is what makes it a contract rather than
// a description - a renamed method or a swallowed unsupported signal fails here.
static bool useCardPlanContract()
{
    ScopedConfigValue scripts(QStringLiteral("AiIsolatedScripts"),
                              QStringList({QStringLiteral("decision-core.lua"), QStringLiteral("strategy-hooks.lua")}));
    Room room(nullptr, QStringLiteral("02_1v1"));
    if (!room.roomRuntime()->ai().lua().rawState())
        return false;
    LuaRuntime &runtime = room.roomRuntime()->ai().lua();
    LuaRuntime::Binding luaBinding(runtime);
    const int top = lua_gettop(runtime.state());
    QString error;
    const bool loaded = runtime.loadScript(
        QStringLiteral("tests/lua/isolated-use-plan-contract.lua"), &error);
    lua_settop(runtime.state(), top);
    if (!loaded) {
        qCritical().noquote() << error;
        return false;
    }
    return true;
}

// The first vertical slice: a physical Slash and a physical Peach planned by the shared
// card strategies. All three outcomes must be observable and each must carry a reason -
// a plan, a strategy that declines, and a context the AI does not cover. The card's
// judgement must also be the same whether activate asked for it or a handler asked
// aiUseCard directly, because there is only one target algorithm underneath.
static bool slashAndPeachPlanThroughOneSharedPath()
{
    ScopedConfigValue scripts(QStringLiteral("AiIsolatedScripts"),
                              QStringList({QStringLiteral("decision-core.lua"), QStringLiteral("strategy-hooks.lua")}));
    Room room(nullptr, QStringLiteral("02_1v1"));
    if (!room.roomRuntime()->ai().lua().rawState())
        return false;

    AIRequest request;
    request.kind = AIRequest::Activate;
    request.viewerObjectName = QStringLiteral("vertical-owner");
    request.worldView.self.objectName = request.viewerObjectName;
    request.worldView.self.alive = true;
    request.worldView.self.dead = false;
    request.worldView.self.hp = 4;
    request.worldView.self.maxHp = 4;
    request.worldView.self.wounded = false;
    request.worldView.self.attackRange = 1;
    request.worldView.currentPlayer = request.viewerObjectName;
    request.worldView.playerOrder << request.viewerObjectName
                                  << QStringLiteral("vertical-weak")
                                  << QStringLiteral("vertical-strong");
    request.worldView.alivePlayerOrder = request.worldView.playerOrder;

    AIPlayerView weak;
    weak.objectName = QStringLiteral("vertical-weak");
    weak.alive = true;
    weak.dead = false;
    weak.hp = 1;
    weak.handcardCount = 0;
    weak.handVisible = true;
    AIPlayerView strong = weak;
    strong.objectName = QStringLiteral("vertical-strong");
    strong.hp = 5;
    request.worldView.players << weak << strong;

    AICardView slash;
    slash.cardId = 7;
    slash.effectiveId = 7;
    slash.objectName = QStringLiteral("slash");
    slash.className = QStringLiteral("Slash");
    slash.kindOfNames << QStringLiteral("Slash") << QStringLiteral("BasicCard");
    AICardView peach;
    peach.cardId = 9;
    peach.effectiveId = 9;
    peach.objectName = QStringLiteral("peach");
    peach.className = QStringLiteral("Peach");
    peach.kindOfNames << QStringLiteral("Peach") << QStringLiteral("BasicCard");
    request.worldView.handCards << slash << peach;

    QJsonObject relations, ownRow, objectives;
    ownRow.insert(QStringLiteral("vertical-weak"), QStringLiteral("enemy"));
    ownRow.insert(QStringLiteral("vertical-strong"), QStringLiteral("enemy"));
    relations.insert(request.viewerObjectName, ownRow);
    objectives.insert(QStringLiteral("vertical-weak"), 5);
    objectives.insert(QStringLiteral("vertical-strong"), 5);
    QJsonObject policy;
    policy.insert(QStringLiteral("managed"), true);
    policy.insert(QStringLiteral("relations"), relations);
    policy.insert(QStringLiteral("objectives"), objectives);
    request.worldView.modePolicy = policy;

    AICardCandidateView slashCandidate;
    slashCandidate.cardId = 7;
    slashCandidate.available = true;
    slashCandidate.completeCoverage = true;
    slashCandidate.legalTargets << QStringLiteral("vertical-strong")
                                << QStringLiteral("vertical-weak");
    for (const QString &name : slashCandidate.legalTargets)
        slashCandidate.targetCombinations << QStringList{name};
    AICardCandidateView peachCandidate;
    peachCandidate.cardId = 9;
    peachCandidate.available = true;
    peachCandidate.targetFixed = true;
    peachCandidate.feasibleWithNoTarget = true;
    peachCandidate.completeCoverage = true;
    // A legal target-free action is one empty sequence, not zero feasible sequences.
    peachCandidate.targetCombinations << QStringList();
    request.cardCandidates << slashCandidate << peachCandidate;
    // This fixture is a complete question: it carries no view-as skill, so "no
    // conversion" here is an answer rather than something this side failed to work out.
    request.conversionsEnumerated = true;

    // Plan: the Slash outranks the unwounded Peach and goes to the weaker enemy, not
    // simply to the first legal target the authority listed.
    const AIResult planned = room.roomRuntime()->ai().decideIsolated(request);
    if (!planned.handled || planned.kind != AIResult::UseCard
        || planned.action.useCardId != 7
        || planned.action.selectedTargetNames != QStringList({QStringLiteral("vertical-weak")})
        || !planned.errorCode.isEmpty()) {
        qCritical() << "The Slash was not planned onto the weaker enemy"
                    << planned.handled << planned.action.useCardId
                    << planned.action.selectedTargetNames << planned.errorCode;
        return false;
    }

    // Unknown alternatives remain coverage debt, but cannot erase a separately
    // authorized Slash plan. The selected answer never needs a legacy AI callback.
    AIRequest mixed = request;
    AICardView trick;
    trick.cardId = 11;
    trick.effectiveId = 11;
    trick.objectName = QStringLiteral("uncovered_contract_card");
    trick.className = QStringLiteral("UncoveredContractCard");
    trick.kindOfNames << QStringLiteral("UncoveredContractCard") << QStringLiteral("TrickCard");
    mixed.worldView.handCards << trick;
    AICardCandidateView trickCandidate;
    trickCandidate.cardId = 11;
    trickCandidate.available = true;
    trickCandidate.targetFixed = true;
    trickCandidate.feasibleWithNoTarget = true;
    trickCandidate.completeCoverage = true;
    trickCandidate.targetCombinations << QStringList();
    mixed.cardCandidates << trickCandidate;
    const AIResult partial = room.roomRuntime()->ai().decideIsolated(mixed);
    if (!partial.handled || partial.kind != AIResult::UseCard
        || partial.action.useCardId != 7 || !partial.errorCode.isEmpty()) {
        qCritical() << "An unknown alternative erased an authorized Slash plan"
                    << partial.handled << int(partial.kind) << partial.action.useCardId;
        return false;
    }

    AIRequest unknownOnly = mixed;
    unknownOnly.cardCandidates = {trickCandidate};
    const AIResult unknown = room.roomRuntime()->ai().decideIsolated(unknownOnly);
    if (unknown.handled || !unknown.errorCode.isEmpty()) {
        qCritical() << "An unknown-only turn was mistaken for a completed decision";
        return false;
    }

    // Decline: with only an unwounded Peach in hand there is a strategy, and it says no.
    {
        LuaRuntime::Binding luaBinding(room.roomRuntime()->ai().lua());
        lua_State *L = room.roomRuntime()->ai().lua().state();
        if (luaL_dostring(L, "ai_coverage.clearUncovered()") != 0) {
            lua_pop(L, 1);
            return false;
        }
    }
    AIRequest peachOnly = request;
    peachOnly.cardCandidates.clear();
    peachOnly.cardCandidates << peachCandidate;
    const AIResult declined = room.roomRuntime()->ai().decideIsolated(peachOnly);
    if (!declined.handled || declined.kind != AIResult::Pass
        || !declined.errorCode.isEmpty()) {
        qCritical() << "An unwounded Peach was not declined as a pass"
                    << declined.handled << int(declined.kind) << declined.errorCode;
        return false;
    }

    // Wounded to the bone, the same Peach is played; the strategy, not the card, changed.
    AIRequest dying = peachOnly;
    dying.worldView.self.hp = 1;
    dying.worldView.self.wounded = true;
    const AIResult healed = room.roomRuntime()->ai().decideIsolated(dying);
    if (!healed.handled || healed.kind != AIResult::UseCard
        || healed.action.useCardId != 9 || !healed.action.selectedTargetNames.isEmpty()) {
        qCritical() << "A dying viewer did not drink its own Peach"
                    << healed.handled << healed.action.useCardId;
        return false;
    }

    // Unknown relations are coverage debt, not "no enemies" or a legal pass.
    AIRequest unmanaged = request;
    unmanaged.worldView.modePolicy = QJsonObject();
    const AIResult uncovered = room.roomRuntime()->ai().decideIsolated(unmanaged);
    if (uncovered.handled || !uncovered.errorCode.isEmpty()) {
        qCritical() << "An uncovered context was mistaken for a completed decision"
                    << uncovered.handled << int(uncovered.kind) << uncovered.errorCode;
        return false;
    }
    {
        LuaRuntime::Binding luaBinding(room.roomRuntime()->ai().lua());
        lua_State *L = room.roomRuntime()->ai().lua().state();
        if (luaL_dostring(L,
            "local entries = ai_coverage.uncovered(); "
            "uncovered_probe = (#entries == 1) and (entries[1].kind .. '|' .. "
            "entries[1].key .. '|' .. entries[1].reason) or ('count=' .. #entries)") != 0) {
            lua_pop(L, 1);
            return false;
        }
        lua_getglobal(L, "uncovered_probe");
        const QString probe = QString::fromUtf8(lua_tostring(L, -1));
        lua_pop(L, 1);
        if (probe != QStringLiteral("activate|Slash|the mode policy does not describe "
                                    "relations")) {
            qCritical() << "The uncovered context was not recorded with its reason" << probe;
            return false;
        }
    }

    // Same card, asked directly through aiUseCard: same target. Registering this
    // probe replaces the core's activate handler, so it runs last.
    {
        LuaRuntime::Binding luaBinding(room.roomRuntime()->ai().lua());
        lua_State *L = room.roomRuntime()->ai().lua().state();
        if (luaL_dostring(L,
            "ai_register_handler('activate', function(self, request) "
            "local hand = self.player:getHandcards(); "
            "for _, held in ipairs(hand) do "
            "if held:isKindOf('Slash') then "
            "local plan, status = self:tryUseCard(held); "
            "if status ~= 'planned' then return nil end; "
            "return plan:toAnswer() end end; "
            "return nil end)") != 0) {
            qCritical().noquote() << QString::fromUtf8(lua_tostring(L, -1));
            lua_pop(L, 1);
            return false;
        }
    }
    const AIResult direct = room.roomRuntime()->ai().decideIsolated(request);
    if (!direct.handled || direct.action.useCardId != planned.action.useCardId
        || direct.action.selectedTargetNames != planned.action.selectedTargetNames) {
        qCritical() << "Direct planning disagreed with the activate path"
                    << direct.action.useCardId << direct.action.selectedTargetNames;
        return false;
    }

    return true;
}

// PR 05: Duel and the strip family (Snatch / Dismantlement). Two things have to hold
// at once here. A card family may not reuse the Slash target order blindly - the shared
// rankTargets still decides who counts as an enemy and who is dangerous, but what a
// Snatch is worth against each of them is that card's own question, and in this fixture
// the two answers differ on purpose. And what the viewer knows about their own hand may
// not be confused with what they guess about someone else's: the Duel is a race of
// Slash counts, exact on one side and an estimate on the other.
static bool trickFamiliesPlanWithTheirOwnValuations()
{
    ScopedConfigValue scripts(QStringLiteral("AiIsolatedScripts"),
                              QStringList({QStringLiteral("ask-for-use-card.lua"),
                                           QStringLiteral("ask-for-choice.lua"),
                                           QStringLiteral("decision-core.lua"), QStringLiteral("strategy-hooks.lua")}));
    Room room(nullptr, QStringLiteral("02_1v1"));
    if (!room.roomRuntime()->ai().lua().rawState())
        return false;
    // The per-family boundaries are plain Lua, so they run in this same VM rather than
    // paying for a second Room: every branch of each strategy, including the reasons
    // its unsupported signals carry.
    {
        LuaRuntime &runtime = room.roomRuntime()->ai().lua();
        LuaRuntime::Binding luaBinding(runtime);
        const int top = lua_gettop(runtime.state());
        QString error;
        const bool loaded = runtime.loadScript(
            QStringLiteral("tests/lua/isolated-trick-families-contract.lua"), &error);
        lua_settop(runtime.state(), top);
        if (!loaded) {
            qCritical().noquote() << error;
            return false;
        }
    }

    AIRequest request;
    request.kind = AIRequest::Activate;
    // A complete question: this fixture carries no view-as skill, so "no
    // conversion" is its answer rather than something it failed to work out.
    request.conversionsEnumerated = true;
    request.viewerObjectName = QStringLiteral("strip-owner");
    request.worldView.self.objectName = request.viewerObjectName;
    request.worldView.self.alive = true;
    request.worldView.self.dead = false;
    request.worldView.self.hp = 4;
    request.worldView.self.maxHp = 4;
    request.worldView.self.attackRange = 1;
    request.worldView.currentPlayer = request.viewerObjectName;
    request.worldView.playerOrder << request.viewerObjectName
                                  << QStringLiteral("strip-poor")
                                  << QStringLiteral("strip-rich");
    request.worldView.alivePlayerOrder = request.worldView.playerOrder;

    AICardView crossbow;
    crossbow.cardId = 30;
    crossbow.effectiveId = 30;
    crossbow.objectName = QStringLiteral("crossbow");
    crossbow.className = QStringLiteral("Crossbow");
    crossbow.kindOfNames << QStringLiteral("Crossbow") << QStringLiteral("Weapon")
                         << QStringLiteral("EquipCard");

    // The shared ranking puts the dying enemy first; the richer one is worth more to
    // strip. One fixture, two different right answers depending on the card asked.
    AIPlayerView poor;
    poor.objectName = QStringLiteral("strip-poor");
    poor.alive = true;
    poor.dead = false;
    poor.hp = 1;
    poor.maxHp = 4;
    poor.handcardCount = 1;
    AIPlayerView rich = poor;
    rich.objectName = QStringLiteral("strip-rich");
    rich.hp = 4;
    rich.equips << crossbow;
    request.worldView.players << poor << rich;

    QJsonObject relations, ownRow;
    ownRow.insert(QStringLiteral("strip-poor"), QStringLiteral("enemy"));
    ownRow.insert(QStringLiteral("strip-rich"), QStringLiteral("enemy"));
    relations.insert(request.viewerObjectName, ownRow);
    QJsonObject policy;
    policy.insert(QStringLiteral("managed"), true);
    policy.insert(QStringLiteral("relations"), relations);
    policy.insert(QStringLiteral("objectives"), QJsonObject());
    request.worldView.modePolicy = policy;

    AICardView snatch;
    snatch.cardId = 21;
    snatch.effectiveId = 21;
    snatch.objectName = QStringLiteral("snatch");
    snatch.className = QStringLiteral("Snatch");
    snatch.kindOfNames << QStringLiteral("Snatch") << QStringLiteral("SingleTargetTrick")
                       << QStringLiteral("TrickCard");
    AICardCandidateView snatchCandidate;
    snatchCandidate.cardId = 21;
    snatchCandidate.available = true;
    snatchCandidate.completeCoverage = true;
    snatchCandidate.legalTargets << QStringLiteral("strip-poor")
                                 << QStringLiteral("strip-rich");
    for (const QString &name : snatchCandidate.legalTargets)
        snatchCandidate.targetCombinations << QStringList{name};

    AIRequest stripping = request;
    stripping.worldView.self.handcardCount = 1;
    stripping.worldView.handCards << snatch;
    stripping.cardCandidates << snatchCandidate;
    const AIResult snatched = room.roomRuntime()->ai().decideIsolated(stripping);
    if (!snatched.handled || snatched.kind != AIResult::UseCard
        || snatched.action.useCardId != 21
        || snatched.action.selectedTargetNames
               != QStringList({QStringLiteral("strip-rich")})
        || !snatched.errorCode.isEmpty()) {
        qCritical() << "The Snatch did not go to the target worth stripping"
                    << snatched.handled << snatched.action.useCardId
                    << snatched.action.selectedTargetNames << snatched.errorCode;
        return false;
    }

    // The Duel is a Slash race, and the two counts come from different places: the
    // viewer's own two Slashes are known exactly, the enemy's single face-down card is
    // only an estimate. Two against an estimate below one is worth opening, and it goes
    // to the enemy the shared ranking put first - the strip order above was that card's
    // own judgement, not a replacement for the ranking.
    AICardView duel;
    duel.cardId = 20;
    duel.effectiveId = 20;
    duel.objectName = QStringLiteral("duel");
    duel.className = QStringLiteral("Duel");
    duel.kindOfNames << QStringLiteral("Duel") << QStringLiteral("TrickCard");
    AICardView heldSlash;
    heldSlash.cardId = 7;
    heldSlash.effectiveId = 7;
    heldSlash.objectName = QStringLiteral("slash");
    heldSlash.className = QStringLiteral("Slash");
    heldSlash.kindOfNames << QStringLiteral("Slash") << QStringLiteral("BasicCard");
    AICardView secondSlash = heldSlash;
    secondSlash.cardId = 8;
    secondSlash.effectiveId = 8;
    AICardCandidateView duelCandidate;
    duelCandidate.cardId = 20;
    duelCandidate.available = true;
    duelCandidate.completeCoverage = true;
    duelCandidate.legalTargets << QStringLiteral("strip-poor")
                               << QStringLiteral("strip-rich");
    for (const QString &name : duelCandidate.legalTargets)
        duelCandidate.targetCombinations << QStringList{name};

    AIRequest armed = request;
    armed.worldView.self.handcardCount = 3;
    armed.worldView.handCards << duel << heldSlash << secondSlash;
    armed.cardCandidates << duelCandidate;
    const AIResult opened = room.roomRuntime()->ai().decideIsolated(armed);
    if (!opened.handled || opened.kind != AIResult::UseCard
        || opened.action.useCardId != 20
        || opened.action.selectedTargetNames
               != QStringList({QStringLiteral("strip-poor")})
        || !opened.errorCode.isEmpty()) {
        qCritical() << "The Duel was not opened against the ranked enemy"
                    << opened.handled << opened.action.useCardId
                    << opened.action.selectedTargetNames << opened.errorCode;
        return false;
    }

    // Same Duel, same enemies, no Slash in hand: the race is lost before it starts.
    // That is a strategy declining, so the turn answers with a pass - it is not an
    // uncovered context and must not be reported as one.
    AIRequest unarmed = request;
    unarmed.worldView.self.handcardCount = 1;
    unarmed.worldView.handCards << duel;
    unarmed.cardCandidates << duelCandidate;
    const AIResult refused = room.roomRuntime()->ai().decideIsolated(unarmed);
    if (!refused.handled || refused.kind != AIResult::Pass
        || !refused.errorCode.isEmpty()) {
        qCritical() << "A Duel with no Slash behind it was not declined as a pass"
                    << refused.handled << int(refused.kind) << refused.errorCode;
        return false;
    }

    // Relations unknown is still not "no enemies" for the new families either.
    {
        LuaRuntime::Binding luaBinding(room.roomRuntime()->ai().lua());
        lua_State *L = room.roomRuntime()->ai().lua().state();
        if (luaL_dostring(L, "ai_coverage.clearUncovered()") != 0) {
            lua_pop(L, 1);
            return false;
        }
    }
    AIRequest unmanaged = stripping;
    unmanaged.worldView.modePolicy = QJsonObject();
    const AIResult uncovered = room.roomRuntime()->ai().decideIsolated(unmanaged);
    if (uncovered.handled || !uncovered.errorCode.isEmpty()) {
        qCritical() << "An unmanaged mode answered a Snatch instead of falling back"
                    << uncovered.handled << int(uncovered.kind);
        return false;
    }
    {
        LuaRuntime::Binding luaBinding(room.roomRuntime()->ai().lua());
        lua_State *L = room.roomRuntime()->ai().lua().state();
        if (luaL_dostring(L,
            "local entries = ai_coverage.uncovered(); "
            "uncovered_strip = (#entries == 1) and (entries[1].kind .. '|' .. "
            "entries[1].key .. '|' .. entries[1].reason) or ('count=' .. #entries)") != 0) {
            lua_pop(L, 1);
            return false;
        }
        lua_getglobal(L, "uncovered_strip");
        const QString probe = QString::fromUtf8(lua_tostring(L, -1));
        lua_pop(L, 1);
        if (probe != QStringLiteral("activate|Snatch|the mode policy does not describe "
                                    "relations")) {
            qCritical() << "The uncovered Snatch was not recorded with its reason" << probe;
            return false;
        }
    }
    return true;
}

// The development machine's config.ini carries AiIsolatedScripts, and that override is
// taken literally, so the discovery path can only be observed with the key gone.
class ScopedConfigRemoval
{
public:
    explicit ScopedConfigRemoval(const QString &key)
        : m_key(key), m_existed(Config.contains(key)), m_previous(Config.value(key))
    {
        Config.remove(key);
    }

    ~ScopedConfigRemoval()
    {
        if (m_existed)
            Config.setValue(m_key, m_previous);
    }

private:
    QString m_key;
    bool m_existed;
    QVariant m_previous;
};

// With no configured list the runtime is assembled the way smart-ai.lua assembles
// itself: isolated-bootstrap.lua declares its own dispatcher core, and a package's
// handlers arrive because that package is enabled and lua/ai/isolated/<package>-ai.lua
// exists.  Nothing here may depend on a C++ script list or on a local config.ini.
static bool isolatedScriptsComeFromLuaAndEnabledPackages()
{
    ScopedConfigRemoval scripts(QStringLiteral("AiIsolatedScripts"));
    Room room(nullptr, QStringLiteral("02_1v1"));
    QString error;
    room.roomRuntime()->ai().shutdown();
    if (!room.roomRuntime()->ai().initialize(&error)) {
        qCritical() << "Isolated runtime did not assemble itself" << error;
        return false;
    }
    LuaRuntime::Binding luaBinding(room.roomRuntime()->ai().lua());
    lua_State *L = room.roomRuntime()->ai().lua().state();
    if (luaL_dostring(L,
        "assembly_probe = table.concat({"
        // The core list is Lua's to declare, and every file it names is loaded.
        "tostring(type(ai_isolated_core) == 'table' and #ai_isolated_core >= 3), "
        "tostring(type(ai_skill_use) == 'table'), "
        "tostring(type(ai_skill_choice) == 'table'), "
        "tostring(type(ai_keep_value) == 'table'), "
        // standard-ai.lua is named by no C++ list; it loads because the standard
        // package is enabled and the file is on disk under that package's name.
        "tostring(type(ai_skill_use['@@lianying']) == 'function')}, '|')") != 0) {
        lua_pop(L, 1);
        return false;
    }
    lua_getglobal(L, "assembly_probe");
    const QString probe = QString::fromUtf8(lua_tostring(L, -1));
    lua_pop(L, 1);
    if (probe != QStringLiteral("true|true|true|true|true")) {
        qCritical() << "Isolated runtime assembly did not match the Lua declaration"
                    << probe;
        return false;
    }
    return true;
}

// A package whose isolated handler is broken loses its own AI and nothing else: the
// dispatchers stay up, exactly as smart-ai.lua's pcall keeps one damaged package AI
// from taking the whole AI down.
static bool aBrokenPackageHandlerDoesNotStopTheRuntime()
{
    const QStringList enabled = Sanguosha ? Sanguosha->getExtensions() : QStringList();
    if (enabled.isEmpty())
        return false;
    // Pick an enabled package that has no isolated handler yet, so the temporary file
    // is the only thing this case adds to the tree.
    QString victim;
    foreach (const QString &package, enabled) {
        const QString candidate = QStringLiteral("lua/ai/isolated/%1-ai.lua")
            .arg(package.toLower());
        if (!QFile::exists(candidate)) {
            victim = candidate;
            break;
        }
    }
    if (victim.isEmpty())
        return false;
    QFile broken(victim);
    if (!broken.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    broken.write("this is not valid lua(");
    broken.close();

    ScopedConfigRemoval scripts(QStringLiteral("AiIsolatedScripts"));
    Room room(nullptr, QStringLiteral("02_1v1"));
    QString error;
    room.roomRuntime()->ai().shutdown();
    const bool initialized = room.roomRuntime()->ai().initialize(&error);
    bool dispatchersSurvived = false;
    if (initialized) {
        LuaRuntime::Binding luaBinding(room.roomRuntime()->ai().lua());
        lua_State *L = room.roomRuntime()->ai().lua().state();
        lua_getglobal(L, "ai_skill_use");
        dispatchersSurvived = lua_istable(L, -1);
        lua_pop(L, 1);
    }
    QFile::remove(victim);
    if (!initialized || !dispatchersSurvived) {
        qCritical() << "A broken package handler took the isolated runtime down"
                    << error;
        return false;
    }
    return true;
}

// A conversion is something the authority issued, never something an author asserted.
// The old buildSpecCard let three different forgeries through - any registered view-as
// skill name, any engine card name, and a skill addressed by bare name so neither the
// instance nor its quota was checked - so the contract here is that a card_spec buys
// nothing except by naming a ticket this very request handed out, and that every field
// beside the ticket is checked against the authority's own record rather than trusted.
static bool conversionsAreAuthorizedNotClaimed()
{
    {
        // The author-facing half first: a renamed facade method, a dropped ticket or a
        // swallowed unsupported signal fails here rather than further in.
        ScopedConfigValue scripts(QStringLiteral("AiIsolatedScripts"),
                                  QStringList({QStringLiteral("decision-core.lua"), QStringLiteral("strategy-hooks.lua")}));
        Room contractRoom(nullptr, QStringLiteral("02_1v1"));
        LuaRuntime &runtime = contractRoom.roomRuntime()->ai().lua();
        if (!runtime.rawState()) {
            qCritical() << "The conversion contract had no isolated Lua state";
            return false;
        }
        LuaRuntime::Binding luaBinding(runtime);
        const int top = lua_gettop(runtime.state());
        QString error;
        const bool loaded = runtime.loadScript(
            QStringLiteral("tests/lua/isolated-conversion-contract.lua"), &error);
        lua_settop(runtime.state(), top);
        if (!loaded) {
            qCritical().noquote() << error;
            return false;
        }
    }

    std::unique_ptr<Room> room(new Room(nullptr, QStringLiteral("02_1v1")));
    EngineRuntimeContextScope contextScope(*Sanguosha, room.get());
    room->roomRuntime()->state().reset();
    ServerPlayer *viewer = RoomTestAccess::addRobotPlayer(*room);
    viewer->setObjectName(QStringLiteral("conversion-viewer"));
    viewer->setSeat(1);
    viewer->setMaxHp(4);
    viewer->setHp(4);
    viewer->setPhase(Player::Play);
    ServerPlayer *other = RoomTestAccess::addRobotPlayer(*room);
    other->setObjectName(QStringLiteral("conversion-other"));
    other->setSeat(2);
    other->setMaxHp(4);
    other->setHp(4);
    room->setCurrent(viewer);
    room->rebuildAlivePlayers();

    if (!Sanguosha->getViewAsSkill(QStringLiteral("active_skill_v2_test"))
        || !Sanguosha->getViewAsSkill(QStringLiteral("active_skill_v2_cost_test"))) {
        qCritical() << "The ~test conversion fixtures are not registered";
        return false;
    }
    // A red card to pay the cost fixture with, and one the cost fixture must refuse.
    const Card *red = nullptr;
    const Card *black = nullptr;
    for (int id = 0; id < Sanguosha->getCardCount() && (!red || !black); ++id) {
        const Card *card = Sanguosha->getCard(id);
        if (!card) continue;
        if (!red && card->isRed()) red = card;
        if (!black && card->isBlack()) black = card;
    }
    if (!red || !black) {
        qCritical() << "The deck did not offer one red and one black card";
        return false;
    }
    viewer->addCard(red->getEffectiveId(), Player::PlaceHand);
    viewer->addCard(black->getEffectiveId(), Player::PlaceHand);

    const int freeId = viewer->createSkillInstance(
        QStringLiteral("active_skill_v2_test"), SourceAcquired, true);
    const int costId = viewer->createSkillInstance(
        QStringLiteral("active_skill_v2_cost_test"), SourceAcquired, true);
    if (freeId <= 0 || costId <= 0) {
        qCritical() << "The conversion skills were not instantiated" << freeId << costId;
        return false;
    }

    const AIRequest request = RoomTestAccess::makeRequest(*room, viewer,
        AIRequest::Activate, CardUseStruct::CARD_USE_REASON_PLAY, QString(),
        Card::MethodUse);
    if (!request.conversionsEnumerated) {
        qCritical() << "The request did not claim to have enumerated its conversions";
        return false;
    }

    // The zero-cost fixture converts into a Slash; the cost fixture converts into a
    // Slash by paying one red card and must not offer the black one.
    const AICardConversionView *freeSlash = nullptr;
    const AICardConversionView *paidSlash = nullptr;
    foreach (const AICardConversionView &conversion, request.cardConversions) {
        if (conversion.activationRef.key.skillName == QStringLiteral("active_skill_v2_test"))
            freeSlash = &conversion;
        if (conversion.activationRef.key.skillName == QStringLiteral("active_skill_v2_cost_test")) {
            if (conversion.subcardIds == QList<int>({black->getEffectiveId()})) {
                qCritical() << "A cost the skill refuses was still enumerated";
                return false;
            }
            if (conversion.subcardIds == QList<int>({red->getEffectiveId()}))
                paidSlash = &conversion;
        }
    }
    if (!freeSlash || !paidSlash) {
        qCritical() << "The conversions were not enumerated"
                    << request.cardConversions.size() << bool(freeSlash) << bool(paidSlash);
        return false;
    }
    // The produced card's identity comes from the authority's own build.
    if (freeSlash->name != QStringLiteral("slash")
        || !freeSlash->kindOfNames.contains(QStringLiteral("Slash"))
        || !freeSlash->subcardIds.isEmpty()
        || paidSlash->name != QStringLiteral("slash")
        || paidSlash->subcardIds != QList<int>({red->getEffectiveId()})
        || freeSlash->conversionId == paidSlash->conversionId) {
        qCritical() << "A conversion did not describe the card the authority built"
                    << freeSlash->name << freeSlash->kindOfNames << paidSlash->name
                    << paidSlash->subcardIds << freeSlash->conversionId
                    << paidSlash->conversionId;
        return false;
    }
    // Targets are described by the same probe physical candidates get.
    if (!freeSlash->legalTargets.contains(other->objectName())
        || freeSlash->legalTargets.contains(viewer->objectName())
        || !freeSlash->completeCoverage) {
        qCritical() << "The conversion's targets were not described like a card's"
                    << freeSlash->legalTargets << freeSlash->completeCoverage;
        return false;
    }

    // A ticket-bearing answer is accepted and carries the instance identity forward.
    AIResult honest;
    honest.kind = AIResult::UseCard;
    honest.handled = true;
    honest.decisionId = request.decisionId;
    honest.stateRevision = request.stateRevision;
    honest.action.hasCardSpec = true;
    honest.action.cardSpec.conversionId = paidSlash->conversionId;
    honest.action.cardSpec.name = paidSlash->name;
    honest.action.cardSpec.suit = paidSlash->suit;
    honest.action.cardSpec.number = paidSlash->number;
    honest.action.cardSpec.skillName = paidSlash->activationRef.key.skillName;
    honest.action.cardSpec.subcardIds = paidSlash->subcardIds;
    honest.action.selectedTargetNames << other->objectName();
    CardUseStruct honestUse;
    if (!RoomTestAccess::applyResult(*room, viewer, request, honest, honestUse)
        || !honestUse.card
        || honestUse.card->objectName() != QStringLiteral("slash")
        || honestUse.activationRef != paidSlash->activationRef
        || honestUse.sourceRef != paidSlash->sourceRef
        || !honestUse.m_validateTargets) {
        qCritical() << "An authorized conversion was refused or lost its identity"
                    << bool(honestUse.card) << honestUse.m_validateTargets;
        return false;
    }

    // Forgery 1: a card name the authority never issued. This is the one the old code
    // waved through - it cloned whatever name arrived.
    AIResult forgedName = honest;
    forgedName.action.cardSpec.name = QStringLiteral("peach");
    CardUseStruct forgedNameUse;
    if (RoomTestAccess::applyResult(*room, viewer, request, forgedName, forgedNameUse)) {
        qCritical() << "A forged card name was built anyway";
        return false;
    }
    // Forgery 2: the authorized card, paid for with a different card of the hand.
    AIResult forgedCost = honest;
    forgedCost.action.cardSpec.subcardIds = QList<int>({black->getEffectiveId()});
    CardUseStruct forgedCostUse;
    if (RoomTestAccess::applyResult(*room, viewer, request, forgedCost, forgedCostUse)) {
        qCritical() << "A forged cost was accepted";
        return false;
    }
    // Forgery 3: no ticket at all. A spec that names no conversion of this request
    // authorizes nothing, however well formed it is.
    AIResult noTicket = honest;
    noTicket.action.cardSpec.conversionId = -1;
    CardUseStruct noTicketUse;
    if (RoomTestAccess::applyResult(*room, viewer, request, noTicket, noTicketUse)) {
        qCritical() << "A card spec with no conversion ticket was honoured";
        return false;
    }
    // Forgery 4: a ticket this request never issued.
    AIResult strayTicket = honest;
    strayTicket.action.cardSpec.conversionId = 9999;
    CardUseStruct strayTicketUse;
    if (RoomTestAccess::applyResult(*room, viewer, request, strayTicket, strayTicketUse)) {
        qCritical() << "A conversion ticket from no request was honoured";
        return false;
    }
    // Forgery 5: naming a skill the player does not hold. The old code accepted any
    // globally registered view-as skill name here.
    AIResult borrowedName = honest;
    borrowedName.action.cardSpec.skillName = QStringLiteral("active_skill_v2_proxy_ui_test");
    CardUseStruct borrowedNameUse;
    if (RoomTestAccess::applyResult(*room, viewer, request, borrowedName, borrowedNameUse)) {
        qCritical() << "A skill name that does not own this conversion was honoured";
        return false;
    }

    // Same name, two instances: each is its own conversion and one ticket never
    // authorizes the other.
    const int secondFreeId = viewer->createSkillInstance(
        QStringLiteral("active_skill_v2_test"), SourceAcquired, true);
    if (secondFreeId <= 0 || secondFreeId == freeId) {
        qCritical() << "A second instance of the same skill was not created"
                    << secondFreeId << freeId;
        return false;
    }
    const AIRequest twoInstances = RoomTestAccess::makeRequest(*room, viewer,
        AIRequest::Activate, CardUseStruct::CARD_USE_REASON_PLAY, QString(),
        Card::MethodUse);
    QList<int> instanceIds;
    foreach (const AICardConversionView &conversion, twoInstances.cardConversions) {
        if (conversion.activationRef.key.skillName == QStringLiteral("active_skill_v2_test"))
            instanceIds << conversion.activationRef.key.instanceID;
    }
    if (!instanceIds.contains(freeId) || !instanceIds.contains(secondFreeId)) {
        qCritical() << "Two instances of one skill did not produce two conversions"
                    << instanceIds;
        return false;
    }

    // Quota: the fixture spends against its source, so filling the source's usage mark
    // refuses the conversion at apply time even though the ticket was issued before.
    const AICardConversionView *quotaTarget = nullptr;
    foreach (const AICardConversionView &conversion, twoInstances.cardConversions) {
        if (conversion.activationRef.key.skillName == QStringLiteral("active_skill_v2_test")) {
            quotaTarget = &conversion;
            break;
        }
    }
    if (!quotaTarget) {
        qCritical() << "No conversion to exhaust the quota of";
        return false;
    }
    AIResult quotaAnswer;
    quotaAnswer.kind = AIResult::UseCard;
    quotaAnswer.handled = true;
    quotaAnswer.decisionId = twoInstances.decisionId;
    quotaAnswer.stateRevision = twoInstances.stateRevision;
    quotaAnswer.action.hasCardSpec = true;
    quotaAnswer.action.cardSpec.conversionId = quotaTarget->conversionId;
    quotaAnswer.action.cardSpec.name = quotaTarget->name;
    quotaAnswer.action.cardSpec.suit = quotaTarget->suit;
    quotaAnswer.action.cardSpec.number = quotaTarget->number;
    quotaAnswer.action.cardSpec.subcardIds = quotaTarget->subcardIds;
    quotaAnswer.action.selectedTargetNames << other->objectName();
    CardUseStruct beforeQuota;
    if (!RoomTestAccess::applyResult(*room, viewer, twoInstances, quotaAnswer, beforeQuota)) {
        qCritical() << "The conversion was refused before its quota was even spent";
        return false;
    }
    const QString usageKey = SkillInstanceUtils::formatUsageMarkKey(
        quotaTarget->sourceRef.key.skillName, quotaTarget->sourceRef.key.instanceID,
        QStringLiteral("-Clear"));
    ServerPlayer *quotaHolder = room->findPlayerByObjectName(
        quotaTarget->sourceRef.ownerObjectName);
    if (usageKey.isEmpty() || !quotaHolder) {
        qCritical() << "The conversion did not name a usage holder" << usageKey;
        return false;
    }
    quotaHolder->setMark(usageKey, 2);
    CardUseStruct afterQuota;
    if (RoomTestAccess::applyResult(*room, viewer, twoInstances, quotaAnswer, afterQuota)) {
        qCritical() << "An exhausted quota still produced the conversion" << usageKey;
        return false;
    }
    quotaHolder->setMark(usageKey, 0);

    // A physical card still needs the candidate this request offered: owning it is not
    // the same as having been asked about it.
    const AICardCandidateView *offered = candidateFor(request, red->getEffectiveId());
    if (!offered || offered->candidateId <= 0) {
        qCritical() << "The physical candidate carried no ticket";
        return false;
    }
    AIResult strayCandidate;
    strayCandidate.kind = AIResult::UseCard;
    strayCandidate.handled = true;
    strayCandidate.decisionId = request.decisionId;
    strayCandidate.stateRevision = request.stateRevision;
    strayCandidate.action.useCardId = red->getEffectiveId();
    strayCandidate.action.candidateId = 9999;
    CardUseStruct strayCandidateUse;
    if (RoomTestAccess::applyResult(*room, viewer, request, strayCandidate,
                                    strayCandidateUse)) {
        qCritical() << "A candidate ticket from no request was honoured";
        return false;
    }
    return true;
}

static bool coverageReportListsWhatIsWired()
{
    ScopedConfigValue scripts(QStringLiteral("AiIsolatedScripts"),
                              QStringList({QStringLiteral("ask-for-use-card.lua"),
                                           QStringLiteral("ask-for-choice.lua"),
                                           QStringLiteral("decision-core.lua"),
                                           QStringLiteral("strategy-hooks.lua"),
                                           QStringLiteral("standard-ai.lua")}));
    Room room(nullptr, QStringLiteral("02_1v1"));
    LuaRuntime::Binding luaBinding(room.roomRuntime()->ai().lua());
    lua_State *L = room.roomRuntime()->ai().lua().state();
    // The report is built from what the registries actually hold, never assumed.
    if (luaL_dostring(L,
        "ai_skill_choice['covered-choice'] = function() return 'a' end; "
        "ai_cardshow['covered-show'] = function() return 7 end; "
        "coverage_probe = table.concat({"
        "tostring(ai_coverage.covers('use_card', '@@lianying')), "
        "tostring(ai_coverage.covers('use_card', 'never-registered')), "
        "tostring(ai_coverage.covers('choice', 'covered-choice')), "
        "tostring(ai_coverage.covers('respond_card', 'askForCardShow:covered-show')), "
        "tostring(ai_coverage.covers('activate')), "
        "tostring(ai_coverage.covers('guanxing')), "
        "tostring(#ai_coverage.summary() > 0)}, '|')") != 0) {
        lua_pop(L, 1);
        return false;
    }
    lua_getglobal(L, "coverage_probe");
    const QString probe = QString::fromUtf8(lua_tostring(L, -1));
    lua_pop(L, 1);
    if (probe != QStringLiteral("true|false|true|true|true|false|true")) {
        qCritical() << "Coverage report did not match what is wired" << probe;
        return false;
    }
    return true;
}

static bool officialLianyingHandlerMatchesIsolated()
{
    ScopedAiEnabled enabled(true);
    // This fixture intentionally compares the actual LuaAI. Admission must be
    // explicit before Room initializes its game VM, not just before cloneAI.
    ScopedConfigValue legacy(QStringLiteral("AiLegacyDirectCallbacks"),
                             QStringList{QStringLiteral("askForUseCard:@@lianying")});
    ScopedConfigValue isolatedRoutes(QStringLiteral("AiIsolatedCallbacks"), QStringList());
    Room room(nullptr, QStringLiteral("02_1v1"));
    ServerPlayer *player = RoomTestAccess::addRobotPlayer(room);
    player->setObjectName(QStringLiteral("lianying-owner"));
    player->setRole(QStringLiteral("lord"));
    player->setPhase(Player::Play);
    player->setMark(QStringLiteral("lianying"), 1);
    CardsMoveOneTimeStruct move = {};
    player->setTag(QStringLiteral("LianyingMoveData"), QVariant::fromValue(move));

    AIRequest request = RoomTestAccess::makeRequest(room, player, AIRequest::UseCard);
    request.pattern = QStringLiteral("@@lianying");
    AIResult official;
    {
        LuaRuntime::Binding luaBinding(room.roomRuntime()->lua());
        EngineRuntimeContextScope contextScope(*Sanguosha, &room);
        AI *officialAI = RoomTestAccess::cloneAI(room, player);
        LuaAI *officialLuaAI = qobject_cast<LuaAI *>(officialAI);
        if (!officialLuaAI)
            return false;
        official = officialLuaAI->decide(request);
    }
    const AIResult isolated = room.roomRuntime()->ai().decideIsolated(request);
    if (!official.handled || official.kind != AIResult::UseCard
        || official.action.legacyCardString
            != QStringLiteral("@LianyingCard=.->lianying-owner")
        || !isolated.handled || isolated.kind != AIResult::UseCard
        || isolated.action.legacyCardString != official.action.legacyCardString)
        return false;

    request.worldView.self.publicMarks.insert(QStringLiteral("lianying"), 2);
    const AIResult unsupported = room.roomRuntime()->ai().decideIsolated(request);
    return !unsupported.handled && unsupported.errorCode.isEmpty();
}

static bool modePolicyAndRoleVisibilityAreRoomLocal()
{
    Room first(nullptr, QStringLiteral("02_1v1"));
    Room second(nullptr, QStringLiteral("02_1v1"));
    EngineRuntimeContextScope contextScope(*Sanguosha, &first);
    ServerPlayer *a = RoomTestAccess::addRobotPlayer(first);
    ServerPlayer *b = RoomTestAccess::addRobotPlayer(first);
    ServerPlayer *outsider = RoomTestAccess::addRobotPlayer(second);
    ServerPlayer *outsideTarget = RoomTestAccess::addRobotPlayer(second);
    a->setObjectName(QStringLiteral("policy-a"));
    b->setObjectName(QStringLiteral("policy-b"));
    outsider->setObjectName(QStringLiteral("policy-a"));
    outsideTarget->setObjectName(QStringLiteral("policy-b"));
    a->setRole(QStringLiteral("fixture_identity_a"));
    b->setRole(QStringLiteral("fixture_identity_b"));
    outsider->setRole(QStringLiteral("fixture_identity_a"));
    outsideTarget->setRole(QStringLiteral("fixture_identity_b"));
    second.revealRole(outsideTarget);
    if (!first.canSeeRole(a, a) || first.isRoleRevealed(a)
        || first.canSeeRole(a, b) || first.canSeeRole(outsider, a)) return false;

    quint64 revision = first.roomRuntime()->stateRevision();
    first.revealRoleTo(a, b);
    if (!first.canSeeRole(a, b) || first.isRoleRevealed(b)
        || first.roomRuntime()->stateRevision() == revision) return false;
    revision = first.roomRuntime()->stateRevision();
    first.syncRole(a, b);
    first.revealRoleTo(a, b);
    if (first.roomRuntime()->stateRevision() != revision) return false;

    // Identity replacement must invalidate knowledge, even if a later role has the same name.
    b->setRole(QStringLiteral("fixture_identity_c"));
    b->setRole(QStringLiteral("fixture_identity_b"));
    if (first.canSeeRole(a, b)) return false;
    AIWorldView world = first.buildAIWorldView(a);
    if (!world.self.roleVisible || world.self.roleRevealed || world.players.size() != 1
        || world.players.first().roleVisible || !world.players.first().role.isEmpty()) return false;

    first.revealRole(b);
    if (!first.isRoleRevealed(b) || !first.canSeeRole(a, b)) return false;
    revision = first.roomRuntime()->stateRevision();
    first.revealRole(b);
    first.syncRole(a, b);
    if (first.roomRuntime()->stateRevision() != revision) return false;
    {
        LuaRuntime::Binding binding(first.roomRuntime()->lua());
        lua_State *L = first.getLuaState();
        const int top = lua_gettop(L);
        if (luaL_dostring(L, "sgs.registerModeAI('02_1v1', {"
            "relation=function(ctx, from, to) return 'friend' end})") != 0) return false;
        lua_settop(L, top);
    }
    world = first.buildAIWorldView(a);
    if (world.revision != revision || first.roomRuntime()->stateRevision() != revision
        || world.modeId != first.getMode()
        || world.modePolicy.value("relations").toObject().value(a->objectName()).toObject()
            .value(b->objectName()).toString() != QStringLiteral("friend")) return false;
    // The same mode ID in a second Room has no registration or mind from the first.
    const AIWorldView isolated = second.buildAIWorldView(outsider);
    if (!isolated.modePolicy.value("managed").toBool()
        || isolated.modePolicy.value("relations").toObject().value(outsider->objectName()).toObject()
            .value(outsideTarget->objectName()).toString() != QStringLiteral("unknown")) return false;
    return true;
}

static bool pureModePolicyContract()
{
    LuaRuntime runtime(LuaRuntime::Auxiliary);
    QString error;
    LuaRuntime::Binding luaBinding(runtime);
    if (!runtime.initialize(&error)
        || !runtime.loadScript(QStringLiteral("tests/lua/mode-ai-contract.lua"), &error)) {
        qCritical().noquote() << error;
        return false;
    }
    return true;
}

static bool pureValueBoundaryContract()
{
    // The shared type boundary is plain Lua, so it runs without a Room or the Engine.
    LuaRuntime runtime(LuaRuntime::Auxiliary);
    QString error;
    LuaRuntime::Binding luaBinding(runtime);
    if (!runtime.initialize(&error)
        || !runtime.loadScript(QStringLiteral("tests/lua/value-boundary-contract.lua"), &error)) {
        qCritical().noquote() << error;
        return false;
    }
    return true;
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
    room->rebuildAlivePlayers();

    if (Sanguosha->getCardCount() < 2)
        return false;
    viewer->addCard(0, Player::PlaceHand);
    other->addCard(1, Player::PlaceHand);
    room->setPlayerMark(other, QStringLiteral("public_mark"), 2);
    room->setPlayerMark(other, QStringLiteral("private_mark"), 3,
                        QList<ServerPlayer *>() << other);
    other->setMark(QStringLiteral("server_internal_mark"), 4);

    if (!Sanguosha->getSkill(QStringLiteral("lianying")))
        return false;
    const int viewerSkillId = viewer->createSkillInstance(
        QStringLiteral("lianying"), SourceAcquired, true);
    const int otherSkillId = other->createSkillInstance(
        QStringLiteral("lianying"), SourceAcquired, true);
    if (viewerSkillId <= 0 || otherSkillId <= 0)
        return false;

    QVariantMap nestedState;
    nestedState.insert(QStringLiteral("answer"), 42);
    viewer->Player::setSkillInstanceStateValue(
        QStringLiteral("lianying"), viewerSkillId, QStringLiteral("count"), 2);
    viewer->Player::setSkillInstanceStateValue(
        QStringLiteral("lianying"), viewerSkillId, QStringLiteral("nested"), nestedState);
    viewer->Player::setSkillInstanceStateValue(
        QStringLiteral("lianying"), viewerSkillId, QStringLiteral("unsafe"),
        QVariant::fromValue(static_cast<QObject *>(viewer)));
    if (!viewer->setSkillInstanceCorrectStateValue(
            QStringLiteral("lianying"), viewerSkillId, QStringLiteral("bonus"), 1))
        return false;
    other->Player::setSkillInstanceStateValue(
        QStringLiteral("lianying"), otherSkillId, QStringLiteral("secret"), 9);
    if (!other->setSkillInstanceCorrectStateValue(
            QStringLiteral("lianying"), otherSkillId, QStringLiteral("public"), 3))
        return false;

    const quint64 revision = room->roomRuntime()->stateRevision();
    const AIRequest request = RoomTestAccess::makeRequest(*room, viewer, AIRequest::UseCard);
    const Card *physicalHandCard = Sanguosha->getCard(0);
    if (room->roomRuntime()->stateRevision() != revision
        || request.stateRevision != revision || request.worldView.revision != revision
        || request.worldView.self.objectName != viewer->objectName()
        || !request.worldView.self.alive || request.worldView.self.dead
        || request.worldView.self.kongcheng || !request.worldView.self.wounded
        || request.worldView.currentPlayer != viewer->objectName()
        || request.worldView.currentPhase != int(Player::Play)
        || request.worldView.playerOrder != QStringList({viewer->objectName(), other->objectName()})
        || request.worldView.alivePlayerOrder != request.worldView.playerOrder
        || !physicalHandCard || request.worldView.handCards.size() != 1
        || request.worldView.players.size() != 1) {
        qCritical() << "The built request did not describe the board"
                    << request.stateRevision << revision
                    << request.worldView.revision
                    << request.worldView.self.objectName
                    << request.worldView.self.alive << request.worldView.self.dead
                    << request.worldView.self.kongcheng
                    << request.worldView.self.wounded
                    << request.worldView.currentPlayer
                    << request.worldView.currentPhase
                    << request.worldView.playerOrder
                    << request.worldView.alivePlayerOrder
                    << request.worldView.handCards.size()
                    << request.worldView.players.size()
                    << room->getAlivePlayers().size()
                    << viewer->getHandcardNum();
        return false;
    }

    const AICardView &handCardView = request.worldView.handCards.first();
    if (handCardView.cardId != physicalHandCard->getId()
        || handCardView.effectiveId != physicalHandCard->getEffectiveId()
        || handCardView.objectName != physicalHandCard->objectName()
        || handCardView.className != physicalHandCard->getClassName()
        || handCardView.suit != int(physicalHandCard->getSuit())
        || handCardView.number != physicalHandCard->getNumber()
        || handCardView.skillName != physicalHandCard->getSkillName(false)
        || handCardView.red != physicalHandCard->isRed()
        || handCardView.black != physicalHandCard->isBlack()
        || !handCardView.kindOfNames.contains(physicalHandCard->getClassName())
        || !handCardView.kindOfNames.contains(QStringLiteral("Card"))) {
        qCritical() << "The hand card projection did not match the physical card";
        return false;
    }

    if (request.worldView.self.skills.size() != 1) {
        qCritical() << "The viewer skill projection had the wrong size"
                    << request.worldView.self.skills.size();
        return false;
    }
    const AISkillView &selfSkill = request.worldView.self.skills.first();
    if (selfSkill.skillName != QStringLiteral("lianying")
        || selfSkill.instanceId != viewerSkillId || !selfSkill.hasPrivateState
        || selfSkill.state.value(QStringLiteral("count")).toInt() != 2
        || selfSkill.state.value(QStringLiteral("nested")).toObject()
               .value(QStringLiteral("answer")).toInt() != 42
        || selfSkill.state.contains(QStringLiteral("unsafe"))
        || selfSkill.correctState.value(QStringLiteral("bonus")).toInt() != 1) {
        qCritical() << "The viewer skill state was not projected as declared";
        return false;
    }

    const AIPlayerView &otherView = request.worldView.players.first();
    if (otherView.objectName != other->objectName() || otherView.handcardCount != 1
        || otherView.publicMarks.value(QStringLiteral("public_mark")) != 2
        || otherView.publicMarks.contains(QStringLiteral("private_mark"))
        || otherView.publicMarks.contains(QStringLiteral("server_internal_mark"))
        || otherView.skills.size() != 1) {
        qCritical() << "The other player projection leaked or lost a field"
                    << otherView.handcardCount << otherView.skills.size();
        return false;
    }
    const AISkillView &otherSkill = otherView.skills.first();
    if (otherSkill.skillName != QStringLiteral("lianying")
        || otherSkill.instanceId != otherSkillId || otherSkill.hasPrivateState
        || !otherSkill.state.isEmpty()
        || otherSkill.correctState.value(QStringLiteral("public")).toInt() != 3) {
        qCritical() << "Another player's private skill state crossed the boundary";
        return false;
    }

    AIResult result;
    result.kind = AIResult::Pass;
    result.handled = true;
    result.decisionId = request.decisionId;
    result.stateRevision = request.stateRevision;
    CardUseStruct use;
    if (!RoomTestAccess::applyResult(*room, viewer, request, result, use)) {
        qCritical() << "A current-revision pass was rejected";
        return false;
    }

    const quint64 noOpRevision = room->roomRuntime()->stateRevision();
    room->setCurrent(viewer);
    room->setPlayerMark(other, QStringLiteral("public_mark"), 2);
    if (room->roomRuntime()->stateRevision() != noOpRevision) {
        qCritical() << "A no-op mark write moved the state revision" << noOpRevision
                    << room->roomRuntime()->stateRevision();
        return false;
    }

    // 2026-09-20 (fc1c09e): a flag is gameplay state, not scratch state. The contract
    // used to be that a flag set and cleared left the revision untouched. It cannot be:
    // hasFlag is read by roughly two thousand rule sites, InfinityAttackRange changes
    // the attackRange this very snapshot publishes, and buildWorldView caches its
    // distance table under stateRevision - so a flag a distance skill reads could hand
    // out a stale table. Nothing tells a scratch flag from a rule flag by name, so
    // every flag write moves the revision and invalidates the snapshot.
    viewer->setFlags(QStringLiteral("AI_TestScratch"));
    const quint64 flagSetRevision = room->roomRuntime()->stateRevision();
    if (flagSetRevision <= noOpRevision) {
        qCritical() << "Setting a flag did not move the state revision" << noOpRevision
                    << flagSetRevision;
        return false;
    }
    // A write that changes nothing is still not a change: the flag is already set.
    viewer->setFlags(QStringLiteral("AI_TestScratch"));
    if (room->roomRuntime()->stateRevision() != flagSetRevision) {
        qCritical() << "A no-op flag write moved the state revision" << flagSetRevision
                    << room->roomRuntime()->stateRevision();
        return false;
    }
    viewer->setFlags(QStringLiteral("-AI_TestScratch"));
    const quint64 flagClearedRevision = room->roomRuntime()->stateRevision();
    if (flagClearedRevision <= flagSetRevision) {
        qCritical() << "Clearing a flag did not move the state revision" << flagSetRevision
                    << flagClearedRevision;
        return false;
    }
    viewer->setFlags(QStringLiteral("-AI_TestScratch"));
    if (room->roomRuntime()->stateRevision() != flagClearedRevision) {
        qCritical() << "A no-op flag clear moved the state revision" << flagClearedRevision
                    << room->roomRuntime()->stateRevision();
        return false;
    }

    const quint64 hpRevision = room->roomRuntime()->stateRevision();
    viewer->setHp(viewer->getHp());
    if (room->roomRuntime()->stateRevision() != hpRevision) {
        qCritical() << "A no-op hp write moved the state revision" << hpRevision
                    << room->roomRuntime()->stateRevision();
        return false;
    }
    viewer->setHp(viewer->getHp() - 1);
    if (room->roomRuntime()->stateRevision() <= hpRevision) {
        qCritical() << "Losing hp did not move the state revision" << hpRevision
                    << room->roomRuntime()->stateRevision();
        return false;
    }
    if (RoomTestAccess::applyResult(*room, viewer, request, result, use)) {
        qCritical() << "A stale-revision answer was accepted";
        return false;
    }

    const quint64 limitationRevision = room->roomRuntime()->stateRevision();
    viewer->setCardLimitation(QStringLiteral("use"), QStringLiteral("."),
                              QStringLiteral("ai-world-view-test"), false);
    if (room->roomRuntime()->stateRevision() <= limitationRevision) {
        qCritical() << "A card limitation did not move the state revision";
        return false;
    }

    const quint64 stateRevision = room->roomRuntime()->stateRevision();
    viewer->Player::setSkillInstanceStateValue(
        QStringLiteral("lianying"), viewerSkillId, QStringLiteral("count"), 3);
    if (room->roomRuntime()->stateRevision() <= stateRevision) {
        qCritical() << "A skill instance state write did not move the state revision";
        return false;
    }

    const quint64 skillRevision = room->roomRuntime()->stateRevision();
    viewer->createSkillInstance(QStringLiteral("ai_world_view_test_skill"), SourceAcquired, true);
    if (room->roomRuntime()->stateRevision() <= skillRevision) {
        qCritical() << "Acquiring a skill did not move the state revision";
        return false;
    }

    const AIRequest fresh = RoomTestAccess::makeRequest(*room, viewer, AIRequest::UseCard);
    result.decisionId = fresh.decisionId;
    result.stateRevision = fresh.stateRevision;
    if (fresh.worldView.revision != fresh.stateRevision
        || fresh.worldView.self.skills.size() != 1
        || fresh.worldView.self.skills.first().state
               .value(QStringLiteral("count")).toInt() != 3) {
        qCritical() << "A fresh request did not carry the current revision or state"
                    << fresh.worldView.self.skills.size();
        return false;
    }
    if (!RoomTestAccess::applyResult(*room, viewer, fresh, result, use)) {
        qCritical() << "A current-revision answer was rejected";
        return false;
    }

    // --ai off is a different question, and makeRequest answers it deliberately: it
    // stops after the header. Nothing reads what it skips - every decideIsolated call
    // site pins the route to AiRouteLegacyDirect while AI is off - and building it
    // anyway would cost one gameplay-Lua mode policy call plus a quadratic target
    // probe for every decision a trusted player makes on a human-only server. That is
    // the behaviour this block pins, so the saving cannot be undone by accident.
    {
        ScopedAiEnabled aiOff(false);
        const AIRequest off = RoomTestAccess::makeRequest(*room, viewer, AIRequest::UseCard);
        if (off.viewerObjectName != viewer->objectName()
            || off.stateRevision != room->roomRuntime()->stateRevision()) {
            qCritical() << "An --ai off request lost the header it still has to carry"
                        << off.viewerObjectName << off.stateRevision;
            return false;
        }
        if (off.worldView.revision != 0 || !off.worldView.self.objectName.isEmpty()
            || !off.worldView.playerOrder.isEmpty() || !off.worldView.handCards.isEmpty()
            || !off.worldView.players.isEmpty() || !off.cardCandidates.isEmpty()
            || !off.skillActions.isEmpty()) {
            qCritical() << "An --ai off request still built the snapshot nobody reads"
                        << off.worldView.revision << off.worldView.playerOrder.size()
                        << off.worldView.handCards.size() << off.worldView.players.size()
                        << off.cardCandidates.size() << off.skillActions.size();
            return false;
        }
    }
    return true;
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
    QCoreApplication::sendPostedEvents(pointer.data(), QEvent::DeferredDelete);
    return pointer.isNull();
}

static bool loadIsolatedTestHandler(Room &room)
{
    LuaRuntime::Binding luaBinding(room.roomRuntime()->ai().lua());
    lua_State *L = room.roomRuntime()->ai().lua().state();
    return luaL_dostring(L,
        "ai_register_handler('use_card', function(self, request) "
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

static bool aiIsolatedParsesPassAndUseCard(Room &room)
{
    if (!loadIsolatedTestHandler(room))
        return false;

    AIRequest request;
    request.kind = AIRequest::UseCard;
    request.viewerObjectName = QStringLiteral("ai_player");
    request.worldView.self.objectName = request.viewerObjectName;
    request.pattern = QStringLiteral("pass");
    const AIResult pass = room.roomRuntime()->ai().decideIsolated(request);
    if (!pass.handled || pass.kind != AIResult::Pass || !pass.errorCode.isEmpty())
        return false;

    request.stateRevision = 42;
    request.worldView.revision = 42;
    AICardView handCard;
    handCard.cardId = 7;
    request.worldView.handCards << handCard;
    AIPlayerView otherPlayer;
    otherPlayer.objectName = QStringLiteral("other");
    otherPlayer.publicMarks.insert(QStringLiteral("ready"), 2);
    request.worldView.players << otherPlayer;
    request.pattern = QStringLiteral("world");
    const AIResult world = room.roomRuntime()->ai().decideIsolated(request);
    if (!world.handled || world.kind != AIResult::Pass || !world.errorCode.isEmpty())
        return false;

    request.pattern = QStringLiteral("use");
    const AIResult useCard = room.roomRuntime()->ai().decideIsolated(request);
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
    const AIResult structured = room.roomRuntime()->ai().decideIsolated(request);
    if (!structured.handled || structured.kind != AIResult::UseCard
        || !structured.errorCode.isEmpty() || !structured.action.hasSkillActionContext
        || structured.action.skillActionContext.activationRef
            != request.skillActionContext.activationRef)
        return false;

    request.pattern = QStringLiteral("huge");
    const AIResult invalidNumber = room.roomRuntime()->ai().decideIsolated(request);
    if (invalidNumber.errorCode != QStringLiteral("AI_INVALID_RESULT"))
        return false;
    request.pattern = QStringLiteral("too_many");
    const AIResult oversized = room.roomRuntime()->ai().decideIsolated(request);
    return oversized.errorCode == QStringLiteral("AI_INVALID_RESULT");
}

static bool aiInstructionLimitRebuildsRuntime(Room &room)
{
    const quint64 generation = room.roomRuntime()->ai().lua().generation();
    {
        LuaRuntime::Binding luaBinding(room.roomRuntime()->ai().lua());
        lua_State *L = room.roomRuntime()->ai().lua().state();
        if (luaL_dostring(L,
            "ai_register_handler('activate', function(self, request) while true do end end)") != 0)
            return false;
    }
    AIRequest request;
    request.kind = AIRequest::Activate;
    // A complete question: this fixture carries no view-as skill, so "no
    // conversion" is its answer rather than something it failed to work out.
    request.conversionsEnumerated = true;
    request.viewerObjectName = QStringLiteral("ai_player");
    request.worldView.self.objectName = request.viewerObjectName;
    const AIResult result = room.roomRuntime()->ai().decideIsolated(request);
    return result.errorCode == QStringLiteral("AI_INSTRUCTION_LIMIT")
        && room.roomRuntime()->ai().lua().rawState()
        && room.roomRuntime()->ai().lua().generation() > generation;
}

static bool collateralTargetValidationContract()
{
    std::unique_ptr<Room> room(new Room(nullptr, QStringLiteral("02_1v1")));
    EngineRuntimeContextScope contextScope(*Sanguosha, room.get());
    room->roomRuntime()->state().reset();
    ServerPlayer *viewer = RoomTestAccess::addRobotPlayer(*room);
    viewer->setObjectName(QStringLiteral("collateral-viewer"));
    viewer->setSeat(1);
    viewer->setMaxHp(4);
    viewer->setHp(4);
    viewer->setPhase(Player::Play);
    ServerPlayer *other = RoomTestAccess::addRobotPlayer(*room);
    other->setObjectName(QStringLiteral("collateral-other"));
    other->setSeat(2);
    other->setMaxHp(4);
    other->setHp(4);
    room->setCurrent(viewer);
    room->rebuildAlivePlayers();

    const auto findCard = [](const QString &name) -> const Card * {
        for (int id = 0; id < Sanguosha->getCardCount(); ++id) {
            const Card *card = Sanguosha->getCard(id);
            if (card && card->objectName() == name)
                return card;
        }
        return nullptr;
    };
    // Collateral returns false from targetFilter even for a legal pair;
    // maxVotes carries its selection contract, including the armed first target.
    const Card *collateral = findCard(QStringLiteral("collateral"));
    const Card *weapon = findCard(QStringLiteral("crossbow"));
    if (!collateral || !weapon)
        return false;
    other->Player::addCard(weapon->getId(), Player::PlaceEquip);
    CardUseStruct collateralUse;
    collateralUse.card = collateral;
    collateralUse.from = viewer;
    collateralUse.to << other << viewer;
    if (!RoomTestAccess::areCardTargetsLegal(*room, collateralUse)) {
        qCritical() << "A legal Collateral pair was rejected";
        return false;
    }
    collateralUse.to.removeLast();
    if (RoomTestAccess::areCardTargetsLegal(*room, collateralUse)) {
        qCritical() << "Collateral accepted a missing Slash victim";
        return false;
    }
    collateralUse.to << viewer << other << viewer;
    if (RoomTestAccess::areCardTargetsLegal(*room, collateralUse)) {
        qCritical() << "Collateral exceeded its target capacity";
        return false;
    }
    other->Player::removeCard(weapon->getId(), Player::PlaceEquip);
    collateralUse.to = QList<ServerPlayer *>({other, viewer});
    if (RoomTestAccess::areCardTargetsLegal(*room, collateralUse)) {
        qCritical() << "Collateral accepted an unarmed first target";
        return false;
    }
    return true;
}

// Explicit independent-cost fixture, local to this suite and owned by its Room.
class PlanningCostSkill : public ViewAsSkillV2
{
public:
    explicit PlanningCostSkill(const QString &name, const QString &output, int count = 2)
        : ViewAsSkillV2(name, count), output(output) {}
    bool canActivate(const ActiveSkillRequest &r) const override
    { return r.initiator && r.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool hasIndependentAIConversion() const override { return independent; }
    bool canSelectCard(const ActiveSkillRequest &r, const Card *c) const override
    {
        return c && r.initiator && r.initiator->handCards().contains(c->getEffectiveId())
            && c->getEffectiveId() != rejectedId && !r.selectedCardIds.contains(c->getEffectiveId())
            && r.selectedCardIds.size() < getN();
    }
    const Card *createCard(const ActiveSkillRequest &r) const override
    {
        if (!cardSelectionFeasible(r)) return nullptr;
        Card *c = Sanguosha->cloneCard(output, Card::NoSuit, 0);
        if (c) { c->setSkillName(objectName()); c->addSubcards(r.selectedCardIds); }
        return c;
    }
    bool independent = true;
    int rejectedId = -1;
    QString output;
};

// Response-only V2 conversion fixture. It deliberately has a real one-card
// cost so responseCard must consume the request ticket and revalidate it.
class ResponseConversionSkill : public ViewAsSkillV2
{
public:
    ResponseConversionSkill() : ViewAsSkillV2(QStringLiteral("response_v2_cost"), 1) {}
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
            && request.pattern == QStringLiteral("jink");
    }
    bool hasIndependentAIConversion() const override { return true; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        return request.initiator && candidate && candidate->isRed()
            && request.selectedCardIds.isEmpty()
            && request.selectedCardIds.size() < getN()
            && request.initiator->handCards().contains(candidate->getEffectiveId());
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return nullptr;
        Card *card = Sanguosha->cloneCard(QStringLiteral("jink"), Card::NoSuit, 0);
        if (card) { card->setSkillName(objectName()); card->addSubcards(request.selectedCardIds); }
        return card;
    }
    TargetMode targetMode() const override { return NoTarget; }
};

static int isolatedPlanningContracts()
{
    ScopedConfigValue scripts(QStringLiteral("AiIsolatedScripts"), QStringList{QStringLiteral("decision-core.lua"), QStringLiteral("strategy-hooks.lua")});
    ScopedConfigValue instructions(QStringLiteral("AiLuaInstructionBudget"), 100000);
    ScopedAiEnabled enabled(true);
    Room room(nullptr, QStringLiteral("02_1v1"));
    EngineRuntimeContextScope scope(*Sanguosha, &room);
    LuaRuntime::Binding gameplayBinding(room.roomRuntime()->lua());
    room.roomRuntime()->state().reset();
    ServerPlayer *viewer = RoomTestAccess::addRobotPlayer(room);
    viewer->setObjectName(QStringLiteral("plan-viewer")); viewer->setSeat(1);
    viewer->setMaxHp(4); viewer->setHp(4); viewer->setPhase(Player::Play);
    ServerPlayer *armed = RoomTestAccess::addRobotPlayer(room);
    armed->setObjectName(QStringLiteral("plan-armed")); armed->setSeat(2);
    armed->setMaxHp(4); armed->setHp(4);
    ServerPlayer *victim = RoomTestAccess::addRobotPlayer(room);
    victim->setObjectName(QStringLiteral("plan-victim")); victim->setSeat(3);
    victim->setMaxHp(4); victim->setHp(4);
    room.setCurrent(viewer); room.rebuildAlivePlayers();
    const Card *collateral = findEngineCard(QStringLiteral("collateral"));
    const Card *weapon = findEngineCard(QStringLiteral("crossbow"));
    if (!collateral || !weapon) return 37;
    viewer->addCard(collateral->getId(), Player::PlaceHand);
    armed->Player::addCard(weapon->getId(), Player::PlaceEquip);
    const auto make = [&]() { return RoomTestAccess::makeRequest(room, viewer, AIRequest::Activate,
        CardUseStruct::CARD_USE_REASON_PLAY, QString(), Card::MethodUse); };
    const quint64 before = room.roomRuntime()->stateRevision();
    const AIRequest targets = make();
    const auto *projection = candidateFor(targets, collateral->getEffectiveId());
    const QList<QStringList> expected{{armed->objectName(), viewer->objectName()},
                                      {armed->objectName(), victim->objectName()}};
    if (!projection || !projection->completeCoverage || projection->targetCombinations != expected) {
        qCritical() << "PR07 ordered target projection failed";
        return 37;
    }
    for (const auto &row : projection->targetCombinations) {
        CardUseStruct use; use.from = viewer; use.card = collateral;
        for (const auto &name : row) use.to << room.findPlayerByObjectName(name);
        if (!RoomTestAccess::areCardTargetsLegal(room, use)) return 37;
        std::reverse(use.to.begin(), use.to.end());
        if (RoomTestAccess::areCardTargetsLegal(room, use)) return 37;
    }
    if (room.roomRuntime()->stateRevision() != before) return 37;

    PlanningCostSkill *skill = new PlanningCostSkill(QStringLiteral("pr07-cost"), QStringLiteral("collateral"));
    room.roomRuntime()->addSkills(QList<const Skill *>{skill});
    const int instance = viewer->createSkillInstance(skill->objectName(), SourceAcquired, true);
    if (instance <= 0) return 39;
    QList<int> costs{collateral->getId()};
    for (int id = 0; id < Sanguosha->getCardCount() && costs.size() < 3; ++id) {
        if (id == collateral->getId() || id == weapon->getId()) continue;
        viewer->addCard(id, Player::PlaceHand); costs << id;
    }
    const AIRequest request = make();
    if (!request.conversionsEnumerated || request.cardConversions.size() != 1) return 39;
    const auto &conversion = request.cardConversions.first();
    if (conversion.costCount != 2 || conversion.eligibleSubcardIds.size() != 3
        || conversion.activationRef.key.instanceID != instance
        || conversion.targetCombinations != expected || !conversion.completeCoverage) {
        qCritical() << "PR07 independent cost contract or shared targets missing"; return 39;
    }
    AIResult result; result.handled = true; result.kind = AIResult::UseCard;
    result.decisionId = request.decisionId; result.stateRevision = request.stateRevision;
    result.action.hasCardSpec = true;
    result.action.cardSpec.conversionId = conversion.conversionId;
    result.action.cardSpec.name = conversion.name;
    result.action.cardSpec.suit = conversion.suit; result.action.cardSpec.number = conversion.number;
    result.action.cardSpec.skillName = skill->objectName();
    result.action.cardSpec.subcardIds = {costs[1], costs[2]}; // not the representative pair
    result.action.selectedTargetNames = expected.first();
    CardUseStruct valid;
    if (!RoomTestAccess::applyResult(room, viewer, request, result, valid)
        || !RoomTestAccess::areCardTargetsLegal(room, valid)
        || valid.card->getSubcards() != result.action.cardSpec.subcardIds) {
        qCritical() << "PR07 parameterized alternate selection rejected"; return 39;
    }
    for (const QList<int> &bad : QList<QList<int>>{{costs[1], costs[1]}, {costs[1]}, {costs[1], weapon->getId()}}) {
        AIResult forged = result; forged.action.cardSpec.subcardIds = bad;
        CardUseStruct rejected;
        if (RoomTestAccess::applyResult(room, viewer, request, forged, rejected)) {
            qCritical() << "PR07 forged parameterized cost accepted"; return 39;
        }
    }
    skill->rejectedId = costs[2];
    CardUseStruct changed;
    if (RoomTestAccess::applyResult(room, viewer, request, result, changed)) {
        qCritical() << "PR07 prefix cost revalidation bypassed"; return 39;
    }
    skill->rejectedId = -1;
    skill->independent = false;
    if (make().conversionsEnumerated) { qCritical() << "PR07 undeclared multi-cost inferred"; return 39; }
    skill->independent = true;

    LuaRuntime &lua = room.roomRuntime()->ai().lua();
    {
        LuaRuntime::Binding binding(lua);
        QString error;
        if (!lua.loadScript(QStringLiteral("tests/lua/isolated-planning-contract.lua"), &error)) {
            qCritical().noquote() << error; return 38;
        }
        // Exercise the real host instruction hook during a nested plan, not a Lua counter.
        if (luaL_dostring(lua.state(), "ai_card_use.PR07Spin=function() while true do end end; "
            "ai_register_handler('activate',function(self) self:aiUseCard(CardView.new({id=99,effective_id=99,name='spin',class_name='PR07Spin'})) end)") != 0) return 38;
    }
    const AIResult exhausted = room.roomRuntime()->ai().decideIsolated(request);
    if (exhausted.handled || exhausted.errorCode != QStringLiteral("AI_INSTRUCTION_LIMIT")) {
        qCritical() << "PR07 instruction limit became a decision" << exhausted.errorCode; return 38;
    }
    if (room.roomRuntime()->stateRevision() != request.stateRevision) return 38;
    {
        LuaRuntime &rebuilt = room.roomRuntime()->ai().lua();
        LuaRuntime::Binding binding(rebuilt);
        if (luaL_dostring(rebuilt.state(), "assert(ai_card_use.PR07Spin == nil)") != 0) return 38;
    }
    // Lower the production budget on the same three-player fixture: reaching it
    // must clear the partial result, without building an unrelated large snapshot.
    ScopedConfigValue projectionBudget(QStringLiteral("AiTargetProjectionBudget"), 8);
    const AIRequest boundedRequest = make();
    const auto *bounded = candidateFor(boundedRequest, collateral->getEffectiveId());
    if (!bounded || bounded->completeCoverage || !bounded->targetCombinations.isEmpty()) {
        qCritical() << "PR07 target projection budget did not fail closed"; return 37;
    }
    qInfo() << "PR07 contracts passed: ordered targets, branches, costs, instruction limit";
    return 0;
}

int runIsolatedPlanningTests()
{
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &message) {
        std::fprintf(stderr, "%s\n", message.toUtf8().constData()); std::fflush(stderr);
    });
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) return 1;
    const int result = isolatedPlanningContracts();
    EngineBootstrap::shutdown();
    return result;
}

class LegacyResponseCoverageSkill : public ZeroCardViewAsSkill
{
public:
    LegacyResponseCoverageSkill()
        : ZeroCardViewAsSkill(QStringLiteral("common-legacy-response-coverage")) {}
    bool isEnabledAtPlay(const Player *) const override { return false; }
    bool isEnabledAtResponse(const Player *, const QString &pattern) const override
    { return enabled && pattern == QStringLiteral("jink"); }
    const Card *viewAs() const override
    { return Sanguosha->cloneCard(QStringLiteral("jink"), Card::NoSuit, 0); }
    bool enabled = true;
};

class DecisionContextFilterSkill : public FilterSkill
{
public:
    DecisionContextFilterSkill() : FilterSkill(QStringLiteral("common-context-filter")) {}
    bool viewFilter(const Card *) const override { ++calls; return true; }
    const Card *viewAs(const Card *card) const override { ++calls; return card; }
    mutable int calls = 0;
};

static bool physicalResponseCoveragePreservesConversions(Room &room)
{
    ScopedAiEnabled enabled(true);
    EngineRuntimeContextScope scope(*Sanguosha, &room);
    LuaRuntime::Binding binding(room.roomRuntime()->lua());
    room.roomRuntime()->state().reset();
    ServerPlayer *viewer = RoomTestAccess::addRobotPlayer(room);
    viewer->setObjectName(QStringLiteral("common-response-viewer"));
    viewer->setSeat(1);
    viewer->setMaxHp(4);
    viewer->setHp(4);
    room.setCurrent(viewer);
    room.rebuildAlivePlayers();
    const auto response = [&]() {
        return RoomTestAccess::makeResponseRequest(room, viewer, QStringLiteral("askForCard"),
                                                   QStringLiteral("jink"), Card::MethodResponse);
    };
    const AIRequest empty = response();
    if (!empty.conversionsEnumerated || !empty.choiceOptions.candidatesComplete
        || !empty.choiceOptions.cardIds.isEmpty()) {
        qCritical() << "A hand without conversion skills lost complete empty coverage";
        return false;
    }
    auto *skill = new LegacyResponseCoverageSkill;
    room.roomRuntime()->addSkills(QList<const Skill *>{skill});
    if (viewer->createSkillInstance(skill->objectName(), SourceAcquired, true) <= 0)
        return false;
    // Empty physical cards do not prove a decline: the enabled V1 skill can answer.
    const AIRequest converted = response();
    if (!skill->isAvailable(viewer, converted.reason, converted.pattern)
        || converted.conversionsEnumerated || converted.choiceOptions.candidatesComplete
        || !converted.choiceOptions.cardIds.isEmpty()) {
        qCritical() << "An enabled legacy conversion became a complete empty response";
        return false;
    }
    // Showing and pindian intentionally ask for physical cards even with that skill.
    for (const QString &question : {QStringLiteral("askForCardShow"), QStringLiteral("askForPindian")}) {
        const AIRequest physical = RoomTestAccess::makeResponseRequest(room, viewer, question,
            QString(), question == QStringLiteral("askForPindian") ? Card::MethodPindian : Card::MethodNone);
        if (!physical.choiceOptions.candidatesComplete || !physical.choiceOptions.cardIds.isEmpty()) {
            qCritical() << "A physical-only question lost complete hand coverage" << question;
            return false;
        }
    }
    skill->enabled = false;
    // V1 can use question-specific hooks; isEnabledAtResponse alone cannot prove
    // nullification or compulsory-pattern coverage, even when that hook says no.
    for (const QString &pattern : {QStringLiteral("jink!"), QStringLiteral("nullification")}) {
        const AIRequest unknown = RoomTestAccess::makeResponseRequest(room, viewer,
            pattern == QStringLiteral("nullification") ? QStringLiteral("askForNullification")
                                                       : QStringLiteral("askForCard"),
            pattern, Card::MethodUse);
        if (unknown.conversionsEnumerated || unknown.choiceOptions.candidatesComplete) {
            qCritical() << "A legacy response hook was inferred complete" << pattern;
            return false;
        }
    }
    const Card *jink = findEngineCard(QStringLiteral("jink"));
    if (!jink) return false;
    viewer->addCard(jink->getId(), Player::PlaceHand);
    JudgeStruct judge;
    judge.who = viewer;
    judge.card = jink;
    judge.pattern = QStringLiteral(".|red");
    judge.good = true;
    judge.reason = QStringLiteral("common-context-judge");
    judge.updateResult();
    AIRequest judged = response();
    const quint64 revision = room.roomRuntime()->stateRevision();
    RoomTestAccess::projectDecisionContext(room, viewer, QVariant::fromValue(&judge), judged);
    const QJsonObject view = judged.choiceOptions.context.value("judge").toObject();
    const QJsonObject outcomes = view.value("outcome_by_id").toObject();
    if (!view.value("outcomes_complete").toBool() || outcomes.size() != 1
        || outcomes.value(QString::number(jink->getId())).toBool() != judge.isGood(jink)
        || view.value("card").toObject().contains("id")
        || view.value("card").toObject().contains("subcards")
        || room.roomRuntime()->stateRevision() != revision) {
        qCritical() << "Judgement context lost authority outcomes or leaked backing card data";
        return false;
    }
    auto *filter = new DecisionContextFilterSkill;
    room.roomRuntime()->addSkills(QList<const Skill *>{filter});
    if (viewer->createSkillInstance(filter->objectName(), SourceAcquired, true) <= 0) return false;
    filter->calls = 0;
    RoomTestAccess::projectDecisionContext(room, viewer, QVariant::fromValue(&judge), judged);
    const QJsonObject filtered = judged.choiceOptions.context.value("judge").toObject();
    if (filtered.value("outcomes_complete").toBool()
        || !filtered.value("outcome_by_id").toObject().isEmpty() || filter->calls != 0) {
        qCritical() << "A judgement context speculated through a filter skill";
        return false;
    }
    return true;
}

static bool v2ResponseConversionCardSpecContracts()
{
    const auto failed = [](int line) {
        qCritical() << "V2 response conversion contract failed at line" << line;
        return false;
    };
    ScopedAiEnabled enabled(true);
    std::unique_ptr<Room> room(new Room(nullptr, QStringLiteral("02_1v1")));
    EngineRuntimeContextScope roomScope(*Sanguosha, room.get());
    LuaRuntime::Binding binding(room->roomRuntime()->lua());
    room->roomRuntime()->state().reset();
    ServerPlayer *viewer = RoomTestAccess::addRobotPlayer(*room);
    viewer->setObjectName(QStringLiteral("v2-response-viewer"));
    viewer->setSeat(1); viewer->setMaxHp(4); viewer->setHp(4);
    room->setCurrent(viewer); room->rebuildAlivePlayers();

    const Card *red = nullptr;
    const Card *black = nullptr;
    for (int id = 0; id < Sanguosha->getCardCount() && (!red || !black); ++id) {
        const Card *card = Sanguosha->getCard(id);
        if (!card) continue;
        // Keep physical Jink out of the hand so the response path must choose
        // the V2 conversion rather than an equally legal physical answer.
        if (card->isKindOf("Jink")) continue;
        if (!red && card->isRed()) red = card;
        if (!black && card->isBlack()) black = card;
    }
    if (!red || !black) return failed(__LINE__);
    viewer->addCard(red->getEffectiveId(), Player::PlaceHand);
    viewer->addCard(black->getEffectiveId(), Player::PlaceHand);
    auto *skill = new ResponseConversionSkill;
    room->roomRuntime()->addSkills(QList<const Skill *>{skill});
    const int instance = viewer->createSkillInstance(skill->objectName(), SourceAcquired, true);
    if (instance <= 0) return failed(__LINE__);

    auto make = [&]() {
        return RoomTestAccess::makeResponseRequest(*room, viewer, QStringLiteral("askForCard"),
                                                   QStringLiteral("jink"), Card::MethodResponse);
    };
    const AIRequest request = make();
    const AICardConversionView *ticket = nullptr;
    for (const AICardConversionView &conversion : request.cardConversions) {
        if (conversion.activationRef.key.skillName == skill->objectName()
            && conversion.activationRef.key.instanceID == instance)
            ticket = &conversion;
    }
    if (!ticket || ticket->name != QStringLiteral("jink")
        || ticket->subcardIds != QList<int>({red->getEffectiveId()})
        || !ticket->completeCoverage || !request.conversionsEnumerated) {
        qCritical() << "V2 response conversion ticket was not projected"
                    << request.cardConversions.size();
        return failed(__LINE__);
    }

    // The production Lua response path must parse a ticket-bearing answer.
    const AIResult parsed = room->roomRuntime()->ai().decideIsolated(request);
    if (!parsed.handled || parsed.kind != AIResult::Answer || !parsed.action.hasCardSpec
        || parsed.action.cardSpec.conversionId != ticket->conversionId
        || parsed.action.cardSpec.name != ticket->name
        || parsed.action.cardSpec.subcardIds != ticket->subcardIds) {
        qCritical() << "Lua did not return the authorized response card_spec"
                    << parsed.errorCode << parsed.action.cardSpec.conversionId;
        return failed(__LINE__);
    }

    // The Lua result parser rejects a conversion spec accompanied by a physical
    // card field; this is a representation error before native revalidation.
    {
        LuaRuntime::Binding luaBinding(room->roomRuntime()->ai().lua());
        const QByteArray mixed = QByteArrayLiteral(
            "ai_register_handler('respond_card',function() return "
            "{kind='answer',card_spec={name='jink',conversion_id=1},"
            "cards={999999}} end)");
        if (luaL_dostring(room->roomRuntime()->ai().lua().state(), mixed.constData()) != 0)
            return failed(__LINE__);
    }
    const AIResult mixedParsed = room->roomRuntime()->ai().decideIsolated(request);
    // The dispatcher rejects malformed handler output before native parsing.
    if (mixedParsed.errorCode != QStringLiteral("AI_RUNTIME_ERROR")) return failed(__LINE__);
    {
        LuaRuntime::Binding luaBinding(room->roomRuntime()->ai().lua());
        if (luaL_dostring(room->roomRuntime()->ai().lua().state(),
            "ai_decide=function() return {kind='answer',"
            "card_spec={name='jink',conversion_id=1},cards={999999}} end") != LUA_OK)
            return failed(__LINE__);
    }
    // Raw entry-point output must also be rejected by the native parser.
    const AIResult mixedNative = room->roomRuntime()->ai().decideIsolated(request);
    if (mixedNative.errorCode != QStringLiteral("AI_INVALID_RESULT")) return failed(__LINE__);

    auto answerFor = [&](const AIRequest &req) {
        AIResult answer = parsed;
        answer.decisionId = req.decisionId;
        answer.stateRevision = req.stateRevision;
        return answer;
    };
    // Binding only selects a runtime context; no LuaInvocationScope is active
    // here. Drain pre-existing requests before taking the isolated baseline.
    globalCardLifetimeManager().drain();
    const CardLifetimeGauge before = globalCardLifetimeManager().gauge();
    QPointer<Card> responseGuard;
    {
        CardLifetimeScope cardScope(globalCardLifetimeManager());
        const Card *answered = RoomTestAccess::responseCard(*room, viewer, request, parsed);
        if (!answered || answered->objectName() != QStringLiteral("jink")
            || answered->getSubcards() != ticket->subcardIds
            || answered->getSkillName() != skill->objectName()
            || answered->getActivationSkillName() != skill->objectName()
            || answered->getActivationSkillInstanceId() != instance
            || answered->getSourceSkillName() != skill->objectName()
            || answered->getSourceSkillInstanceId() != instance) return failed(__LINE__);
        if (!answered->lifetimeIsLive()) return failed(__LINE__);
        responseGuard = const_cast<Card *>(answered);
    }
    // Leaving CardLifetimeScope releases its lease but does not reclaim Cards.
    globalCardLifetimeManager().drain();
    // Managed drain retires the token and queues QObject destruction separately.
    if (responseGuard && responseGuard->lifetimeIsLive()) return failed(__LINE__);
    if (responseGuard)
        QCoreApplication::sendPostedEvents(responseGuard.data(), QEvent::DeferredDelete);
    if (!responseGuard.isNull()) return failed(__LINE__);
    if (globalCardLifetimeManager().gauge().managed_live != before.managed_live) return failed(__LINE__);

    // Identity, cost and representation are all bound to the issued ticket.
    for (const auto &mutation : QList<std::function<void(AIResult &)>>{
             [](AIResult &r) { r.action.cardSpec.name = QStringLiteral("peach"); },
             [black](AIResult &r) { r.action.cardSpec.subcardIds = {black->getEffectiveId()}; },
             [](AIResult &r) { r.action.cardSpec.conversionId = -1; },
             [](AIResult &r) { r.action.selectedCardIds = {999999}; }}) {
        AIResult forged = parsed;
        mutation(forged);
        CardLifetimeScope cardScope(globalCardLifetimeManager());
        if (RoomTestAccess::responseCard(*room, viewer, request, forged)) return failed(__LINE__);
    }

    // The issued ticket is request- and revision-scoped.
    AIRequest stale = make();
    AIResult staleAnswer = answerFor(stale);
    room->setPlayerMark(viewer, QStringLiteral("v2_response_stale"), 1);
    CardLifetimeScope staleScope(globalCardLifetimeManager());
    if (RoomTestAccess::responseCard(*room, viewer, stale, staleAnswer)) return failed(__LINE__);

    // Show and pindian requests remain physical-only, even with the V2 skill held.
    for (const QString &question : {QStringLiteral("askForCardShow"),
                                    QStringLiteral("askForPindian")}) {
        const Card::HandlingMethod method = question == QStringLiteral("askForPindian")
            ? Card::MethodPindian : Card::MethodNone;
        const AIRequest physical = RoomTestAccess::makeResponseRequest(
            *room, viewer, question, QString(), method);
        if (!physical.choiceOptions.candidatesComplete)
            return failed(__LINE__);
        AIResult forged = parsed;
        forged.decisionId = physical.decisionId;
        forged.stateRevision = physical.stateRevision;
        CardLifetimeScope cardScope(globalCardLifetimeManager());
        if (RoomTestAccess::responseCard(*room, viewer, physical, forged)) return failed(__LINE__);
    }
    return true;
}

static bool configuredLegacyAdmissionMatchesEffectiveRoutes()
{
    ScopedConfigValue direct(QStringLiteral("AiLegacyDirectCallbacks"), QStringList());
    ScopedConfigValue adapted(QStringLiteral("AiLegacyAdaptedCallbacks"), QStringList());
    ScopedConfigValue isolated(QStringLiteral("AiIsolatedCallbacks"), QStringList());
    if (AiLuaRuntime::requiresLegacyRuntime()) return false;
    {
        ScopedConfigValue invalid(QStringLiteral("AiLegacyDirectCallbacks"),
                                  QStringList{QStringLiteral("not-a-callback:fixture")});
        if (AiLuaRuntime::requiresLegacyRuntime()) return false;
    }
    for (const QString &key : {QStringLiteral("AiLegacyDirectCallbacks"),
                               QStringLiteral("AiLegacyAdaptedCallbacks")}) {
        ScopedConfigValue legacy(key, QStringList{QStringLiteral(" askForCard : fixture ")});
        if (!AiLuaRuntime::requiresLegacyRuntime()) return false;
        ScopedConfigValue overrideRoute(QStringLiteral("AiIsolatedCallbacks"),
                                        QStringList{QStringLiteral("askForCard:fixture")});
        if (AiLuaRuntime::requiresLegacyRuntime()) return false;
    }
    return true;
}

// SmartAI is the fallback every isolated refusal lands on, so a default Room bootstraps
// it on the game VM.  What must stay independent is the routing: the isolated dispatcher
// still answers first, and the legacy AI is reached only when it declines.
static bool defaultRoomLoadsSmartAiFallbackAndStillRoutesIsolated()
{
    ScopedAiEnabled enabled(true);
    ScopedConfigValue direct(QStringLiteral("AiLegacyDirectCallbacks"), QStringList());
    ScopedConfigValue adapted(QStringLiteral("AiLegacyAdaptedCallbacks"), QStringList());
    ScopedConfigValue isolated(QStringLiteral("AiIsolatedCallbacks"), QStringList());
    Room room(nullptr, QStringLiteral("05p"));
    EngineRuntimeContextScope roomScope(*Sanguosha, &room);
    if (!room.roomRuntime()->definitionsLoaded() || !room.roomRuntime()->ai().lua().rawState()) return false;
    {
        LuaRuntime::Binding binding(room.roomRuntime()->lua());
        lua_State *state = room.roomRuntime()->lua().state();
        const int top = lua_gettop(state);
        lua_getglobal(state, "SmartAI");
        const bool hasSmartAI = !lua_isnil(state, -1);
        lua_pop(state, 1);
        lua_getglobal(state, "CloneAI");
        const bool hasCloneAI = lua_isfunction(state, -1);
        lua_pop(state, 1);
        lua_getglobal(state, "sgs");
        const bool hasSgs = lua_istable(state, -1);
        if (hasSgs) lua_getfield(state, -1, "registerModeAI");
        const bool hasModePolicy = hasSgs && lua_isfunction(state, -1);
        lua_settop(state, top);
        if (!hasSmartAI || !hasCloneAI || !hasModePolicy) return false;
    }
    ServerPlayer *viewer = RoomTestAccess::addRobotPlayer(room);
    viewer->setObjectName(QStringLiteral("standalone-viewer"));
    viewer->setSeat(1); viewer->setRole(QStringLiteral("lord"));
    viewer->setMaxHp(4); viewer->setHp(4);
    room.setCurrent(viewer); room.rebuildAlivePlayers();
    // A mandatory one-option choice exercises the production dispatcher and native
    // application; the isolated route answers it even though the legacy fallback exists.
    QString answer;
    return RoomTestAccess::decideAiChoice(room, viewer, QStringLiteral("standalone-fixture"),
                                         QStringLiteral("only"), QVariant(), answer)
        && answer == QStringLiteral("only")
        && room.roomRuntime()->ai().routes().routeFor(AIRequest::Choice, QStringLiteral("askForChoice"))
            == AiRouteIsolated;
}

static bool nativeEventIntentionAndPrivacyContracts()
{
    ScopedAiEnabled enabled(true);
    ScopedConfigValue direct(QStringLiteral("AiLegacyDirectCallbacks"), QStringList());
    ScopedConfigValue adapted(QStringLiteral("AiLegacyAdaptedCallbacks"), QStringList());
    ScopedConfigValue isolated(QStringLiteral("AiIsolatedCallbacks"), QStringList());
    // Poison the true hidden identity both ways: only observed conduct may
    // change the lord viewer's model, never this native-only role string.
    for (const QString &hiddenRole : {QStringLiteral("loyalist"), QStringLiteral("rebel")}) {
        Room room(nullptr, QStringLiteral("05p"));
        EngineRuntimeContextScope roomScope(*Sanguosha, &room);
        ServerPlayer *viewer = RoomTestAccess::addRobotPlayer(room);
        ServerPlayer *other = RoomTestAccess::addRobotPlayer(room);
        viewer->setObjectName(QStringLiteral("event-lord"));
        other->setObjectName(QStringLiteral("event-hidden"));
        viewer->setSeat(1); other->setSeat(2);
        viewer->setRole(QStringLiteral("lord")); other->setRole(hiddenRole);
        for (ServerPlayer *player : {viewer, other}) { player->setMaxHp(4); player->setHp(4); }
        room.setCurrent(viewer); room.rebuildAlivePlayers();
        room.revealRole(viewer);
        const quint64 revision = room.roomRuntime()->stateRevision();
        const auto relation = [viewer, other](const AIWorldView &world) {
            return world.modePolicy.value("relations").toObject().value(viewer->objectName()).toObject()
                .value(other->objectName()).toString();
        };
        AIWorldView world = RoomTestAccess::eventWorld(room, viewer);
        if (!world.modePolicy.value("managed").toBool() || world.players.size() != 1
            || world.players.first().roleVisible || !world.players.first().role.isEmpty()
            || relation(world) != QStringLiteral("unknown") || !world.events.isEmpty()) return false;
        AIEventView inert;
        inert.revision = revision;
        inert.triggerEvent = EventPhaseStart;
        inert.kind = QStringLiteral("other");
        inert.to = viewer->objectName();
        inert.details.insert("player", viewer->objectName());
        QString eventError;
        if (!room.roomRuntime()->ai().processEvent(world, inert, room.roomRuntime()->lua(), &eventError)
            || !eventError.isEmpty() || room.roomRuntime()->stateRevision() != revision) return false;
        {
            LuaRuntime::Binding binding(room.roomRuntime()->ai().lua());
            lua_State *state = room.roomRuntime()->ai().lua().state();
            // Count real calls while preserving the entire production event path.
            if (luaL_dostring(state, "local original=assert(ai_event); native_event_calls=0; native_event_failures=0; "
                "ai_event=function(world,event) native_event_calls=native_event_calls+1; "
                "local ok,result=pcall(original,world,event); if not ok then "
                "native_event_failures=native_event_failures+1; error(result,0) end; "
                "return result end") != LUA_OK) return false;
        }
        const auto calls = [&room]() {
            LuaRuntime::Binding binding(room.roomRuntime()->ai().lua());
            lua_State *state = room.roomRuntime()->ai().lua().state();
            lua_getglobal(state, "native_event_calls");
            const int count = int(lua_tointeger(state, -1));
            lua_pop(state, 1);
            lua_getglobal(state, "native_event_failures");
            const int failures = int(lua_tointeger(state, -1));
            lua_pop(state, 1);
            return failures == 0 ? count : -1;
        };
        DamageStruct damage(QStringLiteral("native-event-fixture"), other, viewer, 1, DamageStruct::Normal);
        RoomTestAccess::recordAiEvent(room, DamageInflicted, viewer, QVariant::fromValue(damage));
        world = room.buildAIWorldView(viewer);
        if (relation(world) != QStringLiteral("enemy") || calls() != 2
            || room.roomRuntime()->stateRevision() != revision || world.revision != revision
            || world.events.isEmpty() || world.events.last().triggerEvent != DamageInflicted
            || world.events.last().revision != revision) return false;
        const quint64 damageSequence = world.events.last().sequence;
        // Same payload at another trigger stage must not apply a second delta.
        RoomTestAccess::recordAiEvent(room, DamageCaused, viewer, QVariant::fromValue(damage));
        if (calls() != 2) return false;
        for (int index = 0; index < 65; ++index)
            RoomTestAccess::recordAiEvent(room, EventPhaseStart, viewer, QVariant());
        world = room.buildAIWorldView(viewer);
        if (world.events.size() != 64 || world.events.first().sequence <= damageSequence
            || relation(world) != QStringLiteral("enemy") || calls() != 2
            || room.roomRuntime()->stateRevision() != revision) return false;
        // Re-reading snapshots cannot replay events that have left the ring.
        room.buildAIWorldView(viewer);
        if (calls() != 2) return false;

        for (const QString &kind : {QStringLiteral("skillChoice"), QStringLiteral("skillInvoke")}) {
            const QString reason = QStringLiteral("private-") + kind;
            RoomTestAccess::recordAiEvent(room, ChoiceMade, other,
                QVariant(kind + QChar(':') + reason + QStringLiteral(":secret-answer")));
            const AIWorldView privateWorld = room.buildAIWorldView(other);
            const AIWorldView publicWorld = room.buildAIWorldView(viewer);
            bool actorSawAnswer = false;
            for (const AIEventView &event : privateWorld.events) {
                if (event.reason == reason)
                    actorSawAnswer = event.privateEvent
                        && event.details.value("answer").toString() == QStringLiteral("secret-answer");
            }
            if (!actorSawAnswer) return false;
            for (const AIEventView &event : publicWorld.events)
                if (event.reason == reason || event.details.value("answer").toString() == QStringLiteral("secret-answer"))
                    return false;
        }
        if (calls() != 4) return false; // Private choices run only in the actor's view.
        RoomTestAccess::recordAiEvent(room, ChoiceMade, other,
            QVariant(QStringLiteral("Yiji:private-gift:event-lord:999999+888888")));
        for (ServerPlayer *observer : {viewer, other}) {
            const AIWorldView giftWorld = room.buildAIWorldView(observer);
            const AIEventView &gift = giftWorld.events.last();
            const QByteArray details = QJsonDocument(gift.details).toJson(QJsonDocument::Compact);
            if (gift.reason != QStringLiteral("private-gift") || gift.targets != QStringList{viewer->objectName()}
                || !gift.cardIds.isEmpty() || !gift.privateCardIds.isEmpty()
                || details.contains("999999") || details.contains("888888")) return false;
        }
        if (room.roomRuntime()->stateRevision() != revision) return false;
        room.setPlayerMark(viewer, QStringLiteral("event-fixture-mutation"), 1);
        if (room.roomRuntime()->stateRevision() <= revision) return false;
    }
    return true;
}

static bool runIsolatedCommonContract(AiLuaRuntime &isolated, const QString &contract,
                                      QString *error)
{
    QFile file(QStringLiteral("tests/lua/") + contract);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return false;
    }
    // Register a test entry point without executing the fixture yet. Its local
    // ai_decide remains the production dispatcher, never a legacy AI callback.
    const QByteArray source = QByteArrayLiteral(
        "local decide = ai_decide\n"
        "local function contract(ai_decide)\n") + file.readAll() + QByteArrayLiteral(
        "\nend\n"
        "ai_decide = function(request)\n"
        "  assert(SmartAI == nil and global_room == nil and current_self == nil)\n"
        "  assert(sgs.Sanguosha == nil)\n"
        "  local ok, result = pcall(contract, decide)\n"
        "  if not ok then _isolated_contract_error = tostring(result); error(result, 0) end\n"
        "  assert(result == true, 'contract did not finish')\n"
        "  return {kind = 'pass'}\n"
        "end\n");
    {
        LuaRuntime::Binding binding(isolated.lua());
        lua_State *state = isolated.lua().state();
        const QByteArray chunkName = (QStringLiteral("@tests/lua/") + contract).toUtf8();
        if (luaL_loadbuffer(state, source.constData(), size_t(source.size()), chunkName.constData()) != LUA_OK
            || lua_pcall(state, 0, 0, 0) != LUA_OK) {
            *error = QString::fromUtf8(lua_tostring(state, -1));
            lua_pop(state, 1);
            return false;
        }
    }
    // Defaults use the decision-scoped RNG. Loading the fixture as a script
    // would bypass ExecutionBinding and incorrectly fail on math.random().
    AIRequest request;
    request.kind = AIRequest::Activate;
    request.decisionId = 1;
    request.stateRevision = 1;
    const AIResult result = isolated.decideIsolated(request);
    if (result.handled && result.kind == AIResult::Pass && result.errorCode.isEmpty()
        && result.decisionId == request.decisionId && result.stateRevision == request.stateRevision)
        return true;

    *error = result.errorCode.isEmpty() ? QStringLiteral("Isolated contract was not handled")
                                      : result.errorCode;
    if (isolated.lua().rawState()) {
        LuaRuntime::Binding binding(isolated.lua());
        lua_State *state = isolated.lua().state();
        lua_getglobal(state, "_isolated_contract_error");
        if (lua_isstring(state, -1))
            *error += QStringLiteral(": ") + QString::fromUtf8(lua_tostring(state, -1));
        lua_pop(state, 1);
    }
    return false;
}

int runIsolatedCommonTests()
{
    const QStringList arguments = QCoreApplication::arguments();
    const int caseIndex = arguments.indexOf(QStringLiteral("--case"));
    const QString selected = caseIndex < 0 ? QString() : arguments.value(caseIndex + 1);
    const QStringList cases{QStringLiteral("policy"), QStringLiteral("admission"),
        QStringLiteral("standalone"), QStringLiteral("events"),
        QStringLiteral("response"), QStringLiteral("conversion"), QStringLiteral("lua")};
    if (caseIndex >= 0 && !cases.contains(selected)) {
        qCritical() << "Unknown ai-common case:" << selected << "Expected:" << cases;
        return 64;
    }
    // Select bounded cases without removing any contract from the default suite.
    const auto runs = [&selected](const char *name) {
        return selected.isEmpty() || selected == QLatin1String(name);
    };
    // Policy contracts run as pure Lua; decision contracts use the production sandbox.
    // One Room provides enum/runtime ownership without reloading gameplay per fixture.
    for (const char *path : {"tests/lua/mode-ai-contract.lua",
                            "tests/lua/isolated-mode-hooks-contract.lua"}) {
        if (!runs("policy")) break;
        std::unique_ptr<lua_State, decltype(&lua_close)> state(luaL_newstate(), &lua_close);
        if (!state) return 40;
        luaL_openlibs(state.get());
        if (luaL_dofile(state.get(), path) != LUA_OK) {
            qCritical() << path << lua_tostring(state.get(), -1);
            return 40;
        }
    }
    if (selected == QLatin1String("policy")) return 0;
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) return 1;
    int result = 0;
    {
        ScopedConfigValue scripts(QStringLiteral("AiIsolatedScripts"), QStringList{
            QStringLiteral("ask-for-use-card.lua"), QStringLiteral("ask-for-choice.lua"),
            QStringLiteral("decision-core.lua"), QStringLiteral("retrial.lua"),
            QStringLiteral("strategy-hooks.lua"), QStringLiteral("event-intention.lua")});
        if (runs("admission") && !configuredLegacyAdmissionMatchesEffectiveRoutes()) result = 45;
        if (result == 0 && runs("standalone") && !defaultRoomLoadsSmartAiFallbackAndStillRoutesIsolated()) result = 46;
        if (result == 0 && runs("events") && !nativeEventIntentionAndPrivacyContracts()) result = 47;
        if (result == 0 && runs("conversion") && !v2ResponseConversionCardSpecContracts()) result = 44;
        // Only physical response and pure-value contracts need this shared Room.
        std::unique_ptr<Room> room;
        if (result == 0 && (runs("response") || runs("lua")))
            room.reset(new Room(nullptr, QStringLiteral("02_1v1")));
        if (result == 0 && runs("response") && !physicalResponseCoveragePreservesConversions(*room)) result = 43;
        const QStringList contracts{
            QStringLiteral("isolated-context-contract.lua"),
            QStringLiteral("isolated-strategy-contract.lua"),
            QStringLiteral("isolated-choice-defaults-contract.lua"),
            QStringLiteral("isolated-response-use-contract.lua"),
            QStringLiteral("isolated-hooks-contract.lua"),
            QStringLiteral("isolated-strategic-helpers-contract.lua"),
            QStringLiteral("isolated-retrial-contract.lua"),
            QStringLiteral("isolated-value-hooks-contract.lua"),
            QStringLiteral("isolated-event-intention-contract.lua")};
        for (const QString &contract : contracts) {
            if (!runs("lua") || !room) break;
            // Each fixture may register callbacks; never let one fixture mask another.
            AiLuaRuntime isolated(room.get());
            if (!isolated.initialize(&error)) { result = 41; continue; }
            if (!runIsolatedCommonContract(isolated, contract, &error)) {
                qCritical().noquote() << contract << error;
                result = 42;
            }
        }
    }
    EngineBootstrap::shutdown();
    return result;
}

int runRoomRuntimeIsolationTests()
{
    // Windows GUI-subsystem builds route Qt messages to the debugger only;
    // mirror them to stderr so checkpoint failures stay visible in CTest logs.
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &message) {
        std::fprintf(stderr, "%s\n", message.toUtf8().constData());
        std::fflush(stderr);
    });
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "Engine bootstrap failed:" << error;
        return 1;
    }
    if (!collateralTargetValidationContract()) {
        qCritical() << "Collateral target validation contract failed";
        return 36;
    }
    const QString bootstrapLuaPackages = Config.value(QStringLiteral("LuaPackages")).toString();
    if (!isolatedInitializationIsBudgeted()) {
        qCritical() << "Isolated AI initialization did not fail within its instruction budget";
        return 16;
    }
    if (!sharedFacadesAreAvailableToAllDecisions()) {
        qCritical() << "Shared AI facades were not available to every isolated decision";
        return 22;
    }
    // 2026-09-20: fc1c09e made makeRequest stop after the request header while AI is
    // off, and routeFor pin every decision to AiRouteLegacyDirect there. Both are the
    // right production behaviour for an --ai off server, and both make this suite's
    // subject unreachable, so the suite asks its questions with the AI on. The off
    // shape is a contract of its own and is pinned inside
    // aiWorldViewIsScopedAndRevisioned().
    ScopedAiEnabled aiEnabled(true);
    ScopedConfigValue isolatedScripts(QStringLiteral("AiIsolatedScripts"),
                                      QStringList({QStringLiteral("ask-for-use-card.lua"),
                                                   QStringLiteral("standard-ai.lua")}));

    const GameSessionConfig sessionConfig(Q_UINT64_C(0x12345678abcdef01));
    std::unique_ptr<Room> first(new Room(nullptr, QStringLiteral("02_1v1"), sessionConfig));
    std::unique_ptr<Room> second(new Room(nullptr, QStringLiteral("02_1v1"), sessionConfig));
    if (Config.value(QStringLiteral("LuaPackages")).toString() != bootstrapLuaPackages
        || !first->roomRuntime()->definitionsLoaded()
        || !second->roomRuntime()->definitionsLoaded()) {
        qCritical() << "Room definition loading mutated bootstrap configuration";
        return 2;
    }
    if (first->getGameSeed() != sessionConfig.seed || second->getGameSeed() != sessionConfig.seed
        || RoomTestAccess::drawPile(*first) != RoomTestAccess::drawPile(*second)) {
        qCritical() << "The session seed did not control the initial room draw pile";
        return 18;
    }
    const int firstLuaRandom = gameLuaRandomAfterLocalSeed(*first, 1);
    const int secondLuaRandom = gameLuaRandomAfterLocalSeed(*second, 999);
    if (firstLuaRandom < 0 || firstLuaRandom != secondLuaRandom) {
        qCritical() << "The session seed did not control the game Lua random sequence";
        return 19;
    }
    const QByteArray firstHashOrder = gameLuaHashOrder(*first);
    if (firstHashOrder.isEmpty() || firstHashOrder != gameLuaHashOrder(*second)) {
        qCritical() << "The session seed did not control the game Lua hash order";
        return 20;
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
    if (!pureValueBoundaryContract()) {
        qCritical() << "Shared value boundary contract failed";
        return 24;
    }
    if (!modePolicyAndRoleVisibilityAreRoomLocal()) {
        qCritical() << "Mode policy or role visibility crossed rooms";
        return 17;
    }
    if (!pureModePolicyContract()) {
        qCritical() << "Mode policy contract failed";
        return 17;
    }
    if (!aiWorldViewIsScopedAndRevisioned()) {
        qCritical() << "AI world view scope or state revision gate failed";
        return 17;
    }
    if (!officialLianyingHandlerMatchesIsolated()) {
        qCritical() << "Official lianying handler did not match isolated AI";
        return 21;
    }
    if (!valueDecisionsRouteThroughTheIsolatedVm()) {
        qCritical() << "Value-typed AI decisions did not route through the isolated VM";
        return 25;
    }
    if (!decisionCorePlansATurnFromCandidates()) {
        qCritical() << "The generic decision core did not plan a turn from candidates";
        return 26;
    }
    if (!useCardPlanContract()) {
        qCritical() << "The isolated use-plan contract failed";
        return 30;
    }
    if (!cardCandidatesDescribeTheQuestionAsked()) {
        qCritical() << "Card candidates did not describe the question that was asked";
        return 31;
    }
    if (!illegalAiTargetsNeverReachGameplay()) {
        qCritical() << "An illegal AI target reached gameplay";
        return 32;
    }
    if (!slashAndPeachPlanThroughOneSharedPath()) {
        qCritical() << "The Slash/Peach vertical did not plan through one shared path";
        return 33;
    }
    if (!trickFamiliesPlanWithTheirOwnValuations()) {
        qCritical() << "The Duel and strip families did not plan with their own "
                       "valuations";
        return 34;
    }
    if (const int planningResult = isolatedPlanningContracts()) return planningResult;
    if (!conversionsAreAuthorizedNotClaimed()) {
        qCritical() << "A card conversion was claimed rather than authorized";
        return 35;
    }
    if (!coverageReportListsWhatIsWired()) {
        qCritical() << "The isolated coverage report did not match the wired handlers";
        return 27;
    }
    if (!isolatedScriptsComeFromLuaAndEnabledPackages()) {
        qCritical() << "Isolated scripts did not come from the Lua declaration and the "
                       "enabled packages";
        return 28;
    }
    if (!aBrokenPackageHandlerDoesNotStopTheRuntime()) {
        qCritical() << "A broken package handler stopped the isolated runtime";
        return 29;
    }
    if (!productionIsolatedScriptAndFallback(*first)
        || !ownedAiProxyUsesValueLifetime()) {
        qCritical() << "Production isolated loading or proxy ownership failed";
        return 15;
    }
    if (!legacyCallbackAbiAndResultConversion(*first)) {
        qCritical() << "Legacy callback ABI or result conversion contract failed";
        return 23;
    }
    if (!aiIsolatedParsesPassAndUseCard(*second)) {
        qCritical() << "AI isolated result parsing failed";
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
        || !invokeIncrement(*second, secondCallback, 41) || !aiIsolatedParsesPassAndUseCard(*second)) {
        qCritical() << "Destroying one room invalidated another room runtime";
        return 10;
    }

    second.reset();
    EngineBootstrap::shutdown();
    qInfo() << "room runtime isolation passed";
    return 0;
}
