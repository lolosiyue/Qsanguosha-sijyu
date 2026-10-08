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

class IkXinchaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkXinchaoCard();
};

class IkSishiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkSishiCard();
};

class IkZangyuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkZangyuCard();
};

class IkShitieCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkShitieCard();
};

class IkBingyanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkBingyanCard();
};

class IkDingpinCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkDingpinCard();
};

class IkHuanzhouCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkHuanzhouCard();
};

class IkLvdongCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkLvdongCard();
};

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
