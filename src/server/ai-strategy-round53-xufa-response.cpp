#include "ai-strategy-round53-xufa-response.h"
#include "ai-strategy-round45-xufa-slot.h"
#include "ai-strategy-round44-certificate.h"
#include "ai.h"
#include "ai-runtime.h"
#include "card.h"
#include "card-lifetime-manager.h"
#include "engine.h"
#include "ol.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "skill.h"
#include <QJsonArray>
#include <QPointer>
#include <QWeakPointer>
#include <typeinfo>
namespace {
struct Frame{
 Room*room=nullptr;QPointer<ServerPlayer>owner;QPointer<const Card>card;
 std::shared_ptr<const CardLifetimeToken>token;CardLifetimeLease lease;
 QString chosen;bool claimed=false;Frame*previous=nullptr;
 const Card*liveCard()const{
  // Identity custody for the exact accepted native outer SkillCard. This is
  // not a readonly observation and grants no access to its tags or graph.
  if(card.isNull()||!token||!token->live||!lease.isValid())return nullptr;
  return globalCardLifetimeManager().liveToken(card.data())==token?card.data():nullptr;
 }
};
thread_local Frame*active=nullptr;
struct Inquiry{Frame*origin=nullptr;Inquiry*previous=nullptr;quint64 decision=0,revision=0;
 QWeakPointer<AIConversionLedger>ledger;bool bound=false;};
thread_local Inquiry*inquiry=nullptr;
bool current(const AIRequest&q,ServerPlayer*p){return p&&p->getRoom()&&q.decisionId>0
 &&p->getRoom()->getAllPlayers(true).contains(p)&&!p->getRoom()->roomRuntime()->isClosing()
 &&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()
 &&aiRound44RangeRequestCurrent(*p->getRoom(),q,p);}
bool play(const AIRequest&q,ServerPlayer*p){if(!current(q,p)||q.kind!=AIRequest::Activate
 ||q.reason!=CardUseStruct::CARD_USE_REASON_PLAY||!q.pattern.isEmpty()||p->getPhase()!=Player::Play)return false;
 // Presence is enough for a source readonly constructor. No extra hasSkill
 // invalidity/activation test is added to the reached source callback.
 for(const auto&i:p->getSkillInstances())if(i.skillName=="xufa")return true;return false;}
bool disclosed(const AIRequest&q,const Card*c){if(!c)return false;
 auto match=[&](const AICardView&v){return v.nativeIdentity&&globalCardLifetimeManager().isLive(v.nativeIdentity)
  &&v.nativeIdentity->address==c&&v.cardId==c->getId()&&v.effectiveId==c->getEffectiveId()
  &&v.objectName==c->objectName()&&v.className==c->getClassName()&&v.suit==int(c->getSuit())
  &&v.number==c->getNumber()&&v.skillName==c->getSkillName(false)&&v.subcardIds==c->getSubcards()
  &&v.virtualCard==c->isVirtualCard();};
 for(const auto&v:q.worldView.handCards)if(match(v))return true;
 for(const auto&v:q.worldView.self.equips)if(match(v))return true;
 for(const auto&pile:q.worldView.self.piles)for(const auto&v:pile.cards)if(match(v))return true;
 return false;}
bool material(const AIRequest&q,ServerPlayer*p,int id,bool pile){
 // Source getCards('he') returns inner equipment objects. Authenticate that
 // exact disclosed native object, never substitute outer wrapper metadata.
 for(const auto*c:p->getCards("he"))if(c&&c->getEffectiveId()==id&&disclosed(q,c))return true;
 return pile&&p->getPile("xufa").contains(id)&&disclosed(q,Sanguosha->getCard(id));
}
// The exact fixed ordinary V1 raw-wire path returns before GUI viewAs. Refuse
// private attachment resolution before it could select/inquire on a provider.
bool ordinaryRaw(ServerPlayer*p,const Card*c,QString&error){
 if(!c||c->getActivationSkillInstanceId()!=0){error="Xufa raw source unexpectedly carries an instance";return false;}
 const QString name=c->getActivationSkillName();const auto*vs=Sanguosha->getViewAsSkill(name);
 if(dynamic_cast<const ViewAsSkillV2*>(vs)){error="Xufa selected replacement reaches a V2 provider outside this fixed raw branch";return false;}
 bool ordinary=false,attached=false;
 for(const auto&i:p->getSkillInstances())if(i.skillName==name){
  if(i.source==SourceAttached&&p->getSkillInstanceStateValue(name,i.instanceID,"legacy_activation_lifecycle").toBool())attached=true;
  else ordinary=true;
 }
 if(attached&&!ordinary){error="Xufa solely attached provider requires separate native continuation";return false;}
 return true;
}
struct Receipt{QWeakPointer<AIConversionLedger>ledger;QPointer<ServerPlayer>owner;quint64 decision=0,revision=0;
 bool first=false;QPointer<const Card>source;QString wire;QStringList targets;AICardSpec spec;SkillInstanceRef activation,root;};
class Ledger final:public QObject{public:explicit Ledger(Room&r):QObject(&r){}QList<Receipt>rows;};
Ledger*ledger(Room*r,bool create=false){if(!r)return nullptr;for(auto*c:r->children())if(auto*l=dynamic_cast<Ledger*>(c))return l;return create?new Ledger(*r):nullptr;}
AICardSpec specOf(const Card*c,int id){AICardSpec s;s.name=c->objectName();s.suit=c->getSuit();s.number=c->getNumber();s.skillName=c->getSkillName(false);s.subcardIds=c->getSubcards();s.conversionId=id;return s;}
bool same(const AICardSpec&a,const AICardSpec&b){return a.name==b.name&&a.suit==b.suit&&a.number==b.number&&a.skillName==b.skillName&&a.subcardIds==b.subcardIds&&a.conversionId==b.conversionId;}
QJsonObject json(const AICardSpec&s){QJsonArray a;for(int id:s.subcardIds)a<<id;return{{"name",s.name},{"suit",s.suit},{"number",s.number},{"skill",s.skillName},{"subcards",a},{"conversion_id",s.conversionId}};}
bool prepare(const AIRequest&q,ServerPlayer*p,bool first,const AIReadonlyCardLease&lease,const QStringList&targets,
 const std::function<bool(CardUseStruct&)>&admit,CardUseStruct&use,QString&wire,QString&error){
 auto fail=[&](const char*s){error=QString::fromLatin1(s);return false;};
 if(!current(q,p)||!admit||!lease)return fail("Xufa selected request or custody expired");
 const Card*saved=lease();if(!saved)return fail("Xufa exact selected source expired");
 if(first){
  if(!play(q,p)||typeid(*saved)!=typeid(XufaCard)||saved->getSkillName(false)!="xufa"||!targets.isEmpty())return fail("Xufa first selector differs from exact source SkillCard");
  if(saved->getSubcards().isEmpty())return fail("Xufa fixed first source never returns empty materials");
  for(int id:saved->getSubcards())if(!material(q,p,id,true))return fail("Xufa first source material is no longer disclosed owner HE/pile");
 }else{
  if(!aiRound53XufaQuestion(q,p))return fail("Xufa saved selector is outside actual native inquiry");
  auto actual=readAIRound45XufaSlot(q,p);
  if(actual.kind!=AIRound45XufaSlotRead::CardPlan||actual.targetsMissing||!actual.lease||actual.lease()!=saved||actual.targets!=targets)
   return fail("Xufa actual source card or ordered targets changed");
  // This adapter closes the actual source xufa declaration and source-selected
  // physical replacements. Other fixed use-revise providers stay named debt.
  bool numeric=false;saved->toString().toInt(&numeric);
  if(!numeric&&(saved->getSkillName(false)!="xufa"||!saved->isNDTrick()||saved->getSubcards().size()>1))
   return fail("Xufa selected source replacement needs its own current provider adapter");
  if(numeric&&!material(q,p,saved->getEffectiveId(),false))return fail("Xufa physical replacement left disclosed owner HE");
  for(int id:saved->getSubcards())if(!material(q,p,id,false))return fail("Xufa later trick material left disclosed owner HE");
  if(saved->canRecast()&&targets.isEmpty()&&q.handlingMethod!=Card::MethodRecast)return fail("Xufa source declined recast before selection");
 }
 wire=saved->toString();
 // Parse schedules virtual products before returning. Protect that interval
 // until the following CardUseStruct acquires its event-payload native lease;
 // otherwise final native reconstruction can retire the product inside Parse.
 CardLifetimeScope parseCustody(globalCardLifetimeManager());
 const Card*parsed=Card::Parse(wire);if(!parsed)return fail("Xufa exact source wire did not parse");
 // Parse already scheduled virtual returns; normal event-payload custody only.
 use=CardUseStruct(parsed,p);
 for(const auto&name:targets){auto*t=p->getRoom()->findPlayerByObjectName(name,true);if(!t)return fail("Xufa ordered source target left roster");use.to<<t;}
 if(!ordinaryRaw(p,parsed,error)||!admit(use))return fail(error.isEmpty()?"Xufa original native raw provider rejected":error.toUtf8().constData());
 if(!current(q,p)||lease()!=saved||saved->toString()!=wire)return fail("Xufa source changed during raw reconstruction");
 if(!first){auto actual=readAIRound45XufaSlot(q,p);if(actual.kind!=AIRound45XufaSlotRead::CardPlan||actual.targetsMissing||!actual.lease||actual.lease()!=saved||actual.targets!=targets)return fail("Xufa actual slot changed during raw reconstruction");}
 return true;
}
QJsonObject issue(const AIRequest&q,ServerPlayer*p,bool first,AIReadonlyCardLease source,const QStringList&targets,const std::function<bool(CardUseStruct&)>&admit){
 CardUseStruct use;QString wire,error;if(!prepare(q,p,first,source,targets,admit,use,wire,error))return{{"error",error}};
 if(!q.conversionLedger)return{{"error","Xufa current conversion ledger unavailable"}};
 const int id=q.cardConversions.size()+q.conversionLedger->tickets.size()+1;
 Receipt receipt;receipt.ledger=q.conversionLedger;receipt.owner=p;receipt.decision=q.decisionId;receipt.revision=q.stateRevision;
 receipt.first=first;receipt.source=source();q.conversionLedger->observedProducts.insert(id,std::move(source));receipt.wire=wire;receipt.targets=targets;receipt.spec=specOf(use.card,id);receipt.activation=use.activationRef;receipt.root=use.sourceRef;
 AICardConversionView marker;marker.conversionId=id;marker.policyClass="@xufa_raw_source";marker.name=receipt.spec.name;marker.available=false;
 q.conversionLedger->tickets<<marker;p->getRoom()->roomRuntime()->ai().trackObservationLedger(q.conversionLedger);auto*l=ledger(p->getRoom(),true);
 for(auto i=l->rows.begin();i!=l->rows.end();)if(i->ledger.isNull())i=l->rows.erase(i);else ++i;l->rows<<receipt;
 return{{"card_spec",json(receipt.spec)},{"targets",QJsonArray::fromStringList(targets)}};
}
}
struct AIRound53XufaQuestionScope::State{Frame f;};
AIRound53XufaQuestionScope::AIRound53XufaQuestionScope(Room&r,ServerPlayer*p,const Card*c,const QString&chosen):d(new State){
 d->f.previous=active;active=&d->f; // Invalid/nested entries shadow the outer scope.
 if(p&&p->getRoom()==&r&&c&&typeid(*c)==typeid(XufaCard)&&!chosen.isEmpty()){
  d->f.room=&r;d->f.owner=p;d->f.card=c;d->f.chosen=chosen;
  // This compiled on-use scope borrows its actual live `this` object. The
  // native factory need not have registered it yet: observeCard registers
  // lifetime/destruction tracking without taking ownership or disclosing face.
  d->f.token=globalCardLifetimeManager().observeCard(const_cast<Card*>(c));
  d->f.lease=CardLifetimeLease(globalCardLifetimeManager(),d->f.token);
 }
}
AIRound53XufaQuestionScope::~AIRound53XufaQuestionScope(){active=d->f.previous;}
struct AIRound53XufaInquiryScope::State{Inquiry i;};
AIRound53XufaInquiryScope::AIRound53XufaInquiryScope(Room&r,ServerPlayer*p,const QString&pattern,const QString&prompt,int method):d(new State){
 d->i.previous=inquiry;inquiry=&d->i; // Every native inquiry shadows the outer one.
 if(!active||active->claimed)return;
 active->claimed=true; // The compiled origin may authorize only its immediate first inquiry.
 if(active->room==&r&&active->owner==p&&active->liveCard()
  &&pattern=="@@xufa"&&prompt=="xufa0:"+active->chosen&&method==int(Card::MethodUse))d->i.origin=active;
}
AIRound53XufaInquiryScope::~AIRound53XufaInquiryScope(){inquiry=d->i.previous;}
namespace {
bool questionShape(const AIRequest&q,ServerPlayer*p){return current(q,p)&&inquiry&&inquiry->origin
 &&inquiry->origin==active&&active->room==p->getRoom()&&active->owner==p&&active->liveCard()
 &&q.kind==AIRequest::UseCard&&q.pattern=="@@xufa"&&q.prompt=="xufa0:"+active->chosen
 &&q.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&q.handlingMethod==Card::MethodUse;}
}
void bindAIRound53XufaQuestion(const AIRequest&q,ServerPlayer*p){
 if(!questionShape(q,p)||inquiry->bound||!q.conversionLedger)return;
 inquiry->bound=true;inquiry->decision=q.decisionId;inquiry->revision=q.stateRevision;inquiry->ledger=q.conversionLedger;
}
bool aiRound53XufaQuestion(const AIRequest&q,ServerPlayer*p){return questionShape(q,p)&&inquiry->bound
 &&inquiry->decision==q.decisionId&&inquiry->revision==q.stateRevision
 &&q.conversionLedger&&inquiry->ledger.toStrongRef()==q.conversionLedger;}
std::unique_ptr<Card> constructAIRound53XufaFirst(const AIRequest&q,ServerPlayer*p,const QList<int>&costs,QString*error){
 auto fail=[&](const char*s)->std::unique_ptr<Card>{if(error)*error=QString::fromLatin1(s);return{};};
 if(!play(q,p)||costs.isEmpty())return fail("Xufa first source requires reached owner Play and nonempty selected list");
 for(int id:costs)if(!material(q,p,id,true))return fail("Xufa initial source material is not current disclosed owner HE/pile");
 std::unique_ptr<Card>c(new XufaCard);c->setSkillName("xufa");c->addSubcards(costs);
 if(error)error->clear();return c;
}
QJsonObject issueAIRound53XufaFirst(const AIRequest&q,ServerPlayer*p,AIReadonlyCardLease c,const std::function<bool(CardUseStruct&)>&f){return issue(q,p,true,std::move(c),{},f);}
QJsonObject issueAIRound53XufaSaved(const AIRequest&q,ServerPlayer*p,AIReadonlyCardLease c,const QStringList&t,const std::function<bool(CardUseStruct&)>&f){return issue(q,p,false,std::move(c),t,f);}
bool hasAIRound53XufaReceipt(const AIRequest&q,int id){if(!q.conversionLedger)return false;for(const auto&t:q.conversionLedger->tickets)if(t.conversionId==id&&t.policyClass=="@xufa_raw_source")return true;return false;}
bool applyAIRound53XufaReceipt(const AIRequest&q,ServerPlayer*p,const AICardSpec&spec,const QStringList&t,const std::function<bool(CardUseStruct&)>&admit,CardUseStruct&use){
 if(!current(q,p)||!hasAIRound53XufaReceipt(q,spec.conversionId))return false;auto*l=ledger(p->getRoom());if(!l)return false;
 for(const auto&e:l->rows)if(e.ledger.toStrongRef()==q.conversionLedger&&e.owner==p&&e.decision==q.decisionId&&e.revision==q.stateRevision&&same(e.spec,spec)&&e.targets==t){
  const Receipt copy=e;QString wire,error;CardUseStruct actual;
  const auto source=q.conversionLedger->observedProducts.value(spec.conversionId);
  if(!source||!copy.source||source()!=copy.source.data())return false;
  if(!prepare(q,p,copy.first,source,t,admit,actual,wire,error)||wire!=copy.wire||!same(specOf(actual.card,spec.conversionId),spec)||actual.activationRef!=copy.activation||actual.sourceRef!=copy.root)return false;
  actual.m_validateTargets=true;use=actual;return true;
 }return false;
}
