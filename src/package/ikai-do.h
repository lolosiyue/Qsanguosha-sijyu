#ifndef IKAIDO_H
#define IKAIDO_H

#include "card.h"
#include "package.h"
#include "standard.h"

class IkaiDoPackage : public Package
{
    Q_OBJECT

public:
    IkaiDoPackage();
};

// Card-string shells for SmartAI; the server rebuilds them through the V2 skills.

class IkXingyuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkXingyuCard();
};

class IkYuanheCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkYuanheCard();
};

// 紫莲圣咏, a delayed trick that helps its target. Only 本音 makes it; no deck holds it.
class PurpleSong : public DelayedTrick
{
    Q_OBJECT
    Q_CLASSINFO("CardName", "purple_song")

public:
    Q_INVOKABLE PurpleSong(Card::Suit suit, int number);
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void takeEffect(ServerPlayer *target) const;
};

#endif
