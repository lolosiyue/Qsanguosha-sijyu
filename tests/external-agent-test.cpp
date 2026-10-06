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
#include "standard.h"
#include "wrapped-card.h"
#include <QCoreApplication>
#include <QTimer>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QDebug>
#include <lua.hpp>
#include <QFile>
#include <QCryptographicHash>
#include <atomic>
#include <future>
#include <thread>

#define CHECK(x) do { if (!(x)) qFatal("CHECK failed at %s:%d: %s", __FILE__, __LINE__, #x); } while (false)

struct RoomTestAccess {
    static void attachThread(Room &room) { room.thread = new RoomThread(&room); }
    static AiDecisionCoordinator &ai(Room &room) { return *room.m_aiDecisions; }
    static void begin(ExternalAgentEndpoint &e, const AIRequest &q) { e.begin(q); }
    static auto await(ExternalAgentEndpoint &e, AIResult &r) { return e.awaitReply(r); }
    static void reject(ExternalAgentEndpoint &e, const QString &error) { e.reject(error); }
    static void finish(ExternalAgentEndpoint &e) { e.finish(); }
    static bool runNativeChoice(Room &room, ServerPlayer *player,
                                const AIRequest &request, AIResult &result,
                                int &nativeCalls) {
        auto &ai = *room.m_aiDecisions;
        return ai.runAnswer(player, request, QStringLiteral("askForChoice"),
            [&nativeCalls](const AIRequest &q) {
                ++nativeCalls;
                return AiDecisionCoordinator::legacyAnswerResult(q, QStringLiteral("no"));
            }, result);
    }
    static bool hybridSelected(const Room &room) { return room.m_hybrid50Selected; }
    static QString hybridGameId(const Room &room) { return room.m_sessionConfig.hybridGameId; }
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
    // Only the opted-in hybrid uses an absolute deadline. After it falls
    // back, a late result for the same ticket cannot replace native SmartAI.
    ExternalAgentEndpoint bounded("seat", ExternalAgentEndpoint::SmartAIFallback);
    bounded.enableBoundedHybrid(25);
    CHECK(bounded.boundedHybrid());
    q = question();
    RoomTestAccess::begin(bounded, q);
    QThread::msleep(10);
    RoomTestAccess::reject(bounded, QStringLiteral("invalid-answer"));
    CHECK(RoomTestAccess::await(bounded, ignored) == ExternalAgentEndpoint::Fallback);
    CHECK(!bounded.submit(mockExternalAgentAnswer(q), &error));
    RoomTestAccess::finish(bounded);
    CHECK(!fallback.boundedHybrid());
    ExternalAgentEndpoint late("seat", ExternalAgentEndpoint::SmartAIFallback);
    late.enableBoundedHybrid(10);
    RoomTestAccess::begin(late, q);
    QThread::msleep(20); // expire before either submit or await runs
    CHECK(!late.submit(mockExternalAgentAnswer(q), &error) && error == "adapter-timeout");
    CHECK(RoomTestAccess::await(late, ignored) == ExternalAgentEndpoint::Fallback);
    CHECK(!late.requestLocal(q.decisionId, q.stateRevision, &error));
    RoomTestAccess::finish(late);
    // Local is a per-decision resolution, independent of the disconnect policy.
    ExternalAgentEndpoint local("seat", ExternalAgentEndpoint::Pause);
    q = question();
    CHECK(!local.requestLocal(q.decisionId,q.stateRevision,&error) && error == "not-waiting");
    RoomTestAccess::begin(local,q);
    CHECK(!local.requestLocal(q.decisionId-1,q.stateRevision,&error) && error == "stale");
    CHECK(!local.requestLocal(q.decisionId,q.stateRevision-1,&error) && error == "stale");
    CHECK(local.pending(resumed) && resumed.decisionId == q.decisionId);
    CHECK(local.requestLocal(q.decisionId,q.stateRevision,&error) && error.isEmpty());
    CHECK(local.lastError().isEmpty() && local.status() == "local-queued");
    CHECK(!local.pending(resumed));
    CHECK(!local.requestLocal(q.decisionId,q.stateRevision,&error) && error == "duplicate");
    CHECK(!local.submit(mockExternalAgentAnswer(q),&error) && error == "duplicate");
    CHECK(RoomTestAccess::await(local,ignored) == ExternalAgentEndpoint::Local);
    RoomTestAccess::finish(local);
    CHECK(local.status() == "idle" && local.lastError().isEmpty());
    CHECK(!local.requestLocal(q.decisionId,q.stateRevision,&error) && error == "not-waiting");
    ++q.decisionId;
    RoomTestAccess::begin(local,q);
    CHECK(local.pending(resumed));
    CHECK(!local.requestLocal(q.decisionId-1,q.stateRevision,&error) && error == "stale");
    CHECK(local.submit(mockExternalAgentAnswer(q),&error) && error.isEmpty());
    CHECK(RoomTestAccess::await(local,ignored) == ExternalAgentEndpoint::Reply);
    RoomTestAccess::finish(local);
    // Both seats can be pending at once, and one seat's route cannot resolve the other.
    auto otherQuestion = q; otherQuestion.viewerObjectName = "other"; ++otherQuestion.decisionId;
    ExternalAgentEndpoint other("other", ExternalAgentEndpoint::Pause);
    ++q.decisionId; ++q.decisionId;
    RoomTestAccess::begin(local,q); RoomTestAccess::begin(other,otherQuestion);
    CHECK(!local.requestLocal(otherQuestion.decisionId,otherQuestion.stateRevision,&error) && error == "stale");
    CHECK(other.pending(resumed) && resumed.viewerObjectName == "other");
    CHECK(local.requestLocal(q.decisionId,q.stateRevision));
    CHECK(other.pending(resumed));
    CHECK(other.submit(mockExternalAgentAnswer(otherQuestion)));
    CHECK(RoomTestAccess::await(local,ignored) == ExternalAgentEndpoint::Local);
    CHECK(RoomTestAccess::await(other,ignored) == ExternalAgentEndpoint::Reply);
    RoomTestAccess::finish(local); RoomTestAccess::finish(other);
    // Concurrent local and external resolutions accept exactly one contender.
    for (int i = 0; i < 8; ++i) {
        ++q.decisionId; RoomTestAccess::begin(local,q);
        std::atomic_bool start{false};
        auto native = std::async(std::launch::async,[&] {
            while (!start.load()) std::this_thread::yield();
            return local.requestLocal(q.decisionId,q.stateRevision);
        });
        auto external = std::async(std::launch::async,[&] {
            while (!start.load()) std::this_thread::yield();
            return local.submit(mockExternalAgentAnswer(q));
        });
        start = true;
        const bool nativeAccepted = native.get(), externalAccepted = external.get();
        CHECK(nativeAccepted != externalAccepted);
        CHECK(RoomTestAccess::await(local,ignored)
            == (nativeAccepted ? ExternalAgentEndpoint::Local : ExternalAgentEndpoint::Reply));
        RoomTestAccess::finish(local);
        CHECK(local.status() == "idle" && local.lastError().isEmpty());
    }
    // A successful local choice clears an earlier authority rejection.
    ++q.decisionId; RoomTestAccess::begin(local,q);
    CHECK(local.submit(mockExternalAgentAnswer(q)));
    CHECK(RoomTestAccess::await(local,ignored) == ExternalAgentEndpoint::Reply);
    RoomTestAccess::reject(local,"illegal-action");
    CHECK(local.pending(resumed) && local.lastError() == "illegal-action");
    CHECK(local.requestLocal(q.decisionId,q.stateRevision));
    CHECK(local.lastError().isEmpty());
    CHECK(RoomTestAccess::await(local,ignored) == ExternalAgentEndpoint::Local);
    RoomTestAccess::finish(local);
    // Socket loss after accepting local cannot change that queued route.
    ++q.decisionId; RoomTestAccess::begin(local,q);
    CHECK(local.requestLocal(q.decisionId,q.stateRevision)); local.disconnect();
    CHECK(RoomTestAccess::await(local,ignored) == ExternalAgentEndpoint::Local);
    RoomTestAccess::finish(local); local.reconnect();
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
    qInfo() << "PASS mailbox: asynchronous wait, stale, duplicate, shape, reconnect, cancellation, explicit fallback, atomic local, concurrent routes, multiple seats";
}

static QJsonObject transportCall(QTcpSocket &client, const QJsonObject &message)
{
    CHECK(client.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n') > 0);
    QElapsedTimer clock; clock.start();
    while (!client.canReadLine()) {
        CHECK(clock.elapsed() < 2000);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        std::this_thread::yield();
    }
    QJsonParseError error;
    const auto reply = QJsonDocument::fromJson(client.readLine(), &error);
    CHECK(error.error == QJsonParseError::NoError && reply.isObject());
    return reply.object();
}

static void transportTests()
{
    auto endpoint = std::make_shared<ExternalAgentEndpoint>("seat",ExternalAgentEndpoint::Pause);
    auto other = std::make_shared<ExternalAgentEndpoint>("other",ExternalAgentEndpoint::Pause);
    ExternalAgentLocalTransport transport(endpoint), otherTransport(other);
    CHECK(transport.listen() && otherTransport.listen());
    QTcpSocket client, otherClient;
    const auto connect = [](QTcpSocket &socket, ExternalAgentLocalTransport &host) {
        const auto bootstrap = host.bootstrap();
        socket.connectToHost(bootstrap["host"].toString(),quint16(bootstrap["port"].toInt()));
        CHECK(socket.waitForConnected(2000));
        CHECK(transportCall(socket,{{"op","hello"},{"version",1},{"token",bootstrap["token"]}})["ok"].toBool());
    };
    connect(client,transport); connect(otherClient,otherTransport);
    auto q = question();
    auto second = q; second.viewerObjectName = "other"; ++second.decisionId;
    RoomTestAccess::begin(*endpoint,q); RoomTestAccess::begin(*other,second);
    QJsonObject command{{"op","local"},{"decisionId",QString::number(q.decisionId)},
                        {"stateRevision",QString::number(q.stateRevision)}};
    auto invalid = command; invalid["decisionId"] = 9;
    CHECK(transportCall(client,invalid)["error"] == "invalid-local");
    invalid = command; invalid["decisionId"] = "09";
    CHECK(transportCall(client,invalid)["error"] == "invalid-local");
    invalid = command; invalid["decisionId"] = "18446744073709551616";
    CHECK(transportCall(client,invalid)["error"] == "invalid-local");
    invalid = command; invalid.remove("stateRevision");
    CHECK(transportCall(client,invalid)["error"] == "invalid-local");
    invalid = command; invalid["seat"] = "other";
    CHECK(transportCall(client,invalid)["error"] == "unknown-operation");
    invalid = command; invalid["stateRevision"] = "16";
    CHECK(transportCall(client,invalid)["error"] == "stale");
    CHECK(transportCall(otherClient,command)["error"] == "stale");
    const auto accepted = transportCall(client,command);
    CHECK(accepted["ok"].toBool() && accepted["queued"].toBool() && accepted["route"] == "local");
    CHECK(transportCall(client,command)["error"] == "duplicate");
    const auto queued = transportCall(client,{{"op","poll"}});
    CHECK(queued["status"] == "local-queued" && !queued.contains("request"));
    CHECK(transportCall(otherClient,{{"op","poll"}})["request"].toObject()["viewerObjectName"] == "other");
    AIResult ignored;
    CHECK(RoomTestAccess::await(*endpoint,ignored) == ExternalAgentEndpoint::Local);
    RoomTestAccess::finish(*endpoint);
    CHECK(transportCall(client,{{"op","poll"}})["status"] == "idle");
    ++q.decisionId; ++q.decisionId; RoomTestAccess::begin(*endpoint,q);
    CHECK(transportCall(client,command)["error"] == "stale");
    const auto next = transportCall(client,{{"op","poll"}});
    CHECK(next["status"] == "waiting-no-clock");
    CHECK(next["request"].toObject()["decisionId"] == QString::number(q.decisionId));
    const QJsonObject answer{{"decisionId",QString::number(q.decisionId)},
        {"stateRevision",QString::number(q.stateRevision)},{"kind","answer"},
        {"action",QJsonObject{{"userString","yes"}}}};
    CHECK(transportCall(client,{{"op","submit"},{"result",answer}})["ok"].toBool());
    CHECK(RoomTestAccess::await(*endpoint,ignored) == ExternalAgentEndpoint::Reply);
    RoomTestAccess::finish(*endpoint);
    CHECK(other->requestLocal(second.decisionId,second.stateRevision));
    CHECK(RoomTestAccess::await(*other,ignored) == ExternalAgentEndpoint::Local);
    RoomTestAccess::finish(*other);
    qInfo() << "PASS transport: bounded local shape, stale and duplicate commands, independent seat capabilities, local then external";
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

static std::thread localNext(std::shared_ptr<ExternalAgentEndpoint> endpoint)
{
    return std::thread([endpoint] {
        QElapsedTimer clock; clock.start(); AIRequest q;
        while (!endpoint->pending(q)) {
            CHECK(clock.elapsed() < 10000);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        CHECK(endpoint->requestLocal(q.decisionId,q.stateRevision));
    });
}

class CountingAI : public TrustAI {
public:
    explicit CountingAI(ServerPlayer *p) : TrustAI(p) {}
    int calls = 0;
    int activateCalls = 0, useCalls = 0, responseCalls = 0;
    void activate(CardUseStruct &use) override { ++activateCalls; use.card = nullptr; }
    QString askForUseCard(const QString &, const QString &, const Card::HandlingMethod) override {
        ++useCalls; return ".";
    }
    const Card *askForCard(const QString &, const QString &, const QVariant &,
                          const Card::HandlingMethod) override {
        ++responseCalls; return nullptr;
    }
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
    auto *seatCounting = new CountingAI(p); p->setAI(seatCounting);
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
    // Explicit local resolves one callback, and the next prompt returns to external
    // without reconnecting. Exercise both Pause and disconnect-fallback seats.
    worker = localNext(endpoint);
    CHECK(RoomTestAccess::ai(room).decideChoice(p,"test","yes+no",QVariant(),answer));
    worker.join(); CHECK(answer == "no" && seatCounting->calls == 1);
    CHECK(endpoint->status() == "idle" && endpoint->lastError().isEmpty());
    worker = answerNext(endpoint);
    CHECK(RoomTestAccess::ai(room).decideChoice(p,"test","yes+no",QVariant(),answer));
    worker.join(); CHECK(answer == "yes" && seatCounting->calls == 1);
    for (const auto kind : {AIRequest::Activate, AIRequest::UseCard}) {
        auto action = RoomTestAccess::ai(room).makeRequest(p,kind,CardUseStruct::CARD_USE_REASON_PLAY,
                                                         QString(),QString(),Card::MethodUse);
        worker = localNext(endpoint);
        CHECK(RoomTestAccess::ai(room).decide(p,action,use));
        worker.join(); CHECK(!use.card);
        CHECK((kind == AIRequest::Activate ? seatCounting->activateCalls : seatCounting->useCalls) == 1);
        action = RoomTestAccess::ai(room).makeRequest(p,kind,CardUseStruct::CARD_USE_REASON_PLAY,
                                                    QString(),QString(),Card::MethodUse);
        worker = answerNext(endpoint);
        CHECK(RoomTestAccess::ai(room).decide(p,action,use));
        worker.join(); CHECK(!use.card);
        CHECK((kind == AIRequest::Activate ? seatCounting->activateCalls : seatCounting->useCalls) == 1);
    }
    worker = localNext(endpoint);
    CHECK(!RoomTestAccess::ai(room).decideResponseCard(p,"nullification","test",QVariant(),Card::MethodResponse));
    worker.join(); CHECK(seatCounting->responseCalls == 1);
    worker = answerNext(endpoint);
    CHECK(!RoomTestAccess::ai(room).decideResponseCard(p,"nullification","test",QVariant(),Card::MethodResponse));
    worker.join(); CHECK(seatCounting->responseCalls == 1);
    worker = localNext(otherEndpoint);
    CHECK(RoomTestAccess::ai(room).decideChoice(other,"test","yes+no",QVariant(),answer));
    worker.join(); CHECK(answer == "no" && counting->calls == 1);
    CHECK(otherEndpoint->status() == "idle" && otherEndpoint->lastError().isEmpty());
    worker = answerNext(otherEndpoint);
    CHECK(RoomTestAccess::ai(room).decideChoice(other,"test","yes+no",QVariant(),answer));
    worker.join(); CHECK(answer == "yes" && counting->calls == 1);
    // Explicit fallback invokes the configured seat AI exactly once, and reconnect returns to external.
    otherEndpoint->disconnect();
    CHECK(RoomTestAccess::ai(room).decideChoice(other,"test","yes+no",QVariant(),answer));
    CHECK(answer == "no" && counting->calls == 2);
    otherEndpoint->reconnect();
    worker = answerNext(otherEndpoint);
    CHECK(RoomTestAccess::ai(room).decideChoice(other,"test","yes+no",QVariant(),answer));
    worker.join(); CHECK(answer == "yes" && counting->calls == 2);
    // Opt-in bounded room path: a submitted answer for an obsolete world is
    // discarded, current native SmartAI runs once, and the room stays usable.
    otherEndpoint->enableBoundedHybrid(1000);
    AIChoiceOptions staleOptions;
    staleOptions.reason = QStringLiteral("test");
    staleOptions.choices << QStringLiteral("yes") << QStringLiteral("no");
    auto staleChoice = RoomTestAccess::ai(room).makeChoiceRequest(other, AIRequest::Choice,
                                                                 staleOptions);
    staleChoice.stateRevision = 0;
    AIResult staleResult;
    int staleNativeCalls = 0;
    worker = answerNext(otherEndpoint);
    CHECK(RoomTestAccess::runNativeChoice(room, other, staleChoice, staleResult,
                                         staleNativeCalls));
    worker.join();
    CHECK(staleNativeCalls == 1 && staleResult.action.userString == "no");
    CHECK(staleResult.stateRevision == room.roomRuntime()->stateRevision());
    CHECK(!otherEndpoint->submit(mockExternalAgentAnswer(staleChoice)));
    CHECK(otherEndpoint->status() == "smart-ai-fallback");
    otherEndpoint->reconnect();
    auto illegalHybrid = RoomTestAccess::ai(room).makeRequest(other, AIRequest::Activate,
        CardUseStruct::CARD_USE_REASON_PLAY, QString(), QString(), Card::MethodUse);
    worker = std::thread([&] {
        QElapsedTimer clock; clock.start(); AIRequest q;
        while (!otherEndpoint->pending(q)) {
            CHECK(clock.elapsed() < 10000);
            std::this_thread::yield();
        }
        auto r = mockExternalAgentAnswer(q);
        r.kind = AIResult::UseCard;
        r.action.useCardId = jink; // belongs to the other actor
        CHECK(otherEndpoint->submit(r));
    });
    const int beforeHybridNative = counting->activateCalls;
    CHECK(RoomTestAccess::ai(room).decide(other, illegalHybrid, use));
    worker.join();
    CHECK(!use.card && counting->activateCalls == beforeHybridNative + 1);
    CHECK(otherEndpoint->status() == "smart-ai-fallback");
    auto race = std::async(std::launch::async, [&] { return RoomTestAccess::waitRace(room); });
    CHECK(race.wait_for(std::chrono::milliseconds(150)) == std::future_status::timeout);
    // An authoritative revision change never restamps an old result.
    play.stateRevision = 0;
    worker = localNext(endpoint);
    bool staleCancelled = false;
    try { RoomTestAccess::ai(room).decide(p, play, use); }
    catch (TriggerEvent e) { staleCancelled = e == GameFinished; }
    worker.join(); CHECK(staleCancelled); CHECK(endpoint->lastError() == "stale-world");
    CHECK(seatCounting->activateCalls == 1); // stale local never reaches the native callback
    CHECK(race.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    CHECK(!race.get());
    // Cancellation wakes an indefinite native request wait and external decision.
    auto aborted = std::async(std::launch::async, [&] { return RoomTestAccess::wait(room,p,-1); });
    room.abortWaitingRequests();
    CHECK(aborted.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    CHECK(endpoint->status() == "cancelled");
    qInfo() << "PASS room: hidden information, seat isolation, strict choices, Jink/Peach/Nullification, no-clock wake/abort";
}

// Offline target-enumeration fixtures. The cards and rule skills below are only
// registered in this synthetic room; no game workers or provider requests run.
class Target50Prohibit : public ProhibitSkill {
public:
    Target50Prohibit() : ProhibitSkill("#target50_prohibit") {}
    bool isProhibited(const Player *from, const Player *to, const Card *,
                      const QList<const Player *> &) const override {
        return from && to && (to->hasFlag("Target50Blocked")
            || (from->hasFlag("Target50OnlyThree") && (to->getSeat() < 2 || to->getSeat() > 4)));
    }
};

class Target50GlobalExtra : public TargetModSkill {
public:
    Target50GlobalExtra() : TargetModSkill("target50_global_extra") {}
    int getExtraTargetNum(const Player *from, const Card *) const override {
        return from && from->hasFlag("Target50GlobalExtra") ? 1 : 0;
    }
};

class Target50CandidateExtra : public TargetModSkillV2 {
public:
    Target50CandidateExtra() : TargetModSkillV2("target50_candidate_extra") {}
    CorrectSkillResult getCorrection(const CorrectSkillContext &context) const override {
        if (context.modType == TargetModSkill::ExtraTarget && context.secondary && onSecondaryQuery) {
            auto action = std::move(onSecondaryQuery);
            onSecondaryQuery = {};
            action();
        }
        if (context.modType == TargetModSkill::ExtraTarget && context.primary
            && context.primary->hasFlag("Target50CandidateExtra") && context.secondary
            && context.secondary->getSeat() == 4)
            return CorrectSkillResult::useAmount(1);
        return CorrectSkillResult::noEffect();
    }
    mutable std::function<void()> onSecondaryQuery;
};

class Target50Card : public BasicCard {
public:
    enum Shape { OrderedPairs, PrefixDependent, Overflow, RepeatedVotes };
    explicit Target50Card(Shape shape) : BasicCard(Card::Spade, 7), shape(shape) {
        setObjectName("target50_fixture");
    }
    QString getSubtype() const override { return "target50_fixture"; }
    bool isAvailable(const Player *) const override { return true; }
    bool targetsFeasible(const QList<const Player *> &targets, const Player *) const override {
        ++feasibleCalls;
        return targets.size() == (shape == RepeatedVotes ? 1 : 2);
    }
    bool targetFilter(const QList<const Player *> &targets, const Player *to,
                      const Player *from, int &votes) const override {
        ++filterCalls; votes = 0;
        if (to == from || targets.size() >= 2) return false;
        bool allowed = false;
        if (shape == Overflow || shape == RepeatedVotes) allowed = !targets.contains(to);
        else if (targets.isEmpty()) allowed = to->getSeat() == 2 || to->getSeat() == 3;
        else if (shape == OrderedPairs) allowed = to->getSeat() == 5 || to->getSeat() == 6;
        else allowed = to->getSeat() == (targets.first()->getSeat() == 2 ? 5 : 6);
        if (allowed) votes = shape == RepeatedVotes ? 2 : 1;
        return allowed;
    }
    mutable int filterCalls = 0, feasibleCalls = 0;
    Shape shape;
};

// This subclass intentionally shares Slash's QObject metadata and printed name,
// but permits two targets. An optimization based only on name/metaObject is unsafe.
class Target50ExtendedSlash : public Slash {
public:
    Target50ExtendedSlash() : Slash(Card::Spade, 7) {}
    bool isAvailable(const Player *) const override { return true; }
    bool targetsFeasible(const QList<const Player *> &targets, const Player *) const override {
        return targets.size() == 2;
    }
    bool targetFilter(const QList<const Player *> &targets, const Player *to,
                      const Player *from) const override {
        return targets.size() < 2 && to != from && !targets.contains(to);
    }
};

static QString target50Hash(const QList<QStringList> &combinations)
{
    QStringList keys;
    for (const auto &targets : combinations) keys << targets.join(QChar(0x1f));
    keys.sort();
    return QString::fromLatin1(QCryptographicHash::hash(keys.join(QChar(0x1e)).toUtf8(),
                                                       QCryptographicHash::Sha256).toHex());
}

static QList<QStringList> target50Oracle(Room &room, ServerPlayer *from, const Card *card, int maxDepth)
{
    QList<QStringList> result;
    QList<const Player *> prefix;
    QStringList names;
    std::function<void()> visit = [&] {
        if (card->targetsFeasible(prefix,from)) result << names;
        if (prefix.size() == maxDepth) return;
        for (auto *target : room.getAlivePlayers()) {
            if (prefix.contains(target)) continue;
            int votes = 0;
            card->targetFilter(prefix,target,from,votes);
            CHECK(votes <= 1); // this oracle intentionally excludes unsupported repeated votes
            if (!votes || room.isProhibited(from,target,card,prefix)) continue;
            prefix << target; names << target->objectName();
            visit();
            prefix.removeLast(); names.removeLast();
        }
    };
    visit();
    return result;
}

static int target50Benchmark(QCoreApplication &app)
{
    const bool requireComplete = app.arguments().contains("--target50-require-complete");
    Room room(nullptr,"50p",GameSessionConfig(20261005));
    EngineRuntimeContextScope scope(*Sanguosha,&room);
    LuaRuntime::Binding binding(room.roomRuntime()->lua());
    room.roomRuntime()->state().reset();
    RoomTestAccess::attachThread(room);
    QList<ServerPlayer *> seats;
    for (int i = 1; i <= 50; ++i) {
        auto *seat = addSeat(room,QString("target50_%1").arg(i),i == 1 ? "lord" : "rebel");
        seat->setAI(new TrustAI(seat)); seats << seat;
    }
    auto *from = seats.first();
    from->setFlags("InfinityAttackRange");
    auto *candidateExtra = new Target50CandidateExtra;
    Sanguosha->addSkills({new Target50Prohibit, new Target50GlobalExtra, candidateExtra});
    from->acquireSkill("target50_global_extra"); from->acquireSkill("target50_candidate_extra");
    room.setCurrentCardUse(QString(),CardUseStruct::CARD_USE_REASON_PLAY);
    int cardId = -1;
    for (int id : room.getDrawPile()) {
        if (Sanguosha->getCard(id)->objectName() == "slash") { cardId = id; break; }
    }
    CHECK(cardId >= 0);
    room.moveCardTo(Sanguosha->getCard(cardId),from,Player::PlaceHand,false);
    auto *wrapped = dynamic_cast<WrappedCard *>(room.getCard(cardId)); CHECK(wrapped);
    const QVariantMap originalOverrides = Config.valueOverrides();
    QJsonArray rows;
    int repeatCount = 7;
    const auto args = app.arguments();
    const int repeatAt = args.indexOf("--target50-repeats");
    if (repeatAt >= 0 && repeatAt + 1 < args.size()) {
        bool ok = false; repeatCount = args.at(repeatAt+1).toInt(&ok);
        CHECK(ok && repeatCount >= 1 && repeatCount <= 100);
    }
    const auto run = [&](const QString &name, int maxDepth, bool overflowExpected = false,
                         bool repeatedVotes = false, bool mustComplete = false) {
        const Card *card = wrapped;
        const auto expected = repeatedVotes ? QList<QStringList>() : target50Oracle(room,from,card,maxDepth);
        QJsonArray timings;
        AICardCandidateView candidate;
        for (int i = -2; i < repeatCount; ++i) {
            QElapsedTimer timer; timer.start();
            const auto request = RoomTestAccess::ai(room).makeRequest(from,AIRequest::Activate,
                CardUseStruct::CARD_USE_REASON_PLAY,QString(),QString(),Card::MethodUse);
            const auto elapsed = timer.nsecsElapsed();
            CHECK(request.cardCandidates.size() == 1);
            candidate = request.cardCandidates.first();
            CHECK(candidate.cardId == cardId && candidate.available && !candidate.limited);
            if (candidate.completeCoverage) {
                CHECK(!overflowExpected && !repeatedVotes);
                CHECK(candidate.targetCombinations.size() == expected.size());
                CHECK(target50Hash(candidate.targetCombinations) == target50Hash(expected));
            } else CHECK(candidate.targetCombinations.isEmpty());
            if (mustComplete || (requireComplete && name == "native_slash_49")) CHECK(candidate.completeCoverage);
            if (overflowExpected || repeatedVotes) CHECK(!candidate.completeCoverage);
            if (i >= 0) timings << QString::number(elapsed);
        }
        QJsonObject row{{"case",name},{"complete",candidate.completeCoverage},
            {"native_combinations",candidate.targetCombinations.size()},
            {"oracle_combinations",expected.size()},{"oracle_sha256",target50Hash(expected)},
            {"native_sha256",target50Hash(candidate.targetCombinations)},
            {"first_targets",candidate.legalTargets.size()},{"request_ns",timings},
            {"overflow_expected",overflowExpected},{"repeated_votes",repeatedVotes}};
        if (const auto *fixture = dynamic_cast<const Target50Card *>(wrapped->getRealCard())) {
            row["fixture_filter_calls_total"] = fixture->filterCalls;
            row["fixture_feasible_calls_total"] = fixture->feasibleCalls;
        }
        rows << row;
        qInfo().noquote() << "TARGET50_CASE" << QJsonDocument(row).toJson(QJsonDocument::Compact);
    };
    run("native_slash_49",1); // baseline may be conservatively incomplete
    from->setFlags("Target50OnlyThree");
    run("native_slash_three",1,false,false,true);
    seats.at(2)->setFlags("Target50Blocked");
    run("native_slash_changed_prohibition",1,false,false,true);
    seats.at(2)->setFlags("-Target50Blocked");
    from->setFlags("Target50GlobalExtra");
    run("native_slash_global_extra",2,false,false,true);
    from->setFlags("-Target50GlobalExtra"); from->setFlags("Target50CandidateExtra");
    run("native_slash_candidate_extra",2,false,false,true);
    from->setFlags("-Target50CandidateExtra"); from->setFlags("-Target50OnlyThree");
    wrapped->takeOver(new Target50Card(Target50Card::OrderedPairs));
    run("custom_ordered_pairs",2,false,false,true);
    seats.at(5)->setFlags("Target50Blocked");
    run("custom_changed_prohibition",2,false,false,true);
    seats.at(5)->setFlags("-Target50Blocked");
    wrapped->takeOver(new Target50Card(Target50Card::PrefixDependent));
    run("custom_prefix_dependent",2,false,false,true);
    wrapped->takeOver(new Target50Card(Target50Card::RepeatedVotes));
    run("custom_repeated_votes",1,false,true);
    wrapped->takeOver(new Target50Card(Target50Card::Overflow));
    run("custom_combination_overflow",2,true);
    wrapped->takeOver(new Target50ExtendedSlash);
    run("slash_subclass_overflow",2,true);
    room.resetCard(cardId);
    QVariantMap limitedOverrides = originalOverrides;
    limitedOverrides.insert("AiTargetProjectionBudget",4); Config.setValueOverrides(limitedOverrides);
    run("native_probe_overflow",1,true);
    Config.setValueOverrides(originalOverrides);
    // Extra correctness checks outside the unchanged before/after timing cases.
    // Availability already queried the global modifier. Arm only a secondary
    // query, which the new bound performs after capturing enumeration state.
    // Persistent flag mutations need an explicit guard: flags need not bump revision.
    for (int mode = 0; mode < 3; ++mode) {
        bool mutationObserved = false;
        candidateExtra->onSecondaryQuery = [&, mode] {
            mutationObserved = true;
            if (mode == 0) from->setFlags("Target50DuringBound");
            else if (mode == 1)
                room.setPlayerMark(from,"target50_mutation",from->getMark("target50_mutation")+1);
            else Sanguosha->addSkills({new TargetModSkill("target50_registered_during_bound")});
        };
        const auto definitionsBefore = room.roomRuntime()->definitions().skillDefinitionVersion();
        const auto request = RoomTestAccess::ai(room).makeRequest(from,AIRequest::Activate,
            CardUseStruct::CARD_USE_REASON_PLAY,QString(),QString(),Card::MethodUse);
        CHECK(request.cardCandidates.size() == 1);
        CHECK(!request.cardCandidates.first().completeCoverage);
        CHECK(request.cardCandidates.first().targetCombinations.isEmpty());
        CHECK(mutationObserved && !candidateExtra->onSecondaryQuery);
        if (mode == 2) CHECK(room.roomRuntime()->definitions().skillDefinitionVersion() > definitionsBefore);
        from->setFlags("-Target50DuringBound");
    }
    qInfo() << "PASS target50 mutation guard: ExtraTarget callback flag, revision and definition changes leave coverage incomplete";
    const QJsonObject report{{"schema",1},{"fixture","native_target50"},{"seats",50},
        {"seed","20261005"},{"provider_requests",0},{"cases",rows},
        {"timing_scope","whole native makeRequest; target legality oracle outside timing"}};
    qInfo().noquote() << "TARGET50_REPORT" << QJsonDocument(report).toJson(QJsonDocument::Compact);
    return 0;
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

static int hybridSetupContract()
{
    // The test subprocess deliberately has no runtime secret, so signup can
    // prove missing setup continues with native AI without any paid request.
    CHECK(qEnvironmentVariableIsEmpty("TYPESAFE_API_KEY"));
    CHECK(Sanguosha->getAvailableModes().contains(QStringLiteral("50p")));
    CHECK(!useJevHybrid50("02p", true, true));
    CHECK(!useJevHybrid50("50p", false, true));
    CHECK(!useJevHybrid50("50p", true, false));
    CHECK(!useJevHybrid50("50p", true, true, true));
    CHECK(useJevHybrid50("50p", true, true));
    Config.BanPackages.clear();
    Config.setValueOverrides({{"JevHybrid50P", true}});
    GameSessionConfig session(20261005);
    const auto preservedId = session.hybridGameId;
    Room room(nullptr, "50p", session);
    CHECK(room.hasLuaRuntime());
    CHECK(RoomTestAccess::hybridSelected(room));
    CHECK(RoomTestAccess::hybridGameId(room) == preservedId);
    auto *robot = room.addAIPlayer();
    room.signup(robot, QStringLiteral("OfflineRobot"), QString(), true);
    CHECK(!room.externalAgent(robot->objectName()));
    CHECK(robot->getState() == "robot");
    Config.setValueOverrides({{"JevHybrid50P", false}});
    CHECK(RoomTestAccess::hybridSelected(room)); // saved room choice survives toggles
    Room ordinary(nullptr, "50p", GameSessionConfig(20261006));
    CHECK(!RoomTestAccess::hybridSelected(ordinary));
    Config.setValueOverrides({{"JevHybrid50P", true}});
    Room otherMode(nullptr, "02p", GameSessionConfig(20261007));
    CHECK(!RoomTestAccess::hybridSelected(otherMode));
    qInfo() << "PASS selectable 50p room gate, stable game id, no-secret native fallback";
    return 0;
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
    transportTests();
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
    Config.GameMode = Sanguosha->getGameMode(app.arguments().contains("--target50-benchmark") ? "50p" : "02p");
    // A sufficient, explicit native general pool keeps this a boundary test.
    Config.BanPackages.clear();
    const QStringList packages = {"standard", "standard_cards", "standard_ex_cards", "maneuvering"};
    for (const auto *package : Sanguosha->getPackages())
        if (!packages.contains(package->objectName())) Config.BanPackages << package->objectName();
    Config.AIDelay = Config.OriginAIDelay = 0;
    int result = 0;
    if (app.arguments().contains("--target50-benchmark")) result = target50Benchmark(app);
    else if (app.arguments().contains("--hybrid-setup-contract")) result = hybridSetupContract();
    else if (app.arguments().contains("--game") || app.arguments().contains("--cancel-game")) result = gameTest(app);
    else if (app.arguments().contains("--lua-corrections"))
        result = correctionCompatibility(app.arguments().last()) ? 0 : 1;
    else roomTests();
    EngineBootstrap::shutdown();
    return result;
}
