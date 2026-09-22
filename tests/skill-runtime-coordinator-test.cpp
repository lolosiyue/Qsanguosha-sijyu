#include "engine-bootstrap.h"
#include "ai.h"
#include "engine.h"
#include "room-test-access.h"
#include "room.h"
#include "roomthread.h"
#include "serverplayer.h"
#include "skill.h"
#include "skill-instance-utils.h"
#include "lua-runtime.h"
#include "lua.hpp"
#include "protocol/protocol-runtime.h"
#include "protocol/skill-instance-message.h"
#include "settings.h"
#include "server-info.h"
#include "standard-cards.h"
#include "room-runtime.h"

#include <QCoreApplication>
#include <QDebug>
#include <QScopeGuard>
#include <memory>

namespace {

class TestLimitedSkill : public ViewAsSkillV2
{
public:
    TestLimitedSkill()
        : ViewAsSkillV2(QStringLiteral("test-skill-runtime-limited"))
    {
    }

    LimitScope getLimitScope() const override
    {
        return Limit_Turn;
    }
};

static bool amountCorrectStateAndInvalidity(Room &room, ServerPlayer *owner,
                                            DistanceSkillV2 &skill,
                                            const SkillInstanceRef &ref,
                                            int otherInstanceId)
{
    bool ok = false;
    if (room.getSkillInstanceAmount(ref, &ok) != skill.getBaseAmount() || !ok)
        return false;
    if (!room.setSkillInstanceAmount(owner, ref, 5, QStringLiteral("test"))
        || room.getSkillInstanceAmount(ref) != 5)
        return false;
    if (!room.addSkillInstanceAmount(owner, ref, -2, QStringLiteral("test"))
        || room.getSkillInstanceAmount(ref) != 3)
        return false;
    if (!room.resetSkillInstanceAmount(owner, ref, QStringLiteral("test"))
        || room.getSkillInstanceAmount(ref) != skill.getBaseAmount())
        return false;

    if (!room.setSkillInstanceCorrectState(owner, ref, QStringLiteral("offset"), 7)
        || owner->getSkillInstanceCorrectStateValue(skill.objectName(),
                                                     ref.key.instanceID,
                                                     QStringLiteral("offset")).toInt() != 7)
        return false;
    if (!room.removeSkillInstanceCorrectState(owner, ref, QStringLiteral("offset"))
        || owner->getSkillInstanceCorrectState(skill.objectName(),
                                                ref.key.instanceID).contains("offset"))
        return false;
    if (!room.setSkillInstanceCorrectState(owner, ref, QStringLiteral("first"), 1)
        || !room.setSkillInstanceCorrectState(owner, ref, QStringLiteral("second"), 2)
        || !room.clearSkillInstanceCorrectState(owner, ref)
        || !owner->getSkillInstanceCorrectState(skill.objectName(),
                                                 ref.key.instanceID).isEmpty())
        return false;

    room.addSkillInvalidity(owner, skill.objectName(), QStringLiteral("source"),
                            QStringLiteral("test"), ref.key.instanceID);
    if (!owner->isSkillInvalid(skill.objectName(), ref.key.instanceID)
        || owner->isSkillInvalid(skill.objectName(), otherInstanceId))
        return false;
    room.removeSkillInvalidity(owner, skill.objectName(), QStringLiteral("source"),
                               QStringLiteral("test"), ref.key.instanceID);
    return !owner->isSkillInvalid(skill.objectName(), ref.key.instanceID);
}

static bool usageReservation(Room &room, ServerPlayer *owner,
                             const TestLimitedSkill &skill, int instanceId)
{
    SkillContext context;
    context.initiator = owner;
    context.invoker = owner;
    context.owner = owner;
    context.instanceID = instanceId;
    context.activationRef = SkillInstanceRef(
        owner->objectName(), SkillInstanceKey(skill.objectName(), instanceId));
    context.sourceRef = context.activationRef;

    const bool firstReserved = RoomTestAccess::reserveUsage(room, &skill, context);
    const bool duplicateReserved = RoomTestAccess::reserveUsage(room, &skill, context);
    if (!firstReserved || duplicateReserved)
        return false;
    RoomTestAccess::releaseUsage(room, &skill, context);
    const bool reservedAfterRelease = RoomTestAccess::reserveUsage(room, &skill, context);
    if (!reservedAfterRelease)
        return false;
    RoomTestAccess::commitUsage(room, &skill, context);
    const bool reservedAfterCommit = RoomTestAccess::reserveUsage(room, &skill, context);
    return !reservedAfterCommit;
}

static bool executionRegistry(Room &room, ServerPlayer *owner,
                              const SkillInstanceRef &sourceRef)
{
    qint64 executionId = 0;
    {
        SkillContext context;
        context.skill_name = sourceRef.key.skillName;
        context.sourceRef = sourceRef;
        context.activationRef = sourceRef;
        context.initiator = owner;
        context.invoker = owner;
        context.owner = owner;
        context.instanceID = sourceRef.key.instanceID;

        SkillExecutionRegistry::Guard guard = room.beginSkillExecution(
            context, QStringLiteral("backing"));
        executionId = guard.executionID();
        SkillExecutionRegistry::Entry *entry = room.findSkillExecution(executionId);
        if (executionId <= 0 || !entry || context.original_data != &entry->backingData)
            return false;

        SkillContext updated = room.getSkillExecutionContext(executionId);
        updated.amount = 9;
        room.setSkillExecutionContext(executionId, updated);
        if (room.getSkillExecutionContext(executionId).amount != 9
            || !guard.finish(SkillExecutionCompleted)
            || guard.finish(SkillExecutionCompleted))
            return false;
    }
    return room.findSkillExecution(executionId) == nullptr;
}


#define SHIMING_CHECK(condition) do { if (!(condition)) { \
    qCritical() << "Shiming check failed at line" << __LINE__ << #condition; return false; \
} } while (false)

class ShimingCallbackProbe : public TriggerSkillV2
{
public:
    ShimingCallbackProbe() : TriggerSkillV2("test-shiming-callback") { shiming_skill = true; }
    bool trigger(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return false; }
    void onShimingSuccess(Room *, ServerPlayer *, const SkillInstanceRef &ref) const override
    { successes << ref; }
    void onShimingFail(Room *, ServerPlayer *, const SkillInstanceRef &ref) const override
    { failures << ref; }
    mutable QList<SkillInstanceRef> successes, failures;
};

class ShimingEventProbe : public TriggerSkill
{
public:
    ShimingEventProbe() : TriggerSkill("test-shiming-events")
    {
        global = true;
        events << EventShimingSuccess << EventShimingFail;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const SkillInstanceRef ref = data.value<SkillInstanceRef>();
        refs << ref;
        eventsSeen << event;
        ownerMatches = ownerMatches && ref.ownerObjectName == player->objectName();
        if (removeRef == ref) player->removeSkillInstance(ref.key.skillName, ref.key.instanceID);
        if (resetRef == ref) {
            resetRef = SkillInstanceRef();
            room->setShimingStatus(ref, 0);
            room->setShimingStatus(ref, 1);
        }
        return false;
    }
    mutable QList<SkillInstanceRef> refs;
    mutable QList<TriggerEvent> eventsSeen;
    mutable bool ownerMatches = true;
    SkillInstanceRef removeRef;
    mutable SkillInstanceRef resetRef;
};

static bool shimingInstancePipeline()
{
    ShimingCallbackProbe skill;
    ShimingEventProbe probe;
    Sanguosha->addSkills(QList<const Skill *>() << &skill);
    Room room(nullptr, "02_1v1");
    RoomTestAccess::attachThread(room, &probe);
    ServerPlayer *owner = RoomTestAccess::addOrdinaryPlayer(room, "shiming-owner");
    ServerPlayer *other = RoomTestAccess::addOrdinaryPlayer(room, "shiming-other");
    room.setCurrent(owner);
    auto acquire = [&](ServerPlayer *p) {
        return SkillInstanceRef(p->objectName(), SkillInstanceKey(skill.objectName(),
            room.acquireSkill(p, skill.objectName(), false, false, false)));
    };
    const SkillInstanceRef first = acquire(owner), second = acquire(owner), foreign = acquire(other);
    QList<SkillInstanceMessage> ownerPackets, otherPackets;
    int logs = 0;
    bool logLabelsMatch = true;
    auto watch = [&](ServerPlayer *p, QList<SkillInstanceMessage> &packets) {
        QObject::connect(p, &ServerPlayer::message_ready, p, [&, p](const QByteArray &frame) {
            QSanProtocol::ProtocolMessage message;
            if (!QSanProtocol::ProtocolCodecRouter().decode(frame, &message).success) return;
            if (message.command == QSanProtocol::S_COMMAND_SKILL_INSTANCE) {
                SkillInstanceMessage state;
                if (state.tryParse(message.payload)) packets << state;
            }
            if (p == owner && message.command == QSanProtocol::S_COMMAND_LOG_SKILL) {
                ++logs;
                const QVariantMap body = message.payload.toMap();
                logLabelsMatch = logLabelsMatch && body.value("from_player").toString() == owner->objectName()
                    && body.value("arguments").toList().value(0).toString() == skill.objectName();
            }
        });
    };
    watch(owner, ownerPackets);
    watch(other, otherPackets);
    SHIMING_CHECK(room.getShimingStatus(first) == 0 && room.getShimingStatus(second) == 0);
    SHIMING_CHECK(room.setShimingStatus(first, 1));
    SHIMING_CHECK(room.getShimingStatus(first) == 1 && room.getShimingStatus(second) == 0);
    SHIMING_CHECK(room.sendShimingLog(second, false, 4));
    SHIMING_CHECK(room.getShimingStatus(first) == 1 && room.getShimingStatus(second) == 2);
    SHIMING_CHECK(room.getShimingStatus(foreign) == 0);
    SHIMING_CHECK(skill.successes == QList<SkillInstanceRef>{first});
    SHIMING_CHECK(skill.failures == QList<SkillInstanceRef>{second});
    SHIMING_CHECK(probe.refs == (QList<SkillInstanceRef>{first, second}) && probe.ownerMatches);
    SHIMING_CHECK(probe.eventsSeen == (QList<TriggerEvent>{EventShimingSuccess, EventShimingFail}));
    SHIMING_CHECK(!room.sendShimingLog(first) && !room.sendShimingLog(second, false));
    SHIMING_CHECK(logs == 2 && logLabelsMatch && ownerPackets.size() == 2 && otherPackets.isEmpty());
    SHIMING_CHECK(ownerPackets[0].instanceId == first.key.instanceID
        && ownerPackets[0].value.toMap().value("shiming_status").toInt() == 1);
    SHIMING_CHECK(ownerPackets[1].instanceId == second.key.instanceID
        && ownerPackets[1].value.toMap().value("shiming_status").toInt() == 2);
    SHIMING_CHECK(owner->getMark(skill.objectName()) == 0
        && owner->getMark(skill.objectName() + "__success") == 0);
    RoomTestAccess::notifySkillInstanceSnapshot(room, owner);
    RoomTestAccess::notifySkillInstanceSnapshot(room, other);
    const SkillInstanceMessage snapshot = ownerPackets.last(), observerSnapshot = otherPackets.last();
    SHIMING_CHECK(snapshot.action == SkillInstanceMessage::Snapshot);
    int ownStates = 0;
    for (const auto &entry : snapshot.entries) {
        if (entry.ownerName == owner->objectName()) {
            ++ownStates;
            SHIMING_CHECK(entry.privateState.value("shiming_status").toInt()
                == (entry.instance.instanceID == first.key.instanceID ? 1 : 2));
        }
    }
    SHIMING_CHECK(ownStates == 2);
    for (const auto &entry : observerSnapshot.entries)
        if (entry.ownerName == owner->objectName()) SHIMING_CHECK(entry.privateState.isEmpty());
    const int oldLogs = logs;
    SHIMING_CHECK(!room.setShimingStatus(SkillInstanceRef(owner->objectName(), SkillInstanceKey(skill.objectName(), 0)), 1));
    SHIMING_CHECK(!room.setShimingStatus(SkillInstanceRef("missing", first.key), 1));
    SHIMING_CHECK(!room.setShimingStatus(second, 3) && logs == oldLogs);
    SHIMING_CHECK(owner->removeSkillInstance(skill.objectName(), first.key.instanceID));
    SHIMING_CHECK(!room.setShimingStatus(first, 2) && room.getShimingStatus(second) == 2);
    const SkillInstanceRef third = acquire(owner);
    SHIMING_CHECK(third.key.instanceID > second.key.instanceID && room.getShimingStatus(third) == 0);
    probe.removeRef = third;
    SHIMING_CHECK(room.setShimingStatus(third, 1) && skill.successes.size() == 1);
    probe.resetRef = second;
    SHIMING_CHECK(room.setShimingStatus(second, 1));
    SHIMING_CHECK(skill.successes == (QList<SkillInstanceRef>{first, second}));
    SHIMING_CHECK(room.getShimingStatus(second) == 1);
    // Single-instance compatibility: success/fail/reset, idempotence and owner identity.
    SHIMING_CHECK(room.setShimingStatus(foreign, 1));
    SHIMING_CHECK(room.setShimingStatus(foreign, 2));
    SHIMING_CHECK(room.setShimingStatus(foreign, 0));
    SHIMING_CHECK(room.getShimingStatus(foreign) == 0 && room.getShimingStatus(second) == 1);
    qInfo() << "Shiming dual-instance, single-instance, stale/reentrant callback and owner-only snapshot tests passed";
    return true;
}

static bool shimingLuaCallbacks()
{
    Room room(nullptr, "02_1v1");
    RoomTestAccess::attachThread(room);
    ServerPlayer *owner = RoomTestAccess::addOrdinaryPlayer(room, "lua-shiming-owner");
    room.setCurrent(owner);
    EngineRuntimeContextScope engineScope(*Sanguosha, &room);
    LuaRuntime::Binding binding(*room.luaRuntime());
    lua_State *L = room.getLuaState();
    const char *script = R"lua(
        si_callbacks = {}
        si_skill = sgs.CreateTriggerSkillV2 {
            name = "test-shiming-lua", events = {}, shiming_skill = true,
            on_shiming_success = function(self, room, player, ref)
                assert(ref.ownerObjectName == player:objectName())
                assert(room:getShimingStatus(ref) == 1)
                table.insert(si_callbacks, ref.key.instanceID * 10 + 1)
            end,
            on_shiming_fail = function(self, room, player, ref)
                assert(ref.ownerObjectName == player:objectName())
                assert(room:getShimingStatus(ref) == 2)
                table.insert(si_callbacks, ref.key.instanceID * 10 + 2)
            end,
        }
        si_dispatch = sgs.CreateTriggerSkillV2 {
            name = "test-shiming-lua-v2", events = {sgs.Dying},
            shiming_skill = true, frequency = sgs.Skill_Compulsory,
            can_trigger = function(self, event, room, player, data)
                if player and player:hasSkill(self:objectName()) then return self:objectName() end
                return false
            end,
            on_cost = function(self, event, room, player, ctx)
                return room:getShimingStatus(ctx:getActivationRef()) == 0
            end,
            on_effect = function(self, event, room, player, ctx)
                local ref = ctx:getActivationRef()
                if room:getShimingStatus(ref) == 0 then room:sendShimingLog(ref, false) end
                return false
            end,
        }
        si_event = sgs.CreateTriggerSkill {
            name = "test-shiming-lua-event", events = {sgs.EventShimingFail}, global = true,
            on_trigger = function(self, event, player, data, room)
                local ref = data:toSkillInstanceRef()
                assert(ref.ownerObjectName == player:objectName())
                player:setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "event_seen", sgs.QVariant(true))
                return false
            end,
        }
        local skills = sgs.SkillList()
        skills:append(si_skill)
        skills:append(si_dispatch)
        skills:append(si_event)
        sgs.Sanguosha:addSkills(skills)
    )lua";
    if (luaL_dostring(L, script) != 0) {
        qCritical() << lua_tostring(L, -1); lua_pop(L, 1); return false;
    }
    const int first = room.acquireSkill(owner, "test-shiming-lua", false, false, false);
    const int second = room.acquireSkill(owner, "test-shiming-lua", false, false, false);
    SHIMING_CHECK(room.setShimingStatus(SkillInstanceRef(owner->objectName(), SkillInstanceKey("test-shiming-lua", first)), 1));
    SHIMING_CHECK(room.setShimingStatus(SkillInstanceRef(owner->objectName(), SkillInstanceKey("test-shiming-lua", second)), 2));
    const QByteArray assertion = QString("assert(#si_callbacks == 2 and si_callbacks[1] == %1 and si_callbacks[2] == %2)")
        .arg(first * 10 + 1).arg(second * 10 + 2).toUtf8();
    if (luaL_dostring(L, assertion.constData()) != 0) {
        qCritical() << lua_tostring(L, -1); lua_pop(L, 1); return false;
    }
    const TriggerSkillV2 *dispatch = qobject_cast<const TriggerSkillV2 *>(Sanguosha->getSkill("test-shiming-lua-v2"));
    const TriggerSkill *eventProbe = Sanguosha->getTriggerSkill("test-shiming-lua-event");
    SHIMING_CHECK(dispatch && eventProbe);
    room.getThread()->addTriggerSkill(eventProbe);
    const int a1 = room.acquireSkill(owner, dispatch->objectName(), false, false, false);
    const int a2 = room.acquireSkill(owner, dispatch->objectName(), false, false, false);
    const SkillInstanceRef r1(owner->objectName(), SkillInstanceKey(dispatch->objectName(), a1));
    const SkillInstanceRef r2(owner->objectName(), SkillInstanceKey(dispatch->objectName(), a2));
    room.addSkillInvalidity(owner, dispatch->objectName(), "test", "test", a1);
    QVariant data;
    // Exercise RoomThread V2 expansion and invalid-instance filtering.
    room.getThread()->trigger(Dying, &room, owner, data);
    SHIMING_CHECK(room.getShimingStatus(r1) == 0 && room.getShimingStatus(r2) == 2);
    SHIMING_CHECK(owner->getSkillInstanceStateValue(dispatch->objectName(), a2, "event_seen").toBool());
    SHIMING_CHECK(!owner->getSkillInstanceStateValue(dispatch->objectName(), a1, "event_seen").toBool());
    room.removeSkillInvalidity(owner, dispatch->objectName(), "test", "test", a1);
    // Exercise RoomThread V2 expansion and invalid-instance filtering.
    room.getThread()->trigger(Dying, &room, owner, data);
    SHIMING_CHECK(room.getShimingStatus(r1) == 2 && room.getShimingStatus(r2) == 2);
    qInfo() << "Shiming Lua exact-reference callbacks, events and dispatch tests passed";
    return true;
}


static bool shimingPackageTriggers()
{
    Room room(nullptr, "04p");
    RoomTestAccess::attachThread(room);
    ServerPlayer *owner = RoomTestAccess::addOrdinaryPlayer(room, "mission-owner");
    ServerPlayer *victim = RoomTestAccess::addOrdinaryPlayer(room, "mission-victim");
    ServerPlayer *third = RoomTestAccess::addOrdinaryPlayer(room, "mission-third");
    RoomTestAccess::resetAlive(room);
    owner->setNext(victim); victim->setNext(third); third->setNext(owner);
    for (ServerPlayer *p : {owner, victim, third}) { p->setMaxHp(4); p->setHp(4); }
    room.setCurrent(owner);
    auto acquire = [&](const QString &name) {
        return SkillInstanceRef(owner->objectName(), SkillInstanceKey(name,
            room.acquireSkill(owner, name, false, false, false)));
    };
    const TriggerSkill *weiming = Sanguosha->getTriggerSkill("weiming");
    const TriggerSkill *powei = Sanguosha->getTriggerSkill("xinpowei");
    const TriggerSkill *qingyu = Sanguosha->getTriggerSkill("secondmobilexinqingyu");
    SHIMING_CHECK(weiming && powei && qingyu);
    const SkillInstanceRef w1 = acquire("weiming"), w2 = acquire("weiming");
    owner->setSkillInstanceStateValue("weiming", w1.key.instanceID, "weiming_targets", QStringList{victim->objectName()});
    owner->setSkillInstanceStateValue("weiming", w2.key.instanceID, "weiming_targets", QStringList{third->objectName()});
    DamageStruct damage("test", owner, victim);
    DeathStruct death; death.who = victim; death.damage = &damage;
    QVariant data = QVariant::fromValue(death);
    weiming->trigger(Death, &room, victim, data);
    SHIMING_CHECK(room.getShimingStatus(w1) == 2 && room.getShimingStatus(w2) == 1);
    SHIMING_CHECK(victim->getSkillInstanceIds("weiming").isEmpty());
    // Both missions saw the same death, but only one had marked that victim.
    SHIMING_CHECK(owner->getSkillInstanceStateValue("weiming", w1.key.instanceID, "weiming_targets").toStringList().isEmpty());
    const SkillInstanceRef p1 = acquire("xinpowei"), p2 = acquire("xinpowei");
    data = QVariant();
    powei->trigger(GameStart, &room, owner, data);
    owner->setSkillInstanceStateValue("xinpowei", p1.key.instanceID, "powei_targets", QStringList());
    owner->setPhase(Player::RoundStart);
    powei->trigger(EventPhaseStart, &room, owner, data);
    SHIMING_CHECK(room.getShimingStatus(p1) == 1 && room.getShimingStatus(p2) == 0);
    SHIMING_CHECK(owner->getSkillInstanceStateValue("xinpowei", p2.key.instanceID, "powei_targets").toStringList().size() == 2);
    SHIMING_CHECK(owner->getSkillInstanceIds("xinshenzhuo").size() == 1);
    // One completed Qingyu copy must not suppress failure of its pending sibling.
    const SkillInstanceRef q1 = acquire("secondmobilexinqingyu"), q2 = acquire("secondmobilexinqingyu");
    SHIMING_CHECK(room.setShimingStatus(q1, 1));
    DyingStruct dying; dying.who = owner;
    data = QVariant::fromValue(dying);
    qingyu->trigger(Dying, &room, owner, data);
    SHIMING_CHECK(room.getShimingStatus(q1) == 1 && room.getShimingStatus(q2) == 2 && owner->getMaxHp() == 3);
    qingyu->trigger(Dying, &room, owner, data);
    SHIMING_CHECK(owner->getMaxHp() == 3);
    // A fresh single instance follows the old failure path exactly once.
    room.detachSkillFromPlayer(owner, q1.key.toString(), false, false, false);
    room.detachSkillFromPlayer(owner, q2.key.toString(), false, false, false);
    const SkillInstanceRef q3 = acquire("secondmobilexinqingyu");
    qingyu->trigger(Dying, &room, owner, data);
    SHIMING_CHECK(room.getShimingStatus(q3) == 2 && owner->getMaxHp() == 2);
    qInfo() << "Shiming Weiming/Powei/Qingyu package dual-instance and single-instance tests passed";
    return true;
}


static bool shimingExternalLuaMigration()
{
    if (!qEnvironmentVariableIsSet("QSAN_TEST_SHIMING_EXTERNAL")) return true;
    Room room(nullptr, "02_1v1");
    RoomTestAccess::attachThread(room);
    ServerPlayer *owner = RoomTestAccess::addOrdinaryPlayer(room, "external-owner");
    ServerPlayer *target = RoomTestAccess::addOrdinaryPlayer(room, "external-target");
    RoomTestAccess::resetAlive(room);
    room.setCurrent(owner);
    owner->setMaxHp(4); owner->setHp(4);
    target->setMaxHp(4); target->setHp(4);
    EngineRuntimeContextScope engineScope(*Sanguosha, &room);
    LuaRuntime::Binding binding(*room.luaRuntime());
    auto acquire = [&](const QString &name) {
        return SkillInstanceRef(owner->objectName(), SkillInstanceKey(name,
            room.acquireSkill(owner, name, false, false, false)));
    };
    const TriggerSkillV2 *powei = qobject_cast<const TriggerSkillV2 *>(Sanguosha->getSkill("powei"));
    SHIMING_CHECK(powei);
    const SkillInstanceRef p1 = acquire("powei"), p2 = acquire("powei");
    QVariant data;
    room.getThread()->trigger(CardFinished, &room, owner, data);
    SHIMING_CHECK(room.getShimingStatus(p1) == 1 && room.getShimingStatus(p2) == 1);
    SHIMING_CHECK(owner->getSkillInstanceIds("shenzhuo").size() == 2);
    room.getThread()->trigger(CardFinished, &room, owner, data);
    SHIMING_CHECK(owner->getSkillInstanceIds("shenzhuo").size() == 2);
    const TriggerSkillV2 *fuhan = qobject_cast<const TriggerSkillV2 *>(Sanguosha->getSkill("s4_fuhan"));
    SHIMING_CHECK(fuhan);
    const SkillInstanceRef f1 = acquire("s4_fuhan"), f2 = acquire("s4_fuhan");
    owner->setSkillInstanceStateValue("s4_fuhan", f1.key.instanceID, "shiming_status", 1);
    owner->setPhase(Player::Play);
    const TriggerList fuhanTriggers = fuhan->triggerable(EventPhaseEnd, &room, owner, data);
    SHIMING_CHECK(fuhanTriggers.value(owner) == QStringList{f2.key.toString()});
    const TriggerSkillV2 *ganglie = qobject_cast<const TriggerSkillV2 *>(Sanguosha->getSkill("s4_ganglie"));
    SHIMING_CHECK(ganglie);
    const SkillInstanceRef g1 = acquire("s4_ganglie"), g2 = acquire("s4_ganglie");
    DamageStruct damage("test", target, owner);
    data = QVariant::fromValue(damage);
    SkillContext ctx;
    ctx.owner = owner; ctx.activationRef = g1; ctx.sourceRef = g1;
    ctx.skill_name = "s4_ganglie"; ctx.instanceID = g1.key.instanceID; ctx.original_data = &data;
    ganglie->record(Damaged, &room, owner, ctx);
    SHIMING_CHECK(owner->getSkillInstanceStateValue("s4_ganglie", g1.key.instanceID, "attackers").toString().contains(target->objectName()));
    SHIMING_CHECK(owner->getSkillInstanceStateValue("s4_ganglie", g2.key.instanceID, "attackers").toString().isEmpty());
    DamageStruct killing("test", owner, target);
    DeathStruct death; death.who = target; death.damage = &killing;
    data = QVariant::fromValue(death);
    const TriggerList ganglieTriggers = ganglie->triggerable(Death, &room, owner, data);
    SHIMING_CHECK(ganglieTriggers.value(owner) == QStringList{g1.key.toString()});
    qInfo() << "Shiming external Lua Powei/Fuhan/Ganglie migration tests passed";
    return true;
}

#undef SHIMING_CHECK

template <typename Base>
class ConcealedCorrectionProbe : public Base
{
public:
    explicit ConcealedCorrectionProbe(const QString &name) : Base(name) {}
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        ++calls;
        return CorrectSkillResult::useAmount(ctx.currentAmount);
    }
    CorrectSkillResult getFixedValue(const CorrectSkillContext &ctx) const override
    {
        ++fixedCalls;
        return CorrectSkillResult::useAmount(ctx.currentAmount);
    }
    mutable int calls = 0;
    mutable int fixedCalls = 0;
};

static bool concealedCorrectionEffects()
{
#define PASSIVE_CHECK(condition) do { if (!(condition)) { \
    qCritical() << "Concealed correction contract failed" << __LINE__ << #condition; return false; \
} } while (false)
    const bool previousHegemony = Config.EnableHegemony;
    const bool previousServerHegemony = ServerInfo.EnableHegemony;
    auto restore = qScopeGuard([=]() {
        Config.EnableHegemony = previousHegemony;
        ServerInfo.EnableHegemony = previousServerHegemony;
    });
    Config.EnableHegemony = true;
    ServerInfo.EnableHegemony = true;
    Room room(nullptr, QStringLiteral("04p"));
    EngineRuntimeContextScope engineScope(*Sanguosha, &room);
    LuaRuntime::Binding binding(*room.luaRuntime());
    ServerPlayer *owner = RoomTestAccess::addOrdinaryPlayer(room, "passive-owner");
    ServerPlayer *other = RoomTestAccess::addOrdinaryPlayer(room, "passive-other");
    owner->setNext(other);
    other->setNext(owner);
    RoomTestAccess::resetAlive(room);
    room.setCurrent(owner);

    auto *distance = new ConcealedCorrectionProbe<DistanceSkillV2>("test-concealed-distance");
    auto *maxcards = new ConcealedCorrectionProbe<MaxCardsSkillV2>("test-concealed-maxcards");
    auto *target = new ConcealedCorrectionProbe<TargetModSkillV2>("test-concealed-target");
    auto *range = new ConcealedCorrectionProbe<AttackRangeSkillV2>("test-concealed-range");
    Sanguosha->addSkills({distance, maxcards, target, range});
    auto add = [](ServerPlayer *holder, const Skill *skill, SkillInstanceSource source, int slot) {
        const int id = holder->createSkillInstance(skill->objectName(), source);
        SkillInstance instance = *holder->findSkillInstance(skill->objectName(), id);
        instance.bindHead = slot;
        holder->upsertSkillInstance(instance);
        return id;
    };
    const int head = add(owner, distance, SourceInnate, 1);
    const int deputy = add(owner, distance, SourceInnate, 2);
    const int maxId = add(owner, maxcards, SourceInnate, 1);
    add(owner, target, SourceInnate, 1);
    add(owner, range, SourceInnate, 1);
    std::unique_ptr<Card> slash(Sanguosha->cloneCard("slash"));
    PASSIVE_CHECK(slash);

    // Only TargetMod previews the user's own hidden root. Continuous effects
    // and the unlimited-history exemption still require a public source.
    const auto query = [&]() {
        Sanguosha->correctDistance(owner, other);
        Sanguosha->correctMaxCards(owner);
        Sanguosha->correctCardTarget(TargetModSkill::Residue, owner, slash.get(), other);
        Sanguosha->correctAttackRange(owner);
    };
    query();
    PASSIVE_CHECK(distance->calls == 0 && maxcards->calls == 0 && target->calls == 1 && range->calls == 0);
    target->calls = 0;
    target->setBaseAmount(-1);
    owner->addHistory("Slash", 2);
    {
        TargetModSkillQueryScope countedUse(owner, {}, "Slash");
        PASSIVE_CHECK(owner->usedTimes("Slash", true) == 1);
        {
            TargetModSkillQueryScope separateQuery(other, {});
            PASSIVE_CHECK(owner->usedTimes("Slash", true) == 2);
        }
        PASSIVE_CHECK(owner->usedTimes("Slash", true) == 1);
    }
    PASSIVE_CHECK(owner->usedTimes("Slash", true) == 2);
    owner->clearHistory("Slash");
    PASSIVE_CHECK(!Sanguosha->hasResidueUnlimited(owner, slash.get(), other));
    PASSIVE_CHECK(target->calls == 0);
    {
        TargetModSkillQueryScope publicOnly(owner, {});
        PASSIVE_CHECK(Sanguosha->correctCardTarget(TargetModSkill::Residue, owner, slash.get(), other) == 0);
        PASSIVE_CHECK(target->calls == 0);
    }
    PASSIVE_CHECK(Sanguosha->correctCardTarget(TargetModSkill::Residue, owner, slash.get(), other) == 1000);
    target->setBaseAmount(1);
    target->calls = 0;
    PASSIVE_CHECK(Sanguosha->listMaxCardsSkillContributions(maxcards, owner).isEmpty());
    PASSIVE_CHECK(maxcards->fixedCalls == 0);
    PASSIVE_CHECK(owner->getValidSkillInstanceIds(distance->objectName()).size() == 2);
    owner->setGeneral2Showed(true);
    PASSIVE_CHECK(Sanguosha->contributionOfDistanceSkill(distance, owner, other) == 1);
    PASSIVE_CHECK(!owner->isSkillInstanceEffectAvailable(distance->objectName(), head));
    PASSIVE_CHECK(owner->isSkillInstanceEffectAvailable(distance->objectName(), deputy));
    owner->setGeneralShowed(true);
    distance->calls = 0;
    query();
    PASSIVE_CHECK(distance->calls == 2 && maxcards->calls == 1 && target->calls == 1 && range->calls == 1);
    PASSIVE_CHECK(Sanguosha->listMaxCardsSkillContributions(maxcards, owner).size() == 1);
    PASSIVE_CHECK(maxcards->fixedCalls == 1);

    const SkillInstanceRef root(owner->objectName(), SkillInstanceKey(distance->objectName(), head));
    const int helper = owner->createSkillInstance(maxcards->objectName(), SourceHelper,
                                                  distance->objectName(), head, false);
    const int attached = other->createSkillInstance(distance->objectName(), SourceAttached, root);
    owner->setGeneralShowed(false);
    PASSIVE_CHECK(!owner->isSkillInstanceEffectAvailable(maxcards->objectName(), helper));
    PASSIVE_CHECK(Sanguosha->contributionOfDistanceSkill(distance, other, owner) == 0);
    owner->setGeneralShowed(true);
    PASSIVE_CHECK(owner->isSkillInstanceEffectAvailable(maxcards->objectName(), helper));
    PASSIVE_CHECK(Sanguosha->contributionOfDistanceSkill(distance, other, owner) == 1);
    owner->setTag("SkillInvalidityRecords", QStringList{
        SkillInstanceUtils::formatName(distance->objectName(), head) + "|probe|test"});
    PASSIVE_CHECK(!other->isSkillInstanceEffectAvailable(distance->objectName(), attached));
    PASSIVE_CHECK(!owner->isSkillInstanceEffectAvailable(maxcards->objectName(), helper));
    PASSIVE_CHECK(owner->isSkillInstanceEffectAvailable(distance->objectName(), deputy));
    owner->removeTag("SkillInvalidityRecords");

    // A foreign hidden root cannot change another player's target preview.
    owner->setGeneralShowed(false);
    const int remoteTarget = other->createSkillInstance(target->objectName(), SourceAttached, root);
    PASSIVE_CHECK(Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, other, slash.get(), owner) == 0);
    PASSIVE_CHECK(!other->isSkillInstanceEffectAvailable(target->objectName(), remoteTarget, other));
    owner->setGeneralShowed(true);
    PASSIVE_CHECK(Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, other, slash.get(), owner) == 1);

    // An independent acquired copy remains active even with both generals hidden.
    owner->setGeneralShowed(false);
    owner->setGeneral2Showed(false);
    const int acquired = add(owner, distance, SourceAcquired, 0);
    PASSIVE_CHECK(Sanguosha->contributionOfDistanceSkill(distance, owner, other) == 1);
    PASSIVE_CHECK(!owner->isSkillInstanceEffectAvailable(distance->objectName(), head));
    PASSIVE_CHECK(owner->isSkillInstanceEffectAvailable(distance->objectName(), acquired));
    const int orphan = add(other, maxcards, SourceAttached, 0);
    PASSIVE_CHECK(!other->isSkillInstanceEffectAvailable(maxcards->objectName(), orphan));
    SkillInstance cycle = *other->findSkillInstance(distance->objectName(), attached);
    cycle.parentRef = SkillInstanceRef(other->objectName(), cycle.key());
    other->upsertSkillInstance(cycle);
    PASSIVE_CHECK(!other->isSkillInstanceEffectAvailable(distance->objectName(), attached));

    // System corrections have no general source; identity keeps legacy behavior.
    range->setHolderSelector(CorrectSkill_System);
    range->calls = 0;
    Sanguosha->correctAttackRange(owner);
    PASSIVE_CHECK(range->calls == 1);
    Config.EnableHegemony = false;
    ServerInfo.EnableHegemony = false;
    PASSIVE_CHECK(owner->isSkillInstanceEffectAvailable(maxcards->objectName(), maxId));
    PASSIVE_CHECK(Sanguosha->contributionOfDistanceSkill(distance, owner, other) == 3);
#undef PASSIVE_CHECK
    return true;
}

static bool standardEquipmentV2Contracts()
{
    const bool oldHegemony = Config.EnableHegemony;
    const auto restoreMode = qScopeGuard([oldHegemony] { Config.EnableHegemony = oldHegemony; });
    Config.EnableHegemony = false;
    Room room(nullptr, "02_1v1");
    EngineRuntimeContextScope runtimeScope(*Sanguosha, &room);
    room.roomRuntime()->state().reset();
    ServerPlayer *owner = RoomTestAccess::addOrdinaryPlayer(room, "equipment_owner");
    ServerPlayer *other = RoomTestAccess::addOrdinaryPlayer(room, "equipment_other");
    owner->setSeat(1);
    other->setSeat(2);
    owner->setMaxHp(4);
    owner->setHp(4);
    other->setMaxHp(4);
    other->setHp(4);
    room.setCurrent(owner);

    QMap<QString, int> equipment;
    QList<int> hand;
    for (int id = 0; id < Sanguosha->getCardCount(); ++id) {
        const Card *card = Sanguosha->getEngineCard(id);
        if (card->isKindOf("EquipCard") && !equipment.contains(card->objectName()))
            equipment.insert(card->objectName(), id);
        if (card->isKindOf("Slash") && hand.size() < 2) hand << id;
    }
    if (hand.size() != 2) return false;
    for (int id : hand) {
        owner->addCard(id, Player::PlaceHand);
        room.setCardMapping(id, owner, Player::PlaceHand);
    }
    const auto equip = [&](const QString &name) {
        if (!equipment.contains(name)) return false;
        for (const Card *old : owner->getEquips()) owner->removeEquip(old);
        const int id = equipment.value(name);
        owner->setEquip(Sanguosha->getCard(id));
        room.setCardMapping(id, owner, Player::PlaceEquip);
        return true;
    };

    for (const QString &name : {QString("spear"), QString("axe"), QString("wooden_ox")}) {
        const auto *skill = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill(name));
        if (!skill || !equip(name)) return false;
        const int instance = owner->acquireSkill(name);
        ActiveSkillRequest request;
        request.initiator = owner;
        request.activationRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(name, instance));
        request.reason = name == "axe" ? CardUseStruct::CARD_USE_REASON_UNKNOWN
                                      : CardUseStruct::CARD_USE_REASON_PLAY;
        request.pattern = name == "axe" ? "@axe" : QString();
        if (!skill->canActivate(request) || skill->cardSelectionFeasible(request)) return false;
        request.selectedCardIds = name == "wooden_ox" ? QList<int>{hand.first()} : hand;
        const Card *card = RoomTestAccess::resolveActiveRequest(room, owner, skill, request);
        if (!card || card->getSubcards() != request.selectedCardIds) return false;
        const_cast<Card *>(card)->deleteLater();
        if (name == "spear") {
            if (!card->isKindOf("Slash") || skill->historyKey(request) != "Slash") return false;
            ActiveSkillRequest selecting = request;
            selecting.selectedCardIds.clear();
            if (skill->canSelectCard(selecting, owner->getWeapon())) return false;
            request.reason = CardUseStruct::CARD_USE_REASON_RESPONSE;
            request.pattern = "slash";
            if (!skill->canActivate(request)) return false;
            request.pattern = "jink";
            if (skill->canActivate(request)) return false;
            request.pattern = "slash";
        } else if (name == "axe") {
            if (!card->isKindOf("DummyCard")) return false;
            ActiveSkillRequest selecting = request;
            selecting.selectedCardIds.clear();
            if (skill->canSelectCard(selecting, owner->getWeapon())) return false;
            request.reason = CardUseStruct::CARD_USE_REASON_PLAY;
            if (skill->canActivate(request)) return false;
            request.reason = CardUseStruct::CARD_USE_REASON_UNKNOWN;
        } else {
            if (!qobject_cast<const ActiveSkillCard *>(card) || !card->targetFixed()
                || card->getHandlingMethod() != Card::MethodNone || skill->willThrowSelectedCards()
                || skill->historyKey(request) != "WoodenOxCard") return false;
            const Card *legacy = Card::Parse("@WoodenOxCard=" + QString::number(hand.first()));
            if (!legacy || !qobject_cast<const ActiveSkillCard *>(legacy) || !legacy->targetFixed()) return false;
            const_cast<Card *>(legacy)->deleteLater();
            owner->addHistory("WoodenOxCard");
            if (skill->canActivate(request)) return false;
            owner->clearHistory("WoodenOxCard");
        }
        // Server reconstruction must reject duplicate IDs and another player's card.
        request.selectedCardIds = QList<int>{hand.first(), hand.first()};
        if (RoomTestAccess::resolveActiveRequest(room, owner, skill, request)) return false;
        request.selectedCardIds = name == "wooden_ox" ? QList<int>{hand.first()} : hand;
        owner->removeCard(hand.first(), Player::PlaceHand);
        other->addCard(hand.first(), Player::PlaceHand);
        room.setCardMapping(hand.first(), other, Player::PlaceHand);
        if (RoomTestAccess::resolveActiveRequest(room, owner, skill, request)) return false;
        other->removeCard(hand.first(), Player::PlaceHand);
        owner->addCard(hand.first(), Player::PlaceHand);
        room.setCardMapping(hand.first(), owner, Player::PlaceHand);
    }

    const auto *crossbow = dynamic_cast<const TargetModSkillV2 *>(Sanguosha->getSkill("crossbow"));
    const auto *halberd = dynamic_cast<const TargetModSkillV2 *>(Sanguosha->getSkill("halberd"));
    const auto *horse = dynamic_cast<const DistanceSkillV2 *>(Sanguosha->getSkill("horse"));
    if (!crossbow || !halberd || !horse || !equip("crossbow")) return false;
    Slash slash(Card::NoSuit, 0);
    CorrectSkillContext context;
    context.primary = owner;
    context.secondary = other;
    context.card = &slash;
    context.modType = TargetModSkill::Residue;
    context.currentAmount = crossbow->getBaseAmount();
    if (crossbow->getCorrection(context).value != 999) return false;
    context.modType = TargetModSkill::ExtraTarget;
    if (crossbow->getCorrection(context).applies) return false;
    Config.EnableHegemony = true;
    slash.addSubcard(owner->getWeapon());
    context.modType = TargetModSkill::Residue;
    if (crossbow->getCorrection(context).applies) return false;
    Config.EnableHegemony = false;
    if (!equip("halberd")) return false;
    Slash lastHand(Card::NoSuit, 0);
    lastHand.addSubcards(hand);
    context.card = &lastHand;
    context.modType = TargetModSkill::ExtraTarget;
    context.currentAmount = halberd->getBaseAmount();
    if (halberd->getCorrection(context).value != 2) return false;
    context.card = Sanguosha->getCard(hand.first());
    if (halberd->getCorrection(context).applies) return false;
    Config.EnableHegemony = true;
    if (!equip("chitu") || horse->getCorrection(context).value != -1) return false;
    return horse->getHolderSelector() == CorrectSkill_System
        && dynamic_cast<const WeaponSkillV2 *>(Sanguosha->getSkill("blade"));
}

static bool lifecycleAndRuntimeFacade()
{
    DistanceSkillV2 rootSkill(QStringLiteral("test-skill-runtime-root"));
    rootSkill.setBaseAmount(2);
    DistanceSkillV2 attachedSkill(QStringLiteral("test-skill-runtime-attached"));
    TestLimitedSkill limitedSkill;
    Sanguosha->addSkills(QList<const Skill *>()
                         << &rootSkill << &attachedSkill << &limitedSkill);

    Room room(nullptr, QStringLiteral("02_1v1"));
    RoomTestAccess::attachThread(room);
    ServerPlayer *owner = RoomTestAccess::addOrdinaryPlayer(room, QStringLiteral("owner"));
    ServerPlayer *recipient = RoomTestAccess::addOrdinaryPlayer(room, QStringLiteral("recipient"));
    room.setCurrent(owner);

    const int firstRootId = room.acquireSkill(owner, rootSkill.objectName(),
                                              false, false, false);
    const int secondRootId = room.acquireSkill(owner, rootSkill.objectName(),
                                               false, false, false);
    const int limitedId = room.acquireSkill(owner, limitedSkill.objectName(),
                                            false, false, false);
    if (firstRootId <= 0 || secondRootId <= firstRootId || limitedId <= 0)
        return false;

    const SkillInstanceRef rootRef(
        owner->objectName(), SkillInstanceKey(rootSkill.objectName(), firstRootId));
    if (!amountCorrectStateAndInvalidity(room, owner, rootSkill,
                                         rootRef, secondRootId))
        return false;

    const SkillInstanceRef childRef = room.attachSkillToPlayer(
        recipient, attachedSkill.objectName(), rootRef);
    if (!childRef.isValid())
        return false;
    if (room.attachSkillToPlayer(recipient, attachedSkill.objectName(), rootRef) != childRef)
        return false;
    if (RoomTestAccess::resolveRoot(room, childRef) != rootRef)
        return false;

    if (!usageReservation(room, owner, limitedSkill, limitedId))
        return false;
    if (!executionRegistry(room, owner, rootRef))
        return false;

    const int removedId = room.detachSkillFromPlayer(
        owner, SkillInstanceUtils::formatName(rootSkill.objectName(), firstRootId),
        false, false, false);
    if (removedId != firstRootId
        || owner->hasSkillInstance(rootSkill.objectName(), firstRootId)
        || recipient->hasSkillInstance(attachedSkill.objectName(),
                                       childRef.key.instanceID)
        || !owner->hasSkillInstance(rootSkill.objectName(), secondRootId))
        return false;

    const int thirdRootId = room.acquireSkill(owner, rootSkill.objectName(),
                                              false, false, false);
    return thirdRootId > secondRootId;
}

}

int runSkillRuntimeCoordinatorTests()
{
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "engine initialization failed:" << error;
        return 1;
    }
    if (!lifecycleAndRuntimeFacade()) {
        qCritical() << "SkillRuntimeCoordinator regression failed";
        return 2;
    }
    if (!shimingInstancePipeline() || !shimingPackageTriggers() || !shimingLuaCallbacks()
        || !shimingExternalLuaMigration()) return 3;
    if (!concealedCorrectionEffects()) return 4;
    if (!standardEquipmentV2Contracts()) {
        qCritical() << "Standard equipment V2 contracts failed";
        return 5;
    }
    qInfo() << "SkillRuntimeCoordinator regression passed";
    return 0;
}
