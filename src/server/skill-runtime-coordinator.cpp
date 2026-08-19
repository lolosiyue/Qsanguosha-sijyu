#include "skill-runtime-coordinator.h"

#include "engine.h"
#include "protocol/skill-instance-message.h"
#include "room.h"
#include "room-notifier.h"
#include "room-runtime.h"
#include "roomthread.h"
#include "serverplayer.h"

#include <limits>
#include <QDebug>

using namespace QSanProtocol;

namespace {

bool canReceiveSkillInstance(const ServerPlayer *receiver, const ServerPlayer *owner,
                             const SkillInstance &instance)
{
    if (receiver == owner)
        return true;
    const Skill *skill = Sanguosha->getSkill(instance.skillName);
    return instance.source != SourceHelper && instance.visible && skill && skill->isVisible();
}

SkillInstanceEntryMessage skillInstanceMessage(const ServerPlayer *owner,
                                               const SkillInstance &instance,
                                               bool includePrivateState)
{
    SkillInstanceEntryMessage message;
    message.ownerName = owner->objectName();
    message.instance = instance;
    if (includePrivateState) {
        message.privateState = owner->getSkillInstanceState(instance.skillName,
                                                            instance.instanceID);
    }
    return message;
}

QString skillAmountGuardKey(const SkillInstanceRef &ref)
{
    return ref.ownerObjectName + QChar('\x1f') + ref.key.skillName
        + QChar('#') + QString::number(ref.key.instanceID);
}

class SkillAmountRecursionGuard
{
public:
    SkillAmountRecursionGuard(QSet<QString> &activeKeys, const QString &key)
        : m_activeKeys(activeKeys), m_key(key)
    {
    }

    ~SkillAmountRecursionGuard()
    {
        m_activeKeys.remove(m_key);
    }

private:
    QSet<QString> &m_activeKeys;
    QString m_key;
};

bool isCorrectSkillV2Definition(const Skill *skill)
{
    return dynamic_cast<const DistanceSkillV2 *>(skill)
        || dynamic_cast<const MaxCardsSkillV2 *>(skill)
        || dynamic_cast<const TargetModSkillV2 *>(skill)
        || dynamic_cast<const AttackRangeSkillV2 *>(skill);
}

bool hasViewAsSkillEffect(const Player *player, const QString &skillName)
{
    return player && player->getMark("ViewAsSkill_" + skillName + "Effect") > 0;
}

}

SkillRuntimeCoordinator::SkillRuntimeCoordinator(Room &room)
    : m_room(room)
{
}

void SkillRuntimeCoordinator::attachSkillToPlayer(ServerPlayer *player,
                                                   const QString &skillName)
{
    const int instanceId = player->acquireSkill(skillName);
    const SkillInstance *instance = player->findSkillInstance(skillName, instanceId);
    if (instance)
        notifySkillInstanceUpsert(player, *instance);

    const Skill *skill = Sanguosha->getSkill(skillName);
    if (skill && skill->isVisible()
        && skill->getFrequency() == Skill::Club && !skill->getClubName().isEmpty()) {
        player->addClub(skill->getClubName());
    }

    m_room.thread->addTriggerSkill(Sanguosha->getTriggerSkill(skillName));
}

SkillInstanceRef SkillRuntimeCoordinator::attachSkillToPlayer(
    ServerPlayer *player, const QString &skillName, const SkillInstanceRef &parentRef)
{
    if (!player || !parentRef.isValid())
        return SkillInstanceRef();
    ServerPlayer *parentOwner = m_room.findPlayerByObjectName(parentRef.ownerObjectName, true);
    if (!parentOwner
        || !parentOwner->hasSkillInstance(parentRef.key.skillName, parentRef.key.instanceID)) {
        return SkillInstanceRef();
    }

    foreach (const SkillInstanceRef &child,
             m_room.roomRuntime()->attachedSkills().childrenOf(parentRef)) {
        if (child.ownerObjectName == player->objectName() && child.key.skillName == skillName)
            return child;
    }

    const Skill *skill = Sanguosha->getSkill(skillName);
    if (!skill)
        return SkillInstanceRef();
    const int instanceId = player->createSkillInstance(skillName, SourceAttached,
                                                       parentRef, skill->isVisible());
    const SkillInstanceRef child(player->objectName(),
                                 SkillInstanceKey(skillName, instanceId));
    if (!m_room.roomRuntime()->attachedSkills().attach(parentRef, child)) {
        player->removeSkillInstance(skillName, instanceId);
        return SkillInstanceRef();
    }
    if (skill->inherits("TriggerSkill"))
        m_room.thread->addTriggerSkill(qobject_cast<const TriggerSkill *>(skill));
    notifySkillInstanceUpsert(player, *player->findSkillInstance(skillName, instanceId));
    return child;
}

bool SkillRuntimeCoordinator::detachAttachedSkill(const SkillInstanceRef &ref)
{
    if (!ref.isValid())
        return false;
    const bool removeRoot = m_room.roomRuntime()->attachedSkills().contains(ref);
    const QList<SkillInstanceRef> removed = m_room.roomRuntime()->attachedSkills().detach(ref);
    bool changed = false;
    foreach (const SkillInstanceRef &entry, removed) {
        if (entry == ref && !removeRoot)
            continue;
        ServerPlayer *owner = m_room.findPlayerByObjectName(entry.ownerObjectName, true);
        if (!owner)
            continue;
        const SkillInstance *instance = owner->findSkillInstance(entry.key.skillName,
                                                                  entry.key.instanceID);
        if (!instance || instance->source != SourceAttached)
            continue;
        const SkillInstance snapshot = *instance;
        if (owner->removeSkillInstance(entry.key.skillName, entry.key.instanceID)) {
            notifySkillInstanceRemove(owner, snapshot);
            changed = true;
        }
    }
    return changed;
}

int SkillRuntimeCoordinator::chooseSkillInstance(ServerPlayer *chooser, ServerPlayer *owner,
                                                 const QString &skillName, bool visibleOnly,
                                                 bool acquiredOnly)
{
    if (!chooser || !owner)
        return 0;

    QStringList choices;
    QList<int> candidateIds;
    foreach (int instanceId, owner->getSkillInstanceIds(skillName)) {
        const SkillInstance *instance = owner->findSkillInstance(skillName, instanceId);
        if (!instance || instance->source == SourceHelper)
            continue;
        if (acquiredOnly && instance->source != SourceAcquired)
            continue;
        const Skill *skill = Sanguosha->getSkill(skillName);
        if (visibleOnly && (!instance->visible || !skill || !skill->isVisible()))
            continue;
        candidateIds << instanceId;
        choices << SkillInstanceUtils::formatName(skillName, instanceId);
    }

    if (candidateIds.isEmpty())
        return 0;
    if (candidateIds.length() == 1)
        return candidateIds.first();

    const QString answer = m_room.askForChoice(chooser, "remove_skill_instance",
                                                choices.join("+"));
    QString selectedName;
    const int selectedId = SkillInstanceUtils::parseName(answer, selectedName);
    return candidateIds.contains(selectedId) ? selectedId : candidateIds.first();
}

bool SkillRuntimeCoordinator::removeSkillInstanceFromPlayer(
    ServerPlayer *owner, const QString &skillName, int instanceId,
    bool isEquip, bool eventAndLog)
{
    if (!owner || instanceId <= 0)
        return false;
    const SkillInstance *instance = owner->findSkillInstance(skillName, instanceId);
    if (!instance)
        return false;

    const SkillInstance removed = *instance;
    const SkillInstanceRef instanceRef(owner->objectName(), instance->key());
    if (instance->source == SourceAttached)
        return detachAttachedSkill(instanceRef);
    detachAttachedSkill(instanceRef);

    const QList<SkillInstanceKey> children = owner->getChildSkillInstanceKeys(removed.key());
    if (!owner->removeSkillInstance(skillName, instanceId))
        return false;
    notifySkillInstanceRemove(owner, removed);

    const Skill *skill = Sanguosha->getSkill(skillName);
    if (skill && skill->isVisible() && !isEquip && eventAndLog) {
        LogMessage log;
        log.type = "#LoseSkill";
        log.from = owner;
        log.arg = skillName;
        m_room.sendLog(log);
    }

    if (eventAndLog) {
        SkillChangeStruct change(skillName, instanceId);
        change.source = removed.source;
        change.parentSkillName = removed.parent.skillName;
        change.parentInstanceID = removed.parent.instanceID;
        change.visible = removed.visible;
        QVariant data = change.toVariant();
        m_room.thread->trigger(EventLoseSkill, &m_room, owner, data);
    }

    foreach (const SkillInstanceKey &child, children) {
        removeSkillInstanceFromPlayer(owner, child.skillName, child.instanceID,
                                      true, eventAndLog);
    }

    if (skill && !owner->ownsSkill(skillName)) {
        if (skill->getFrequency() == Skill::Club && !skill->getClubName().isEmpty())
            m_room.clearClub(skill->getClubName());
        if (skill->inherits("ViewAsEquipSkill")) {
            const ViewAsEquipSkill *viewAsEquip = Sanguosha->getViewAsEquipSkill(skillName);
            const QString view = viewAsEquip->viewAsEquip(owner);
            foreach (const QString &equipName, view.split(",", Qt::SkipEmptyParts)) {
                if (skillName != equipName && Sanguosha->getViewAsSkill(equipName))
                    detachSkillFromPlayer(owner, equipName, true, false, true);
            }
        }
    }

    owner->refreshUIState();
    return true;
}

int SkillRuntimeCoordinator::detachSkillFromPlayer(ServerPlayer *player,
                                                   const QString &skillName,
                                                   bool isEquip, bool acquireOnly,
                                                   bool eventAndLog)
{
    if (!player)
        return 0;
    QString baseName;
    const int requestedId = SkillInstanceUtils::parseName(skillName, baseName);
    int instanceId = requestedId;
    if (instanceId > 0) {
        const SkillInstance *instance = player->findSkillInstance(baseName, instanceId);
        if (!instance || instance->source == SourceHelper
            || (acquireOnly && instance->source != SourceAcquired)) {
            return 0;
        }
    } else {
        instanceId = chooseSkillInstance(player, player, baseName, false, acquireOnly);
    }
    if (instanceId <= 0)
        return 0;
    return removeSkillInstanceFromPlayer(player, baseName, instanceId,
                                         isEquip, eventAndLog) ? instanceId : 0;
}

int SkillRuntimeCoordinator::discardSkillInstance(ServerPlayer *chooser,
                                                  ServerPlayer *owner,
                                                  const QString &skillName,
                                                  bool eventAndLog)
{
    if (!chooser || !owner)
        return 0;
    QString baseName;
    const int requestedId = SkillInstanceUtils::parseName(skillName, baseName);
    int instanceId = 0;

    if (requestedId > 0) {
        const SkillInstance *instance = owner->findSkillInstance(baseName, requestedId);
        const Skill *skill = Sanguosha->getSkill(baseName);
        if (instance && instance->source != SourceHelper && instance->visible
            && skill && skill->isVisible()) {
            instanceId = requestedId;
        }
    } else {
        instanceId = chooseSkillInstance(chooser, owner, baseName, true, false);
        if (instanceId == 0) {
            foreach (int id, owner->getSkillInstanceIds(baseName)) {
                const SkillInstance *instance = owner->findSkillInstance(baseName, id);
                if (instance && instance->source != SourceHelper) {
                    instanceId = id;
                    break;
                }
            }
        }
    }

    if (instanceId <= 0)
        return 0;
    return removeSkillInstanceFromPlayer(owner, baseName, instanceId,
                                         false, eventAndLog) ? instanceId : 0;
}

void SkillRuntimeCoordinator::handleAcquireDetachSkills(
    ServerPlayer *player, const QStringList &skillNames,
    bool acquireOnly, bool getmark, bool eventAndLog)
{
    foreach (const QString &skillName, skillNames) {
        if (skillName.startsWith("-")) {
            detachSkillFromPlayer(player, skillName.mid(1), false,
                                  acquireOnly, eventAndLog);
        } else {
            acquireSkill(player, skillName, true, getmark, eventAndLog);
        }
    }
}

int SkillRuntimeCoordinator::acquireSkill(ServerPlayer *player, const Skill *skill,
                                          bool open, bool getmark, bool eventAndLog)
{
    if (!skill)
        return 0;
    return acquireSkill(player, skill->objectName(), open, getmark, eventAndLog);
}

int SkillRuntimeCoordinator::acquireSkill(ServerPlayer *player, const QString &skillName,
                                          bool open, bool getmark, bool eventAndLog)
{
    const Skill *skill = Sanguosha->getSkill(skillName);
    if (!skill)
        return 0;

    const int instanceId = player->acquireSkill(skillName);
    const SkillInstance *created = player->findSkillInstance(skillName, instanceId);
    if (created)
        notifySkillInstanceUpsert(player, *created);

    if (skill->inherits("TriggerSkill")) {
        m_room.thread->addTriggerSkill(qobject_cast<const TriggerSkill *>(skill));
    } else if (skill->inherits("ViewAsEquipSkill")) {
        const ViewAsEquipSkill *viewAsEquip = Sanguosha->getViewAsEquipSkill(skillName);
        const QString view = viewAsEquip->viewAsEquip(player);
        foreach (const QString &equipName, view.split(",", Qt::SkipEmptyParts)) {
            if (Sanguosha->getViewAsSkill(equipName))
                attachSkillToPlayer(player, equipName);
        }
    }

    if (skill->isVisible()) {
        if (getmark && !skill->getLimitMark().isEmpty())
            m_room.setPlayerMark(player, skill->getLimitMark(), 1);
        if (skill->getFrequency() == Skill::Club && !skill->getClubName().isEmpty())
            player->addClub(skill->getClubName());
        if (open && eventAndLog) {
            LogMessage log;
            log.from = player;
            log.type = "#AcquireSkill";
            log.arg = skillName;
            m_room.sendLog(log);
        }
    }

    if (eventAndLog) {
        SkillChangeStruct change(skillName, instanceId);
        change.source = SourceAcquired;
        change.visible = skill->isVisible();
        QVariant data = change.toVariant();
        m_room.thread->trigger(EventAcquireSkill, &m_room, player, data);
    }

    foreach (const Skill *related, Sanguosha->getRelatedSkills(skillName)) {
        const int helperId = player->createSkillInstance(related->objectName(), SourceHelper,
                                                         skillName, instanceId,
                                                         related->isVisible());
        const SkillInstance *helper = player->findSkillInstance(related->objectName(), helperId);
        if (helper)
            notifySkillInstanceUpsert(player, *helper);
        if (related->inherits("TriggerSkill"))
            m_room.thread->addTriggerSkill(qobject_cast<const TriggerSkill *>(related));

        if (eventAndLog) {
            SkillChangeStruct helperChange(related->objectName(), helperId);
            helperChange.source = SourceHelper;
            helperChange.parentSkillName = skillName;
            helperChange.parentInstanceID = instanceId;
            helperChange.visible = related->isVisible();
            QVariant helperData = helperChange.toVariant();
            m_room.thread->trigger(EventAcquireSkill, &m_room, player, helperData);
        }
    }

    player->refreshUIState();
    return instanceId;
}

void SkillRuntimeCoordinator::notifySkillInstanceSnapshot(ServerPlayer *receiver)
{
    if (!receiver)
        return;
    QList<SkillInstanceEntryMessage> entries;
    foreach (ServerPlayer *owner, m_room.getAllPlayers(true)) {
        foreach (const SkillInstance &instance, owner->getSkillInstances()) {
            if (canReceiveSkillInstance(receiver, owner, instance))
                entries << skillInstanceMessage(owner, instance, receiver == owner);
        }
    }
    const SkillInstanceMessage message = SkillInstanceMessage::makeSnapshot(entries);
    m_room.doNotify(receiver, S_COMMAND_SKILL_INSTANCE, message.toVariant());
}

void SkillRuntimeCoordinator::notifySkillInstanceUpsert(ServerPlayer *owner,
                                                        const SkillInstance &instance)
{
    foreach (ServerPlayer *receiver, m_room.getPlayers()) {
        if (!canReceiveSkillInstance(receiver, owner, instance))
            continue;
        const SkillInstanceMessage message = SkillInstanceMessage::makeUpsert(
            skillInstanceMessage(owner, instance, receiver == owner));
        m_room.doNotify(receiver, S_COMMAND_SKILL_INSTANCE, message.toVariant());
    }
}

void SkillRuntimeCoordinator::notifySkillInstanceRemove(ServerPlayer *owner,
                                                        const SkillInstance &instance)
{
    foreach (ServerPlayer *receiver, m_room.getPlayers()) {
        if (!canReceiveSkillInstance(receiver, owner, instance))
            continue;
        const SkillInstanceMessage message = SkillInstanceMessage::makeRemove(
            owner->objectName(), instance.skillName, instance.instanceID);
        m_room.doNotify(receiver, S_COMMAND_SKILL_INSTANCE, message.toVariant());
    }
}

void SkillRuntimeCoordinator::notifySkillInstanceAmount(ServerPlayer *owner,
                                                        const SkillInstance &instance)
{
    if (!owner)
        return;
    foreach (ServerPlayer *receiver, m_room.getPlayers()) {
        if (!canReceiveSkillInstance(receiver, owner, instance))
            continue;
        const SkillInstanceMessage message = SkillInstanceMessage::makeAmount(
            owner->objectName(), instance.skillName, instance.instanceID,
            instance.hasAmountOverride, instance.amountOverride);
        m_room.doNotify(receiver, S_COMMAND_SKILL_INSTANCE, message.toVariant());
    }
}

void SkillRuntimeCoordinator::notifySkillInstanceCorrectState(
    ServerPlayer *owner, const SkillInstance &instance, const QString &operation,
    const QString &key, const QVariant &value)
{
    if (!owner)
        return;
    foreach (ServerPlayer *receiver, m_room.getPlayers()) {
        if (!canReceiveSkillInstance(receiver, owner, instance))
            continue;
        const SkillInstanceMessage message = SkillInstanceMessage::makeCorrectState(
            owner->objectName(), instance.skillName, instance.instanceID,
            operation, key, value);
        m_room.doNotify(receiver, S_COMMAND_SKILL_INSTANCE, message.toVariant());
    }
}

void SkillRuntimeCoordinator::notifySkillInstanceState(
    ServerPlayer *owner, const SkillInstance &instance, const QString &operation,
    const QString &key, const QVariant &value)
{
    m_room.m_notifier->notifySkillInstanceState(owner, instance, operation, key, value);
}

int SkillRuntimeCoordinator::getSkillInstanceAmount(const SkillInstanceRef &ref, bool *ok) const
{
    if (ok)
        *ok = false;
    if (!ref.isValid())
        return 0;
    ServerPlayer *owner = m_room.findPlayerByObjectName(ref.ownerObjectName, true);
    if (!owner)
        return 0;
    const SkillInstance *instance = owner->findSkillInstance(ref.key.skillName,
                                                              ref.key.instanceID);
    const Skill *skill = Sanguosha->getSkill(ref.key.skillName);
    const AmountSkillV2 *amountSkill = dynamic_cast<const AmountSkillV2 *>(skill);
    if (!instance || !amountSkill)
        return 0;
    if (ok)
        *ok = true;
    return instance->hasAmountOverride ? instance->amountOverride
                                       : amountSkill->getBaseAmount();
}

bool SkillRuntimeCoordinator::setSkillInstanceAmount(
    ServerPlayer *source, const SkillInstanceRef &ref, int amount, const QString &reason)
{
    bool valid = false;
    const int oldAmount = getSkillInstanceAmount(ref, &valid);
    if (!valid)
        return false;
    ServerPlayer *owner = m_room.findPlayerByObjectName(ref.ownerObjectName, true);
    const SkillInstance *before = owner
        ? owner->findSkillInstance(ref.key.skillName, ref.key.instanceID) : nullptr;
    if (!before)
        return false;
    if (before->hasAmountOverride && oldAmount == amount)
        return true;

    const QString guardKey = skillAmountGuardKey(ref);
    if (m_changingSkillAmounts.contains(guardKey)) {
        qWarning() << "Recursive skill amount change rejected:" << guardKey;
        return false;
    }
    m_changingSkillAmounts.insert(guardKey);
    SkillAmountRecursionGuard recursionGuard(m_changingSkillAmounts, guardKey);

    SkillAmountChangeStruct change;
    change.source = source;
    change.skillRef = ref;
    change.oldAmount = oldAmount;
    change.newAmount = amount;
    change.reason = reason;
    QVariant data = QVariant::fromValue(change);
    const bool intercepted = m_room.thread->trigger(EventSkillAmountChanging,
                                                     &m_room, owner, data);
    SkillAmountChangeStruct updated = data.value<SkillAmountChangeStruct>();
    updated.source = source;
    updated.skillRef = ref;
    updated.oldAmount = oldAmount;
    updated.reason = reason;
    updated.resetToBase = false;

    bool changed = false;
    if (!intercepted && !updated.canceled) {
        changed = owner->setSkillInstanceAmountOverride(ref.key.skillName,
                                                        ref.key.instanceID,
                                                        updated.newAmount);
        const SkillInstance *instance = owner->findSkillInstance(ref.key.skillName,
                                                                  ref.key.instanceID);
        if (changed && instance)
            notifySkillInstanceAmount(owner, *instance);
        if (changed) {
            QVariant changedData = QVariant::fromValue(updated);
            m_room.thread->trigger(EventSkillAmountChanged, &m_room, owner, changedData);
        }
    }
    return changed;
}

bool SkillRuntimeCoordinator::addSkillInstanceAmount(
    ServerPlayer *source, const SkillInstanceRef &ref, int delta, const QString &reason)
{
    bool valid = false;
    const int current = getSkillInstanceAmount(ref, &valid);
    if (!valid)
        return false;
    const qint64 next = static_cast<qint64>(current) + static_cast<qint64>(delta);
    if (next < std::numeric_limits<int>::min()
        || next > std::numeric_limits<int>::max()) {
        qWarning() << "Skill amount overflow rejected:"
                   << ref.key.skillName << ref.key.instanceID;
        return false;
    }
    return setSkillInstanceAmount(source, ref, static_cast<int>(next), reason);
}

bool SkillRuntimeCoordinator::resetSkillInstanceAmount(
    ServerPlayer *source, const SkillInstanceRef &ref, const QString &reason)
{
    bool valid = false;
    const int oldAmount = getSkillInstanceAmount(ref, &valid);
    if (!valid)
        return false;
    ServerPlayer *owner = m_room.findPlayerByObjectName(ref.ownerObjectName, true);
    const SkillInstance *before = owner
        ? owner->findSkillInstance(ref.key.skillName, ref.key.instanceID) : nullptr;
    const Skill *skill = Sanguosha->getSkill(ref.key.skillName);
    const AmountSkillV2 *amountSkill = dynamic_cast<const AmountSkillV2 *>(skill);
    if (!before || !amountSkill)
        return false;
    if (!before->hasAmountOverride)
        return true;

    const QString guardKey = skillAmountGuardKey(ref);
    if (m_changingSkillAmounts.contains(guardKey)) {
        qWarning() << "Recursive skill amount reset rejected:" << guardKey;
        return false;
    }
    m_changingSkillAmounts.insert(guardKey);
    SkillAmountRecursionGuard recursionGuard(m_changingSkillAmounts, guardKey);

    SkillAmountChangeStruct change;
    change.source = source;
    change.skillRef = ref;
    change.oldAmount = oldAmount;
    change.newAmount = amountSkill->getBaseAmount();
    change.reason = reason;
    change.resetToBase = true;
    QVariant data = QVariant::fromValue(change);
    const bool intercepted = m_room.thread->trigger(EventSkillAmountChanging,
                                                     &m_room, owner, data);
    SkillAmountChangeStruct updated = data.value<SkillAmountChangeStruct>();
    updated.source = source;
    updated.skillRef = ref;
    updated.oldAmount = oldAmount;
    updated.reason = reason;
    updated.resetToBase = true;

    bool changed = false;
    if (!intercepted && !updated.canceled) {
        if (updated.newAmount == amountSkill->getBaseAmount()) {
            changed = owner->resetSkillInstanceAmountOverride(ref.key.skillName,
                                                              ref.key.instanceID);
        } else {
            changed = owner->setSkillInstanceAmountOverride(ref.key.skillName,
                                                            ref.key.instanceID,
                                                            updated.newAmount);
        }
        const SkillInstance *instance = owner->findSkillInstance(ref.key.skillName,
                                                                  ref.key.instanceID);
        if (changed && instance)
            notifySkillInstanceAmount(owner, *instance);
        if (changed) {
            QVariant changedData = QVariant::fromValue(updated);
            m_room.thread->trigger(EventSkillAmountChanged, &m_room, owner, changedData);
        }
    }
    return changed;
}

bool SkillRuntimeCoordinator::setSkillInstanceCorrectState(
    ServerPlayer *source, const SkillInstanceRef &ref,
    const QString &key, const QVariant &value)
{
    Q_UNUSED(source);
    if (!ref.isValid() || key.isEmpty())
        return false;
    ServerPlayer *owner = m_room.findPlayerByObjectName(ref.ownerObjectName, true);
    const Skill *skill = Sanguosha->getSkill(ref.key.skillName);
    if (!owner || !isCorrectSkillV2Definition(skill)
        || !owner->setSkillInstanceCorrectStateValue(ref.key.skillName,
                                                      ref.key.instanceID,
                                                      key, value)) {
        return false;
    }
    const SkillInstance *instance = owner->findSkillInstance(ref.key.skillName,
                                                              ref.key.instanceID);
    if (instance)
        notifySkillInstanceCorrectState(owner, *instance, "set", key, value);
    return instance != nullptr;
}

bool SkillRuntimeCoordinator::removeSkillInstanceCorrectState(
    ServerPlayer *source, const SkillInstanceRef &ref, const QString &key)
{
    Q_UNUSED(source);
    if (!ref.isValid() || key.isEmpty())
        return false;
    ServerPlayer *owner = m_room.findPlayerByObjectName(ref.ownerObjectName, true);
    const Skill *skill = Sanguosha->getSkill(ref.key.skillName);
    if (!owner || !isCorrectSkillV2Definition(skill)
        || !owner->removeSkillInstanceCorrectStateValue(ref.key.skillName,
                                                         ref.key.instanceID,
                                                         key)) {
        return false;
    }
    const SkillInstance *instance = owner->findSkillInstance(ref.key.skillName,
                                                              ref.key.instanceID);
    if (instance)
        notifySkillInstanceCorrectState(owner, *instance, "remove", key);
    return instance != nullptr;
}

bool SkillRuntimeCoordinator::clearSkillInstanceCorrectState(
    ServerPlayer *source, const SkillInstanceRef &ref)
{
    Q_UNUSED(source);
    if (!ref.isValid())
        return false;
    ServerPlayer *owner = m_room.findPlayerByObjectName(ref.ownerObjectName, true);
    const Skill *skill = Sanguosha->getSkill(ref.key.skillName);
    if (!owner || !isCorrectSkillV2Definition(skill)
        || !owner->clearSkillInstanceCorrectState(ref.key.skillName,
                                                   ref.key.instanceID)) {
        return false;
    }
    const SkillInstance *instance = owner->findSkillInstance(ref.key.skillName,
                                                              ref.key.instanceID);
    if (instance)
        notifySkillInstanceCorrectState(owner, *instance, "clear");
    return instance != nullptr;
}

void SkillRuntimeCoordinator::addSkillInvalidity(
    ServerPlayer *target, const QString &skillName, const QString &sourceName,
    const QString &reason, int instanceId)
{
    if (!target)
        return;

    QStringList records = target->getTag("SkillInvalidityRecords").toStringList();
    QString recordSkillName = skillName;
    if (instanceId > 0)
        recordSkillName = QString("%1#%2").arg(skillName).arg(instanceId);
    const QString newRecord = QString("%1|%2|%3").arg(recordSkillName, sourceName, reason);

    if (!records.contains(newRecord)) {
        QVariant data = recordSkillName;
        if (m_room.thread->trigger(EventSkillInvalidated, &m_room, target, data))
            return;

        records.append(newRecord);
        target->setTag("SkillInvalidityRecords", records);
        m_room.doNotify(target, S_COMMAND_UPDATE_SKILL, recordSkillName);
    }
}

void SkillRuntimeCoordinator::removeSkillInvalidity(
    ServerPlayer *target, const QString &skillName, const QString &sourceName,
    const QString &reason, int instanceId)
{
    if (!target)
        return;

    QStringList records = target->getTag("SkillInvalidityRecords").toStringList();
    QString recordSkillName = skillName;
    if (instanceId > 0)
        recordSkillName = QString("%1#%2").arg(skillName).arg(instanceId);
    const QString targetRecord = QString("%1|%2|%3").arg(recordSkillName,
                                                          sourceName, reason);

    if (records.removeOne(targetRecord)) {
        target->setTag("SkillInvalidityRecords", records);
        m_room.doNotify(target, S_COMMAND_UPDATE_SKILL, recordSkillName);

        QVariant data = recordSkillName;
        m_room.thread->trigger(EventSkillValidityRestored, &m_room, target, data);
    }
}

void SkillRuntimeCoordinator::clearSkillInvalidityBySource(ServerPlayer *source)
{
    if (!source)
        return;
    const QString sourceName = source->objectName();

    foreach (ServerPlayer *player, m_room.getAlivePlayers()) {
        const QStringList records = player->getTag("SkillInvalidityRecords").toStringList();
        QStringList recordsToKeep;
        bool changed = false;

        foreach (const QString &record, records) {
            const QStringList parts = record.split("|");
            if (parts.size() >= 2) {
                if (parts.at(1) == sourceName)
                    changed = true;
                else
                    recordsToKeep.append(record);
            }
        }

        if (!changed)
            continue;
        player->setTag("SkillInvalidityRecords", recordsToKeep);

        foreach (const QString &removed, records) {
            if (recordsToKeep.contains(removed))
                continue;
            const QStringList parts = removed.split("|");
            if (parts.isEmpty())
                continue;
            QString skillName;
            const int instanceId = SkillInstanceUtils::parseName(parts.at(0), skillName);
            const QString notifyName = SkillInstanceUtils::formatName(skillName, instanceId);
            m_room.doNotify(player, S_COMMAND_UPDATE_SKILL, notifyName);

            QVariant data = notifyName;
            m_room.thread->trigger(EventSkillValidityRestored, &m_room, player, data);
        }
    }
}

SkillInstanceRef SkillRuntimeCoordinator::resolveSkillInstanceRootRef(
    const SkillInstanceRef &ref) const
{
    SkillInstanceRef current = ref;
    QList<SkillInstanceRef> visited;
    while (current.isValid() && !visited.contains(current)) {
        visited << current;
        ServerPlayer *owner = m_room.findPlayerByObjectName(current.ownerObjectName, true);
        if (!owner)
            return SkillInstanceRef();
        const SkillInstance *instance = owner->findSkillInstance(current.key.skillName,
                                                                  current.key.instanceID);
        if (!instance)
            return SkillInstanceRef();
        if (!instance->parentRef.isValid())
            return current;
        current = instance->parentRef;
    }
    return SkillInstanceRef();
}

bool SkillRuntimeCoordinator::resolveCardSkillInstance(CardUseStruct &use)
{
    if (!use.card || !use.from)
        return false;
    QString activationName = use.card->getActivationSkillName();
    int activationId = use.card->getActivationSkillInstanceId();
    if (!use.hasSkillActivationRequest && activationId == 0)
        return true;
    if (activationId == 0) {
        if (activationName.isEmpty())
            return true;
        const QList<int> ids = use.from->getSkillInstanceIds(activationName);
        if (ids.isEmpty())
            return false;
        activationId = ids.first();
    }
    if (activationName.isEmpty() || activationId < 0)
        return false;
    const ViewAsSkill *activationSkill = Sanguosha->getViewAsSkill(activationName);
    if (!use.card->isVirtualCard() && activationSkill == nullptr) {
        Card *mutableCard = const_cast<Card *>(use.card);
        mutableCard->setSkillInstanceId(0);
        mutableCard->setSourceSkill(QString(), 0);
        mutableCard->setActivationSkill(QString(), 0);
        use.hasSkillActivationRequest = false;
        use.sourceRef = SkillInstanceRef();
        use.activationRef = SkillInstanceRef();
        return true;
    }
    const ViewAsSkillV2 *activeSkill = dynamic_cast<const ViewAsSkillV2 *>(activationSkill);
    const bool hasActivationInstance = use.from->hasSkillInstance(activationName,
                                                                  activationId);
    const bool continuesViewAsEffect = activeSkill
        && hasViewAsSkillEffect(use.from, activationName);
    if (!hasActivationInstance && !continuesViewAsEffect)
        return false;

    use.activationRef = SkillInstanceRef(use.from->objectName(),
                                         SkillInstanceKey(activationName, activationId));
    use.sourceRef = resolveSkillInstanceRootRef(use.activationRef);
    if (!use.sourceRef.isValid()) {
        if (!continuesViewAsEffect)
            return false;
        use.sourceRef = use.activationRef;
    }

    Card *mutableCard = const_cast<Card *>(use.card);
    mutableCard->setActivationSkill(use.activationRef.key.skillName,
                                    use.activationRef.key.instanceID);
    mutableCard->setSourceSkill(use.sourceRef.key.skillName,
                                use.sourceRef.key.instanceID);

    if (activeSkill) {
        ActiveSkillRequest request;
        request.reason = m_room.roomRuntime()->state().getCurrentCardUseReason();
        request.pattern = m_room.roomRuntime()->state().getCurrentCardUsePattern();
        request.initiator = use.from;
        request.activationRef = use.activationRef;
        request.selectedCardIds = use.card->getSubcards();
        if (activeSkill->targetMode() != ViewAsSkillV2::NoTarget) {
            foreach (ServerPlayer *target, use.to)
                request.selectedTargetNames << target->objectName();
        }
        if (use.card->isKindOf("SkillCard")) {
            request.userString = qobject_cast<const SkillCard *>(
                use.card->getRealCard())->getUserString();
        }
        const Card *serverCard = m_room.resolveActiveSkillRequest(use.from,
                                                                  activeSkill,
                                                                  request);
        if (!serverCard)
            return false;
        use.changeCard(const_cast<Card *>(serverCard));
        const_cast<Card *>(use.card)->change_cards.clear();
    }
    return true;
}

bool SkillRuntimeCoordinator::reserveActiveSkillUsage(
    const ViewAsSkillV2 *skill, const SkillContext &context)
{
    if (!skill || skill->getLimitScope() == Skill::Limit_None)
        return true;
    if (skill->getLimitScope() == Skill::Limit_Custom)
        return skill->isUsable(context);
    ServerPlayer *holder = skill->getUsageHolder(context);
    if (!holder || !skill->isUsable(context))
        return false;
    const QString usageTagKey = skill->getUsageTagKey(context);
    const QString reservationKey = SkillInstanceUtils::formatUsageReservationKey(
        holder->objectName(), usageTagKey);
    return m_activeSkillUsageReservations.reserve(
        reservationKey, holder->getMark(usageTagKey), skill->getMaxUsageLimit(context));
}

void SkillRuntimeCoordinator::releaseActiveSkillUsage(
    const ViewAsSkillV2 *skill, const SkillContext &context)
{
    if (!skill || skill->getLimitScope() == Skill::Limit_None
        || skill->getLimitScope() == Skill::Limit_Custom) {
        return;
    }
    ServerPlayer *holder = skill->getUsageHolder(context);
    if (!holder)
        return;
    const QString reservationKey = SkillInstanceUtils::formatUsageReservationKey(
        holder->objectName(), skill->getUsageTagKey(context));
    m_activeSkillUsageReservations.release(reservationKey);
}

void SkillRuntimeCoordinator::commitActiveSkillUsage(
    const ViewAsSkillV2 *skill, const SkillContext &context)
{
    if (!skill || skill->getLimitScope() == Skill::Limit_None
        || skill->getLimitScope() == Skill::Limit_Custom) {
        return;
    }
    ServerPlayer *holder = skill->getUsageHolder(context);
    if (!holder)
        return;
    const QString reservationKey = SkillInstanceUtils::formatUsageReservationKey(
        holder->objectName(), skill->getUsageTagKey(context));
    if (m_activeSkillUsageReservations.release(reservationKey))
        skill->addUsage(context);
}

SkillExecutionRegistry::Guard SkillRuntimeCoordinator::beginSkillExecution(
    const QVariant &backingData)
{
    return m_room.roomRuntime()->skillExecutions().begin(backingData);
}

SkillExecutionRegistry::Guard SkillRuntimeCoordinator::beginSkillExecution(
    SkillContext &context, const QVariant &backingData)
{
    SkillExecutionRegistry::Guard guard =
        m_room.roomRuntime()->skillExecutions().begin(backingData);
    SkillExecutionRegistry::Entry *entry = guard.get();
    context.executionID = guard.executionID();
    context.original_data = entry ? &entry->backingData : nullptr;
    if (entry) {
        entry->immutableContextData = QVariant::fromValue(context);
        entry->contextData = QVariant::fromValue(context);
    }
    return guard;
}

SkillExecutionRegistry::Entry *SkillRuntimeCoordinator::findSkillExecution(
    qint64 executionID) const
{
    return m_room.roomRuntime()->skillExecutions().find(executionID);
}

SkillContext SkillRuntimeCoordinator::getSkillExecutionContext(qint64 executionID) const
{
    SkillExecutionRegistry::Entry *entry = findSkillExecution(executionID);
    SkillContext context = entry ? entry->contextData.value<SkillContext>() : SkillContext();
    if (entry)
        context.original_data = &entry->backingData;
    return context;
}

void SkillRuntimeCoordinator::setSkillExecutionContext(
    qint64 executionID, const SkillContext &context)
{
    SkillExecutionRegistry::Entry *entry = findSkillExecution(executionID);
    if (!entry)
        return;
    SkillContext stored = context;
    stored.original_data = &entry->backingData;
    entry->contextData = QVariant::fromValue(stored);
}
