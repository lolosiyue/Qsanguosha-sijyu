#include "tui-play-skills.h"
#include "tui-text.h"

#include "card.h"
#include "client-game-state.h"
#include "engine.h"
#include "skill.h"
#include "tui-client-player.h"

#include <QObject>
#include <QRegularExpression>
#include <QSet>

namespace {

} // namespace

QString tuiPatternSkillName(const QString &pattern, int *instanceId)
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

CardUseStruct::CardUseReason tuiSkillPromptReason(InteractionType type, int handlingMethod,
                                                  const QString &pattern)
{
    switch (type) {
    case InteractionType::PlayCard:
        return CardUseStruct::CARD_USE_REASON_PLAY;
    // Both are presented as RespondingUse no matter what the wire says, and
    // the wire says nothing: neither builder sets a handling method.
    case InteractionType::AskPeach:
    case InteractionType::Nullification:
        return CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    // Client::presentCardResponse() turns the handling method into a status,
    // and RoomScene::updateStatus() turns that status into this reason.
    case InteractionType::ResponseCard:
        switch (static_cast<Card::HandlingMethod>(handlingMethod)) {
        case Card::MethodPlay:
            return CardUseStruct::CARD_USE_REASON_PLAY;
        case Card::MethodUse:
            return CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
        case Card::MethodResponse:
            return CardUseStruct::CARD_USE_REASON_RESPONSE;
        default:
            // RespondingForDiscard and RespondingNonTrigger: the desktop leaves
            // its skill buttons dead unless the prompt names a skill.
            return tuiPatternSkillName(pattern).isEmpty()
                ? CardUseStruct::CARD_USE_REASON_UNKNOWN
                : CardUseStruct::CARD_USE_REASON_RESPONSE;
        }
    default:
        return CardUseStruct::CARD_USE_REASON_UNKNOWN;
    }
}

QString tuiSkillActivationHint(const QString &skillName, int instanceId,
                               CardUseStruct::CardUseReason reason, const QString &pattern)
{
    // No engine-backed self means no opinion; silence beats a wrong mark.
    if (Sanguosha == nullptr || QSanEngine::Self == nullptr)
        return QString();
    const Player *self = QSanEngine::Self;
    const ViewAsSkill *skill = Sanguosha->getViewAsSkill(skillName);
    if (skill == nullptr)
        return tuiText("tui_skill_unavailable");

    const auto *activeSkill = dynamic_cast<const ViewAsSkillV2 *>(skill);
    if (activeSkill == nullptr)
        return skill->isAvailable(self, reason, pattern) ? QString() : tuiText("tui_skill_unavailable");

    // V2 asks canActivate() rather than the isEnabledAt* pair, and the
    // activation instance has to exist and be valid first -- the same order
    // RoomScene::isSkillButtonAvailable() walks.
    const bool continuesEffect
        = self->getMark(QStringLiteral("ViewAsSkill_") + skill->objectName()
                        + QStringLiteral("Effect")) > 0;
    if (instanceId > 0) {
        const bool hasInstance = self->hasSkillInstance(skill->objectName(), instanceId);
        if ((!hasInstance && !continuesEffect)
            || (hasInstance && self->isSkillInvalid(skill->objectName(), instanceId)))
            return tuiText("tui_skill_unavailable");
    } else if (!self->hasSkill(skill->objectName()) && !continuesEffect) {
        return tuiText("tui_skill_unavailable");
    }

    ActiveSkillRequest request;
    request.reason = reason;
    request.pattern = pattern;
    request.initiator = self;
    request.activationRef = SkillInstanceRef(self->objectName(),
        SkillInstanceKey(skill->objectName(), instanceId));
    return activeSkill->canActivate(request) ? QString() : tuiText("tui_skill_unavailable");
}

void tuiFillSkillCandidates(const ClientGameState &state, const QString &pattern,
                            CardInteractionPayload *payload)
{
    if (payload == nullptr || Sanguosha == nullptr)
        return;
    const QString self = state.selfName();
    if (self.isEmpty())
        return;

    QList<SkillActivationCandidate> candidates;
    QSet<QString> seenKeys;
    QSet<QString> seenNames;
    auto addSkill = [&](const QString &name, int instanceId) {
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

    const QVariantMap instances = state.playerValue(self, QStringLiteral("skill_instances")).toMap();
    for (auto it = instances.constBegin(); it != instances.constEnd(); ++it) {
        const QVariantMap entry = it.value().toMap();
        if (!entry.value(QStringLiteral("visible"), true).toBool())
            continue;
        addSkill(entry.value(QStringLiteral("skill_name")).toString(),
                 entry.value(QStringLiteral("instance_id")).toInt());
    }
    for (const QString &name : state.playerValue(self, QStringLiteral("skills")).toStringList())
        addSkill(name, 0);
    for (int cardId : state.cardsForPlayer(self, 1)) {
        const Card *equip = Sanguosha->getEngineCard(cardId);
        if (equip != nullptr)
            addSkill(equip->objectName(), 0);
    }

    // A prompt that names a skill accepts that skill's card and nothing else,
    // so it gets the list to itself -- including when the player holds the
    // skill through an effect rather than the skill list the server sent.
    // An @-shaped pattern that resolves to no skill is not a naming at all,
    // and must not take the player's own skills away with it.
    int namedInstance = 0;
    const QString named = tuiPatternSkillName(pattern, &namedInstance);
    if (!named.isEmpty() && Sanguosha->getViewAsSkill(named) != nullptr) {
        QList<SkillActivationCandidate> only;
        for (const SkillActivationCandidate &candidate : candidates) {
            if (candidate.skillName == named
                && (namedInstance == 0 || candidate.instanceId == namedInstance))
                only.append(candidate);
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

QString tuiResolveSkillCardWireText(const QString &selfName, const QString &skillName,
                                    int instanceId, const QList<int> &subcardIds, QString *error,
                                    const Card **builtCard)
{
    if (builtCard != nullptr)
        *builtCard = nullptr;
    if (Sanguosha == nullptr) {
        if (error != nullptr)
            *error = tuiText("tui_engine_not_loaded");
        return QString();
    }
    const ViewAsSkill *viewAs = Sanguosha->getViewAsSkill(skillName);
    if (viewAs == nullptr) {
        if (error != nullptr)
            *error = tuiText("tui_skill_not_view_as");
        return QString();
    }

    const Card *card = nullptr;
    if (const auto *v2 = dynamic_cast<const ViewAsSkillV2 *>(viewAs)) {
        ActiveSkillRequest request;
        request.reason = CardUseStruct::CARD_USE_REASON_PLAY;
        request.selectedCardIds = subcardIds;
        request.activationRef = SkillInstanceRef(selfName,
            SkillInstanceKey(skillName, instanceId));
        card = v2->createCard(request);
    } else if (const auto *zero = qobject_cast<const ZeroCardViewAsSkill *>(viewAs)) {
        card = zero->viewAs();
    } else {
        QList<const Card *> selected;
        for (int cardId : subcardIds) {
            const Card *subcard = Sanguosha->getEngineCard(cardId);
            if (subcard == nullptr) {
                if (error != nullptr)
                    *error = tuiText("tui_skill_card_missing");
                return QString();
            }
            selected.append(subcard);
        }
        card = viewAs->viewAs(selected);
    }
    if (card == nullptr) {
        if (error != nullptr) {
            *error = subcardIds.isEmpty()
                ? tuiText("tui_skill_needs_cards")
                : tuiText("tui_skill_cards_rejected");
        }
        return QString();
    }

    Card *mutableCard = const_cast<Card *>(card);
    mutableCard->setActivationSkill(skillName, instanceId);
    const QString text = card->toString();
    if (builtCard != nullptr)
        *builtCard = card;
    // Card::deleteLater() also drain()s the global lifetime manager and can
    // reap unrelated pending engine cards. Queue only this temporary virtual.
    if (card->isVirtualCard() && card->parent() == nullptr)
        mutableCard->QObject::deleteLater();
    return text;
}
