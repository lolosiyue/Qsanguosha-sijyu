#include "ai-strategy-round51-notified-costs.h"
#include "ai-strategy-room-ledger.h"
#include "ai-strategy-projection.h"
#include "ai-strategy-card-identity.h"
#include "ai.h"
#include "card.h"
#include "card-lifetime-manager.h"
#include <QSet>
#include "room.h"
#include "room-runtime.h"
#include "engine.h"
#include "serverplayer.h"
#include "skill.h"
#include "tenyear.h"
#include <QJsonArray>
#include <QRegularExpression>
#include <QWeakPointer>
#include <typeinfo>
namespace {
struct Frame {Room*room;ServerPlayer*viewer;ServerPlayer*other;QList<int>offered;QString prompt;int depth;};
thread_local QList<Frame> frames;
struct Receipt {QWeakPointer<AIConversionLedger>ledger;quint64 decision,revision;QString viewer,wire;AICardSpec spec;SkillInstanceRef activation,source;};
class Ledger final:public AIRoomLedger {public:explicit Ledger(Room&r):AIRoomLedger(r){}QList<Receipt>rows;};
Ledger*ledger(Room&r,bool make=false){for(auto*c:r.children())if(auto*l=dynamic_cast<Ledger*>(c))return l;return make?new Ledger(r):nullptr;}
bool live(const AIRequest&q,ServerPlayer*p){return p&&p->getRoom()&&p->getRoom()->getPlayers().contains(p)&&p->getRoom()->roomRuntime()&&!p->getRoom()->roomRuntime()->isClosing()&&q.decisionId&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&q.stateRevision==p->getRoom()->roomRuntime()->stateRevision();}
AICardSpec specOf(const Card*c,int id){AICardSpec s;s.name=c->objectName();s.suit=c->getSuit();s.number=c->getNumber();s.skillName=c->getSkillName(false);s.subcardIds=c->getSubcards();s.conversionId=id;return s;}
bool same(const AICardSpec&a,const AICardSpec&b){return a.name==b.name&&a.suit==b.suit&&a.number==b.number&&a.skillName==b.skillName&&a.subcardIds==b.subcardIds&&a.conversionId==b.conversionId;}
QJsonObject json(const AICardSpec&s){QJsonArray ids;for(int id:s.subcardIds)ids<<id;return{{"name",s.name},{"suit",s.suit},{"number",s.number},{"skill",s.skillName},{"subcards",ids},{"conversion_id",s.conversionId}};}
QList<int>tagIds(const QVariant&v){if(v.userType()==qMetaTypeId<QList<int>>())return v.value<QList<int>>();QList<int>ids;for(auto x:v.toList())ids<<x.toInt();return ids;}
QJsonArray array(const QList<int>&ids){QJsonArray a;for(int id:ids)a<<id;return a;}
bool disclosed(const AIRequest&q,ServerPlayer*p,ServerPlayer*owner,int id){
 // Find independently issued current face FIRST. Notification printed faces do
 // not enter this proof; never compare a hidden wrapper to the printed catalog.
 const AICardView*known=nullptr;
 if(owner==p){for(const auto&v:q.worldView.handCards)if(v.effectiveId==id){known=&v;break;}}
 else for(const auto&v:q.worldView.players)if(v.objectName==owner->objectName())for(const auto&c:v.knownCards)if(c.effectiveId==id){known=&c;break;}
 if(!known||!known->nativeIdentity||known->legacyIdentityToken.isEmpty())return false;
 Room&r=*p->getRoom();const Card*c=r.getCard(id);
 return c&&r.getCardOwner(id)==owner&&r.getCardPlace(id)==Player::PlaceHand
  &&globalCardLifetimeManager().liveToken(c)==known->nativeIdentity
  &&aiLegacyCardIdentity(r,p,c)==known->legacyIdentityToken
  &&c->getEffectiveId()==id&&!c->isVirtualCard()&&known->objectName==c->objectName()
  &&known->className==c->getClassName()&&known->suit==int(c->getSuit())&&known->number==c->getNumber()
  &&known->cardId==c->getId()&&known->virtualCard==c->isVirtualCard()&&known->subcardIds==c->getSubcards()
  &&known->skillName==c->getSkillName(false)&&known->sourceSkillName==c->getSourceSkillName()
  &&known->kindOfNames==c->getKindOfNames()&&known->typeId==int(c->getTypeId())
  &&known->red==c->isRed()&&known->black==c->isBlack()&&known->canRecast==c->canRecast()
  &&known->willThrow==c->willThrow()&&known->targetFixed==c->targetFixed()
  &&known->handlingMethod==int(c->getHandlingMethod())&&known->damageCard==c->isDamageCard()
  &&known->singleTarget==c->isSingleTargetCard()&&known->legacyStringKey==c->toString();
}
bool prepare(const AIRequest&q,ServerPlayer*p,const QString&wire,const std::function<bool(CardUseStruct&)>&admit,CardUseStruct&use,QString&why){
 const auto fail=[&](const QString&s){why=s;return false;};
 if(!live(q,p)||frames.isEmpty())return fail("Sushou source question is not current");
 Room&r=*p->getRoom();const Frame f=frames.last();
 if(f.room!=&r||f.viewer!=p||!f.other||!r.getPlayers().contains(f.other)||r.getCurrent()!=f.other||f.other==p
  ||r.roomRuntime()->lua().protectedCallDepth()!=f.depth||q.kind!=AIRequest::UseCard
  ||q.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE||q.handlingMethod!=Card::MethodUse
  ||q.pattern!="@@sushou"||q.prompt!=f.prompt)return fail("Sushou exact compiled inquiry scope differs");
 const auto w=readAIStrategyNotifiedPile(r,p,"#sushou");const auto seq=tagIds(p->getTag("sushouForAI"));
 if(seq.isEmpty()||seq!=f.offered||w.value("ordered_ids").toArray()!=array(seq)
  ||q.worldView.strategyFacts.value("tenyear_hc").toObject().value("sushouForAI").toArray()!=array(seq))return fail("Sushou recipient notification changed");
 // Full source loads every offered card before valuation/selection.
 for(int id:seq){const auto loc=w.value("locations").toObject().value(QString::number(id)).toObject();
  if(loc.value("owner").toString()!=f.other->objectName()||loc.value("place").toInt(-1)!=int(Player::PlaceHand)
   ||!disclosed(q,p,f.other,id))return fail("Sushou source needs independently disclosed current foreign hand face");}
 static const QRegularExpression syntax("^@SushouCard=([0-9]+)(\\+[0-9]+)+$");
 if(!syntax.match(wire).hasMatch())return fail("Sushou source raw wire malformed");
 QList<int>costs;for(const auto&s:wire.mid(QStringLiteral("@SushouCard=").size()).split('+')){bool ok=false;int id=s.toInt(&ok);if(!ok||id<0||QString::number(id)!=s)return fail("Sushou source integer wire malformed");costs<<id;}
 if(costs.size()%2||costs.size()>p->getLostHp()*2)return fail("Sushou source pair count differs");
 QMap<int,int>offeredCounts;for(int id:seq)++offeredCounts[id];QSet<int>own;
 for(int i=0;i<costs.size();i+=2){int other=costs[i],self=costs[i+1];
  if(offeredCounts.value(other)<=0||own.contains(self)||!disclosed(q,p,p,self))return fail("Sushou ordered foreign/own source cost pair differs");
  --offeredCounts[other];own.insert(self);
 }
 // Card::Parse is the fixed legacy AI ABI; no GUI fake-pile viewFilter/viewAs.
 const Card*parsed=Card::Parse(wire);if(!parsed)return fail("Sushou source raw parser rejected wire");
 CardUseStruct candidate(parsed,p); // manager event-payload lease; Parse scheduled deleteLater.
 if(typeid(*parsed)!=typeid(SushouCard)||parsed->getSubcards()!=costs||parsed->getHandlingMethod()!=Card::MethodNone||parsed->willThrow()||!parsed->targetFixed())return fail("Sushou native raw product differs");
 // Fixed ordinary V1 early return is inquiry-free. Attached alternatives would
 // ask a provider-choice question; refuse that different native continuation.
 bool ordinary=false;int attached=0;
 for(const auto&i:p->getSkillInstances())if(i.skillName=="sushou"){
  if(i.source==SourceAttached&&p->getSkillInstanceStateValue(i.skillName,i.instanceID,"legacy_activation_lifecycle").toBool())++attached;else ordinary=true;}
 if(!ordinary&&attached)return fail("Sushou attached-provider reconstruction is outside the fixed ordinary inquiry");
 if(!admit||!admit(candidate)||!live(q,p)||readAIStrategyNotifiedPile(r,p,"#sushou")!=w||tagIds(p->getTag("sushouForAI"))!=seq)return fail("Sushou native reconstruction rejected or changed source context");
 if(candidate.card!=parsed||candidate.card->getSubcards()!=costs)return fail("Sushou resolver replaced original raw product");
 use=candidate;return true;
}
}
const Card*askAIRound51Sushou(Room&r,ServerPlayer*p,ServerPlayer*other,const QList<int>&ids,const QString&prompt){
 frames<<Frame{&r,p,other,ids,prompt,r.roomRuntime()->lua().protectedCallDepth()};
 struct Pop{~Pop(){frames.removeLast();}}pop;
 return r.askForUseCard(p,"@@sushou",prompt);
}
QJsonObject issueAIRound51NotifiedCosts(const AIRequest&q,ServerPlayer*p,const QString&wire,const std::function<bool(CardUseStruct&)>&admit){
 CardUseStruct use;QString why;if(!prepare(q,p,wire,admit,use,why))return{{"error",why},{"key","sushou.notified_costs"}};
 if(!q.conversionLedger)return{{"error","Sushou active receipt ledger missing"}};
 const int id=q.cardConversions.size()+q.conversionLedger->tickets.size()+1;
 Receipt proof{q.conversionLedger,q.decisionId,q.stateRevision,p->objectName(),wire,specOf(use.card,id),use.activationRef,use.sourceRef};
 AICardConversionView marker;marker.conversionId=id;marker.policyClass="@r51_sushou_raw_costs";marker.name=proof.spec.name;marker.available=false;
 q.conversionLedger->tickets<<marker;auto*l=ledger(*p->getRoom(),true);
 for(auto it=l->rows.begin();it!=l->rows.end();)if(it->ledger.isNull())it=l->rows.erase(it);else ++it;
 l->rows<<proof;return{{"card_spec",json(proof.spec)},{"targets",QJsonArray()}};
}
bool hasAIRound51NotifiedCosts(const AIRequest&q,int id){if(q.conversionLedger)for(const auto&t:q.conversionLedger->tickets)if(t.conversionId==id&&t.policyClass=="@r51_sushou_raw_costs")return true;return false;}
bool applyAIRound51NotifiedCosts(const AIRequest&q,ServerPlayer*p,const AICardSpec&s,const QStringList&targets,const std::function<bool(CardUseStruct&)>&admit,CardUseStruct&use){
 if(!live(q,p)||!targets.isEmpty()||!hasAIRound51NotifiedCosts(q,s.conversionId))return false;auto*l=ledger(*p->getRoom());if(!l)return false;
 Receipt proof;bool found=false;for(const auto&v:l->rows)if(v.ledger.toStrongRef()==q.conversionLedger&&v.decision==q.decisionId&&v.revision==q.stateRevision&&v.viewer==p->objectName()&&same(v.spec,s)){proof=v;found=true;break;}
 if(!found)return false;CardUseStruct rebuilt;QString why;
 if(!prepare(q,p,proof.wire,admit,rebuilt,why)||!same(specOf(rebuilt.card,s.conversionId),s)||rebuilt.activationRef!=proof.activation||rebuilt.sourceRef!=proof.source)return false;
 use=rebuilt;return true;
}
