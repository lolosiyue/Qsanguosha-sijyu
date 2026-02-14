#ifndef TENYEAR_STRENGTHEN_H
#define TENYEAR_STRENGTHEN_H

#include "package.h"
#include "card.h"
//#include "skill.h"

class TenyearStStandardPackage : public Package
{
    Q_OBJECT

public:
    TenyearStStandardPackage();
};

class TenyearStYJ2011Package : public Package
{
    Q_OBJECT

public:
    TenyearStYJ2011Package();
};

class TenyearStYJ2012Package : public Package
{
    Q_OBJECT

public:
    TenyearStYJ2012Package();
};

class TenyearStYJ2013Package : public Package
{
    Q_OBJECT

public:
    TenyearStYJ2013Package();
};

class TenyearStYJ2014Package : public Package
{
    Q_OBJECT

public:
    TenyearStYJ2014Package();
};

class TenyearStYJ2015Package : public Package
{
    Q_OBJECT

public:
    TenyearStYJ2015Package();
};

class TenyearStWindPackage : public Package
{
    Q_OBJECT

public:
    TenyearStWindPackage();
};




class TenyearZhihengCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearZhihengCard();
    void onUse(Room *room, CardUseStruct &card_use) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class TenyearJieyinCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearJieyinCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const;
    void onEffect(CardEffectStruct &effect) const;
};

class TenyearRendeCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearRendeCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class TenyearYijueCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearYijueCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class TenyearQingjianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearQingjianCard();
    void onEffect(CardEffectStruct &effect) const;
};

class TenyearTuxiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearTuxiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class TenyearQingnangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearQingnangCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const;
    void onEffect(CardEffectStruct &effect) const;
};

class TenyearQimouCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearQimouCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class TenyearShensuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearShensuCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class TenyearTianxiangCard :public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearTianxiangCard(QString this_skill_name = "tenyeartianxiang");
    void onEffect(CardEffectStruct &effect) const;
private:
    QString this_skill_name;
};

class TenyearSanyaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearSanyaoCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *) const;
    void onEffect(CardEffectStruct &effect) const;
};

class TenyearChunlaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearChunlaoCard(QString tenyearchunlao = "tenyearchunlao");
    void use(Room *, ServerPlayer *source, QList<ServerPlayer *> &) const;
private:
    QString tenyearchunlao;
};

class TenyearChunlaoWineCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearChunlaoWineCard(QString tenyearchunlao = "tenyearchunlao");
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
private:
    QString tenyearchunlao;
};

class SecondTenyearChunlaoCard : public TenyearChunlaoCard
{
    Q_OBJECT

public:
    Q_INVOKABLE SecondTenyearChunlaoCard();
};

class SecondTenyearChunlaoWineCard : public TenyearChunlaoWineCard
{
    Q_OBJECT

public:
    Q_INVOKABLE SecondTenyearChunlaoWineCard();
};

class TenyearJiangchiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearJiangchiCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class TenyearWurongCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearWurongCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class TenyearYanzhuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearYanzhuCard();
    void onEffect(CardEffectStruct &effect) const;
};

class TenyearXingxueCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearXingxueCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *, QList<ServerPlayer *> &targets) const;
};

class TenyearShenduanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearShenduanCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class TenyearQiaoshuiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearQiaoshuiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class TenyearQiaoshuiTargetCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearQiaoshuiTargetCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class TenyearXianzhenCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearXianzhenCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class SecondTenyearXianzhenCard : public TenyearXianzhenCard
{
    Q_OBJECT

public:
    Q_INVOKABLE SecondTenyearXianzhenCard();
};

class TenyearZishouCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearZishouCard();
    void use(Room *, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class TenyearyongjinCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearyongjinCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class TenyearXuanhuoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearXuanhuoCard();
    void onEffect(CardEffectStruct &effect) const;
};

class TenyearSidiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearSidiCard();
    void use(Room *room, ServerPlayer *, QList<ServerPlayer *> &) const;
};

class TenyearHuaiyiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearHuaiyiCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class TenyearHuaiyiSnatchCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearHuaiyiSnatchCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class TenyearGongqiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearGongqiCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class TenyearJiefanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearJiefanCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
    void onEffect(CardEffectStruct &effect) const;
};

class TenyearXianzhouDamageCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearXianzhouDamageCard();
    void onUse(Room *room, CardUseStruct &card_use) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
};

class TenyearXianzhouCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearXianzhouCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class TenyearShenxingCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearShenxingCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class TenyearBingyiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearBingyiCard();

    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

#include "standard.h"

class TenyearZongxuanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearZongxuanCard();
    void use(Room *room, ServerPlayer *, QList<ServerPlayer *> &targets) const;
};

class TenyearYjYanyuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearYjYanyuCard();
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class TenyearJiaozhaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearJiaozhaoCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class TenyearGanluCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearGanluCard();
    void swapEquip(ServerPlayer *first, ServerPlayer *second) const;

    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class TenyearJianyanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearJianyanCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class TenyearAnxuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearAnxuCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class TenyearQinwangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearQinwangCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class TenyearXiansiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearXiansiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class TenyearXiansiSlashCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearXiansiSlashCard();

    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    const Card *validate(CardUseStruct &cardUse) const;
};

class TenyearFenchengCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearFenchengCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
    void onEffect(CardEffectStruct &effect) const;
};

class TenyearMiejiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearMiejiCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class TenyearMingceCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearMingceCard();
    void onEffect(CardEffectStruct &effect) const;
};

#endif