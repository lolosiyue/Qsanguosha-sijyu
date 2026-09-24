/********************************************************************
    Copyright (c) 2013-2015 - Mogara

    This file is part of QSanguosha-Hegemony.

    This game is free software; you can redistribute it and/or
    modify it under the terms of the MOL General Public License as
    published by the Free Software Foundation; either version 3.0
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

    See the LICENSE file for more details.

    Mogara
    *********************************************************************/

#ifndef H_MOL_H
#define H_MOL_H

#include "package.h"
#include "card.h"
#include "wrapped-card.h"
#include "skill.h"
#include "standard.h"





class HHongyuanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HHongyuanCard();
    virtual void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
    virtual void extraCost(Room *room, const CardUseStruct &card_use) const;
};

class HJiansuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HJiansuCard();

    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual void onEffect(CardEffectStruct &effect) const;
};

class HBiaozhaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HBiaozhaoCard();

    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    virtual void onUse(Room *room, CardUseStruct &card_use) const;
    virtual void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class HKanjiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HKanjiCard();
    virtual void extraCost(Room *room, const CardUseStruct &card_use) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};





class HMOLPackage : public Package
{
    Q_OBJECT

public:
    HMOLPackage();
};


class HOverseasPackage : public Package
{
    Q_OBJECT

public:
    HOverseasPackage();
};


#endif // H_MOL_H
