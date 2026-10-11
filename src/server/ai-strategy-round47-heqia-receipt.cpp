#include "ai-strategy-round47-heqia.h"
#include "ai-strategy-round41-response.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "skill.h"
#include <QJsonArray>
#include <QWeakPointer>
#include <QSet>
namespace {
struct Receipt {
 QWeakPointer<AIConversionLedger> ledger;quint64 decision=0,revision=0;
 QString viewer,token,wire;bool first=false;QList<int> firstCosts;QStringList baseTargets;QStringList targets;AICardSpec spec;
 SkillInstanceRef activation,source;
};
class Ledger final:public QObject{public:explicit Ledger(Room&r):QObject(&r){}QList<Receipt>receipts;};
Ledger*ledger(Room*r,bool create=false){if(!r)return nullptr;for(auto*c:r->children())if(auto*l=dynamic_cast<Ledger*>(c))return l;return create?new Ledger(*r):nullptr;}
bool sameSpec(const AICardSpec&a,const AICardSpec&b){return a.name==b.name&&a.suit==b.suit&&a.number==b.number
 &&a.skillName==b.skillName&&a.subcardIds==b.subcardIds&&a.conversionId==b.conversionId;}
AICardSpec specOf(const Card *c,int id){AICardSpec s;s.name=c->objectName();s.suit=c->getSuit();s.number=c->getNumber();
 s.skillName=c->getSkillName(false);s.subcardIds=c->getSubcards();s.conversionId=id;return s;}
QJsonObject jsonSpec(const AICardSpec&s){QJsonArray costs;for(int id:s.subcardIds)costs<<id;
 return{{"name",s.name},{"suit",s.suit},{"number",s.number},{"skill",s.skillName},{"subcards",costs},{"conversion_id",s.conversionId}};}

bool current(const AIRequest&r,const ServerPlayer*p){return p&&p->getRoom()&&r.decisionId>0
 &&r.viewerObjectName==p->objectName()&&r.worldView.self.objectName==p->objectName()
 &&r.stateRevision==p->getRoom()->roomRuntime()->stateRevision();}
}
namespace {
bool prepareFirst(const AIRequest&r,ServerPlayer*p,const QList<int>&costs,const QStringList&targets,
 const std::function<bool(CardUseStruct&)>&admit,CardUseStruct&use,QString&error){
 auto fail=[&](const char*m){error=QString::fromLatin1(m);return false;};
 if(!current(r,p)||r.kind!=AIRequest::UseCard||r.pattern!="@@heqia1"
  ||r.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE||r.handlingMethod!=Card::MethodNone)
  return fail("Heqia first selector requires exact native MethodNone inquiry");
 const auto*vs=Sanguosha->getViewAsSkill("heqia");
 if(!vs||dynamic_cast<const ViewAsSkillV2*>(vs)||!vs->isEnabledAtResponse(p,r.pattern))return fail("Heqia first native V1 inquiry unavailable");
 QList<const Card*>materials;QSet<int>seen;
 for(int id:costs){if(seen.contains(id)||p->getRoom()->getCardOwner(id)!=p
   ||(p->getRoom()->getCardPlace(id)!=Player::PlaceHand&&p->getRoom()->getCardPlace(id)!=Player::PlaceEquip))return fail("Heqia first material is not distinct current owner HE");
  seen.insert(id);const auto*c=Sanguosha->getCard(id);if(!c||c->hasFlag("using")||!vs->viewFilter(materials,c))return fail("Heqia first native material filter rejected");materials<<c;}
 const Card*product=vs->viewAs(materials);if(!product)return fail("Heqia first native constructor returned nil");
 std::unique_ptr<const Card>owned(product->isVirtualCard()?product:nullptr);
 if(!owned||product->getClassName()!="HeqiaCard"||product->getSubcards()!=costs)return fail("Heqia first native product mismatch");
 // Card::Parse schedules its virtual product. Only the native event-payload
 // lease is required; no redundant raw OwnedCardPtr survives Lua shutdown.
 const Card*parsed=Card::Parse(product->toString());if(!parsed)return fail("Heqia first source wire did not parse");
 use=CardUseStruct(parsed,p);for(const auto&name:targets){auto*t=p->getRoom()->findPlayerByObjectName(name,true);if(!t)return fail("Heqia first target unavailable");use.to<<t;}
 if(!admit(use))return fail("Heqia first original native provider rejected");
 return current(r,p)||fail("Heqia first native admission changed current request");
}
}
AIRound41FaqiSelection prepareAIRound47HeqiaSelection(const AIRequest&r,ServerPlayer*p,
 const QString&token,const QStringList&selectedTargets,const std::function<bool(CardUseStruct&)> &admit){
 AIRound41FaqiSelection out;AIRound47HeqiaSlotStatus slotStatus=AIRound47HeqiaSlotStatus::Unknown;
 const Card *saved=readAIRound47HeqiaSlot(r,p,token,&out.targets,nullptr,&out.error,&slotStatus);
 if(slotStatus==AIRound47HeqiaSlotStatus::SourcePass)out.outcome=AIRound41FaqiOutcome::SourcePass;
 if(slotStatus==AIRound47HeqiaSlotStatus::SourceError)out.outcome=AIRound41FaqiOutcome::SourceError;
 if(!saved||!admit)return out;
 const QStringList baseTargets=out.targets;out.targets=selectedTargets;

 // Exact fixed source @@heqia serializes now, not at AG. Card::Parse intentionally
 // drops old request instance fields and current native attribution starts anew.
 out.sourceWire=saved->toString();
 const Card *parsed=Card::Parse(out.sourceWire);
 if(!parsed){out.outcome=AIRound41FaqiOutcome::NativeRejected;out.error="saved Heqia source wire has no native parsed product";return out;}
 out.use.from=p;
 // Numeric Card::Parse returns an Engine-owned wrapper even when filtered.
 // The wire form, not isVirtualCard(include_filter), determines parse custody.
 bool physicalWire=false;out.sourceWire.toInt(&physicalWire);
 // Card::Parse has already scheduled every virtual return with the lifetime
 // manager. Retain the normal event-payload lease without a second raw-pointer
 // OwnedCardPtr deleter: Lua-boxed CardUseStruct copies can outlive shutdown's
 // releaseEventPayloads/preclose drain. Their destruction must not dereference
 // an already retired parsed Card. Physical returns remain Engine-owned.
 out.use=CardUseStruct(parsed,p);
 for(const auto &name:out.targets){auto *target=p->getRoom()->findPlayerByObjectName(name,true);
  if(!target){out.outcome=AIRound41FaqiOutcome::NativeRejected;out.error="saved Heqia target no longer exists";return out;}out.use.to<<target;}
 const QString skill=parsed->getActivationSkillName();
 const auto *vs=Sanguosha->getViewAsSkill(skill);
 const auto *v2=dynamic_cast<const ViewAsSkillV2 *>(vs);
 // No post-revision check is advertised as a mutation guard. Only these fixed
 // compiled V2 paths are audited side-effect/inquiry-free during reconstruction.
 if(vs && (skill!="heqia" && (!v2 || skill!="limu"))){
  out.error="Heqia original provider is outside audited inquiry-free reconstruction";return out;
 }
 if(skill=="heqia"){
  // The fixed AI sends its real Basic, NOT the GUI HeqiaUseCard proxy.
  // Authenticate its actual native inquiry/material domain and preserve the
  // legacy original-provider resolution below, including no-instance refusal.
  if(!vs||v2||!vs->isEnabledAtResponse(p,"@@heqia2")||saved->getSkillName(false)!="_heqia"
    ||saved->getSubcards().size()!=1){
   out.outcome=AIRound41FaqiOutcome::NativeRejected;out.error="Heqia actual source differs from current native declaration/material";return out;
  }
  const int id=saved->getSubcards().first();
  if(p->getRoom()->getCardOwner(id)!=p
    ||(p->getRoom()->getCardPlace(id)!=Player::PlaceHand&&p->getRoom()->getCardPlace(id)!=Player::PlaceEquip)||Sanguosha->getCard(id)->hasFlag("using")){
   out.outcome=AIRound41FaqiOutcome::NativeRejected;out.error="Heqia source payment left current owner HE";return out;
  }
 }
 // Ordinary V1 declarations do not inquire. A solely attached multi-provider
 // V1 source would invoke native provider choice; never execute it in a query.
 if(skill=="heqia"){
  bool ordinary=false;int attached=0;
  for(const auto&i:p->getSkillInstances())if(i.skillName==skill){
   if(i.source==SourceAttached&&p->getSkillInstanceStateValue(skill,i.instanceID,"legacy_activation_lifecycle").toBool()){
    if(p->getValidSkillInstanceIds(skill).contains(i.instanceID))++attached;
   }else ordinary=true;
  }
  if(!ordinary&&attached>1){out.error="Heqia attached provider inquiry is outside this readonly selection";return out;}
 }
 bool privateAttachment=false;
 for(const auto &instance:p->getSkillInstances())if(instance.skillName==skill&&instance.source==SourceAttached
  &&p->getSkillInstanceStateValue(skill,instance.instanceID,"legacy_activation_lifecycle").toBool())privateAttachment=true;
 if(skill!="heqia" && !v2 && (privateAttachment || parsed->getActivationSkillInstanceId()!=0)){
  out.error="Heqia non-ViewAs attachment continuation is not audited";return out;
 }
 if(v2 && parsed->getActivationSkillInstanceId()==0){
  QList<int> ids;for(int id:p->getValidSkillInstanceIds(skill))
   if(p->getRoom()->canShowGeneralForSkill(SkillInstanceRef(p->objectName(),SkillInstanceKey(skill,id))))ids<<id;
  const int scoped=p->getMark(ViewAsSkillV2::borrowedActivationMarkName(skill));
  if(scoped>0){if(!ids.contains(scoped)){out.outcome=AIRound41FaqiOutcome::NativeRejected;out.error="native borrowed provider is unavailable";return out;}ids={scoped};}
  if(ids.size()>1){out.error="Heqia current original provider needs native instance-choice continuation";return out;}
 }
 // Source getCard alternatives are own cards. A numeric parse never confers
 // authority over a foreign/discard card merely because it was observed.
 if(physicalWire && (!p->handCards().contains(parsed->getEffectiveId())
   &&!p->getEquipsId().contains(parsed->getEffectiveId())&&!p->getHandPile().contains(parsed->getEffectiveId()))){
  out.outcome=AIRound41FaqiOutcome::NativeRejected;out.error="saved physical replacement left original owner material domain";return out;
 }
 if(!v2 && !physicalWire && parsed->getTypeId()==Card::TypeSkill){
  out.error="Heqia non-ViewAs SkillCard native activation is not audited";return out;
 }
 if(!admit(out.use)){out.outcome=AIRound41FaqiOutcome::NativeRejected;out.error="Heqia current original native provider rejected saved source";return out;}
 {
  QStringList actualTargets;QString why;const Card*actual=readAIRound47HeqiaSlot(r,p,token,&actualTargets,nullptr,&why);
  if(!actual||actual!=saved||actualTargets!=baseTargets||actual->toString()!=out.sourceWire){out.error=why.isEmpty()?"Heqia actual source changed during admission":why;return out;}
 }
 if(!current(r,p)){out.error="Heqia reconstruction changed current request state";return out;}
 out.outcome=AIRound41FaqiOutcome::Ready;out.error.clear();return out;
}

QJsonObject issueAIRound47HeqiaReceipt(const AIRequest&r,ServerPlayer*p,const QString&token,
 const QStringList&targets,const std::function<bool(CardUseStruct&)>&admit){
 auto selected=prepareAIRound47HeqiaSelection(r,p,token,targets,admit);
 if(selected.outcome==AIRound41FaqiOutcome::SourcePass)return{{"source_pass",true}};
 if(selected.outcome==AIRound41FaqiOutcome::SourceError)return{{"source_error",selected.error}};
 if(selected.outcome!=AIRound41FaqiOutcome::Ready)return{{"error",selected.error},{"native_rejected",selected.outcome==AIRound41FaqiOutcome::NativeRejected}};
 if(!r.conversionLedger)return{{"error","Heqia request ledger unavailable"}};
 const int id=r.cardConversions.size()+r.conversionLedger->tickets.size()+1;
 Receipt receipt;receipt.ledger=r.conversionLedger;receipt.decision=r.decisionId;receipt.revision=r.stateRevision;
 receipt.viewer=p->objectName();receipt.token=token;receipt.targets=selected.targets;receipt.wire=selected.sourceWire;
 receipt.spec=specOf(selected.use.card,id);receipt.activation=selected.use.activationRef;receipt.source=selected.use.sourceRef;
 AICardConversionView reservation;reservation.conversionId=id;reservation.policyClass="@heqia_saved_reconstruction";
 reservation.name=receipt.spec.name;reservation.available=false; // Never generic buildSpec authority.
 r.conversionLedger->tickets<<reservation;
 auto *l=ledger(p->getRoom(),true);
 for(auto it=l->receipts.begin();it!=l->receipts.end();)if(it->ledger.isNull())it=l->receipts.erase(it);else ++it;
 l->receipts<<receipt;
 return{{"card_spec",jsonSpec(receipt.spec)},{"targets",QJsonArray::fromStringList(receipt.targets)}};
}
QJsonObject issueAIRound47HeqiaFirst(const AIRequest&r,ServerPlayer*p,const QList<int>&costs,
 const QStringList&targets,const std::function<bool(CardUseStruct&)>&admit){
 CardUseStruct use;QString error;
 if(!prepareFirst(r,p,costs,targets,admit,use,error))return{{"error",error},{"native_rejected",true}};
 if(!r.conversionLedger)return{{"error","Heqia first request ledger unavailable"}};
 const int id=r.cardConversions.size()+r.conversionLedger->tickets.size()+1;
 Receipt receipt;receipt.ledger=r.conversionLedger;receipt.decision=r.decisionId;receipt.revision=r.stateRevision;
 receipt.viewer=p->objectName();receipt.first=true;receipt.firstCosts=costs;receipt.targets=targets;receipt.wire=use.card->toString();
 receipt.spec=specOf(use.card,id);receipt.activation=use.activationRef;receipt.source=use.sourceRef;
 AICardConversionView reservation;reservation.conversionId=id;reservation.policyClass="@heqia_saved_reconstruction";reservation.name=receipt.spec.name;reservation.available=false;
 r.conversionLedger->tickets<<reservation;ledger(p->getRoom(),true)->receipts<<receipt;
 return{{"card_spec",jsonSpec(receipt.spec)},{"targets",QJsonArray::fromStringList(targets)}};
}
bool hasAIRound47HeqiaReceipt(const AIRequest&r,int id){
 // A marker only; final lookup additionally authenticates the Room-owned proof.
 if(!r.conversionLedger)return false;
 for(const auto &ticket:r.conversionLedger->tickets)
  if(ticket.conversionId==id&&ticket.policyClass=="@heqia_saved_reconstruction")return true;
 return false;
}
bool applyAIRound47HeqiaReceipt(const AIRequest&r,ServerPlayer*p,const AICardSpec&spec,
 const QStringList&targets,const std::function<bool(CardUseStruct&)>&admit,CardUseStruct&use){
 if(!current(r,p)||!hasAIRound47HeqiaReceipt(r,spec.conversionId))return false;
 auto *l=ledger(p->getRoom());if(!l)return false;
 const Receipt *found=nullptr;
 for(const auto &entry:l->receipts)if(entry.spec.conversionId==spec.conversionId
  &&entry.ledger.toStrongRef()==r.conversionLedger&&entry.decision==r.decisionId
  &&entry.revision==r.stateRevision&&entry.viewer==p->objectName()){found=&entry;break;}
 if(!found)return false;const Receipt receipt=*found;
 if(!sameSpec(spec,receipt.spec)||targets!=receipt.targets)return false;
 if(receipt.first){
  CardUseStruct selected;QString error;
  if(!prepareFirst(r,p,receipt.firstCosts,targets,admit,selected,error)
    ||!sameSpec(specOf(selected.card,spec.conversionId),receipt.spec)
    ||selected.activationRef!=receipt.activation||selected.sourceRef!=receipt.source)return false;
  use=selected;return true;
 }
 auto selected=prepareAIRound47HeqiaSelection(r,p,receipt.token,targets,admit);
 if(selected.outcome!=AIRound41FaqiOutcome::Ready||selected.sourceWire!=receipt.wire
  ||!sameSpec(specOf(selected.use.card,spec.conversionId),receipt.spec)
  ||selected.use.activationRef!=receipt.activation||selected.use.sourceRef!=receipt.source)return false;
 use=selected.use;return true;
}
