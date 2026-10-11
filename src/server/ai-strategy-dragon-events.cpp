#include "ai-strategy-dragon-events.h"

void projectAIStrategyDragonEvent(AIWorldView &world, const AIEventView &event)
{
    world.strategyFacts.remove("tongling_role_witness");
    if (event.triggerEvent != CardFinished || event.cardName != "tonglingCard"
        || event.from.isEmpty()) return;
    const QString actor = event.details.value("player").toString();
    if (!actor.isEmpty() && actor != event.from) return;
    const AIPlayerView *player = nullptr;
    if (world.self.objectName == event.from) player = &world.self;
    else for (const AIPlayerView &candidate : world.players)
        if (candidate.objectName == event.from) { player = &candidate; break; }
    // Role visibility to the owner alone is insufficient: this callback follows
    // the actual public broadcast in tonglingCard::on_use, not a speculative fill.
    if (!player || !player->roleRevealed) return;
    const QString role = player->role;
    if (role != "lord" && role != "loyalist" && role != "rebel" && role != "renegade") return;
    world.strategyFacts.insert("tongling_role_witness",
        QJsonObject{{"player",player->objectName},{"role",role}});
}
