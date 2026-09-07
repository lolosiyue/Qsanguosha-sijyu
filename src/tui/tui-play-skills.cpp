#include "tui-play-skills.h"
#include "tui-text.h"

#include "runtime/client-selection-runtime.h"

QString tuiPatternSkillName(const QString &pattern, int *instanceId)
{
    return ClientRules::patternSkillName(pattern, instanceId);
}

CardUseStruct::CardUseReason tuiSkillPromptReason(
    InteractionType type, int handlingMethod, const QString &pattern)
{
    return ClientRules::skillPromptReason(type, handlingMethod, pattern);
}

void tuiFillSkillCandidates(const ClientGameState &state, const QString &pattern,
                            CardInteractionPayload *payload)
{
    ClientRules::fillSkillCandidates(state, pattern, payload);
}

QString tuiSkillActivationHint(const QString &skillName, int instanceId,
                               CardUseStruct::CardUseReason reason, const QString &pattern)
{
    const ClientRules::SkillActivationResult result
        = ClientRules::evaluateSkillActivation(skillName, instanceId, reason, pattern);
    // A missing runtime has no opinion; the hint must never become a local wall.
    if (!result.known || result.available)
        return QString();
    return tuiText("tui_skill_unavailable");
}

QString tuiResolveSkillCardWireText(const QString &selfName, const QString &skillName,
                                    int instanceId, const QList<int> &subcardIds,
                                    QString *error, const Card **builtCard)
{
    if (builtCard != nullptr)
        *builtCard = nullptr;
    if (error != nullptr)
        error->clear();

    ClientRules::SkillCardBuildRequest request;
    request.selfName = selfName;
    request.skillName = skillName;
    request.instanceId = instanceId;
    request.subcardIds = subcardIds;
    // The active ClientRoomContext already carries the prompt's exact reason
    // and pattern. This replaces the old V2-only hard-coded PLAY request.

    const ClientRules::SkillCardBuildResult result
        = ClientRules::buildSkillCard(request);
    if (result.built()) {
        if (builtCard != nullptr)
            *builtCard = result.nativeCard;
        return result.cardText;
    }

    if (error == nullptr)
        return QString();
    switch (result.status) {
    case ClientRules::SkillCardBuildStatus::EngineUnavailable:
        *error = tuiText("tui_engine_not_loaded");
        break;
    case ClientRules::SkillCardBuildStatus::MissingSkill:
        *error = tuiText("tui_skill_not_view_as");
        break;
    case ClientRules::SkillCardBuildStatus::MissingSubcard:
        *error = tuiText("tui_skill_card_missing");
        break;
    case ClientRules::SkillCardBuildStatus::IncompleteSelection:
    case ClientRules::SkillCardBuildStatus::CreateRejected:
        *error = subcardIds.isEmpty()
            ? tuiText("tui_skill_needs_cards")
            : tuiText("tui_skill_cards_rejected");
        break;
    case ClientRules::SkillCardBuildStatus::WrongSelf:
    case ClientRules::SkillCardBuildStatus::ActivationUnavailable:
        *error = tuiText("tui_skill_unavailable");
        break;
    case ClientRules::SkillCardBuildStatus::CardRejected:
        *error = tuiText("tui_skill_cards_rejected");
        break;
    case ClientRules::SkillCardBuildStatus::Unknown:
    case ClientRules::SkillCardBuildStatus::Built:
        break;
    }
    return QString();
}
