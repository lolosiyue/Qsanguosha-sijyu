#ifndef TOUHOUTSUKI_H
#define TOUHOUTSUKI_H

#include "card.h"
#include "package.h"

class TouhouTsukiPackage : public Package
{
    Q_OBJECT

public:
    TouhouTsukiPackage();
};

// Card-string shells for SmartAI; the server rebuilds them through the V2 skills.

class ThYejunCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThYejunCard();
};

class ThJinguoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThJinguoCard();
};

class ThHeiguanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThHeiguanCard();
};

class ThKanyaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThKanyaoCard();
};

class ThExiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThExiCard();
};

class ThGuixuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThGuixuCard();
};

class ThShenbaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThShenbaoCard();
};

#endif
