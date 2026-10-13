#ifndef GU_AUDIT2025_H
#define GU_AUDIT2025_H

#include "package.h"
#include "card.h"
#include "skill.h"

// GU-audit2025 follow-up: generals from Tom's 2025 audit list SOURCE.md / SOURCE-2.md.
void guAudit2025Tenyear(Package *pkg);
void guAudit2025Doudizhu(Package *pkg);
void guAudit2025OLSp(Package *pkg);
void guAudit2025Mobile(Package *pkg);

class GuXiaowuCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE GuXiaowuCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const override;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const override;
};

class GuZhongeCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE GuZhongeCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const override;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const override;
};

class GuJuekaiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE GuJuekaiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const override;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const override;
};

class GuHuoluanCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE GuHuoluanCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const override;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const override;
};

class GuMuzhenCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE GuMuzhenCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const override;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const override;
};

class GuZhengbingCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE GuZhengbingCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const override;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const override;
};

class GuQuanjianCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE GuQuanjianCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const override;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const override;
};

class GuPingtaoCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE GuPingtaoCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const override;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const override;
};

class GuJiangxianCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE GuJiangxianCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const override;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const override;
};

class GuChuanyuCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE GuChuanyuCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const override;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const override;
};


// SOURCE-2 overwrite makers
Skill *guNewChaozhen();
Skill *guNewLianjie();
Skill *guNewLianjieMod();
Skill *guNewJiangxian();
Skill *guNewXidi();
Skill *guNewXidiMod();
Skill *guNewChengyan();
Skill *guNewChuanyu();
Skill *guNewYitou();
Skill *guNewYitouClear();


class GuJichouCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE GuJichouCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const override;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const override;
};


class GuJichouGiveCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE GuJichouGiveCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const override;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const override;
};



class GuS2LiyongCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE GuS2LiyongCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const override;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const override;
};


class GuS2DaozhuanCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE GuS2DaozhuanCard();
    bool targetFixed() const override;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const override;
    const Card *validate(CardUseStruct &use) const override;
    const Card *validateInResponse(ServerPlayer *user) const override;
};

class GuMutaoCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE GuMutaoCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const override;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const override;
};


Skill *guNewJieyan();
Skill *guNewJinghua();
Skill *guNewShuiyue();
Skill *guNewJinming();
Skill *guNewXiaoshi();
Skill *guNewXiaoshiAfter();
Skill *guNewOLLiyong();
Skill *guNewJiaoyu();
Skill *guNewJiaoyuProhibit();
Skill *guNewNeixun();
Skill *guNewNeixunMax();
Skill *guNewLeiluan();
Skill *guNewFuchao();
Skill *guNewJigu();
Skill *guNewJiewan();
Skill *guNewJiewanDist();
Skill *guNewLixian();
Skill *guNewBingcai();
Skill *guNewOLZhenlie();
Skill *guNewOLMiji();


class GuLeiluanPlayCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE GuLeiluanPlayCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const override;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const override;
};

Skill *guNewLeiluanWake();
Skill *guNewGuli();
Skill *guNewAoshi();
Skill *guNewAoshiMod();
Skill *guNewDaozhuan();
void guAudit2025Source2Mobile(Package *pkg);
void guAudit2025Source2OL(Package *pkg);

class GuKuangjuanCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE GuKuangjuanCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const override;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const override;
};

void guAudit2025Source3Tenyear(Package *pkg);
void guAudit2025Source3OL(Package *pkg);

#endif

