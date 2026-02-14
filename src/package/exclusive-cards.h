#ifndef EXCLUSIVE_CARDS_H
#define EXCLUSIVE_CARDS_H

#include "yingbian.h"

class ExclusiveCardsPackage : public Package
{
    Q_OBJECT
public:
    ExclusiveCardsPackage();
};

class Hongduanqiang : public Weapon
{
    Q_OBJECT
public:
    Q_INVOKABLE Hongduanqiang(Card::Suit suit, int number);
};

class Liecuidao : public Weapon
{
    Q_OBJECT
public:
    Q_INVOKABLE Liecuidao(Card::Suit suit, int number);
};

class ShuibojianCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE ShuibojianCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class Shuibojian : public Weapon
{
    Q_OBJECT
public:
    Q_INVOKABLE Shuibojian(Card::Suit suit, int number);
    void onUninstall(ServerPlayer *player) const;
};

class Hunduwanbi : public Weapon
{
    Q_OBJECT
public:
    Q_INVOKABLE Hunduwanbi(Card::Suit suit, int number);
};

class Tianleiren : public Weapon
{
    Q_OBJECT
public:
    Q_INVOKABLE Tianleiren(Card::Suit suit, int number);
};

class Piliche : public Weapon
{
    Q_OBJECT
public:
    Q_INVOKABLE Piliche(Card::Suit suit, int number);
};

class SecondPiliche : public Weapon
{
    Q_OBJECT
public:
    Q_INVOKABLE SecondPiliche(Card::Suit suit, int number);
};


class Sichengliangyu : public Treasure
{
    Q_OBJECT
public:
    Q_INVOKABLE Sichengliangyu(Card::Suit suit, int number);
};

class Tiejixuanyu : public Treasure
{
    Q_OBJECT
public:
    Q_INVOKABLE Tiejixuanyu(Card::Suit suit, int number);
};

class Feilunzhanyu : public Treasure
{
    Q_OBJECT
public:
    Q_INVOKABLE Feilunzhanyu(Card::Suit suit, int number);
};

class Qiongshu : public Treasure
{
    Q_OBJECT
public:
    Q_INVOKABLE Qiongshu(Card::Suit suit, int number);
};

class Xishu : public Treasure
{
    Q_OBJECT
public:
    Q_INVOKABLE Xishu(Card::Suit suit, int number);
};

class Jinshu : public Treasure
{
    Q_OBJECT
public:
    Q_INVOKABLE Jinshu(Card::Suit suit, int number);
};

class TenyearPiliche : public Treasure
{
    Q_OBJECT
public:
    Q_INVOKABLE TenyearPiliche(Card::Suit suit, int number);
};

class ZhizheBasic : public BasicCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ZhizheBasic(Card::Suit suit, int number);
    virtual QString getSubtype() const;
    virtual bool isAvailable(const Player *) const;
};

class ZhizheTrick : public TrickCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ZhizheTrick(Card::Suit suit, int number);
    virtual QString getSubtype() const;
    virtual bool isAvailable(const Player *) const;
};

class ZhizheSuijiyingbian : public Suijiyingbian
{
    Q_OBJECT

public:
    Q_INVOKABLE ZhizheSuijiyingbian(Card::Suit suit, int number);
    virtual QString getSubtype() const;
};

class ZhizheWeapon : public Weapon
{
    Q_OBJECT

public:
    Q_INVOKABLE ZhizheWeapon(Card::Suit suit, int number);
    virtual QString getSubtype() const;
};

class ZhizheArmor : public Armor
{
    Q_OBJECT

public:
    Q_INVOKABLE ZhizheArmor(Card::Suit suit, int number);
    virtual QString getSubtype() const;
};

class ZhizheTreasure : public Treasure
{
    Q_OBJECT

public:
    Q_INVOKABLE ZhizheTreasure(Card::Suit suit, int number);
    virtual QString getSubtype() const;
};

class Meirenji :public SingleTargetTrick
{
    Q_OBJECT

public:
    Q_INVOKABLE Meirenji(Card::Suit suit, int number);

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class Xiaolicangdao :public SingleTargetTrick
{
    Q_OBJECT

public:
    Q_INVOKABLE Xiaolicangdao(Card::Suit suit, int number);

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class Bintieshuangji : public Weapon
{
    Q_OBJECT

public:
    Q_INVOKABLE Bintieshuangji(Card::Suit suit, int number);
};

class Sanlve : public Treasure
{
    Q_OBJECT
public:
    Q_INVOKABLE Sanlve(Card::Suit suit, int number);
};

class Zhaogujing : public Treasure
{
    Q_OBJECT
public:
    Q_INVOKABLE Zhaogujing(Card::Suit suit, int number);
};

class DagongcheJinji : public Weapon
{
    Q_OBJECT
public:
    Q_INVOKABLE DagongcheJinji(Card::Suit suit, int number);
    void onInstall(ServerPlayer *player) const;
    void onUninstall(ServerPlayer *player) const;
};

class DagongcheShouyu : public Weapon
{
    Q_OBJECT
public:
    Q_INVOKABLE DagongcheShouyu(Card::Suit suit, int number);
    void onInstall(ServerPlayer *player) const;
    void onUninstall(ServerPlayer *player) const;
};

class Zhuobang : public Weapon
{
    Q_OBJECT
public:
    Q_INVOKABLE Zhuobang(Card::Suit suit, int number);
    int getRange(const Player *player = nullptr) const;
};

class Youbi : public Weapon
{
    Q_OBJECT
public:
    Q_INVOKABLE Youbi(Card::Suit suit, int number);
    int getRange(const Player *player = nullptr) const;
};





#endif