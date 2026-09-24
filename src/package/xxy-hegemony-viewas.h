#ifndef XXY_HEGEMONY_VIEWAS_H
#define XXY_HEGEMONY_VIEWAS_H

#include "skill.h"

// Imported card builders are pure request callbacks. They never borrow the
// client's global Self or store a selection on the shared skill definition.
class XxyHegemonyViewAsSkill : public ViewAsSkillV2
{
public:
    explicit XxyHegemonyViewAsSkill(const QString &name, int n = 0);

    bool canActivate(const ActiveSkillRequest &request) const override;
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override;
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override;
    const Card *createCard(const ActiveSkillRequest &request) const override;
    QString historyKey(const ActiveSkillRequest &request) const override;
    bool isEnabledAtResponse(const Player *player, const QString &pattern) const override;

    virtual bool donorViewFilter(const ActiveSkillRequest &request,
        const QList<const Card *> &selected, const Card *candidate) const = 0;
    virtual const Card *donorViewAs(const ActiveSkillRequest &request,
        const QList<const Card *> &cards) const = 0;

protected:
    bool selectedCards(const ActiveSkillRequest &request, QList<const Card *> &cards) const;
};

class XxyHegemonyOneCardViewAsSkill : public XxyHegemonyViewAsSkill
{
public:
    explicit XxyHegemonyOneCardViewAsSkill(const QString &name);

    bool donorViewFilter(const ActiveSkillRequest &request,
        const QList<const Card *> &selected, const Card *candidate) const override;
    const Card *donorViewAs(const ActiveSkillRequest &request,
        const QList<const Card *> &cards) const override;
    virtual bool donorViewFilter(const ActiveSkillRequest &request, const Card *candidate) const;
    virtual const Card *donorViewAs(const ActiveSkillRequest &request, const Card *card) const = 0;

protected:
    QString filter_pattern;
};

class XxyHegemonyZeroCardViewAsSkill : public XxyHegemonyViewAsSkill
{
public:
    explicit XxyHegemonyZeroCardViewAsSkill(const QString &name);

    bool donorViewFilter(const ActiveSkillRequest &request,
        const QList<const Card *> &selected, const Card *candidate) const override;
    const Card *donorViewAs(const ActiveSkillRequest &request,
        const QList<const Card *> &cards) const override;
    virtual const Card *donorViewAs(const ActiveSkillRequest &request) const = 0;
};

#endif
