#include "ai-strategy-round21-original.h"
#include "ai.h"
#include "room.h"
#include "serverplayer.h"
#include "hegemony-mode.h"

namespace {
bool admitted(const AIWorldView &world, const Room &room, const ServerPlayer *viewer)
{
    return viewer && viewer->getRoom() == &room && world.self.objectName == viewer->objectName()
        && HegemonyMode::enabled();
}
}
void projectAIStrategyRound21OriginalWorld(AIWorldView &world, Room &room, ServerPlayer *viewer)
{
    if (!admitted(world, room, viewer)) return;
    QJsonObject marks, visibleCounts;
    for (const auto *target : room.getAllPlayers(true)) {
        // Only this observer's AI "already viewed" marker. Never a target's
        // actual hidden general, another viewer's tag, or their personal marks.
        const QString key = "KnownBoth_" + viewer->objectName() + "_" + target->objectName();
        marks.insert(target->objectName(), target->getMark(key));
        QList<int> ids = target->handCards();
        ids.append(target->getPile("wooden_ox"));
        int visible = 0;
        const QString flag = "visible_" + viewer->objectName() + "_" + target->objectName();
        for (int id : ids) {
            const Card *card = room.getCard(id);
            if (card && (target == viewer || card->hasFlag("visible") || card->hasFlag(flag))) ++visible;
        }
        // Original common3037/4749 counts only hand+WoodenOx cards already
        // visible to this observer. Neither hidden IDs nor faces are exported.
        visibleCounts.insert(target->objectName(), visible);
    }
    world.strategyFacts.insert("original_known_both_marks", marks);
    world.strategyFacts.insert("original_known_both_visible_counts", visibleCounts);
}
void projectAIStrategyRound21OriginalRequest(AIRequest &request, Room &room, ServerPlayer *viewer,
    const QVariant &data, QJsonObject &context)
{
    if (!admitted(request.worldView, room, viewer) || request.viewerObjectName != viewer->objectName()
        || request.kind != AIRequest::Choice || request.choiceOptions.reason != "known_both") return;
    if (!data.canConvert<ServerPlayer *>()) return;
    const auto *target = data.value<ServerPlayer *>();
    if (target && target->getRoom() != &room) return;
    context.insert("original_known_both_target", target ? QJsonValue(target->objectName()) : QJsonValue(false));
}
void projectAIStrategyRound21OriginalEvent(AIWorldView &world, Room &room, ServerPlayer *viewer,
    const AIEventView &event)
{
    if (!admitted(world, room, viewer) || event.triggerEvent != ChoiceMade || event.kind != "choice"
        || event.from != viewer->objectName() || event.reason != "known_both"
        || event.details.value("choice_kind") != "skillChoice"
        || !event.privateEvent || event.privateViewer != viewer->objectName()) return;
    // HKnownBoth::onEffect sets this exact flag around askForChoice. The actor
    // already knows the selected effect target, but has not yet seen its hidden
    // general: only a roster name is projected before native disclosure occurs.
    QJsonValue target(false);
    for (const auto *other : room.getOtherPlayers(viewer)) {
        if (other->hasFlag("KnownBothTarget")) { target = other->objectName(); break; }
    }
    world.decisionContext.insert("original_known_both_target", target);
}
