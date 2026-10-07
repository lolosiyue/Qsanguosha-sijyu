#include "room-managed-state.h"
#include "room.h"
#include "serverplayer.h"
#include "socket.h"
#include "engine.h"
#include "engine-bootstrap.h"
#include "runtime-paths.h"
#include "settings.h"
#include <QCoreApplication>
#include <QDebug>
#include <lua.hpp>
#include <stdexcept>
#include <thread>

namespace {
int checks = 0;
QString error;
#define CHECK(x) do { ++checks; if (!(x)) qFatal("CHECK failed at %s:%d: %s (%s)", __FILE__, __LINE__, #x, qPrintable(error)); } while (false)

class RecordingSocket final : public ClientSocket {
public:
    int sent = 0;
    bool connected = true;
    QByteArray last;
    void connectToHost() override { connected = true; }
    void send(const QByteArray &message) override { ++sent; last = message; }
    bool isConnected() const override { return connected; }
    QString peerName() const override { return QStringLiteral("managed-fixture"); }
    QString peerAddress() const override { return QStringLiteral("127.0.0.1"); }
    void disconnectFromHost() override { connected = false; }
};

bool run(LuaRuntime &runtime, const QString &script)
{
    LuaRuntime::Binding binding(runtime, false);
    LuaRuntime::LuaInvocationScope invocation(runtime);
    auto *L = runtime.state();
    const int top = lua_gettop(L);
    const QByteArray bytes = script.toUtf8();
    bool ok = luaL_loadbuffer(L, bytes.constData(), size_t(bytes.size()), "managed-live-test") == LUA_OK;
    if (ok) ok = LuaRuntime::protectedCall(L, 0, 0, 0) == LUA_OK;
    if (!ok) error = QString::fromUtf8(lua_tostring(L, -1));
    lua_settop(L, top);
    return ok;
}

struct Faults { bool reject = false; bool throws = false; bool initialMutation = false; bool aiThrows = false; };
GameState::ProviderRegistry registry(const std::shared_ptr<Faults> &faults)
{
    GameState::ProviderRegistry result;
    GameState::ProviderContract skill;
    skill.id = "fixture.skill"; skill.version = 1; skill.skillDefinitions = {"fixture.guard"};
    skill.audit = "Fixture guard has immutable code; all persistent counters and targets are explicit managed values.";
    skill.prepare = [faults](GameState::WorldState &state, QString *) {
        if (faults->initialMutation) state.players["p1"].hp = 1;
        if (faults->throws || faults->reject) {
            state.players["p1"].hp = 0;
            state.providers["fixture.skill"].state["partial"] = true;
            if (faults->throws) throw std::runtime_error("failed after detached changes");
            return false;
        }
        return true;
    };
    CHECK(result.registerProvider(skill, &error));
    GameState::ProviderContract ai;
    ai.id = "fixture.ai"; ai.version = 1;
    ai.audit = "Fixture AI remembers only managed confidence and target; no closure/upvalue state.";
    ai.prepare = [faults](GameState::WorldState &state, QString *) {
        if (faults->aiThrows) {
            state.providers["fixture.ai"].state["partial"] = true;
            throw std::runtime_error("AI failed after detached changes");
        }
        return true;
    };
    CHECK(result.registerProvider(ai, &error));
    return result;
}

void demonstration()
{
    auto faults = std::make_shared<Faults>();
    const auto contract = registry(faults);
    Room room(nullptr, "02p", GameSessionConfig(1234), Room::RuntimeInitializationPolicy::Deferred);
    EngineRuntimeContextScope engineScope(*Sanguosha, &room);
    auto &game = room.roomRuntime()->lua();
    auto &ai = room.roomRuntime()->ai().lua();
    CHECK(game.initialize(&error) && ai.initialize(&error));
    auto *socket = new RecordingSocket;
    auto *p1 = room.addSocket(socket); p1->setObjectName("p1");
    auto *p2 = room.addAIPlayer(); p2->setObjectName("p2");
    for (auto *player : {p1, p2}) { player->setMaxHp(4); player->setHp(4); }
    p1->setHp(3); p1->setMark("@HuJia", 2); p1->setMark("fixture_guard", 1);
    p1->setTag("target", GameState::reference("player", "p2"));
    room.setTag("fixture", QStringLiteral("before"));
    const int skill = p1->createSkillInstance("fixture.guard", SourceAcquired);
    p1->setSkillInstanceState("fixture.guard", skill, {{"uses", 1}});
    CHECK(p1->setSkillInstanceCorrectStateValue("fixture.guard", skill, "amount", 2));
    const QString stableSkill = RoomManagedState::skillId("p1", "fixture.guard", skill);
    CHECK(stableSkill != RoomManagedState::skillId("p", "1fixture.guard", skill));
    auto &managed = room.managedState();
    faults->initialMutation = true;
    CHECK(!managed.initialize(contract, &error));
    CHECK(!managed.worldStore() && p1->getHp() == 3); // No split native/provider root on enrollment failure.
    faults->initialMutation = false;
    CHECK(managed.initialize(contract, &error));
    CHECK(managed.installLuaProvider(game, "fixture.skill", &error));
    CHECK(managed.installLuaProvider(ai, "fixture.ai", &error));
    const auto beforeBadInstall = managed.worldStore()->capture();
    CHECK(!managed.installLuaProvider(game, "missing-provider", &error));
    CHECK(managed.worldStore()->capture() == beforeBadInstall);
    CHECK(run(game, QString(R"(
        assert(sgs.ManagedState.set('skill', '%1', 'uses', 4))
        assert(sgs.ManagedState.set('player', 'p1', 'target', {['$ref']={kind='player',id='p2'}}))
        function use_fixture_guard()
            local n = assert(sgs.ManagedState.get('skill', '%1', 'uses'))
            assert(sgs.ManagedState.set('skill', '%1', 'uses', n + 1))
        end
    )").arg(stableSkill)));
    CHECK(run(ai, "assert(sgs.ManagedState.set('player','p1','confidence',0.75))"));
    CHECK(run(game, "assert(sgs.ManagedState.set('player','p1','temporary',7)); assert(sgs.ManagedState.get('player','p1','temporary')==7); assert(sgs.ManagedState.remove('player','p1','temporary')); assert(sgs.ManagedState.get('player','p1','temporary')==nil)"));
    CHECK(run(ai, "assert(sgs.ManagedState.set('player','p2','temporary',8)); assert(sgs.ManagedState.remove('player','p2','temporary')); assert(sgs.ManagedState.get('player','p2','temporary')==nil)"));
    CHECK(run(game, "local t={n=1}; local ok,err=sgs.ManagedState.set('player','p1','bad_alias',{a=t,b=t}); assert(not ok and err)"));
    CHECK(run(ai, "local ok,err=sgs.ManagedState.set('player','p1','bad_meta',setmetatable({},{__index={}})); assert(not ok and err)"));
    CHECK(run(game, R"(
        local api = sgs.ManagedState
        assert(api.set('player','p1','empty_array',api.array()))
        assert(api.set('player','p1','empty_object',{}))
        assert(api.set('player','p1','array_roundtrip',api.get('player','p1','empty_array')))
        local ok,err = api.set('player','p1','invalid_utf8',string.char(255))
        assert(not ok and err)
    )"));
    const auto typedPayload = managed.worldStore()->state().providers.value("fixture.skill").state
        .value("owners").toMap().value("player").toMap().value("p1").toMap();
    CHECK(typedPayload.value("array_roundtrip").userType() == QMetaType::QVariantList);
    CHECK(typedPayload.value("empty_object").userType() == QMetaType::QVariantMap);
    CHECK(!typedPayload.contains("invalid_utf8"));
    const QString anchor = managed.checkpoint("before-guard", &error);
    CHECK(!anchor.isEmpty());
    CHECK(managed.worldStore()->timeline().anchors().last().kind == GameTimeline::AnchorKind::ManagedSetup);
    CHECK(managed.worldStore()->timeline().previousPlayerTurn().id.isEmpty());
    CHECK(managed.worldStore()->timeline().previousFullRound().id.isEmpty());
    const auto root = managed.worldStore()->timeline().rootGameId();
    auto *runtimeIdentity = room.roomRuntime();
    auto *gameVm = game.rawState(); auto *aiVm = ai.rawState();

    p1->setHp(1); p1->setMaxHp(3); p1->setMark("@HuJia", 0); p1->setMark("fixture_guard", 7);
    p1->setTag("target", GameState::reference("player", "p1")); p1->setPhase(Player::Play);
    p1->setSkillInstanceState("fixture.guard", skill, {{"uses", 9}});
    CHECK(p1->setSkillInstanceCorrectStateValue("fixture.guard", skill, "amount", 8));
    room.setTag("fixture", QStringLiteral("after"));
    CHECK(run(game, "use_fixture_guard()"));
    CHECK(run(ai, "assert(sgs.ManagedState.set('player','p1','confidence',0.1))"));
    const auto beforeFailure = managed.worldStore()->capture();
    const auto beforeRevision = managed.worldStore()->revision();
    CHECK(managed.checkpoint("before-guard", &error).isEmpty());
    CHECK(managed.worldStore()->capture() == beforeFailure && managed.worldStore()->revision() == beforeRevision);

    faults->throws = true;
    CHECK(!managed.prepareRestore(anchor, &error));
    CHECK(managed.worldStore()->capture() == beforeFailure && p1->getHp() == 1);
    faults->throws = false; faults->reject = true;
    CHECK(!managed.prepareRestore(anchor, &error));
    CHECK(managed.worldStore()->capture() == beforeFailure && p1->getHujia() == 0);
    faults->reject = false;
    faults->aiThrows = true;
    CHECK(!managed.prepareRestore(anchor, &error));
    CHECK(managed.worldStore()->capture() == beforeFailure && p1->getHp() == 1);
    CHECK(run(game, QString("assert(sgs.ManagedState.get('skill','%1','uses')==5)").arg(stableSkill)));
    CHECK(run(ai, "assert(sgs.ManagedState.get('player','p1','confidence')==0.1)"));
    faults->aiThrows = false;
    auto candidate = managed.prepareRestore(anchor, &error);
    CHECK(candidate && p1->getHp() == 1);
    p1->setMark("fixture_guard", 8);
    CHECK(!managed.publish(std::move(*candidate), &error));
    CHECK(managed.worldStore()->timeline().generation() == 0 && p1->getMark("fixture_guard") == 8);
    candidate = managed.prepareRestore(anchor, &error);
    CHECK(candidate);
    CHECK(run(ai, "assert(sgs.ManagedState.set('player','p1','confidence',0.2))"));
    CHECK(!managed.publish(std::move(*candidate), &error));
    CHECK(managed.worldStore()->timeline().generation() == 0 && p1->getHp() == 1);

    candidate = managed.prepareRestore(anchor, &error);
    CHECK(candidate);
    int nativeSignals = 0;
    QObject::connect(p1, &Player::hp_changed, &room, [&] { ++nativeSignals; });
    QObject::connect(p1, &Player::mark_changed, &room, [&] { ++nativeSignals; });
    QObject::connect(p1, &Player::skill_state_changed, &room, [&] { ++nativeSignals; });
    const int sentBefore = socket->sent;
    CHECK(managed.publish(std::move(*candidate), &error));
    CHECK(!managed.publish(std::move(*candidate), &error));
    CHECK(nativeSignals == 0 && socket->sent == sentBefore);
    CHECK(room.getPlayers()[0] == p1 && room.getPlayers()[1] == p2 && p1->getRoom() == &room);
    CHECK(room.roomRuntime() == runtimeIdentity && game.rawState() == gameVm && ai.rawState() == aiVm);
    CHECK(p1->getHp() == 3 && p1->getMaxHp() == 4 && p1->getHujia() == 2);
    CHECK(p1->getMark("fixture_guard") == 1 && p1->getPhase() == Player::NotActive);
    CHECK(p1->getTag("target") == QVariant(GameState::reference("player", "p2")));
    CHECK(room.getTag("fixture").toString() == "before");
    CHECK(p1->getSkillInstanceStateValue("fixture.guard", skill, "uses").toInt() == 1);
    CHECK(p1->getSkillInstanceCorrectStateValue("fixture.guard", skill, "amount").toInt() == 2);
    CHECK(run(game, QString("assert(sgs.ManagedState.get('skill','%1','uses') == 4)").arg(stableSkill)));
    CHECK(run(ai, "assert(sgs.ManagedState.get('player','p1','confidence') == 0.75)"));
    CHECK(managed.worldStore()->timeline().generation() == 1 && managed.worldStore()->timeline().rootGameId() == root);
    p1->message_ready(QByteArray("same-socket-after-restore"));
    CHECK(socket->last == "same-socket-after-restore" && socket->isConnected());

    CHECK(run(game, "use_fixture_guard()"));
    candidate = managed.prepareRestore(anchor, &error);
    CHECK(candidate && managed.publish(std::move(*candidate), &error));
    CHECK(managed.worldStore()->timeline().generation() == 2);
    CHECK(run(game, QString("assert(sgs.ManagedState.get('skill','%1','uses') == 4)").arg(stableSkill)));
    const auto immutable = managed.worldStore()->capture();
    CHECK(run(game, "local ok,err=sgs.ManagedState.set('player','p1','bad',{['$ref']={kind='player',id='missing'}}); assert(not ok and err)"));
    CHECK(managed.worldStore()->capture() == immutable);
    room.setTag("unmanaged_native", QVariant::fromValue(static_cast<QObject *>(p1)));
    CHECK(room.getTag("unmanaged_native").userType() == QMetaType::QObjectStar);
    CHECK(!managed.prepareRestore(anchor, &error));
    CHECK(managed.worldStore()->capture() == immutable && p1->getHp() == 3);
    room.removeTag("unmanaged_native");
    {
        Room second(nullptr, "02p", GameSessionConfig(1234), Room::RuntimeInitializationPolicy::Deferred);
        EngineRuntimeContextScope otherScope(*Sanguosha, &second);
        auto &otherGame = second.roomRuntime()->lua(); auto &otherAi = second.roomRuntime()->ai().lua();
        CHECK(otherGame.initialize(&error) && otherAi.initialize(&error));
        for (const auto &id : {QStringLiteral("p1"), QStringLiteral("p2")}) {
            auto *player = second.addAIPlayer(); player->setObjectName(id); player->setMaxHp(4); player->setHp(4);
        }
        auto &other = second.managedState();
        CHECK(other.initialize(contract, &error));
        CHECK(other.installLuaProvider(otherGame, "fixture.skill", &error));
        CHECK(other.installLuaProvider(otherAi, "fixture.ai", &error));
        CHECK(run(otherGame, "assert(sgs.ManagedState.get('player','p1','target')==nil); assert(sgs.ManagedState.set('player','p1','only_other',42))"));
        CHECK(run(otherAi, "assert(sgs.ManagedState.set('player','p1','confidence',0.33))"));
        candidate = managed.prepareRestore(anchor, &error);
        CHECK(candidate && !other.publish(std::move(*candidate), &error));
        CHECK(other.worldStore()->timeline().generation() == 0 && second.getPlayers()[0]->getHp() == 4);
        CHECK(run(game, "assert(sgs.ManagedState.get('player','p1','only_other')==nil)"));
        CHECK(run(ai, "assert(sgs.ManagedState.get('player','p1','confidence')==0.75)"));
        CHECK(run(otherAi, "assert(sgs.ManagedState.get('player','p1','confidence')==0.33)"));
    }
    bool wrongThreadRejected = false;
    std::thread wrong([&] { QString why; wrongThreadRejected = !managed.prepareRestore(anchor, &why); });
    wrong.join(); CHECK(wrongThreadRejected);
    { LuaRuntime::Binding lock(game, false); LuaRuntime::LuaInvocationScope invocation(game);
      CHECK(!managed.prepareRestore(anchor, &error)); }
    room.roomRuntime()->rng().generate();
    CHECK(!managed.prepareRestore(anchor, &error)); // Out-of-slice RNG change cannot silently diverge.
    CHECK(managed.worldStore()->timeline().generation() == 2);
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    CHECK(QSanRuntimePaths::resolve(app.arguments(), &error));
    CHECK(EngineBootstrap::initialize(false, &error));
    QObject::disconnect(&app, SIGNAL(aboutToQuit()), Sanguosha, SLOT(deleteLater()));
    Config.init(); Config.EnableAI = false; Config.Enable2ndGeneral = false; Config.EnableHegemony = false;
    demonstration();
    EngineBootstrap::shutdown();
    qInfo() << "room-managed-state:" << checks << "live-engine checks passed";
    return 0;
}
