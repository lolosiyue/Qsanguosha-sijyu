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
};

class OLWulieCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLWulieCard();
};

class OLFangquanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLFangquanCard();
};

class OLZhibaCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLZhibaCard();
};

class OLZhibaPindianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLZhibaPindianCard();
};

class OLChangbiaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLChangbiaoCard();
};

class OLTiaoxinCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLTiaoxinCard();
};

class OLZaiqiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLZaiqiCard();
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
};

class OLJianmieCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLJianmieCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const;
};

class OLMiejiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLMiejiCard();

};

class OLChunlaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLChunlaoCard();
};

class OLGanluCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLGanluCard();

    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
};

class OLZhijianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLZhijianCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
};

class OLXuanhuoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLXuanhuoCard();
};

class OLZongxuanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLZongxuanCard();
};

class OLQingjianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLQingjianCard();
};

class OLJiaozhaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLJiaozhaoCard();
};













#endif
