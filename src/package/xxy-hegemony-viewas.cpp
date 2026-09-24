#include "xxy-hegemony-viewas.h"

#include "card.h"
#include "engine.h"
#include "player.h"

#include <QSet>

namespace {
// A donor may return an existing physical card. Only freshly constructed
// virtual previews belong to the feasibility/history query.
void releasePreview(const Card *card)
{
    if (card && card->isVirtualCard() && !card->parent())
        const_cast<Card *>(card)->deleteLater();
}
}

XxyHegemonyViewAsSkill::XxyHegemonyViewAsSkill(const QString &name, int n)
    : ViewAsSkillV2(name, n)
{
}

bool XxyHegemonyViewAsSkill::canActivate(const ActiveSkillRequest &request) const
{
    if (!request.initiator) return false;
    // Source ownership, validity, payment and revealing remain the ordinary
    // V2 pipeline's responsibility, including attached and helper sources.
    switch (request.reason) {
    case CardUseStruct::CARD_USE_REASON_PLAY:
        return isEnabledAtPlay(request.initiator);
    case CardUseStruct::CARD_USE_REASON_RESPONSE:
    case CardUseStruct::CARD_USE_REASON_RESPONSE_USE:
        return isEnabledAtResponse(request.initiator, request.pattern);
    case CardUseStruct::CARD_USE_REASON_UNKNOWN:
        // MethodNone/Recast prompts retain UNKNOWN; only an explicitly
        // accepted skill selector may activate through this non-use path.
        return request.pattern.startsWith("@@")
            && isEnabledAtResponse(request.initiator, request.pattern);
    default:
        return false;
    }
}

bool XxyHegemonyViewAsSkill::isEnabledAtResponse(const Player *, const QString &pattern) const
{
    // Donor response selectors are exact tokens, not substring matches.
    return !response_pattern.isEmpty() && pattern == response_pattern;
}

bool XxyHegemonyViewAsSkill::selectedCards(const ActiveSkillRequest &request,
    QList<const Card *> &cards) const
{
    if (!request.initiator || !Sanguosha) return false;
    QSet<int> seen;
    for (int id : request.selectedCardIds) {
        if (id < 0 || seen.contains(id)) return false;
        const Card *card = Sanguosha->getCard(id);
        if (!card) return false;
        seen.insert(id);
        cards << card;
    }
    return true;
}

bool XxyHegemonyViewAsSkill::canSelectCard(const ActiveSkillRequest &request,
    const Card *candidate) const
{
    if (!candidate || candidate->getEffectiveId() < 0
        || request.selectedCardIds.contains(candidate->getEffectiveId())) return false;
    QList<const Card *> selected;
    return selectedCards(request, selected) && donorViewFilter(request, selected, candidate);
}

bool XxyHegemonyViewAsSkill::cardSelectionFeasible(const ActiveSkillRequest &request) const
{
    QList<const Card *> cards;
    if (!selectedCards(request, cards)) return false;
    ActiveSkillRequest prefix = request;
    prefix.selectedCardIds.clear();
    QList<const Card *> selected;
    for (const Card *card : cards) {
        if (!donorViewFilter(prefix, selected, card)) return false;
        prefix.selectedCardIds << card->getEffectiveId();
        selected << card;
    }
    const Card *preview = donorViewAs(request, cards);
    const bool feasible = preview != nullptr;
    releasePreview(preview);
    return feasible;
}

const Card *XxyHegemonyViewAsSkill::createCard(const ActiveSkillRequest &request) const
{
    QList<const Card *> cards;
    if (!selectedCards(request, cards)) return nullptr;
    // Keep the registered SkillCard or converted card identity. The V2 source
    // gate decorates it; its normal card pipeline owns movement and effects.
    // In particular, V2 pay must not also discard a SkillCard's subcards.
    const Card *card = donorViewAs(request, cards);
    if (!card || card->isVirtualCard()) return card;

    // A physical selection (for example Midao's rice) belongs to the room.
    // Source decoration must target an independent virtual card, never the
    // permanent WrappedCard. Keep its current filtered identity and one cost.
    const Card *real = card->getRealCard();
    if (!real || card->getEffectiveId() < 0) return nullptr;
    Card *selection = Sanguosha->cloneCard(card->objectName(), card->getSuit(),
        card->getNumber(), card->getFlags());
    if (selection && selection->metaObject() != real->metaObject()) {
        delete selection;
        selection = nullptr;
    }
    // A mode-specific name may resolve to another class; the explicit factory
    // retains the selected physical card's class across mixed content pools.
    if (!selection)
        selection = Sanguosha->cloneCard(QString::fromLatin1(real->metaObject()->className()),
            card->getSuit(), card->getNumber(), card->getFlags());
    if (!selection || selection->metaObject() != real->metaObject()) {
        delete selection;
        return nullptr;
    }
    selection->setObjectName(card->objectName());
    selection->setTransferable(card->isTransferable());
    selection->setSkillName(objectName());
    selection->addSubcard(card->getEffectiveId());
    return selection;
}

QString XxyHegemonyViewAsSkill::historyKey(const ActiveSkillRequest &request) const
{
    QList<const Card *> cards;
    if (!selectedCards(request, cards)) return QString();
    const Card *preview = donorViewAs(request, cards);
    if (!preview) return QString();
    const QString key = preview->getClassName();
    releasePreview(preview);
    return key;
}

XxyHegemonyOneCardViewAsSkill::XxyHegemonyOneCardViewAsSkill(const QString &name)
    : XxyHegemonyViewAsSkill(name, 1)
{
}

bool XxyHegemonyOneCardViewAsSkill::donorViewFilter(const ActiveSkillRequest &request,
    const QList<const Card *> &selected, const Card *candidate) const
{
    return selected.isEmpty() && candidate && !candidate->hasFlag("using")
        && donorViewFilter(request, candidate);
}

bool XxyHegemonyOneCardViewAsSkill::donorViewFilter(const ActiveSkillRequest &request,
    const Card *candidate) const
{
    const Player *self = request.initiator;
    if (!self || !candidate || filter_pattern.isEmpty()) return false;
    QString pattern = filter_pattern;
    if (pattern.endsWith('!')) {
        if (self->isJilei(candidate)) return false;
        pattern.chop(1);
    } else if (isResponseOrUse() && pattern.contains("hand")) {
        QStringList handPiles;
        handPiles << "hand";
        for (const QString &pile : self->getPileNames()) {
            if (pile.startsWith('&') || pile == "wooden_ox") handPiles << pile;
        }
        pattern.replace("hand", handPiles.join(','));
    }
    return Sanguosha && Sanguosha->matchExpPattern(pattern, self, candidate);
}

const Card *XxyHegemonyOneCardViewAsSkill::donorViewAs(const ActiveSkillRequest &request,
    const QList<const Card *> &cards) const
{
    return cards.size() == 1 ? donorViewAs(request, cards.first()) : nullptr;
}

XxyHegemonyZeroCardViewAsSkill::XxyHegemonyZeroCardViewAsSkill(const QString &name)
    : XxyHegemonyViewAsSkill(name)
{
}

bool XxyHegemonyZeroCardViewAsSkill::donorViewFilter(const ActiveSkillRequest &,
    const QList<const Card *> &, const Card *) const
{
    return false;
}

const Card *XxyHegemonyZeroCardViewAsSkill::donorViewAs(const ActiveSkillRequest &request,
    const QList<const Card *> &cards) const
{
    return cards.isEmpty() ? donorViewAs(request) : nullptr;
}
