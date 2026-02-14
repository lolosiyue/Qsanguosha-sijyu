#ifndef OL_STRENGTHEN_H
#define OL_STRENGTHEN_H

//#include "package.h"
//#include "card.h"
//#include "skill.h"
#include "tenyear-strengthen.h"
#include "standard-generals.h"
#include "mountain.h"

class OLStStandardPackage : public Package
{
    Q_OBJECT

public:
    OLStStandardPackage();
};

class OLStWindPackage : public Package
{
    Q_OBJECT

public:
    OLStWindPackage();
};

class OLStThicketPackage : public Package
{
    Q_OBJECT

public:
    OLStThicketPackage();
};

class OLStFirePackage : public Package
{
    Q_OBJECT

public:
    OLStFirePackage();
};

class OLStMountainPackage : public Package
{
    Q_OBJECT

public:
    OLStMountainPackage();
};

class OLStYJ2011Package : public Package
{
    Q_OBJECT

public:
    OLStYJ2011Package();
};

class OLStYJ2012Package : public Package
{
    Q_OBJECT

public:
    OLStYJ2012Package();
};

class OLStYJ2013Package : public Package
{
    Q_OBJECT

public:
    OLStYJ2013Package();
};

class OLStYJ2014Package : public Package
{
    Q_OBJECT

public:
    OLStYJ2014Package();
};

class OLStYC2016Package : public Package
{
    Q_OBJECT

public:
    OLStYC2016Package();
};





class OLJijiangCard : public JijiangCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLJijiangCard();
};

class OLHuangtianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLHuangtianCard();
    void onUse(Room *room, CardUseStruct &card_use) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
};

class OLGuhuoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLGuhuoCard();
    bool olguhuo(ServerPlayer *yuji) const;

    bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;

    const Card *validate(CardUseStruct &card_use) const;
    const Card *validateInResponse(ServerPlayer *user) const;
};

class OLQimouCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLQimouCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class OLTianxiangCard : public TenyearTianxiangCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLTianxiangCard();
};

class SecondOLHanzhanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE SecondOLHanzhanCard();
    void onUse(Room *, CardUseStruct &) const;
};

class OLWulieCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLWulieCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
    void onEffect(CardEffectStruct &effect) const;
};

class OLFangquanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLFangquanCard();
    void onEffect(CardEffectStruct &effect) const;
};

class OLZhibaCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLZhibaCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class OLZhibaPindianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLZhibaPindianCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class OLChangbiaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLChangbiaoCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class OLTiaoxinCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLTiaoxinCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class OLZaiqiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLZaiqiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class OLQiaobianCard : public QiaobianCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLQiaobianCard();
};

class OLQiangxiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLQiangxiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class OLJianmieCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLJianmieCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const;
    void onEffect(CardEffectStruct &effect) const;
};

class OLMiejiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLMiejiCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class OLChunlaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLChunlaoCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class OLGanluCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLGanluCard();
    void swapEquip(ServerPlayer *first, ServerPlayer *second) const;

    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class OLXianzhouCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLXianzhouCard();
    void swapEquip(ServerPlayer *first, ServerPlayer *second) const;

    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class OLZhijianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLZhijianCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class OLXuanhuoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLXuanhuoCard();
    void onEffect(CardEffectStruct &effect) const;
};

class OLZongxuanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLZongxuanCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class OLQingjianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLQingjianCard();
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class OLJiaozhaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLJiaozhaoCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};













#endif