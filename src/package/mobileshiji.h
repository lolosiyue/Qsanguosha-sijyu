#ifndef MOBILESHIJI_H
#define MOBILESHIJI_H

#include "package.h"
#include "card.h"
//#include "skill.h"

class MobileZhiPackage : public Package
{
    Q_OBJECT

public:
    MobileZhiPackage();
};

class MobileZhiQiaiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE MobileZhiQiaiCard();
    void onEffect(CardEffectStruct &effect) const;
};

class MobileZhiShamengCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE MobileZhiShamengCard();
    void onEffect(CardEffectStruct &effect) const;
};

class SecondMobileZhiZuiciCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE SecondMobileZhiZuiciCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class SecondMobileZhiZuiciMarkCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE SecondMobileZhiZuiciMarkCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *) const;
    void onUse(Room *, CardUseStruct &card_use) const;
};

class MobileZhiDuojiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE MobileZhiDuojiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class MobileZhiJianzhanCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE MobileZhiJianzhanCard();
    void onEffect(CardEffectStruct &effect) const;
};

class SecondMobileZhiDuojiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE SecondMobileZhiDuojiCard();
    void onEffect(CardEffectStruct &effect) const;
};

class SecondMobileZhiDuojiRemove : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE SecondMobileZhiDuojiRemove();
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class MobileZhiWanweiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE MobileZhiWanweiCard();
    void onEffect(CardEffectStruct &effect) const;
};

class MobileZhiJianyuCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE MobileZhiJianyuCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class MobileZhiMiewuCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE MobileZhiMiewuCard();
    bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    const Card *validate(CardUseStruct &card_use) const;
    const Card *validateInResponse(ServerPlayer *user) const;
};



class MobileXinPackage : public Package
{
    Q_OBJECT

public:
    MobileXinPackage();
};

class MobileXinYinjuCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE MobileXinYinjuCard();
    void onEffect(CardEffectStruct &effect) const;
};

class MobileXinCunsiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE MobileXinCunsiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const;
    void onEffect(CardEffectStruct &effect) const;
};

class MobileXinMouliCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE MobileXinMouliCard();
    void onEffect(CardEffectStruct &effect) const;
};

class MobileXinChuhaiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileXinChuhaiCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class MobileXinLirangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileXinLirangCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class MobileXinRongbeiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE MobileXinRongbeiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const;
    void onEffect(CardEffectStruct &effect) const;
};

class SecondMobileXinMouliCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE SecondMobileXinMouliCard();
    void onEffect(CardEffectStruct &effect) const;
};


class MobileRenPackage : public Package
{
    Q_OBJECT

public:
    MobileRenPackage();
};

class MobileRenRenshiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE MobileRenRenshiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class MobileRenBuqiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE MobileRenBuqiCard();
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class MobileRenBomingCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE MobileRenBomingCard();
    void onEffect(CardEffectStruct &effect) const;
};

class MobileRenMuzhenCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE MobileRenMuzhenCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};


class MobileYongPackage : public Package
{
    Q_OBJECT

public:
    MobileYongPackage();
};

class MobileYongJungongCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileYongJungongCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};


class MobileYanPackage : public Package
{
    Q_OBJECT

public:
    MobileYanPackage();
};

class MobileYanYajunCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileYanYajunCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class MobileYanYajunPutCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileYanYajunPutCard();
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class MobileYanZundiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileYanZundiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const;
    void onEffect(CardEffectStruct &effect) const;
};

class MobileYanYanjiaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileYanYanjiaoCard();
    void onEffect(CardEffectStruct &effect) const;
};

class MobileYanJincuiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileYanJincuiCard();
    void onEffect(CardEffectStruct &effect) const;
};

class MobileYanShangyiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileYanShangyiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_selet, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class ShenpeiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ShenpeiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_selet, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class XingzhenCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE XingzhenCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};










#endif