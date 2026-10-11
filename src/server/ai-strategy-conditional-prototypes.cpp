#include "ai-strategy-conditional-prototypes.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"

namespace {
bool currentOwner(const AIRequest &request, const ServerPlayer *viewer)
{
    return viewer && viewer->getRoom() && !request.viewerObjectName.isEmpty()
        && request.viewerObjectName == viewer->objectName()
        && request.stateRevision == viewer->getRoom()->roomRuntime()->stateRevision();
}
bool play(const AIRequest &request)
{
    return request.kind == AIRequest::Activate
        && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
}
bool response(const AIRequest &request, const QString &pattern)
{
    return (request.kind == AIRequest::UseCard || request.kind == AIRequest::RespondCard)
        && (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
            || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
        && request.pattern == pattern;
}
bool defaultFace(int suit, int number, const QList<int> &costs)
{
    return suit == int(Card::NoSuit) && number == 0 && costs.isEmpty();
}
const Card *singleOwnerMaterial(const QList<int> &costs, const ServerPlayer *viewer,
                                const QList<int> &zone)
{
    if (costs.size() != 1 || costs.first() < 0 || !zone.contains(costs.first())
        || viewer->getRoom()->getCardOwner(costs.first()) != viewer) return nullptr;
    return Sanguosha->getCard(costs.first());
}
}

int aiAuditedConditionalSourcePrototype(const AIRequest &request, const QString &skill,
                                       const QString &prototype, const ServerPlayer *viewer)
{
    if (!currentOwner(request, viewer)) return -1;
    // Fixed tenyear2.cpp Zunwei::viewAs returns new ZunweiCard with zero
    // materials. This only names the readonly/selected prototype; existing
    // native activation, instance, factory and final target gates still run.
    if (skill == "zunwei" && prototype == "ZunweiCard")
        return play(request) ? 0 : -1;
    // Fixed sijyuoffline.lua18018/18040-18060 and source AI4885: only
    // this first response question constructs the zero-material SkillCard.
    // The second @@sfofl_zhengan_basic question creates an ordinary BasicCard.
    if (skill == "sfofl_zhengan_basic" && prototype == "#sfofl_zhengan_basicCard")
        return request.kind == AIRequest::UseCard
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            && request.pattern == "@@sfofl_zhengan"
            && request.handlingMethod == Card::MethodUse ? 0 : -1;
    // Fixed gaoda.lua5791/6238. Their response products are ordinary cards.
    // Fixed AI1579/1768 literally uses %d: ListS2I skips this invalid integer,
    // so LuaSkillCard::Parse produces zero subcards, not ID 0 and not an error.
    if (skill == "ntdtwo" && prototype == "#ntdtwo")
        return play(request) && request.pattern != "@@ntdtwo" && viewer->getMark("@NTD2") > 0 ? 0 : -1;
    if (skill == "ntdthree" && prototype == "#ntdthree")
        return play(request) && request.pattern != "@@ntdthree" && viewer->getMark("@NTD3") > 0 ? 0 : -1;
    // gaoda.lua3441: PLAY uses one non-Nullification Trick outside lizhan.
    // The exact chosen card is checked by the existing native payment pipeline.
    if (skill == "luashipo" && prototype == "#luashipo")
        return play(request) && viewer->getPile("lizhan").size() < 3 ? 1 : -1;
    // gaoda.lua7229: the owner property chooses SkillCard versus normal card.
    if (skill == "haidao" && prototype == "#haidaocard")
        return response(request, "@@haidao") && viewer->property("haidao").toString() == "armor" ? 0 : -1;
    // gaoda.lua7884 and AI2558-2600: only marks 2/3 enter the source turn fill.
    // Source mark-3 fill is an empty seed; its use function later supplies two
    // ordered hand costs. Never reinterpret mark-1's Slash as this SkillCard.
    if (skill == "aoyi" && prototype == "#aoyi" && play(request) && !viewer->hasUsed("#aoyi")) {
        const int mark = viewer->getMark("@aoyi");
        if (mark == 2) return 0;
        if (mark == 3 && viewer->getHandcardNum() >= 2) return 1;
    }
    return -1;
}

bool aiAuditedConditionalOrdinaryProduct(const AIRequest &request, const QString &sourceSkill,
                                        const QString &printedSkill, const QString &name,
                                        int suit, int number, const QList<int> &costs,
                                        const ServerPlayer *viewer)
{
    if (!currentOwner(request, viewer)) return false;
    if ((sourceSkill == "ntdtwo" || sourceSkill == "ntdthree")
        && printedSkill == sourceSkill && name == "dismantlement")
        return response(request, "@@" + sourceSkill) && defaultFace(suit, number, costs);
    if (sourceSkill == "haidao" && printedSkill == "haidaocard"
        && response(request, "@@haidao") && defaultFace(suit, number, costs)) {
        // These are the only ordinary products emitted by AI2258-2337.
        return (name == "slash" || name == "iron_chain" || name == "shoot")
            && viewer->property("haidao").toString() == name;
    }
    if (sourceSkill == "luashipo" && printedSkill == "luashipo" && name == "nullification"
        && response(request, "nullification")) {
        const Card *material = singleOwnerMaterial(costs, viewer, viewer->getPile("lizhan"));
        return material && suit == int(material->getSuit()) && number == material->getNumber();
    }
    if (sourceSkill == "aoyi" && printedSkill == "aoyi" && name == "slash"
        && response(request, "@@aoyi") && viewer->getMark("@aoyi") == 1) {
        // Native permits non-equipped cards; fixed AI2528 selects only hand.
        const Card *material = singleOwnerMaterial(costs, viewer, viewer->handCards());
        return material && material->isRed()
            && suit == int(material->getSuit()) && number == material->getNumber();
    }
    return false;
}

bool aiAuditedConditionalOrdinaryPreview(const AIRequest &request, const QString &sourceSkill,
                                        const QString &printedSkill, const QString &name,
                                        int suit, int number, const QList<int> &costs,
                                        const ServerPlayer *viewer)
{
    if (aiAuditedConditionalOrdinaryProduct(request, sourceSkill, printedSkill, name,
                                          suit, number, costs, viewer)) return true;
    // AI2534 probes targets before choosing a material. No native product or
    // mark check occurs at that source point: preserve this hypothesis even if
    // the later selected proposal is rejected by native mark/cost validation.
    return currentOwner(request, viewer) && response(request, "@@aoyi")
        && sourceSkill == "aoyi" && printedSkill.isEmpty() && name == "slash"
        && suit == int(Card::NoSuitRed) && number == 0 && costs.isEmpty();
}
