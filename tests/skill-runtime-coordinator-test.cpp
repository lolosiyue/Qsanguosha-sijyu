#include "engine-bootstrap.h"
#include "ai.h"
#include "engine.h"
#include "room.h"
#include "roomthread.h"
#include "serverplayer.h"
#include "skill.h"
#include "skill-instance-utils.h"

#include <QCoreApplication>
#include <QDebug>

struct RoomTestAccess
{
    static ServerPlayer *addPlayer(Room &room, const QString &objectName)
    {
        ServerPlayer *player = new ServerPlayer(&room);
        player->setObjectName(objectName);
        player->setAlive(true);
        player->setRemoved(false);
        TrustAI *ai = new TrustAI(player);
        ai->setParent(player);
        player->setAI(ai);
        room.addPlayerToRoster(player);
        return player;
    }

    static void attachThread(Room &room)
    {
        room.thread = new RoomThread(&room);
    }

    static SkillInstanceRef resolveRoot(Room &room, const SkillInstanceRef &ref)
    {
        return room.resolveSkillInstanceRootRef(ref);
    }

    static bool reserveUsage(Room &room, const ViewAsSkillV2 *skill,
                             const SkillContext &context)
    {
        return room.reserveActiveSkillUsage(skill, context);
    }

    static void releaseUsage(Room &room, const ViewAsSkillV2 *skill,
                             const SkillContext &context)
    {
        room.releaseActiveSkillUsage(skill, context);
    }

    static void commitUsage(Room &room, const ViewAsSkillV2 *skill,
                            const SkillContext &context)
    {
        room.commitActiveSkillUsage(skill, context);
    }
};

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
    ServerPlayer *owner = RoomTestAccess::addPlayer(room, QStringLiteral("owner"));
    ServerPlayer *recipient = RoomTestAccess::addPlayer(room, QStringLiteral("recipient"));
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

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "engine initialization failed:" << error;
        return 1;
    }
    if (!lifecycleAndRuntimeFacade()) {
        qCritical() << "SkillRuntimeCoordinator regression failed";
        return 2;
    }
    qInfo() << "SkillRuntimeCoordinator regression passed";
    return 0;
}
