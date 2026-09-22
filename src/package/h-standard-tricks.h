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

#ifndef _H_STANDARD_TRICK_H
#define _H_STANDARD_TRICK_H

#include "standard.h"
#include "maneuvering.h"
#include "standard-cards.h"

class HegNullification : public Nullification {
    Q_OBJECT

public:
    Q_INVOKABLE HegNullification(Card::Suit suit, int number);
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const override;
};

class HAwaitExhausted : public TrickCard{
    Q_OBJECT

public:
    Q_INVOKABLE HAwaitExhausted(Card::Suit suit, int number);

    virtual QString getSubtype() const;
    virtual bool isAvailable(const Player *player) const;

    virtual void onUse(Room *room, CardUseStruct &card_use) const;
    virtual void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
    virtual void onEffect(CardEffectStruct &effect) const;
};

class HKnownBoth : public SingleTargetTrick{
    Q_OBJECT

public:
    Q_INVOKABLE HKnownBoth(Card::Suit suit, int number);
    virtual bool isAvailable(const Player *player) const;

    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    virtual void onUse(Room *room, CardUseStruct &card_use) const;
    virtual void onEffect(CardEffectStruct &effect) const;
};

class HBefriendAttacking : public SingleTargetTrick{
    Q_OBJECT

public:
    Q_INVOKABLE HBefriendAttacking(Card::Suit suit = Heart, int number = 9);

    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual void onEffect(CardEffectStruct &effect) const;
    virtual bool isAvailable(const Player *player) const;
};

#endif
