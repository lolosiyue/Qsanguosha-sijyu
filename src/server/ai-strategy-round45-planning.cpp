#include "ai-strategy-round56-projection.h"
#include "ai-strategy-round56-maxcards.h"
#include "ai-strategy-round45-planning.h"
#include "ai-strategy-round46-planning-slots.h"
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
bool response(const AIRequest&r,const QString&pattern){return (r.kind==AIRequest::UseCard||r.kind==AIRequest::RespondCard)&&r.pattern==pattern&&(r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE||r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE);}
bool same(const AICardView&v,const Card*c,int id,Room&room,const ServerPlayer*p){return c&&v.effectiveId==id&&v.objectName==c->objectName()&&v.className==c->getClassName()&&v.suit==int(c->getSuit())&&v.number==c->getNumber()&&!v.legacyIdentityToken.isEmpty()&&v.legacyIdentityToken==aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c);}
const Card*visibleMaterial(const AIWorldView&w,Room&room,const ServerPlayer*p,int id){
 const Card*c=Sanguosha->getCard(id);if(!c)return nullptr;
 for(const auto&v:w.handCards)if(same(v,c,id,room,p))return c;
 for(const auto&v:w.discardPile)if(same(v,c,id,room,p))return c;
 QList<AIPlayerView>players=w.players;players.prepend(w.self);
 for(const auto&v:players){
  for(const auto&k:v.knownCards)if(same(k,c,id,room,p))return c;
  for(const auto&k:v.equips)if(same(k,c,id,room,p))return c;
  for(const auto&k:v.judgingArea)if(same(k,c,id,room,p))return c;
  for(const auto&pile:v.piles)if(v.objectName==p->objectName()||pile.open)for(const auto&k:pile.cards)if(same(k,c,id,room,p))return c;
 }
 return nullptr;
}
const Card*material(const AIRequest&r,Room&room,const ServerPlayer*p,int id,const QString&zones){
 const auto proof=r.worldView.strategyFacts.value("round45_hand_pile_ids");
 if(zones.contains('p')&&proof.isArray()&&proof.toArray().contains(id)&&p->getHandPile().contains(id))
  return visibleMaterial(r.worldView,room,p,id);
 if(room.getCardOwner(id)!=p)return nullptr;const Card*c=Sanguosha->getCard(id);if(!c)return nullptr;
 if(zones.contains('h')&&room.getCardPlace(id)==Player::PlaceHand&&p->handCards().contains(id))for(const auto&v:r.worldView.handCards)if(same(v,c,id,room,p))return c;
 // Source getCards("he") enumerates Player::getEquips() inner objects.
 // The wrapper returned by Engine::getCard(id) is a different live identity.
 // This is ONLY the authenticated current owner's e branch, not permission
 // to substitute an inner object's face for any hand-pile getCard(id) read.
 if(zones.contains('e')&&room.getCardPlace(id)==Player::PlaceEquip)
  for(const Card*equipped:p->getEquips())if(equipped->getEffectiveId()==id)
   for(const auto&v:r.worldView.self.equips)if(same(v,equipped,id,room,p))return equipped;
 return nullptr;
}
bool publicName(const AIRequest&r,const QString&name){return !name.isEmpty()&&r.worldView.strategyFacts.value("pattern_names").toArray().contains(name);}
bool godMaterial(const AIRequest&r,Room&room,const ServerPlayer*p,int id){
 Q_UNUSED(room);
 return aiRound46PlanningGodMaterial(r,const_cast<ServerPlayer*>(p),id)!=nullptr;
}

bool shisheng(const ServerPlayer*p){if(p->hasSkill("shisheng"))return true;for(int i=1;i<=10;++i)if(p->hasSkill("shisheng"+QString::number(i)))return true;return false;}
}
bool aiRound45PlanningConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 if(!p||p->getRoom()!=&room||room.roomRuntime()->isClosing()||!r.decisionId||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()||!aiRound44RangeRequestCurrent(room,r,p))return false;
 static const QSet<QString>fields{"source_key","name","skill","suit","number","subcards"};
 const auto key=s.value("source_key").toString(),name=s.value("name").toString(),raw=s.value("skill").toString();
 const bool recast=key=="newgenerals._xdyifu";
 if(s.size()!=fields.size()+int(recast))return false;for(auto i=s.begin();i!=s.end();++i)if(!fields.contains(i.key())&&!(recast&&i.key()=="can_recast"))return false;
 if(!s.value("source_key").isString()||!s.value("name").isString()||!s.value("skill").isString()||!s.value("subcards").isArray()||(recast&&s.value("can_recast")!=QJsonValue(false)))return false;
 int suit=0,number=0;if(!integer(s.value("suit"),suit)||!integer(s.value("number"),number))return false;
 QList<int>ids;for(const auto&v:s.value("subcards").toArray()){int id=-1;if(!integer(v,id)||id<0)return false;ids<<id;}
 // No canActivate/canSelect/using/jilei checks: these are source hypotheses,
 // including proposals which the source never chooses. The later native bind
 // independently checks the current real provider, ordered prefix and product.
 if(key=="mountainrumour.shisheng_preview"){
  if(raw!=""||suit!=int(Card::NoSuit)||number!=-1||!ids.isEmpty()||!shisheng(p))return false;
  static const QSet<QString>names{"slash","fire_slash","thunder_slash","jink","peach","analeptic","nullification","snatch","dismantlement","collateral","ex_nihilo","duel","fire_attack","amazing_grace","savage_assault","archery_attack","god_salvation","iron_chain"};
  if(play(r))return names.contains(name);
  const auto reason=r.choiceOptions.reason;bool choice=reason=="shisheng_card";for(int i=1;i<=10;++i)choice=choice||reason=="shisheng_sc_re"+QString::number(i);
  return r.kind==AIRequest::Choice&&choice&&name!="esc"&&r.choiceOptions.choices.contains(name)&&publicName(r,name);
 }
 if(key=="mountainrumour.shisheng"){
  if(!shisheng(p)||!response(r,"@@shisheng")||raw!="shisheng"||suit!=int(Card::SuitToBeDecided)||number!=0||ids.size()>3||name.isEmpty()||name!=p->property("shisheng_sel").toString())return false;
  QSet<int>seen;for(int id:ids){if(seen.contains(id)||!material(r,room,p,id,"he"))return false;seen.insert(id);}
  // x is viewer strategy memory, not a gameplay mark: permit the exact source
  // recipe union 0..3 distinct HE materials. No trust in x for native payment.
  return true;
 }
 if(key!="newgenerals."+raw||suit!=int(Card::NoSuit)||number!=0||!publicName(r,name))return false;
 QString skill=raw;if(raw=="_xdyifu")skill="xdyifuvs";else if(raw=="_ofyouchong")skill="ofyouchongvs";else if(raw=="_ofsifen")skill="ofsifen";
 if(!p->hasSkill(skill)&&!p->hasSkill(skill+"&"))return false;
 QString zones="hep";bool admitted=false;
 if(raw=="keolkenshang")admitted=ids.size()>=2&&ids.size()<=999;
 else if(raw=="keolyilie"){zones="h";admitted=ids.size()==2;}
 else if(raw=="moujiang"){zones="h";admitted=play(r)&&name=="duel"&&!ids.isEmpty()&&ids==p->handCards();}
 else if(raw=="mobilexiaoni"){zones="he";admitted=play(r)&&ids.size()==1;}
 else if(raw=="keolweilin")admitted=ids.size()==1;
 else if(raw=="_xdyifu"){zones="h";admitted=play(r)&&ids.size()==1&&(name=="lightning"||name=="iron_chain"||name==p->property("Suijiyingbian").toString());}
 else if(raw=="ofsifen")admitted=play(r)&&name=="duel"&&!ids.isEmpty()&&ids.size()==p->getMark("ofsifenRed-PlayClear");
 else if(raw=="_ofsifen"){zones="h";admitted=response(r,"@@ofsifen!")&&name=="duel"&&(ids.size()==1||ids.size()==(p->getHandcardNum()+1)/2)&&!ids.isEmpty();}
 else if(raw=="ofjunwei")admitted=name=="nullification"&&ids.size()==2; // Source does not compare colors.
 else if(raw=="_ofyouchong"){zones="he";admitted=response(r,"@@ofyouchong")&&ids.size()==3&&name==p->property("ofyouchongCN").toString();}
 else if(raw=="godshenji")return r.kind==AIRequest::PlayerChosen&&r.choiceOptions.reason=="godshenji"&&ids.size()==1&&p->getTag("godshenjiCNS").toString().split('+').contains(name)&&godMaterial(r,room,p,ids.first());
 if(!admitted)return false;
 QMap<int,int>seen,limit;
 if(aiRound45PlanningRepeatedMaterials(s)){
  for(const Card*c:p->getCards("he"))++limit[c->getEffectiveId()];
  for(int id:p->getHandPile())++limit[id];
 }
 for(int id:ids)if(++seen[id]>(aiRound45PlanningRepeatedMaterials(s)?limit.value(id):1))return false;
 QList<const Card*>cards;for(int id:ids){const auto*c=material(r,room,p,id,zones);if(!c)return false;cards<<c;}
 if(raw=="keolyilie"&&cards[0]->getColor()!=cards[1]->getColor())return false;
 if(raw=="_xdyifu"&&!cards[0]->isKindOf("BasicCard"))return false;
 if(raw=="ofsifen")for(const auto*c:cards)if(!c->isRed())return false;
 return true;
}
bool aiRound45PlanningProduct(const QJsonObject&s,const Card&c){
 const auto skill=s.value("skill").toString();
 // Guhuo callback also constructs its caller-supplied public toname, without
 // the fill callback's Basic filter. Do not narrow that readonly source union.
 if(skill=="mobilexiaoni")return c.isDamageCard()&&!c.isKindOf("DelayedTrick");
 // keolweilin cardsview uses dummyCard(class_name), even when a caller asks a
 // class its selected native provider does not support. Do not pre-filter it.
 return true;
}
QJsonValue aiRound45PlanningHandPile(const AIWorldView&w,Room&room,const ServerPlayer*p){
 if(!p||p->getRoom()!=&room||w.self.objectName!=p->objectName())return false;
 QJsonArray ids;
 for(int id:p->getHandPile()){
  if(!visibleMaterial(w,room,p,id))return false;
  ids.append(id); // Native order/duplicates; no partial list or hidden member ID.
 }
 return ids;
}
void projectAIRound45PlanningFacts(AIWorldView&w,Room&room,const ServerPlayer*p){
 if(!p||p->getRoom()!=&room||w.self.objectName!=p->objectName())return;
 w.strategyFacts.insert("round45_hand_pile_ids",aiRound45PlanningHandPile(w,room,p));
 auto facts=w.strategyFacts.value("newgenerals").toObject();
 auto by=facts.value("player_properties").toObject();auto own=by.value(p->objectName()).toObject();
 // Fixed newgenerals-ai6138/6936 reads only self.player's values. QVariant's
 // absent toString() is the source's known empty string, never a foreign guess.
 if(([&] { std::unique_ptr<AIRound56WorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIRound56ProjectionScope(room,gameLua))
            source.reset(new AIRound56WorldSkillQueriesScope(room,const_cast<ServerPlayer *>(p),gameLua));
        return p->hasSkill("xdyifuvs"); }())||([&] { std::unique_ptr<AIRound56WorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIRound56ProjectionScope(room,gameLua))
            source.reset(new AIRound56WorldSkillQueriesScope(room,const_cast<ServerPlayer *>(p),gameLua));
        return p->hasSkill("xdyifuvs&"); }()))own.insert("Suijiyingbian",p->property("Suijiyingbian").toString());
 if(([&] { std::unique_ptr<AIRound56WorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIRound56ProjectionScope(room,gameLua))
            source.reset(new AIRound56WorldSkillQueriesScope(room,const_cast<ServerPlayer *>(p),gameLua));
        return p->hasSkill("ofyouchongvs"); }())||([&] { std::unique_ptr<AIRound56WorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIRound56ProjectionScope(room,gameLua))
            source.reset(new AIRound56WorldSkillQueriesScope(room,const_cast<ServerPlayer *>(p),gameLua));
        return p->hasSkill("ofyouchongvs&"); }()))own.insert("ofyouchongCN",p->property("ofyouchongCN").toString());
 by.insert(p->objectName(),own);facts.insert("player_properties",by);w.strategyFacts.insert("newgenerals",facts);
}

bool aiRound45PlanningRepeatedMaterials(const QJsonObject&s){
 const auto raw=s.value("skill").toString();return s.value("source_key").toString()=="newgenerals."+raw&&(raw=="keolkenshang"||raw=="ofsifen"||raw=="ofjunwei");
}
