#ifndef IKAIMOKU_H
#define IKAIMOKU_H

#include "card.h"
#include "package.h"

class IkaiMokuPackage : public Package
{
    Q_OBJECT

public:
    IkaiMokuPackage();
};

// Card-string shells for SmartAI; the server rebuilds them through the V2 skills.

class IkHuanghunCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkHuanghunCard();
};

class IkSuinieCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkSuinieCard();
};

class IkQiangxiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkQiangxiCard();
};

class IkYihuoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkYihuoCard();
};

class IkJilveCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkJilveCard();
};

class IkYuanjieCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkYuanjieCard();
};

class IkBianshengCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkBianshengCard();
};

class IkXuzhaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkXuzhaoCard();
};

class IkYujiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkYujiCard();
};

#endif
