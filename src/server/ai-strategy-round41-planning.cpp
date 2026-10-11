#include "ai-strategy-round41-planning.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonArray>
#include <QSet>
#include <cmath>
#include <limits>
namespace {
bool active(const AIRequest&r,Room&room,const ServerPlayer*p){
 return p&&p->getRoom()==&room&&!room.roomRuntime()->isClosing()&&r.decisionId!=0
  &&r.viewerObjectName==p->objectName()&&r.worldView.self.objectName==p->objectName()
  &&r.stateRevision==room.roomRuntime()->stateRevision();
}
bool play(const AIRequest&r){return r.kind==AIRequest::Activate
 &&r.reason==CardUseStruct::CARD_USE_REASON_PLAY&&r.pattern.isEmpty();}
bool response(const AIRequest&r,const QString&pattern){return r.kind==AIRequest::UseCard
 &&r.pattern==pattern&&(r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE
 ||r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE);}
bool integer(const QJsonValue&v,int&n){
 if(!v.isDouble())return false;const double d=v.toDouble();
 if(!std::isfinite(d)||d!=std::floor(d)||d<std::numeric_limits<int>::min()||d>std::numeric_limits<int>::max())return false;
 n=int(d);return true;
}
bool sameFace(const AICardView&v,const Card*c,int id){return c&&v.effectiveId==id
 &&v.objectName==c->objectName()&&v.className==c->getClassName()
 &&v.suit==int(c->getSuit())&&v.number==c->getNumber();}
const Card*material(const AIRequest&r,Room&room,const ServerPlayer*p,int id,const QString&zone){
 if(id<0||room.getCardOwner(id)!=p)return nullptr;
 const Card*c=Sanguosha->getCard(id);if(!c)return nullptr;
 if(zone.contains('h')&&p->handCards().contains(id)&&room.getCardPlace(id)==Player::PlaceHand)
  for(const auto&v:r.worldView.handCards)if(sameFace(v,c,id))return c;
 if(zone.contains('e')&&room.getCardPlace(id)==Player::PlaceEquip)
  for(const auto&v:r.worldView.self.equips)if(sameFace(v,c,id))return c;
 // Pile knowledge is required in the original snapshot as well as live state.
 for(const auto&pile:r.worldView.self.piles){
  if((zone=="ShenJian"&&pile.name==zone&&p->getPile(zone).contains(id))
   ||(zone=="he+handpile"&&p->getHandPile().contains(id)&&p->getPile(pile.name).contains(id)))
   for(const auto&v:pile.cards)if(sameFace(v,c,id))return c;
 }
 return nullptr;
}
}
bool aiRound41PlanningConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 static const QSet<QString>fields{"source_key","name","skill","suit","number","subcards"};
 if(!active(r,room,p)||s.size()!=fields.size())return false;
 for(auto it=s.begin();it!=s.end();++it)if(!fields.contains(it.key()))return false;
 if(!s.value("source_key").isString()||!s.value("name").isString()||!s.value("skill").isString()
  ||!s.value("subcards").isArray())return false;
 const auto key=s.value("source_key").toString(),name=s.value("name").toString(),skill=s.value("skill").toString();
 int suit=0,number=0;if(!integer(s.value("suit"),suit)||!integer(s.value("number"),number))return false;
 const QString ownerSkill=skill=="mouduanliangv"?QStringLiteral("mouduanliangg"):skill;
 if(key!="fcDIY."+ownerSkill||(!p->hasSkill(ownerSkill)&&!p->hasLordSkill(ownerSkill)))return false;
 QList<int>ids;for(const auto&v:s.value("subcards").toArray()){
  int id=-1;if(!integer(v,id)||id<0||ids.contains(id))return false;ids.append(id);
 }
 const bool standardFace=suit==int(Card::NoSuit)&&number==0;
 // ai/fcDIY-ai.lua3580,4399,5283. Named response probes, not activation rights.
 if(skill=="fcj_qinguo"||skill=="kjmoushifeng")
  return standardFace&&ids.isEmpty()&&name=="slash"&&response(r,"@@"+skill);
 if(skill=="fcj_leixi"&&name=="duel")
  return standardFace&&ids.isEmpty()&&response(r,"@@fcj_leixiDuel");
 // ai/fcDIY-ai.lua5211/5214: normal source argument is the empty NoSuit/0
 // mouduanliangg SkillCard. Neither hypothesis contains that SkillCard as cost.
 if(skill=="mouduanliangv")return play(r)&&standardFace&&ids.isEmpty()
  &&(name=="supply_shortage"||name=="duel");
 // ai/fcDIY-ai.lua3056/3075/4374. Preserve -1 and SuitToBeDecided;
 // native Card getters derive the actual material face after addSubcard.
 if(skill=="sp_lvezhen"||skill=="fcj_leixi"){
  if(suit!=int(Card::SuitToBeDecided)||number!=-1||ids.size()!=1)return false;
  const Card*c=material(r,room,p,ids.first(),"h");if(!c)return false;
  if(skill=="fcj_leixi")return name=="thunder_slash"&&c->isBlack()&&response(r,"@@fcj_leixiSlash");
  return(name=="snatch"&&c->isRed()&&(play(r)||response(r,"@@sp_lvezhen_SSQY")))
   ||(name=="dismantlement"&&c->isBlack()&&(play(r)||response(r,"@@sp_lvezhen_GHCQ")));
 }
 if(!play(r)||!standardFace)return false;
 // ai/fcDIY-ai.lua832: exact source pile order, INCLUDING the one-card
 // proposal at HP <= 0. Native selected feasibility remains stricter there.
 if(skill=="f_luanshe"){
  const auto pile=p->getPile("ShenJian");const int count=qMin(int(pile.size()),qMax(1,p->getHp()*2));
  if(name!="archery_attack"||pile.size()<=6||ids.size()!=count)return false;
  for(int i=0;i<ids.size();++i)if(ids[i]!=pile[i]||!material(r,room,p,ids[i],"ShenJian"))return false;
  return true;
 }
 if(skill=="sp_hengsaoqianjun"){
  if(name!="slash"||ids.size()!=2)return false;
  const Card*a=material(r,room,p,ids[0],"he+handpile"),*b=material(r,room,p,ids[1],"he+handpile");
  return a&&b&&a->getColor()!=b->getColor();
 }
 if(ids.size()!=1)return false;
 const Card*c=material(r,room,p,ids.first(),skill=="sp_taoyuanyi"?"h":"he+handpile");
 if(!c)return false;
 if(skill=="sp_taoyuanyi")return name=="god_salvation"&&c->getSuit()==Card::Heart;
 if(skill=="sp_qianlixing")return name=="slash"&&c->isRed()&&!c->isKindOf("TrickCard");
 return false;
}
bool aiRound41PlanningPileMaterial(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s,int id){
 return s.value("source_key")==QJsonValue("fcDIY.f_luanshe")
  &&aiRound41PlanningConstruction(r,room,p,s)&&s.value("subcards").toArray().contains(id)
  &&material(r,room,p,id,"ShenJian");
}
