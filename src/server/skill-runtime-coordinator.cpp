#include "skill-runtime-coordinator.h"

#include "engine.h"
#include "protocol/skill-instance-message.h"
#include "room.h"
#include "room-notifier.h"
#include "room-runtime.h"
#include "roomthread.h"
#include "serverplayer.h"
#include "hegemony-mode.h"
#include "settings.h"

#include <limits>
#include <QDebug>

using namespace QSanProtocol;

bool SkillRuntimeCoordinator::canReceiveSkillInstance(const Room &room, const ServerPlayer *receiver,
                             const ServerPlayer *owner, const SkillInstance &instance)
{
    if (!receiver || !owner)
        return false;
    if (!Config.EnableHegemony) {
        if (receiver == owner)
            return true;
        const Skill *skill = Sanguosha->getSkill(instance.skillName);
        return instance.source != SourceHelper && instance.visible && skill && skill->isVisible();
    }

    const ServerPlayer *sourceOwner = owner;
    const SkillInstance *source = &instance;
    QSet<QString> visited;
    while (source) {
        // A public attachment must not disclose a concealed provider via parentRef.
        const QString identity = sourceOwner->objectName() + QChar('\x1f')
            + SkillInstanceUtils::formatName(source->skillName, source->instanceID);
        if (visited.contains(identity) || source->source == SourceHelper)
            return false;
        visited.insert(identity);
        if (receiver != sourceOwner) {
            const Skill *skill = Sanguosha->getSkill(source->skillName);
            if (!source->visible || !skill || !skill->isVisible())
                return false;
        }
        if (source->source == SourceAttached) {
            if (!source->parentRef.isValid())
                return false;
            sourceOwner = room.findPlayerByObjectName(source->parentRef.ownerObjectName, true);
            if (!sourceOwner)
                return false;
            source = sourceOwner->findSkillInstance(source->parentRef.key.skillName,
                                                    source->parentRef.key.instanceID);
            continue;
        }
        if (receiver == sourceOwner || source->source == SourceAcquired)
            return true;
        // Use this instance's binding, including a removed instance's snapshot.
        // A same-name skill on the other general must not reveal this instance.
        if (source->source == SourceInnate)
            return HegemonyMode::innateGeneralShown(sourceOwner, source->bindHead);
        return false;
    }
    return false;
}

namespace {

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

void recordAmountDescription(ServerPlayer *owner, ServerPlayer *source,
                             const SkillInstanceRef &ref, int value, const QString &reason)
{
    if (owner->hasSkillInstanceAmountOverride(ref.key.skillName, ref.key.instanceID)) {
        owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "__description_amount",
            QVariantMap{{"value", value}, {"source_player", source ? source->objectName() : QString()},
                        {"reason", reason}});
    } else {
        owner->removeSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "__description_amount");
    }
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
    return attachSkillToPlayer(player, skillName, parentRef, true);
}

SkillInstanceRef SkillRuntimeCoordinator::attachSkillToPlayer(
    ServerPlayer *player, const QString &skillName, const SkillInstanceRef &parentRef, bool visible)
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
    // Visibility is fixed before the first projection notification. Private
    // grants must never send a public upsert and hide themselves afterward.
    const int instanceId = player->createSkillInstance(skillName, SourceAttached,
                                                       parentRef, visible && skill->isVisible());
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
        if (visibleOnly && Config.EnableHegemony
            && !canReceiveSkillInstance(m_room, chooser, owner, *instance))
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
    QList<ServerPlayer *> lossLogReceivers;
    if (Config.EnableHegemony) {
        lossLogReceivers << owner;
        foreach (ServerPlayer *receiver, m_room.getAllPlayers(true)) {
            if (receiver != owner && canReceiveSkillInstance(m_room, receiver, owner, removed))
                lossLogReceivers << receiver;
        }
    }
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
        // Capture visibility before removal; an empty recipient list means broadcast.
        if (Config.EnableHegemony)
            m_room.sendLog(log, lossLogReceivers);
        else
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
            && skill && skill->isVisible()
            && (!Config.EnableHegemony
                || canReceiveSkillInstance(m_room, chooser, owner, *instance))) {
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
    return acquireSkillForSlot(player, skill->objectName(), true, open, getmark, eventAndLog);
}

int SkillRuntimeCoordinator::acquireSkill(ServerPlayer *player, const QString &skillName,
                                          bool open, bool getmark, bool eventAndLog)
{
    return acquireSkillForSlot(player, skillName, true, open, getmark, eventAndLog);
}

int SkillRuntimeCoordinator::acquireSkillForSlot(ServerPlayer *player, const QString &skillName,
                                                 bool head, bool open, bool getmark,
                                                 bool eventAndLog)
{
    const Skill *skill = Sanguosha->getSkill(skillName);
    if (!skill)
        return 0;

    const int instanceId = player->acquireSkill(skillName, head);
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
        if (helper) {
            // createSkillInstance defaults to an unbound helper.  Bind it to
            // the same general slot before the first client upsert/event.
            const_cast<SkillInstance *>(helper)->bindHead = head ? 1 : 2;
            notifySkillInstanceUpsert(player, *helper);
        }
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
            if (canReceiveSkillInstance(m_room, receiver, owner, instance))
                entries << skillInstanceMessage(owner, instance, receiver == owner);
        }
    }
    const SkillInstanceMessage message = SkillInstanceMessage::makeSnapshot(entries);
    m_room.doNotify(receiver, S_COMMAND_SKILL_INSTANCE, message.toVariant());
    // Snapshot replacement clears client instances first; private preshow must
    // follow it, including reconnect and reveal snapshots sent to other owners.
    if (Config.EnableHegemony) receiver->notifyPreshow();
}

void SkillRuntimeCoordinator::notifySkillInstanceUpsert(ServerPlayer *owner,
                                                        const SkillInstance &instance)
{
    foreach (ServerPlayer *receiver, m_room.getPlayers()) {
        if (!canReceiveSkillInstance(m_room, receiver, owner, instance))
            continue;
        const SkillInstanceMessage message = SkillInstanceMessage::makeUpsert(
            skillInstanceMessage(owner, instance, receiver == owner));
        m_room.doNotify(receiver, S_COMMAND_SKILL_INSTANCE, message.toVariant());
    }
    if (Config.EnableHegemony && instance.source == SourceInnate) owner->notifyPreshow();
}

void SkillRuntimeCoordinator::notifySkillInstanceRemove(ServerPlayer *owner,
                                                        const SkillInstance &instance)
{
    // The removed attachment's provider may already be gone. A filtered snapshot
    // clears stale client entries without revealing a now-private source identity.
    if (Config.EnableHegemony && instance.source == SourceAttached) {
        foreach (ServerPlayer *receiver, m_room.getPlayers())
            notifySkillInstanceSnapshot(receiver);
        return;
    }
    foreach (ServerPlayer *receiver, m_room.getPlayers()) {
        if (!canReceiveSkillInstance(m_room, receiver, owner, instance))
            continue;
        const SkillInstanceMessage message = SkillInstanceMessage::makeRemove(
            owner->objectName(), instance.skillName, instance.instanceID);
        m_room.doNotify(receiver, S_COMMAND_SKILL_INSTANCE, message.toVariant());
    }
    if (Config.EnableHegemony && instance.source == SourceInnate) owner->notifyPreshow();
}

void SkillRuntimeCoordinator::notifySkillInstanceAmount(ServerPlayer *owner,
                                                        const SkillInstance &instance)
{
    if (!owner)
        return;
    foreach (ServerPlayer *receiver, m_room.getPlayers()) {
        if (!canReceiveSkillInstance(m_room, receiver, owner, instance))
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
        if (!canReceiveSkillInstance(m_room, receiver, owner, instance))
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
    if (Config.EnableHegemony && !canReceiveSkillInstance(m_room, owner, owner, instance))
        return;
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
        if (changed) recordAmountDescription(owner, source, ref, updated.newAmount, reason);
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
        if (changed) recordAmountDescription(owner, source, ref, updated.newAmount, reason);
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
    if (instance) {
        QVariantMap sources = owner->getSkillInstanceStateValue(ref.key.skillName,
            ref.key.instanceID, "__description_correct").toMap();
        sources.insert(key, QVariantMap{{"value", value}, {"source_player", source ? source->objectName() : QString()}});
        owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "__description_correct", sources);
    }
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
    QVariantMap sources = owner->getSkillInstanceStateValue(ref.key.skillName,
        ref.key.instanceID, "__description_correct").toMap();
    sources.remove(key);
    owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "__description_correct", sources);
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
    owner->removeSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "__description_correct");
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
    return SkillInstanceUtils::resolveRootRef(ref, [this](const SkillInstanceRef &current) {
        const ServerPlayer *owner = m_room.findPlayerByObjectName(current.ownerObjectName, true);
        return owner ? owner->findSkillInstance(current.key.skillName, current.key.instanceID) : nullptr;
    });
}

bool SkillRuntimeCoordinator::resolveCardSkillInstance(CardUseStruct &use)
{
    if (!use.card || !use.from)
        return false;
    QString activationName = use.card->getActivationSkillName();
    int activationId = use.card->getActivationSkillInstanceId();
    if (activationName.isEmpty() && use.card->getTypeId() == Card::TypeSkill) {
        // Native C++ viewAs() may return an unnamed SkillCard; Card::Parse
        // derives the same name for its serialized counterpart.
        QString className = QString::fromLatin1(use.card->metaObject()->className());
        if (className.endsWith(QStringLiteral("Card"))) {
            className.chop(4);
            const QString candidate = className.toLower();
            for (int id : use.from->getSkillInstanceIds(candidate)) {
                if (use.from->getSkillInstanceStateValue(candidate, id,
                        QStringLiteral("legacy_activation_lifecycle")).toBool()) {
                    activationName = candidate;
                    break;
                }
            }
        }
    }
    const auto *equipmentViewAs = dynamic_cast<const ViewAsSkillV2 *>(
        Sanguosha->getViewAsSkill(activationName));
    if (equipmentViewAs && equipmentViewAs->isEquipSkill() && activationId == 0) {
        // Also rebuild legacy AI/card-string submissions with instance zero.
        // Equipment ownership is authoritative; a supplied instance cannot grant it.
        if (!use.card->isVirtualCard())
            return false;
        SkillContext source;
        source.owner = use.from;
        source.invoker = use.from;
        source.initiator = use.from;
        if (!equipmentViewAs->prepareEquipSource(&m_room, source)) return false;
        ActiveSkillRequest request;
        request.reason = m_room.roomRuntime()->state().getCurrentCardUseReason();
        request.pattern = m_room.roomRuntime()->state().getCurrentCardUsePattern();
        request.initiator = use.from;
        request.activationRef = source.activationRef;
        request.setCardSelection(use.card);
        for (ServerPlayer *target : use.to)
            request.selectedTargetNames << target->objectName();
        const Card *rebuilt = m_room.resolveActiveSkillRequest(use.from, equipmentViewAs, request);
        if (!rebuilt) return false;
        use.activationRef = source.activationRef;
        use.sourceRef = source.sourceRef;
        use.changeCard(const_cast<Card *>(rebuilt));
        const_cast<Card *>(use.card)->change_cards.clear();
        // The server-created equipment conversion has no other owner. Keep it
        // alive across use copies, then retire it through CardUseStruct's deleter.
        use.setOwnedCard(const_cast<Card *>(rebuilt));
        return true;
    }
    if (!use.hasSkillActivationRequest && activationId == 0) {
        // Old card strings carry only a skill name. Attribute an opt-in private
        // attachment before entering the existing use/response lifecycle.
        QList<int> projected;
        bool hasOrdinarySource = false;
        for (const SkillInstance &instance : use.from->getSkillInstances()) {
            if (instance.skillName != activationName) continue;
            if (instance.source == SourceAttached
                && use.from->getSkillInstanceStateValue(instance.skillName, instance.instanceID,
                    QStringLiteral("legacy_activation_lifecycle")).toBool()) {
                if (use.from->getValidSkillInstanceIds(activationName).contains(instance.instanceID))
                    projected << instance.instanceID;
            } else {
                hasOrdinarySource = true;
            }
        }
        if (projected.isEmpty() || hasOrdinarySource) return true;
        activationId = projected.first();
        if (projected.size() > 1) {
            // Multiple providers remain distinct even for an old AI/card string.
            QStringList choices;
            for (int id : projected)
                choices << SkillInstanceUtils::formatName(activationName, id);
            const QString choice = m_room.askForChoice(use.from, activationName, choices.join("+"));
            const int index = choices.indexOf(choice);
            if (index < 0) return false;
            activationId = projected.at(index);
        }
    }
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
    const bool continuesViewAsEffect = activationSkill
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
        request.setCardSelection(use.card);
        if (activeSkill->targetMode() != ViewAsSkillV2::NoTarget) {
            foreach (ServerPlayer *target, use.to)
                request.selectedTargetNames << target->objectName();
        }
        const Card *serverCard = m_room.resolveActiveSkillRequest(use.from,
                                                                  activeSkill,
                                                                  request);
        if (!serverCard)
            return false;
        use.changeCard(const_cast<Card *>(serverCard));
        const_cast<Card *>(use.card)->change_cards.clear();
        // The server-created card must retire with the submitted use.
        use.setOwnedCard(const_cast<Card *>(serverCard));
    }
    return true;
}

QVariantMap SkillRuntimeCoordinator::describeSkillUsage(ServerPlayer *owner,
                                                       const SkillInstance &instance) const
{
    const Skill *skill = Sanguosha->getSkill(instance.skillName);
    if (!owner || !skill) return QVariantMap{{"scope", "unavailable"}};
    const Skill::LimitScope scope = skill->getLimitScope();
    if (scope == Skill::Limit_None || scope == Skill::Limit_Custom) {
        // Explicit adapters for legacy/custom rules: never guess a mark from the
        // skill name. These properties name the exact authoritative counter.
        const QString mark = skill->property("DescriptionUsageMark").toString();
        const QString stateKey = skill->property("DescriptionUsageState").toString();
        const QString scopeLabel = skill->property("DescriptionUsageScope").toString();
        if ((!mark.isEmpty() || !stateKey.isEmpty()) && !scopeLabel.isEmpty()
            && skill->property("DescriptionUsageLimit").isValid()) {
            const int used = !mark.isEmpty() ? owner->getMark(mark)
                : owner->getSkillInstanceStateValue(instance.skillName, instance.instanceID, stateKey, 0).toInt();
            const QString counter = owner->objectName() + QChar('\x1f')
                + (!mark.isEmpty() ? "mark:" + mark : "state:" + SkillInstanceUtils::formatName(
                    instance.skillName, instance.instanceID) + ":" + stateKey);
            QVariantMap result{{"scope", "custom"}, {"scope_label", scopeLabel}, {"used", used},
                {"limit", skill->property("DescriptionUsageLimit")}, {"counter", counter},
                {"label", skill->property("DescriptionUsageLabel").toString()}};
            const QString reservedMark = skill->property("DescriptionUsageReservedMark").toString();
            if (!reservedMark.isEmpty()) result.insert("reserved", owner->getMark(reservedMark));
            return result;
        }
    }
    if (scope == Skill::Limit_None) return QVariantMap{{"scope", "none"}};
    if (scope == Skill::Limit_Custom) {
        // Custom counters have no common mark/holder contract. Authors publish an
        // owner-only snapshot alongside the very state that drives their rule.
        QVariantMap result{{"scope", "custom"}};
        const QVariantMap supplied = owner->getSkillInstanceStateValue(instance.skillName,
            instance.instanceID, "__description_usage").toMap();
        for (const QString &key : {QStringLiteral("label"), QStringLiteral("scope_label"),
                                  QStringLiteral("counter")}) {
            if (supplied.value(key).userType() == QMetaType::QString)
                result.insert(key, supplied.value(key));
        }
        for (const QString &key : {QStringLiteral("used"), QStringLiteral("limit"), QStringLiteral("reserved")}) {
            const QVariant value = supplied.value(key);
            const int type = value.userType();
            const bool numeric = type == QMetaType::Int || type == QMetaType::UInt
                || type == QMetaType::LongLong || type == QMetaType::ULongLong || type == QMetaType::Double;
            bool ok = false;
            const qlonglong number = value.toLongLong(&ok);
            if (numeric && ok && number >= 0 && number <= std::numeric_limits<int>::max()
                && value.toDouble() == static_cast<double>(number)) result.insert(key, static_cast<int>(number));
        }
        // An author counter is always scoped by holder, never a bare instance ID.
        if (result.contains("counter"))
            result["counter"] = owner->objectName() + QChar('\x1f') + result.value("counter").toString();
        return result;
    }
    SkillContext context;
    context.skill_name = instance.skillName;
    context.owner = context.invoker = context.initiator = owner;
    context.instanceID = instance.instanceID;
    context.activationRef = SkillInstanceRef(owner->objectName(),
        SkillInstanceKey(instance.skillName, instance.instanceID));
    context.sourceRef = resolveSkillInstanceRootRef(context.activationRef);
    if (const auto *amountSkill = dynamic_cast<const AmountSkillV2 *>(skill))
        context.amount = instance.hasAmountOverride ? instance.amountOverride : amountSkill->getBaseAmount();

    QString scopeName;
    switch (scope) {
    case Skill::Limit_Round: scopeName = "round"; break;
    case Skill::Limit_Turn: scopeName = "turn"; break;
    case Skill::Limit_Phase: scopeName = "phase"; break;
    case Skill::Limit_Game: scopeName = "game"; break;
    default: return QVariantMap{{"scope", "unavailable"}};
    }
    QVariantMap result{{"scope", scopeName}, {"phase", skill->getPhaseName()}};
    // Use exactly the same resolution as reserve/commit, including fail-closed roots.
    // This is a quota snapshot, not a phase/target/cost/activation query.
    ServerPlayer *holder = skill->getUsageHolder(context);
    const QString mark = skill->getUsageTagKey(context);
    if (!holder || mark.isEmpty()) {
        result.insert("unavailable", true);
        return result;
    }
    const QString counter = SkillInstanceUtils::formatUsageReservationKey(holder->objectName(), mark);
    result.insert("counter", counter);
    result.insert("holder", holder->objectName());
    result.insert("mark", mark);
    result.insert("used", holder->getMark(mark));
    result.insert("limit", skill->getMaxUsageLimit(context));
    result.insert("reserved", m_activeSkillUsageReservations.count(counter));
    // The authoritative reference catches shared counters even when the holder is the same player.
    const SkillInstanceRef usageRef = skill->getUsageRef(context);
    result.insert("shared", !(usageRef == context.activationRef));
    return result;
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
    const bool reserved = m_activeSkillUsageReservations.reserve(
        reservationKey, holder->getMark(usageTagKey), skill->getMaxUsageLimit(context));
    if (reserved && m_room.getThread()) m_room.getThread()->markSkillDescriptionsDirty();
    return reserved;
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
    if (m_activeSkillUsageReservations.release(reservationKey))
        if (m_room.getThread()) m_room.getThread()->markSkillDescriptionsDirty();
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
    if (m_activeSkillUsageReservations.release(reservationKey)) {
        if (m_room.getThread()) m_room.getThread()->markSkillDescriptionsDirty();
        skill->addUsage(context);
    }
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
