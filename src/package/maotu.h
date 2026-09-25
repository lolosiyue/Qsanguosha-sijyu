#ifndef MAOTU_H
#define MAOTU_H

//#include "package.h"
#include "standard.h"

class MaotuPackage : public Package
{
    Q_OBJECT

public:
    MaotuPackage();
};

class MTYinglveCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MTYinglveCard();
    bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class MTHongwuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MTHongwuCard();
    bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
};

#endif