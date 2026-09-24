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
#ifndef _H_TRANSFORMATION_H
#define _H_TRANSFORMATION_H

#include "package.h"
#include "card.h"
#include "wrapped-card.h"
#include "skill.h"
#include "standard.h"

class HYongjinCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HYongjinCard();

    virtual void onUse(Room *room, CardUseStruct &card_use) const override;
    virtual void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const override;
};

class HQiceCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HQiceCard();

    virtual bool targetFixed() const override;
    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override;
    virtual bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const override;
    virtual void onUse(Room *room, CardUseStruct &card_use) const override;
};

class HYiguiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HYiguiCard();

    virtual bool targetFixed() const override;
    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override;
    virtual bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const override;
    virtual const Card *validate(CardUseStruct &card_use) const override;
    virtual const Card *validateInResponse(ServerPlayer *user) const override;
};

class HXiongsuanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HXiongsuanCard();

    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override;
    virtual void onUse(Room *room, CardUseStruct &card_use) const override;
    virtual void onEffect(CardEffectStruct &effect) const override;
};

class HSanyaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HSanyaoCard();

    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override;
    virtual void onEffect(CardEffectStruct &effect) const override;
};

class HLianziCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HLianziCard();

    virtual void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const override;
};

class HFlameMapCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HFlameMapCard();
    virtual void onUse(Room *room, CardUseStruct &card_use) const override;
};

class HLuminousPearl : public Treasure
{
    Q_OBJECT

public:
    Q_INVOKABLE HLuminousPearl(Card::Suit suit = Diamond, int number = 6);

    virtual void onUninstall(ServerPlayer *player) const override;
};

class HZhihengLPCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HZhihengLPCard();
    virtual void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const override;
};

class HHaoshiFlamemapCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HHaoshiFlamemapCard();

    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override;
    virtual void onUse(Room *room, CardUseStruct &use) const override;
    virtual void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const override;
};

// Shared soul-pool helpers retain exact private attached sources.
void HRefreshHuashenProjection(ServerPlayer *player);
void HDropHuashenGeneral(ServerPlayer *player, const QString &name);

class HTransformationPackage : public Package
{
    Q_OBJECT

public:
    HTransformationPackage();
};

class HTransformationEquipPackage : public Package
{
    Q_OBJECT

public:
    HTransformationEquipPackage();
};

#endif

