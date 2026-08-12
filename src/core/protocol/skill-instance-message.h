#ifndef SKILL_INSTANCE_MESSAGE_H
#define SKILL_INSTANCE_MESSAGE_H

#include "skill-instance-types.h"

#include <QList>
#include <QVariant>

struct SkillInstanceEntryMessage
{
    QString ownerName;
    SkillInstance instance;
    QVariantMap privateState;

    QVariant toVariant() const;
    bool tryParse(const QVariant &value);
};

struct SkillInstanceMessage
{
    enum Action {
        Invalid,
        Snapshot,
        Upsert,
        Remove,
        Amount,
        CorrectState,
        State
    };

    Action action = Invalid;
    QList<SkillInstanceEntryMessage> entries;
    SkillInstanceEntryMessage entry;
    QString ownerName;
    QString skillName;
    int instanceId = 0;
    bool hasAmountOverride = false;
    int amount = 0;
    QString operation;
    QString key;
    QVariant value;

    static SkillInstanceMessage makeSnapshot(const QList<SkillInstanceEntryMessage> &entries);
    static SkillInstanceMessage makeUpsert(const SkillInstanceEntryMessage &entry);
    static SkillInstanceMessage makeRemove(const QString &ownerName, const QString &skillName,
                                           int instanceId);
    static SkillInstanceMessage makeAmount(const QString &ownerName, const QString &skillName,
                                           int instanceId, bool hasOverride, int amount);
    static SkillInstanceMessage makeCorrectState(const QString &ownerName, const QString &skillName,
                                                 int instanceId, const QString &operation,
                                                 const QString &key, const QVariant &value);
    static SkillInstanceMessage makeState(const QString &ownerName, const QString &skillName,
                                          int instanceId, const QString &operation,
                                          const QString &key, const QVariant &value);

    QVariant toVariant() const;
    bool tryParse(const QVariant &value);
};

#endif
