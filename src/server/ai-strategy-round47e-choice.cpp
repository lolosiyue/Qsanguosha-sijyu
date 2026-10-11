#include "ai-strategy-round47e-choice.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonObject>
#include <QSet>

void projectAIRound47EQinyin(AIRequest &request, Room &room, ServerPlayer *viewer)
{
    auto properties = request.worldView.strategyFacts.value("owner_skill_properties").toObject();
    properties.remove("qhfireqinyin-AI");
    const auto publish = [&] {
        if (properties.isEmpty()) request.worldView.strategyFacts.remove("owner_skill_properties");
        else request.worldView.strategyFacts.insert("owner_skill_properties", properties);
    };
    publish();
    if (!viewer || viewer->getRoom() != &room || !room.roomRuntime()
        || room.roomRuntime()->isClosing() || !room.getPlayers().contains(viewer)
        || !request.decisionId || request.viewerObjectName != viewer->objectName()
        || request.worldView.self.objectName != viewer->objectName()
        || request.stateRevision != room.roomRuntime()->stateRevision()
        || request.kind != AIRequest::PlayerChosen
        || request.choiceOptions.reason != "qhfireqinyin"
        || request.choiceOptions.question != "askForPlayerChosen") return;
    QSet<QString> names;
    for (const QString &name : request.choiceOptions.playerNames) {
        ServerPlayer *candidate = room.findPlayerByObjectName(name, true);
        if (!candidate || !room.getPlayers().contains(candidate) || names.contains(name)) return;
        names.insert(name);
    }
    // Fixed extensions/qhstandard.lua:5453 publishes exactly this current
    // owner's property immediately before the native PlayerChosen question.
    // The global mark-resolution source can ask a player after skill loss;
    // do not add a hasSkill/phase/current-player gate absent from that source.
    // QVariant::toString preserves absent/empty and non-choice string values.
    // No prompt-derived repair, foreign property, callback or history read.
    properties.insert("qhfireqinyin-AI", viewer->property("qhfireqinyin-AI").toString());
    publish();
}
