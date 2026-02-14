#ifndef MOBILE_STRENGTHEN_H
#define MOBILE_STRENGTHEN_H

//#include "package.h"
//#include "card.h"
//#include "skill.h"
#include "wind.h"

class MobileStStandardPackage : public Package
{
    Q_OBJECT

public:
    MobileStStandardPackage();
};

class MobileStWindPackage : public Package
{
    Q_OBJECT

public:
    MobileStWindPackage();
};

class MobileStThicketPackage : public Package
{
    Q_OBJECT

public:
    MobileStThicketPackage();
};

class MobileStFirePackage : public Package
{
    Q_OBJECT

public:
    MobileStFirePackage();
};

class MobileStMountainPackage : public Package
{
    Q_OBJECT

public:
    MobileStMountainPackage();
};

class MobileStYJ2011Package : public Package
{
    Q_OBJECT

public:
    MobileStYJ2011Package();
};

class MobileStYJ2012Package : public Package
{
    Q_OBJECT

public:
    MobileStYJ2012Package();
};

class MobileStYJ2013Package : public Package
{
    Q_OBJECT

public:
    MobileStYJ2013Package();
};

class MobileStYJ2014Package : public Package
{
    Q_OBJECT

public:
    MobileStYJ2014Package();
};

class MobileStYJ2015Package : public Package
{
    Q_OBJECT

public:
    MobileStYJ2015Package();
};


class MobileQingjianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileQingjianCard();
    void onEffect(CardEffectStruct &effect) const;
};

class MobileQiangxiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileQiangxiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class MobileNiepanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileNiepanCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class MobileZaiqiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileZaiqiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class MobilePoluCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobilePoluCard();
    bool targetFilter(const QList<const Player *> &, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class MobileTiaoxinCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileTiaoxinCard();
    void onEffect(CardEffectStruct &effect) const;
};

class MobileZhijianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileZhijianCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class MobileFangquanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileFangquanCard();
    void onEffect(CardEffectStruct &effect) const;
};

class MobileGanluCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileGanluCard();
    void swapEquip(ServerPlayer *first, ServerPlayer *second) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class MobileJieyueCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileJieyueCard();
    void onUse(Room *, CardUseStruct &) const;
};

class MobileAnxuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileAnxuCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class MobileGongqiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileGongqiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class MobileZongxuanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileZongxuanCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class MobileZongxuanPutCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileZongxuanPutCard();
    void use(Room *, ServerPlayer *, QList<ServerPlayer *> &) const;
};

class MobileJunxingCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileJunxingCard();
    void onEffect(CardEffectStruct &effect) const;
};

class MobileMiejiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileMiejiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class MobileMiejiDiscardCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileMiejiDiscardCard();
    void onUse(Room *, CardUseStruct &) const;
};

class MobileXianzhenCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileXianzhenCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class MobileQiaoshuiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileQiaoshuiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class MobileZongshihCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileZongshihCard();
    void onUse(Room *, CardUseStruct &) const;
};

class MobileDingpinCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileDingpinCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class MobileShenxingCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileShenxingCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class MobileBingyiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileBingyiCard();

    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class MobileJianyingDialog : public GuhuoDialog
{
    Q_OBJECT

public:
    static MobileJianyingDialog *getInstance(const QString &object);

protected:
    explicit MobileJianyingDialog(const QString &object);
    bool isButtonEnabled(const QString &button_name) const;
};

class MobileJianyingCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileJianyingCard();
    bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    const Card *validate(CardUseStruct &card_use) const;
};

class MobileYanzhuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileYanzhuCard();
    void onEffect(CardEffectStruct &effect) const;
};

class MobileXingxueCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileXingxueCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const;
    bool hasAliveTargets(ServerPlayer *player, QList<ServerPlayer *> &targets) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class MobileSidiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileSidiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class MobileYaomingCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileYaomingCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class MobileFurongCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileFurongCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};













#endif