#include "ai-strategy-round42-context.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonObject>
#include <QSet>

void projectAIRound42Liehou(AIRequest &request, Room &room, ServerPlayer *viewer)
{
    // Remove a copied older inquiry's answer before checking any new scope.
    QJsonObject family = request.worldView.strategyFacts.value("tenyear_hc").toObject();
    family.remove("LiehouTarget");
    const auto publish = [&] {
        if (family.isEmpty()) request.worldView.strategyFacts.remove("tenyear_hc");
        else request.worldView.strategyFacts.insert("tenyear_hc", family);
    };
    publish();
    if (!viewer || viewer->getRoom() != &room || room.roomRuntime()->isClosing()
        || !room.getPlayers().contains(viewer) || !request.decisionId
        || request.viewerObjectName != viewer->objectName()
        || request.worldView.self.objectName != viewer->objectName()
        || request.stateRevision != room.roomRuntime()->stateRevision()
        || request.kind != AIRequest::Yiji || request.choiceOptions.reason != "liehou") return;
    // The real V2 receive frame has already transferred the demanded cards.
    // Its inquiry offers the responder's hand, not a donor's hidden cards.
    if (request.choiceOptions.cardIds.isEmpty()) return;
    QSet<int> ids;
    for (int id : request.choiceOptions.cardIds) {
        if (id < 0 || ids.contains(id) || !viewer->handCards().contains(id)
            || room.getCardOwner(id) != viewer || room.getCardPlace(id) != Player::PlaceHand) return;
        ids.insert(id);
    }
    QSet<QString> targets;
    for (const QString &name : request.choiceOptions.playerNames) {
        if (targets.contains(name) || !room.findPlayerByObjectName(name, true)) return;
        targets.insert(name);
    }
    // Exactly QVariant::toPlayer in swig/qvariant.i:70, including null for
    // missing/wrong-type values. Never dereference an unvalidated tag pointer.
    ServerPlayer *donor = viewer->getTag("LiehouTarget").value<ServerPlayer *>();
    if (!donor) family.insert("LiehouTarget", false);
    else {
        if (!room.getPlayers().contains(donor)) return;
        family.insert("LiehouTarget", donor->objectName());
    }
    publish();
}
