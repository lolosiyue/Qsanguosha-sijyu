#ifndef TOUHOUKAZE_H
#define TOUHOUKAZE_H

#include "card.h"
#include "package.h"

class TouhouKazePackage : public Package
{
    Q_OBJECT

public:
    TouhouKazePackage();
};

// Card-string shells for SmartAI ("@ThJiyiCard=."). The server rebuilds each
// submission through the V2 skill named by the shell, so they carry no rules.

class ThJiyiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThJiyiCard();
};

class ThNiankeCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThNiankeCard();
};

class ThEnanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThEnanCard();
};

class ThQiaogongCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThQiaogongCard();
};

class ThQianyiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThQianyiCard();
};

class ThKunyiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThKunyiCard();
};

class ThCannveCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThCannveCard();
};

class ThGelongCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThGelongCard();
};

class ThDasuiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThDasuiCard();
};

class ThSuilunCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThSuilunCard();
};

class ThRansangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThRansangCard();
};

class ThYanxingCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThYanxingCard();
};

class ThSangzhiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThSangzhiCard();
};

class ThXinhuaCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThXinhuaCard();
};

#endif
