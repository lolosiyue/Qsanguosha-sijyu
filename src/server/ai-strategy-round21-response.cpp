#include "ai-strategy-round21-response.h"
#include "ai.h"
#include "ai-strategy-public-disclosure.h"
#include "room.h"
#include "serverplayer.h"

void projectAIStrategyRound21Response(AIRequest &request, Room &room, ServerPlayer *viewer,
                                    QJsonObject &context)
{
    if (!viewer || viewer->getRoom()!=&room || request.viewerObjectName!=viewer->objectName()
        || request.worldView.self.objectName!=viewer->objectName()
        || request.kind!=AIRequest::RespondCard || request.choiceOptions.question!="askForCardShow"
        || request.choiceOptions.reason!="sr_yinmeng") return;
    QJsonObject row{{"known",false}};
    // Fixed source srkill.lua4060 publicly shows the chosen card BEFORE tag,
    // flag and askForCardShow. Require that disclosure in the current snapshot;
    // do not call Engine::getCard on the room tag's possibly private ID.
    const QVariant raw=room.getTag("yinmengid");
    if (!raw.isValid()) { context.insert("sr_yinmeng",row); return; }
    const int id=raw.toInt();
    int type=-1;
    const auto scan=[&](const QList<AICardView> &cards) {
        for (const auto &card:cards) if (card.effectiveId==id) {type=card.typeId;return true;}
        return false;
    };
    for (const auto *cards:{&request.worldView.handCards,&request.worldView.discardPile,&request.choiceOptions.cards})
        if (scan(*cards)) break;
    const auto player=[&](const AIPlayerView &p) {
        for (const auto *cards:{&p.knownCards,&p.equips,&p.judgingArea}) if (scan(*cards)) return true;
        for (const auto &pile:p.piles) if (pile.open && scan(pile.cards)) return true;
        return false;
    };
    if (type<0) player(request.worldView.self);
    if (type<0) for (const auto &p:request.worldView.players) if (player(p)) break;
    if (type<0) {
        const auto witness=readAIStrategyPublicDisclosure(room,viewer,id);
        const auto card=witness.value("card").toObject();
        if (witness.value("known").toBool() && card.value("type_id").isDouble()) type=card.value("type_id").toInt(-1);
    }
    if (type<0) { context.insert("sr_yinmeng",row); return; }
    QString target;
    // The only fixed writers are srkill.lua4062/4065, both public setPlayerFlag.
    // Preserve Room::getOtherPlayers order and source's first-match selection.
    for (ServerPlayer *other:room.getOtherPlayers(viewer)) {
        if (other->hasFlag("yinmengname")) {target=other->objectName();break;}
    }
    row.insert("known",true);row.insert("target",target);
    row.insert("card_id",id);row.insert("card_type",type);
    context.insert("sr_yinmeng",row);
}
