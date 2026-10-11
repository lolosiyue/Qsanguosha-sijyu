#include "ai-strategy-round49-guhuo.h"
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
bool current(const AIRequest&r,Room&room,const ServerPlayer*p){
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||!r.decisionId||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||!aiRound44RangeRequestCurrent(room,r,p))return false;
 bool owned=false;for(const auto&i:p->getSkillInstances())if(i.skillName=="qhwindguhuo")owned=true;
 return owned;
}
}
void projectAIRound49Guhuo(AIRequest&r,Room&room,ServerPlayer*p){
 auto props=r.worldView.strategyFacts.value("owner_skill_properties").toObject();props.remove("qhwindguhuoused");
 const auto publish=[&]{if(props.isEmpty())r.worldView.strategyFacts.remove("owner_skill_properties");else r.worldView.strategyFacts.insert("owner_skill_properties",props);};publish();
 if(!current(r,room,p))return;
 const bool play=r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY;
 const bool response=(r.kind==AIRequest::RespondCard||r.kind==AIRequest::UseCard)
  &&(r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE||r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE);
 if(!play&&!response)return;
 // Fixed qhstandard-ai.lua2915/2934 reads the actual actor's current public
 // property. Preserve absent/empty/duplicate name text; no last-writer ledger.
 props.insert("qhwindguhuoused",p->property("qhwindguhuoused").toString());publish();
}
bool aiRound49GuhuoConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 if(!current(r,room,p)||r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY
  ||!r.pattern.isEmpty()||p->isKongcheng()||p->getMark("qhwindguhuoNum")>3)return false;
 const QSet<QString>fields{"source_key","skill","name","suit","number","subcards"};
 if(s.size()!=fields.size())return false;for(auto i=s.begin();i!=s.end();++i)if(!fields.contains(i.key()))return false;
 static const QStringList names{"ex_nihilo","snatch","peach","slash","archery_attack","savage_assault","dismantlement","fire_attack"};
 const QString name=s.value("name").toString();
 if(s.value("source_key")!=QJsonValue("qhwindguhuo")||s.value("skill")!=QJsonValue(QString())
  ||!s.value("name").isString()||!names.contains(name)||!s.value("suit").isDouble()||!s.value("number").isDouble()
  ||!s.value("subcards").isArray()||!s.value("subcards").toArray().isEmpty()
  ||p->property("qhwindguhuoused").toString().split('+').contains(name))return false;
 // Empty-cost fresh source clone has the face of one actually disclosed hand
 // card. Sorting/first acceptable name remain in the full source policy.
 for(int id:p->handCards())if(room.getCardOwner(id)==p&&room.getCardPlace(id)==Player::PlaceHand){
  const Card*c=Sanguosha->getCard(id);if(!c||s.value("suit")!=QJsonValue(int(c->getSuit()))||s.value("number")!=QJsonValue(c->getNumber()))continue;
  for(const auto&v:r.worldView.handCards)if(v.effectiveId==id&&v.objectName==c->objectName()&&v.className==c->getClassName()
   &&v.suit==int(c->getSuit())&&v.number==c->getNumber()&&!v.legacyIdentityToken.isEmpty()
   &&v.legacyIdentityToken==aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c))return true;
 }
 return false;
}
