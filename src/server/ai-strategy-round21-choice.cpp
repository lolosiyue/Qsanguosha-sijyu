#include "ai-strategy-round21-choice.h"
#include "ai.h"
#include "engine.h"
#include "maneuvering.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"

int aiRound21ChoicePrototype(const AIRequest &request, const ServerPlayer *viewer,
                             const QString &skill, const QString &prototype)
{
    if (!viewer || !viewer->getRoom() || request.viewerObjectName != viewer->objectName()
        || request.stateRevision != viewer->getRoom()->roomRuntime()->stateRevision()
        || !viewer->hasSkill(skill)) return -1;
    const bool play = request.kind == AIRequest::Activate
        && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    // b6025 ai/meow-ai.lua99-110; extensions/meow.lua177/253. Source seed
    // and selected product have zero materials; targets are chosen by usefunc.
    if (skill == QLatin1String("Meowlijian") && prototype == QLatin1String("#MeowlijianCard"))
        return play ? 0 : -1;
    // b6025 ai/keguibao-ai.lua252-300; extensions/keguibao.lua624/641.
    // Empty readonly seed is allowed. Nonempty selection feasibility, ordered
    // owner materials and target count are still checked by the real V2 rule.
    if (skill == QLatin1String("keguitiqi") && prototype == QLatin1String("#keguitiqiCard"))
        return play ? 1 : -1;
    // Exact live response-use inquiries; these masks permit the donor single
    // material proposal, never arbitrary skill/prototype aliases.
    if (request.kind == AIRequest::UseCard
        && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        if (skill == "MeowTianxiang" && prototype == "#MeowTianxiangCard"
            && request.pattern == "@@MeowTianxiang" && request.handlingMethod == Card::MethodDiscard) return 1;
        if ((skill == "kexiantianbian" || skill == "kejiexiantianbian")
            && prototype == "#" + skill + "_Card" && request.pattern == "@" + skill) return 1;
    }
    // b6025 ai/inovation-ai.lua1119-1186; inovation.cpp3900/3986. Source
    // declares four response Basic names and Tacos in Play; it pays no cards.
    // Do not repair its free-global `to_use` bug or bypass the native judge.
    if (skill == QLatin1String("inovation_fengzhu")
        && prototype == QLatin1String("InovationFengzhuCard")) {
        const bool response = (request.kind == AIRequest::RespondCard || request.kind == AIRequest::UseCard)
            && (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
            && !request.pattern.startsWith(QLatin1String("@@"));
        return play || response ? 2 : -1;
    }
    return -1;
}

bool aiRound21ChoiceAnalepticAvailable(const ServerPlayer *viewer)
{
    return viewer && Analeptic::IsAvailable(viewer, nullptr);
}

namespace {
bool sameKnownFace(const AICardView &row, const Card *card)
{
    return row.effectiveId == card->getEffectiveId() && row.objectName == card->objectName()
        && row.className == card->getClassName() && row.suit == int(card->getSuit())
        && row.number == card->getNumber();
}
bool lawfulPindianFace(const AIRequest &request, Room &room, const Card *card)
{
    if (!card || card->isVirtualCard() || card->getEffectiveId() < 0) return false;
    const int id=card->getEffectiveId();
    const Card *live=room.getCard(id);
    if (!live || live->objectName()!=card->objectName() || live->getClassName()!=card->getClassName()
        || live->getSuit()!=card->getSuit() || live->getNumber()!=card->getNumber()) return false;
    // serverplayer.cpp1098-1112 sends $PindianResult and a visible atomic
    // move to PlaceTable BEFORE finishPindian triggers PindianVerifying1137.
    // Live public zone is independent proof; the tag/prompt alone is not.
    const auto place=room.getCardPlace(id);
    if (place==Player::PlaceTable || place==Player::DiscardPile) return true;
    for (const auto &known:request.worldView.handCards) if (sameKnownFace(known,card)) return true;
    for (const auto &player:request.worldView.players)
        for (const auto &known:player.knownCards) if (sameKnownFace(known,card)) return true;
    return false;
}
bool rosterPlayer(Room &room, const ServerPlayer *player)
{
    return !player || room.findPlayerByObjectName(player->objectName(),true)==player;
}
}

void projectAIRound21ChoiceContext(AIRequest &request, Room &room, ServerPlayer *viewer,
    QJsonObject &context, const std::function<QJsonObject(const Card *)> &publicCard)
{
    if (!viewer || viewer->getRoom()!=&room || request.viewerObjectName!=viewer->objectName()
        || request.stateRevision!=room.roomRuntime()->stateRevision() || !publicCard
        || request.kind!=AIRequest::UseCard
        || request.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE) return;
    QJsonObject row=context.value("round21_choice").toObject();
    if (request.pattern=="@@MeowTianxiang" && request.prompt=="@tianxiang-card"
        && request.handlingMethod==Card::MethodDiscard && viewer->hasSkill("MeowTianxiang")) {
        // extensions/meow.lua1355 stores this owner's DamageInflicted data;
        // 1356 opens exactly this request. No other owner's tag is accessed.
        const QVariant value=viewer->getTag("MeowTianxiangDamage");
        if (!value.isValid() || value.isNull()) row.insert("meow_tianxiang_damage",false);
        else if (value.canConvert<DamageStruct>()) {
            const auto damage=value.value<DamageStruct>();
            if (rosterPlayer(room,damage.from) && rosterPlayer(room,damage.to)) {
                row.insert("meow_tianxiang_damage",QJsonObject{
                    {"from",damage.from?QJsonValue(damage.from->objectName()):QJsonValue(false)},
                    {"to",damage.to?QJsonValue(damage.to->objectName()):QJsonValue(false)},
                    {"amount",damage.damage},{"nature",int(damage.nature)},
                    {"chain",damage.chain},{"transfer",damage.transfer},
                    {"card",damage.card?QJsonValue(publicCard(damage.card)):QJsonValue(false)}});
            }
        }
    }
    const QString skill=request.pattern=="@kexiantianbian"?"kexiantianbian"
        :request.pattern=="@kejiexiantianbian"?"kejiexiantianbian":QString();
    if (!skill.isEmpty() && viewer->hasSkill(skill)
        && request.prompt.startsWith("@kexiantianbian_Pindian::"+skill+":")) {
        // Both fixed extension on_cost writers use public room:setPlayerFlag;
        // preserve the full getAllPlayers loop and last matching player in Lua.
        QJsonObject flags;
        for (const auto *p:room.getAllPlayers(true)) flags.insert(p->objectName(),QJsonObject{
            {"Source",p->hasFlag(skill+"_Source")},{"Target",p->hasFlag(skill+"_Target")}});
        row.insert("tianbian_skill",skill);row.insert("tianbian_flags",flags);
        const QVariant value=room.getTag("CurrentPindianStruct");
        if (!value.isValid() || value.isNull()) row.insert("tianbian_pindian",false);
        else if (value.canConvert<PindianStruct *>()) {
            const auto *p=value.value<PindianStruct *>();
            if (!p) row.insert("tianbian_pindian",false);
            else if (rosterPlayer(room,p->from) && rosterPlayer(room,p->to)
                && lawfulPindianFace(request,room,p->from_card) && lawfulPindianFace(request,room,p->to_card)) {
                row.insert("tianbian_pindian",QJsonObject{
                    {"from_card",publicCard(p->from_card)},{"to_card",publicCard(p->to_card)}});
            }
            // An unproved face leaves the field absent: never pretend no event.
        }
    }
    if (!row.isEmpty()) context.insert("round21_choice",row);
}
