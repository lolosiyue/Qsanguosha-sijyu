#include "ai-strategy-round48-response.h"
#include "ai.h"
#include "card.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonArray>

bool aiRound48ChoulieEvaluation(const AIRequest &request, const ServerPlayer *viewer,
    const QJsonObject &proposal)
{
    if (!viewer || !viewer->getRoom() || !viewer->getRoom()->roomRuntime()
        || request.viewerObjectName != viewer->objectName()
        || request.stateRevision != viewer->getRoom()->roomRuntime()->stateRevision()
        || proposal.size() != 4 || proposal.value("kind") != "evaluate_skill_card"
        || proposal.value("skill") != "choulie" || proposal.value("name") != "slash"
        || !proposal.value("subcards").isArray() || !proposal.value("subcards").toArray().isEmpty())
        return false;
    // Native Choulie::cost asks all other players. Source eligibility remains
    // in the complete AI callback; this grants only its unskilled dummy Slash.
    if (request.kind == AIRequest::PlayerChosen)
        return request.choiceOptions.reason == "choulie";
    // Choulie::effect passes this exact public target in the native question.
    // Do not infer an undisclosed target or admit choulie2 (it has no clone).
    const QString target = request.choiceOptions.context.value("player").toString();
    return request.kind == AIRequest::RespondCard
        && request.choiceOptions.question == "askForCard"
        && request.pattern == ".." && request.choiceOptions.reason == ".."
        && !target.isEmpty() && request.prompt == "choulie1:" + target;
}

bool configureAIRound48ChoulieEvaluation(Card &card)
{
    // Fixed addFunction dummyCard() is exactly cloneCard("slash",6,0).
    // Never configure a live physical card or substitute a class alias.
    if (card.objectName() != "slash" || !card.isKindOf("Slash")
        || !card.isVirtualCard() || !card.getSubcards().isEmpty()
        || card.getSuit() != Card::NoSuit || card.getNumber() != 0
        || !card.getSkillName(false).isEmpty())
        return false;
    card.setFlags("Qinggang");
    return true;
}
