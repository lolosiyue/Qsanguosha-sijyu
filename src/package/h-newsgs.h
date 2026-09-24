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

#ifndef H_NEWSGS_H
#define H_NEWSGS_H

#include "package.h"
#include "card.h"
#include "wrapped-card.h"
#include "skill.h"
#include "standard.h"

class HBoyanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HBoyanCard();

    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual void onEffect(CardEffectStruct &effect) const;
};

class HBoyanZonghengCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HBoyanZonghengCard();

    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual void onEffect(CardEffectStruct &effect) const;
};

class HWeimengCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HWeimengCard();

    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual void onEffect(CardEffectStruct &effect) const;
};

class HWeimengZonghengCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HWeimengZonghengCard();

    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual void onEffect(CardEffectStruct &effect) const;
};

class HDaoshuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HDaoshuCard();

    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual void onEffect(CardEffectStruct &effect) const;
};

class HJingheCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HJingheCard();

    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    virtual void extraCost(Room *room, const CardUseStruct &card_use) const;
    virtual void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class HHuoqiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HHuoqiCard();

    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual void onEffect(CardEffectStruct &effect) const;
};

class HXianshouCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HXianshouCard();

    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual void onEffect(CardEffectStruct &effect) const;
};

class HFenglveCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HFenglveCard();

    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual void onEffect(CardEffectStruct &effect) const;
};

class HFenglveZonghengCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HFenglveZonghengCard();

    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual void onEffect(CardEffectStruct &effect) const;
};

class HZhuangrongCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HZhuangrongCard();

    virtual void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class HMingfaCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HMingfaCard();

    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual void onEffect(CardEffectStruct &effect) const;
};

class HMingfaZonghengCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HMingfaZonghengCard();

    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual void onEffect(CardEffectStruct &effect) const;
};

class HJianguoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HJianguoCard();

    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual void onEffect(CardEffectStruct &effect) const;
};

class HQuanjianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HQuanjianCard();

    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual void onEffect(CardEffectStruct &effect) const;
};




class HManoeuvrePackage : public Package
{
    Q_OBJECT

public:
    HManoeuvrePackage();
};


class HNewSGSPackage : public Package
{
    Q_OBJECT

public:
    HNewSGSPackage();
};


#endif // H_NEWSGS_H
