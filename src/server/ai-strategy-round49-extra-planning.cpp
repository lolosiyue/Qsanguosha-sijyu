#include "ai-strategy-round49-extra-planning.h"
#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-card-identity.h"
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
bool integer(const QJsonValue&v,int&n){const double d=v.toDouble();if(!v.isDouble()||!std::isfinite(d)||d!=std::floor(d)||d<std::numeric_limits<int>::min()||d>std::numeric_limits<int>::max())return false;n=int(d);return true;}
bool play(const AIRequest&r){return r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY&&r.pattern.isEmpty();}
bool question(const AIRequest&r,AIRequest::DecisionKind kind,const QString&why,const QString&q){return r.kind==kind&&r.choiceOptions.reason==why&&r.choiceOptions.question==q;}
bool response(const AIRequest&r,const QString&pattern){return r.kind==AIRequest::UseCard&&r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&r.pattern==pattern;}
bool slashResponse(const AIRequest&r){return (r.kind==AIRequest::UseCard||r.kind==AIRequest::RespondCard)&&r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&r.pattern.contains("slash",Qt::CaseInsensitive);}
bool instance(const ServerPlayer*p,const QString&name){for(const auto&i:p->getSkillInstances())if(i.skillName==name)return true;return false;}
bool same(const AICardView&v,const Card*c,int id,Room&room,const ServerPlayer*p){return c&&v.effectiveId==id&&c->getEffectiveId()==id&&v.objectName==c->objectName()&&v.className==c->getClassName()&&v.suit==int(c->getSuit())&&v.number==c->getNumber()&&!v.legacyIdentityToken.isEmpty()&&v.legacyIdentityToken==aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c);}
const Card*knownWrapper(const AIRequest&r,Room&room,const ServerPlayer*p,int id){
 const Card*c=Sanguosha->getCard(id);if(!c)return nullptr;
 for(const auto&v:r.worldView.handCards)if(same(v,c,id,room,p))return c;
 for(const auto&v:r.worldView.discardPile)if(same(v,c,id,room,p))return c;
 auto players=r.worldView.players;players.prepend(r.worldView.self);
 for(const auto&row:players){
  for(const auto&v:row.knownCards)if(same(v,c,id,room,p))return c;
  for(const auto&v:row.equips)if(same(v,c,id,room,p))return c;
  for(const auto&v:row.judgingArea)if(same(v,c,id,room,p))return c;
  for(const auto&pile:row.piles)if(row.objectName==p->objectName()||pile.open)for(const auto&v:pile.cards)if(same(v,c,id,room,p))return c;
 }return nullptr;
}
const Card*material(const AIRequest&r,Room&room,const ServerPlayer*p,int id,const QString&zones){
 if(zones.contains('p')){
  const auto complete=r.worldView.strategyFacts.value("round45_hand_pile_ids");
  if(complete.isArray()&&complete.toArray().contains(id)&&p->getHandPile().contains(id))
   if(const auto*c=knownWrapper(r,room,p,id))return c;
 }
 if(room.getCardOwner(id)!=p)return nullptr;
 if(zones.contains('h')&&room.getCardPlace(id)==Player::PlaceHand&&p->handCards().contains(id))
  for(const auto&v:r.worldView.handCards)if(same(v,Sanguosha->getCard(id),id,room,p))return Sanguosha->getCard(id);
 if(zones.contains('e')&&room.getCardPlace(id)==Player::PlaceEquip)
  for(const Card*c:p->getEquips())if(c->getEffectiveId()==id)for(const auto&v:r.worldView.self.equips)if(same(v,c,id,room,p))return c;
 if(zones=="jbbing"&&p->getPile("jbbing").contains(id))
  for(const auto&pile:r.worldView.self.piles)if(pile.name=="jbbing")for(const auto&v:pile.cards)if(same(v,Sanguosha->getCard(id),id,room,p))return Sanguosha->getCard(id);
 return nullptr;
}
bool publicName(const AIRequest&r,const QString&name){return !name.isEmpty()&&r.worldView.strategyFacts.value("pattern_names").toArray().contains(name);}
}
void projectAIRound49ExtraPlanningContext(AIRequest&r,const QVariant&data){
 r.choiceOptions.context.remove("round49_cuixin_name");
 if(!question(r,AIRequest::SkillInvoke,"cuixin","askForSkillInvoke")||data.metaType().id()!=QMetaType::QString)return;
 const auto parts=data.toString().split(':');
 if(parts.size()==2&&(parts[0]=="shangjia"||parts[0]=="xiajia"))r.choiceOptions.context.insert("round49_cuixin_name",parts[1]);
}
bool aiRound49ExtraPlanningConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()||!r.decisionId||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()||!aiRound44RangeRequestCurrent(room,r,p))return false;
 static const QSet<QString>fields{"source_key","name","skill","suit","number","subcards"};
 if(s.size()!=fields.size())return false;for(auto i=s.begin();i!=s.end();++i)if(!fields.contains(i.key()))return false;
 if(!s.value("source_key").isString()||!s.value("name").isString()||!s.value("skill").isString()||!s.value("subcards").isArray())return false;
 const auto key=s.value("source_key").toString(),raw=s.value("skill").toString(),name=s.value("name").toString();
 int suit,number;if(!integer(s.value("suit"),suit)||!integer(s.value("number"),number))return false;
 const auto costs=s.value("subcards").toArray();if(costs.size()>1)return false;
 int id=-1;if(!costs.isEmpty()&&(!integer(costs.first(),id)||id<0))return false;
 const bool defaultFace=suit==int(Card::NoSuit)&&number==0;
 // Every exact response/question below authenticates its own current callback;
 // recipients such as Zhenge need not own the source skill. No added hasSkill,
 // mark, availability, invalidity, jilei or using gate before source evaluation.
 if(key=="juanxia"||key=="tenyearjuanxia")return defaultFace&&costs.isEmpty()&&raw=="_"+key
  &&question(r,AIRequest::Choice,key,"askForChoice")&&r.choiceOptions.choices.contains(name)&&publicName(r,name);
 if(key=="@@jueman!")return defaultFace&&costs.isEmpty()&&raw=="_jueman"&&response(r,key)
  &&r.prompt.split(':').value(1)==name&&p->property("JuemanCardName").toString()==name&&publicName(r,name);
 // Fixed native producer asks @@libang!, whereas the donor registers @@libang.
 // Do not alias these questions or repair the later source string-call error.
 if(key=="@@libang")return defaultFace&&costs.isEmpty()&&name=="slash"&&raw=="_libang"&&response(r,key);
 if(key=="@@zhenge!")return defaultFace&&costs.isEmpty()&&name=="slash"&&raw=="_zhenge"&&response(r,key);
 if(key=="@@pingxiang"||key=="PingxiangCard")return defaultFace&&costs.isEmpty()&&name=="fire_slash"&&raw=="_pingxiang"
  &&(key=="@@pingxiang"?response(r,key):play(r)&&instance(p,"pingxiang"));
 if(key=="#xunshi")return defaultFace&&costs.isEmpty()&&name=="slash"&&raw=="xunshi"&&question(r,AIRequest::PlayersChosen,key,"askForPlayersChosen");
 if(key=="cuixin")return defaultFace&&costs.isEmpty()&&raw=="cuixin"&&question(r,AIRequest::SkillInvoke,key,"askForSkillInvoke")
  &&r.choiceOptions.context.value("round49_cuixin_name")==QJsonValue(name)&&publicName(r,name);
 if(key=="SecondZhanyiViewAsBasicCard"){
  if(!play(r)||!instance(p,"secondzhanyi")||!costs.isEmpty()||(raw!=""&&raw!="secondzhanyi")||!publicName(r,name))return false;
  // The source clone reads the issued SkillCard's derived face, with no costs
  // copied into its hypothetical Basic clone. Its native prototype has one own
  // Basic material: preserve that face union, not arbitrary suit/number input.
  for(const auto&v:r.worldView.handCards){const auto*c=material(r,room,p,v.effectiveId,"hp");if(c&&c->isKindOf("BasicCard")&&suit==int(c->getSuit())&&number==c->getNumber())return true;}
  const auto hp=r.worldView.strategyFacts.value("round45_hand_pile_ids");if(hp.isArray())for(const auto&v:hp.toArray()){int n;if(integer(v,n)){const auto*c=material(r,room,p,n,"p");if(c&&c->isKindOf("BasicCard")&&suit==int(c->getSuit())&&number==c->getNumber())return true;}}
  return false;
 }
 if(!defaultFace)return false;
 QString skill,zones="hep";bool addFirst=false;enum Filter{Any,Equip,WeaponArmor,Black,Red}filter=Any;
 if(key=="@@saowei"){
  if(!response(r,key)||name!="slash"||raw!="saowei")return false;zones="h";
 }else{
  if(key=="tenyearjinggong"||key=="kanpodz"){
   skill=key;addFirst=true;zones=key=="kanpodz"?"hp":"hep";filter=key=="tenyearjinggong"?Equip:Any;
   if(name!="slash"||(!play(r)&&!slashResponse(r)))return false;
  }else if(key=="qiaoli"){skill=key;filter=Equip;if(!play(r)||name!="duel")return false;
  }else if(key=="tenyearlingyin"){skill=key;zones="he";filter=WeaponArmor;if(!play(r)||name!="duel")return false;
  }else if(key=="jibing"){skill=key;zones="jbbing";if(!play(r)||name!="slash")return false;
  }else if(key=="spniluan"||key=="SpNiluanCard"){skill="spniluan";filter=Black;if(!play(r)||name!="slash")return false;
  }else if(key=="WeiwuCard"){skill="weiwu";filter=Red;if(!play(r)||name!="snatch")return false;
  }else return false;
  if(!instance(p,skill)||(raw!=skill&&!(addFirst&&raw.isEmpty()&&costs.size()==1)))return false;
 }
 if(costs.isEmpty())return !addFirst; // setSkillName before addSubcard.
 const Card*c=material(r,room,p,id,zones);if(!c)return false;
 if(filter==Equip&&c->getTypeId()!=Card::TypeEquip)return false;
 if(filter==WeaponArmor&&!c->isKindOf("Weapon")&&!c->isKindOf("Armor"))return false;
 if(filter==Black&&!c->isBlack())return false;
 if(filter==Red&&!c->isRed())return false;
 return true;
}
