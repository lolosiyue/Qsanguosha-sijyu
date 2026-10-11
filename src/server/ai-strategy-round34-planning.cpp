#include "ai-strategy-round34-planning.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "serverplayer.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
namespace {
QStringList matchedKinds(const Card *card) {
    if(!card)return {};
    // Complete finite native override audit, not a catalogue cache:
    // Card.cpp169/180: Qt ancestor names + Original H aliases; QObject is
    // intentionally omitted by getKindOfNames and included here.
    // lua-wrapper.h: class_names + LuaCard + native ancestors; LuaTrickCard
    // additionally enumerates its current SingleTarget/Delayed/Global/AOE subclass.
    // wrapped-card.h285/291 delegates both getters to the same current inner card.
    // Only two fixed native overrides add names absent from that enumeration:
    // MeleeSlashJink (meleemode.cpp212): Slash/Jink; YanxiaoCard (bgm.h73): SkillCard.
    // Recheck native isKindOf rather than treating metadata entries as true.
    const QStringList enumerated=card->getKindOfNames();
    QSet<QString> names(enumerated.cbegin(),enumerated.cend());
    names.insert(QStringLiteral("QObject"));names.insert(QStringLiteral("Slash"));
    names.insert(QStringLiteral("Jink"));names.insert(QStringLiteral("SkillCard"));
    QStringList result;
    for(const QString &name:names){const QByteArray bytes=name.toUtf8();if(card->isKindOf(bytes.constData()))result.append(name);}
    result.sort();return result;
}
void addCounts(QJsonObject &counts,const QStringList &names) {
    QSet<QString> once(names.cbegin(),names.cend());
    for(const auto &name:once)counts.insert(name,counts.value(name).toInt()+1);
}
}
void projectAIRound34PlanningCardCounts(AIWorldView &world,Room &room,const ServerPlayer *viewer) {
    if(!viewer || viewer->getRoom()!=&room || world.self.objectName!=viewer->objectName())return;
    const auto hegemony=world.strategyFacts.value("hegemony").toObject();
    if(!hegemony.value("enabled").toBool() || !hegemony.value("original_overrides_loaded").toBool())return;
    QList<const Card *> definitions;
    for(int id=0;id<Sanguosha->getCardCount();++id)definitions.append(Sanguosha->getEngineCard(id));
    QJsonObject engineCounts;bool missingEngineCard=false;
    // Public catalogue only: deliberately not getDeckCardIds/getRandomCards.
    for(const Card *card:definitions){if(!card)missingEngineCard=true;else addCounts(engineCounts,matchedKinds(card));}
    QJsonObject discards;
    for(int id:room.getDiscardPile()){
        const Card *printed=Sanguosha->getEngineCard(id);
        if(printed)discards.insert(QString::number(id),QJsonArray::fromStringList(matchedKinds(printed)));
        else discards.insert(QString::number(id),false);
    }
    QJsonObject knowledge;
    for(const ServerPlayer *player:room.getAllPlayers(true)){
        const QString visibleFlag=QStringLiteral("visible_%1_%2").arg(viewer->objectName(),player->objectName());
        const auto visible=[&](const Card *card){return card && (player==viewer || card->hasFlag("visible") || card->hasFlag(visibleFlag));};
        QJsonObject counts;int handCount=0,oxCount=0;
        const auto observe=[&](const Card *card){
            auto kinds=matchedKinds(card);
            const QString suit=card->getSuitString();
            if(suit=="club"||suit=="spade"||suit=="diamond"||suit=="heart")kinds.append(suit);
            if(card->isRed())kinds.append("red");if(card->isBlack())kinds.append("black");
            addCounts(counts,kinds);
        };
        for(const Card *card:player->getHandcards())if(visible(card)){++handCount;observe(card);}
        for(int id:player->getPile("wooden_ox")){
            const Card *card=Sanguosha->getCard(id);
            if(visible(card)){++oxCount;observe(card);}
        }
        knowledge.insert(player->objectName(),QJsonObject{{"complete",true},{"hand_visible_count",handCount},{"wooden_ox_visible_count",oxCount},{"counts",counts}});
    }
    world.strategyFacts.insert("original_card_counts",QJsonObject{
        {"complete",true},{"missing_engine_card",missingEngineCard},{"engine_size",definitions.size()},{"engine_counts",engineCounts},
        {"discard_engine_kinds",discards},{"ban_packages",QJsonArray::fromStringList(Sanguosha->getBanPackages())},
        {"knowledge",knowledge}});
}
