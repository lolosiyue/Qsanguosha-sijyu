#include "ai-strategy-round49-notified.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "engine.h"
#include "card.h"
#include "skill.h"
#include "ai-strategy-projection.h"
#include "ai-strategy-card-identity.h"
#include <QJsonArray>
namespace {
bool live(const AIRequest&q,Room&r,ServerPlayer*p){return p&&r.getPlayers().contains(p)&&p->getRoom()==&r&&r.roomRuntime()&&!r.roomRuntime()->isClosing()&&q.decisionId&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&q.stateRevision==r.roomRuntime()->stateRevision();}
QJsonArray ordered(ServerPlayer*p,const QString&s){QJsonArray a;for(const auto&v:p->getTag(s+"ForAI").toList())a.append(v.toInt());return a;}
}
QString aiRound49NotifiedSkill(const AIRequest&q){
 if(q.kind!=AIRequest::UseCard||q.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE||q.handlingMethod!=Card::MethodUse)return {};
 if(q.pattern=="@@chuiti"&&q.prompt=="@chuiti")return "chuiti";
 if(q.pattern=="@@boxuan"&&q.prompt=="boxuan1")return "boxuan";
 if(q.pattern=="@@tanluan"&&q.prompt=="tanluan0")return "tanluan";
 return {};
}
void projectAIRound49Notified(AIRequest&q,Room&r,ServerPlayer*p){
 auto facts=q.worldView.strategyFacts.value("tenyear_hc").toObject();facts.remove("chuitiForAI");facts.remove("boxuanForAI");facts.remove("tanluanForAI");
 if(facts.isEmpty())q.worldView.strategyFacts.remove("tenyear_hc");else q.worldView.strategyFacts.insert("tenyear_hc",facts);
 const QString s=aiRound49NotifiedSkill(q);if(s.isEmpty()||!live(q,r,p))return;
 const auto witness=readAIStrategyNotifiedPile(r,p,"#"+s);
 if(!q.round49NotifiedBound){q.round49NotifiedBound=true;q.round49NotifiedWitness=witness;}
 if(q.round49NotifiedWitness!=witness)return;
 const auto ids=ordered(p,s);const auto cards=witness.value("cards").toObject();
 if(!ids.isEmpty()&&(!witness.value("ordered_ids").isArray()||witness.value("ordered_ids").toArray()!=ids))return;
 for(const auto&v:ids)if(!cards.contains(QString::number(v.toInt())))return;
 // Empty QVariant list is a known source empty loop, not a fabricated offering.
 facts.insert(s+"ForAI",ids);q.worldView.strategyFacts.insert("tenyear_hc",facts);
 for(const auto&v:ids)if(witness.value("current_card_ids").toArray().contains(v)){const Card*c=r.getCard(v.toInt());if(c&&!q.round49NotifiedIdentities.contains(v.toInt()))q.round49NotifiedIdentities.insert(v.toInt(),aiLegacyCardIdentity(r,p,c));}
}
bool authorizeAIRound49NotifiedEvaluation(const AIRequest&q,Room&r,ServerPlayer*p,int id,QJsonObject*out){
 const QString s=aiRound49NotifiedSkill(q);if(s.isEmpty()||id<0||!live(q,r,p)||!q.round49NotifiedBound)return false;
 const auto w=readAIStrategyNotifiedPile(r,p,"#"+s);
 if(w.isEmpty()||w!=q.round49NotifiedWitness||!w.value("current_card_ids").toArray().contains(id))return false;
 const auto ids=ordered(p,s);if(!w.value("ordered_ids").isArray()||w.value("ordered_ids").toArray()!=ids||!ids.contains(id)||ids!=q.worldView.strategyFacts.value("tenyear_hc").toObject().value(s+"ForAI").toArray())return false;
 const auto f=w.value("cards").toObject().value(QString::number(id)).toObject();const Card*c=r.getCard(id);
 if(!c||q.round49NotifiedIdentities.value(id).isEmpty()||q.round49NotifiedIdentities.value(id)!=aiLegacyCardIdentity(r,p,c)||c->isVirtualCard()||c->getEffectiveId()!=id||f.isEmpty()||f.value("name").toString()!=c->objectName()||f.value("class_name").toString()!=c->getClassName()||f.value("suit").toInt()!=int(c->getSuit())||f.value("number").toInt()!=c->getNumber())return false;
 if(out)*out=f;return true;
}
bool authorizeAIRound49NotifiedPhysical(const AIRequest&q,Room&r,ServerPlayer*p,const SkillInstanceRef&a,const SkillInstanceRef&source,int id){
 if(!authorizeAIRound49NotifiedEvaluation(q,r,p,id))return false;const QString s=aiRound49NotifiedSkill(q);
 if(!a.isValid()||!source.isValid()||a.ownerObjectName!=p->objectName()||a.key.skillName!=s||!p->hasSkillInstance(s,a.key.instanceID)||p->isSkillInvalid(s,a.key.instanceID))return false;
 auto root=r.resolveSkillInstanceRootRef(a);if(!root.isValid())root=a;if(root!=source)return false;
 bool issued=q.hasSkillActionContext&&q.skillActionContext.activationRef==a&&q.skillActionContext.sourceRef==source;
 for(const auto&v:q.skillActions)if(v.activationRef==a&&v.sourceRef==source)issued=true;
 const auto*v=Sanguosha->getViewAsSkill(s);if(!issued||!v||dynamic_cast<const ViewAsSkillV2*>(v)||v->getExpandPile()!="#"+s||!v->isEnabledAtResponse(p,q.pattern))return false;
 const Card*c=r.getCard(id);if(!c)return false;
 if(s=="tanluan"&&(Sanguosha->getCurrentCardUsePattern()!=q.pattern||Sanguosha->getCurrentCardUseReason()!=q.reason))return false;
 // Source submits c:toString(), i.e. original physical ID. The legacy Room
 // path does NOT replay the client-only #pile viewFilter. Do not invent a
 // server pile or convert this to ActiveSkillCard. V1 viewAs returns c itself.
 const Card*product=v->viewAs(QList<const Card*>{c});const bool same=product==c;
 if(product&&product!=c&&product->isVirtualCard())const_cast<Card*>(product)->deleteLater();
 return same&&Sanguosha->getViewAsSkill(s)==v&&r.getCard(id)==c&&live(q,r,p)&&authorizeAIRound49NotifiedEvaluation(q,r,p,id);
}
