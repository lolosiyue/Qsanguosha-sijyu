#ifndef IKAIDO_H
#define IKAIDO_H

#include "card.h"
#include "package.h"

class IkaiDoPackage : public Package
{
    Q_OBJECT

public:
    IkaiDoPackage();
};

// Card-string shells for SmartAI; the server rebuilds them through the V2 skills.

class IkXingyuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkXingyuCard();
};

class IkYuanheCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkYuanheCard();
};

#endif
