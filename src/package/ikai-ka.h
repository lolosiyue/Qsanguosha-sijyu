#ifndef IKAIKA_H
#define IKAIKA_H

#include "card.h"
#include "package.h"

class IkaiKaPackage : public Package
{
    Q_OBJECT

public:
    IkaiKaPackage();
};

// Card-string shells for SmartAI; the server rebuilds them through the V2 skills.

class IkZhijuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkZhijuCard();
};

class IkJilunCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkJilunCard();
};

class IkHunkaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkHunkaoCard();
};

class IkHuangshiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkHuangshiCard();
};

class IkDongzhaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkDongzhaoCard();
};

class IkJimuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkJimuCard();
};

class IkDengpoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkDengpoCard();
};

class IkLingchaCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkLingchaCard();
};

class IkDuanniCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkDuanniCard();
};

class IkLinghuiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkLinghuiCard();
};

class IkMingwangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkMingwangCard();
};

class IkXiaowuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkXiaowuCard();
};

class IkLihunCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkLihunCard();
};

class IkManwuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkManwuCard();
};

class IkSuyiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkSuyiCard();
};

class IkQiansheCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkQiansheCard();
};

class IkDaoleiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkDaoleiCard();
};

#endif
