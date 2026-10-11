#include "ai-strategy-round22-olclan-observations.h"
#include "ai-strategy-projection.h"
#include "ai.h"
#include "card.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "structs.h"
#include <QJsonArray>

void projectAIStrategyRound22OlClanObservations(AIRequest &request, Room &room,
    ServerPlayer *viewer, const QVariant &data, QJsonObject &context,
    const std::function<QJsonObject(const Card *)> &publicCard)
{
    if (!viewer || viewer->getRoom()!=&room || request.viewerObjectName!=viewer->objectName()
        || request.worldView.self.objectName!=viewer->objectName()) return;
    auto rows=context.value("olclan_observations").toObject();
    const QString reason=request.choiceOptions.reason;
    const auto question=[&](AIRequest::DecisionKind kind,const QString &key) {
        return request.kind==kind && (reason==key || reason==key+"$-1");
    };
    const auto roster=room.getAllPlayers(true);
    const auto playerTag=[&](const QString &key) {
        const QVariant value=viewer->getTag(key);
        const auto *p=value.value<ServerPlayer *>();
        QJsonObject row{{"known",!p || roster.contains(const_cast<ServerPlayer *>(p))},
            {"present",value.isValid()}};
        if (!p) row.insert("value",QJsonValue(QJsonValue::Null));
        else if (roster.contains(const_cast<ServerPlayer *>(p))) row.insert("value",p->objectName());
        rows.insert(key,row);
    };
    const auto useTag=[&](const QString &key,const QVariant &value) {
        QJsonObject row{{"known",true},{"present",value.isValid()}};
        const CardUseStruct use=value.value<CardUseStruct>();
        QJsonObject result;QJsonArray targets;
        // All three audited writers use this actor's current public CardUsed /
        // TargetSpecifying data. A foreign/stale use cannot borrow the question.
        if (use.card && (use.from!=viewer || !publicCard)) row.insert("known",false);
        else {
            bool valid=true;
            for (auto *p:use.to) {
                if (!p || !roster.contains(p)) {valid=false;break;}
                targets.append(p->objectName());
            }
            if (!valid) row.insert("known",false);
            else {
                result.insert("from",use.from ? QJsonValue(use.from->objectName()):QJsonValue(QJsonValue::Null));
                result.insert("to",targets);
                if (use.card) {
                    auto face=publicCard(use.card);
                    if (face.isEmpty()) row.insert("known",false);
                    else {
                        // E's only cost query is subcardsLength. Count and
                        // virtuality suffice; the public descriptor deliberately
                        // does not disclose underlying virtual payment IDs.
                        face.insert("virtual_card",use.card->isVirtualCard());
                        face.insert("subcard_count",use.card->subcardsLength());
                        result.insert("card",face);
                    }
                }
                row.insert("value",result);
            }
        }
        rows.insert(key,row);
    };
    if (question(AIRequest::PlayerChosen,"ny_sankuang"))
        useTag("ny_sankuang_use",viewer->getTag("ny_sankuang_use")); // olClan.lua1639–1641
    if (question(AIRequest::PlayersChosen,"zuheta"))
        useTag("zuheta_data",viewer->getTag("zuheta_data")); // 3308–3309
    if (question(AIRequest::SkillInvoke,"kezumingjie")) {
        // Only the actual 3641 string recipient. No newer/private tag substitute.
        const QString input=data.toString();
        if (input.startsWith("kezumingjie0:") && input.split(':').size()==2) {
            bool found=false;
            for (auto *p:roster) if (p->objectName()==input.section(':',1,1)) found=true;
            if (found) { rows.insert("invoke_string",QJsonObject{{"known",true},{"value",input}});
                useTag("kezumingjieData",room.getTag("kezumingjieData")); }
        }
    }
    if (question(AIRequest::SkillInvoke,"kezushangshen")) playerTag("fenchaiPlayer"); // own tag1959/1976
    if (question(AIRequest::Discard,"kezulianhe")) playerTag("kezulianheFrom"); // Exchange3103–3104
    if (question(AIRequest::PlayerChosen,"kezufangzhen"))
        rows.insert("TurnLengthCount",QJsonObject{{"known",true},{"value",room.getTag("TurnLengthCount").toInt()}});
    // Room::notifyMoveToPile writes each ForAI list and viewer-only disclosure.
    // Preserve its order and duplicates. A tag alone is NOT permission to resolve
    // Engine::getCard(id); missing disclosed face leaves the row unknown.
    const auto pile=[&](const QString &name) {
        const QVariant value=viewer->getTag(name+"ForAI");
        // Room stores the ledger under the fake-pile name (#reason),
        // while the owner tag remains reason+ForAI.
        const auto disclosed=readAIStrategyNotifiedPile(room,viewer,"#"+name).value("cards").toObject();
        QJsonArray faces;bool known=true;
        for (const QVariant &raw:value.toList()) {
            const QString id=QString::number(raw.toInt());
            if (!disclosed.value(id).isObject()) {known=false;break;}
            faces.append(disclosed.value(id));
        }
        rows.insert(name+"ForAI",QJsonObject{{"known",known},{"present",value.isValid()},{"value",faces}});
    };
    if (request.kind==AIRequest::UseCard && request.pattern=="@@zujieli") {pile("zujieli");pile("zujieliN");}
    if (request.kind==AIRequest::UseCard && request.pattern=="@@zugezhi") pile("zugezhi");
    if (!rows.isEmpty()) context.insert("olclan_observations",rows);
}

int aiRound22OlClanObservationPrototype(const AIRequest &request, const ServerPlayer *viewer,
    const QString &skill, const QString &prototype)
{
    if (!viewer || !viewer->getRoom() || request.viewerObjectName!=viewer->objectName()
        || request.stateRevision!=viewer->getRoom()->roomRuntime()->stateRevision()
        || request.kind!=AIRequest::UseCard
        || request.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE
        || !viewer->hasSkill(skill)) return -1;
    // Actual trigger recipients own each view-as skill (olClan.lua710/6555).
    // Only these exact questions, not the native VS's broader prefix allowance.
    if (skill=="zujieli" && prototype=="#zujieliCard" && request.pattern=="@@zujieli") return 1;
    if (skill=="zugezhi" && prototype=="#zugezhiCard" && request.pattern=="@@zugezhi") return 1;
    return -1;
}
