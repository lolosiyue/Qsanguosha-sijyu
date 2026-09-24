#include "engine-bootstrap.h"
#include "ai.h"
#include "engine.h"
#include "gamerule.h"
#include "general.h"
#include "lua-runtime.h"
#include "package.h"
#include "room-test-access.h"
#include "room-state.h"
#include "roomthread.h"
#include "server-info.h"
#include "settings.h"
#include "skill-instance-utils.h"
#include "protocol/protocol-runtime.h"

#include <QDebug>
#include <QScopeGuard>
#include <memory>

namespace {

#define HEG_CHECK(condition) do { if (!(condition)) { \
    qCritical() << "Hegemony contract failed at line" << __LINE__ << #condition; \
    return false; \
} } while (false)

const QString limitSkill = QStringLiteral("heg_test_hegemony_shared_limit");
const QString limitMark = QStringLiteral("@test_hegemony_limit");

class SharedRevealProbe : public TriggerSkillV2
{
public:
    SharedRevealProbe() : TriggerSkillV2("test_shared_reveal")
    {
        frequency = Compulsory;
        events << EventPhaseStart;
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const override
    {
        TriggerList result;
        for (ServerPlayer *owner : room->getPlayers())
            if (owner->hasSkill(objectName())) result[owner] << objectName();
        return result;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override { return payment; }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override
    {
        ++effects;
        return false;
    }
    bool payment = true;
    mutable int effects = 0;
};

class RevealChoiceAI : public TrustAI
{
public:
    explicit RevealChoiceAI(ServerPlayer *owner) : TrustAI(owner), owner(owner) {}
    QString askForTriggerOrder(const QString &, QMap<ServerPlayer *, QStringList> &skills,
                              bool optional, const QVariant &) override
    {
        ++requests;
        privateChoices = privateChoices && skills.size() == 1 && skills.contains(owner);
        lastOptional = optional;
        const QString selected = answer;
        answer = "cancel";
        return selected;
    }
    ServerPlayer *owner;
    QString answer = "cancel";
    int requests = 0;
    bool privateChoices = true;
    bool lastOptional = false;
};

class HegemonyLimitedProbe : public TriggerSkillV2
{
public:
    HegemonyLimitedProbe() : TriggerSkillV2(limitSkill)
    {
        frequency = Limited;
        limit_mark = limitMark;
    }

    bool trigger(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override
    {
        return false;
    }
};

class TargetModRevealProbe : public TargetModSkillV2
{
public:
    enum Mode { None, ResidueOnly, ExtraTargetOnly, DistanceOnly };

    TargetModRevealProbe() : TargetModSkillV2(QStringLiteral("test_heg_target_mod")) {}

    CorrectSkillResult getCorrection(const CorrectSkillContext &context) const override
    {
        if (mode == ResidueOnly && context.modType == TargetModSkill::Residue)
            return CorrectSkillResult::useAmount(1);
        if (mode == ExtraTargetOnly && context.modType == TargetModSkill::ExtraTarget)
            return CorrectSkillResult::useAmount(1);
        if (mode == DistanceOnly && context.modType == TargetModSkill::DistanceLimit
            && context.secondary && context.secondary->property("test_far_target").toBool())
            return CorrectSkillResult::useAmount(100);
        return CorrectSkillResult::noEffect();
    }

    static Mode mode;
};

TargetModRevealProbe::Mode TargetModRevealProbe::mode = TargetModRevealProbe::None;

class HegemonyFixture
{
public:
    HegemonyFixture()
        : room(nullptr, QStringLiteral("04p")), engineScope(*Sanguosha, &room),
          luaBinding(*room.luaRuntime())
    {
        RoomTestAccess::attachThread(room);
        room.getRoomState()->reset();
        for (int id : room.getDrawPile())
            room.setCardMapping(id, nullptr, Player::DrawPile);
        // Room-owned definitions avoid depending on either the current HEG
        // content or the next heg_ migration, and cannot pollute another suite.
        auto *package = new Package(QStringLiteral("test_hegemony_rules"));
        auto *head = new General(package, "test_heg_wei_head", "wei", 4);
        head->addSkill(new HegemonyLimitedProbe);
        auto *targetMod = new TargetModRevealProbe;
        head->addSkill(targetMod);
        auto *deputy = new General(package, "test_heg_wei_deputy", "wei", 3, false);
        deputy->addSkill(limitSkill);
        deputy->addSkill(targetMod->objectName());
        head->addCompanion(deputy->objectName());
        new General(package, "test_heg_wei_even", "wei", 4);
        new General(package, "test_heg_shu_head", "shu", 4);
        new General(package, "test_heg_shu_deputy", "shu", 4);
        new General(package, "test_heg_wu_head", "wu", 4);
        new General(package, "test_heg_wu_deputy", "wu", 4);
        new General(package, "test_heg_qun_head", "qun", 4);
        new General(package, "test_heg_qun_deputy", "qun", 4);
        new General(package, "test_heg_dual_head", "wei+shu", 4);
        new General(package, "test_heg_dual_deputy", "wei+shu", 4);
        new General(package, "lord_test_heg_wei$", "wei", 4);
        Sanguosha->addPackage(package);
    }

    ServerPlayer *add(const QString &head, const QString &deputy)
    {
        const int seat = room.getPlayers().size() + 1;
        ServerPlayer *player = RoomTestAccess::addPlayer(
            room, QStringLiteral("heg-seat-%1").arg(seat), QStringLiteral("robot"));
        auto *ai = new TrustAI(player);
        ai->setParent(player);
        player->setAI(ai);
        player->setActualGeneral1Name(head);
        player->setActualGeneral2Name(deputy);
        player->setGeneralName("anjiang");
        player->setGeneral2Name("anjiang");
        player->setGeneralShowed(false);
        player->setGeneral2Showed(false);
        player->setKingdom("god");
        player->setRole(HegemonyRule::getMappedRole(player->getActualGeneral1()->getKingdom()));
        player->setShownRole(false);
        player->setPhase(Player::NotActive);
        player->setSeat(seat);
        player->setMaxHp(player->getGeneralMaxHp());
        player->setHp(player->getGeneralStartHp());
        room.setTag(player->objectName(), QStringList{head, deputy});
        player->setProperty("hegemony_generals", head + '+' + deputy);
        return player;
    }

    ServerPlayer *addWei()
    {
        return add("test_heg_wei_head", "test_heg_wei_deputy");
    }

    void prepare()
    {
        const QList<ServerPlayer *> players = room.getPlayers();
        for (int i = 0; i < players.size(); ++i)
            players.at(i)->setNext(players.at((i + 1) % players.size()));
        RoomTestAccess::resetAlive(room);
        room.setCurrent(players.first());
        room.preparePlayers();
    }

    void ready()
    {
        HegemonyRule rule(nullptr);
        QVariant data;
        rule.trigger(GameReady, &room, nullptr, data);
    }

    void installRule()
    {
        auto *rule = new HegemonyRule(room.getThread());
        rule->setParent(room.getThread());
        room.getThread()->addTriggerSkill(rule);
    }

    Room room;
    EngineRuntimeContextScope engineScope;
    LuaRuntime::Binding luaBinding;
};

const AIPlayerView *findView(const AIWorldView &world, const ServerPlayer *player)
{
    if (world.self.objectName == player->objectName()) return &world.self;
    for (const AIPlayerView &view : world.players)
        if (view.objectName == player->objectName()) return &view;
    return nullptr;
}

QList<int> visibleProbeInstances(const AIPlayerView &view)
{
    QList<int> ids;
    for (const AISkillView &skill : view.skills)
        if (skill.skillName == limitSkill) ids << skill.instanceId;
    return ids;
}

bool revealPreservesInstancesAndPrivacy()
{
    HegemonyFixture fixture;
    ServerPlayer *owner = fixture.addWei();
    ServerPlayer *observer = fixture.add("test_heg_shu_head", "test_heg_shu_deputy");
    fixture.add("test_heg_wu_head", "test_heg_wu_deputy");
    fixture.add("test_heg_qun_head", "test_heg_qun_deputy");
    fixture.prepare();
    fixture.ready();

    const QList<int> ids = owner->getSkillInstanceIds(limitSkill);
    HEG_CHECK(ids.size() == 2);
    const int headId = ids.first(), deputyId = ids.last();
    HEG_CHECK(owner->findSkillInstance(limitSkill, headId)->bindHead == 1);
    HEG_CHECK(owner->findSkillInstance(limitSkill, deputyId)->bindHead == 2);
    owner->setSkillInstanceStateValue(limitSkill, headId, "uses", 3);
    owner->setSkillInstanceStateValue(limitSkill, deputyId, "uses", 7);
    HEG_CHECK(owner->getMark(limitMark) == 1);

    const AIWorldView ownWorld = fixture.room.buildAIWorldView(owner);
    HEG_CHECK(ownWorld.self.generalName == owner->getActualGeneral1Name());
    HEG_CHECK(ownWorld.self.publicMarks.value(limitMark) == 1);
    HEG_CHECK(ownWorld.self.general2Name == owner->getActualGeneral2Name());
    HEG_CHECK(visibleProbeInstances(ownWorld.self) == ids);
    AIWorldView world = fixture.room.buildAIWorldView(observer);
    const AIPlayerView *view = findView(world, owner);
    HEG_CHECK(view && view->generalName.isEmpty() && view->general2Name.isEmpty());
    HEG_CHECK(!view->roleVisible && view->role.isEmpty() && view->kingdom.isEmpty());
    HEG_CHECK(visibleProbeInstances(*view).isEmpty());
    HEG_CHECK(!view->publicMarks.contains(limitMark));

    owner->showGeneral(true, false, false);
    HEG_CHECK(owner->hasShownGeneral() && !owner->hasShownGeneral2());
    HEG_CHECK(fixture.room.isRoleRevealed(owner));
    HEG_CHECK(owner->property("hegemony_generals").toString() == owner->getActualGeneral2Name());
    HEG_CHECK(owner->getSkillInstanceIds(limitSkill) == ids);
    HEG_CHECK(owner->getSkillInstanceStateValue(limitSkill, headId, "uses").toInt() == 3);
    HEG_CHECK(owner->getSkillInstanceStateValue(limitSkill, deputyId, "uses").toInt() == 7);
    world = fixture.room.buildAIWorldView(observer);
    view = findView(world, owner);
    HEG_CHECK(view && view->generalName == owner->getActualGeneral1Name());
    HEG_CHECK(view->general2Name.isEmpty() && view->roleVisible && view->kingdom == "wei");
    HEG_CHECK(visibleProbeInstances(*view) == QList<int>{headId});
    for (const AISkillView &skill : view->skills)
        HEG_CHECK(!skill.hasPrivateState && skill.state.isEmpty());

    owner->hideGeneral(true);
    HEG_CHECK(!owner->hasShownOneGeneral() && !fixture.room.isRoleRevealed(owner));
    HEG_CHECK(owner->getMark(limitMark) == 1 && owner->getSkillInstanceIds(limitSkill) == ids);
    world = fixture.room.buildAIWorldView(observer);
    view = findView(world, owner);
    HEG_CHECK(view && visibleProbeInstances(*view).isEmpty() && !view->publicMarks.contains(limitMark));

    // The same skill name on the head must not redirect a deputy activation.
    const SkillInstanceRef deputyRef(owner->objectName(), SkillInstanceKey(limitSkill, deputyId));
    HEG_CHECK(fixture.room.showGeneralForSkill(deputyRef));
    HEG_CHECK(!owner->hasShownGeneral() && owner->hasShownGeneral2());
    HEG_CHECK(owner->getSkillInstanceIds(limitSkill) == ids);
    owner->removeGeneral(false);
    HEG_CHECK(owner->getActualGeneral2Name() == "sujiangf");
    HEG_CHECK(owner->hasSkillInstance(limitSkill, headId));
    HEG_CHECK(!owner->hasSkillInstance(limitSkill, deputyId));
    HEG_CHECK(owner->getSkillInstanceStateValue(limitSkill, headId, "uses").toInt() == 3);
    return true;
}

bool majorityCareeristAndWinner()
{
    HegemonyFixture fixture;
    ServerPlayer *first = fixture.addWei();
    ServerPlayer *second = fixture.addWei();
    ServerPlayer *third = fixture.addWei();
    ServerPlayer *enemy = fixture.add("test_heg_shu_head", "test_heg_shu_deputy");
    fixture.prepare();
    first->showGeneral(true, false, false);
    HEG_CHECK(second->willBeFriendWith(first));
    // A deputy-only reveal counts toward the faction's half-room quota.
    second->showGeneral(false, false, false);
    HEG_CHECK(first->isFriendWith(second));
    HEG_CHECK(!third->willBeFriendWith(first));
    third->showGeneral(false, false, false);
    HEG_CHECK(third->getRole() == "careerist" && !third->isFriendWith(first));
    third->showGeneral(true, false, false);
    HEG_CHECK(third->getRole() == "careerist");
    HEG_CHECK(HegemonyRule::winner(&fixture.room).isEmpty());
    HEG_CHECK(!enemy->hasShownOneGeneral()); // Winner prediction cannot reveal an enemy.

    second->setAlive(false);
    third->setAlive(false);
    enemy->setAlive(false);
    RoomTestAccess::roster(fixture.room).rebuildAlive();
    const QStringList winners = HegemonyRule::winner(&fixture.room).split('+');
    HEG_CHECK(winners.size() == 2 && winners.contains(first->objectName())
        && winners.contains(second->objectName()));
    HEG_CHECK(!winners.contains(third->objectName()) && !winners.contains(enemy->objectName()));
    HEG_CHECK(first->hasShownAllGenerals());
    return true;
}

bool sovereignMustRevealAtStart()
{
    HegemonyFixture fixture;
    ServerPlayer *lord = fixture.add("lord_test_heg_wei", "test_heg_wei_deputy");
    ServerPlayer *ordinary = fixture.addWei();
    fixture.add("test_heg_shu_head", "test_heg_shu_deputy");
    fixture.prepare();

    HegemonyRule rule(nullptr);
    PhaseChangeStruct change;
    change.from = Player::NotActive;
    change.to = Player::RoundStart;
    QVariant data = QVariant::fromValue(change);
    rule.trigger(EventPhaseChanging, &fixture.room, lord, data);
    HEG_CHECK(!lord->hasShownOneGeneral());

    // Even after leaving the optional reveal window hidden, the sovereign must show.
    change.from = Player::RoundStart;
    change.to = Player::Start;
    data = QVariant::fromValue(change);
    rule.trigger(EventPhaseChanging, &fixture.room, lord, data);
    HEG_CHECK(lord->hasShownGeneral() && !lord->hasShownGeneral2());
    HEG_CHECK(lord->getGeneralName() == "lord_test_heg_wei");
    rule.trigger(EventPhaseChanging, &fixture.room, ordinary, data);
    HEG_CHECK(!ordinary->hasShownOneGeneral());
    return true;
}

bool sovereignRevealAndDeath()
{
    HegemonyFixture fixture;
    ServerPlayer *first = fixture.addWei();
    ServerPlayer *second = fixture.addWei();
    ServerPlayer *third = fixture.addWei();
    ServerPlayer *lord = fixture.add("lord_test_heg_wei", "test_heg_wei_even");
    fixture.prepare();
    first->showGeneral(true, false, false);
    second->showGeneral(true, false, false);
    third->showGeneral(true, false, false);
    HEG_CHECK(third->getRole() == "careerist"); // Concealed lord gives no public exemption.
    lord->showGeneral(true, false, false);
    HEG_CHECK(lord->isHegemonyLord() && third->getRole() != "careerist");
    HEG_CHECK(first->isFriendWith(third) && first->isFriendWith(lord));

    lord->setAlive(false);
    RoomTestAccess::roster(fixture.room).rebuildAlive();
    DeathStruct death;
    death.who = lord;
    death.damage = nullptr;
    QVariant data = QVariant::fromValue(death);
    HegemonyRule rule(nullptr);
    rule.trigger(BuryVictim, &fixture.room, lord, data);
    HEG_CHECK(first->getRole() == "careerist" && second->getRole() == "careerist"
        && third->getRole() == "careerist");
    HEG_CHECK(!first->isFriendWith(second));
    HEG_CHECK(HegemonyRule::winner(&fixture.room).isEmpty());
    return true;
}

bool preshowOwnerAndLifecycleContract()
{
    using namespace QSanProtocol;
    HegemonyFixture fixture;
    ServerPlayer *owner = fixture.addWei();
    ServerPlayer *observer = fixture.add("test_heg_shu_head", "test_heg_shu_deputy");
    fixture.prepare();
    owner->setState("online");
    const QList<int> ids = owner->getSkillInstanceIds(limitSkill);
    const QString head = SkillInstanceUtils::formatName(limitSkill, ids.first());
    const QString deputy = SkillInstanceUtils::formatName(limitSkill, ids.last());
    const SkillInstanceRef headRef(owner->objectName(), SkillInstanceKey(limitSkill, ids.first()));
    const SkillInstanceRef deputyRef(owner->objectName(), SkillInstanceKey(limitSkill, ids.last()));
    QList<ProtocolMessage> ownPackets, otherPackets;
    const auto ownConnection = QObject::connect(owner, &ServerPlayer::message_ready, owner,
        [&](const QByteArray &frame) {
            ProtocolMessage message;
            if (ProtocolCodecRouter().decode(frame, &message).success) ownPackets << message;
        });
    const auto otherConnection = QObject::connect(observer, &ServerPlayer::message_ready, observer,
        [&](const QByteArray &frame) {
            ProtocolMessage message;
            if (ProtocolCodecRouter().decode(frame, &message).success) otherPackets << message;
        });
    auto disconnect = qScopeGuard([&]() {
        QObject::disconnect(ownConnection);
        QObject::disconnect(otherConnection);
    });
    auto request = [&](const QString &name, const QVariant &enabled, bool drain = true) {
        ProtocolMessage message;
        message.type = ProtocolMessageType::Request;
        message.source = ProtocolEndpoint::Client;
        message.destination = ProtocolEndpoint::Room;
        message.command = S_COMMAND_PRESHOW;
        message.messageId = 1;
        message.hasPayload = true;
        message.payload = QVariantMap{{"schema_version", 1}, {"skill_name", name}, {"preshowed", enabled}};
        RoomTestAccess::dispatch(fixture.room, owner, message);
        if (drain) RoomTestAccess::processPendingPreshows(fixture.room);
    };
    HEG_CHECK(owner->canPreshowSkill(head) && owner->canPreshowSkill(deputy));
    HEG_CHECK(!fixture.room.isSkillPreshownForTrigger(headRef));
    request(head, true, false);
    HEG_CHECK(!owner->hasPreshowedSkill(head)); // Enqueue never touches gameplay state.
    request(head, false, false);
    RoomTestAccess::processPendingPreshows(fixture.room);
    HEG_CHECK(!owner->hasPreshowedSkill(head)); // Last value wins before the drain.
    request(limitSkill, true); // Ambiguous bare name cannot toggle both generals.
    request(head, QStringLiteral("true"));
    HEG_CHECK(!owner->hasPreshowedSkill(head));
    request(head, true);
    HEG_CHECK(owner->hasPreshowedSkill(head) && !owner->hasPreshowedSkill(deputy));
    HEG_CHECK(fixture.room.isSkillPreshownForTrigger(headRef)
        && !fixture.room.isSkillPreshownForTrigger(deputyRef));
    HEG_CHECK(!owner->hasShownOneGeneral() && otherPackets.isEmpty());
    const AIWorldView observerWorld = fixture.room.buildAIWorldView(observer);
    const AIPlayerView *view = findView(observerWorld, owner);
    HEG_CHECK(view && visibleProbeInstances(*view).isEmpty());
    // Updating public instance metadata cannot opt the deputy in or clear head.
    owner->upsertSkillInstance(*owner->findSkillInstance(limitSkill, ids.last()));
    HEG_CHECK(owner->hasPreshowedSkill(head) && !owner->hasPreshowedSkill(deputy));
    ownPackets.clear();
    RoomTestAccess::notifySkillInstanceSnapshot(fixture.room, owner);
    HEG_CHECK(ownPackets.size() == 2 && ownPackets.first().command == S_COMMAND_SKILL_INSTANCE
        && ownPackets.last().command == S_COMMAND_PRESHOW);
    const QVariantMap states = ownPackets.last().payload.toMap().value("states").toMap();
    HEG_CHECK(states.value(head).toBool() && !states.value(deputy).toBool());
    HEG_CHECK(!states.contains(limitSkill));
    owner->setDisableShow("d", "preshow-test");
    request(deputy, true);
    HEG_CHECK(!owner->hasPreshowedSkill(deputy));
    owner->removeDisableShow("preshow-test");
    request(head, false);
    HEG_CHECK(!fixture.room.isSkillPreshownForTrigger(headRef));
    request(deputy, true);
    const int helperId = owner->createSkillInstance("preshow-test-helper", SourceHelper, deputyRef, false);
    const SkillInstanceRef helper(owner->objectName(), SkillInstanceKey("preshow-test-helper", helperId));
    HEG_CHECK(fixture.room.isSkillPreshownForTrigger(helper));
    owner->showGeneral(false, false, false);
    HEG_CHECK(!owner->canPreshowSkill(deputy));
    owner->hideGeneral(false);
    HEG_CHECK(!owner->hasPreshowedSkill(deputy) && !fixture.room.isSkillPreshownForTrigger(helper));
    request(head, true, false);
    owner->removeSkillInstance(limitSkill, ids.first());
    RoomTestAccess::processPendingPreshows(fixture.room);
    HEG_CHECK(!owner->hasPreshowedSkill(head));
    // IDs restored by a fresh snapshot never inherit the previous private bit.
    const QList<SkillInstance> saved = owner->getSkillInstances();
    owner->setSkillPreshowed(deputy, true);
    owner->clearSkillInstances();
    for (const SkillInstance &instance : saved) owner->upsertSkillInstance(instance);
    HEG_CHECK(!owner->hasPreshowedSkill(deputy));
    return true;
}

bool sharedV2RevealContract()
{
    HegemonyFixture fixture;
    ServerPlayer *owner = fixture.addWei();
    ServerPlayer *eventPlayer = fixture.add("test_heg_shu_head", "test_heg_shu_deputy");
    fixture.prepare();
    auto *probe = new SharedRevealProbe;
    Sanguosha->addSkills({probe});
    owner->addSkill(probe->objectName(), true);
    fixture.room.getThread()->addTriggerSkill(probe);
    auto *choice = new RevealChoiceAI(owner);
    choice->setParent(owner);
    owner->setAI(choice);
    const int headId = owner->getSkillInstanceIds(probe->objectName()).first();
    const SkillInstanceRef head(owner->objectName(), SkillInstanceKey(probe->objectName(), headId));
    QVariant data;
    owner->setState("online");
    // An unprepared human skill must not create a request at all.
    fixture.room.getThread()->trigger(EventPhaseStart, &fixture.room, eventPlayer, data);
    HEG_CHECK(choice->requests == 0 && probe->effects == 0);
    owner->setState("robot");
    // One hidden compulsory option must reach its owner, not auto-reveal or
    // leak to the different player whose phase caused the event.
    fixture.room.getThread()->trigger(EventPhaseStart, &fixture.room, eventPlayer, data);
    HEG_CHECK(choice->requests == 1 && choice->lastOptional && choice->privateChoices);
    HEG_CHECK(probe->effects == 0 && fixture.room.isGeneralHiddenForSkill(head));

    probe->payment = false;
    choice->answer = SkillInstanceUtils::formatName(probe->objectName(), headId);
    fixture.room.getThread()->trigger(EventPhaseStart, &fixture.room, eventPlayer, data);
    HEG_CHECK(probe->effects == 0 && !owner->hasShownOneGeneral());

    probe->payment = true;
    owner->addSkill(probe->objectName(), false);
    const QList<int> ids = owner->getSkillInstanceIds(probe->objectName());
    HEG_CHECK(ids.size() == 2);
    const int deputyId = ids.last();
    const SkillInstanceRef deputy(owner->objectName(), SkillInstanceKey(probe->objectName(), deputyId));
    choice->answer = SkillInstanceUtils::formatName(probe->objectName(), deputyId);
    fixture.room.getThread()->trigger(EventPhaseStart, &fixture.room, eventPlayer, data);
    HEG_CHECK(probe->effects == 1 && !owner->hasShownGeneral() && owner->hasShownGeneral2());
    HEG_CHECK(fixture.room.isGeneralHiddenForSkill(head) && !fixture.room.isGeneralHiddenForSkill(deputy));
    HEG_CHECK(choice->privateChoices && choice->lastOptional);
    HEG_CHECK(owner->getSkillInstanceIds(probe->objectName()) == ids);
    // Invalid mandatory input falls back to the shown deputy, never the
    // still-concealed head with the same compulsory skill name.
    choice->answer = "invalid-reply";
    fixture.room.getThread()->trigger(EventPhaseStart, &fixture.room, eventPlayer, data);
    HEG_CHECK(probe->effects == 2 && !owner->hasShownGeneral() && owner->hasShownGeneral2());
    return true;
}

bool preshowSnapshotReplacementContract()
{
    HegemonyFixture fixture;
    ServerPlayer *owner = fixture.addWei();
    fixture.add("test_heg_shu_head", "test_heg_shu_deputy");
    fixture.prepare();
    const QList<int> ids = owner->getSkillInstanceIds(limitSkill);
    HEG_CHECK(ids.size() == 2);
    const QString first = SkillInstanceUtils::formatName(limitSkill, ids.first());
    const QString second = SkillInstanceUtils::formatName(limitSkill, ids.last());
    const int acquiredId = owner->createSkillInstance(limitSkill, SourceAcquired);
    const QString acquired = SkillInstanceUtils::formatName(limitSkill, acquiredId);
    owner->replacePreshowedSkillInstances({});

    int notifications = 0;
    const auto connection = QObject::connect(owner, &Player::skill_state_changed,
                                             owner, [&]() { ++notifications; });
    const auto disconnect = qScopeGuard([&]() { QObject::disconnect(connection); });
    HEG_CHECK(owner->replacePreshowedSkillInstances({first, second}));
    HEG_CHECK(notifications == 1 && owner->hasPreshowedSkill(first) && owner->hasPreshowedSkill(second));
    HEG_CHECK(!owner->replacePreshowedSkillInstances({second, first}));
    HEG_CHECK(notifications == 1); // Identical sets must not clear/reapply or notify again.

    // A missing key clears only that source; bare names, invalid IDs and acquired sources are ignored.
    HEG_CHECK(owner->replacePreshowedSkillInstances({second, limitSkill, acquired,
        limitSkill + "#0", limitSkill + "#0" + QString::number(ids.first()),
        SkillInstanceUtils::formatName(limitSkill, 1000000)}));
    HEG_CHECK(notifications == 2 && !owner->hasPreshowedSkill(first) && owner->hasPreshowedSkill(second));
    HEG_CHECK(owner->getPreshowedSkillInstances() == QSet<QString>{second});
    HEG_CHECK(owner->replacePreshowedSkillInstances({}));
    HEG_CHECK(notifications == 3 && owner->getPreshowedSkillInstances().isEmpty());
    HEG_CHECK(!owner->replacePreshowedSkillInstances({}));
    HEG_CHECK(notifications == 3);
    return true;
}

bool publicFactionAndOwnKingdomContract()
{
    HegemonyFixture fixture;
    ServerPlayer *owner = fixture.add("test_heg_dual_head", "test_heg_dual_deputy");
    ServerPlayer *other = fixture.add("test_heg_shu_head", "test_heg_shu_deputy");
    ServerPlayer *third = fixture.add("test_heg_wu_head", "test_heg_wu_deputy");
    fixture.add("test_heg_qun_head", "test_heg_qun_deputy");
    fixture.prepare();
    // A legal dual-kingdom pair can select a faction other than its head's first kingdom.
    owner->setHegemonyKingdom("shu");
    owner->setRole(HegemonyRule::getMappedRole("shu"));
    HEG_CHECK(owner->getActualGeneral1()->getKingdom() == "wei");
    HEG_CHECK(fixture.room.buildAIWorldView(owner).self.kingdom == "shu");
    const AIWorldView hiddenWorld = fixture.room.buildAIWorldView(other);
    const AIPlayerView *hidden = findView(hiddenWorld, owner);
    HEG_CHECK(hidden && hidden->kingdom.isEmpty() && !hidden->roleVisible);
    HEG_CHECK(AI::GetRelationHegemony(owner, owner) == AI::Friend);
    HEG_CHECK(AI::GetRelationHegemony(owner, other) == AI::Neutrality);

    other->setGeneralShowed(true);
    other->setKingdom("shu");
    other->setRole(HegemonyRule::getMappedRole("shu"));
    other->setShownRole(true);
    HEG_CHECK(owner->willBeFriendWith(other)); // Own prospective knowledge stays separate from public facts.
    HEG_CHECK(AI::GetRelationHegemony(owner, other) == AI::Neutrality);
    owner->setGeneralShowed(true);
    owner->setKingdom("shu");
    owner->setShownRole(true);
    HEG_CHECK(AI::GetRelationHegemony(owner, other) == AI::Friend);
    third->setGeneralShowed(true);
    third->setKingdom("wu");
    third->setRole(HegemonyRule::getMappedRole("wu"));
    third->setShownRole(true);
    HEG_CHECK(AI::GetRelationHegemony(owner, third) == AI::Enemy);

    // Recruitment is public allegiance even while both generals remain concealed.
    owner->setGeneralShowed(false);
    other->setGeneralShowed(false);
    owner->setKingdom("god");
    other->setKingdom("god");
    owner->setRole("careerist_contract_a");
    other->setRole("careerist_contract_a");
    HEG_CHECK(owner->isFriendWith(other));
    HEG_CHECK(AI::GetRelationHegemony(owner, other) == AI::Friend);
    HEG_CHECK(AI::GetRelationHegemony(other, owner) == AI::Friend);
    const AIWorldView recruitedWorld = fixture.room.buildAIWorldView(third);
    const AIPlayerView *recruited = findView(recruitedWorld, owner);
    HEG_CHECK(recruited && recruited->roleVisible && recruited->role == "careerist_contract_a");
    HEG_CHECK(recruited->kingdom.isEmpty() && recruited->generalName.isEmpty()
        && recruited->general2Name.isEmpty());
    owner->setGeneralShowed(true);
    owner->setKingdom("shu");
    const AIWorldView shownWorld = fixture.room.buildAIWorldView(third);
    const AIPlayerView *shown = findView(shownWorld, owner);
    HEG_CHECK(shown && shown->kingdom == "shu" && shown->role == "careerist_contract_a");
    owner->setGeneralShowed(false);
    owner->setKingdom("god");
    other->setRole("careerist_contract_b");
    HEG_CHECK(!owner->isFriendWith(other));
    HEG_CHECK(AI::GetRelationHegemony(owner, other) == AI::Enemy);
    other->setShownRole(false);
    HEG_CHECK(AI::GetRelationHegemony(owner, other) == AI::Neutrality);

    owner->setGeneralShowed(true);
    owner->setKingdom("shu");
    owner->setRole("careerist");
    other->setGeneralShowed(true);
    other->setKingdom("shu");
    other->setRole("careerist");
    other->setShownRole(true);
    HEG_CHECK(AI::GetRelationHegemony(owner, owner) == AI::Friend);
    HEG_CHECK(AI::GetRelationHegemony(owner, other) == AI::Enemy);
    return true;
}

bool useRuleReward(HegemonyFixture &fixture, ServerPlayer *owner, const QString &name,
                   const QList<ServerPlayer *> &targets = {})
{
    const auto *skill = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getSkill(name));
    const QList<int> instances = owner->getSkillInstanceIds(name);
    HEG_CHECK(skill && instances.size() == 1);
    ActiveSkillRequest request;
    request.initiator = owner;
    request.reason = CardUseStruct::CARD_USE_REASON_PLAY;
    request.activationRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(name, instances.first()));
    for (ServerPlayer *target : targets) request.selectedTargetNames << target->objectName();
    const Card *card = RoomTestAccess::resolveActiveRequest(fixture.room, owner, skill, request);
    HEG_CHECK(card && card->isKindOf("ActiveSkillCard"));
    CardUseStruct use(card, owner);
    use.to = targets;
    use.activationRef = request.activationRef;
    use.m_validateTargets = true;
    const_cast<Card *>(card)->deleteLater();
    // Exercise admission, payment and effect through the production V2 pipeline.
    HEG_CHECK(fixture.room.useCard(use) && use.cardFinished);
    HEG_CHECK(!skill->canActivate(request));
    HEG_CHECK(!RoomTestAccess::resolveActiveRequest(fixture.room, owner, skill, request));
    return true;
}

class RevealTimingProbe : public GameRule
{
public:
    RevealTimingProbe() : GameRule(nullptr)
    {
        setObjectName("test_heg_reveal_timing");
        events.clear();
        events << TurnStart << GeneralShown << EventPhaseProceeding;
    }

    int getPriority(TriggerEvent) const override { return 10; }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event == TurnStart) {
            // Keep a real outer turn frame alive while running the phase machinery.
            player->play({Player::RoundStart, Player::Play, Player::NotActive});
            return true;
        }
        if (event == GeneralShown) {
            room->setTag("test_reward_waited_for_reveal", player->getMark("@firstshow") == 0);
        } else if (event == EventPhaseProceeding && player->getPhase() == Player::Play) {
            room->setTag("test_reward_ready_in_play", player->getMark("@firstshow") == 1);
            return true; // No interactive play loop is needed to observe this boundary.
        }
        return false;
    }
};

bool revealRewardSettlesBeforePlay()
{
    HegemonyFixture fixture;
    ServerPlayer *owner = fixture.addWei();
    fixture.add("test_heg_shu_head", "test_heg_shu_deputy");
    fixture.add("test_heg_wu_head", "test_heg_wu_deputy");
    fixture.add("test_heg_qun_head", "test_heg_qun_deputy");
    fixture.prepare();
    fixture.ready();
    fixture.installRule();
    fixture.room.registerTestOverride(owner, "choice", "HegemonyReveal", QStringLiteral("head"));
    auto *probe = new RevealTimingProbe;
    probe->setParent(fixture.room.getThread());
    fixture.room.getThread()->addTriggerSkill(probe);
    QVariant data;
    fixture.room.getThread()->trigger(TurnStart, &fixture.room, owner, data);
    HEG_CHECK(fixture.room.getTag("test_reward_waited_for_reveal").toBool());
    HEG_CHECK(fixture.room.getTag("test_reward_ready_in_play").toBool());
    HEG_CHECK(owner->getMark("@firstshow") == 1);
    return true;
}

bool revealRewardsAreGrantedOnce()
{
    HegemonyFixture fixture;
    ServerPlayer *owner = fixture.addWei();
    ServerPlayer *other = fixture.add("test_heg_shu_head", "test_heg_shu_deputy");
    fixture.add("test_heg_wu_head", "test_heg_wu_deputy");
    fixture.add("test_heg_qun_head", "test_heg_qun_deputy");
    fixture.prepare();
    fixture.ready();
    HEG_CHECK(owner->getGeneralMaxHp() == 3 && owner->getMaxHp() == 3);
    HEG_CHECK(owner->getMark("HalfMaxHpLeft") == 1 && owner->getMark("CompanionEffect") == 1);
    HEG_CHECK(other->getMark("HalfMaxHpLeft") == 0 && other->getMark("CompanionEffect") == 0);
    fixture.room.registerTestOverride(owner, "choice", "heg_firstshow_see", QStringLiteral("head_general"));
    fixture.installRule();

    const int initialCards = owner->getHandcardNum();
    HEG_CHECK(owner->getMark("@firstshow") == 0 && owner->getMark("@halfmaxhp") == 0
        && owner->getMark("@companion") == 0);
    owner->showGeneral(true);
    // Revealing grants a token, not an immediate draw or an implicit payment.
    HEG_CHECK(owner->getHandcardNum() == initialCards && owner->getMark("@firstshow") == 1);
    HEG_CHECK(fixture.room.getTag("TheFirstToShowRewarded").toBool());
    HEG_CHECK(owner->getMark("HalfMaxHpLeft") == 1 && owner->getMark("CompanionEffect") == 1);
    owner->showGeneral(false);
    HEG_CHECK(owner->getHandcardNum() == initialCards);
    HEG_CHECK(owner->getMark("HalfMaxHpLeft") == 0 && owner->getMark("CompanionEffect") == 0);
    HEG_CHECK(owner->getMark("@halfmaxhp") == 1 && owner->getMark("@companion") == 1);

    const auto *companion = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getSkill("heg_companion"));
    HEG_CHECK(companion);
    ActiveSkillRequest rescue;
    rescue.initiator = owner;
    rescue.reason = CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    // Both saving another player and self-rescue use named response patterns.
    for (const QString &pattern : {QStringLiteral("peach"), QStringLiteral("peach+analeptic")}) {
        rescue.pattern = pattern;
        HEG_CHECK(companion->canActivate(rescue));
        std::unique_ptr<const Card> peach(companion->createCard(rescue));
        HEG_CHECK(peach && peach->isKindOf("Peach") && peach->subcardsLength() == 0);
    }
    owner->setFlags("Global_PreventPeach");
    HEG_CHECK(!companion->canActivate(rescue));
    owner->setFlags("-Global_PreventPeach");
    rescue.pattern = QStringLiteral("jink");
    HEG_CHECK(!companion->canActivate(rescue));

    const auto *firstShow = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getSkill("heg_firstshow"));
    HEG_CHECK(firstShow);
    ActiveSkillRequest invalid;
    invalid.initiator = owner;
    invalid.reason = CardUseStruct::CARD_USE_REASON_PLAY;
    HEG_CHECK(!firstShow->targetsFeasible(invalid, {})); // A concealed opponent must be chosen.
    HEG_CHECK(owner->getMark("@firstshow") == 1 && owner->getHandcardNum() == initialCards);

    owner->setPhase(Player::Play);
    fixture.room.getRoomState()->setCurrentCardUseReason(CardUseStruct::CARD_USE_REASON_PLAY);
    HEG_CHECK(useRuleReward(fixture, owner, "heg_firstshow", {other}));
    const int filledCards = qMax(4, initialCards);
    HEG_CHECK(owner->getMark("@firstshow") == 0 && owner->getHandcardNum() == filledCards);
    HEG_CHECK(!other->hasShownOneGeneral()); // The viewing reward does not publicly reveal its target.
    HEG_CHECK(useRuleReward(fixture, owner, "heg_halfmaxhp"));
    HEG_CHECK(owner->getMark("@halfmaxhp") == 0 && owner->getHandcardNum() == filledCards + 1);
    HEG_CHECK(useRuleReward(fixture, owner, "heg_companion"));
    HEG_CHECK(owner->getMark("@companion") == 0 && owner->getHandcardNum() == filledCards + 3);

    owner->hideGeneral(true);
    owner->hideGeneral(false);
    owner->showGeneral(true);
    owner->showGeneral(false);
    HEG_CHECK(owner->getHandcardNum() == filledCards + 3);
    HEG_CHECK(owner->getMark("@firstshow") == 0 && owner->getMark("@halfmaxhp") == 0
        && owner->getMark("@companion") == 0);
    const int otherCards = other->getHandcardNum();
    other->showGeneral(true);
    HEG_CHECK(other->getHandcardNum() == otherCards && other->getMark("@firstshow") == 0);
    return true;
}

bool targetModSelectionAndRevealContract()
{
    // A normal Slash remains legal with the hidden target-mod instances.
    {
        HegemonyFixture fixture;
        ServerPlayer *owner = fixture.addWei();
        ServerPlayer *target = fixture.add("test_heg_shu_head", "test_heg_shu_deputy");
        fixture.add("test_heg_wu_head", "test_heg_wu_deputy");
        fixture.add("test_heg_qun_head", "test_heg_qun_deputy");
        fixture.prepare();
        fixture.ready();
        owner->setPhase(Player::Play);
        fixture.room.getRoomState()->setCurrentCardUseReason(CardUseStruct::CARD_USE_REASON_PLAY);
        TargetModRevealProbe::mode = TargetModRevealProbe::ResidueOnly;
        std::unique_ptr<Card> slash(Sanguosha->cloneCard(QStringLiteral("slash")));
        CardUseStruct use(slash.get(), owner, QList<ServerPlayer *>() << target);
        use.m_validateTargets = true;
        fixture.room.prepareTargetModSkillReveal(use);
        fixture.room.planTargetModSkillReveal(use);
        owner->addHistory(QStringLiteral("Slash"));
        HEG_CHECK(fixture.room.showRequiredTargetModSkillsV2(use));
        HEG_CHECK(!owner->hasShownGeneral() && !owner->hasShownGeneral2());
    }

    // A second Slash needs one residue contributor; either same-named innate
    // copy is sufficient and the choice is made only after planning.
    {
        HegemonyFixture fixture;
        ServerPlayer *owner = fixture.addWei();
        ServerPlayer *target = fixture.add("test_heg_shu_head", "test_heg_shu_deputy");
        fixture.add("test_heg_wu_head", "test_heg_wu_deputy");
        fixture.add("test_heg_qun_head", "test_heg_qun_deputy");
        fixture.prepare();
        fixture.ready();
        owner->setPhase(Player::Play);
        fixture.room.getRoomState()->setCurrentCardUseReason(CardUseStruct::CARD_USE_REASON_PLAY);
        TargetModRevealProbe::mode = TargetModRevealProbe::ResidueOnly;
        owner->addHistory(QStringLiteral("Slash"));
        fixture.room.registerTestOverride(owner, "choice", "HegemonyReveal", QStringLiteral("head"));
        std::unique_ptr<Card> slash(Sanguosha->cloneCard(QStringLiteral("slash")));
        CardUseStruct use(slash.get(), owner, QList<ServerPlayer *>() << target);
        use.m_validateTargets = true;
        fixture.room.prepareTargetModSkillReveal(use);
        // Planning is frozen after the history change and before the reveal gate.
        fixture.room.planTargetModSkillReveal(use);
        owner->addHistory(QStringLiteral("Slash"));
        HEG_CHECK(fixture.room.showRequiredTargetModSkillsV2(use));
        HEG_CHECK(owner->hasShownGeneral() != owner->hasShownGeneral2());
    }

    // Removing the planned source and acquiring a new same-named instance
    // cannot substitute for the original leaf/root pair.
    {
        HegemonyFixture fixture;
        ServerPlayer *owner = fixture.addWei();
        ServerPlayer *target = fixture.add("test_heg_shu_head", "test_heg_shu_deputy");
        fixture.add("test_heg_wu_head", "test_heg_wu_deputy");
        fixture.add("test_heg_qun_head", "test_heg_qun_deputy");
        fixture.prepare();
        fixture.ready();
        owner->setPhase(Player::Play);
        fixture.room.getRoomState()->setCurrentCardUseReason(CardUseStruct::CARD_USE_REASON_PLAY);
        TargetModRevealProbe::mode = TargetModRevealProbe::ResidueOnly;
        owner->addHistory(QStringLiteral("Slash"));
        std::unique_ptr<Card> slash(Sanguosha->cloneCard(QStringLiteral("slash")));
        CardUseStruct use(slash.get(), owner, QList<ServerPlayer *>() << target);
        use.m_validateTargets = true;
        fixture.room.prepareTargetModSkillReveal(use);
        fixture.room.planTargetModSkillReveal(use);
        const QList<int> originalIds = owner->getSkillInstanceIds(QStringLiteral("test_heg_target_mod"));
        for (int id : originalIds)
            owner->removeSkillInstance(QStringLiteral("test_heg_target_mod"), id);
        owner->addSkill(QStringLiteral("test_heg_target_mod"), true);
        HEG_CHECK(!fixture.room.showRequiredTargetModSkillsV2(use));
    }

    // A helper's own invalidity must be rechecked after planning, even when
    // its innate root remains valid and revealable.
    {
        HegemonyFixture fixture;
        ServerPlayer *owner = fixture.addWei();
        ServerPlayer *target = fixture.add("test_heg_shu_head", "test_heg_shu_deputy");
        fixture.prepare();
        fixture.ready();
        owner->setPhase(Player::Play);
        fixture.room.getRoomState()->setCurrentCardUseReason(CardUseStruct::CARD_USE_REASON_PLAY);
        TargetModRevealProbe::mode = TargetModRevealProbe::ResidueOnly;
        const QString name = "test_heg_target_mod";
        for (int id : owner->getSkillInstanceIds(name)) owner->removeSkillInstance(name, id);
        const int root = owner->getSkillInstanceIds(limitSkill).first();
        const int helper = owner->createSkillInstance(name, SourceHelper, limitSkill, root, false);
        owner->addHistory("Slash");
        std::unique_ptr<Card> slash(Sanguosha->cloneCard("slash"));
        CardUseStruct use(slash.get(), owner, target);
        use.m_validateTargets = true;
        fixture.room.prepareTargetModSkillReveal(use);
        fixture.room.planTargetModSkillReveal(use);
        HEG_CHECK(!use.targetModReveal.options.isEmpty());
        owner->setTag("SkillInvalidityRecords", QStringList{
            SkillInstanceUtils::formatName(name, helper) + "|probe|test"});
        HEG_CHECK(!fixture.room.showRequiredTargetModSkillsV2(use));
        HEG_CHECK(!owner->hasShownOneGeneral());
    }

    // Collateral's accepted two-person draft remains valid when onUse turns
    // it into a single killer. Do not revalidate that resolved list as a draft.
    {
        HegemonyFixture fixture;
        ServerPlayer *owner = fixture.addWei();
        ServerPlayer *first = fixture.add("test_heg_shu_head", "test_heg_shu_deputy");
        ServerPlayer *second = fixture.add("test_heg_wu_head", "test_heg_wu_deputy");
        fixture.add("test_heg_qun_head", "test_heg_qun_deputy");
        fixture.prepare();
        fixture.ready();
        owner->setPhase(Player::Play);
        fixture.room.getRoomState()->setCurrentCardUseReason(CardUseStruct::CARD_USE_REASON_PLAY);
        const Card *weapon = nullptr;
        for (int id = 0; id < Sanguosha->getCardCount(); ++id) {
            const Card *candidate = Sanguosha->getEngineCard(id);
            if (candidate != nullptr && candidate->objectName() == QStringLiteral("crossbow")) {
                weapon = candidate;
                break;
            }
        }
        HEG_CHECK(weapon != nullptr);
        first->setEquip(weapon);
        TargetModRevealProbe::mode = TargetModRevealProbe::None;
        std::unique_ptr<Card> collateral(Sanguosha->cloneCard(QStringLiteral("collateral")));
        CardUseStruct use(collateral.get(), owner, QList<ServerPlayer *>() << first << second);
        use.m_validateTargets = true;
        HEG_CHECK(RoomTestAccess::cardTargetsLegal(fixture.room, use));
        fixture.room.prepareTargetModSkillReveal(use);
        fixture.room.planTargetModSkillReveal(use);
        use.to = QList<ServerPlayer *>() << first;
        HEG_CHECK(fixture.room.showRequiredTargetModSkillsV2(use));
        HEG_CHECK(!owner->hasShownOneGeneral());
    }

    // Three Slash targets require both +1 copies. The original no-distance
    // selection flag may already be cleared when the delayed gate is reached.
    {
        HegemonyFixture fixture;
        ServerPlayer *owner = fixture.addWei();
        ServerPlayer *a = fixture.add("test_heg_shu_head", "test_heg_shu_deputy");
        ServerPlayer *b = fixture.add("test_heg_wu_head", "test_heg_wu_deputy");
        ServerPlayer *c = fixture.add("test_heg_qun_head", "test_heg_qun_deputy");
        fixture.prepare();
        fixture.ready();
        owner->setPhase(Player::Play);
        owner->setFlags("slashNoDistanceLimit");
        fixture.room.getRoomState()->setCurrentCardUseReason(CardUseStruct::CARD_USE_REASON_PLAY);
        TargetModRevealProbe::mode = TargetModRevealProbe::ExtraTargetOnly;
        std::unique_ptr<Card> slash(Sanguosha->cloneCard("slash"));
        CardUseStruct use(slash.get(), owner, QList<ServerPlayer *>{a, b, c});
        use.m_validateTargets = true;
        HEG_CHECK(RoomTestAccess::cardTargetsLegal(fixture.room, use));
        fixture.room.prepareTargetModSkillReveal(use);
        fixture.room.planTargetModSkillReveal(use);
        owner->setFlags("-slashNoDistanceLimit");
        use.skipSkillEffect = true;
        HEG_CHECK(fixture.room.showRequiredTargetModSkillsV2(use));
        HEG_CHECK(!owner->hasShownOneGeneral());
        use.skipSkillEffect = false;
        HEG_CHECK(fixture.room.showRequiredTargetModSkillsV2(use));
        HEG_CHECK(owner->hasShownAllGenerals());
    }

    // A to-dependent distance correction previews only its eligible target.
    // A disabled head cannot be selected as a substitute for the deputy.
    {
        HegemonyFixture fixture;
        ServerPlayer *owner = fixture.addWei();
        ServerPlayer *near = fixture.add("test_heg_shu_head", "test_heg_shu_deputy");
        ServerPlayer *far = fixture.add("test_heg_wu_head", "test_heg_wu_deputy");
        fixture.add("test_heg_qun_head", "test_heg_qun_deputy");
        fixture.prepare();
        fixture.ready();
        owner->setPhase(Player::Play);
        owner->setDisableShow("h", "test-distance");
        far->setProperty("test_far_target", true);
        fixture.room.getRoomState()->setCurrentCardUseReason(CardUseStruct::CARD_USE_REASON_PLAY);
        TargetModRevealProbe::mode = TargetModRevealProbe::DistanceOnly;
        std::unique_ptr<Card> slash(Sanguosha->cloneCard("slash"));
        HEG_CHECK(Sanguosha->correctCardTarget(TargetModSkill::DistanceLimit, owner, slash.get(), near) == 0);
        HEG_CHECK(Sanguosha->correctCardTarget(TargetModSkill::DistanceLimit, owner, slash.get(), far) == 100);
        CardUseStruct use(slash.get(), owner, far);
        use.m_validateTargets = true;
        HEG_CHECK(RoomTestAccess::cardTargetsLegal(fixture.room, use));
        fixture.room.prepareTargetModSkillReveal(use);
        fixture.room.planTargetModSkillReveal(use);
        HEG_CHECK(fixture.room.showRequiredTargetModSkillsV2(use));
        HEG_CHECK(!owner->hasShownGeneral() && owner->hasShownGeneral2());

        // Explicit server-created uses retain their target bypass contract.
        use.m_validateTargets = false;
        fixture.room.prepareTargetModSkillReveal(use);
        HEG_CHECK(use.targetModReveal.sources.isEmpty());
        HEG_CHECK(fixture.room.showRequiredTargetModSkillsV2(use));
    }
    TargetModRevealProbe::mode = TargetModRevealProbe::None;
    return true;
}

#undef HEG_CHECK

}

int runHegemonyRulesTests()
{
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "Hegemony test engine initialization failed:" << error;
        return 1;
    }
    const bool hegemony = Config.EnableHegemony;
    const bool second = Config.Enable2ndGeneral;
    const int delay = Config.AIDelay;
    const ServerInfoStruct previousServerInfo = ServerInfo;
    const QVariantMap previousOverrides = Config.valueOverrides();
    auto restore = qScopeGuard([=]() {
        Config.EnableHegemony = hegemony;
        Config.Enable2ndGeneral = second;
        Config.AIDelay = delay;
        Config.setValueOverrides(previousOverrides);
        ServerInfo = previousServerInfo;
    });
    Config.EnableHegemony = true;
    Config.Enable2ndGeneral = true;
    Config.AIDelay = 0;
    // AI snapshots use the negotiated mode flags as well as room settings.
    ServerInfo.EnableHegemony = true;
    ServerInfo.Enable2ndGeneral = true;
    QVariantMap overrides = previousOverrides;
    overrides.insert("RewardTheFirstShowingPlayer", true);
    Config.setValueOverrides(overrides);

    if (!revealPreservesInstancesAndPrivacy()) return 2;
    if (!majorityCareeristAndWinner()) return 3;
    if (!sovereignRevealAndDeath()) return 4;
    if (!sovereignMustRevealAtStart()) return 4;
    if (!revealRewardsAreGrantedOnce()) return 5;
    if (!revealRewardSettlesBeforePlay()) return 5;
    if (!sharedV2RevealContract()) return 6;
    if (!preshowOwnerAndLifecycleContract()) return 7;
    if (!targetModSelectionAndRevealContract()) return 8;
    if (!publicFactionAndOwnKingdomContract()) return 9;
    if (!preshowSnapshotReplacementContract()) return 10;
    qInfo() << "HEGEMONY_RULES_TEST_RESULT status=PASS";
    return 0;
}
