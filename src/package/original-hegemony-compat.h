#ifndef ORIGINAL_HEGEMONY_COMPAT_H
#define ORIGINAL_HEGEMONY_COMPAT_H

#include "skill.h"

inline bool isHegemonyCardClassName(const QString &name)
{
    // H is a namespace prefix only before an uppercase class name. Native
    // Horse/Halberd and skill cards such as HuashenCard are not HEG cards.
    return name.size() > 1 && name.at(0) == QLatin1Char('H')
        && name.at(1) >= QLatin1Char('A') && name.at(1) <= QLatin1Char('Z');
}


// Shared summon eligibility for the legacy and V2 entry points.
bool canSummonOriginalHegemonyArray(const Player *player, const QString &skillName, const QString &arrayType);

// The 2014 selector returns the decision maker, not necessarily the skill owner.
// Keep that contract separate from TriggerSkillV2's owner-indexed selectors.
class HegemonyTriggerSkill : public TriggerSkill
{
public:
    explicit HegemonyTriggerSkill(const QString &name);

    virtual int getPriority() const;
    int getPriority(TriggerEvent event) const override;
    bool triggerable(const ServerPlayer *target) const override;
    virtual TriggerList triggerable(TriggerEvent event, Room *room,
                                   ServerPlayer *target, QVariant &data) const;
    virtual QStringList triggerable(TriggerEvent event, Room *room, ServerPlayer *target,
                                    QVariant &data, ServerPlayer *&askWho) const;
    virtual bool cost(TriggerEvent event, Room *room, ServerPlayer *target,
                      QVariant &data, ServerPlayer *askWho = nullptr) const;
    virtual bool effect(TriggerEvent event, Room *room, ServerPlayer *target,
                        QVariant &data, ServerPlayer *askWho = nullptr) const;

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *target,
                 QVariant &data) const override;
};

class OriginalHegemonyMasochismSkill : public HegemonyTriggerSkill
{
public:
    explicit OriginalHegemonyMasochismSkill(const QString &name);
    bool effect(TriggerEvent event, Room *room, ServerPlayer *target,
                QVariant &data, ServerPlayer *askWho = nullptr) const override;
    virtual void onDamaged(ServerPlayer *target, const DamageStruct &damage) const = 0;
};

class OriginalHegemonyPhaseChangeSkill : public HegemonyTriggerSkill
{
public:
    explicit OriginalHegemonyPhaseChangeSkill(const QString &name);
    bool effect(TriggerEvent event, Room *room, ServerPlayer *target,
                QVariant &data, ServerPlayer *askWho = nullptr) const override;
    virtual bool onPhaseChange(ServerPlayer *target) const = 0;
};

class OriginalHegemonyDrawCardsSkill : public HegemonyTriggerSkill
{
public:
    explicit OriginalHegemonyDrawCardsSkill(const QString &name);
    bool effect(TriggerEvent event, Room *room, ServerPlayer *target,
                QVariant &data, ServerPlayer *askWho = nullptr) const override;
    virtual int getDrawNum(ServerPlayer *target, int count) const = 0;
};

class OriginalHegemonyGameStartSkill : public HegemonyTriggerSkill
{
public:
    explicit OriginalHegemonyGameStartSkill(const QString &name);
    bool effect(TriggerEvent event, Room *room, ServerPlayer *target,
                QVariant &data, ServerPlayer *askWho = nullptr) const override;
    virtual void onGameStart(ServerPlayer *target) const = 0;
};

class OriginalHegemonyBattleArraySkill : public HegemonyTriggerSkill
{
public:
    OriginalHegemonyBattleArraySkill(const QString &name, const QString &arrayType);
    bool triggerable(const ServerPlayer *target) const override;
    virtual void summonFriends(ServerPlayer *player) const;
    QString getArrayType() const { return m_arrayType; }

private:
    QString m_arrayType;
};

class OriginalHegemonyDetachEffectSkill : public HegemonyTriggerSkill
{
public:
    OriginalHegemonyDetachEffectSkill(const QString &skillName,
                                     const QString &pileName = QString());
    QStringList triggerable(TriggerEvent event, Room *room, ServerPlayer *target,
                            QVariant &data, ServerPlayer *&askWho) const override;
    bool effect(TriggerEvent event, Room *room, ServerPlayer *target,
                QVariant &data, ServerPlayer *askWho = nullptr) const override;
    virtual void onSkillDetached(Room *room, ServerPlayer *target) const;
private:
    QString m_skillName;
    QString m_pileName;
};

// RoomThread calls this once at the group's priority, bypassing legacy owner
// dispatch for these definitions. Selectors (including record-only ones) run
// even when the event target does not own any of the group's skills.
bool dispatchOriginalHegemonySkills(TriggerEvent event, Room *room,
    ServerPlayer *eventTarget, QVariant &data,
    const QList<const HegemonyTriggerSkill *> &samePriority);

#endif
