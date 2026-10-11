#include "ai-strategy-round43-context.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonObject>
#include <QSet>

void projectAIRound43Qingsi(AIRequest &request, Room &room, ServerPlayer *viewer)
{
    auto family = request.worldView.strategyFacts.value("yinhu").toObject();
    family.remove("qingsi_num");
    const auto publish = [&] {
        if (family.isEmpty()) request.worldView.strategyFacts.remove("yinhu");
        else request.worldView.strategyFacts.insert("yinhu", family);
    };
    publish();
    if (!viewer || viewer->getRoom() != &room || room.roomRuntime()->isClosing()
        || !room.getPlayers().contains(viewer) || !request.decisionId
        || request.viewerObjectName != viewer->objectName()
        || request.worldView.self.objectName != viewer->objectName()
        || request.stateRevision != room.roomRuntime()->stateRevision()
        || request.kind != AIRequest::PlayerChosen
        || request.choiceOptions.reason != "yhchuzhu") return;
    QSet<QString> offered;
    for (const QString &name : request.choiceOptions.playerNames) {
        ServerPlayer *candidate = room.findPlayerByObjectName(name, true);
        if (!candidate || !room.getPlayers().contains(candidate) || offered.contains(name)) return;
        offered.insert(name);
    }
    // Native YHChuzhu and donor QVariant:toInt use this exact conversion:
    // absent/unconvertible values are known zero; negative values are not clamped.
    // Only this viewer's real accumulated native tag is read. No foreign writer
    // occurrence/status, guessed shared history, or extra public mark is exposed.
    family.insert("qingsi_num", viewer->getTag("YHQingsiNum").toInt());
    publish();
}
