#include "ai-strategy-round44-response.h"
#include "ai-strategy-room-ledger.h"
#include "ai-strategy-projection.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "skill.h"
#include "engine.h"
#include <QObject>
#include <QPointer>
#include <QJsonArray>
#include <QSet>
namespace {
struct Observation {QPointer<ServerPlayer>target;QList<int>ids;QJsonObject printed;SkillInstanceRef activation;quint64 revision=0;};
class Ledger final:public AIRoomLedger {public:explicit Ledger(Room&r):AIRoomLedger(r){}QHash<ServerPlayer*,Observation>observations;};
Ledger*ledger(Room&r,bool create){for(auto*c:r.children())if(auto*l=dynamic_cast<Ledger*>(c))return l;return create?new Ledger(r):nullptr;}
SkillInstanceRef current(Room&r,ServerPlayer*p,const QString&skill){
 if(!p)return {};const int id=p->getMark(ViewAsSkillV2::borrowedActivationMarkName(skill));
 const SkillInstanceRef ref(p->objectName(),SkillInstanceKey(skill,id));return r.isAcceptedViewAsEffect(ref)?ref:SkillInstanceRef();
}
bool scope(const AIRequest&r,Room&room,ServerPlayer*p,const QString&skill){
 return p&&p->getRoom()==&room&&r.decisionId&&r.viewerObjectName==p->objectName()
  &&r.worldView.self.objectName==p->objectName()&&r.stateRevision==room.roomRuntime()->stateRevision()
  &&r.kind==AIRequest::UseCard&&r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE
  &&r.pattern=="@@"+skill&&r.handlingMethod==Card::MethodUse
  &&r.skillActionContext.activationRef==current(room,p,skill)&&r.skillActionContext.activationRef.isValid();
}
const Observation*observed(const AIRequest&r,Room&room,ServerPlayer*p){
 auto*l=ledger(room,false);if(!l||!scope(r,room,p,"poxi"))return nullptr;
 const auto it=l->observations.constFind(p);if(it==l->observations.cend())return nullptr;
 const auto&o=it.value();if(!o.target||o.activation!=r.skillActionContext.activationRef
  ||o.revision!=r.stateRevision||r.prompt!="@poxi:"+o.target->objectName())return nullptr;
 return &o;
}
}
void recordAIRound44PoxiNotification(Room&room,ServerPlayer*viewer,const QList<int>&ids,const QString&reason,bool active){
 if(reason!="poxi"||!viewer)return;auto*l=ledger(room,true);l->observations.remove(viewer);
 if(!active||ids.isEmpty())return;const auto ref=current(room,viewer,"poxi");if(!ref.isValid())return;
 const auto notification=readAIStrategyNotifiedPile(room,viewer,"#poxi");
 const auto locations=notification.value("locations").toObject(),printed=notification.value("cards").toObject();
 Observation o;o.activation=ref;o.revision=room.roomRuntime()->stateRevision();QSet<int>unique;
 for(int id:ids){const auto location=locations.value(QString::number(id)).toObject();
  if(id<0||unique.contains(id)||!printed.value(QString::number(id)).isObject()
   ||location.value("place").toInt(-1)!=int(Player::PlaceHand))return;
  auto*target=room.findPlayerByObjectName(location.value("owner").toString(),true);
  if(!target||target==viewer||(o.target&&o.target!=target))return;
  o.target=target;unique.insert(id);o.ids<<id;
 }
 o.printed=printed;l->observations.insert(viewer,o);
}
void projectAIRound44LeiExchange(AIRequest&r,Room&room,ServerPlayer*p){
 const auto*o=observed(r,room,p);if(!o)return;
 QJsonArray ids,faces;bool currentComplete=true;
 for(int id:o->ids){ids.append(id);
  // A fake-pile notification authenticates order and printed identity only.
  // Never consult a foreign live wrapper to decide whether it is unchanged.
  // Existing request knownCards is built only after viewer visibility proves
  // the live wrapped face. Fake-pile printed cards are not added to that list.
  const AICardView*known=nullptr;
  for(const auto&player:r.worldView.players)if(player.objectName==o->target->objectName())
   for(const auto&card:player.knownCards)if(card.effectiveId==id){known=&card;break;}
  if(!known){currentComplete=false;continue;}
  faces.append(QJsonObject{{"id",id},{"effective_id",id},{"name",known->objectName},
   {"class_name",known->className},{"suit",known->suit},{"number",known->number},
   {"virtual_card",false},{"subcards",QJsonArray()}});
 }
 QJsonObject row{{"known",true},{"target",o->target->objectName()},{"card_ids",ids},
  {"current_faces_complete",currentComplete},{"cards",faces},
  {"decision_id",r.getDecisionId()},{"state_revision",r.getStateRevision()}};
 auto facts=r.worldView.strategyFacts.value("lei").toObject();facts.insert("poxi_disclosure",row);r.worldView.strategyFacts.insert("lei",facts);
}
int aiRound44LeiPrototype(const AIRequest&r,const ServerPlayer*viewer,const QString&skill,const QString&prototype){
 static const QHash<QString,QString>prototypes{{"poxi","PoxiDisCard"},{"hongju","HongjuCard"},{"olhongju","OLHongjuCard"},{"mobilehongju","MobileHongjuCard"}};
 if(!viewer||!viewer->getRoom()||!prototypes.contains(skill)||prototypes.value(skill)!=prototype)return -1;
 auto*p=const_cast<ServerPlayer*>(viewer);return scope(r,*p->getRoom(),p,skill)?1:-1;
}
bool authorizeAIRound44PoxiEvaluation(const AIRequest&r,int id,QJsonObject*face){
 if(face)*face={};if(r.kind!=AIRequest::UseCard||r.pattern!="@@poxi"||r.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE)return false;
 const auto row=r.worldView.strategyFacts.value("lei").toObject().value("poxi_disclosure").toObject();
 if(row.value("known")!=QJsonValue(true)||row.value("current_faces_complete")!=QJsonValue(true)
  ||row.value("decision_id")!=QJsonValue(r.getDecisionId())||row.value("state_revision")!=QJsonValue(r.getStateRevision()))return false;
 const auto ids=row.value("card_ids").toArray(),cards=row.value("cards").toArray();if(ids.size()!=cards.size())return false;
 for(int n=0;n<ids.size();++n)if(ids.at(n)==QJsonValue(id)){
  const auto c=cards.at(n).toObject();if(c.value("effective_id")!=QJsonValue(id))return false;if(face)*face=c;return true;
 }
 return false;
}
bool authorizeAIRound44LeiPayment(const AIRequest&r,Room&room,ServerPlayer*p,const SkillInstanceRef&activation,
 const SkillInstanceRef&source,const QList<int>&costs,QList<int>*foreign){
 if(foreign)foreign->clear();const QString skill=activation.key.skillName;
 if(!scope(r,room,p,skill)||activation!=r.skillActionContext.activationRef||source!=r.skillActionContext.sourceRef)return false;
 QSet<int>seen;for(int id:costs){if(id<0||seen.contains(id))return false;seen.insert(id);}
 if(skill=="poxi"){
  const auto*o=observed(r,room,p);if(!o||costs.size()!=4)return false;QSet<Card::Suit>suits;
  for(int id:costs){const auto*c=Sanguosha->getCard(id);if(!c||suits.contains(c->getSuit())||room.getCardPlace(id)!=Player::PlaceHand)return false;suits.insert(c->getSuit());
   if(p->handCards().contains(id)){if(p->isJilei(c))return false;}
   else if(o->ids.contains(id)&&o->target->handCards().contains(id)&&p->canDiscard(o->target,id)){if(foreign)*foreign<<id;}
   else return false;
  }return suits.size()==4;
 }
 if(skill!="hongju"&&skill!="olhongju"&&skill!="mobilehongju")return false;
 int hand=0,pile=0;const auto rong=p->getPile("rong");for(int id:costs){if(p->handCards().contains(id))++hand;else if(rong.contains(id))++pile;else return false;}
 return hand>0&&hand==pile&&costs.size()<=2*rong.size();
}
