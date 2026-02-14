#ifndef GODLAILAILAI_H
#define GODLAILAILAI_H

//#include "package.h"
//#include "card.h"
//#include "standard.h"
#include "maneuvering.h"

class GodlailailaiPackage: public Package {
    Q_OBJECT

public:
    GodlailailaiPackage();
};

class KuangxiCard: public SkillCard {
    Q_OBJECT

public:
    Q_INVOKABLE KuangxiCard();
    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual void onEffect(CardEffectStruct &effect) const;
};

class GodBlade: public Weapon {
    Q_OBJECT

public:
    Q_INVOKABLE GodBlade(Card::Suit suit, int number);
};

class GodHalberd: public Weapon {
    Q_OBJECT

public:
    Q_INVOKABLE GodHalberd(Card::Suit suit, int number);
};

class GodQin: public Weapon {
    Q_OBJECT

public:
    Q_INVOKABLE GodQin(Card::Suit suit, int number);
};

class GodSword: public Weapon {
    Q_OBJECT

public:
    Q_INVOKABLE GodSword(Card::Suit suit, int number);
};

class GodDoubleSword : public Weapon {
    Q_OBJECT

public:
    Q_INVOKABLE GodDoubleSword(Card::Suit suit, int number);
};

class GodBow : public Weapon {
    Q_OBJECT

public:
    Q_INVOKABLE GodBow(Card::Suit suit, int number);
};

class GodAxe : public Weapon {
    Q_OBJECT

public:
    Q_INVOKABLE GodAxe(Card::Suit suit, int number);
};

class GodDiagram: public Armor {
    Q_OBJECT

public:
    Q_INVOKABLE GodDiagram(Card::Suit suit, int number);
};

class GodPao: public Armor {
    Q_OBJECT

public:
    Q_INVOKABLE GodPao(Card::Suit suit, int number);
};

class GodHorse: public DefensiveHorse {
    Q_OBJECT

public:
    Q_INVOKABLE GodHorse(Card::Suit suit, int number);
};

class GodDeer: public OffensiveHorse {
    Q_OBJECT

public:
    Q_INVOKABLE GodDeer(Card::Suit suit, int number);
};

class GodShip: public OffensiveHorse {
    Q_OBJECT

public:
    Q_INVOKABLE GodShip(Card::Suit suit, int number);
    void onUninstall(ServerPlayer *player) const;
};

class GodHat: public Treasure {
    Q_OBJECT

public:
    Q_INVOKABLE GodHat(Card::Suit suit, int number);
};

class GodEdict: public Treasure {
    Q_OBJECT

public:
    Q_INVOKABLE GodEdict(Card::Suit suit, int number);
};

class GodHeaddress: public Treasure {
    Q_OBJECT

public:
    Q_INVOKABLE GodHeaddress(Card::Suit suit, int number);
};

class GodSlash : public NatureSlash {
    Q_OBJECT

public:
    Q_INVOKABLE GodSlash(Card::Suit suit, int number);
};

class GodNihilo: public SingleTargetTrick {
    Q_OBJECT

public:
    Q_INVOKABLE GodNihilo(Card::Suit suit, int number);
    bool isAvailable(const Player *player) const;
    virtual bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual void onUse(Room *room, CardUseStruct &card_use) const;
    virtual void onEffect(CardEffectStruct &effect) const;
};

class GodFlower: public SingleTargetTrick {
    Q_OBJECT

public:
    Q_INVOKABLE GodFlower(Card::Suit suit, int number);
    bool isAvailable(const Player *player) const;
    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual void onEffect(CardEffectStruct &effect) const;
};

class GodSpeel: public SingleTargetTrick {
    Q_OBJECT

public:
    Q_INVOKABLE GodSpeel(Card::Suit suit, int number);
    bool isAvailable(const Player *player) const;
    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual void onEffect(CardEffectStruct &effect) const;
};

#endif