#include "ai-strategy-round54-jijiang.h"
#include "ai.h"
#include "ai-runtime.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "skill.h"
#include "card.h"
#include "card-lifetime-manager.h"
#include <QHash>
#include <QJsonArray>
#include <QPointer>
#include <limits>

namespace {
// Strategy memory stays in the isolated observer VM. This operation storage
// also retains weak constructor origins for one fixed, publicly declared list.
struct ConstructedSkillNames {std::weak_ptr<const CardLifetimeToken>token;QPointer<const Card>card;};
struct State final:QObject {
 explicit State(Room&r):QObject(&r),room(r){}
 Room&room;quint64 next=0;QHash<const Card*,ConstructedSkillNames>constructedSkills;
};
State*state(Room&r,bool create=false){
 for(auto*c:r.children())if(auto*s=dynamic_cast<State*>(c))return s;
 return create?new State(r):nullptr;
}
thread_local AIRound54JijiangScope::Data*active=nullptr;
thread_local AIRound54JijiangLiege::Data*liegeFrame=nullptr;
thread_local AIRound54JijiangEvent::Data*eventFrame=nullptr;
QJsonObject no(const char*why){return{{"kind","ol_jijiang_state"},{"known",false},{"reason",why}};}
QJsonObject absent(){return{{"kind","ol_jijiang_state"},{"known",true},{"present",false}};}
}
// Sole callers are native OLJijiangVS createCard and its paid replacement.
void recordAIRound54FreshOLJijiangSkills(Room&r,const Card*c){
 if(!c||!c->isVirtualCard()||!c->isKindOf("Slash")||c->getSkillName(false)!="oljijiang"
  ||c->getSkillNames()!=QStringList{QStringLiteral("oljijiang")})return;
 const auto token=globalCardLifetimeManager().observeCard(const_cast<Card*>(c));
 if(!token||!token->live)return;auto&entries=state(r,true)->constructedSkills;
 for(auto i=entries.begin();i!=entries.end();){const auto t=i->token.lock();
  if(!t||!t->live||i->card.isNull())i=entries.erase(i);else ++i;}
 if(!entries.contains(c))entries.insert(c,{token,c});
}
QJsonObject readAIRound54OLJijiangConstructedSkills(Room&r,ServerPlayer*viewer,const Card*c){
 if(!viewer||viewer->getRoom()!=&r||!c)return{};auto*s=state(r);if(!s)return{};
 const auto i=s->constructedSkills.constFind(c);
 if(i==s->constructedSkills.cend()||i->card.isNull()||i->card.data()!=c)return{};
 const auto token=i->token.lock();
 if(!token||!token->live||globalCardLifetimeManager().liveToken(c)!=token
  ||c->getSkillNames()!=QStringList{QStringLiteral("oljijiang")})return{};
 return{{"skill_names",QJsonArray{QStringLiteral("oljijiang")}},{"skill_names_complete",true}};
}
struct AIRound54JijiangScope::Data {
 QPointer<State>s;QPointer<ServerPlayer>lord;const SkillContext*context=nullptr;
 SkillInstanceRef activation,source;quint64 operation=0;bool cleared=false;
 Data*previous=nullptr;
};
struct AIRound54JijiangLiege::Data {
 AIRound54JijiangScope::Data*operation=nullptr;QPointer<ServerPlayer>liege;
 Data*previous=nullptr;
};
struct AIRound54JijiangEvent::Data {
 AIRound54JijiangScope::Data*operation=nullptr;QPointer<ServerPlayer>actor;
 QString payload;bool read=false,cleared=false;Data*previous=nullptr;
};
namespace {
bool live(AIRound54JijiangScope::Data*d,Room&r){
 return d&&d==active&&d->s && &d->s->room==&r&&d->operation&&d->lord
  &&d->lord->getRoom()==&r&&r.roomRuntime()&&!r.roomRuntime()->isClosing()
  &&d->context&&!d->context->is_canceled&&d->context->invoker==d->lord
  &&d->context->activationRef==d->activation&&d->context->sourceRef==d->source;
}
void begin(AIRound54JijiangScope::Data*d,Room&r,const SkillContext&ctx){
 if(!r.roomRuntime()||r.roomRuntime()->isClosing()||ctx.is_canceled
  ||!ctx.invoker||ctx.invoker->getRoom()!=&r||!r.getAllPlayers(true).contains(ctx.invoker)
  ||ctx.activationRef.key.skillName!="oljijiang"||!ctx.activationRef.isValid()||!ctx.sourceRef.isValid())return;
 auto*s=state(r,true);if(s->next==std::numeric_limits<quint64>::max())return;
 d->s=s;d->lord=ctx.invoker;d->context=&ctx;d->activation=ctx.activationRef;d->source=ctx.sourceRef;
 d->operation=++s->next;
}
bool answering(Room&r,ServerPlayer*p){
 return live(active,r)&&liegeFrame&&liegeFrame->operation==active&&liegeFrame->liege==p;
}
QJsonObject source(AIRound54JijiangScope::Data*d){
 auto out=absent();out.insert("operation",QString::number(d->operation));
 if(!d->cleared){out.insert("present",true);out.insert("player",d->lord->objectName());}
 return out;
}
}
AIRound54JijiangScope::AIRound54JijiangScope(Room&r,const SkillContext&ctx):d(new Data){
 d->previous=active;active=d;begin(d,r,ctx);
}
AIRound54JijiangScope::AIRound54JijiangScope(Room&r,const SkillContext&ctx,int event,bool effect):d(new Data){
 d->previous=active;active=d;
 // Cost-time invoke observation is not yet a live liege question. A cancelled
 // cost has nothing transferable; effect obtains its own native operation.
 if(effect&&event==CardAsked&&(ctx.current_event==EventSkillEffect||ctx.current_event==CardAsked))begin(d,r,ctx);
}
AIRound54JijiangScope::~AIRound54JijiangScope(){active=d->previous;delete d;}
AIRound54JijiangLiege::AIRound54JijiangLiege(Room&r,ServerPlayer*p):d(new Data){
 d->previous=liegeFrame;liegeFrame=d;
 if(live(active,r)&&p&&p->getRoom()==&r&&r.getLieges("shu",active->lord).contains(p)){
  d->operation=active;d->liege=p;
 }
}
AIRound54JijiangLiege::~AIRound54JijiangLiege(){liegeFrame=d->previous;delete d;}
AIRound54JijiangEvent::AIRound54JijiangEvent(Room&r,int event,ServerPlayer*p,const QVariant&data):d(new Data){
 d->previous=eventFrame;eventFrame=d;
 if(event!=ChoiceMade||!answering(r,p))return;
 const auto fields=data.toString().split(':');
 if(fields.size()<5||fields.at(0)!="cardResponded"||fields.at(1)!="slash"
  ||fields.at(2)!="@oljijiang-slash"||fields.at(3)!=active->lord->objectName())return;
 d->operation=active;d->actor=p;d->payload=data.toString();
}
AIRound54JijiangEvent::~AIRound54JijiangEvent(){eventFrame=d->previous;delete d;}

QJsonObject queryAIRound54Jijiang(Room&r,const AIRequest&q,ServerPlayer*p,const QString&op,const QString&value){
 if(!r.roomRuntime()||!p||p->getRoom()!=&r||!value.isEmpty()
  ||(op!="source_read"&&op!="source_clear"))return no("OLJijiang source operation unavailable");
 auto&runtime=r.roomRuntime()->ai();
 const bool responseEvent=answering(r,p)&&eventFrame&&eventFrame->operation==active
  &&eventFrame->actor==p&&runtime.currentOLJijiangResponseEvent(q,p,eventFrame->payload);
 if(responseEvent){
  if(op=="source_read"){eventFrame->read=true;return source(active);}
  const auto fields=eventFrame->payload.split(':');
  // Preserve the donor's literal nonempty test: '.' is not the empty answer.
  bool clears=!fields.last().isEmpty();
  if(!clears&&!active->cleared){
   const auto lieges=r.getLieges("shu",active->lord);
   clears=!lieges.isEmpty()&&lieges.last()==p;
  }
  if(!eventFrame->read||eventFrame->cleared||!clears)
   return no("OLJijiang clear is not the reached response continuation");
  active->cleared=true;eventFrame->cleared=true;return source(active);
 }
 if(op!="source_read"||!runtime.currentOLJijiangPlanningRequest(q,p))
  return no("OLJijiang source has no active observer request");
 if(q.kind!=AIRequest::RespondCard||!q.prompt.startsWith("@oljijiang-slash:"))return absent();
 if(!answering(r,p)||q.pattern!="slash"||q.handlingMethod!=Card::MethodResponse
  ||q.prompt!="@oljijiang-slash:"+active->lord->objectName())
  return no("OLJijiang response does not match the native askForCard operation");
 return source(active);
}

bool initializeAIRound54Jijiang(Room&r,lua_State*){return r.roomRuntime()&&!r.roomRuntime()->isClosing();}
void clearAIRound54Jijiang(Room&r){delete state(r);}
// Ordinary events and GameVM calls cannot retire observer memory. The lexical
// pay/effect and liege scopes alone delimit the source's current-use eligibility.
void enterAIRound54JijiangEvent(Room&,int,ServerPlayer*){}
void enterAIRound54JijiangFilter(Room&,int,ServerPlayer*,const QVariant&){}
void observeAIRound54JijiangEntry(Room&,lua_State*,int)noexcept{}
bool acceptAIRound54JijiangEntry(Room&,lua_State*,int)noexcept{return false;}
bool allowAIRound54JijiangWrite(Room&,lua_State*,const lua_TableObservation&)noexcept{return false;}
