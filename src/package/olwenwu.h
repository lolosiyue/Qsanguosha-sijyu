#ifndef OLWENWU_H
#define OLWENWU_H

//#include "package.h"
//#include "card.h"
//#include "skill.h"
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
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class JinXiongzhiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JinXiongzhiCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class JinQinglengCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JinQinglengCard();
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class ChexuanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ChexuanCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class CaozhaoDialog : public GuhuoDialog
{
    Q_OBJECT

public:
    static CaozhaoDialog *getInstance(const QString &object);

protected:
    explicit CaozhaoDialog(const QString &object);
    bool isButtonEnabled(const QString &button_name) const;
};

class CaozhaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE CaozhaoCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
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
    void onUse(Room *, CardUseStruct &) const;
};

class JinShiduCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JinShiduCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class JinRuilveGiveCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JinRuilveGiveCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
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
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class JinYanxiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JinYanxiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class JinSanchenCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JinSanchenCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const;
    void onEffect(CardEffectStruct &effect) const;
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
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class JinBingxinCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JinBingxinCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetFixed() const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    const Card *validate(CardUseStruct &use) const;
    const Card *validateInResponse(ServerPlayer *player) const;
};

class TousuiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TousuiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    const Card *validate(CardUseStruct &use) const;
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
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class JinXianwanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JinXianwanCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    const Card *validate(CardUseStruct &cardUse) const;
    const Card *validateInResponse(ServerPlayer *user) const;
};









#endif