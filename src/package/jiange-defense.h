#ifndef _JIANGE_DEFENSE_H
#define _JIANGE_DEFENSE_H

#include "package.h"
#include "card.h"

class JianGeDefensePackage : public Package
{
    Q_OBJECT

public:
    JianGeDefensePackage();
};

class JGJiaoxieCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JGJiaoxieCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
};

class JGYingjiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JGYingjiCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
};

class JGKedingCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JGKedingCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *) const;
};

class JGHanjunCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JGHanjunCard();

};


#endif