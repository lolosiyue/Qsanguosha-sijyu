#include "ai-strategy-round40-planning.h"
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
bool ownerPlay(const AIRequest&r,Room&room,const ServerPlayer*p){
 return p&&p->getRoom()==&room&&!room.roomRuntime()->isClosing()&&r.decisionId!=0
  &&r.viewerObjectName==p->objectName()&&r.worldView.self.objectName==p->objectName()
  &&r.stateRevision==room.roomRuntime()->stateRevision()&&r.kind==AIRequest::Activate
  &&r.reason==CardUseStruct::CARD_USE_REASON_PLAY&&r.pattern.isEmpty()
  &&p->hasSkill("tyshouli");
}
bool material(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonValue&v){
 if(!ownerPlay(r,room,p)||!v.isArray())return false;
 const auto ids=v.toArray();if(ids.size()!=1||!ids.first().isDouble())return false;
 const double n=ids.first().toDouble(-1);if(n<0||n>std::numeric_limits<int>::max()||n!=std::floor(n))return false;
 const int id=int(n);const auto place=room.getCardPlace(id);
 if(place!=Player::PlaceEquip&&place!=Player::PlaceDelayedTrick)return false;
 auto*holder=room.getCardOwner(id);if(!holder||!holder->isAlive())return false;
 // Read EXACT public e/j source card, preserving inner EquipCard identity.
 // Sanguosha->getCard(id)'s outer wrapper is not substituted for getCards().
 const Card*actual=nullptr;
 for(const Card*c:holder->getCards("ej"))if(c&&c->getEffectiveId()==id){actual=c;break;}
 if(!actual||!actual->isKindOf("OffensiveHorse"))return false;
 const AIPlayerView*view=nullptr;
 if(r.worldView.self.objectName==holder->objectName())view=&r.worldView.self;
 else for(const auto&other:r.worldView.players)if(other.objectName==holder->objectName()){view=&other;break;}
 if(!view||!view->alive)return false;
 const auto&faces=place==Player::PlaceEquip?view->equips:view->judgingArea;
 for(const AICardView&f:faces)if(f.effectiveId==id){
  return f.objectName==actual->objectName()&&f.className==actual->getClassName()
   &&f.suit==int(actual->getSuit())&&f.number==actual->getNumber()
   &&!f.legacyIdentityToken.isEmpty()
   &&f.legacyIdentityToken==aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),actual);
 }
 return false;
}
}
bool aiRound40PlanningConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 static const QSet<QString>fields{"source_key","name","skill","suit","number","subcards"};
 if(s.size()!=fields.size())return false;
 for(auto it=s.begin();it!=s.end();++it)if(!fields.contains(it.key()))return false;
 return s.value("source_key")==QJsonValue("tyshouli")&&s.value("name")==QJsonValue("slash")
  &&s.value("skill")==QJsonValue("tyshouli")&&s.value("suit")==QJsonValue(int(Card::NoSuit))
  &&s.value("number")==QJsonValue(0)&&material(r,room,p,s.value("subcards"));
}
bool aiRound40TyshouliSeed(const AIRequest&r,Room&room,const ServerPlayer*p,
 const QString&skill,const QString&prototype,const QJsonObject&proposal){
 return skill=="tyshouli"&&prototype=="#tyshouli"
  &&proposal.value("user_string")==QJsonValue("slash")
  &&material(r,room,p,proposal.value("subcards"));
}
