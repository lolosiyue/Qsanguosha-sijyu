#ifndef _MOUNTAIN_H
#define _MOUNTAIN_H

#if !defined(QSAN_ENGINE_BUILD)
#include "package-dialogs.h"
#endif
#include "skill.h"

class QiaobianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE QiaobianCard();

    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
    void onEffect(CardEffectStruct &effect) const;
};

class TiaoxinCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TiaoxinCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
};

class ZhijianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ZhijianCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
};

class GuzhengCard : public SkillCard
{
    Q_OBJECT
        
public:
    Q_INVOKABLE GuzhengCard();

    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class ZhibaCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ZhibaCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
};

class FangquanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE FangquanCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
};

class MountainPackage : public Package
{
    Q_OBJECT

public:
    MountainPackage();
};

class NewShenPackage : public Package
{
    Q_OBJECT

public:
    NewShenPackage();
};

class JilveCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JilveCard();
};

class Longhun : public ViewAsSkillV2
{
public:
    explicit Longhun(const QString &name = "longhun");
    bool canActivate(const ActiveSkillRequest &request) const override;
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override;
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override;
    const Card *createCard(const ActiveSkillRequest &request) const override;
    int getEffectIndex(const ServerPlayer *player, const Card *card) const override;
    QString historyKey(const ActiveSkillRequest &request) const override;
protected:
    virtual int getEffHp(const Player *zhaoyun) const;
};

void MigrateToMobileStMountain(Package *pkg);

#endif
