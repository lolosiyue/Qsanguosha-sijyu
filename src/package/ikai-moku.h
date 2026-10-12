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

class IkTiaoxinCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkTiaoxinCard();
};

class IkYihuoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkYihuoCard();
};

class IkYuanjieCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkYuanjieCard();
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
