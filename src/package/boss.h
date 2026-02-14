#ifndef _BOSS_H
#define _BOSS_H

#include "package.h"
#include "skill.h"
//#include "standard.h"
#include "maneuvering.h"

class BossModePackage : public Package
{
    Q_OBJECT

public:
    BossModePackage();
};

class HulaoPassPackage : public Package
{
    Q_OBJECT

public:
    HulaoPassPackage();
};

class JiwuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JiwuCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class Wushuangji: public Weapon {
    Q_OBJECT

public:
    Q_INVOKABLE Wushuangji(Card::Suit suit, int number);
};

class Baihuapao: public Armor {
    Q_OBJECT

public:
    Q_INVOKABLE Baihuapao(Card::Suit suit, int number);
};

class Shimandai: public Armor {
    Q_OBJECT

public:
    Q_INVOKABLE Shimandai(Card::Suit suit, int number);
};

class Zijinguan: public Treasure {
    Q_OBJECT

public:
    Q_INVOKABLE Zijinguan(Card::Suit suit, int number);
};

class Lianjunshengyan : public GlobalEffect
{
    Q_OBJECT

public:
    Q_INVOKABLE Lianjunshengyan(Card::Suit suit, int number);
    void onEffect(CardEffectStruct &effect) const;

};


#endif