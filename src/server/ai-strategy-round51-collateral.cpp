#include "ai-strategy-round51-collateral.h"
#include "ai-strategy-round44-certificate.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QHash>
#include <QPointer>
#include <atomic>
namespace {
thread_local QHash<Room*,QList<AIRound51CollateralSource*>> sources;
thread_local QHash<Room*,QList<AIRound51CollateralQuestion*>> questions;
std::atomic<quint64> nextSerial{0};
QString face(const Card*c){return c->objectName()+"|"+c->getClassName()+"|"+QString::number(int(c->getSuit()))+"|"+QString::number(c->getNumber())+"|"+c->getSkillName(false)+"|"+c->toString();}
QString expectedRaw(const CardUseStruct&u){QStringList parts;parts<<u.card->toString();for(auto*t:u.to)parts<<t->objectName();parts<<"tenyearbenxi";return parts.join('+');}
}
struct AIRound51CollateralSource::Data {Room*room;QPointer<ServerPlayer>owner;CardUseStruct use;QString raw,face;bool valid=false,bound=false;};
struct AIRound51CollateralQuestion::Data {Room*room;QPointer<ServerPlayer>owner;AIRound51CollateralSource*source=nullptr;quint64 serial=0;QString prompt;};
struct AIRound51CollateralAccess {
 static AIRound51CollateralQuestion::Data*question(Room&room){auto it=questions.constFind(&room);return it==questions.cend()||it->isEmpty()?nullptr:it->last()->d.get();}
 static bool current(const AIRequest&r,Room&room,ServerPlayer*p){
  auto*q=question(room);if(!q||!q->source||!q->serial||q->serial!=r.round51CollateralQuestionSerial||q->owner!=p||!p||p->getRoom()!=&room)return false;
  const auto it=sources.constFind(&room);if(it==sources.cend()||it->isEmpty()||it->last()!=q->source)return false;
  const auto*s=q->source->d.get();return s->valid&&s->owner==p&&p->property("extra_collateral").toString()==s->raw
   &&room.roomRuntime()&&!room.roomRuntime()->isClosing()&&r.decisionId&&r.viewerObjectName==p->objectName()&&r.worldView.self.objectName==p->objectName()
   &&r.kind==AIRequest::UseCard&&r.pattern=="@@tenyearbenxi!"&&r.prompt==q->prompt&&r.handlingMethod==Card::MethodUse
   &&r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&aiRound44RangeRequestCurrent(room,r,p);
 }
 static QString raw(Room&room){return question(room)->source->d->raw;}
 static bool physical(Room&room,const Card*c){const auto*s=question(room)->source->d.get();
  // Numeric Card_Parse uses the current WrappedCard, not getRealCard. The
  // current face must still be the exact face revealed by this source use.
  return c&&c->getEffectiveId()==s->use.card->getEffectiveId()&&face(c)==s->face;
 }
};
AIRound51CollateralSource::AIRound51CollateralSource(Room&room,ServerPlayer*p,const CardUseStruct&u):d(new Data){
 d->room=&room;d->owner=p;d->use=u;
 if(p&&p->getRoom()==&room&&u.from==p&&u.card&&u.card->isKindOf("Collateral")&&u.to.size()==1){
  bool roster=true;for(auto*t:u.to)if(!t||room.findPlayerByObjectName(t->objectName(),true)!=t)roster=false;
  if(roster){d->face=face(u.card);d->raw=expectedRaw(u);d->valid=p->property("extra_collateral").toString()==d->raw;}
 }sources[&room].append(this);
}
AIRound51CollateralSource::~AIRound51CollateralSource(){auto&v=sources[d->room];if(!v.isEmpty()&&v.last()==this)v.removeLast();else v.clear();if(v.isEmpty())sources.remove(d->room);}
AIRound51CollateralQuestion::AIRound51CollateralQuestion(Room&room,ServerPlayer*p,const QString&pattern,const QString&prompt,int method):d(new Data){
 d->room=&room;d->owner=p;d->prompt=prompt;auto it=sources.find(&room);
 if(it!=sources.end()&&!it->isEmpty()){
  auto*s=it->last();auto*sd=s->d.get();
  if(sd->valid&&!sd->bound&&sd->owner==p&&pattern=="@@tenyearbenxi!"&&prompt=="@tenyearbenxi-extra:"+sd->use.card->objectName()&&method==int(Card::MethodUse)){
   sd->bound=true;d->source=s;d->serial=++nextSerial;
  }
 }questions[&room].append(this);
}
AIRound51CollateralQuestion::~AIRound51CollateralQuestion(){auto&v=questions[d->room];if(!v.isEmpty()&&v.last()==this)v.removeLast();else v.clear();if(v.isEmpty())questions.remove(d->room);}
void projectAIRound51Collateral(AIRequest&r,Room&room,ServerPlayer*p){
 auto facts=r.worldView.strategyFacts.value("tenyear_st_standard").toObject();facts.remove("extra_collateral_current");facts.remove("extra_collateral_card");r.worldView.strategyFacts.insert("tenyear_st_standard",facts);r.round51CollateralQuestionSerial=0;
 auto*q=AIRound51CollateralAccess::question(room);if(!q)return;r.round51CollateralQuestionSerial=q->serial;
 if(!AIRound51CollateralAccess::current(r,room,p)){r.round51CollateralQuestionSerial=0;return;}
 facts.insert("extra_collateral_current",AIRound51CollateralAccess::raw(room));r.worldView.strategyFacts.insert("tenyear_st_standard",facts);
}
bool aiRound51CollateralCurrent(const AIRequest&r,Room&room,ServerPlayer*p,const QString&wire){
 return AIRound51CollateralAccess::current(r,room,p)&&wire==AIRound51CollateralAccess::raw(room).split('+',Qt::KeepEmptyParts).first();
}
const Card*parseAIRound51Collateral(const AIRequest&r,Room&room,ServerPlayer*p,const QString&wire,bool*authorized){
 *authorized=aiRound51CollateralCurrent(r,room,p,wire);if(!*authorized)return nullptr;
 // This is the exact native parser and first literal source token. In
 // particular, never strip a trailing skill or repair truncated + materials.
 const Card*c=Card::Parse(wire);
 if(c&&!c->isVirtualCard()&&!AIRound51CollateralAccess::physical(room,c)){*authorized=false;return nullptr;}
 return c;
}
