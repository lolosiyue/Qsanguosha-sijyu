#ifndef DOUDIZHU_H
#define DOUDIZHU_H

#include "package.h"
#include "standard.h"
//#include "skill.h"

class DoudizhuPackage : public Package
{
    Q_OBJECT

public:
    DoudizhuPackage();
};

class FeiyangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE FeiyangCard();
    void onUse(Room *room, CardUseStruct &use) const;
};

class JingubangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JingubangCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class Jingubang : public Weapon
{
    Q_OBJECT

public:
    Q_INVOKABLE Jingubang(Card::Suit suit, int number);
};

class SitianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE SitianCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class LisaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE LisaoCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
};

class QixinCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE QixinCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};




#endif