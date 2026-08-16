#ifndef SKILL_RUNTIME_COORDINATOR_H
#define SKILL_RUNTIME_COORDINATOR_H

#include "skill.h"
#include "skill-execution-registry.h"
#include "skill-instance-utils.h"

#include <QSet>

class Room;

class SkillRuntimeCoordinator
{
public:
    explicit SkillRuntimeCoordinator(Room &room);

    void attachSkillToPlayer(ServerPlayer *player, const QString &skillName);
    SkillInstanceRef attachSkillToPlayer(ServerPlayer *player, const QString &skillName,
                                         const SkillInstanceRef &parentRef);
    bool detachAttachedSkill(const SkillInstanceRef &ref);
    int detachSkillFromPlayer(ServerPlayer *player, const QString &skillName,
                              bool isEquip, bool acquireOnly, bool eventAndLog);
    int discardSkillInstance(ServerPlayer *chooser, ServerPlayer *owner,
                             const QString &skillName, bool eventAndLog);
    void handleAcquireDetachSkills(ServerPlayer *player, const QStringList &skillNames,
                                   bool acquireOnly, bool getmark, bool eventAndLog);
    int acquireSkill(ServerPlayer *player, const Skill *skill, bool open,
                     bool getmark, bool eventAndLog);
    int acquireSkill(ServerPlayer *player, const QString &skillName, bool open,
                     bool getmark, bool eventAndLog);

    void notifySkillInstanceSnapshot(ServerPlayer *receiver);
    void notifySkillInstanceUpsert(ServerPlayer *owner, const SkillInstance &instance);
    void notifySkillInstanceRemove(ServerPlayer *owner, const SkillInstance &instance);
    void notifySkillInstanceAmount(ServerPlayer *owner, const SkillInstance &instance);
    void notifySkillInstanceCorrectState(ServerPlayer *owner, const SkillInstance &instance,
                                         const QString &operation,
                                         const QString &key = QString(),
                                         const QVariant &value = QVariant());
    void notifySkillInstanceState(ServerPlayer *owner, const SkillInstance &instance,
                                  const QString &operation,
                                  const QString &key = QString(),
                                  const QVariant &value = QVariant());

    int getSkillInstanceAmount(const SkillInstanceRef &ref, bool *ok = nullptr) const;
    bool setSkillInstanceAmount(ServerPlayer *source, const SkillInstanceRef &ref, int amount,
                                const QString &reason);
    bool addSkillInstanceAmount(ServerPlayer *source, const SkillInstanceRef &ref, int delta,
                                const QString &reason);
    bool resetSkillInstanceAmount(ServerPlayer *source, const SkillInstanceRef &ref,
                                  const QString &reason);
    bool setSkillInstanceCorrectState(ServerPlayer *source, const SkillInstanceRef &ref,
                                      const QString &key, const QVariant &value);
    bool removeSkillInstanceCorrectState(ServerPlayer *source, const SkillInstanceRef &ref,
                                         const QString &key);
    bool clearSkillInstanceCorrectState(ServerPlayer *source, const SkillInstanceRef &ref);

    void addSkillInvalidity(ServerPlayer *target, const QString &skillName,
                            const QString &sourceName, const QString &reason,
                            int instanceId);
    void removeSkillInvalidity(ServerPlayer *target, const QString &skillName,
                               const QString &sourceName, const QString &reason,
                               int instanceId);
    void clearSkillInvalidityBySource(ServerPlayer *source);

    SkillInstanceRef resolveSkillInstanceRootRef(const SkillInstanceRef &ref) const;
    bool resolveCardSkillInstance(CardUseStruct &use);

    bool reserveActiveSkillUsage(const ViewAsSkillV2 *skill, const SkillContext &context);
    void releaseActiveSkillUsage(const ViewAsSkillV2 *skill, const SkillContext &context);
    void commitActiveSkillUsage(const ViewAsSkillV2 *skill, const SkillContext &context);

    SkillExecutionRegistry::Guard beginSkillExecution(const QVariant &backingData);
    SkillExecutionRegistry::Guard beginSkillExecution(SkillContext &context,
                                                       const QVariant &backingData);
    SkillExecutionRegistry::Entry *findSkillExecution(qint64 executionID) const;
    SkillContext getSkillExecutionContext(qint64 executionID) const;
    void setSkillExecutionContext(qint64 executionID, const SkillContext &context);

private:
    int chooseSkillInstance(ServerPlayer *chooser, ServerPlayer *owner,
                            const QString &skillName, bool visibleOnly,
                            bool acquiredOnly);
    bool removeSkillInstanceFromPlayer(ServerPlayer *owner, const QString &skillName,
                                       int instanceId, bool isEquip, bool eventAndLog);

    Room &m_room;
    QSet<QString> m_changingSkillAmounts;
    SkillInstanceUtils::UsageReservationLedger m_activeSkillUsageReservations;
};

#endif
