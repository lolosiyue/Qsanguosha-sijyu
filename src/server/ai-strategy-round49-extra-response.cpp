#include "ai-strategy-round49-extra-response.h"
#include "ai.h"
#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-card-identity.h"
#include "engine.h"
#include "standard.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonArray>
#include <cmath>
#include <limits>
namespace {
bool current(const AIRequest&r,Room&room,const ServerPlayer*p){
 return p&&p->getRoom()==&room&&room.roomRuntime()&&!room.roomRuntime()->isClosing()
  &&r.decisionId&&room.getPlayers().contains(const_cast<ServerPlayer*>(p))
  &&r.viewerObjectName==p->objectName()&&r.worldView.self.objectName==p->objectName()
  &&aiRound44RangeRequestCurrent(room,r,p);
}
bool owns(const ServerPlayer*p,const char*name){for(const auto &i:p->getSkillInstances())if(i.skillName==QLatin1String(name))return true;return false;}
// The issued inquiry is source scope. Do not re-run invalidity or activation
// gates before the fixed callback constructs its readonly policy card.
bool invoke(const AIRequest&r,const char*key){return r.kind==AIRequest::SkillInvoke&&r.choiceOptions.reason==QLatin1String(key);}
bool face(const AICardView&f,const Card*c,Room&r,const ServerPlayer*p){return c&&f.effectiveId==c->getEffectiveId()&&f.objectName==c->objectName()&&f.className==c->getClassName()&&f.suit==int(c->getSuit())&&f.number==c->getNumber()&&!f.legacyIdentityToken.isEmpty()&&f.legacyIdentityToken==aiLegacyCardIdentity(r,const_cast<ServerPlayer*>(p),c);}
}
void projectAIRound49HTMS(AIRequest&r,Room&room,ServerPlayer*p)
{
 // Reprojection is replacement of this slice, never an additive cache.
 // Clear before current() so stale/closing/foreign requests cannot retain facts.
 auto props=r.worldView.strategyFacts.value("owner_skill_properties").toObject();
 props.remove("maware_use");props.remove("s_newtype");
 auto htms=r.worldView.strategyFacts.value("htms").toObject();htms.remove("nvzidaoTarget");
 auto publicProps=htms.value("public_player_properties").toObject();
 for(const QString &name:publicProps.keys())if(publicProps.value(name).isObject()){
  auto row=publicProps.value(name).toObject();row.remove("s_newtype");
  if(row.isEmpty())publicProps.remove(name);else publicProps.insert(name,row);
 }
 if(publicProps.isEmpty())htms.remove("public_player_properties");else htms.insert("public_player_properties",publicProps);
 const auto publish=[&]{
  if(props.isEmpty())r.worldView.strategyFacts.remove("owner_skill_properties");else r.worldView.strategyFacts.insert("owner_skill_properties",props);
  if(htms.isEmpty())r.worldView.strategyFacts.remove("htms");else r.worldView.strategyFacts.insert("htms",htms);
 };publish();
 if(!current(r,room,p))return;
 if(r.kind==AIRequest::UseCard&&r.pattern=="@@maware")props.insert("maware_use",p->property("maware_use").toString());
 if(r.kind==AIRequest::PlayerChosen&&r.choiceOptions.reason=="nvzidaoCard"){
  const auto *who=room.getTag("nvzidaoTarget").value<ServerPlayer*>();
  // Null is source-known nil. An alien pointer is never dereferenced or guessed.
  if(!who)htms.insert("nvzidaoTarget",false);
  else if(room.getAllPlayers(true).contains(const_cast<ServerPlayer*>(who)))htms.insert("nvzidaoTarget",who->objectName());
 }
 if(r.kind==AIRequest::UseCard&&r.pattern=="@@s_newtypeOther"){
  auto values=htms.value("public_player_properties").toObject();
  for(const auto *other:room.getAllPlayers(true)){
   auto row=values.value(other->objectName()).toObject();
   // Fixed writer is Room::setPlayerProperty, broadcast to every recipient.
   // Preserve the serialized value; do not substitute newtype_pattern/name.
   row.insert("s_newtype",other->property("s_newtype").toString());values.insert(other->objectName(),row);
  }
  htms.insert("public_player_properties",values);
  props.insert("s_newtype",p->property("s_newtype").toString());
 }
 publish();
}
bool aiRound49HTMSConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&q)
{
 if(!current(r,room,p)||q.size()!=6||!q.value("source_key").isString()||!q.value("name").isString()||!q.value("skill").isString()||!q.value("subcards").isArray())return false;
 const QString key=q.value("source_key").toString(),name=q.value("name").toString(),skill=q.value("skill").toString();
 const auto costs=q.value("subcards").toArray();
 const bool play=r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY&&r.pattern.isEmpty();
 int suit=int(Card::NoSuit),number=0,maxCosts=0;QString raw;bool scope=false;
 if(key=="#htms_zangsongCard"){scope=play&&owns(p,"htms_zangsong")&&name=="slash";raw="htms_zangsong";maxCosts=1;}
 else if(key=="#wangzhe"){scope=play&&owns(p,"wangzhe")&&name=="slash";raw="wangzhe";}
 else if(key=="soulfire"){scope=play&&owns(p,"soulfire")&&name=="fire_slash";raw="soulfire";maxCosts=1;}
 else if(key=="jiasuduijue"){scope=invoke(r,"jiasuduijue")&&name=="Duel";raw="jiasuduijue";suit=int(Card::SuitToBeDecided);}
 else if(key=="tprs"){scope=invoke(r,"tprs")&&name=="fire_slash";raw="tprs";}
 else if(key=="slash_defence"){scope=invoke(r,"slash_defence")&&name=="duel";raw="zhanxianfanyu";}
 else if(key=="@@maware"){
  const QStringList fields=r.worldView.strategyFacts.value("owner_skill_properties").toObject().value("maware_use").toString().split('+');
  bool numeric=false;const int count=fields.value(0).toInt(&numeric);
  scope=r.kind==AIRequest::UseCard&&r.pattern==key&&numeric&&count>0&&fields.value(1)==name
      &&p->property("maware_use").toString()==r.worldView.strategyFacts.value("owner_skill_properties").toObject().value("maware_use").toString();
  raw="samsara";number=-1;maxCosts=count;
 }
 else if(key=="@@s_newtypeOther"){
  scope=r.kind==AIRequest::UseCard&&r.pattern==key;raw="s_newtype";number=-1;
  bool declared=false;const auto all=r.worldView.strategyFacts.value("htms").toObject().value("public_player_properties").toObject();
  for(auto i=all.begin();i!=all.end();++i)if(i.value().toObject().value("s_newtype")==QJsonValue(name))declared=true;
  scope=scope&&declared;
 }
 if(!scope||(skill!=""&&skill!=raw)||q.value("suit")!=QJsonValue(suit)||q.value("number")!=QJsonValue(number)||costs.size()>maxCosts)return false;
 for(const auto &v:costs){
  if(!v.isDouble()||!std::isfinite(v.toDouble())||v.toDouble()!=std::floor(v.toDouble())||v.toDouble()<0||v.toDouble()>std::numeric_limits<int>::max())return false;
  const int id=v.toInt();const Card*c=nullptr;bool disclosed=false;
  if(p->handCards().contains(id)){
   c=Sanguosha->getCard(id);for(const auto &f:r.worldView.handCards)if(face(f,c,room,p))disclosed=true;
  }else if((key=="soulfire"&&p->getHandPile().contains(id))||(key=="@@maware"&&p->getPile("wooden_ox").contains(id))){
   c=Sanguosha->getCard(id);for(const auto &pile:r.worldView.self.piles)if(key!="@@maware"||pile.name=="wooden_ox")for(const auto &f:pile.cards)if(face(f,c,room,p))disclosed=true;
  }else if(key=="soulfire"){
   for(const auto *eq:p->getEquips())if(eq->getEffectiveId()==id){c=eq;for(const auto &f:r.worldView.self.equips)if(face(f,c,room,p))disclosed=true;}
  }
  if(!c||!disclosed||room.getCardOwner(id)!=p)return false;
  if(key=="#htms_zangsongCard"&&!c->isDamageCard())return false;
  if(key=="soulfire"&&!c->isKindOf("Slash")&&!c->isKindOf("Jink"))return false;
 }
 return current(r,room,p);
}
