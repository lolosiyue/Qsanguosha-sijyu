#include "ai-strategy-round49-maotu-yinhu.h"
#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-card-identity.h"
#include "ai-strategy-projection.h"
#include "ai-strategy-public-disclosure.h"
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
bool current(const AIRequest&r,Room&room,const ServerPlayer*p){
 return p&&p->getRoom()==&room&&room.roomRuntime()&&!room.roomRuntime()->isClosing()
  &&r.decisionId&&r.viewerObjectName==p->objectName()&&r.worldView.self.objectName==p->objectName()
  &&room.getPlayers().contains(const_cast<ServerPlayer*>(p))&&aiRound44RangeRequestCurrent(room,r,p);
}
bool own(const ServerPlayer*p,const QString&s){for(const auto&i:p->getSkillInstances())if(i.skillName==s)return true;return false;}
bool play(const AIRequest&r){return r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY;}
bool prompt(const AIRequest&r,const QString&s){
 return r.kind==AIRequest::UseCard&&r.pattern=="@@"+s
  &&(r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE||r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE);
}
bool scope(const AIRequest&r,Room&room,const ServerPlayer*p,const QString&s){
 if(!current(r,room,p))return false;
 if(s=="mtweiqie")return r.kind==AIRequest::RespondCard&&r.pattern=="@@mtweiqie"
   &&r.prompt.section(':',0,0)=="@mtweiqie"&&r.handlingMethod==Card::MethodNone;
 if(s=="mtxianzheng"||s=="yhshanhai")return prompt(r,s);
 if(s=="yhxinghan")return play(r)&&own(p,s);
 if(s=="yhduwei")return (play(r)||prompt(r,s))&&own(p,s);
 if(s=="mtzhihe")return (play(r)||r.kind==AIRequest::RespondCard||r.kind==AIRequest::UseCard)&&own(p,s);
 return false;
}
bool integer(const QJsonValue&v,int&n){
 const double d=v.toDouble(-1);if(!v.isDouble()||!std::isfinite(d)||d<0||d!=std::floor(d)||d>std::numeric_limits<int>::max())return false;n=int(d);return true;
}
bool exact(const AICardView&v,const Card*c,Room&room,const ServerPlayer*p,int id){
 return c&&v.effectiveId==id&&c->getEffectiveId()==id&&v.objectName==c->objectName()
  &&v.className==c->getClassName()&&v.suit==int(c->getSuit())&&v.number==c->getNumber()
  &&!v.legacyIdentityToken.isEmpty()&&v.legacyIdentityToken==aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c);
}
const Card*material(const AIRequest&r,Room&room,const ServerPlayer*p,int id,bool handOnly){
 if(room.getCardOwner(id)!=p)return nullptr;
 if(room.getCardPlace(id)==Player::PlaceHand&&p->handCards().contains(id)){
  const Card*c=Sanguosha->getCard(id);for(const auto&v:r.worldView.handCards)if(exact(v,c,room,p,id))return c;
 }
 if(handOnly)return nullptr;
 if(room.getCardPlace(id)==Player::PlaceEquip)for(const Card*c:p->getEquips())if(c->getEffectiveId()==id)
  for(const auto&v:r.worldView.self.equips)if(exact(v,c,room,p,id))return c;
 if(p->getHandPile().contains(id))for(const auto&pile:r.worldView.self.piles)for(const auto&v:pile.cards)
  if(v.effectiveId==id){const Card*c=Sanguosha->getCard(id);if(exact(v,c,room,p,id))return c;}
 return nullptr;
}
QJsonArray shown(const AIRequest&r,Room&room,const ServerPlayer*p,bool&known,QJsonObject *disclosedFaces=nullptr){
 known=false;if(!scope(r,room,p,"mtweiqie"))return{};
 const auto row=readAIStrategyNotifiedPile(room,const_cast<ServerPlayer*>(p),"#mtweiqie");
 if(!row.value("ordered_ids").isArray())return{};
 const auto ids=row.value("ordered_ids").toArray();const auto locations=row.value("locations").toObject();QJsonObject faces;
 // notifyMoveToPile writes the exact old tag as QVariantList. Order and
 // duplicates are authenticated by the actual notification, never a subset.
 QJsonArray tag;for(const auto&v:p->getTag("mtweiqieForAI").toList())tag.append(v.toInt());
 if(ids!=tag)return{};
 for(const auto&v:ids){int id;if(!integer(v,id))return{};
  // The notification provides order; the actual public table move provides
  // the current wrapped face. Printed-face equality alone grants no knowledge.
  const auto disclosure=readAIStrategyPublicDisclosure(room,const_cast<ServerPlayer*>(p),id);
  if(!disclosure.value("known").toBool()||disclosure.value("place")!=QJsonValue(int(Player::PlaceTable)))return{};
  const auto face=disclosure.value("card").toObject();const auto loc=locations.value(QString::number(id)).toObject();
  if(face.isEmpty()||loc.value("place")!=QJsonValue(int(Player::PlaceTable))||room.getCardPlace(id)!=Player::PlaceTable)return{};
  const Card*c=Sanguosha->getCard(id);
  if(!c||face.value("name")!=QJsonValue(c->objectName())||face.value("class_name")!=QJsonValue(c->getClassName())
   ||face.value("suit")!=QJsonValue(int(c->getSuit()))||face.value("number")!=QJsonValue(c->getNumber()))return{};
  faces.insert(QString::number(id),face);
 }
 if(!current(r,room,p))return{};if(disclosedFaces)*disclosedFaces=faces;known=true;return ids;
}
}
void projectAIRound49MaotuYinhu(AIRequest&r,Room&room,ServerPlayer*p){
 auto props=r.worldView.strategyFacts.value("owner_skill_properties").toObject();
 props.remove("yhduwei_damage_card");props.remove("YHShanhaiGetIds");
 if(scope(r,room,p,"yhduwei"))props.insert("yhduwei_damage_card",p->property("yhduwei_damage_card").toString());
 if(scope(r,room,p,"yhshanhai"))props.insert("YHShanhaiGetIds",p->property("YHShanhaiGetIds").toString());
 if(props.isEmpty())r.worldView.strategyFacts.remove("owner_skill_properties");else r.worldView.strategyFacts.insert("owner_skill_properties",props);
 auto m=r.worldView.strategyFacts.value("maotu").toObject();m.remove("mtweiqie_ids");m.remove("mtweiqie_cards");bool ok=false;QJsonObject cards;const auto ids=shown(r,room,p,ok,&cards);
 if(ok){m.insert("mtweiqie_ids",ids);m.insert("mtweiqie_cards",cards);}
 if(m.isEmpty())r.worldView.strategyFacts.remove("maotu");else r.worldView.strategyFacts.insert("maotu",m);
}
bool aiRound49MaotuYinhuConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 const QSet<QString>fields{"source_key","skill","name","suit","number","subcards"};
 if(s.size()!=fields.size())return false;for(auto it=s.begin();it!=s.end();++it)if(!fields.contains(it.key()))return false;
 const auto skill=s.value("source_key").toString();
 if(s.value("skill")!=QJsonValue(skill)||!s.value("name").isString()||s.value("suit")!=QJsonValue(int(Card::NoSuit))
  ||s.value("number")!=QJsonValue(0)||!s.value("subcards").isArray()||!scope(r,room,p,skill))return false;
 const auto ids=s.value("subcards").toArray();if(ids.isEmpty())return false;
 QSet<int>seen;QList<const Card*>materials;
 if(skill=="mtweiqie"){
  bool known=false;const auto all=shown(r,room,p,known);if(!known||s.value("name")!=QJsonValue("slash"))return false;
  QList<int> remaining;for(const auto&v:all)remaining.append(v.toInt());
  for(const auto&v:ids){int id;if(!integer(v,id)||!remaining.removeOne(id))return false;}
  // Preserve repeated source material only up to the actual notification's
  // multiplicity. This readonly path never changes selected native gates.
  return current(r,room,p);
 }
 for(const auto&v:ids){int id;if(!integer(v,id)||seen.contains(id))return false;seen.insert(id);
  const Card*c=material(r,room,p,id,skill=="mtzhihe"||skill=="yhshanhai");if(!c)return false;materials<<c;}
 if(skill=="mtzhihe"){
  QSet<int>suits;bool offered=false;const auto pileIds=p->getPile("yhjyye");
  for(int id:pileIds){const Card*c=nullptr;
   for(const auto&pile:r.worldView.self.piles)if(pile.name=="yhjyye")for(const auto&v:pile.cards)if(v.effectiveId==id){
    const Card*live=Sanguosha->getCard(id);if(exact(v,live,room,p,id))c=live;}
   if(!c)return false;suits.insert(int(c->getSuit()));
   if((c->isKindOf("BasicCard")||c->isNDTrick())&&s.value("name")==QJsonValue(c->objectName()))offered=true;
  }
  if(ids.size()!=qMax(1,4-suits.size()))return false;
  for(const Card*c:materials)if(c->getSuit()!=materials.first()->getSuit())return false;
  // Fill names come from the witnessed pile. Guhuo's requested cname is a
  // public printing name, but its native selected declaration stays separate.
  if(play(r)&&!offered)return false;
  if(!play(r)&&!r.worldView.strategyFacts.value("pattern_names").toArray().contains(s.value("name")))return false;
 }else{
  if(ids.size()!=1)return false;
  if(skill=="yhduwei"){
   const auto name=r.worldView.strategyFacts.value("owner_skill_properties").toObject().value("yhduwei_damage_card");
   if(!name.isString()||name!=s.value("name")||name.toString()!=p->property("yhduwei_damage_card").toString())return false;
  }else{
   if(s.value("name")!=QJsonValue("slash"))return false;
   if(skill=="yhshanhai"){
    const auto value=r.worldView.strategyFacts.value("owner_skill_properties").toObject().value("YHShanhaiGetIds");
    if(!value.isString()||value.toString()!=p->property("YHShanhaiGetIds").toString()
     ||!value.toString().split('+').contains(QString::number(*seen.begin())))return false;
   }
  }
 }
 // No card-limited/type/availability test here: source constructs before those
 // tests, notably yhxinghan's source non-Basic versus native Basic mismatch.
 return current(r,room,p);
}
bool aiRound49MaotuYinhuPublicMaterial(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 return s.value("source_key")==QJsonValue("mtweiqie")&&aiRound49MaotuYinhuConstruction(r,room,p,s);
}
