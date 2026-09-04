#ifndef _INOVATION_H
#define _INOVATION_H

#include "card.h"
#include "package.h"
#include "standard.h"
#include "wind.h"

class MapoTofu : public BasicCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MapoTofu(Card::Suit suit, int number);

    QString getSubtype() const;
    static bool IsAvailable(const Player *player, const Card *analeptic = nullptr);
    bool isAvailable(const Player *player) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
    void onEffect(CardEffectStruct &effect) const;
};

class KeyTrick : public DelayedTrick
{
    Q_OBJECT

public:
    Q_INVOKABLE KeyTrick(Card::Suit suit, int number);

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void takeEffect(ServerPlayer *target) const;
    void onEffect(CardEffectStruct &effect) const;
    void onNullified(ServerPlayer *target) const;
};

class InovationZhurenCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE InovationZhurenCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class DiangongCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE DiangongCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class ZhilingCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ZhilingCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class YouerCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE YouerCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class JizhanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JizhanCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class TaxianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TaxianCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
};

class NingjuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE NingjuCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class JiguanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JiguanCard();

    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class PaojiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE PaojiCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class InovationFengzhuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE InovationFengzhuCard();

    bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    const Card *validate(CardUseStruct &cardUse) const;
    const Card *validateInResponse(ServerPlayer *user) const;
};

class InovationPackage : public Package
{
    Q_OBJECT

public:
    InovationPackage();
};

#endif
