#include "ai-strategy-round33-planning.h"
#include "ai-strategy-card-identity.h"
#include "card.h"
#include "card-lifetime-manager.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "room-state.h"
#include "serverplayer.h"
#include "skill.h"
#include "lua-runtime.h"
#include <QPointer>

namespace {
constexpr const char *skillName = "sfofl_zhaobing";
constexpr const char *pileName = "sfofl_zhoubing";
bool fail(QString *error, const char *why) { if (error) *error=QString::fromLatin1(why);return false; }
bool liveProduct(Room &room, ServerPlayer *viewer, const SkillInstanceRef &activation,
    const SkillInstanceRef &source, const QList<int> &ids, const Card *card)
{
    if (!viewer || viewer->getRoom()!=&room || !viewer->isAlive()
        || viewer->getPhase()!=Player::Play || room.getCurrent()!=viewer
        || !activation.isValid() || activation.ownerObjectName!=viewer->objectName()
        || activation.key.skillName!=skillName || source!=activation
        || !viewer->hasSkill(skillName)
        || !viewer->hasSkillInstance(skillName,activation.key.instanceID)
        || viewer->isSkillInvalid(skillName,activation.key.instanceID)
        || room.resolveSkillInstanceRootRef(activation)!=source || ids.size()!=1 || ids.first()<0)
        return false;
    const int id=ids.first();
    return card && card==Sanguosha->getCard(id) && !card->isVirtualCard()
        && card->getEffectiveId()==id && card->getSubcards()==ids
        && card->isKindOf("EquipCard") && card->appliedPhysicalEffectSource().isEmpty()
        && viewer->getPile(pileName).contains(id)
        && room.getCardOwner(id)==viewer && room.getCardPlace(id)==Player::PlaceSpecial;
}
bool sameFace(const AICardView &face, Room &room, ServerPlayer *viewer,const Card *card)
{
    return card && face.cardId==card->getId() && face.effectiveId==card->getEffectiveId()
        && !face.virtualCard && face.typeId==Card::TypeEquip
        && face.objectName==card->objectName() && face.className==card->getClassName()
        && face.suit==int(card->getSuit()) && face.number==card->getNumber()
        && face.skillName==card->getSkillName(false)
        && !face.legacyIdentityToken.isEmpty()
        && face.legacyIdentityToken==aiLegacyCardIdentity(room,viewer,card);
}
const AICardView *snapshotFace(const AIRequest &request,int id)
{
    for (const auto &pile:request.worldView.self.piles) if (pile.name==pileName) {
        if (!pile.open || !pile.cardIds.contains(id)) return nullptr;
        for (const auto &face:pile.cards) if (face.cardId==id) return &face;
    }
    return nullptr;
}
}

bool aiRound33PlanningPhysicalScope(const AIRequest &request,const QString &skill)
{
    return skill==skillName && request.kind==AIRequest::Activate
        && request.reason==CardUseStruct::CARD_USE_REASON_PLAY && request.pattern.isEmpty()
        && request.handlingMethod==Card::MethodUse;
}
bool authorizeAIRound33PlanningPhysicalProduct(const AIRequest &request,Room &room,
    ServerPlayer *viewer,const SkillInstanceRef &activation,const SkillInstanceRef &source,
    const QList<int> &selected,const Card *produced,QString *error)
{
    if (!aiRound33PlanningPhysicalScope(request,activation.key.skillName)
        || request.decisionId==0 || !viewer || request.viewerObjectName!=viewer->objectName()
        || request.worldView.self.objectName!=viewer->objectName()
        || request.stateRevision!=request.worldView.revision
        || request.stateRevision!=room.roomRuntime()->stateRevision())
        return fail(error,"sfofl physical product requires its current owner Play request");
    if (!liveProduct(room,viewer,activation,source,selected,produced))
        return fail(error,"sfofl physical Equip product left its owned skill/pile/selection domain");
    const auto *face=snapshotFace(request,selected.first());
    if (!face || !sameFace(*face,room,viewer,produced))
        return fail(error,"sfofl physical Equip product differs from the disclosed owner snapshot");
    return true;
}

class AIRound33PhysicalUseProof final {
public:
    QPointer<Room> room;
    QPointer<ServerPlayer> actor;
    QPointer<const Card> card;
    quint64 revision=0;
    SkillInstanceRef activation,source;
    QList<int> selected;
    QStringList targets;
    AICardView face;
};

bool AIRound33PhysicalUseAccess::attach(const AIRequest &request,Room &room,
    ServerPlayer *viewer,const AICardConversionView &ticket,CardUseStruct &use)
{
    if (!authorizeAIRound33PlanningPhysicalProduct(request,room,viewer,
            ticket.activationRef,ticket.sourceRef,ticket.subcardIds,use.card)
        || use.from!=viewer || !use.hasSkillActivationRequest
        || use.bypass_cost || use.skipSkillEffect || use.skillExecutionID!=0
        || use.activationRef!=ticket.activationRef || use.sourceRef!=ticket.sourceRef
        || ticket.producerVersion!=2 || ticket.producerReason!=request.reason
        || !ticket.producerPattern.isEmpty() || ticket.name!=use.card->objectName()
        || ticket.className!=use.card->getClassName() || ticket.suit!=int(use.card->getSuit())
        || ticket.number!=use.card->getNumber()
        || ticket.legacyIdentityToken!=aiLegacyCardIdentity(room,viewer,use.card)) return false;
    auto proof=std::make_shared<AIRound33PhysicalUseProof>();
    proof->room=&room;proof->actor=viewer;proof->card=use.card;proof->revision=request.stateRevision;
    proof->activation=ticket.activationRef;proof->source=ticket.sourceRef;proof->selected=ticket.subcardIds;
    proof->face=*snapshotFace(request,ticket.subcardIds.first());
    for (const auto *target:use.to) {
        if (!target || target->getRoom()!=&room) return false;
        proof->targets<<target->objectName();
    }
    use.m_aiRound33PhysicalUse=std::move(proof);
    return true;
}

int AIRound33PhysicalUseAccess::validate(Room &room,CardUseStruct &use)
{
    const auto proof=use.m_aiRound33PhysicalUse;
    if (!proof) return -1;
    auto *viewer=proof->actor.data();
    if (proof->room!=&room || !viewer || proof->card.isNull() || proof->card!=use.card
        || use.from!=viewer || !use.hasSkillActivationRequest
        || use.bypass_cost || use.skipSkillEffect || use.skillExecutionID!=0
        || use.activationRef!=proof->activation || use.sourceRef!=proof->source
        || room.roomRuntime()->stateRevision()!=proof->revision
        || room.roomState()->getCurrentCardUseReason()!=CardUseStruct::CARD_USE_REASON_PLAY
        || !room.roomState()->getCurrentCardUsePattern().isEmpty()) return 0;
    QStringList targets;
    for (const auto *target:use.to) {
        if (!target || target->getRoom()!=&room) return 0;
        targets<<target->objectName();
    }
    if (targets!=proof->targets) return 0;
    EngineRuntimeContextScope engine(*Sanguosha,&room);
    LuaRuntime::Binding game(room.roomRuntime()->lua(),false);
    if (!liveProduct(room,viewer,proof->activation,proof->source,proof->selected,use.card)
        || !sameFace(proof->face,room,viewer,use.card)) return 0;
    const auto *skill=dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill(skillName));
    if (!skill || skill->isEquipSkill() || room.isAcceptedViewAsEffect(proof->activation)) return 0;
    ActiveSkillRequest request;
    request.initiator=viewer;request.reason=CardUseStruct::CARD_USE_REASON_PLAY;
    request.activationRef=proof->activation;request.sourceRef=proof->source;
    request.selectedCardIds=proof->selected;request.selectedTargetNames=targets;
    const auto stable = [&]() {
        return proof->room==&room && proof->actor==viewer && !proof->card.isNull()
            && proof->card==use.card && room.roomRuntime()->stateRevision()==proof->revision;
    };
    // Same applicable native admission order as Room::resolveActiveSkillRequest.
    // This fixed producer has no declaration/custom cost/pay/on_effect hooks.
    if (!request.generatedCardSelectionValid() || !skill->canActivateRequest(request)
        || !skill->cardSelectionFeasible(request)) return 0;
    if (!stable()) return 0;
    SkillContext context;context.invoker=viewer;context.owner=viewer;context.initiator=viewer;
    context.activationRef=proof->activation;context.instanceID=proof->activation.key.instanceID;
    if (!skill->isUsable(context) || !stable()) return 0;
    const auto expanded=skill->getExpandPileCardIds(viewer);
    if (!stable() || !expanded.contains(proof->selected.first())) return 0;
    ActiveSkillRequest prefix=request;prefix.selectedCardIds.clear();
    if (!skill->canSelectCard(prefix,use.card) || !stable()) return 0;
    prefix.selectedCardIds=proof->selected;
    const Card *produced=skill->createCard(prefix);
    const bool producedLive=produced && globalCardLifetimeManager().isLive(produced);
    const bool exact=stable() && producedLive && produced==use.card && !produced->isVirtualCard()
        && request.generatedCardProductMatches(produced)
        && sameFace(proof->face,room,viewer,produced)
        && liveProduct(room,viewer,proof->activation,proof->source,proof->selected,produced);
    if (producedLive && produced!=use.card && produced->isVirtualCard()) const_cast<Card *>(produced)->deleteLater();
    // No gameplay state/skill attribution/card ownership writes. After this
    // entry gate, Room's existing target/limit/validate/onUse/movement runs.
    return exact?1:0;
}
