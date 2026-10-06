#include "ai-decision-coordinator.h"
#include "engine.h"
#include "external-agent.h"
#include "external-agent-transport.h"
#include "room.h"
#include "room-roster.h"
#include "room-runtime.h"
#include "runtime-paths.h"
#include "serverplayer.h"
#include "settings.h"
#include "skill-set-generation.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <lua.hpp>
#include <cstdio>
#include <cstdlib>
#include <functional>

#define CHECK(x) do { if (!(x)) qFatal("CHECK failed at %s:%d: %s", __FILE__, __LINE__, #x); } while (false)

static void phase(const char *name)
{
    fprintf(stderr, "PROJECTION_PHASE %s\n", name);
    fflush(stderr);
}

// This is a bounded native Room fixture, not a game or provider replay. It exercises
// real projection, native rules hooks and a deterministic room-owned Lua callback.
// Its timings do not reproduce the earlier 224-second live observation.
struct HookCounts {
    qint64 maxCards = 0;
    qint64 validity = 0;
    qint64 frequency = 0;
    qint64 distance = 0;
    qint64 lua = 0;
    QJsonObject json() const {
        return {{"max_cards", double(maxCards)}, {"validity", double(validity)},
            {"frequency", double(frequency)}, {"distance", double(distance)},
            {"lua", double(lua)}};
    }
};

static void runtimeHook(HookCounts &counts, int input)
{
    LuaRuntime *runtime = LuaRuntime::current();
    CHECK(runtime && runtime->isCurrentThreadOwner());
    LuaRuntime::LuaInvocationScope invocation(*runtime);
    lua_State *state = runtime->state();
    const int top = lua_gettop(state);
    lua_getglobal(state, "hybrid50ProjectionHook");
    CHECK(lua_isfunction(state, -1));
    lua_pushinteger(state, input);
    CHECK(LuaRuntime::protectedCall(state, 1, 1, 0) == 0);
    CHECK(lua_isnumber(state, -1));
    lua_settop(state, top);
    ++counts.lua;
}

class FixtureMaxCards : public MaxCardsSkill {
public:
    explicit FixtureMaxCards(HookCounts &counts)
        : MaxCardsSkill("hybrid50_projection_max"), counts(counts) {}
    int getExtra(const Player *player) const override {
        ++counts.maxCards;
        runtimeHook(counts, player->getSeat());
        auto action = std::move(onQuery);
        onQuery = {};
        if (action) action(player);
        return player->getMark("hybrid50_extra");
    }
    HookCounts &counts;
    mutable std::function<void(const Player *)> onQuery;
};

class FixtureValidity : public InvaliditySkill {
public:
    explicit FixtureValidity(HookCounts &counts)
        : InvaliditySkill("#hybrid50_projection_validity"), counts(counts) {}
    bool isSkillValid(const Player *player, const Skill *skill) const override {
        ++counts.validity;
        runtimeHook(counts, player->getSeat());
        return player->getMark("hybrid50_invalid") <= 0
            || skill->objectName() != "hybrid50_projection_skill_0";
    }
    HookCounts &counts;
};

class FixtureSkill : public Skill {
public:
    FixtureSkill(int index, HookCounts &counts)
        : Skill(QString("hybrid50_projection_skill_%1").arg(index)), counts(counts) {}
    Frequency getFrequency(const Player *player = nullptr) const override {
        ++counts.frequency;
        if (player) runtimeHook(counts, player->getSeat());
        return player && player->getMark("hybrid50_frequency") > 0 ? Compulsory : NotFrequent;
    }
    HookCounts &counts;
};

class FixtureDistance : public DistanceSkill {
public:
    explicit FixtureDistance(HookCounts &counts)
        : DistanceSkill("#hybrid50_projection_distance"), counts(counts) {}
    int getCorrect(const Player *from, const Player *to) const override {
        ++counts.distance;
        return from->getMark("hybrid50_distance") > 0 && from->getSeat() < to->getSeat() ? 1 : 0;
    }
    HookCounts &counts;
};

struct RoomTestAccess {
    static AiDecisionCoordinator &ai(Room &room) { return *room.m_aiDecisions; }
    static void add(Room &room, ServerPlayer *player) { room.addPlayerToRoster(player); }
    static void ready(Room &room) { room.m_roster->resetAliveToPlayers(); }
    static void removeAlive(Room &room, ServerPlayer *player) { room.m_roster->removeAlive(player); }
    static void reverse(Room &room) { room.m_roster->reversePlayOrder(); }
    static void invalidate(Room &room) {
        ai(room).m_publicBoardValid = false;
        ai(room).m_distanceCacheValid = false;
    }
    static bool reusable(Room &room) { return ai(room).m_publicBoardValid; }
    static AIWorldView uncached(Room &room, ServerPlayer *viewer, bool compact, bool eventOnly) {
        auto &coordinator = ai(room);
        return coordinator.projectWorldView(coordinator.buildPublicBoard(), viewer, compact, eventOnly);
    }
    static AIRequest response(Room &room, ServerPlayer *viewer) {
        return ai(room).makeResponseRequest(viewer, "askForCard", "fixture_response", "jink",
                                            "fixture_prompt", Card::MethodResponse);
    }
    static QJsonObject benchmarkPublic(Room &room, int iterations, HookCounts &counts) {
        counts = {};
        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < iterations; ++i) {
            const auto board = ai(room).buildPublicBoard();
            CHECK(board.players.size() == 50);
        }
        return {{"workload", "public_only"}, {"iterations", iterations},
            {"wall_ms", timer.nsecsElapsed() / 1e6}, {"callbacks", counts.json()}};
    }
    static QJsonObject benchmarkProjection(Room &room, const QList<ServerPlayer *> &players,
                                           HookCounts &counts) {
        const auto board = ai(room).buildPublicBoard();
        counts = {};
        QList<AIWorldView> worlds;
        QElapsedTimer timer;
        timer.start();
        for (auto *player : players)
            worlds << ai(room).projectWorldView(board, player, true, false);
        const qint64 elapsed = timer.nsecsElapsed();
        CHECK(worlds.size() == 50);
        return {{"workload", "private_projection_only"}, {"iterations", 50},
            {"wall_ms", elapsed / 1e6}, {"callbacks", counts.json()}};
    }
};

static QJsonObject worldJson(const AIWorldView &world)
{
    AIRequest request;
    request.worldView = world;
    return externalAgentRequestJson(request).value("worldView").toObject();
}

static const AIPlayerView &seat(const AIWorldView &world, const QString &name)
{
    if (world.self.objectName == name) return world.self;
    for (const auto &player : world.players)
        if (player.objectName == name) return player;
    qFatal("Missing projection for %s", qPrintable(name));
}

static const AISkillView &skill(const AIPlayerView &player, const QString &name)
{
    for (const auto &view : player.skills)
        if (view.skillName == name) return view;
    qFatal("Missing projected skill %s", qPrintable(name));
}

struct ProjectionFixture {
    HookCounts counts;
    Room *room;
    QList<ServerPlayer *> players;
    FixtureMaxCards *maxCards;
    static constexpr int SkillsPerSeat = 24;

    ProjectionFixture()
        : room(new Room(nullptr, "50p", GameSessionConfig(26092920),
                        Room::RuntimeInitializationPolicy::Deferred)) {
        phase("fixture_room_created");
        EngineRuntimeContextScope context(*Sanguosha, room);
        LuaRuntime &runtime = room->roomRuntime()->lua();
        QString error;
        CHECK(runtime.initialize(&error));
        phase("fixture_lua_initialized");
        LuaRuntime::Binding binding(runtime);
        room->roomRuntime()->state().reset();
        phase("fixture_cards_reset");
        // The callback runs inside the real runtime's protected invocation. Keep the
        // workload fixed and bounded, with no extension or gameplay startup.
        CHECK(luaL_dostring(runtime.state(), R"lua(
            function hybrid50ProjectionHook(input)
                local sum = input
                for i = 1, 48 do sum = (sum * 17 + i) % 997 end
                return sum
            end
            sgs.evaluateModeAI = function(world, viewerOnly)
                local name = world.self.object_name
                local amount = world.self.public_marks.hybrid50_policy or 0
                local objectives = {[name] = amount % 5}
                return {managed = false, predictable = true,
                    relations = {[name] = {[name] = 'friend'}}, objectives = objectives,
                    game_process = amount, process_label = 'hybrid50_projection_fixture'}
            end
        )lua") == 0);
        maxCards = new FixtureMaxCards(counts);
        QList<const Skill *> definitions{maxCards, new FixtureValidity(counts), new FixtureDistance(counts)};
        for (int index = 0; index < SkillsPerSeat; ++index)
            definitions << new FixtureSkill(index, counts);
        room->roomRuntime()->addSkills(definitions);
        phase("fixture_definitions_added");
        for (int index = 0; index < 50; ++index) {
            auto *player = new ServerPlayer(room);
            player->setObjectName(QString("hybrid50_seat_%1").arg(index));
            player->setSeat(index + 1);
            player->setMaxHp(4);
            player->setHp(3 + index % 2);
            player->setRole(index == 0 ? "lord" : index % 3 == 0 ? "renegade" : "rebel");
            player->setKingdom("wei");
            RoomTestAccess::add(*room, player);
            for (int number = 0; number < SkillsPerSeat; ++number) {
                SkillInstance instance;
                instance.skillName = QString("hybrid50_projection_skill_%1").arg(number);
                instance.instanceID = 1;
                instance.source = SourceAcquired;
                instance.correctState = {{"public_counter", index + number}};
                player->upsertSkillInstance(instance);
            }
            player->setSkillInstanceState("hybrid50_projection_skill_0", 1,
                {{"owner_secret", QString("secret_%1").arg(index)}});
            player->setFlags(QString("private_flag_%1").arg(index));
            for (int mark = 0; mark < 24; ++mark)
                player->setMark(QString("fixture_mark_%1").arg(mark), index + mark + 1);
            players << player;
        }
        RoomTestAccess::ready(*room);
        room->setCurrent(players.first());
        players.first()->setPhase(Player::Play);
        counts = {};
        phase("fixture_ready");
    }

    AiDecisionCoordinator &ai() { return RoomTestAccess::ai(*room); }
    AIWorldView world(ServerPlayer *viewer, bool compact = true, bool eventOnly = false) {
        return ai().buildWorldView(viewer, compact, eventOnly);
    }
    void equivalent(ServerPlayer *viewer, bool compact = true, bool eventOnly = false) {
        CHECK(worldJson(world(viewer, compact, eventOnly))
            == worldJson(RoomTestAccess::uncached(*room, viewer, compact, eventOnly)));
    }
};

static void correctness()
{
    phase("correctness_begin");
    ProjectionFixture fixture;
    auto &room = *fixture.room;
    auto &ai = fixture.ai();
    auto *owner = fixture.players.at(1);
    auto *observer = fixture.players.at(2);
    auto *stranger = fixture.players.at(3);
    EngineRuntimeContextScope context(*Sanguosha, &room);
    LuaRuntime::Binding binding(room.roomRuntime()->lua());

    for (auto *viewer : fixture.players) {
        const auto world = fixture.world(viewer, true, true);
        CHECK(world.self.objectName == viewer->objectName());
        CHECK(world.players.size() == 49 && world.playerOrder.size() == 50);
        CHECK(world.distanceScope == "none" && world.distances.isEmpty());
        CHECK(world.events.isEmpty());
        CHECK(skill(world.self, "hybrid50_projection_skill_0").hasPrivateState);
        CHECK(skill(world.self, "hybrid50_projection_skill_0").state.value("owner_secret")
            == QString("secret_%1").arg(viewer->getSeat() - 1));
        for (const auto &other : world.players) {
            CHECK(other.privateFlags.isEmpty() && !other.privateFlagsVisible);
            CHECK(other.skippedPhases.isEmpty());
            for (const auto &view : other.skills) CHECK(!view.hasPrivateState && view.state.isEmpty());
        }
    }
    CHECK(fixture.counts.maxCards == 50); // One complete public fill across 50 viewers.
    CHECK(fixture.counts.distance == 0);
    phase("correctness_50_viewers_done");
    fixture.equivalent(owner);
    fixture.equivalent(observer, false);
    CHECK(fixture.world(owner, false).distances.size() == 50);
    owner->setMark("hybrid50_distance", 1);
    const auto geometry = fixture.world(owner);
    CHECK(geometry.distanceScope == "viewer");
    CHECK(geometry.distances.value(owner->objectName()).value(observer->objectName())
        == owner->distanceTo(observer));
    CHECK(geometry.distances.value(observer->objectName()).value(owner->objectName())
        == observer->distanceTo(owner));
    CHECK(owner->distanceTo(observer) != observer->distanceTo(owner));
    fixture.equivalent(owner, false);
    phase("correctness_geometry_done");

    owner->setMark("hybrid50_extra", 2);
    CHECK(fixture.world(owner).self.maxCards == owner->getMaxCards());
    CHECK(fixture.world(observer).players.size() == 49);
    fixture.equivalent(owner);
    owner->setMark("hybrid50_invalid", 1);
    CHECK(skill(fixture.world(owner).self, "hybrid50_projection_skill_0").invalid);
    owner->setMark("hybrid50_frequency", 1);
    CHECK(skill(fixture.world(owner).self, "hybrid50_projection_skill_0").frequency == Skill::Compulsory);
    fixture.equivalent(observer);
    owner->setSkillInstanceStateValue("hybrid50_projection_skill_0", 1, "owner_secret", "changed_secret");
    CHECK(skill(fixture.world(owner).self, "hybrid50_projection_skill_0").state.value("owner_secret") == "changed_secret");
    CHECK(skill(seat(fixture.world(observer), owner->objectName()), "hybrid50_projection_skill_0").state.isEmpty());
    owner->setSkillInstanceCorrectStateValue("hybrid50_projection_skill_0", 1, "public_counter", 901);
    CHECK(skill(seat(fixture.world(observer), owner->objectName()), "hybrid50_projection_skill_0").correctState.value("public_counter") == 901);
    owner->setMark("hybrid50_policy", 3);
    CHECK(fixture.world(owner).modePolicy.value("game_process") == 3);

    auto before = fixture.counts.maxCards;
    SkillSet::bump();
    fixture.world(observer, true, true);
    CHECK(fixture.counts.maxCards == before + 50);
    class AddedDefinition : public MaxCardsSkill {
    public:
        AddedDefinition() : MaxCardsSkill("#hybrid50_projection_added") {}
        int getExtra(const Player *) const override { return 1; }
    };
    before = fixture.counts.maxCards;
    room.roomRuntime()->addSkills({new AddedDefinition});
    CHECK(fixture.world(owner).self.maxCards == owner->getMaxCards());
    CHECK(fixture.counts.maxCards >= before + 50);
    fixture.equivalent(stranger);
    phase("correctness_state_and_definition_done");

    // Live viewer-only state and permissions must refresh even if a public snapshot
    // is reused. These APIs include card flags and pile visibility without revision bumps.
    CHECK(Sanguosha->getCard(0) && Sanguosha->getCard(1) && Sanguosha->getCard(2));
    owner->addCard(0, Player::PlaceHand);
    owner->addCard(1, Player::PlaceHand);
    owner->addCard(2, Player::PlaceHand);
    owner->setMark("owner_only_mark", 77);
    ai.setMarkVisibility(owner, "owner_only_mark", 77, {owner});
    const auto hidden = fixture.world(observer);
    CHECK(!seat(hidden, owner->objectName()).roleVisible);
    CHECK(seat(hidden, owner->objectName()).knownCards.isEmpty());
    CHECK(!seat(hidden, owner->objectName()).publicMarks.contains("owner_only_mark"));
    CHECK(fixture.world(owner).handCards.size() == 3);
    const quint64 revision = room.roomRuntime()->stateRevision();
    Sanguosha->getCard(0)->setFlags(QString("visible_%1_%2").arg(observer->objectName(), owner->objectName()));
    CHECK(room.roomRuntime()->stateRevision() == revision);
    CHECK(seat(fixture.world(observer), owner->objectName()).knownCards.size() == 1);
    CHECK(seat(fixture.world(stranger), owner->objectName()).knownCards.isEmpty());
    Sanguosha->getCard(1)->setFlags("visible");
    CHECK(seat(fixture.world(stranger), owner->objectName()).knownCards.size() == 1);
    owner->setFlags("live_owner_flag");
    CHECK(fixture.world(owner).self.privateFlags.contains("live_owner_flag"));
    CHECK(seat(fixture.world(observer), owner->objectName()).privateFlags.isEmpty());
    ai.setMarkVisibility(owner, "owner_only_mark", 77, {owner, observer});
    CHECK(seat(fixture.world(observer), owner->objectName()).publicMarks.value("owner_only_mark") == 77);
    CHECK(!seat(fixture.world(stranger), owner->objectName()).publicMarks.contains("owner_only_mark"));
    room.revealRoleTo(observer, owner);
    CHECK(seat(fixture.world(observer), owner->objectName()).role == owner->getRole());
    CHECK(!seat(fixture.world(stranger), owner->objectName()).roleVisible);
    owner->setPileCardPresent("&private_fixture_pile", 3, true);
    CHECK(!seat(fixture.world(observer), owner->objectName()).piles.first().open);
    owner->setPileOpen("&private_fixture_pile", observer->objectName());
    CHECK(seat(fixture.world(observer), owner->objectName()).piles.first().cardIds == QList<int>{3});
    CHECK(seat(fixture.world(stranger), owner->objectName()).piles.first().cardIds.isEmpty());
    fixture.equivalent(owner);
    fixture.equivalent(observer);
    fixture.equivalent(stranger);
    phase("correctness_visibility_done");

    const auto previousRevision = room.roomRuntime()->stateRevision();
    CardsMoveOneTimeStruct hiddenMove{};
    hiddenMove.card_ids = {0, 1};
    hiddenMove.from = nullptr;
    hiddenMove.to = owner;
    hiddenMove.from_places = {Player::DrawPile, Player::DrawPile};
    hiddenMove.from_pile_names = {QString(), QString()};
    hiddenMove.to_place = Player::PlaceHand;
    hiddenMove.open = {false, false};
    hiddenMove.is_last_handcard = false;
    ai.recordEvent(CardsMoveOneTime, owner, QVariant::fromValue(hiddenMove));
    ai.recordEvent(ChoiceMade, owner, "skillChoice:fixture:private_answer");
    DamageStruct damage("fixture", owner, observer, 2);
    ai.recordEvent(Damage, observer, QVariant::fromValue(damage));
    CHECK(room.roomRuntime()->stateRevision() == previousRevision);
    const auto ownEvents = fixture.world(owner).events;
    const auto observedEvents = fixture.world(observer).events;
    CHECK(ownEvents.size() == 3 && observedEvents.size() == 2);
    CHECK(ownEvents.first().privateCardIds == QList<int>({0, 1}));
    CHECK(observedEvents.first().privateCardIds.isEmpty());
    CHECK(ownEvents.last().amount == 2 && ownEvents.last().kind == "damage");
    CHECK(ownEvents.first().sequence < ownEvents.last().sequence);
    fixture.equivalent(owner);
    fixture.equivalent(observer);
    const QByteArray otherBytes = QJsonDocument(worldJson(fixture.world(stranger))).toJson(QJsonDocument::Compact);
    CHECK(!otherBytes.contains("changed_secret") && !otherBytes.contains("private_answer")
        && !otherBytes.contains("live_owner_flag"));
    phase("correctness_history_done");

    // Public fill may enter native/Lua callbacks. A mutating or nested callback must
    // not publish a snapshot built before its state change.
    RoomTestAccess::invalidate(room);
    fixture.maxCards->onQuery = [](const Player *player) {
        const_cast<Player *>(player)->setMark("hybrid50_mid_fill", 1);
    };
    fixture.world(owner, true, true);
    CHECK(!RoomTestAccess::reusable(room));
    fixture.world(observer, true, true);
    CHECK(RoomTestAccess::reusable(room));
    RoomTestAccess::invalidate(room);
    AIWorldView nested;
    fixture.maxCards->onQuery = [&](const Player *player) {
        const_cast<Player *>(player)->setMark("hybrid50_nested", 1);
        nested = fixture.world(stranger, true, true);
    };
    const auto outer = fixture.world(owner, true, true);
    CHECK(nested.self.objectName == stranger->objectName() && outer.self.objectName == owner->objectName());
    CHECK(!RoomTestAccess::reusable(room));
    fixture.equivalent(observer);
    phase("correctness_reentrancy_done");

    RoomTestAccess::reverse(room);
    fixture.equivalent(owner);
    RoomTestAccess::removeAlive(room, fixture.players.last());
    CHECK(fixture.world(owner).alivePlayerOrder.size() == 49);
    auto *extra = new ServerPlayer(&room);
    extra->setObjectName("hybrid50_extra_seat");
    extra->setSeat(51);
    extra->setMaxHp(4);
    extra->setHp(4);
    RoomTestAccess::add(room, extra);
    CHECK(fixture.world(owner).players.size() == 50);
    fixture.equivalent(observer);
    puts("PASS projection: 50-seat equivalence, geometry, role/hand/mark/pile/private-state isolation, live history, state/skill/definition/roster invalidation, mutation and reentrancy");
    fflush(stdout);
}

static void printMetric(QJsonObject metric, int sample)
{
    metric.insert("sample", sample);
    const QByteArray json = QJsonDocument(metric).toJson(QJsonDocument::Compact);
    printf("PROJECTION_METRIC %s\n", json.constData());
    fflush(stdout);
}

static void preparePolicyFixture(ProjectionFixture &fixture, bool local)
{
    EngineRuntimeContextScope context(*Sanguosha, fixture.room);
    LuaRuntime::Binding binding(fixture.room->roomRuntime()->lua());
    for (auto *viewer : fixture.players) {
        viewer->setState("robot");
        const auto endpoint = local
            ? fixture.room->attachExternalAgent(viewer, ExternalAgentEndpoint::Pause,
                                                ExternalAgentProjectionPolicy::LocalDecisions)
            : fixture.room->attachExternalAgent(viewer, ExternalAgentEndpoint::Pause);
        CHECK(endpoint);
        // Different physical cards per seat preserve the native candidate gates.
        CHECK(Sanguosha->getCard(viewer->getSeat() - 1));
        viewer->addCard(viewer->getSeat() - 1, Player::PlaceHand);
    }
    fixture.ai().recordEvent(GameReady, fixture.players.first(), QVariant());
    fixture.ai().recordEvent(ChoiceMade, fixture.players.first(), "skillChoice:fixture:policy_private_answer");
}

static AIRequest policyRequest(ProjectionFixture &fixture, int index)
{
    auto *viewer = fixture.players.at(index);
    viewer->setMark("hybrid50_policy", index + 1); // Force a new public revision for each ask.
    EngineRuntimeContextScope context(*Sanguosha, fixture.room);
    LuaRuntime::Binding binding(fixture.room->roomRuntime()->lua());
    if (index % 13 != 0 && index % 9 != 0)
        return RoomTestAccess::response(*fixture.room, viewer);
    AIChoiceOptions options;
    options.reason = "fixture_policy";
    options.choices = {"option_a", "option_b"};
    options.optional = true;
    return fixture.ai().makeChoiceRequest(viewer,
        index % 13 == 0 ? AIRequest::General : AIRequest::Choice, options);
}

static void policyComparison(int sample)
{
    phase("policy_fixtures_begin");
    ProjectionFixture full;
    ProjectionFixture local;
    preparePolicyFixture(full, false);
    preparePolicyFixture(local, true);
    QList<AIRequest> fullRequests;
    QList<AIRequest> localRequests;
    for (auto profile : {false, true}) {
        auto &fixture = profile ? local : full;
        auto &requests = profile ? localRequests : fullRequests;
        fixture.counts = {};
        QElapsedTimer timer;
        phase(profile ? "policy_local_begin" : "policy_full_begin");
        timer.start();
        for (int index = 0; index < fixture.players.size(); ++index)
            requests << policyRequest(fixture, index);
        const qint64 elapsed = timer.nsecsElapsed();
        printMetric({{"workload", profile ? "local_decision_mix_opt_in" : "local_decision_mix_full"},
            {"iterations", 50}, {"wall_ms", elapsed / 1e6},
            {"callbacks", fixture.counts.json()}}, sample);
    }
    QCryptographicHash retained(QCryptographicHash::Sha256);
    for (int index = 0; index < fullRequests.size(); ++index) {
        const auto &complete = fullRequests.at(index);
        const auto &minimal = localRequests.at(index);
        CHECK(complete.kind == minimal.kind && complete.stateRevision == minimal.stateRevision);
        CHECK(complete.worldView.players.size() == 49 && complete.worldView.handCards.size() == 1);
        CHECK(!complete.worldView.events.isEmpty());
        CHECK(minimal.worldView.modeId == "50p" && minimal.worldView.revision == minimal.stateRevision);
        CHECK(minimal.worldView.self.objectName == minimal.viewerObjectName);
        CHECK(minimal.worldView.players.isEmpty() && minimal.worldView.handCards.isEmpty());
        CHECK(minimal.worldView.self.role.isEmpty() && minimal.worldView.self.skills.isEmpty());
        CHECK(minimal.worldView.events.isEmpty() && minimal.worldView.distances.isEmpty());
        CHECK(minimal.worldView.distanceScope == "none");
        CHECK(minimal.worldView.modePolicy.value("projection_scope") == "local_decision");
        auto fullJson = externalAgentRequestJson(complete);
        auto localJson = externalAgentRequestJson(minimal);
        fullJson.remove("worldView");
        localJson.remove("worldView");
        // Decision ids, stamps, patterns, handling methods, candidate/conversion
        // tickets and choice options must all remain exactly the same.
        CHECK(fullJson == localJson);
        retained.addData(QJsonDocument(fullJson).toJson(QJsonDocument::Compact));
    }
    printMetric({{"workload", "local_decision_mix_contract"}, {"iterations", 50},
        {"retained_request_sha256", QString::fromLatin1(retained.result().toHex())}}, sample);
    CHECK(local.counts.maxCards == 0 && local.counts.validity == 0 && local.counts.frequency == 0);
    // The opt-in affects request construction only. Full direct observations retain
    // every seat and the live, correctly filtered event log.
    const auto fullObservation = local.world(local.players.first());
    CHECK(fullObservation.players.size() == 49 && fullObservation.events.size() == 2);
    CHECK(fullObservation.events.last().details.value("answer") == "policy_private_answer");
    const auto otherObservation = local.world(local.players.last());
    CHECK(otherObservation.events.size() == 1);
    const auto foreignViewer = local.world(full.players.first());
    CHECK(foreignViewer.self.objectName.isEmpty() && foreignViewer.players.isEmpty()
        && foreignViewer.handCards.isEmpty());
    for (const auto kind : {AIRequest::Activate, AIRequest::UseCard, AIRequest::CardChosen,
                           AIRequest::PlayerChosen, AIRequest::PlayersChosen}) {
        const auto endpoint = local.room->externalAgent(local.players.first()->objectName());
        CHECK(endpoint->requiresWorldView(kind));
    }
    // CardChosen consumes its projected target zones; preserve its full construction.
    const auto chosen = local.ai().makeRequest(local.players.first(), AIRequest::CardChosen,
        CardUseStruct::CARD_USE_REASON_UNKNOWN, {}, {}, Card::MethodNone);
    CHECK(chosen.worldView.players.size() == 49 && chosen.worldView.handCards.size() == 1);
    puts("PASS policy: identical 50-request authority contract, explicit minimal worlds, default complete worlds and full live observations");
    fflush(stdout);
}

static void benchmark(int samples)
{
    for (int sample = 0; sample < samples; ++sample) {
        ProjectionFixture fixture;
        phase("benchmark_public_begin");
        printMetric(RoomTestAccess::benchmarkPublic(*fixture.room, 50, fixture.counts), sample);
        phase("benchmark_private_begin");
        printMetric(RoomTestAccess::benchmarkProjection(*fixture.room, fixture.players, fixture.counts), sample);
        for (bool changing : {false, true}) {
            RoomTestAccess::invalidate(*fixture.room);
            fixture.counts = {};
            QList<AIWorldView> worlds;
            QElapsedTimer timer;
            timer.start();
            phase(changing ? "benchmark_changing_begin" : "benchmark_stable_begin");
            for (int index = 0; index < fixture.players.size(); ++index) {
                auto *viewer = fixture.players.at(index);
                if (changing) viewer->setMark("hybrid50_policy", index + 1);
                worlds << fixture.world(viewer);
            }
            const qint64 elapsed = timer.nsecsElapsed();
            // Serialization and hashing are outside the measured projection time.
            QCryptographicHash hash(QCryptographicHash::Sha256);
            for (const auto &world : worlds) {
                CHECK(world.players.size() == 49 && world.self.skills.size() == ProjectionFixture::SkillsPerSeat);
                CHECK(!world.modePolicy.contains("error"));
                hash.addData(QJsonDocument(worldJson(world)).toJson(QJsonDocument::Compact));
            }
            printMetric({{"workload", changing ? "changing_revision" : "stable_revision"},
                {"iterations", 50}, {"wall_ms", elapsed / 1e6},
                {"callbacks", fixture.counts.json()}, {"world_sha256", QString::fromLatin1(hash.result().toHex())}}, sample);
        }
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QString error;
    CHECK(QSanRuntimePaths::resolve(app.arguments(), &error));
    phase("engine_begin");
    Engine engine;
    phase("engine_ready");
    Config.EnableAI = false;
    Config.EnableHegemony = false;
    ServerInfo.EnableHegemony = false;
    ServerInfo.GameMode = "50p";
    const bool benchmarkOnly = app.arguments().contains("--benchmark");
    const bool correctnessOnly = app.arguments().contains("--correctness");
    const bool policyOnly = app.arguments().contains("--policy-only");
    int samples = 1;
    const int sampleOption = app.arguments().indexOf("--samples");
    if (sampleOption >= 0) {
        CHECK(sampleOption + 1 < app.arguments().size());
        bool valid = false;
        samples = app.arguments().at(sampleOption + 1).toInt(&valid);
        CHECK(valid && samples >= 1 && samples <= 5);
    }
    if (!benchmarkOnly && !policyOnly) correctness();
    if (!correctnessOnly && !policyOnly) benchmark(samples);
    for (int sample = 0; sample < (correctnessOnly ? 1 : samples); ++sample)
        policyComparison(sample);
    fflush(stdout);
    // Deferred native snapshots intentionally have no worker/game lifecycle. Like
    // the existing local 50-seat probes, leave process-owned fixtures to process exit.
    std::_Exit(0);
}
