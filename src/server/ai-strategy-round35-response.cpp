#include "ai-strategy-round35-response.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonObject>

void projectAIRound35TrustCardDefault(AIRequest &r, Room &room, ServerPlayer *p)
{
    const auto h=r.worldView.strategyFacts.value("hegemony").toObject();
    if (!p || p->getRoom()!=&room || r.kind!=AIRequest::RespondCard
        || r.choiceOptions.question!="askForCard" || r.decisionId==0
        || r.viewerObjectName!=p->objectName() || r.worldView.self.objectName!=p->objectName()
        || r.stateRevision!=room.roomRuntime()->stateRevision()
        || h.value("enabled")!=QJsonValue(true)
        || h.value("original_overrides_loaded")!=QJsonValue(true))return;
    // Fixed ai.cpp TrustAI::askForCard: no AI callback, no policy fallback, no
    // conversion, no sorting or RNG. All inspected cards are the viewer's HE.
    QString match=r.pattern;
    if(match.endsWith(QLatin1Char('!')))match.chop(1);
    int selected=-1;
    for(const Card *card:p->getCards("he")) {
        if(p->isCardLimited(card,r.handlingMethod))continue;
        if(Sanguosha->matchPattern(match,p,card)){selected=card->getEffectiveId();break;}
    }
    // No visited-card list or new faces: the snapshot already has lawful owner HE.
    r.choiceOptions.context.insert("original_trust_card_default",QJsonObject{
        {"known",true},{"recipe","TrustAI.askForCard/owner-he/v1"},
        {"viewer",r.viewerObjectName},{"decision_id",r.getDecisionId()},
        {"state_revision",r.getStateRevision()},{"pattern",r.pattern},
        {"handling_method",int(r.handlingMethod)},{"selected_id",selected}});
}
