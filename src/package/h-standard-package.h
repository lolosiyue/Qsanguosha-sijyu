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

#ifndef _H_STANDARD_PACKAGE_H
#define _H_STANDARD_PACKAGE_H

#include "package.h"

class HStandardPackage : public Package {
    Q_OBJECT

public:
    HStandardPackage();
    void addWeiGenerals();
    void addShuGenerals();
    void addWuGenerals();
    void addQunGenerals();
};


class HStandardCardPackage : public Package {
    Q_OBJECT

public:
    HStandardCardPackage();

    QList<Card *> basicCards();
    QList<Card *> equipCards();
    void addEquipSkills();
    QList<Card *> trickCards();
};

#endif
