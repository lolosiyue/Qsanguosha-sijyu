#include "ai-strategy-response-catalog.h"
#include "ai.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonArray>
namespace {
QJsonObject unavailable(const QString &reason) { return {{"error", reason}, {"unsupported", true}}; }
}
QJsonObject resolveAIResponseCatalogue(Room &room, ServerPlayer *viewer,
                                     const AIRequest &request, const QJsonObject &query)
{
    if (!viewer || viewer->getRoom() != &room || request.viewerObjectName != viewer->objectName()
        || request.worldView.self.objectName != viewer->objectName()
        || request.stateRevision != room.roomRuntime()->stateRevision())
        return unavailable("public catalog request is stale or belongs to another viewer");
    if (query.size() != 2 || query.value("kind").toString() != "response_catalogue"
        || !query.value("key").isString()) return unavailable("invalid public catalog query");
    const QString key = query.value("key").toString();
    const bool basic = key == "ol_sp_basic_names";
    const bool trick = key == "ol_sp_ndtrick_names";
    const bool random = key == "rushB_xiongsuan";
    const bool qinyi = key == "ov_qinyi_names";
    if (!((basic && request.kind == AIRequest::Activate && viewer->hasSkill("huanpei"))
        || (trick && request.kind == AIRequest::Activate && viewer->hasSkill("oljinjin"))
        || (random && request.kind == AIRequest::Choice && request.choiceOptions.reason == "rushB_xiongsuan")
        || (qinyi && request.kind == AIRequest::SkillInvoke && request.choiceOptions.reason == "ov_qinyi"
            && viewer->hasSkill("ov_qinyi"))))
        return unavailable("public catalog source is outside its audited question");
    if (!request.conversionLedger || request.conversionLedger->queryCount >= 128)
        return unavailable("public catalog query budget exhausted");
    ++request.conversionLedger->queryCount;
    QJsonObject result{{"known", true}, {"key", key}};
    if (basic || trick || qinyi) {
        // Engine::getCardNames intentionally calls getRandomCards before its
        // index-order scan. Do not optimize away that source RNG consumption.
        result.insert("names", QJsonArray::fromStringList(Sanguosha->getCardNames(
            qinyi ? "BasicCard+TrickCard+^DelayedTrick" : basic ? "BasicCard" : "TrickCard+^DelayedTrick")));
    } else {
        QJsonArray ids; QJsonObject cards;
        for (int id : Sanguosha->getRandomCards()) {
            ids.append(id);
            const Card *card = Sanguosha->getEngineCard(id);
            if (!card) return unavailable("public catalog contains an absent printed card");
            // Immutable printed identities, never Room::getCard wrappers.
            QJsonArray kinds;
            for (const char *name : {"BasicCard", "TrickCard", "EquipCard", "DelayedTrick"})
                if (card->isKindOf(name)) kinds.append(name);
            kinds.append(card->getClassName());
            cards.insert(QString::number(id), QJsonObject{{"id", id}, {"effective_id", id},
                {"name", card->objectName()}, {"class_name", card->getClassName()},
                {"type_id", int(card->getTypeId())}, {"suit", int(card->getSuit())},
                {"number", card->getNumber()}, {"kind_of", kinds}, {"virtual_card", false},
                {"subcards", QJsonArray()}, {"skill_name", card->getSkillName(false)},
                {"red", card->isRed()}, {"black", card->isBlack()}});
        }
        result.insert("ids", ids); result.insert("cards", cards);
    }
    return result;
}
