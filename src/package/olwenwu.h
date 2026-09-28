#ifndef OLWENWU_H
#define OLWENWU_H

//#include "package.h"
//#include "card.h"
//#include "skill.h"
#if !defined(QSAN_ENGINE_BUILD)
#include "../ui/special-skill-dialogs.h"
#endif
#include "wind.h"

class LiPackage : public Package
{
    Q_OBJECT

public:
    LiPackage();
};

class JinYingshiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JinYingshiCard();
};

class JinXiongzhiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JinXiongzhiCard();
};

class JinQinglengCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JinQinglengCard();
};

class ChexuanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ChexuanCard();
};

class CaozhaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE CaozhaoCard();
};


class BeiPackage : public Package
{
    Q_OBJECT

public:
    BeiPackage();
};

class JinYishiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JinYishiCard();
};

class JinShiduCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JinShiduCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
};

class JinRuilveGiveCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JinRuilveGiveCard();
};


class GuoPackage : public Package
{
    Q_OBJECT

public:
    GuoPackage();
};

class JinChoufaCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JinChoufaCard();
};

class JinYanxiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JinYanxiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
};

class JinSanchenCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JinSanchenCard();
};


class JiePackage : public Package
{
    Q_OBJECT

public:
    JiePackage();
};

class JinBolanSkillCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JinBolanSkillCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
};

class JinBingxinCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JinBingxinCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetFixed() const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
};

class TousuiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TousuiCard();
};

class YuePackage : public Package
{
    Q_OBJECT

public:
    YuePackage();
};

class JinXuanbeiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JinXuanbeiCard();
};

class JinXianwanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JinXianwanCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
};









#endif
