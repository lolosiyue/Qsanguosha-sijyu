#ifndef IKAISUI_H
#define IKAISUI_H

#include "card.h"
#include "package.h"

class IkaiSuiPackage : public Package
{
    Q_OBJECT

public:
    IkaiSuiPackage();
};

// Card-string shells for SmartAI; the server rebuilds them through the V2 skills.

class IkXielunCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkXielunCard();
};

class IkMoqiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkMoqiCard();
};

class IkTianbeiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkTianbeiCard();
};

class IkDuanmengCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkDuanmengCard();
};

class IkQixinCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkQixinCard();
};

class IkXinbanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkXinbanCard();
};

class IkHuyinCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkHuyinCard();
};

class IkAoxueCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkAoxueCard();
};

class IkZhiyuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkZhiyuCard();
};

class IkFenxunCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkFenxunCard();
};

class IkCangwuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkCangwuCard();
};

class IkLingtongCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkLingtongCard();
};

class IkLunkeCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkLunkeCard();
};

class IkBinglingCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkBinglingCard();
};

class IkHuangpoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkHuangpoCard();
};

class IkCaiyinCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkCaiyinCard();
};

class IkHuzhanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkHuzhanCard();
};

class IkZhangeCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkZhangeCard();
};

class IkXincaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkXincaoCard();
};

class IkJiaojinCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkJiaojinCard();
};

class IkHuisuoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkHuisuoCard();
};

class IkCangliuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkCangliuCard();
};

class IkLianzhenCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE IkLianzhenCard();
};

#endif
