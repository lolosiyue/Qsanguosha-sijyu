#ifndef _FIRE_H
#define _FIRE_H

#include "package.h"
#include "card.h"

class FirePackage : public Package
{
    Q_OBJECT

public:
    FirePackage();
};

class QuhuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE QuhuCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class QiangxiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE QiangxiCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class TianyiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TianyiCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class YeyanCard : public SkillCard
{
    Q_OBJECT

public:
    void damage(ServerPlayer *shenzhouyu, ServerPlayer *target, int point) const;
};

class GreatYeyanCard : public YeyanCard
{
    Q_OBJECT

public:
    Q_INVOKABLE GreatYeyanCard();

    bool targetFilter(const QList<const Player *> &targets,const Player *to_select, const Player *Self) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select,const Player *Self, int &maxVotes) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class SmallYeyanCard : public YeyanCard
{
    Q_OBJECT

public:
    Q_INVOKABLE SmallYeyanCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
    void onEffect(CardEffectStruct &effect) const;
};

class QixingCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE QixingCard();
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class KuangfengCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE KuangfengCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class DawuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE DawuCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};











#endif

