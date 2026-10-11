#include "ai-strategy-round31-choice.h"
#include "ai.h"
#include "card.h"
#include "room.h"
#include "serverplayer.h"
#include <QJsonArray>
#include <QSet>

void projectAIRound31Choice(AIWorldView &world, Room &room, const ServerPlayer *viewer)
{
    if (!viewer || viewer->getRoom() != &room || world.self.objectName != viewer->objectName()) return;
    QJsonObject rows;
    auto append = [&](const AIPlayerView &view) {
        const ServerPlayer *owner = room.findPlayerByObjectName(view.objectName, true);
        if (!owner || owner->getRoom() != &room) return;
        QSet<int> disclosed;
        const auto &known = owner == viewer ? world.handCards : view.knownCards;
        for (const AICardView &card : known) disclosed.insert(card.effectiveId);
        const QString flag = QStringLiteral("visible_%1_%2").arg(viewer->objectName(), owner->objectName());
        QJsonArray ids;
        bool complete = true;
        for (const Card *card : owner->getHandcards()) {
            if (!card || !(card->hasFlag(QStringLiteral("visible")) || card->hasFlag(flag))) continue;
            const int id = card->getEffectiveId();
            // Never enlarge the lawful face domain, including after an event
            // ledger revoked a stale view. Missing evidence is not an empty hand.
            if (!disclosed.contains(id)) { complete = false; break; }
            ids.append(id);
        }
        QJsonObject row{{QStringLiteral("known"), complete}};
        if (complete) row.insert(QStringLiteral("ids"), ids);
        rows.insert(view.objectName, row);
    };
    append(world.self);
    for (const AIPlayerView &view : world.players) append(view);
    QJsonObject facts = world.strategyFacts.value(QStringLiteral("round31_choice")).toObject();
    facts.insert(QStringLiteral("visible_hand_ids"), rows);
    world.strategyFacts.insert(QStringLiteral("round31_choice"), facts);
}
