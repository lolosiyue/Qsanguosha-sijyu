#include "ai-strategy-round23-choice.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "engine.h"
#include <QJsonArray>

int aiRound23ChoicePrototype(const AIRequest &request, const ServerPlayer *viewer,
                             const QString &skill, const QString &prototype)
{
    if (!viewer || !viewer->getRoom()
        || request.viewerObjectName != viewer->objectName()
        || request.stateRevision != viewer->getRoom()->roomRuntime()->stateRevision()
        || !viewer->hasSkill(skill))
        return -1;
    if (request.kind == AIRequest::UseCard && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
        && skill == "MeowLieren" && prototype == "#MeowLierenCard" && request.pattern == "@@MeowLieren") return 0;
    if (request.kind != AIRequest::Activate || request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return -1;
    if ((skill == "Meowxianzhou" || skill == "kexianfenshen"
         || skill == "kejiexianfenshen" || skill == "kexianjishi") && prototype == "#" + skill + "Card")
        return 1;
    if (skill == "MeowGuose" && (prototype == "#MeowGuoseCard" || prototype == "#MeowGuoseCard2"))
        return 1;
    return -1;
}

// Exactly cloneCard("Indulgence") + addSubcard(id). No skill name is assigned
// by the donor; the generic audited constructor handles the same clone and
// inherited one-material getter semantics. It grants no payment receipt.
bool aiRound23ChoiceConstruction(const AIRequest &request, const ServerPlayer *viewer,
                                 const QJsonObject &spec)
{
    if (spec.value("source_key") == QJsonValue("MeowQingguo")) {
        if (!viewer || !viewer->getRoom() || request.viewerObjectName != viewer->objectName()
            || request.stateRevision != viewer->getRoom()->roomRuntime()->stateRevision()
            || request.kind != AIRequest::Activate || request.reason != CardUseStruct::CARD_USE_REASON_PLAY
            || !viewer->hasSkill("MeowQingguo") || viewer->hasSkill("Meowdoumiao")
            || spec.size()!=6 || spec.value("skill")!=QJsonValue("MeowQingguo")
            || spec.value("name")!=QJsonValue("peach") || !spec.value("subcards").isArray()) return false;
        const auto ids=spec.value("subcards").toArray();
        if (ids.size()!=1 || !ids[0].isDouble() || ids[0].toDouble()!=ids[0].toInt(-1)
            || ids[0].toInt(-1)<0 || viewer->getRoom()->getCardOwner(ids[0].toInt())!=viewer) return false;
        const Card *material=viewer->getRoom()->getCard(ids[0].toInt());
        return material && material->isKindOf("Jink")
            && spec.value("suit")==QJsonValue(int(material->getSuit()))
            && spec.value("number")==QJsonValue(material->getNumber());
    }
    if (aiRound23ChoicePrototype(request, viewer, "MeowGuose", "#MeowGuoseCard2") < 0
        || spec.size() != 6 || spec.value("source_key") != QJsonValue("MeowGuose")
        || spec.value("skill") != QJsonValue("") || spec.value("name") != QJsonValue("indulgence")
        || spec.value("suit") != QJsonValue(int(Card::NoSuit)) || spec.value("number") != QJsonValue(0)
        || !spec.value("subcards").isArray()) return false;
    const auto ids = spec.value("subcards").toArray();
    if (ids.size() != 1 || !ids[0].isDouble() || ids[0].toDouble() != ids[0].toInt(-1)
        || ids[0].toInt(-1) < 0) return false;
    return viewer->getRoom()->getCardOwner(ids[0].toInt()) == viewer;
}

void projectAIRound23ChoiceContext(AIRequest &request, Room &room, ServerPlayer *viewer,
    const QVariant &data, QJsonObject &context,
    const std::function<QJsonObject(const Card *)> &publicCard)
{
    if (!viewer || viewer->getRoom() != &room || request.viewerObjectName != viewer->objectName()
        || request.stateRevision != room.roomRuntime()->stateRevision()) return;
    if (viewer->hasSkill("MeowLieren")
        && ((request.kind == AIRequest::SkillInvoke && request.choiceOptions.reason == "MeowLieren")
            || (request.kind == AIRequest::UseCard && request.pattern == "@@MeowLieren"))) {
        QJsonObject flags;
        for (const auto *player : room.getAllPlayers(true))
            flags.insert(player->objectName(), QJsonObject{{"MeowLierenMarkto",player->hasFlag("MeowLierenMarkto")},
                {"MeowLierenMarkfrom",player->hasFlag("MeowLierenMarkfrom")}});
        auto row=context.value("round23_choice").toObject();row.insert("lieren_flags",flags);
        context.insert("round23_choice",row);
    }
    if (request.kind != AIRequest::Choice || request.choiceOptions.reason != "MeowBeige"
        || !viewer->hasSkill("MeowBeige") || !publicCard || !data.canConvert<DamageStruct>()) return;
    const QVariant tag = room.getTag("CurrentDamageStruct");
    if (!tag.canConvert<DamageStruct>()) return;
    const auto actual = tag.value<DamageStruct>(), asked = data.value<DamageStruct>();
    const auto inRoster = [&](const ServerPlayer *p) {
        return p && room.getAllPlayers(true).contains(const_cast<ServerPlayer *>(p));
    };
    if (!inRoster(actual.to) || (actual.from && !inRoster(actual.from))
        || actual.from != asked.from || actual.to != asked.to || actual.card != asked.card
        || actual.damage != asked.damage || actual.nature != asked.nature
        || !actual.card || !actual.card->isKindOf("Slash")) return;
    // Both the actual Choice data and its scoped CurrentDamage tag identify the
    // already announced Slash damage. No unrelated observer/tag query is added.
    QJsonObject row{{"to", actual.to->objectName()}, {"from", actual.from ? QJsonValue(actual.from->objectName()) : QJsonValue(false)},
        {"card", publicCard(actual.card)}, {"amount", actual.damage}, {"damage", actual.damage},
        {"nature", int(actual.nature)}, {"chain", actual.chain}, {"transfer", actual.transfer},
        {"by_user", actual.by_user}, {"reason", actual.reason}};
    context.insert("damage", row);
}
