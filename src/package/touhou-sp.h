#ifndef TOUHOUSP_H
#define TOUHOUSP_H

#include "card.h"
#include "package.h"

class TouhouSPPackage : public Package
{
    Q_OBJECT

public:
    TouhouSPPackage();
};

// Card-string shells for SmartAI; the server rebuilds them through the V2 skills.

class ThYuduCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThYuduCard();
};

class ThZhaoguoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThZhaoguoCard();
};

class ThLunminCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThLunminCard();
};

class ThFenglingCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThFenglingCard();
};

class ThYingshiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThYingshiCard();
};

class ThJingyuanspCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThJingyuanspCard();
};

class ThFeihuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThFeihuCard();
};

class ThGuanzhiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThGuanzhiCard();
};

class ThFuhuaCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThFuhuaCard();
};

class ThHuanyaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThHuanyaoCard();
};

#endif
