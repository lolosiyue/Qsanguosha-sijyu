#ifndef YITIANPACKAGE_H
#define YITIANPACKAGE_H

#include "standard.h"

class YitianPackage : public Package
{
    Q_OBJECT

public:
    YitianPackage();
};

class YitianSword : public Weapon
{
    Q_OBJECT

public:
    Q_INVOKABLE YitianSword(Card::Suit suit = Spade, int number = 6);

    void onUninstall(ServerPlayer *player) const;
};

class LianliSlashCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE LianliSlashCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    const Card *validate(CardUseStruct &cardUse) const;
};

class YitianCardPackage : public Package
{
    Q_OBJECT

public:
    YitianCardPackage();
};

#endif // YITIANPACKAGE_H
