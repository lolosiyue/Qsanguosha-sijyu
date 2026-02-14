#ifndef YJCM2022_H
#define YJCM2022_H

#include "package.h"
#include "card.h"
//#include "skill.h"

class YJCM2022Package : public Package
{
    Q_OBJECT

public:
    YJCM2022Package();
};

class BiejunCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE BiejunCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class ShujianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ShujianCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

#endif