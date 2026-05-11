#ifndef _WIND_H
#define _WIND_H

//#include "package.h"
//#include "card.h"
#include "skill.h"

class HuangtianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HuangtianCard();

    void onUse(Room *room, CardUseStruct &card_use) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
};

class ShensuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ShensuCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class TianxiangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TianxiangCard();

    void onEffect(CardEffectStruct &effect) const;
};

class GuhuoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE GuhuoCard();
    bool guhuo(ServerPlayer *yuji) const;

    bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;

    const Card *validate(CardUseStruct &card_use) const;
    const Card *validateInResponse(ServerPlayer *user) const;
};

class GuhuoDialog : public QDialog
{
    Q_OBJECT

public:
    static GuhuoDialog *getInstance(const QString &object, bool left = true, bool right = true,
        bool play_only = true, bool slash_combined = false, bool delayed_tricks = false, bool update = false);
    
    QList<Card *> getAvailableCards() const;
    bool isButtonEnabled(const QString &button_name) const;
    QString getSkillName() const { return objectName(); }

public slots:
    void popup();
    void selectCard(QAbstractButton *button);

protected:
    explicit GuhuoDialog(const QString &object, bool left = true, bool right = true,
        bool play_only = true, bool slash_combined = false, bool delayed_tricks = false);
    QAbstractButton *createButton(Card *card);

    QHash<QString, const Card *> map;

private:
    QList<Card *> _getBasicCards() const;
    QList<Card *> _getTrickCards() const;
    QGroupBox *createLeft();
    QGroupBox *createRight();
    QButtonGroup *group;

	bool play_only;
    bool slash_combined;
    bool delayed_tricks;

signals:
    void onButtonClick();
};

class Jushou : public PhaseChangeSkill
{
public:
    Jushou();
    bool onPhaseChange(ServerPlayer *target, Room *room) const;

protected:
    virtual int getJushouDrawNum(ServerPlayer *caoren) const;
};

class NosGuhuoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE NosGuhuoCard();
    bool nosguhuo(ServerPlayer *yuji) const;

    bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;

    const Card *validate(CardUseStruct &card_use) const;
    const Card *validateInResponse(ServerPlayer *user) const;
};

class GongxinCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE GongxinCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class WindPackage : public Package
{
    Q_OBJECT

public:
    WindPackage();
};
#endif