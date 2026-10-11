#include "ai-strategy-round44-choice.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "card.h"
namespace {
bool current(const AIRequest &r,const ServerPlayer *p) {
    return p && p->isAlive() && p->getRoom() && r.decisionId
        && r.viewerObjectName==p->objectName() && r.worldView.self.objectName==p->objectName()
        && r.stateRevision==p->getRoom()->roomRuntime()->stateRevision()
        && r.kind==AIRequest::Activate && r.reason==CardUseStruct::CARD_USE_REASON_PLAY
        && p->getPhase()==Player::Play && p->hasSkill("skin");
}
bool actualSource(const AIRequest &r,const ServerPlayer *p,const SkillInstanceRef &a,const SkillInstanceRef &s) {
    if(!current(r,p) || !a.isValid() || !s.isValid() || a.ownerObjectName!=p->objectName()
        || a.key.skillName!="skin" || !p->hasSkillInstance(a.key.skillName,a.key.instanceID)
        || p->isSkillInvalid(a.key.skillName,a.key.instanceID))return false;
    QList<AiSkillActionContext> all=r.skillActions;
    if(r.hasSkillActionContext)all.prepend(r.skillActionContext);
    for(const auto &row:all)if(row.isValid()&&row.activationRef==a&&row.sourceRef==s)return true;
    return false;
}
}
// ai/gaoda-ai.lua126-143: source parses #skin:.:, never a declaration.
// Native scope admission is independent of source hasUsed/RNG branch order.
int aiRound44SkinPrototype(const AIRequest &r,const ServerPlayer *p,const QString &skill,const QString &prototype) {
    if(skill!="skin"||prototype!="#skin"||!current(r,p))return -1;
    QList<AiSkillActionContext> all=r.skillActions;
    if(r.hasSkillActionContext)all.prepend(r.skillActionContext);
    for(const auto &a:all)if(actualSource(r,p,a.activationRef,a.sourceRef))return 0;
    return -1;
}
// gaoda.lua1496-1512: only the actual native V2 producer may generate the
// declaration. This function reads no persisted Skin data and exports none.
bool aiRound44SkinProduct(const AIRequest &r,const ServerPlayer *p,const SkillInstanceRef &a,
                         const SkillInstanceRef &s,const Card *produced,const Card *policy) {
    const auto *actual=dynamic_cast<const SkillCard *>(produced);
    const auto *source=dynamic_cast<const SkillCard *>(policy);
    return actualSource(r,p,a,s) && actual && source && source->getUserString().isEmpty()
        && actual->objectName()=="skin" && source->objectName()=="skin"
        && actual->getClassName()==source->getClassName()
        && actual->isVirtualCard() && source->isVirtualCard()
        && actual->getSubcards().isEmpty() && source->getSubcards().isEmpty()
        && actual->getSuit()==source->getSuit() && actual->getNumber()==source->getNumber()
        && actual->getSkillName(false)==source->getSkillName(false)
        && actual->targetFixed()==source->targetFixed() && actual->willThrow()==source->willThrow()
        && actual->getHandlingMethod()==source->getHandlingMethod() && actual->canRecast()==source->canRecast()
        && actual->getFlags()==source->getFlags();
}
