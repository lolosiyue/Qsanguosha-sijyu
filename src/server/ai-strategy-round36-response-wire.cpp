#include "ai-strategy-round36-response-wire.h"
#include "ai-strategy-round35-choice.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QPointer>
#include <limits>

bool aiRound36OriginalWireSyntax(const QString &wire, int *physicalId)
{
    if (physicalId) *physicalId = -1;
    if (wire == QStringLiteral(".")) return true;
    // ASCII decimal only, preserving spelling. No DummyCard '$', +/- sign,
    // whitespace, declaration, target suffix, or conversion prototype.
    if (wire.isEmpty() || wire.size() > 32) return false;
    for (const QChar c : wire) if (c < QLatin1Char('0') || c > QLatin1Char('9')) return false;
    bool ok = false;
    const qlonglong id = wire.toLongLong(&ok, 10);
    if (!ok || id < 0 || id > std::numeric_limits<int>::max()) return false;
    if (physicalId) *physicalId = int(id);
    return true;
}

bool aiRound36OriginalWireRequest(const AIRequest &request)
{
    return aiRound35OriginalQuestion(request)
        && request.kind == AIRequest::RespondCard
        && request.choiceOptions.question == QStringLiteral("askForCard");
}

static bool disclosedOwnerPhysical(const AIRequest &r, int id)
{
    for (const auto &card : r.worldView.handCards) if (card.effectiveId == id) return true;
    for (const auto &card : r.worldView.self.equips) if (card.effectiveId == id) return true;
    for (const auto &pile : r.worldView.self.piles)
        for (const auto &card : pile.cards) if (card.effectiveId == id) return true;
    return false;
}

static bool currentOwnResponseZone(Room &room, ServerPlayer *p, int id)
{
    if (!p || p->getRoom() != &room || room.getCardOwner(id) != p) return false;
    if (room.getCardPlace(id) == Player::PlaceHand && p->handCards().contains(id)) return true;
    if (room.getCardPlace(id) == Player::PlaceEquip) {
        for (const Card *card : p->getEquips())
            if (card && card->getEffectiveId() == id) return true;
    }
    return p->getHandPile().contains(id);
}

AIOriginalResponseWireOutcome commitAIRound36OriginalResponseWire(
    Room &room, ServerPlayer *viewer, const AIRequest &request, const AIResult &result,
    const std::function<bool()> &commitPriorStrategyEffects)
{
    AIOriginalResponseWireOutcome out;
    if (!result.hasOriginalResponseWire) return out;
    out.applicable = true;
    const auto reject = [&](const char *why) { out.error = QString::fromLatin1(why); return out; };
    int id = -1;
    if (!viewer || viewer->getRoom() != &room || viewer->objectName() != request.viewerObjectName
        || !aiRound36OriginalWireRequest(request) || !result.handled || !result.errorCode.isEmpty()
        || result.decisionId != request.decisionId || result.stateRevision != request.stateRevision
        || !room.roomRuntime() || request.stateRevision != room.roomRuntime()->stateRevision()
        || !aiRound36OriginalWireSyntax(result.originalResponseWire, &id))
        return reject("invalid current Original source-wire witness");
    // Only a physical/no-card result can enter this transition. An issued
    // conversion cannot be re-stamped or carried over the source tag revision.
    if (result.action.hasCardSpec || result.action.hasSkillActionContext
        || result.action.candidateId >= 0 || !result.action.randomCardChoice.isEmpty()
        || result.action.randomCardChoiceResolved
        || !result.action.selectedTargetNames.isEmpty() || !result.action.bottomCardIds.isEmpty()
        || !result.action.userString.isEmpty() || !result.action.legacyCardString.isEmpty()
        || result.action.useCardId >= 0)
        return reject("source-wire transition cannot carry a conversion or mixed action");
    const bool refusal = result.originalResponseWire == QStringLiteral(".");
    if (refusal) {
        if (result.kind != AIResult::Pass || !result.action.selectedCardIds.isEmpty())
            return reject("source refusal must carry no payment");
    } else {
        if (result.kind != AIResult::Answer || result.action.selectedCardIds != QList<int>{id}
            || !disclosedOwnerPhysical(request, id) || !currentOwnResponseZone(room, viewer, id))
            return reject("source physical wire was not an owned disclosed identity");
    }
    QPointer<ServerPlayer> player(viewer);
    if (!commitPriorStrategyEffects || !commitPriorStrategyEffects())
        return reject("source strategy prefix commit failed");
    if (!player || request.stateRevision != room.roomRuntime()->stateRevision())
        return reject("source strategy prefix unexpectedly changed gameplay revision");

    // Exact source order: onTagSet runs synchronously, even for the same string.
    // It may advance revision, move/replace the card, or alter restrictions.
    // Do not cache the Card pointer or any legality predicate before this call.
    room.setTag(QStringLiteral("AiResult"), result.originalResponseWire);
    out.committed = true;
    EngineRuntimeContextScope scope(*Sanguosha, &room);
    const Card *parsed = Card::Parse(result.originalResponseWire);
    if (refusal) {
        if (parsed) return reject("native refusal unexpectedly parsed a card");
        out.accepted = true;
        return out;
    }
    if (!player || !player->isAlive() || !currentOwnResponseZone(room, player, id)
        || !parsed || parsed->isVirtualCard() || parsed->getEffectiveId() != id)
        return reject("source physical product no longer belongs to the current actor");
    QString pattern = request.pattern;
    if (pattern.endsWith(QLatin1Char('!'))) pattern.chop(1);
    if (player->isCardLimited(parsed, request.handlingMethod)
        || (!pattern.isEmpty() && !Sanguosha->matchPattern(pattern, player, parsed)))
        return reject("source physical product is currently limited or mismatched");
    out.card = parsed;
    out.accepted = true;
    return out;
}
