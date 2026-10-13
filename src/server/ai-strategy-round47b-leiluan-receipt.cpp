#include "ai-strategy-round47b-leiluan.h"
#include "ai-strategy-room-ledger.h"
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
namespace {
struct Receipt {
 QWeakPointer<AIConversionLedger> ledger;quint64 decision=0,revision=0;
 QString viewer,token,wire;QStringList targets;AICardSpec spec;
 SkillInstanceRef activation,source;
};
class Ledger final:public AIRoomLedger{public:explicit Ledger(Room&r):AIRoomLedger(r){}QList<Receipt>receipts;};
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
AIRound41FaqiSelection prepareAIRound47BLeiluanSelection(const AIRequest&r,ServerPlayer*p,
 const QString&token,const std::function<bool(CardUseStruct&)> &admit){
 AIRound41FaqiSelection out;AIRound47BLeiluanSlotStatus slotStatus=AIRound47BLeiluanSlotStatus::Unknown;
 const Card *saved=readAIRound47BLeiluanSlot(r,p,token,&out.targets,nullptr,&out.error,&slotStatus);
 if(slotStatus==AIRound47BLeiluanSlotStatus::SourcePass)out.outcome=AIRound41FaqiOutcome::SourcePass;
 if(slotStatus==AIRound47BLeiluanSlotStatus::SourceError)out.outcome=AIRound41FaqiOutcome::SourceError;
 if(!saved||!admit)return out;

 // Exact fixed source @@leiluan serializes now, not at AG. Card::Parse intentionally
 // drops old request instance fields and current native attribution starts anew.
 out.sourceWire=saved->toString();
 const Card *parsed=Card::Parse(out.sourceWire);
 if(!parsed){out.outcome=AIRound41FaqiOutcome::NativeRejected;out.error="saved Leiluan source wire has no native parsed product";return out;}
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
  if(!target){out.outcome=AIRound41FaqiOutcome::NativeRejected;out.error="saved Leiluan target no longer exists";return out;}out.use.to<<target;}
 const QString skill=parsed->getActivationSkillName();
 const auto *vs=Sanguosha->getViewAsSkill(skill);
 const auto *v2=dynamic_cast<const ViewAsSkillV2 *>(vs);
 // No post-revision check is advertised as a mutation guard. Only these fixed
 // compiled V2 paths are audited side-effect/inquiry-free during reconstruction.
 if(vs && (skill!="leiluan" && (!v2 || skill!="limu"))){
  out.error="Leiluan original provider is outside audited inquiry-free reconstruction";return out;
 }
 if(skill=="leiluan"){
  if(!vs||v2||!vs->isEnabledAtResponse(p,"@@leiluan")){out.outcome=AIRound41FaqiOutcome::NativeRejected;out.error="Leiluan native response is disabled";return out;}
  const Card*product=vs->viewAs(QList<const Card*>());
  std::unique_ptr<const Card> owned(product&&product->isVirtualCard()?product:nullptr);
  if(!product||!product->isVirtualCard()||product->toString()!=out.sourceWire){out.outcome=AIRound41FaqiOutcome::NativeRejected;out.error="Leiluan current native zero-cost product differs from saved source";return out;}
 }
 // Ordinary V1 declarations do not inquire. A solely attached multi-provider
 // V1 source would invoke native provider choice; never execute it in a query.
 if(skill=="leiluan"){
  bool ordinary=false;int attached=0;
  for(const auto&i:p->getSkillInstances())if(i.skillName==skill){
   if(i.source==SourceAttached&&p->getSkillInstanceStateValue(skill,i.instanceID,"legacy_activation_lifecycle").toBool()){
    if(p->getValidSkillInstanceIds(skill).contains(i.instanceID))++attached;
   }else ordinary=true;
  }
  if(!ordinary&&attached>1){out.error="Leiluan attached provider inquiry is outside this readonly selection";return out;}
 }
 bool privateAttachment=false;
 for(const auto &instance:p->getSkillInstances())if(instance.skillName==skill&&instance.source==SourceAttached
  &&p->getSkillInstanceStateValue(skill,instance.instanceID,"legacy_activation_lifecycle").toBool())privateAttachment=true;
 if(skill!="leiluan" && !v2 && (privateAttachment || parsed->getActivationSkillInstanceId()!=0)){
  out.error="Leiluan non-ViewAs attachment continuation is not audited";return out;
 }
 if(v2 && parsed->getActivationSkillInstanceId()==0){
  QList<int> ids;for(int id:p->getValidSkillInstanceIds(skill))
   if(p->getRoom()->canShowGeneralForSkill(SkillInstanceRef(p->objectName(),SkillInstanceKey(skill,id))))ids<<id;
  const int scoped=p->getMark(ViewAsSkillV2::borrowedActivationMarkName(skill));
  if(scoped>0){if(!ids.contains(scoped)){out.outcome=AIRound41FaqiOutcome::NativeRejected;out.error="native borrowed provider is unavailable";return out;}ids={scoped};}
  if(ids.size()>1){out.error="Leiluan current original provider needs native instance-choice continuation";return out;}
 }
 // Source getCard alternatives are own cards. A numeric parse never confers
 // authority over a foreign/discard card merely because it was observed.
 if(physicalWire && (!p->handCards().contains(parsed->getEffectiveId())
   &&!p->getEquipsId().contains(parsed->getEffectiveId())&&!p->getHandPile().contains(parsed->getEffectiveId()))){
  out.outcome=AIRound41FaqiOutcome::NativeRejected;out.error="saved physical replacement left original owner material domain";return out;
 }
 if(!v2 && !physicalWire && parsed->getTypeId()==Card::TypeSkill){
  out.error="Leiluan non-ViewAs SkillCard native activation is not audited";return out;
 }
 if(!admit(out.use)){out.outcome=AIRound41FaqiOutcome::NativeRejected;out.error="Leiluan current original native provider rejected saved source";return out;}
 {
  QStringList actualTargets;QString why;const Card*actual=readAIRound47BLeiluanSlot(r,p,token,&actualTargets,nullptr,&why);
  if(!actual||actual!=saved||actualTargets!=out.targets||actual->toString()!=out.sourceWire){out.error=why.isEmpty()?"Leiluan actual source changed during admission":why;return out;}
 }
 if(!current(r,p)){out.error="Leiluan reconstruction changed current request state";return out;}
 out.outcome=AIRound41FaqiOutcome::Ready;out.error.clear();return out;
}

QJsonObject issueAIRound47BLeiluanReceipt(const AIRequest&r,ServerPlayer*p,const QString&token,
 const std::function<bool(CardUseStruct&)>&admit){
 auto selected=prepareAIRound47BLeiluanSelection(r,p,token,admit);
 if(selected.outcome==AIRound41FaqiOutcome::SourcePass)return{{"source_pass",true}};
 if(selected.outcome==AIRound41FaqiOutcome::SourceError)return{{"source_error",selected.error}};
 if(selected.outcome!=AIRound41FaqiOutcome::Ready)return{{"error",selected.error},{"native_rejected",selected.outcome==AIRound41FaqiOutcome::NativeRejected}};
 if(!r.conversionLedger)return{{"error","Leiluan request ledger unavailable"}};
 const int id=r.cardConversions.size()+r.conversionLedger->tickets.size()+1;
 Receipt receipt;receipt.ledger=r.conversionLedger;receipt.decision=r.decisionId;receipt.revision=r.stateRevision;
 receipt.viewer=p->objectName();receipt.token=token;receipt.targets=selected.targets;receipt.wire=selected.sourceWire;
 receipt.spec=specOf(selected.use.card,id);receipt.activation=selected.use.activationRef;receipt.source=selected.use.sourceRef;
 AICardConversionView reservation;reservation.conversionId=id;reservation.policyClass="@leiluan_saved_reconstruction";
 reservation.name=receipt.spec.name;reservation.available=false; // Never generic buildSpec authority.
 r.conversionLedger->tickets<<reservation;
 auto *l=ledger(p->getRoom(),true);
 for(auto it=l->receipts.begin();it!=l->receipts.end();)if(it->ledger.isNull())it=l->receipts.erase(it);else ++it;
 l->receipts<<receipt;
 return{{"card_spec",jsonSpec(receipt.spec)},{"targets",QJsonArray::fromStringList(receipt.targets)}};
}
bool hasAIRound47BLeiluanReceipt(const AIRequest&r,int id){
 // A marker only; final lookup additionally authenticates the Room-owned proof.
 if(!r.conversionLedger)return false;
 for(const auto &ticket:r.conversionLedger->tickets)
  if(ticket.conversionId==id&&ticket.policyClass=="@leiluan_saved_reconstruction")return true;
 return false;
}
bool applyAIRound47BLeiluanReceipt(const AIRequest&r,ServerPlayer*p,const AICardSpec&spec,
 const QStringList&targets,const std::function<bool(CardUseStruct&)>&admit,CardUseStruct&use){
 if(!current(r,p)||!hasAIRound47BLeiluanReceipt(r,spec.conversionId))return false;
 auto *l=ledger(p->getRoom());if(!l)return false;
 const Receipt *found=nullptr;
 for(const auto &entry:l->receipts)if(entry.spec.conversionId==spec.conversionId
  &&entry.ledger.toStrongRef()==r.conversionLedger&&entry.decision==r.decisionId
  &&entry.revision==r.stateRevision&&entry.viewer==p->objectName()){found=&entry;break;}
 if(!found)return false;const Receipt receipt=*found;
 if(!sameSpec(spec,receipt.spec)||targets!=receipt.targets)return false;
 auto selected=prepareAIRound47BLeiluanSelection(r,p,receipt.token,admit);
 if(selected.outcome!=AIRound41FaqiOutcome::Ready||selected.sourceWire!=receipt.wire
  ||!sameSpec(specOf(selected.use.card,spec.conversionId),receipt.spec)
  ||selected.use.activationRef!=receipt.activation||selected.use.sourceRef!=receipt.source)return false;
 use=selected.use;return true;
}
