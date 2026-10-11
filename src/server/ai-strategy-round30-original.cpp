#include "ai-strategy-round30-original.h"
#include "ai.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonArray>
#include <QSet>
#include <memory>

void projectAIRound30OriginalJudge(const AIRequest &request, Room &room,
    ServerPlayer *viewer, const JudgeStruct *judge, QJsonObject &result,
    const std::function<QJsonObject(const Card *)> &publicCard)
{
    if (!viewer || !judge || !judge->who || viewer->getRoom()!=&room
        || request.viewerObjectName!=viewer->objectName()
        || request.worldView.self.objectName!=viewer->objectName()
        || room.roomRuntime()->isClosing()) return;
    const auto facts=request.worldView.strategyFacts.value("hegemony").toObject();
    if (!facts.value("enabled").toBool() || !facts.value("original_overrides_loaded").toBool()) return;
    const auto players=facts.value("players").toObject();
    const auto who=players.value(judge->who->objectName()).toObject();
    if (!who.value("shown_skills").isObject()) return;
    const bool hongyan=who.value("shown_skills").toObject().contains("hongyan");
    // Match the existing judgment projector's privacy boundary. Zone/private
    // predicates for a foreign judged player are not public face predicates.
    bool publicPattern=!judge->pattern.contains('$');
    for (const auto &branch:judge->pattern.split('#')) {
        const auto factors=branch.split('|');
        if (factors.size()>=4 && factors.at(3)!=".") publicPattern=false;
    }
    QList<AICardView> disclosed=request.worldView.handCards;
    disclosed.append(request.worldView.self.equips);
    for (const auto &pile:request.worldView.self.piles)
        if (pile.handPile) disclosed.append(pile.cards);
    disclosed.append(request.choiceOptions.cards);
    QJsonObject rows; QSet<int> seen;
    for (const auto &view:disclosed) {
        const int id=view.effectiveId;
        if (id<0 || seen.contains(id) || view.virtualCard) continue;
        seen.insert(id);
        const Card *current=room.getCard(id);
        if (!current || current->isVirtualCard() || current->getEffectiveId()!=id
            || current->objectName()!=view.objectName || current->getClassName()!=view.className
            || int(current->getSuit())!=view.suit || current->getNumber()!=view.number) continue;
        const Card *candidate=Sanguosha->getEngineCard(id);
        if (!candidate) continue;
        std::unique_ptr<Card> clone;
        if (hongyan && candidate->getSuit()==Card::Spade) {
            clone.reset(Sanguosha->cloneCard(candidate->objectName(),Card::Heart,current->getNumber()));
            if (!clone) continue;
            candidate=clone.get();
        }
        auto face=publicCard(candidate);
        face.insert("evaluation_only",true);
        // No copied live private flags or authority. The printed/fresh face is
        // for source classification only; selection still returns original id.
        face.insert("flags_complete",false);
        face.insert("skill_names_complete",false);
        QJsonObject row{{"card",face},{"hongyan",hongyan && bool(clone)}};
        if (judge->who==viewer || publicPattern) row.insert("good",judge->isGood(candidate));
        rows.insert(QString::number(id),row);
    }
    result.insert("original_retrial_candidates",rows);
}
