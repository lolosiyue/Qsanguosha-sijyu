#include "ai-strategy-round37-planning.h"
#include "ai-strategy-room-ledger.h"
#include "ai-strategy-round35-choice.h"
#include "ai-strategy-flag-lifecycle.h"
#include "ai-runtime.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QHash>
#include <QObject>
#include <cmath>
namespace {
struct Row{qint64 epoch=0;bool value=false;bool known=false;};
class Shadow final:public AIRoomLedger{public:explicit Shadow(Room&r):AIRoomLedger(r){}bool initialized=false,baselineKnown=true;QHash<const ServerPlayer*,Row>owners;};
Shadow*ledger(Room&r){for(auto*c:r.children())if(auto*l=dynamic_cast<Shadow*>(c))return l;return new Shadow(r);}
QJsonObject fail(const char*why){return{{"kind","original_crossbow_shadow"},{"known",false},{"reason",QString::fromLatin1(why)}};}
bool valid(Room&r,const AIRequest&q,const ServerPlayer*p){return p&&p->getRoom()==&r&&r.getAllPlayers(true).contains(const_cast<ServerPlayer*>(p))&&r.roomRuntime()&&q.viewerObjectName==p->objectName()&&q.stateRevision==r.roomRuntime()->stateRevision()&&aiRound35OriginalQuestion(q);}
qint64 epoch(Room&r,const ServerPlayer*p){AIWorldView w;w.self.objectName=p->objectName();projectAIStrategyFlagLifecycle(w,r);return qint64(w.strategyFacts.value("strategy_flag_clear_epochs").toObject().value(p->objectName()).toDouble());}
Row&row(Room&r,const ServerPlayer*p){auto*l=ledger(r);const auto e=epoch(r,p);auto it=l->owners.find(p);if(it==l->owners.end()){
 QObject::connect(p,&QObject::destroyed,l,[l,p]{l->owners.remove(p);}, Qt::DirectConnection);const bool baseline=l->initialized&&l->baselineKnown;
 return l->owners.insert(p,Row{e,baseline?p->hasFlag("AI_needCrossbow"):false,baseline}).value();
 }if(it->epoch!=e)*it=Row{e,false,true};return it.value();}
QJsonObject answer(const Row&r){return r.known?QJsonObject{{"kind","original_crossbow_shadow"},{"known",true},{"value",r.value}}:fail("untracked legacy policy interrupted owner Crossbow observation");}
const Card*sourceCard(Room&r,const AIRequest&q,ServerPlayer*p,const QJsonObject&s){
 if(s.size()!=1)return nullptr;
 if(s.value("recipe_key").isString())return r.roomRuntime()->ai().readonlyCard(q,p,s.value("recipe_key").toString());
 if(!s.value("id").isDouble())return nullptr;const double raw=s.value("id").toDouble();
 if(!std::isfinite(raw)||std::floor(raw)!=raw||raw<0||raw>=Sanguosha->getCardCount())return nullptr;const int id=int(raw);
 bool disclosed=false;for(const auto&c:q.worldView.handCards)if(c.effectiveId==id)disclosed=true;
 for(const auto&c:q.worldView.self.equips)if(c.effectiveId==id)disclosed=true;
 if(!disclosed)return nullptr;for(const Card*c:p->getCards("he"))if(c&&c->getEffectiveId()==id)return c;return nullptr;
}
}
void initializeAIRound37CrossbowShadow(Room&r){auto*l=ledger(r);if(l->initialized)return;l->initialized=true;for(auto*p:r.getAllPlayers(true))row(r,p);}
void invalidateAIRound37CrossbowShadow(Room&r){auto*l=ledger(r);l->baselineKnown=false;for(auto i=l->owners.begin();i!=l->owners.end();++i)i->known=false;}
QJsonObject readAIRound37CrossbowShadow(Room&r,const AIRequest&q,const ServerPlayer*p){if(!valid(r,q,p))return fail("Original Crossbow owner/request mismatch");return answer(row(r,p));}
QJsonObject writeAIRound37CrossbowShadow(Room&r,const AIRequest&q,ServerPlayer*p,const QJsonObject&s){
 if(!valid(r,q,p)||(q.kind!=AIRequest::Activate&&q.kind!=AIRequest::UseCard))return fail("Original Crossbow writer question mismatch");
 const Card*c=sourceCard(r,q,p,s);if(!c||!c->isKindOf("Collateral"))return fail("Original Crossbow source is not current owned/retained Collateral");
 auto&x=row(r,p);x.known=true;x.value=true;return answer(x);
}
