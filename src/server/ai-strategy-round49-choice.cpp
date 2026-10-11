#include "ai-strategy-round49-choice.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonObject>
#include <QSet>

void projectAIRound49ChoiceProperties(AIRequest &request, Room &room, ServerPlayer *viewer)
{
    auto properties = request.worldView.strategyFacts.value("owner_skill_properties").toObject();
    properties.remove("mythluochong_choice");
    properties.remove("qhstandardtuxi_card");
    const auto publish = [&] {
        if (properties.isEmpty()) request.worldView.strategyFacts.remove("owner_skill_properties");
        else request.worldView.strategyFacts.insert("owner_skill_properties", properties);
    };
    publish();
    if (!viewer || viewer->getRoom() != &room || !room.roomRuntime()
        || room.roomRuntime()->isClosing() || !room.getPlayers().contains(viewer)
        || !request.decisionId || request.viewerObjectName != viewer->objectName()
        || request.worldView.self.objectName != viewer->objectName()
        || request.stateRevision != room.roomRuntime()->stateRevision()) return;
    const char *property = nullptr;
    if (request.kind == AIRequest::PlayerChosen
        && request.choiceOptions.reason == "mythluochong"
        && request.choiceOptions.question == "askForPlayerChosen") {
        QSet<QString> names;
        for (const QString &name : request.choiceOptions.playerNames) {
            ServerPlayer *candidate = room.findPlayerByObjectName(name, true);
            if (!candidate || !room.getPlayers().contains(candidate) || names.contains(name)) return;
            names.insert(name);
        }
        property = "mythluochong_choice";
    } else if (request.kind == AIRequest::Discard
        && request.choiceOptions.reason == "qhstandardtuxiCARD"
        && request.choiceOptions.question == "askForDiscard") {
        property = "qhstandardtuxi_card";
    }
    if (!property) return;
    // Fixed qhstandard.lua:8330/8401 and1444 write these owner properties
    // before the exact respective question. Preserve current QVariant::toString
    // including absent/empty/unusual strings; do not infer from prompt/choices.
    // Tuxi IDs do not disclose faces or authorize payment: existing owner/known
    // face access and final native discard admission remain authoritative.
    properties.insert(QString::fromLatin1(property), viewer->property(property).toString());
    publish();
}
