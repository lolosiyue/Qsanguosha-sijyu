#ifndef DREAM_H
#define DREAM_H

#include "ol.h"

class DreamPackage : public Package
{
    Q_OBJECT

public:
    DreamPackage();
};

class IfAnjieCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IfAnjieCard();
    bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    const Card *validateInResponse(ServerPlayer *user) const;
    const Card *validate(CardUseStruct &cardUse) const;
};

class IfSixiangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IfSixiangCard();
    bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    const Card *validateInResponse(ServerPlayer *user) const;
    const Card *validate(CardUseStruct &cardUse) const;
};

class IfJizhiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IfJizhiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    const Card *validateInResponse(ServerPlayer *user) const;
    const Card *validate(CardUseStruct &cardUse) const;
};

class IfEjiangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IfEjiangCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    const Card *validate(CardUseStruct &cardUse) const;
};

#endif
