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

#ifndef _H_H_FORMATION_H
#define _H_H_FORMATION_H

#include "package.h"
#include "card.h"
#include "skill.h"
#include "standard.h"

// One V2 no-target summon proxy is shared by Formation, Momentum, and XXY.
// The activation context retains the exact skill instance that is revealed.
class HArraySummon : public ViewAsSkillV2
{
public:
    HArraySummon(const QString &name, const QString &type);

    bool canActivate(const ActiveSkillRequest &request) const override;
    TargetMode targetMode() const override;
    bool targetsFeasible(const ActiveSkillRequest &request,
                         const QList<const Player *> &targets) const override;
    EffectFlow effect(SkillContext &ctx) const override;

private:
    QString m_type;
};

class HFormationPackage : public Package {
    Q_OBJECT

public:
    HFormationPackage();
};

class HDragonPhoenix : public Weapon{
    Q_OBJECT

public:
    Q_INVOKABLE HDragonPhoenix(Card::Suit suit = Spade, int number = 2);
};

class HFormationEquipPackage : public Package{
    Q_OBJECT

public:
    HFormationEquipPackage();
};

#endif
