#ifndef CHALLENGEDEVELOPER_H
#define CHALLENGEDEVELOPER_H

#include "scenario.h"
//#include "card.h"

class ChallengeDeveloperScenario : public Scenario
{
    Q_OBJECT

public:
    ChallengeDeveloperScenario();
    void onTagSet(Room *, const QString &) const;
};

class DevLvedongCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE DevLvedongCard();

    bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;

    const Card *validate(CardUseStruct &card_use) const;
    const Card *validateInResponse(ServerPlayer *user) const;
};

class DevPofengCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE DevPofengCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class DevXiaohunCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE DevXiaohunCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class DevChengzhiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE DevChengzhiCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class DevBanchengCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE DevBanchengCard();
    void onEffect(CardEffectStruct &effect) const;
};

class DevNiniCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE DevNiniCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class DevGengxinCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE DevGengxinCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class DevMeigongCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE DevMeigongCard();
    void onEffect(CardEffectStruct &effect) const;
};


class DevTumouCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE DevTumouCard();

    bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;

    const Card *validate(CardUseStruct &cardUse) const;
};

#endif