#include "ai-strategy-construction-context.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "serverplayer.h"
#include "structs.h"
#include <QJsonArray>

void projectAIConstructionContext(AIRequest &request, Room &room, const ServerPlayer *viewer,
    const std::function<QJsonObject(const Card *)> &describeCard)
{
    if (!viewer || viewer->getRoom() != &room
        || request.viewerObjectName != viewer->objectName()) return;
    const bool lingfa = request.kind == AIRequest::Discard
        && request.choiceOptions.reason == "MR_lingfa";
    const bool gongxin = request.kind == AIRequest::AmazingGrace
        && request.choiceOptions.reason == "MR_gongxin";
    if (!lingfa && !gongxin) return;
    QJsonObject context = request.choiceOptions.context.value("MRmou").toObject();
    const auto roster = room.getAllPlayers(true);
    if (lingfa) {
        // Fixed MR.lua1048-1050: the already public CardUse is installed only
        // around askForExchange(use.from, MR_lingfa). Never borrow other tags.
        const QVariant tag = room.getTag("MR_lingfa");
        if (!tag.isValid() || tag.isNull()) context.insert("lingfa_use", false);
        else if (tag.canConvert<CardUseStruct>()) {
            const CardUseStruct use = tag.value<CardUseStruct>();
            if (use.from == viewer && use.card && describeCard) {
                QJsonArray targets;
                bool valid = true;
                for (const ServerPlayer *target : use.to) {
                    if (!target || !roster.contains(const_cast<ServerPlayer *>(target))) {
                        valid = false;
                        break;
                    }
                    targets.append(target->objectName());
                }
                const QJsonObject card = describeCard(use.card);
                if (valid && !card.isEmpty()) context.insert("lingfa_use", QJsonObject{
                    {"card", card}, {"from", viewer->objectName()}, {"to", targets}});
            }
        }
        // Invalid/mismatched present data stays missing (unknown), not false.
    }
    if (gongxin) {
        // Source MRmou-ai.lua reads the OWNER tag MR_gongxin. Fixed MR.lua
        // calls doGongxin without its skill_name argument, hence normally asks
        // reason gongxin and writes the different owner tag gongxin. Do not
        // repair that incompatibility by reading the Room tag or aliasing it.
        const QVariant tag = viewer->getTag("MR_gongxin");
        const ServerPlayer *target = tag.value<ServerPlayer *>();
        if (!target) context.insert("gongxin_target", false);
        else if (roster.contains(const_cast<ServerPlayer *>(target)))
            context.insert("gongxin_target", target->objectName());
        QJsonObject printed;
        for (const AICardView &offered : request.choiceOptions.cards) {
            if (offered.effectiveId < 0) continue;
            const Card *card = Sanguosha->getEngineCard(offered.effectiveId);
            if (!card) continue;
            // Immutable public definition of this already disclosed offer;
            // no target hand enumeration, private flags, or conversion tags.
            printed.insert(QString::number(offered.effectiveId), QJsonObject{
                {"id", offered.effectiveId}, {"effective_id", offered.effectiveId},
                {"name", card->objectName()}, {"class_name", card->getClassName()},
                {"subtype", card->getSubtype()},
                {"kind_of", QJsonArray::fromStringList(card->getKindOfNames())},
                {"suit", int(card->getSuit())}, {"number", card->getNumber()},
                {"type_id", int(card->getTypeId())}, {"red", card->isRed()},
                {"black", card->isBlack()},
                {"legacy_string_key", QString::number(offered.effectiveId)},
                {"legacy_sort_key", QString::number(offered.effectiveId)},
                {"virtual_card", false}, {"subcards", QJsonArray()}});
        }
        context.insert("gongxin_engine_cards", printed);
    }
    request.choiceOptions.context.insert("MRmou", context);
}
