/********************************************************************
    Copyright (c) 2013-2015 - Mogara

    This file is part of QSanguosha-Hegemony.

    This game is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 3.0
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

    See the LICENSE file for more details.

    Mogara
    *********************************************************************/
#ifndef _H_POWER_H
#define _H_POWER_H

#include "package.h"
#include "card.h"
#include "wrapped-card.h"
#include "skill.h"
#include "standard.h"

class HZhengbiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HZhengbiCard();

    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override;
    virtual void extraCost(Room *room, const CardUseStruct &card_use) const override;
    virtual void onEffect(CardEffectStruct &effect) const override;
};

class HFengyingCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HFengyingCard();

    const Card *validate(CardUseStruct &card_use) const override;
};

class HJieyueCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HJieyueCard();

    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override;
    virtual void onUse(Room *room, CardUseStruct &card_use) const override;
};

class HJianglveCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HJianglveCard();
    virtual void onUse(Room *room, CardUseStruct &card_use) const override;
    virtual void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const override;
};

class HXuanhuoAttachCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HXuanhuoAttachCard();

    virtual void onUse(Room *room, CardUseStruct &card_use) const override;
    virtual void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const override;
};

class HGanluCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HGanluCard();

    virtual bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const override;
    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override;
    virtual void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const override;
};

class HWeidiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HWeidiCard();

    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override;
    virtual void onEffect(CardEffectStruct &effect) const override;
};

class HHuibianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HHuibianCard();

    virtual bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const override;
    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override;
    virtual void onUse(Room *room, CardUseStruct &card_use) const override;
    virtual void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const override;
};

class HSixDragons : public Horse
{
    Q_OBJECT
public:
    Q_INVOKABLE HSixDragons(Card::Suit suit = Heart, int number = 13, int correct = 0);
    QString getSubtype() const override { return "horse"; }
    Location location() const override { return HorseLocation; }
};

class HPowerPackage : public Package
{
    Q_OBJECT

public:
    HPowerPackage();
};

class HPowerEquipPackage : public Package
{
    Q_OBJECT

public:
    HPowerEquipPackage();
};

#endif

