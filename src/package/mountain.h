#ifndef _MOUNTAIN_H
#define _MOUNTAIN_H

#include "generaloverview.h"
#include "skill.h"

class QiaobianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE QiaobianCard();

    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
    void onEffect(CardEffectStruct &effect) const;
};

class TiaoxinCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TiaoxinCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class ZhijianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ZhijianCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class GuzhengCard : public SkillCard
{
    Q_OBJECT
        
public:
    Q_INVOKABLE GuzhengCard();

    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class ZhibaCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ZhibaCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class FangquanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE FangquanCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class HuashenDialog : public GeneralOverview
{
    Q_OBJECT

public:
    HuashenDialog();

public slots:
    void popup();
};

class MountainPackage : public Package
{
    Q_OBJECT

public:
    MountainPackage();
};

class JilveCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JilveCard();

    void onUse(Room *room, CardUseStruct &card_use) const;
};

class Longhun : public ViewAsSkill
{
public:
    Longhun();
    bool isEnabledAtResponse(const Player *player, const QString &pattern) const;
    bool isEnabledAtPlay(const Player *player) const;
    bool viewFilter(const QList<const Card *> &selected, const Card *card) const;
    const Card *viewAs(const QList<const Card *> &cards) const;
    int getEffectIndex(const ServerPlayer *player, const Card *card) const;
    bool isEnabledAtNullification(const ServerPlayer *player) const;

protected:
    virtual int getEffHp(const Player *zhaoyun) const;
};

#endif