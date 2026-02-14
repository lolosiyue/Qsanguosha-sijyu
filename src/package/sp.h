#ifndef _SP_H
#define _SP_H

//#include "package.h"
//#include "card.h"
#include "standard.h"
#include "wind.h"


class SPPackage : public Package
{
    Q_OBJECT

public:
    SPPackage();
};

class MiscellaneousPackage : public Package
{
    Q_OBJECT

public:
    MiscellaneousPackage();
};

class SPCardPackage : public Package
{
    Q_OBJECT

public:
    SPCardPackage();
};

class HegemonySPPackage : public Package
{
    Q_OBJECT

public:
    HegemonySPPackage();
};

class SPMoonSpear : public Weapon
{
    Q_OBJECT

public:
    Q_INVOKABLE SPMoonSpear(Card::Suit suit = Diamond, int number = 12);
};

class MoonSpear : public Weapon
{
    Q_OBJECT

public:
    Q_INVOKABLE MoonSpear(Card::Suit suit = Diamond, int number = 12);
};

class Yongsi : public TriggerSkill
{
public:
    Yongsi();
    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *yuanshu, QVariant &data) const;

protected:
    virtual int getKingdoms(ServerPlayer *yuanshu) const;
};

class WeidiDialog : public QDialog
{
    Q_OBJECT

public:
    static WeidiDialog *getInstance();

public slots:
    void popup();
    void selectSkill(QAbstractButton *button);

private:
    explicit WeidiDialog();

    QAbstractButton *createSkillButton(const QString &skill_name);
    QButtonGroup *group;
    QVBoxLayout *button_layout;

signals:
    void onButtonClick();
};






class XiemuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE XiemuCard();

    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};


class XintanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE XintanCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const;
    void onEffect(CardEffectStruct &effect) const;
};

class MeibuFilter : public FilterSkill
{
public:
    MeibuFilter(const QString &skill_name);

    bool viewFilter(const Card *to_select) const;

    const Card *viewAs(const Card *originalCard) const;

private:
    QString n;
};

class LianzhuCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE LianzhuCard(QString lianzhu = "lianzhu");
    void onEffect(CardEffectStruct &effect) const;
private:
    QString lianzhu;
};

class TenyearLianzhuCard : public LianzhuCard
{
    Q_OBJECT
public:
    Q_INVOKABLE TenyearLianzhuCard();
};


class FanghunCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE FanghunCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    const Card *validate(CardUseStruct &card_use) const;
    const Card *validateInResponse(ServerPlayer *player) const;
};

class OLFanghunCard : public FanghunCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLFanghunCard();
};

class MobileFanghunCard : public FanghunCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileFanghunCard();
};

class TenyearFanghunCard : public FanghunCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearFanghunCard();
};

class ZhanyiViewAsBasicCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ZhanyiViewAsBasicCard();

    bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;

    const Card *validate(CardUseStruct &cardUse) const;
    const Card *validateInResponse(ServerPlayer *user) const;
};

class ZhanyiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ZhanyiCard();

    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class ShuliangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ShuliangCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;

};





#endif