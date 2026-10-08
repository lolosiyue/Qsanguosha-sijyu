#ifndef TOUHOUBANGAI_H
#define TOUHOUBANGAI_H

#include "card.h"
#include "package.h"

class TouhouBangaiPackage : public Package
{
    Q_OBJECT

public:
    TouhouBangaiPackage();
};

// Card-string shells for SmartAI; the server rebuilds them through the V2 skills.

class ThMiqiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThMiqiCard();
};

class ThXumeiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThXumeiCard();
};

class ThXingxieCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThXingxieCard();
};

class ThYuboCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThYuboCard();
};

class ThGuijuanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThGuijuanCard();
};

class ThWangdaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThWangdaoCard();
};

class ThKongxiangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThKongxiangCard();
};

#endif
