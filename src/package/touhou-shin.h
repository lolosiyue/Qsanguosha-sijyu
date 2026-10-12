#ifndef TOUHOUSHIN_H
#define TOUHOUSHIN_H

#include "card.h"
#include "package.h"

class TouhouShinPackage : public Package
{
    Q_OBJECT

public:
    TouhouShinPackage();
};

// Card-string shells for SmartAI; the server rebuilds them through the V2 skills.

class ThLuanshenCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThLuanshenCard();
};

class ThLianyingCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThLianyingCard();
};

class ThMumiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThMumiCard();
};

class ThHuanjianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThHuanjianCard();
};

class ThShenmiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThShenmiCard();
};

class ThMuyuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThMuyuCard();
};

class ThNihuiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThNihuiCard();
};

class ThKuangwuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThKuangwuCard();
};

class ThDieyingCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThDieyingCard();
};

class ThBiyiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThBiyiCard();
};

class ThTunaCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThTunaCard();
};

class ThNingguCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThNingguCard();
};

class ThAiminCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThAiminCard();
};

class ThRenmoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThRenmoCard();
};

class ThRuizhiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThRuizhiCard();
};

class ThYuguangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThYuguangCard();
};

class ThCanfeiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThCanfeiCard();
};

class ThGuiyuniuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThGuiyuniuCard();
};

class ThHaixingCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThHaixingCard();
};

#endif
