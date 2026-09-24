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

#ifndef _H_LORD_EX_H
#define _H_LORD_EX_H

#include "package.h"
#include "card.h"
#include "wrapped-card.h"
#include "skill.h"
#include "standard.h"

class HPaiyiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HPaiyiCard();

    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual void onEffect(CardEffectStruct &effect) const;
};

class HQuanjinCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HQuanjinCard();

    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual void extraCost(Room *room, const CardUseStruct &card_use) const;
    virtual void onEffect(CardEffectStruct &effect) const;
};

class HZaoyunCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HZaoyunCard();

    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual void onEffect(CardEffectStruct &effect) const;
};

class HDiaoguiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HDiaoguiCard();

    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual void onUse(Room *room, CardUseStruct &card_use) const;

};

class HAocaiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HAocaiCard();

    virtual bool targetFixed() const;
    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;

    virtual const Card *validateInResponse(ServerPlayer *user) const;
    virtual const Card *validate(CardUseStruct &cardUse) const;
};

class HDuwuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HDuwuCard();
    virtual void onUse(Room *room, CardUseStruct &card_use) const;
    virtual void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class HJinfaCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HJinfaCard();

    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual void onEffect(CardEffectStruct &effect) const;
};

class HHuaiyiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HHuaiyiCard();
    virtual void extraCost(Room *room, const CardUseStruct &card_use) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class HQingyinCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HQingyinCard();
    virtual void onUse(Room *room, CardUseStruct &card_use) const;
    virtual void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class HTonglingCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HTonglingCard();

    virtual bool targetFixed() const;
    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    virtual void onUse(Room *room, CardUseStruct &card_use) const;
};

class HJianyanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HJianyanCard();

    virtual void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class HJujianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HJujianCard();

    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual void onEffect(CardEffectStruct &effect) const;
};

class HLordEXPackage : public Package
{
    Q_OBJECT

public:
    HLordEXPackage();
};



class HImperialEdict : public Treasure
{
    Q_OBJECT

public:
    Q_INVOKABLE HImperialEdict(Card::Suit suit, int number);

    virtual void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class HImperialEdictAttachCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HImperialEdictAttachCard();

    virtual void onUse(Room *room, CardUseStruct &card_use) const;
    virtual void onEffect(CardEffectStruct &effect) const;
};

class HImperialEdictTrickCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HImperialEdictTrickCard();

    virtual void onUse(Room *room, CardUseStruct &card_use) const;
    virtual void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class HRuleTheWorld : public SingleTargetTrick
{
    Q_OBJECT

public:
    Q_INVOKABLE HRuleTheWorld(Card::Suit suit = Spade, int number = 12);

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override;
    void onUse(Room *room, CardUseStruct &use) const override;
    virtual void onEffect(CardEffectStruct &effect) const;
};

class HConquering : public GlobalEffect
{
    Q_OBJECT

public:
    Q_INVOKABLE HConquering(Card::Suit suit = Diamond, int number = 1);

    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual void onUse(Room *room, CardUseStruct &card_use) const;
    virtual void onEffect(CardEffectStruct &effect) const;
};

class HConsolidateCountryGiveCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HConsolidateCountryGiveCard();

    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual void onUse(Room *room, CardUseStruct &card_use) const;
};

class HConsolidateCountry : public SingleTargetTrick
{
    Q_OBJECT

public:
    Q_INVOKABLE HConsolidateCountry(Card::Suit suit = Heart, int number = 1);

    virtual void onUse(Room *room, CardUseStruct &card_use) const;
    virtual void onEffect(CardEffectStruct &effect) const;
    virtual bool isAvailable(const Player *player) const;
};

class HChaos : public GlobalEffect
{
    Q_OBJECT

public:
    Q_INVOKABLE HChaos(Card::Suit suit = Club, int number = 12);

    void onUse(Room *room, CardUseStruct &use) const override;
    virtual void onEffect(CardEffectStruct &effect) const;
};

class HLordEXCardPackage : public Package
{
    Q_OBJECT

public:
    HLordEXCardPackage();
};

#endif // _H_LORD_EX_H

