#ifndef CLIENT_SELECTION_RUNTIME_H
#define CLIENT_SELECTION_RUNTIME_H

#include "card.h"
#include "client-game-state.h"
#include "client-player-model.h"
#include "client-target-evaluator.h"
#include "engine.h"
#include "interaction-model.h"
#include "skill.h"
#include "structs.h"

#include <QRegularExpression>
#include <QSet>

namespace ClientRules {

// A prompt pattern such as @@tuxi, @rende or @@guhuo3 names the ViewAs skill
// the server expects. Plain card patterns return an empty name.
inline QString patternSkillName(const QString &pattern, int *instanceId = nullptr)
{
    if (instanceId != nullptr)
        *instanceId = 0;
    static const QRegularExpression named(QStringLiteral("^@@?([_A-Za-z]+)(\\d+)?!?$"));
    const QRegularExpressionMatch match = named.match(pattern);
    if (!match.hasMatch())
        return QString();
    if (instanceId != nullptr && !match.captured(2).isEmpty())
        *instanceId = match.captured(2).toInt();
    return match.captured(1);
}

// Maps the Qt-only interaction model back to the native card-use context the
// engine's ViewAs callbacks consume.
inline CardUseStruct::CardUseReason skillPromptReason(
    InteractionType type, int handlingMethod, const QString &pattern)
{
    switch (type) {
    case InteractionType::PlayCard:
        return CardUseStruct::CARD_USE_REASON_PLAY;
    case InteractionType::AskPeach:
    case InteractionType::Nullification:
        return CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    case InteractionType::ResponseCard:
        switch (static_cast<Card::HandlingMethod>(handlingMethod)) {
        case Card::MethodPlay:
            return CardUseStruct::CARD_USE_REASON_PLAY;
        case Card::MethodUse:
            return CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
        case Card::MethodResponse:
            return CardUseStruct::CARD_USE_REASON_RESPONSE;
        default:
            return patternSkillName(pattern).isEmpty()
                ? CardUseStruct::CARD_USE_REASON_UNKNOWN
                : CardUseStruct::CARD_USE_REASON_RESPONSE;
        }
    default:
        return CardUseStruct::CARD_USE_REASON_UNKNOWN;
    }
}

// Adds the ViewAs skills visible to the client. The returned candidates are
// presentation data only; evaluateSkillActivation() is the native rule query.
inline void fillSkillCandidates(const ClientGameState &state, const QString &pattern,
                                CardInteractionPayload *payload)
{
    if (payload == nullptr || Sanguosha == nullptr)
        return;
    const QString selfName = state.selfName();
    if (selfName.isEmpty())
        return;

    QList<SkillActivationCandidate> candidates;
    QSet<QString> seenKeys;
    QSet<QString> seenNames;
    const auto addSkill = [&](const QString &name, int instanceId) {
        const Skill *skill = Sanguosha->getSkill(name);
        if (skill == nullptr || skill->isHideSkill() || !skill->isVisible())
            return;
        if (skill->inherits("FilterSkill"))
            return;
        if (ViewAsSkill::parseViewAsSkill(skill) == nullptr)
            return;
        const QString key = QStringLiteral("%1#%2").arg(name).arg(instanceId);
        if (seenKeys.contains(key))
            return;
        if (instanceId <= 0 && seenNames.contains(name))
            return;
        seenKeys.insert(key);
        if (instanceId > 0)
            seenNames.insert(name);
        SkillActivationCandidate candidate;
        candidate.skillName = name;
        candidate.instanceId = instanceId;
        candidates.append(candidate);
    };

    const QVariantMap instances = state.playerValue(
        selfName, QStringLiteral("skill_instances")).toMap();
    for (auto it = instances.constBegin(); it != instances.constEnd(); ++it) {
        const QVariantMap entry = it.value().toMap();
        if (!entry.value(QStringLiteral("visible"), true).toBool())
            continue;
        addSkill(entry.value(QStringLiteral("skill_name")).toString(),
                 entry.value(QStringLiteral("instance_id")).toInt());
    }
    for (const QString &name :
         state.playerValue(selfName, QStringLiteral("skills")).toStringList()) {
        addSkill(name, 0);
    }
    for (int cardId : state.cardsForPlayer(selfName, Player::PlaceEquip)) {
        const Card *equip = Sanguosha->getCard(cardId, false);
        if (equip != nullptr)
            addSkill(equip->objectName(), 0);
    }

    int namedInstance = 0;
    const QString named = patternSkillName(pattern, &namedInstance);
    if (!named.isEmpty() && Sanguosha->getViewAsSkill(named) != nullptr) {
        QList<SkillActivationCandidate> only;
        for (const SkillActivationCandidate &candidate : candidates) {
            if (candidate.skillName == named
                && (namedInstance == 0 || candidate.instanceId == namedInstance)) {
                only.append(candidate);
            }
        }
        if (only.isEmpty()) {
            SkillActivationCandidate borrowed;
            borrowed.skillName = named;
            borrowed.instanceId = namedInstance;
            only.append(borrowed);
        }
        candidates = only;
    }
    payload->skillCandidates.append(candidates);
}

enum class SkillActivationStatus
{
    Unknown,
    Available,
    MissingSkill,
    InvalidInstance,
    Unavailable
};

struct SkillActivationResult
{
    SkillActivationStatus status = SkillActivationStatus::Unknown;
    bool known = false;
    bool available = false;
};

inline SkillActivationResult evaluateSkillActivation(
    const QString &skillName, int instanceId,
    CardUseStruct::CardUseReason reason, const QString &pattern)
{
    SkillActivationResult result;
    if (Sanguosha == nullptr || QSanEngine::Self == nullptr)
        return result;

    result.known = true;
    const Player *self = QSanEngine::Self;
    const ViewAsSkill *skill = Sanguosha->getViewAsSkill(skillName);
    if (skill == nullptr) {
        result.status = SkillActivationStatus::MissingSkill;
        return result;
    }

    const auto *activeSkill = dynamic_cast<const ViewAsSkillV2 *>(skill);
    if (activeSkill == nullptr) {
        result.available = skill->isAvailable(self, reason, pattern);
        result.status = result.available
            ? SkillActivationStatus::Available : SkillActivationStatus::Unavailable;
        return result;
    }

    const bool continuesEffect
        = self->getMark(QStringLiteral("ViewAsSkill_") + skill->objectName()
                        + QStringLiteral("Effect")) > 0;
    if (instanceId > 0) {
        const bool hasInstance = self->hasSkillInstance(skill->objectName(), instanceId);
        if ((!hasInstance && !continuesEffect)
            || (hasInstance && self->isSkillInvalid(skill->objectName(), instanceId))) {
            result.status = SkillActivationStatus::InvalidInstance;
            return result;
        }
    } else if (!self->hasSkill(skill->objectName()) && !continuesEffect) {
        result.status = SkillActivationStatus::InvalidInstance;
        return result;
    }

    ActiveSkillRequest request;
    request.reason = reason;
    request.pattern = pattern;
    request.initiator = self;
    request.activationRef = SkillInstanceRef(self->objectName(),
        SkillInstanceKey(skill->objectName(), instanceId));
    result.available = activeSkill->canActivate(request);
    result.status = result.available
        ? SkillActivationStatus::Available : SkillActivationStatus::Unavailable;
    return result;
}

enum class SkillCardBuildStatus
{
    Unknown,
    Built,
    EngineUnavailable,
    MissingSkill,
    WrongSelf,
    ActivationUnavailable,
    MissingSubcard,
    CardRejected,
    IncompleteSelection,
    CreateRejected
};

struct SkillCardBuildRequest
{
    QString selfName;
    QString skillName;
    int instanceId = 0;
    QList<int> subcardIds;
    QStringList selectedTargets;
    QString userString;
    CardUseStruct::CardUseReason reason = CardUseStruct::CARD_USE_REASON_UNKNOWN;
    QString pattern;
    // Runtime frontends normally set the room context before querying. Keeping
    // this true makes TUI/native/WASM consume the exact same reason/pattern.
    bool useCurrentContext = true;
};

struct SkillCardBuildResult
{
    SkillCardBuildStatus status = SkillCardBuildStatus::Unknown;
    QString cardText;
    // Native-only transient handle. A JS/WASM binding must never export it;
    // copy cardText/selection data while this call is on the native side.
    const Card *nativeCard = nullptr;

    bool built() const
    {
        return status == SkillCardBuildStatus::Built && nativeCard != nullptr
            && !cardText.isEmpty();
    }
};

inline SkillCardBuildResult buildSkillCard(const SkillCardBuildRequest &input)
{
    SkillCardBuildResult result;
    if (Sanguosha == nullptr) {
        result.status = SkillCardBuildStatus::EngineUnavailable;
        return result;
    }

    const Player *self = QSanEngine::Self;
    if (self == nullptr || (!input.selfName.isEmpty() && self->objectName() != input.selfName)) {
        result.status = SkillCardBuildStatus::WrongSelf;
        return result;
    }

    const ViewAsSkill *viewAs = Sanguosha->getViewAsSkill(input.skillName);
    if (viewAs == nullptr) {
        result.status = SkillCardBuildStatus::MissingSkill;
        return result;
    }

    SkillCardBuildRequest requestData = input;
    if (requestData.useCurrentContext) {
        requestData.reason = Sanguosha->getCurrentCardUseReason();
        requestData.pattern = Sanguosha->getCurrentCardUsePattern();
    }

    const SkillActivationResult activation = evaluateSkillActivation(
        requestData.skillName, requestData.instanceId,
        requestData.reason, requestData.pattern);
    if (activation.known && !activation.available) {
        result.status = SkillCardBuildStatus::ActivationUnavailable;
        return result;
    }

    const Card *card = nullptr;
    if (const auto *v2 = dynamic_cast<const ViewAsSkillV2 *>(viewAs)) {
        ActiveSkillRequest request;
        request.reason = requestData.reason;
        request.pattern = requestData.pattern;
        request.initiator = self;
        request.activationRef = SkillInstanceRef(self->objectName(),
            SkillInstanceKey(requestData.skillName, requestData.instanceId));
        request.selectedTargetNames = requestData.selectedTargets;
        request.userString = requestData.userString;

        for (int cardId : requestData.subcardIds) {
            const Card *candidate = Sanguosha->getCard(cardId, false);
            if (candidate == nullptr) {
                result.status = SkillCardBuildStatus::MissingSubcard;
                return result;
            }
            if (!v2->canSelectCard(request, candidate)) {
                result.status = SkillCardBuildStatus::CardRejected;
                return result;
            }
            request.selectedCardIds.append(cardId);
        }
        if (!v2->cardSelectionFeasible(request)) {
            result.status = SkillCardBuildStatus::IncompleteSelection;
            return result;
        }
        card = v2->createCard(request);
    } else if (const auto *zero = qobject_cast<const ZeroCardViewAsSkill *>(viewAs)) {
        card = zero->viewAs();
    } else {
        QList<const Card *> selected;
        for (int cardId : requestData.subcardIds) {
            const Card *candidate = Sanguosha->getCard(cardId, false);
            if (candidate == nullptr) {
                result.status = SkillCardBuildStatus::MissingSubcard;
                return result;
            }
            if (!viewAs->viewFilter(selected, candidate)) {
                result.status = SkillCardBuildStatus::CardRejected;
                return result;
            }
            selected.append(candidate);
        }
        card = viewAs->viewAs(selected);
    }

    if (card == nullptr) {
        result.status = SkillCardBuildStatus::CreateRejected;
        return result;
    }

    Card *mutableCard = const_cast<Card *>(card);
    mutableCard->setActivationSkill(requestData.skillName, requestData.instanceId);
    result.cardText = card->toString();
    result.nativeCard = card;
    result.status = SkillCardBuildStatus::Built;

    // Queue this temporary only. Card::deleteLater() also drains the global
    // lifetime manager, which would reap unrelated pending cards.
    if (card->isVirtualCard() && card->parent() == nullptr)
        mutableCard->QObject::deleteLater();
    return result;
}

struct CardSelectionDraft
{
    int cardId = -1; // physical/current room card; ignored when skillName is set
    SkillCardBuildRequest skill;
    QStringList selectedTargets;
    QStringList targetPool;
};

struct CardSelectionEvaluation
{
    bool known = false;
    bool cardReady = false;
    bool canConfirm = false;
    QString cardText;
    SkillCardBuildStatus buildStatus = SkillCardBuildStatus::Unknown;
    TargetStep nextTargets;
    TargetValidation targetValidation;
    const Card *nativeCard = nullptr;
};

// One presentation-neutral selection query for physical cards and ViewAs
// cards. It produces only native rule results; final acceptance remains server
// authoritative.
inline CardSelectionEvaluation evaluateCardSelection(
    const CardSelectionDraft &draft, const PlayerLookup &lookup, const Player *self)
{
    CardSelectionEvaluation result;
    if (Sanguosha == nullptr || self == nullptr || !lookup)
        return result;

    const Card *card = nullptr;
    if (!draft.skill.skillName.isEmpty()) {
        SkillCardBuildRequest request = draft.skill;
        request.selectedTargets = draft.selectedTargets;
        const SkillCardBuildResult built = buildSkillCard(request);
        result.buildStatus = built.status;
        if (!built.built()) {
            result.known = built.status != SkillCardBuildStatus::Unknown;
            return result;
        }
        card = built.nativeCard;
        result.cardText = built.cardText;
    } else {
        card = Sanguosha->getCard(draft.cardId, false);
        if (card == nullptr) {
            result.known = true;
            return result;
        }
        result.cardText = card->toString();
    }

    result.known = true;
    result.cardReady = true;
    result.nativeCard = card;
    result.nextTargets = targetStep(
        card, draft.selectedTargets, draft.targetPool, lookup, self);
    result.targetValidation = validateTargets(
        card, draft.selectedTargets, lookup, self);
    result.canConfirm = result.targetValidation.known && result.targetValidation.valid;
    return result;
}

// Builds the canonical ClientCore response from a native evaluation. The
// existing InteractionReplyEncoder remains the single protocol/wire encoder.
inline InteractionResponse makeCardSelectionResponse(
    const InteractionRequest &request, const CardSelectionDraft &draft,
    const CardSelectionEvaluation &evaluation)
{
    QList<int> ids;
    if (draft.skill.skillName.isEmpty() && draft.cardId >= 0)
        ids.append(draft.cardId);
    InteractionResponse response = InteractionResponse::makeCards(
        request.requestId, ids, evaluation.cardText);
    response.command = request.command;
    if (auto *answer = std::get_if<InteractionResponse::CardSelectionData>(
            &response.payload)) {
        answer->subcardIds = draft.skill.subcardIds;
        answer->targets = draft.selectedTargets;
        answer->activationSkillName = draft.skill.skillName;
        answer->activationSkillInstanceId = draft.skill.instanceId;
    }
    return response;
}

} // namespace ClientRules

#endif
