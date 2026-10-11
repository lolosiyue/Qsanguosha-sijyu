#include "ai-strategy-round45-faqi-slot.h"
#include "ai-strategy-round41-response.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "skill.h"
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QUuid>
#include <QJsonArray>
#include <QWeakPointer>
#include <limits>
namespace {
struct Saved {
 quint64 epoch=0; bool synchronized=false;
 QPointer<ServerPlayer> viewer;
 QString token;
 AIReadonlyCardLease lease;
 QStringList targets;
 QList<int> materials;
 int physical=-1,offered=-1;
};
struct Receipt {
 QWeakPointer<AIConversionLedger> ledger;quint64 decision=0,revision=0;
 QString viewer,token,wire;QStringList targets;AICardSpec spec;
 SkillInstanceRef activation,source;
};
class Ledger final:public QObject {
 public:explicit Ledger(Room &room):QObject(&room){} quint64 epoch=1;bool exhausted=false;QHash<QString,Saved> plans;QList<Receipt> receipts;
};
Ledger *ledger(Room *room,bool create=false){
 if(!room)return nullptr;for(QObject *child:room->children())if(auto *found=dynamic_cast<Ledger*>(child))return found;
 return create?new Ledger(*room):nullptr;
}
bool sameSpec(const AICardSpec&a,const AICardSpec&b){return a.name==b.name&&a.suit==b.suit&&a.number==b.number
 &&a.skillName==b.skillName&&a.subcardIds==b.subcardIds&&a.conversionId==b.conversionId;}
AICardSpec specOf(const Card *c,int id){AICardSpec s;s.name=c->objectName();s.suit=c->getSuit();s.number=c->getNumber();
 s.skillName=c->getSkillName(false);s.subcardIds=c->getSubcards();s.conversionId=id;return s;}
QJsonObject jsonSpec(const AICardSpec&s){QJsonArray costs;for(int id:s.subcardIds)costs<<id;
 return{{"name",s.name},{"suit",s.suit},{"number",s.number},{"skill",s.skillName},{"subcards",costs},{"conversion_id",s.conversionId}};}

bool current(const AIRequest&r,const ServerPlayer*p){return p&&p->getRoom()&&r.decisionId>0
 &&r.viewerObjectName==p->objectName()&&r.worldView.self.objectName==p->objectName()
 &&r.stateRevision==p->getRoom()->roomRuntime()->stateRevision();}
bool shown(const AIRequest&r,int id){
 for(const auto &c:r.worldView.handCards)if(c.effectiveId==id)return true;
 for(const auto &c:r.worldView.discardPile)if(c.effectiveId==id)return true;
 for(const auto &c:r.choiceOptions.cards)if(c.effectiveId==id)return true;
 QList<AIPlayerView> roster=r.worldView.players;roster.prepend(r.worldView.self);
 for(const auto &p:roster){
  for(const auto &c:p.equips)if(c.effectiveId==id)return true;
  for(const auto &c:p.judgingArea)if(c.effectiveId==id)return true;
  for(const auto &c:p.knownCards)if(c.effectiveId==id)return true;
  for(const auto &pile:p.piles)for(const auto &c:pile.cards)if(c.effectiveId==id)return true;
 }
 return false;
}
bool recipient(const AIRequest&r,const ServerPlayer*p,int chosen){
 if(r.kind!=AIRequest::UseCard||r.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE
   ||r.pattern!="@@faqi"||r.handlingMethod!=Card::MethodUse)return false;
 // Only a genuinely issued current Faqi action establishes its native pending
 // choice. Lua's remembered scalar/mark cannot confer this question permission.
 QList<AiSkillActionContext> actions=r.skillActions;
 if(r.hasSkillActionContext)actions.prepend(r.skillActionContext);
 for(const auto &a:actions)if(a.activationRef.ownerObjectName==p->objectName()
   &&a.activationRef.key.skillName=="faqi"&&a.activationRef.key.instanceID>0
   &&p->hasSkillInstance("faqi",a.activationRef.key.instanceID)
   &&!p->isSkillInvalid("faqi",a.activationRef.key.instanceID)
   &&p->getSkillInstanceStateValue("faqi",a.activationRef.key.instanceID,"pendingCardId",-1).toInt()==chosen)
   return true;
 return false;
}
}
QString captureAIRound41FaqiPlan(const AIRequest&r,ServerPlayer*p,int offered,
 AIReadonlyCardLease lease,const QStringList &targets,QString *error,const QJsonObject &sourceFields){
 auto fail=[&](const char*m){if(error)*error=QString::fromLatin1(m);return QString();};
 if(!current(r,p)||r.kind!=AIRequest::AmazingGrace||r.choiceOptions.reason!="faqi"
   ||!r.choiceOptions.cardIds.contains(offered))return fail("Faqi capture requires actual offered AG choice");
 bool offeredFace=false;for(const auto &c:r.choiceOptions.cards)if(c.effectiveId==offered)offeredFace=true;
 if(!offeredFace||!lease)return fail("Faqi capture lacks disclosed offer or native policy custody");
 const Card *card=lease();if(!card)return fail("Faqi selected policy graph expired");
 // Do not read material-derived suit/number/name/string before disclosure.
 const int physical=card->isVirtualCard()?-1:card->getEffectiveId();
 if(physical>=0&&!shown(r,physical))return fail("Faqi selected physical object is not disclosed");
 const auto materials=card->getSubcards();
 for(int id:materials)if(!shown(r,id))return fail("Faqi selected material is not disclosed");
 QSet<QString> roster{p->objectName()};for(const auto &other:r.worldView.players)roster.insert(other.objectName);
 for(const auto &target:targets)if(!roster.contains(target))return fail("Faqi selected target is outside public roster");
 Room *room=p->getRoom();
 auto *ownerLedger=ledger(room,true);if(ownerLedger->exhausted)return fail("Faqi continuity epoch exhausted");
 Saved s;s.epoch=ownerLedger->epoch;s.viewer=p;s.token="faqi-observation:"+QUuid::createUuid().toString(QUuid::WithoutBraces);
 s.lease=std::move(lease);s.targets=targets;s.materials=materials;s.physical=physical;s.offered=offered;
 if(!sourceFields.isEmpty()){
  const QString actual=writeAIRound45FaqiSlot(r,p,offered,s.lease,targets,sourceFields,error);
  if(actual.isEmpty())return {};s.synchronized=true;s.token=actual;
 }
 ownerLedger->plans[p->objectName()]=s;if(error)error->clear();return s.token;
}
const Card *restoreAIRound41FaqiPlan(const AIRequest&r,ServerPlayer*p,const QString&token,
 QStringList *targets,QString *error,AIRound41FaqiOutcome*sourceStatus){
 auto fail=[&](const char*m)->const Card*{if(error)*error=QString::fromLatin1(m);return nullptr;};
 if(!current(r,p))return fail("Faqi saved observation requires current owner request");
 auto *room=ledger(p->getRoom());if(!room)return fail("Faqi saved observation absent");
 auto saved=room->plans.constFind(p->objectName());
 if(room->exhausted||saved==room->plans.cend()||(!saved->synchronized&&saved->epoch!=room->epoch)||saved->viewer!=p||token.isEmpty()||saved->token!=token)
  return fail("Faqi observation token does not match current viewer slot");
 if(!recipient(r,p,saved->offered))return fail("Faqi observation does not match issued continuation and pending choice");
 if(saved->synchronized){
  int offered=-1;AIRound45FaqiSlotStatus status=AIRound45FaqiSlotStatus::Unknown;
  const Card*actual=readAIRound45FaqiSlot(r,p,token,targets,&offered,error,&status);
  if(sourceStatus){if(status==AIRound45FaqiSlotStatus::SourcePass)*sourceStatus=AIRound41FaqiOutcome::SourcePass;
   else if(status==AIRound45FaqiSlotStatus::SourceError)*sourceStatus=AIRound41FaqiOutcome::SourceError;}
  if(!actual||offered!=saved->offered)return nullptr;
  return actual;
 }
 if(saved->physical>=0&&!shown(r,saved->physical))return fail("Faqi physical observation is no longer disclosed");
 for(int id:saved->materials)if(!shown(r,id))return fail("Faqi material observation is no longer disclosed");
 const Card *card=saved->lease();if(!card)return fail("Faqi saved native graph expired");
 if(card->getSubcards()!=saved->materials)return fail("Faqi saved material sequence changed outside observation");
 if(targets)*targets=saved->targets;if(error)error->clear();return card;
}
void interruptAIRound41FaqiPlansAsync(Room&r)noexcept{
 // Retain custody until normal cleanup; destroying a lease during a Lua GC or
 // debug-hook notification could dispatch native deletion side effects.
 if(auto*l=ledger(&r)){
  if(l->epoch==std::numeric_limits<quint64>::max())l->exhausted=true;
  else ++l->epoch;
 }
}
void clearAIRound41FaqiPlans(Room&r){if(auto*l=ledger(&r)){
 // Old overlays still invalidate unconditionally. Actual synchronized source
 // slots are independently re-read on EVERY reconstruction and final apply.
 for(auto i=l->plans.begin();i!=l->plans.end();)if(!i->synchronized||!hasAIRound45FaqiSlot(r,i->viewer,i->token))i=l->plans.erase(i);else ++i;
 for(auto i=l->receipts.begin();i!=l->receipts.end();){auto*p=r.findPlayerByObjectName(i->viewer,true);if(!p||!hasAIRound45FaqiSlot(r,p,i->token))i=l->receipts.erase(i);else ++i;}
}}
AIRound41FaqiSelection prepareAIRound41FaqiSelection(const AIRequest&r,ServerPlayer*p,
 const QString&token,const std::function<bool(CardUseStruct&)> &admit){
 AIRound41FaqiSelection out;
 const Card *saved=restoreAIRound41FaqiPlan(r,p,token,&out.targets,&out.error,&out.outcome);
 if(!saved||!admit)return out;
 auto *observations=ledger(p->getRoom());const quint64 observedEpoch=observations->epoch;
 const bool synchronized=hasAIRound45FaqiSlot(*p->getRoom(),p,token);
 // Exact fixed source @@faqi serializes now, not at AG. Card::Parse intentionally
 // drops old request instance fields and current native attribution starts anew.
 out.sourceWire=saved->toString();
 const Card *parsed=Card::Parse(out.sourceWire);
 if(!parsed){out.outcome=AIRound41FaqiOutcome::NativeRejected;out.error="saved Faqi source wire has no native parsed product";return out;}
 out.use.from=p;
 // Numeric Card::Parse returns an Engine-owned wrapper even when filtered.
 // The wire form, not isVirtualCard(include_filter), determines parse custody.
 bool physicalWire=false;out.sourceWire.toInt(&physicalWire);
 if(!physicalWire)out.use.setOwnedCard(const_cast<Card*>(parsed));else out.use.card=parsed;
 for(const auto &name:out.targets){auto *target=p->getRoom()->findPlayerByObjectName(name,true);
  if(!target){out.outcome=AIRound41FaqiOutcome::NativeRejected;out.error="saved Faqi target no longer exists";return out;}out.use.to<<target;}
 const QString skill=parsed->getActivationSkillName();
 const auto *vs=Sanguosha->getViewAsSkill(skill);
 const auto *v2=dynamic_cast<const ViewAsSkillV2 *>(vs);
 // No post-revision check is advertised as a mutation guard. Only these fixed
 // compiled V2 paths are audited side-effect/inquiry-free during reconstruction.
 if(vs && (!v2 || (skill!="faqi" && skill!="limu"))){
  out.error="Faqi original provider is outside audited inquiry-free reconstruction";return out;
 }
 bool privateAttachment=false;
 for(const auto &instance:p->getSkillInstances())if(instance.skillName==skill&&instance.source==SourceAttached
  &&p->getSkillInstanceStateValue(skill,instance.instanceID,"legacy_activation_lifecycle").toBool())privateAttachment=true;
 if(!v2 && (privateAttachment || parsed->getActivationSkillInstanceId()!=0)){
  out.error="Faqi non-ViewAs attachment continuation is not audited";return out;
 }
 if(v2 && parsed->getActivationSkillInstanceId()==0){
  QList<int> ids;for(int id:p->getValidSkillInstanceIds(skill))
   if(p->getRoom()->canShowGeneralForSkill(SkillInstanceRef(p->objectName(),SkillInstanceKey(skill,id))))ids<<id;
  const int scoped=p->getMark(ViewAsSkillV2::borrowedActivationMarkName(skill));
  if(scoped>0){if(!ids.contains(scoped)){out.outcome=AIRound41FaqiOutcome::NativeRejected;out.error="native borrowed provider is unavailable";return out;}ids={scoped};}
  if(ids.size()>1){out.error="Faqi current original provider needs native instance-choice continuation";return out;}
 }
 // Source getCard alternatives are own cards. A numeric parse never confers
 // authority over a foreign/discard card merely because it was observed.
 if(physicalWire && (!p->handCards().contains(parsed->getEffectiveId())
   &&!p->getEquipsId().contains(parsed->getEffectiveId())&&!p->getHandPile().contains(parsed->getEffectiveId()))){
  out.outcome=AIRound41FaqiOutcome::NativeRejected;out.error="saved physical replacement left original owner material domain";return out;
 }
 if(!v2 && !physicalWire && parsed->getTypeId()==Card::TypeSkill){
  out.error="Faqi non-ViewAs SkillCard native activation is not audited";return out;
 }
 if(!admit(out.use)){out.outcome=AIRound41FaqiOutcome::NativeRejected;out.error="Faqi current original native provider rejected saved source";return out;}
 if(!synchronized&&(observations->exhausted||observations->epoch!=observedEpoch)){
  out.error="Faqi reconstruction crossed an uncertified gameplay entry";return out;
 }
 if(synchronized){
  QStringList actualTargets;int offered=-1;QString why;
  const Card*actual=readAIRound45FaqiSlot(r,p,token,&actualTargets,&offered,&why);
  if(!actual||actual!=saved||actualTargets!=out.targets||actual->toString()!=out.sourceWire){
   out.error=why.isEmpty()?"Faqi actual source changed during current native admission":why;return out;
  }
 }
 if(!current(r,p)){out.error="Faqi reconstruction changed current request state";return out;}
 out.outcome=AIRound41FaqiOutcome::Ready;out.error.clear();return out;
}

std::unique_ptr<Card> constructAIRound41FaqiRevise(const AIRequest&r,ServerPlayer*p,
 const QString&kind,const QList<int>&costs,QString *error){
 auto fail=[&](const char*m)->std::unique_ptr<Card>{if(error)*error=m;return {};};
 if(!current(r,p)||r.kind!=AIRequest::AmazingGrace||r.choiceOptions.reason!="faqi"||!p->hasSkill("limu"))
  return fail("Limu readonly revise constructor requires current Faqi AG and actual owner skill");
 if((kind=="limu"&&costs.size()!=1)||(kind=="spear"&&costs.size()!=2)||(kind!="limu"&&kind!="spear"))
  return fail("Limu source constructor cost shape changed");
 for(int id:costs)if(!shown(r,id)||(!p->handCards().contains(id)&&!p->getEquipsId().contains(id)))
  return fail("Limu source constructor material is not disclosed own HE");
 std::unique_ptr<Card> card;
 if(kind=="limu")card.reset(const_cast<Card*>(Card::Parse("@LimuCard="+QString::number(costs.first()))));
 else{card.reset(Sanguosha->cloneCard("slash",Card::NoSuit,0));if(card){card->setSkillName("spear");card->addSubcards(costs);}}
 if(error)error->clear();return card;
}
QJsonObject issueAIRound41FaqiReceipt(const AIRequest&r,ServerPlayer*p,const QString&token,
 const std::function<bool(CardUseStruct&)>&admit){
 auto selected=prepareAIRound41FaqiSelection(r,p,token,admit);
 if(selected.outcome==AIRound41FaqiOutcome::SourcePass)return{{"source_pass",true}};
 if(selected.outcome==AIRound41FaqiOutcome::SourceError)return{{"source_error",selected.error}};
 if(selected.outcome!=AIRound41FaqiOutcome::Ready)return{{"error",selected.error},{"native_rejected",selected.outcome==AIRound41FaqiOutcome::NativeRejected}};
 if(!r.conversionLedger)return{{"error","Faqi request ledger unavailable"}};
 const int id=r.cardConversions.size()+r.conversionLedger->tickets.size()+1;
 Receipt receipt;receipt.ledger=r.conversionLedger;receipt.decision=r.decisionId;receipt.revision=r.stateRevision;
 receipt.viewer=p->objectName();receipt.token=token;receipt.targets=selected.targets;receipt.wire=selected.sourceWire;
 receipt.spec=specOf(selected.use.card,id);receipt.activation=selected.use.activationRef;receipt.source=selected.use.sourceRef;
 AICardConversionView reservation;reservation.conversionId=id;reservation.policyClass="@faqi_saved_reconstruction";
 reservation.name=receipt.spec.name;reservation.available=false; // Never generic buildSpec authority.
 r.conversionLedger->tickets<<reservation;
 auto *l=ledger(p->getRoom(),true);
 for(auto it=l->receipts.begin();it!=l->receipts.end();)if(it->ledger.isNull())it=l->receipts.erase(it);else ++it;
 l->receipts<<receipt;
 return{{"card_spec",jsonSpec(receipt.spec)},{"targets",QJsonArray::fromStringList(receipt.targets)}};
}
bool hasAIRound41FaqiReceipt(const AIRequest&r,int id){
 // A marker only; final lookup additionally authenticates the Room-owned proof.
 if(!r.conversionLedger)return false;
 for(const auto &ticket:r.conversionLedger->tickets)
  if(ticket.conversionId==id&&ticket.policyClass=="@faqi_saved_reconstruction")return true;
 return false;
}
bool applyAIRound41FaqiReceipt(const AIRequest&r,ServerPlayer*p,const AICardSpec&spec,
 const QStringList&targets,const std::function<bool(CardUseStruct&)>&admit,CardUseStruct&use){
 if(!current(r,p)||!hasAIRound41FaqiReceipt(r,spec.conversionId))return false;
 auto *l=ledger(p->getRoom());if(!l)return false;
 const Receipt *found=nullptr;
 for(const auto &entry:l->receipts)if(entry.spec.conversionId==spec.conversionId
  &&entry.ledger.toStrongRef()==r.conversionLedger&&entry.decision==r.decisionId
  &&entry.revision==r.stateRevision&&entry.viewer==p->objectName()){found=&entry;break;}
 if(!found)return false;const Receipt receipt=*found;
 if(!sameSpec(spec,receipt.spec)||targets!=receipt.targets)return false;
 auto selected=prepareAIRound41FaqiSelection(r,p,receipt.token,admit);
 if(selected.outcome!=AIRound41FaqiOutcome::Ready||selected.sourceWire!=receipt.wire
  ||!sameSpec(specOf(selected.use.card,spec.conversionId),receipt.spec)
  ||selected.use.activationRef!=receipt.activation||selected.use.sourceRef!=receipt.source)return false;
 use=selected.use;return true;
}
