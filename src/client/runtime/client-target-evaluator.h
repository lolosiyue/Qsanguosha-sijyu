#ifndef CLIENT_TARGET_EVALUATOR_H
#define CLIENT_TARGET_EVALUATOR_H

#include "card.h"
#include "player.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace ClientRules {

using PlayerLookup = std::function<const Player *(const QString &)>;

struct TargetStep
{
    bool known = false;
    bool fixed = false;
    bool feasible = false;
    QStringList candidates;
    QHash<QString, int> maxVotes;
};

enum class TargetValidationReason
{
    None,
    InvalidTarget,
    VoteLimitExceeded,
    MissingTarget,
    TargetCount
};

struct TargetValidation
{
    bool known = false;
    bool valid = true;
    bool incomplete = false;
    TargetValidationReason reason = TargetValidationReason::None;
    QString targetName;
    int selectedVotes = 0;
};

namespace Detail {

inline int voteLimit(const Card *card, const QList<const Player *> &chosen,
                     const Player *toSelect, const Player *self)
{
    int maxVotes = 0;
    card->targetFilter(chosen, toSelect, self, maxVotes);
    return maxVotes;
}

} // namespace Detail

inline TargetStep targetStep(const Card *card, const QStringList &chosen,
                             const QStringList &pool, const PlayerLookup &lookup,
                             const Player *self)
{
    TargetStep step;
    if (card == nullptr || self == nullptr || !lookup)
        return step;

    step.known = true;
    if (card->targetFixed()) {
        step.fixed = true;
        step.feasible = true;
        return step;
    }

    QList<const Player *> picked;
    QHash<QString, int> spent;
    for (const QString &name : chosen) {
        const Player *player = lookup(name);
        if (player == nullptr)
            return TargetStep{};
        picked.append(player);
        spent[name] = spent.value(name) + 1;
    }

    step.feasible = card->targetsFeasible(picked, self);

    for (const QString &name : pool) {
        const Player *player = lookup(name);
        if (player == nullptr)
            continue;

        const int votes = Detail::voteLimit(card, picked, player, self);
        if (votes <= spent.value(name))
            continue;

        step.candidates.append(name);
        step.maxVotes.insert(name, votes);
    }

    return step;
}

inline TargetValidation validateTargets(const Card *card,
                                        const QStringList &targets,
                                        const PlayerLookup &lookup,
                                        const Player *self)
{
    TargetValidation result;
    if (card == nullptr || self == nullptr || !lookup)
        return result;

    result.known = true;
    if (card->targetFixed())
        return result;

    QList<const Player *> picked;
    QHash<QString, int> spent;

    for (const QString &name : targets) {
        const Player *target = lookup(name);
        if (target == nullptr)
            return TargetValidation{};

        const int already = spent.value(name);
        if (Detail::voteLimit(card, picked, target, self) <= already) {
            result.valid = false;
            result.reason = already > 0
                ? TargetValidationReason::VoteLimitExceeded
                : TargetValidationReason::InvalidTarget;
            result.targetName = name;
            result.selectedVotes = already;
            return result;
        }

        picked.append(target);
        spent[name] = already + 1;
    }

    if (!card->targetsFeasible(picked, self)) {
        result.valid = false;
        result.incomplete = true;
        result.reason = picked.isEmpty()
            ? TargetValidationReason::MissingTarget
            : TargetValidationReason::TargetCount;
    }

    return result;
}

} // namespace ClientRules

#endif
