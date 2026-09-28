#ifndef _YJCM2014_H
#define _YJCM2014_H

//#include "package.h"
//#include "card.h"
#include "skill.h"

class DingpinCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE DingpinCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class ShenxingCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ShenxingCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class BingyiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE BingyiCard();

    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class Jianying : public TriggerSkillV2
{
public:
    explicit Jianying(const QString &name = "jianying");
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override;
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override;
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override;
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override;

protected:
    QString jianying;
    bool consecutiveMatch(TriggerEvent event, Room *room, ServerPlayer *owner) const;
};

class XianzhouCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE XianzhouCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class XianzhouDamageCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE XianzhouDamageCard();

    void onUse(Room *room, CardUseStruct &card_use) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class SidiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE SidiCard();

    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class PindiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE PindiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class PingkouCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE PingkouCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class YJCM2014Package : public Package
{
    Q_OBJECT

public:
    YJCM2014Package();
};

void MigrateToNostalgiaYJCM2014(Package *pkg);

void MigrateToOLStYJ2014(Package *pkg);

#endif
