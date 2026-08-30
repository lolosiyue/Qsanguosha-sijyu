#ifndef DREAM_H
#define DREAM_H

#include "ol.h"

class DreamPackage : public Package
{
    Q_OBJECT

public:
    DreamPackage();
};

class IfMishouCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IfMishouCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class IfDianbianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IfDianbianCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class IfPiyongCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IfPiyongCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class IfShijiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IfShijiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
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

class IfBaqiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IfBaqiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class IfEjiangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IfEjiangCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    const Card *validate(CardUseStruct &cardUse) const;
};

class IfJilveCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IfJilveCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};





#endif
