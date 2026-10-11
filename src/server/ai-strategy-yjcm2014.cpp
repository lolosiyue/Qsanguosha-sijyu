#include "ai-strategy-yjcm2014.h"

#include "ai.h"
#include "card.h"
#include "room.h"
#include "serverplayer.h"
#include "structs.h"

#include <QJsonArray>
#include <QSet>

void projectAIYjcm2014(AIWorldView &world, Room &room, const ServerPlayer *viewer,
                      const std::function<QJsonObject(const Card *)> &describeCard)
{
    if (!viewer || world.self.objectName != viewer->objectName()) return;
    QJsonObject standard = world.strategyFacts.value("standard").toObject();
    // Fixed donor yjcm2014-ai.lua reads this exact owner property. The current
    // gameplay uses #shenduan instead and has no writer for shenduanForAI;
    // an empty property must remain empty, not silently become the new pile.
    standard.insert("shenduan_for_ai", viewer->property("shenduanForAI").toString());

    QSet<QString> roster;
    QJsonObject hasAI;
    const QJsonObject availability = world.strategyFacts.value("ai_control_availability").toObject();
    for (const ServerPlayer *player : room.getAllPlayers(true)) {
        roster.insert(player->objectName());
        // Reuse the viewer-authorized, read-only controller observation. A
        // foreign non-online actor's private onsole relation is not public,
        // and getAI() may repair malformed controller links while observing.
        const QJsonObject row = availability.value(player->objectName()).toObject();
        if (row.value("known").isBool() && row.value("known").toBool()
            && row.value("has_ai").isBool())
            hasAI.insert(player->objectName(), row.value("has_ai"));
        else
            hasAI.insert(player->objectName(), QJsonValue::Null);
    }
    standard.insert("player_has_ai", hasAI);

    // yjcm2014.cpp Zenhui sets this tag immediately before its owner inquiry
    // and removes it immediately afterwards. The used card is already public;
    // never enumerate arbitrary foreign player tags or expose their hand IDs.
    const QVariant tag = viewer->getTag("zenhui");
    standard.insert("zenhui_use", QJsonValue::Null);
    if (tag.canConvert<CardUseStruct>()) {
        const CardUseStruct use = tag.value<CardUseStruct>();
        if (use.card && use.from == viewer && describeCard) {
            QJsonArray targets;
            bool valid = true;
            for (const ServerPlayer *target : use.to) {
                if (!target || !roster.contains(target->objectName())) { valid = false; break; }
                targets.append(target->objectName());
            }
            const QJsonObject card = describeCard(use.card);
            if (valid && !card.isEmpty()) {
                standard.insert("zenhui_use", QJsonObject{
                    {"card", card}, {"from", viewer->objectName()}, {"to", targets}
                });
            }
        }
    }
    world.strategyFacts.insert("standard", standard);
}
