#ifndef TOUHOUYUKI_H
#define TOUHOUYUKI_H

#include "card.h"
#include "package.h"

class TouhouYukiPackage : public Package
{
    Q_OBJECT

public:
    TouhouYukiPackage();
};

// Card-string shells for SmartAI; the server rebuilds them through the V2 skills.

class ThHuanfaCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThHuanfaCard();
};

class ThYuanqiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThYuanqiCard();
};

class ThBingpuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThBingpuCard();
};

class ThDongmoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThDongmoCard();
};

class ThKujieCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThKujieCard();
};

class ThChuanshangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThChuanshangCard();
};

class ThLingdieCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThLingdieCard();
};

class ThFuyueCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThFuyueCard();
};

#endif
