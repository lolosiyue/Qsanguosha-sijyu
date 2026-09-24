/********************************************************************
    Copyright (c) 2013-2014 - QSanguosha-Rara

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

    QSanguosha-Rara
    *********************************************************************/

#ifndef _H_STRATEGIC_ADVANTAGE_PACKAGE_H
#define _H_STRATEGIC_ADVANTAGE_PACKAGE_H

#include "package.h"
#include "standard.h"
#include "maneuvering.h"
#include "standard-cards.h"
#include "skill.h"
#include "h-standard-equips.h"

class HBlade : public Blade{
    Q_OBJECT

public:
    Q_INVOKABLE HBlade(Card::Suit suit, int number);
};

class HHalberd : public Halberd{
    Q_OBJECT

public:
    Q_INVOKABLE HHalberd(Card::Suit suit, int number);
};

class HHalberdCard: public SkillCard {
    Q_OBJECT

public:
    Q_INVOKABLE HHalberdCard();

    virtual const Card *validate(CardUseStruct &card_use) const;
    virtual const Card *validateInResponse(ServerPlayer *user) const;
    virtual void onUse(Room *room, CardUseStruct &card_use) const;
};

class HBreastplate : public Armor{
    Q_OBJECT

public:
    Q_INVOKABLE HBreastplate(Card::Suit suit = Card::Club, int number = 2);
};

class HIronArmor : public Armor{
    Q_OBJECT

public:
    Q_INVOKABLE HIronArmor(Card::Suit suit = Card::Spade, int number = 2);
};

class HJadeSeal : public Treasure{
    Q_OBJECT

public:
    Q_INVOKABLE HJadeSeal(Card::Suit suit, int number);
};

class HDrowning: public SingleTargetTrick {
    Q_OBJECT

public:
    Q_INVOKABLE HDrowning(Card::Suit suit, int number);

    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual void onEffect(CardEffectStruct &effect) const;
    virtual bool isAvailable(const Player *player) const;

};

class HBurningCamps : public AOE{
    Q_OBJECT

public:
    Q_INVOKABLE HBurningCamps(Card::Suit suit, int number, bool is_transferable = false);

    virtual bool isAvailable(const Player *player) const;
    virtual void onUse(Room *room, CardUseStruct &card_use) const;
    virtual void onEffect(CardEffectStruct &effect) const;
};

class HLureTiger : public TrickCard {
    Q_OBJECT

public:
    Q_INVOKABLE HLureTiger(Card::Suit suit, int number, bool is_transferable = false);

    virtual QString getSubtype() const;

    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
    virtual void onEffect(CardEffectStruct &effect) const;

};

class HFightTogether : public GlobalEffect{
    Q_OBJECT

public:
    Q_INVOKABLE HFightTogether(Card::Suit suit, int number);

    virtual bool isAvailable(const Player *player) const;
    bool canRecastFor(const Player *player) const;

    virtual void onUse(Room *room, CardUseStruct &card_use) const;
    virtual void onEffect(CardEffectStruct &effect) const;
};

class HAllianceFeast : public AOE {
    Q_OBJECT

public:
    Q_INVOKABLE HAllianceFeast(Card::Suit suit = Heart, int number = 1);

    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual void onUse(Room *room, CardUseStruct &card_use) const;
    virtual void onEffect(CardEffectStruct &effect) const;
    virtual bool isAvailable(const Player *player) const;
};

class HThreatenEmperor: public SingleTargetTrick{
    Q_OBJECT

public:
    Q_INVOKABLE HThreatenEmperor(Card::Suit suit, int number);
    virtual void onUse(Room *room, CardUseStruct &card_use) const;
    virtual void onEffect(CardEffectStruct &effect) const;
    virtual bool isAvailable(const Player *player) const;
};

class HImperialOrder: public GlobalEffect{
    Q_OBJECT

public:
    Q_INVOKABLE HImperialOrder(Card::Suit suit, int number);

    virtual bool isAvailable(const Player *player) const;

    virtual void onUse(Room *room, CardUseStruct &card_use) const;
    virtual void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
    virtual void onEffect(CardEffectStruct &effect) const;
};

class HStrategicAdvantagePackage : public Package{
    Q_OBJECT

public:
    HStrategicAdvantagePackage();
    static void recordCardRules(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data);
};

#endif
