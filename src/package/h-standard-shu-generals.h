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

#ifndef _H_STANDARD_SHU_H
#define _H_STANDARD_SHU_H

#include "h-standard-package.h"
#include "skill.h"

// Guanxing and Yizhi share observation rules while retaining their own sources.
class HGuanxing : public TriggerSkillV2 {
public:
    explicit HGuanxing(const QString &name = "heg_guanxing");
    bool canPreshow() const override;
    void record(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override;
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override;
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override;
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override;
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override;
};

#endif
