#ifndef TOUHOUHANA_H
#define TOUHOUHANA_H

#include "card.h"
#include "package.h"

class TouhouHanaPackage : public Package
{
    Q_OBJECT

public:
    TouhouHanaPackage();
};

// Card-string shells for SmartAI; the server rebuilds them through the V2 skills.

class ThJiewuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThJiewuCard();
};

class ThXihuaCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThXihuaCard();
};

class ThQuanshanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThQuanshanCard();
};

class ThDuanzuiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThDuanzuiCard();
};

class ThYachuiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThYachuiCard();
};

class ThGuaitanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThGuaitanCard();
};

class ThDujiaCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThDujiaCard();
};

class ThLeishiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThLeishiCard();
};

class ThLiuzhenCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThLiuzhenCard();
};

#endif
