#ifndef IKAIKIN_H
#define IKAIKIN_H

#include "card.h"
#include "package.h"

class IkaiKinPackage : public Package
{
    Q_OBJECT

public:
    IkaiKinPackage();
};

// Card-string shells for SmartAI; the server rebuilds them through the V2 skills.

class IkSizhuoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkSizhuoCard();
};

class IkJunanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkJunanCard();
};

#endif
