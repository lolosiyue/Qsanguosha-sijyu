#include "tui-target-advice.h"

#include "card.h"
#include "tui-text.h"

namespace {

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
    return ClientRules::targetStep(card, chosen, pool, lookup, self);
}

QString tuiValidateTargets(const Card *card, const QStringList &targets,
                           const TuiPlayerLookup &lookup, const Player *self,
                           const TuiNameText &cardName, const TuiNameText &playerName,
                           bool *incomplete)
{
    if (incomplete != nullptr)
        *incomplete = false;

    const ClientRules::TargetValidation validation
        = ClientRules::validateTargets(card, targets, lookup, self);
    if (!validation.known || validation.valid)
        return QString();

    if (incomplete != nullptr)
        *incomplete = validation.incomplete;

    const QString shownCard = nameOf(cardName, card->objectName());
    switch (validation.reason) {
    case ClientRules::TargetValidationReason::InvalidTarget:
        return tuiText("tui_play_target_invalid")
            .arg(shownCard, nameOf(playerName, validation.targetName));
    case ClientRules::TargetValidationReason::VoteLimitExceeded:
        return tuiText("tui_play_target_votes")
            .arg(shownCard, nameOf(playerName, validation.targetName));
    case ClientRules::TargetValidationReason::MissingTarget:
        return tuiText("tui_play_target_missing").arg(shownCard);
    case ClientRules::TargetValidationReason::TargetCount:
        return tuiText("tui_play_target_count").arg(shownCard);
    case ClientRules::TargetValidationReason::None:
        break;
    }

    return QString();
}
