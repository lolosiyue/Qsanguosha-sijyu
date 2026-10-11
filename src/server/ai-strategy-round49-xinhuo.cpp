#include "ai-strategy-round49-xinhuo.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonObject>
void projectAIRound49Xinhuo(AIRequest &request,Room &room,ServerPlayer *viewer)
{
    auto family=request.worldView.strategyFacts.value("qhstandard").toObject();
    family.remove("xinhuo_suits");
    const auto publish=[&]{if(family.isEmpty())request.worldView.strategyFacts.remove("qhstandard");else request.worldView.strategyFacts.insert("qhstandard",family);};
    publish();
    if(!viewer||viewer->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
        ||!room.getPlayers().contains(viewer)||!request.decisionId
        ||request.viewerObjectName!=viewer->objectName()||request.worldView.self.objectName!=viewer->objectName()
        ||request.stateRevision!=room.roomRuntime()->stateRevision()
        ||request.kind!=AIRequest::RespondCard||request.choiceOptions.question!="askForCardShow"
        ||request.choiceOptions.reason!="qhfirexinhuo")return;
    // Exact fixed source ai/qhstandard-ai.lua:2286 and native writer5481–5532.
    // This is the answering owner's current tag, including absent -> empty.
    // Do not inspect a requester/foreign tag or pre-sample the later hand branch.
    family.insert("xinhuo_suits",viewer->getTag("qhfirexinhuo").toString());
    publish();
}
