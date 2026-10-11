#include "ai-strategy-round31-intention.h"
#include "ai.h"
#include "card.h"
#include "room.h"
#include "serverplayer.h"
#include <QJsonArray>
#include <QSet>

void projectAIRound31IntentionWorld(AIWorldView &world, Room &room, ServerPlayer *viewer)
{
    if (!viewer || viewer->getRoom()!=&room || world.self.objectName!=viewer->objectName()) return;
    QJsonObject roots, cycles;
    for (ServerPlayer *start : room.getAllPlayers(true)) {
        QSet<QString> seen;
        ServerPlayer *current=start;
        bool cycle=false;
        while (current) {
            const QString name=current->objectName();
            if (seen.contains(name)) { cycle=true; break; }
            seen.insert(name);
            const QString next=current->getTag("Controller_Name").toString();
            if (next.isEmpty()) break;
            // Fixed SmartAI uses the default alive-only lookup, unlike the
            // native Room controller getter's includeDead=true lookup.
            ServerPlayer *other=room.findPlayerByObjectName(next);
            if (!other) break;
            current=other;
        }
        roots.insert(start->objectName(),current ? current->objectName() : start->objectName());
        cycles.insert(start->objectName(),cycle);
    }
    world.strategyFacts.insert("intention_controller",QJsonObject{{"roots",roots},{"cycles",cycles}});
}

void projectAIRound31IntentionEvent(AIEventView &event, const AIWorldView &world, Room &room,
    ServerPlayer *observer, ServerPlayer *eventPlayer, const QVariant &data,
    const std::function<QJsonObject(const Card *)> &publicCard)
{
    if (!observer || observer->getRoom()!=&room || world.self.objectName!=observer->objectName()) return;
    if (data.canConvert<CardUseStruct>()) {
        const auto use=data.value<CardUseStruct>();
        // Remove the old shared scalar even when the actual struct is absent.
        event.details.remove("zenhui_user");
        if (use.from && use.card && use.from==observer) {
            event.details.insert("zenhui_user",use.from->hasFlag("ZenhuiUser_"+use.card->toString()));
            event.details.remove("zenhui_user_private");
        } else event.details.insert("zenhui_user_private",true);
    }
    if (event.triggerEvent!=ChoiceMade || data.userType()!=QMetaType::QString || !eventPlayer) return;
    const QStringList prompt=data.toString().split(':');
    const QString kind=prompt.value(0);
    if (kind=="playerChosen" || kind=="Yiji") {
        // Only the public kind/reason/recipient text. Yiji's fourth field is
        // deliberately excluded: its physical IDs need individual knowledge.
        event.details.insert("prompt_list",QJsonArray::fromStringList(prompt.mid(0,3)));
        if (kind=="playerChosen")
            event.details.insert("prompt_string_complete",prompt.size()==3);
    }
    if (kind=="viewCards" && observer==eventPlayer) {
        event.details.insert("view_cards_observed",true);
        QSet<int> ids;
        ServerPlayer *owner=room.findPlayerByObjectName(prompt.value(1));
        if (owner) for (const QString &part:prompt.value(2).split('+',Qt::SkipEmptyParts)) {
            bool ok=false;const int id=part.toInt(&ok);
            if (ok && owner->handCards().contains(id)) ids.insert(id);
        }
        event.details.insert("view_cards_observed_count",ids.size());
    }
    if (kind!="Yiji") return;
    QList<AICardView> known=world.handCards;
    known.append(world.self.equips);
    for (const auto &pile:world.self.piles) known.append(pile.cards);
    for (const auto &player:world.players) {
        known.append(player.knownCards);known.append(player.equips);known.append(player.judgingArea);
        for (const auto &pile:player.piles) known.append(pile.cards);
    }
    QJsonArray cards;bool complete=true;
    for (const QString &part:prompt.value(3).split('+',Qt::SkipEmptyParts)) {
        bool ok=false;const int id=part.toInt(&ok);
        const AICardView *snapshot=nullptr;
        if (ok && id>=0) for (const auto &card:known)
            if (!card.virtualCard && card.effectiveId==id) { snapshot=&card;break; }
        if (!snapshot) { complete=false;break; }
        const Card *card=room.getCard(id);
        if (!card || card->isVirtualCard() || card->getEffectiveId()!=id
            || card->objectName()!=snapshot->objectName || card->getClassName()!=snapshot->className
            || int(card->getSuit())!=snapshot->suit || card->getNumber()!=snapshot->number) { complete=false;break; }
        cards.append(publicCard(card));
    }
    event.details.insert("yiji_cards_complete",complete);
    if (complete) event.details.insert("yiji_cards",cards);
    else event.details.remove("yiji_cards");
}
