// player.h names QObject/QString without including them.
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

#include "tui-target-advice.h"

#include "card.h"
#include "player.h"
#include "tui-text.h"

namespace {

// How many times this player may be named, given the targets already chosen.
//
// Only maxVotes is read, exactly as RoomScene::updateTargetsEnablity() reads it
// (src/ui/roomscene.cpp:2240): Collateral::targetFilter() returns false whether
// or not the target is legal and answers purely through this out-parameter.
int voteLimit(const Card *card, const QList<const Player *> &chosen,
              const Player *toSelect, const Player *self)
{
    int maxVotes = 0;
    card->targetFilter(chosen, toSelect, self, maxVotes);
    return maxVotes;
}

QString nameOf(const TuiNameText &resolver, const QString &raw)
{
    const QString shown = resolver ? resolver(raw) : QString();
    return shown.isEmpty() ? raw : shown;
}

} // namespace

TuiTargetStep tuiTargetStep(const Card *card, const QStringList &chosen,
                            const QStringList &pool, const TuiPlayerLookup &lookup,
                            const Player *self)
{
    TuiTargetStep step;
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
        // A name we cannot place leaves us with no opinion worth having, which
        // is not the same as an objection.
        if (player == nullptr)
            return TuiTargetStep{};
        picked.append(player);
        spent[name] = spent.value(name) + 1;
    }
    step.feasible = card->targetsFeasible(picked, self);

    for (const QString &name : pool) {
        const Player *player = lookup(name);
        if (player == nullptr)
            continue;
        const int votes = voteLimit(card, picked, player, self);
        if (votes <= spent.value(name))
            continue;
        step.candidates.append(name);
        step.maxVotes.insert(name, votes);
    }
    return step;
}

QString tuiValidateTargets(const Card *card, const QStringList &targets,
                           const TuiPlayerLookup &lookup, const Player *self,
                           const TuiNameText &cardName, const TuiNameText &playerName,
                           bool *incomplete)
{
    if (incomplete != nullptr)
        *incomplete = false;
    if (card == nullptr || self == nullptr || !lookup)
        return QString();
    // The targets of a target-fixed card were put there by the server, and a
    // view-as skill's card routinely answers false about them. The desktop
    // submits on targetFixed() || targetsFeasible() (roomscene.cpp:3760).
    if (card->targetFixed())
        return QString();
    const QString shownCard = nameOf(cardName, card->objectName());

    QList<const Player *> picked;
    QHash<QString, int> spent;
    for (const QString &name : targets) {
        const Player *target = lookup(name);
        if (target == nullptr)
            return QString();
        const int already = spent.value(name);
        if (voteLimit(card, picked, target, self) <= already) {
            return tuiText(already > 0 ? "tui_play_target_votes"
                                       : "tui_play_target_invalid")
                .arg(shownCard, nameOf(playerName, name));
        }
        picked.append(target);
        spent[name] = already + 1;
    }
    if (!card->targetsFeasible(picked, self)) {
        if (incomplete != nullptr)
            *incomplete = true;
        return picked.isEmpty() ? tuiText("tui_play_target_missing").arg(shownCard)
                                : tuiText("tui_play_target_count").arg(shownCard);
    }
    return QString();
}
