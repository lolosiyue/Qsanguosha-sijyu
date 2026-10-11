#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-round23-olclan-planning.h"
#include "ai.h"
#include "card.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "structs.h"

bool aiRound23OlClanPlanningQuestion(const AIRequest &request, Room &room,
                                    const ServerPlayer *viewer)
{
    if (!viewer || viewer->getRoom() != &room || request.kind != AIRequest::SkillInvoke
        || request.viewerObjectName != viewer->objectName()
        || request.worldView.self.objectName != viewer->objectName()
        || !aiRound44RangeRequestCurrent(room,request,viewer)) return false;
    // The Room service can normalize the authored '$-1' suffix before inquiry.
    const QString &question = request.choiceOptions.reason;
    if (question != QStringLiteral("zuheta") && question != QStringLiteral("zuheta$-1")) return false;
    return viewer->isAlive() && viewer->hasSkill(QStringLiteral("zuheta"))
        && viewer->getPhase() == Player::Play && !viewer->isChained();
}

QJsonObject aiRound23OlClanPlanningContext(const AIRequest &request, Room &room,
                                          const ServerPlayer *viewer)
{
    if (!aiRound23OlClanPlanningQuestion(request, room, viewer)) return {};
    return {{"known", true}, {"readonly", true}, {"source", "olClan.zuheta"},
        {"viewer", request.viewerObjectName}, {"decision_id", request.getDecisionId()},
        {"state_revision", request.getStateRevision()}, {"kind", "activate"},
        {"reason", int(CardUseStruct::CARD_USE_REASON_PLAY)}, {"pattern", ""},
        {"handling_method", int(Card::MethodUse)}};
}
