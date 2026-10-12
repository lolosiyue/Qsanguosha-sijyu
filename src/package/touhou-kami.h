#ifndef TOUHOUKAMI_H
#define TOUHOUKAMI_H

#include "card.h"
#include "package.h"
#include "standard.h"

class TouhouKamiPackage : public Package
{
    Q_OBJECT

public:
    TouhouKamiPackage();
};

// Card-string shells for SmartAI; the server rebuilds them through the V2 skills.

class ThShenfengCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThShenfengCard();
};

class ThGugaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThGugaoCard();
};

class ThLeshiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThLeshiCard();
};

class ThJingwuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThJingwuCard();
};

class ThYouyaCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThYouyaCard();
};

class ThJinluCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThJinluCard();
};

class ThChuangxinCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThChuangxinCard();
};

class ThTianxinCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThTianxinCard();
};

class ThBaihunCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThBaihunCard();
};

class ThSiqiangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThSiqiangCard();
};

class ThJiefuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThJiefuCard();
};

class ThShuangfengCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThShuangfengCard();
};

class ThJieshaCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThJieshaCard();
};

class ThBingzhangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThBingzhangCard();
};

// 空观剑. Only 冥刃 makes it; no deck holds it.
class Kuukanken : public Weapon
{
    Q_OBJECT
    Q_CLASSINFO("CardName", "kuukanken")

public:
    Q_INVOKABLE Kuukanken(Card::Suit suit, int number);
};

#endif
