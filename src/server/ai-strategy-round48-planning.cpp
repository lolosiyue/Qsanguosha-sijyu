#include "ai-strategy-round48-planning.h"
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
namespace {
bool same(const AICardView &v,const Card *c,Room &room,const ServerPlayer *p){
 return c&&v.effectiveId==c->getEffectiveId()&&v.objectName==c->objectName()
  &&v.className==c->getClassName()&&v.suit==int(c->getSuit())&&v.number==c->getNumber()
  &&!v.legacyIdentityToken.isEmpty()&&v.legacyIdentityToken==aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c);
}
const Card *observed(const AIRequest &r,Room &room,const ServerPlayer *p,int id){
 if(id<0||id>=Sanguosha->getCardCount())return nullptr;
 const Card *c=Sanguosha->getCard(id);if(!c)return nullptr;
 for(const auto &v:r.worldView.handCards)if(same(v,c,room,p))return c;
 for(const auto &v:r.worldView.discardPile)if(same(v,c,room,p))return c;
 QList<AIPlayerView> players=r.worldView.players;players.prepend(r.worldView.self);
 for(const auto &player:players){
  for(const auto &v:player.knownCards)if(same(v,c,room,p))return c;
  for(const auto &v:player.equips)if(same(v,c,room,p))return c;
  for(const auto &v:player.judgingArea)if(same(v,c,room,p))return c;
  for(const auto &pile:player.piles)if(player.objectName==p->objectName()||pile.open)
   for(const auto &v:pile.cards)if(same(v,c,room,p))return c;
 }
 return nullptr;
}
}
bool aiRound48PlanningConstruction(const AIRequest &r,Room &room,const ServerPlayer *p,const QJsonObject &s){
 if(!p||!room.roomRuntime()||p->getRoom()!=&room||room.roomRuntime()->isClosing()||!r.decisionId
  ||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||!aiRound44RangeRequestCurrent(room,r,p))return false;
 const QString key=s.value("source_key").toString();
 const bool fang=key=="newgenerals._xdfangchan",yan=key=="newgenerals._yantaoluan";
 if(!fang&&!yan)return false;
 const QSet<QString>fields{"source_key","name","skill","suit","number","subcards"};
 if(s.size()!=fields.size()+int(fang))return false;
 for(auto i=s.begin();i!=s.end();++i)if(!fields.contains(i.key())&&!(fang&&i.key()=="can_recast"))return false;
 if(!s.value("name").isString()||s.value("skill")!=QJsonValue(fang?"_xdfangchan":"_yantaoluan")
  ||s.value("suit")!=QJsonValue(int(Card::NoSuit))||s.value("number")!=QJsonValue(0)
  ||!s.value("subcards").isArray()||!s.value("subcards").toArray().isEmpty())return false;
 if(yan)return r.kind==AIRequest::Choice&&r.choiceOptions.reason=="yantaoluan"
  &&s.value("name")==QJsonValue("fire_slash");
 if(r.kind!=AIRequest::UseCard||r.pattern!="@@xdfangchan"
  ||(r.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE&&r.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
  ||s.value("can_recast")!=QJsonValue(false))return false;
 const int id=p->getMark("xdfangchanId");
 // Match the actual owner mark read by PlayerView:getStrategyMark. There is
 // no newly invented hasSkill gate: native source can yield to AG/invoke before
 // asking this exact question and does not recheck skill ownership afterward.
 const auto &marks=r.worldView.self.publicMarks;
 if((!marks.contains("xdfangchanId")&&!r.worldView.self.marksComplete)
  ||marks.value("xdfangchanId",0)!=id)return false;
 const auto strategy=r.worldView.modePolicy.value("strategy_marks").toObject()
  .value(p->objectName()).toObject();
 if(strategy.contains("xdfangchanId")&&strategy.value("xdfangchanId")!=QJsonValue(id))return false;
 // The native producer marks an actual public discard/AG card. Require its
 // CURRENT independent full-face/identity observation; the mark is not a hidden
 // ID oracle. An inner Equipment face never authenticates a different wrapper.
 const Card *c=observed(r,room,p,id);
 return c&&s.value("name")==QJsonValue(c->objectName());
}
QJsonObject aiRound48YantaoluanSlash(const AIRequest &r,Room &room,const ServerPlayer *p,const QJsonObject &s,const Card *c){
 if(s.value("source_key")!=QJsonValue("newgenerals._yantaoluan")
  ||!aiRound48PlanningConstruction(r,room,p,s)||!c||c->objectName()!="fire_slash"
  ||c->getSkillName(false)!="_yantaoluan"||!c->getSubcards().isEmpty()
  ||c->getSuit()!=Card::NoSuit||c->getNumber()!=0)return {};
 // This is the actual native Choice's typed Judge who, already projected by
 // projectDecisionContext from data. No arbitrary Lua target or foreign actor.
 QJsonObject context=r.worldView.decisionContext;
 for(auto i=r.choiceOptions.context.begin();i!=r.choiceOptions.context.end();++i)context.insert(i.key(),i.value());
 const QString who=context.value("judge").toObject().value("who").toString();
 ServerPlayer *target=room.findPlayerByObjectName(who,true);if(!target)return {};
 const bool value=p->canSlash(target,c,false,0);
 if(!aiRound44RangeRequestCurrent(room,r,p))return {};
 return {{"actor",p->objectName()},{"target",who},{"distance_limit",false},{"range_fix",0},{"value",value}};
}
