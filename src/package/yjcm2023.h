#ifndef YJCM2023_H
#define YJCM2023_H

#include "package.h"
#include "standard.h"

class YJCM2023Package : public Package
{
    Q_OBJECT

public:
    YJCM2023Package();
};

class GongqiaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE GongqiaoCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class FujueCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE FujueCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class BeiyuCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE BeiyuCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class ThQimeiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE ThQimeiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class Wangmeizhike :public SingleTargetTrick
{
    Q_OBJECT

public:
    Q_INVOKABLE Wangmeizhike(Card::Suit suit, int number);

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};










#endif