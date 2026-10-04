#include "external-agent.h"
#include "external-agent-transport.h"
#include <QJsonArray>
#include <QJsonDocument>
#include "ai-decision-coordinator.h"
#include "engine-bootstrap.h"
#include "engine.h"
#include "package.h"
#include "room.h"
#include "roomthread.h"
#include "request-coordinator.h"
#include "serverplayer.h"
#include "server-core.h"
#include "settings.h"
#include "runtime-paths.h"
#include <QCoreApplication>
#include <QTimer>
#include <QElapsedTimer>
#include <QDebug>
#include <lua.hpp>
#include <QFile>
#include <atomic>
#include <future>
#include <thread>

#define CHECK(x) do { if (!(x)) qFatal("CHECK failed at %s:%d: %s", __FILE__, __LINE__, #x); } while (false)

struct RoomTestAccess {
    static void attachThread(Room &room) { room.thread = new RoomThread(&room); }
    static AiDecisionCoordinator &ai(Room &room) { return *room.m_aiDecisions; }
    static void begin(ExternalAgentEndpoint &e, const AIRequest &q) { e.begin(q); }
    static auto await(ExternalAgentEndpoint &e, AIResult &r) { return e.awaitReply(r); }
    static void finish(ExternalAgentEndpoint &e) { e.finish(); }
    static bool waitRace(Room &room) { return room.m_requests->acquireRaceSignal(-1); }
    static bool wait(Room &room, ServerPlayer *p, time_t timeout) {
        return room.m_requests->acquireInteractive(p, timeout);
    }
};

static AIRequest question()
{
    AIRequest q;
    q.viewerObjectName = "seat";
    q.decisionId = 9;
    q.stateRevision = 17;
    q.kind = AIRequest::Choice;
    q.choiceOptions.choices << "yes" << "no";
    return q;
}

static void mailboxTests()
{
    ExternalAgentEndpoint endpoint("seat", ExternalAgentEndpoint::Pause);
    auto q = question();
    RoomTestAccess::begin(endpoint, q);
    CHECK(endpoint.status() == "waiting-no-clock");
    auto waiting = std::async(std::launch::async, [&] { AIResult r; return RoomTestAccess::await(endpoint, r); });
    CHECK(waiting.wait_for(std::chrono::milliseconds(150)) == std::future_status::timeout);
    auto r = mockExternalAgentAnswer(q);
    QString error;
    r.decisionId--;
    CHECK(!endpoint.submit(r, &error) && error == "stale");
    r = mockExternalAgentAnswer(q); r.stateRevision--;
    CHECK(!endpoint.submit(r, &error) && error == "stale");
    r = mockExternalAgentAnswer(q); r.action.userString = "unoffered";
    CHECK(!endpoint.submit(r, &error) && error == "invalid-answer");
    endpoint.disconnect();
    CHECK(endpoint.status() == "paused-disconnected");
    AIRequest resumed;
    CHECK(!endpoint.pending(resumed));
    CHECK(waiting.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout);
    endpoint.reconnect();
    CHECK(endpoint.pending(resumed) && resumed.decisionId == q.decisionId);
    r = mockExternalAgentAnswer(resumed);
    CHECK(endpoint.submit(r));
    CHECK(!endpoint.submit(r, &error) && error == "duplicate");
    CHECK(waiting.get() == ExternalAgentEndpoint::Reply);
    RoomTestAccess::finish(endpoint);
    CHECK(!endpoint.submit(r));
    RoomTestAccess::begin(endpoint, q); endpoint.cancel();
    AIResult ignored;
    CHECK(RoomTestAccess::await(endpoint, ignored) == ExternalAgentEndpoint::Cancelled);
    CHECK(!endpoint.pending(resumed));
    RoomTestAccess::finish(endpoint);
    ExternalAgentEndpoint fallback("seat", ExternalAgentEndpoint::SmartAIFallback);
    RoomTestAccess::begin(fallback, q); fallback.disconnect();
    CHECK(RoomTestAccess::await(fallback, ignored) == ExternalAgentEndpoint::Fallback);
    CHECK(fallback.status() == "smart-ai-fallback");
    RoomTestAccess::finish(fallback); fallback.reconnect();
    q.decisionId++;
    RoomTestAccess::begin(fallback, q);
    CHECK(!fallback.submit(r));
    fallback.cancel(); RoomTestAccess::finish(fallback);
    // Structured limits and selection contracts.
    q.kind = AIRequest::Discard; q.choiceOptions.cardIds = {1, 2};
    q.choiceOptions.minCount = q.choiceOptions.maxCount = 2;
    r = mockExternalAgentAnswer(q);
    CHECK(ExternalAgentEndpoint::validateShape(q, r));
    r.action.selectedCardIds = {1,1}; CHECK(!ExternalAgentEndpoint::validateShape(q,r));
    r.action.selectedCardIds = {1,3}; CHECK(!ExternalAgentEndpoint::validateShape(q,r));
    r.action.selectedCardIds = {1}; CHECK(!ExternalAgentEndpoint::validateShape(q,r));
    q.kind = AIRequest::RespondCard; q.choiceOptions.minCount = q.choiceOptions.maxCount = 1;
    for (const auto &pattern : {"jink", "peach", "nullification"}) {
        q.pattern = pattern; r = mockExternalAgentAnswer(q);
        CHECK(ExternalAgentEndpoint::validateShape(q,r));
        r.action.selectedCardIds = {99}; CHECK(!ExternalAgentEndpoint::validateShape(q,r));
    }
    qInfo() << "PASS mailbox: asynchronous wait, stale, duplicate, shape, reconnect, cancellation, explicit fallback";
}

static ServerPlayer *addSeat(Room &room, const QString &name, const QString &role)
{
    auto *p = room.addAIPlayer();
    p->setObjectName(name);
    p->setScreenName(name);
    p->setAlive(true);
    p->setSeat(room.getPlayers().size());
    room.rebuildAlivePlayers();
    p->setGeneral(Sanguosha->getGeneral("caocao"));
    p->setMaxHp(4); p->setHp(4); p->setRole(role);
    return p;
}

static std::thread answerNext(std::shared_ptr<ExternalAgentEndpoint> endpoint,
                             std::function<void(const AIRequest &, AIResult &)> inspect = {})
{
    return std::thread([endpoint, inspect] {
        QElapsedTimer clock; clock.start();
        AIRequest q;
        while (!endpoint->pending(q)) {
            CHECK(clock.elapsed() < 10000);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        auto r = mockExternalAgentAnswer(q);
        if (inspect) inspect(q, r);
        CHECK(endpoint->submit(r));
    });
}

class CountingAI : public TrustAI {
public:
    explicit CountingAI(ServerPlayer *p) : TrustAI(p) {}
    int calls = 0;
    QString askForChoice(const QString &, const QString &, const QVariant &) override {
        ++calls; return "no";
    }
};

static void roomTests()
{
    Room room(nullptr, "02p");
    EngineRuntimeContextScope engineContext(*Sanguosha, &room);
    LuaRuntime::Binding luaContext(room.roomRuntime()->lua());
    room.roomRuntime()->state().reset();
    auto *p = addSeat(room, "external", "rebel");
    auto *other = addSeat(room, "opponent", "renegade");
    p->setAI(new TrustAI(p));
    auto *counting = new CountingAI(other); other->setAI(counting);
    auto endpoint = room.attachExternalAgent(p, ExternalAgentEndpoint::Pause);
    CHECK(endpoint);
    CHECK(!room.attachExternalAgent(p, ExternalAgentEndpoint::Pause));
    auto otherEndpoint = room.attachExternalAgent(other, ExternalAgentEndpoint::SmartAIFallback);
    CHECK(otherEndpoint);
    room.setNoClock(true); CHECK(room.noClock());
    auto wait = std::async(std::launch::async, [&] { return RoomTestAccess::wait(room, p, -1); });
    CHECK(wait.wait_for(std::chrono::milliseconds(150)) == std::future_status::timeout);
    p->releaseLock(ServerPlayer::SEMA_COMMAND_INTERACTIVE);
    CHECK(wait.get());
    CHECK(!RoomTestAccess::wait(room, p, 0));
    p->setState("online");
    auto native = std::async(std::launch::async, [&] { return room.getResult(p, 0); });
    CHECK(native.wait_for(std::chrono::milliseconds(150)) == std::future_status::timeout);
    p->releaseLock(ServerPlayer::SEMA_COMMAND_INTERACTIVE);
    CHECK(!native.get()); // wake without a fabricated reply
    room.setNoClock(false);
    CHECK(!room.getResult(p, 0)); // finite rooms still expire
    room.setNoClock(true);
    p->setState("robot");
    RoomTestAccess::attachThread(room);
    // Own cards are visible, the other seat's cards/role/private flags are not.
    int jink = -1, peach = -1, nullification = -1;
    for (int id : room.getDrawPile()) {
        const Card *c = Sanguosha->getCard(id);
        if (c->objectName() == "jink") jink = id;
        if (c->objectName() == "peach") peach = id;
        if (c->objectName() == "nullification") nullification = id;
    }
    CHECK(jink >= 0 && peach >= 0 && nullification >= 0);
    room.moveCardTo(Sanguosha->getCard(jink), p, Player::PlaceHand, false);
    room.moveCardTo(Sanguosha->getCard(peach), p, Player::PlaceHand, false);
    room.moveCardTo(Sanguosha->getCard(nullification), other, Player::PlaceHand, false);
    other->setFlags("private-opponent-flag");
    other->acquireSkill("jianxiong");
    CHECK(!other->getSkillInstances().isEmpty());
    const auto privateSkill = other->getSkillInstances().first();
    other->setSkillInstanceState(privateSkill.skillName, privateSkill.instanceID, {{"private-secret", 739}});
    room.setPlayerMark(other, "private-mark", 12, {other});
    CardsMoveOneTimeStruct hiddenMove{};
    hiddenMove.card_ids << nullification;
    hiddenMove.to_place = Player::DrawPile;
    RoomTestAccess::ai(room).recordEvent(CardsMoveOneTime, nullptr, QVariant::fromValue(hiddenMove));
    auto world = room.buildAIWorldView(p);
    AIRequest observed; observed.worldView = world;
    observed.decisionId = Q_UINT64_C(9007199254740993);
    const auto wire = externalAgentRequestJson(observed);
    CHECK(wire.value("decisionId").toString() == "9007199254740993");
    const auto bytes = QJsonDocument(wire).toJson();
    CHECK(!bytes.contains("private-opponent-flag"));
    CHECK(!bytes.contains("private-secret"));
    CHECK(!bytes.contains("private-mark"));
    CHECK(wire.value("worldView").toObject().value("handCards").toArray().size() == 2);
    AIResult decoded;
    QJsonObject reply{{"decisionId", "9007199254740993"}, {"stateRevision", "1"},
                      {"kind", "pass"}, {"action", QJsonObject()}};
    CHECK(externalAgentResultJson(reply,decoded));
    CHECK(decoded.decisionId == observed.decisionId);
    reply["decisionId"] = 9; CHECK(!externalAgentResultJson(reply,decoded));
    reply["decisionId"] = "9"; reply["action"] = QJsonObject{{"useCardId", 1.5}};
    CHECK(!externalAgentResultJson(reply,decoded));
    CHECK(world.handCards.size() == 2);
    for (const auto &v : world.players) if (v.objectName == other->objectName()) {
        CHECK(v.knownCards.isEmpty()); CHECK(v.role.isEmpty()); CHECK(!v.roleVisible);
        CHECK(v.privateFlags.isEmpty()); CHECK(!v.publicMarks.contains("private-mark"));
        for (const auto &skill : v.skills) CHECK(skill.state.isEmpty());
    }
    for (const auto &event : world.events) {
        CHECK(!event.privateCardIds.contains(nullification));
        CHECK(!event.cardIds.contains(nullification));
    }
    Config.EnableAI = false; // explicit external seats retain their observation/context
    auto worker = answerNext(endpoint, [&](const AIRequest &q, AIResult &r) {
        CHECK(q.viewerObjectName == p->objectName());
        CHECK(q.worldView.handCards.size() == 2);
        CHECK(q.choiceOptions.context.value("player").toString() == other->objectName());
        AIRequest forbidden; CHECK(!otherEndpoint->pending(forbidden));
        auto bad = r; bad.action.userString = "cheat"; CHECK(!endpoint->submit(bad));
    });
    QString answer;
    CHECK(RoomTestAccess::ai(room).decideChoice(p, "test", "yes+no", QVariant::fromValue(other), answer));
    worker.join(); CHECK(answer == "yes");
    Config.EnableAI = true;
    CHECK(endpoint->status() == "idle");
    for (const auto &pattern : {QString("jink"), QString("peach"), QString("nullification")}) {
        worker = answerNext(endpoint, [&](const AIRequest &q, AIResult &r) {
            CHECK(q.kind == AIRequest::RespondCard && q.pattern == pattern);
            if (pattern != "nullification") {
                r.kind = AIResult::Answer;
                r.action.selectedCardIds = {pattern == "jink" ? jink : peach};
                if (pattern == "jink") {
                    auto wrongPattern = r; wrongPattern.action.selectedCardIds = {peach};
                    CHECK(!endpoint->submit(wrongPattern));
                }
            }
        });
        const Card *card = RoomTestAccess::ai(room).decideResponseCard(p, pattern, "test", QVariant(), Card::MethodResponse);
        worker.join();
        CHECK(pattern == "nullification" ? card == nullptr : card != nullptr);
    }
    // Illegal play stays pending; a corrected pass uses the same prompt, without AI takeover.
    auto play = RoomTestAccess::ai(room).makeRequest(p, AIRequest::Activate, CardUseStruct::CARD_USE_REASON_PLAY,
                                  QString(), QString(), Card::MethodUse);
    std::thread illegal([&] {
        QElapsedTimer clock; clock.start(); AIRequest q;
        while (!endpoint->pending(q)) { CHECK(clock.elapsed() < 10000); std::this_thread::yield(); }
        auto r = mockExternalAgentAnswer(q); r.kind = AIResult::UseCard;
        r.action.useCardId = nullification; // belongs to the other seat
        CHECK(endpoint->submit(r));
        while (endpoint->lastError() != "illegal-action") { CHECK(clock.elapsed() < 10000); std::this_thread::yield(); }
        CHECK(endpoint->pending(q)); CHECK(q.decisionId == play.decisionId);
        CHECK(endpoint->submit(mockExternalAgentAnswer(q)));
    });
    CardUseStruct use; CHECK(RoomTestAccess::ai(room).decide(p, play, use));
    illegal.join(); CHECK(!use.card);
    // Explicit fallback invokes the configured seat AI exactly once, and reconnect returns to external.
    otherEndpoint->disconnect();
    CHECK(RoomTestAccess::ai(room).decideChoice(other,"test","yes+no",QVariant(),answer));
    CHECK(answer == "no" && counting->calls == 1);
    otherEndpoint->reconnect();
    worker = answerNext(otherEndpoint);
    CHECK(RoomTestAccess::ai(room).decideChoice(other,"test","yes+no",QVariant(),answer));
    worker.join(); CHECK(answer == "yes" && counting->calls == 1);
    auto race = std::async(std::launch::async, [&] { return RoomTestAccess::waitRace(room); });
    CHECK(race.wait_for(std::chrono::milliseconds(150)) == std::future_status::timeout);
    // An authoritative revision change never restamps an old result.
    play.stateRevision = 0;
    worker = answerNext(endpoint);
    bool staleCancelled = false;
    try { RoomTestAccess::ai(room).decide(p, play, use); }
    catch (TriggerEvent e) { staleCancelled = e == GameFinished; }
    worker.join(); CHECK(staleCancelled); CHECK(endpoint->lastError() == "stale-world");
    CHECK(race.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    CHECK(!race.get());
    // Cancellation wakes an indefinite native request wait and external decision.
    auto aborted = std::async(std::launch::async, [&] { return RoomTestAccess::wait(room,p,-1); });
    room.abortWaitingRequests();
    CHECK(aborted.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    CHECK(endpoint->status() == "cancelled");
    qInfo() << "PASS room: hidden information, seat isolation, strict choices, Jink/Peach/Nullification, no-clock wake/abort";
}

static int gameTest(QCoreApplication &app)
{
    auto *room = new Room(nullptr, "02p", GameSessionConfig(20261003));
    CHECK(room->hasLuaRuntime());
    room->setProperty("to_test", "headless");
    room->setNoClock(true);
    Config.AIDelay = Config.OriginAIDelay = 0;
    Config.CountDownSeconds = 0;
    auto *p = room->addAIPlayer(); p->setOwner(true); room->signup(p,"ExternalMock","",true);
    auto endpoint = room->attachExternalAgent(p, ExternalAgentEndpoint::Pause); CHECK(endpoint);
    auto *opponent = room->addAIPlayer(); room->signup(opponent,"SmartAI","",true);
    int callbackErrors = 0;
    QObject::connect(room, &Room::room_message, &app, [&](const QString &message) {
        qInfo().noquote() << "ROOM:" << message;
        if (message.contains("[AI_CALLBACK_ERROR]") || message.contains("attempt to ")
            || message.contains("bad argument #")) ++callbackErrors;
    });
    QTimer adapter;
    int decisions = 0, heartbeats = 0;
    const bool cancelGame = app.arguments().contains("--cancel-game");
    bool cancelled = false;
    QObject::connect(&adapter, &QTimer::timeout, [&] {
        ++heartbeats;
        if (cancelled && room->allGameThreadsStopped()) {
            adapter.stop(); delete room; room = nullptr;
            CHECK(endpoint->status() == "cancelled");
            qInfo() << "PASS cancellation cleanup: pending decision unwound, workers stopped, room destroyed";
            app.quit(); return;
        }
        AIRequest q;
        if (!endpoint->pending(q)) return;
        if (cancelGame) { cancelled = true; endpoint->cancel(); return; }
        auto r = mockExternalAgentAnswer(q);
        QString error;
        if (!endpoint->submit(r, &error)) qFatal("mock unsupported kind=%d question=%s: %s", int(q.kind), qPrintable(q.choiceOptions.question),qPrintable(error));
        ++decisions;
        qInfo() << "EXTERNAL_MOCK decision" << q.decisionId << "kind" << int(q.kind) << "pattern" << q.pattern;
    });
    adapter.start(5);
    bool terminal = false;
    std::atomic_bool smartAiAtTerminal{false};
    QObject::connect(room, &Room::game_over, &app, [&](const QString &) {
        smartAiAtTerminal = dynamic_cast<LuaAI *>(opponent->getSmartAI()) != nullptr;
    }, Qt::DirectConnection); // inspect on the worker before its AI teardown
    QObject::connect(room, &Room::game_over, &app, [&](const QString &winner) {
        CHECK(!winner.isEmpty()); CHECK(decisions > 0); CHECK(heartbeats > 0);
        CHECK(callbackErrors == 0); CHECK(smartAiAtTerminal);
        terminal = true;
        qInfo() << "PASS complete game winner=" << winner << "external decisions=" << decisions << "policy=pause callback_errors=" << callbackErrors;
        QTimer::singleShot(500, &app, [&] {
            CHECK(room->allGameThreadsStopped());
            adapter.stop(); delete room; room = nullptr;
            CHECK(endpoint->status() == "cancelled");
            qInfo() << "PASS cleanup: workers stopped, room destroyed, endpoint cancelled";
            app.quit();
        });
    });
    QTimer::singleShot(120000, &app, [&] { if (!terminal) qFatal("game timed out"); });
    room->start();
    app.exec();
    CHECK((terminal || cancelled) && room == nullptr);
    return 0;
}

static bool correctionCompatibility(const QString &path)
{
    Room room(nullptr, "02p");
    EngineRuntimeContextScope scope(*Sanguosha, &room);
    LuaRuntime::Binding binding(room.roomRuntime()->lua());
    auto *L = room.roomRuntime()->lua().rawState();
    QFile file(path); CHECK(file.open(QIODevice::ReadOnly));
    const auto source = file.readAll();
    const auto start = source.indexOf("ny_10th_jieling_target = sgs.CreateTargetModSkillV2{");
    const auto end = source.indexOf("\n}\n",start);
    CHECK(start >= 0 && end > start);
    const QByteArray script = QByteArray(R"lua(
local factory = sgs.CreateTargetModSkillV2
sgs.CreateTargetModSkillV2 = function(t) return t end
)lua") + source.mid(start,end+3-start) + R"lua(
sgs.CreateTargetModSkillV2 = factory
local matching = false
local mod = sgs.TargetModSkill_Residue
local ctx = {
    getModType = function() return mod end,
    getPrimary = function() return nil end,
    getCard = function() return {getSkillNames = function()
        return matching and {"ny_10th_jieling"} or {} end} end
}
for _, kind in ipairs({sgs.TargetModSkill_Residue, sgs.TargetModSkill_DistanceLimit}) do
    mod = kind
    matching = false
    local none = ny_10th_jieling_target.correct_func(nil,ctx)
    assert(not none.applies and none.value == 0 and not none.unlimited)
    matching = true
    local amount = ny_10th_jieling_target.correct_func(nil,ctx)
    assert(amount.applies and amount.value == 1000 and not amount.unlimited)
end
mod = -1
assert(not ny_10th_jieling_target.correct_func(nil,ctx).applies)
)lua";
    const int code = luaL_loadbuffer(L,script.constData(),script.size(),"sgs10th-correction-regression");
    if (code != LUA_OK || LuaRuntime::protectedCall(L,0,0,0) != LUA_OK) {
        qCritical() << "Engine Lua" << LUA_RELEASE << lua_tostring(L,-1);
        lua_pop(L,1); return false;
    }
    qInfo() << "PASS actual sgs10th correction callback: noEffect/useAmount, both modifier branches";
    return true;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().contains("--lua-parse")) {
        lua_State *L = luaL_newstate();
        qInfo() << "Parser:" << LUA_RELEASE;
        const auto args = app.arguments();
        for (int i = args.indexOf("--lua-parse") + 1; i < args.size(); ++i) {
            if (luaL_loadfile(L, qPrintable(args[i])) != LUA_OK) {
                qCritical() << lua_tostring(L, -1); lua_close(L); return 1;
            }
            lua_pop(L, 1);
        }
        lua_close(L); return 0;
    }
    mailboxTests();
    QString error;
    CHECK(QSanRuntimePaths::resolve(app.arguments(), &error));
    Config.setValueOverrides({{"AiLegacyDirectCallbacks", QStringLiteral(
        "activate askForUseCard askForSkillInvoke askForChoice askForSuit askForKingdom "
        "askForGeneral askForDiscard askForAG askForCardChosen askForYiji askForPlayerChosen "
        "askForPlayersChosen askForCard askForNullification askForCardShow askForPindian "
        "askForSinglePeach askForGuanxing askForTriggerOrder").split(' ')},
        {"AiIsolatedCallbacks", QStringList()}});
    CHECK(EngineBootstrap::initialize(false, &error));
    QObject::disconnect(&app, SIGNAL(aboutToQuit()), Sanguosha, SLOT(deleteLater()));
    Config.init();
    Config.EnableAI = true;
    Config.Enable2ndGeneral = false;
    Config.EnableHegemony = false;
    Config.GameMode = Sanguosha->getGameMode("02p");
    // A sufficient, explicit native general pool keeps this a boundary test.
    Config.BanPackages.clear();
    const QStringList packages = {"standard", "standard_cards", "standard_ex_cards", "maneuvering"};
    for (const auto *package : Sanguosha->getPackages())
        if (!packages.contains(package->objectName())) Config.BanPackages << package->objectName();
    Config.AIDelay = Config.OriginAIDelay = 0;
    int result = 0;
    if (app.arguments().contains("--game") || app.arguments().contains("--cancel-game")) result = gameTest(app);
    else if (app.arguments().contains("--lua-corrections"))
        result = correctionCompatibility(app.arguments().last()) ? 0 : 1;
    else roomTests();
    EngineBootstrap::shutdown();
    return result;
}
