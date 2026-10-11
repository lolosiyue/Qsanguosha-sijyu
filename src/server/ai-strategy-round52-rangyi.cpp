#include "ai-strategy-round52-rangyi.h"
#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-round45-code-seal.h"
#include "ai-strategy-readonly-card-store.h"
#include "ai-strategy-card-identity.h"
#include "ai.h"
#include "card.h"
#include "card-lifetime-manager.h"
#include "engine.h"
#include "lua-wrapper.h"
#include "room.h"
#include "room-runtime.h"
#include "room-state.h"
#include "serverplayer.h"
#include <QHash>
#include <QPointer>
#include <atomic>
#include "ai-strategy-round45-lua-inspect.h"
// Exact LuaSkillCard userdata decoder lives next to the generated SWIG types.
extern bool matchAIRound52RangyiTemplate(lua_State*,int,const Card*);
namespace {
thread_local QHash<Room*,QList<AIRound52RangyiUse*>> uses;
thread_local QHash<Room*,QList<AIRound52RangyiCallback*>> callbacks;
thread_local QHash<Room*,QList<AIRound52RangyiQuestion*>> questions;
std::atomic<quint64> nextSerial{0};
const QString path=QStringLiteral("extensions/extra.lua");
bool sourceFunction(Room&r,lua_State*L,int index,int first,int last){
 if(!L||!lua_checkstack(L,32)||!matchAIRound45PinnedFunction(r,L,index,path,first,last))return false;
 index=lua_absindex(L,index);const int top=lua_gettop(L);bool env=false,ok=true;
 for(int i=1;;++i){const char*n=lua_getupvalue(L,index,i);if(!n)break;
  if(QString::fromLatin1(n)!="_ENV"){ok=false;lua_pop(L,1);break;}
  AIRound45Inspect::globals(L);env=lua_rawequal(L,-1,-2);lua_pop(L,2);if(!env){ok=false;break;}
 }lua_settop(L,top);return ok&&env;
}
bool sourceFilter(Room&r,lua_State*L,int index){
 if(!L||!lua_checkstack(L,64)||!matchAIRound45PinnedFunction(r,L,index,"lua/sgs_ex.lua",640,649))return false;
 index=lua_absindex(L,index);const int top=lua_gettop(L);bool env=false,spec=false,ok=true;
 for(int i=1;;++i){const char*n=lua_getupvalue(L,index,i);if(!n)break;
  const QByteArray name(n);
  if(name=="_ENV"){AIRound45Inspect::globals(L);env=lua_rawequal(L,-1,-2);lua_pop(L,2);ok=ok&&env;}
  else if(name=="spec"){
   if(!lua_istable(L,-1)){ok=false;lua_pop(L,1);break;}
   // CreateSkillCard's authentic wrapper dispatches through this exact raw
   // spec.filter; a changed filter or inherited/metatable fallback cannot pass.
   if(lua_getmetatable(L,-1)){lua_pop(L,2);ok=false;break;}
   AIRound45Inspect::field(L,-1,"filter");spec=sourceFunction(r,L,-1,2593,2600);lua_pop(L,2);ok=ok&&spec;
  }else{lua_pop(L,1);ok=false;break;}
 }lua_settop(L,top);return ok&&env&&spec;
}
bool productTemplate(Room&r,lua_State*L,const LuaSkillCard*c){
 if(!c||!L||!lua_checkstack(L,64)||c->objectName()!="twyj_rangyiUse"||c->getSkillName(false)!="twyj_rangyiuse")return false;
 const int top=lua_gettop(L);AIRound45Inspect::globals(L);AIRound45Inspect::field(L,-1,"twyj_rangyiUseCard");
 const bool same=matchAIRound52RangyiTemplate(L,-1,c);lua_settop(L,top);if(!same)return false;
 c->about_to_use.push(L);const bool a=sourceFunction(r,L,-1,2609,2630);lua_settop(L,top);
 c->feasible.push(L);const bool b=sourceFunction(r,L,-1,2601,2608);lua_settop(L,top);
 c->filter.push(L);const bool f=sourceFilter(r,L,-1);lua_settop(L,top);return a&&b&&f;
}
template<class T>void pop(QHash<Room*,QList<T*>>&map,Room*r,T*p){auto i=map.find(r);if(i==map.end())return;if(!i->isEmpty()&&i->last()==p)i->removeLast();else i->clear();if(i->isEmpty())map.erase(i);}
}
struct AIRound52RangyiUse::Data{
 Room*room;QPointer<ServerPlayer>from,to;QPointer<Card>card;
 std::shared_ptr<const CardLifetimeToken>token;CardLifetimeLease lease;bool valid=false;
 const Card*liveCard()const{
  // This is identity custody for the already-accepted native outer use, not
  // an owner readonly-physical observation or permission to inspect its tags.
  if(card.isNull()||!token||!token->live||!lease.isValid())return nullptr;
  return globalCardLifetimeManager().liveToken(card.data())==token?card.data():nullptr;
 }
};
struct AIRound52RangyiCallback::Data{Room*room;AIRound52RangyiUse*use=nullptr;QPointer<ServerPlayer>from,to;lua_State*game=nullptr;const void*function=nullptr;quint64 generation=0;bool valid=false,bound=false;};
struct AIRound52RangyiQuestion::Data{Room*room;AIRound52RangyiCallback*callback=nullptr;QPointer<ServerPlayer>viewer;quint64 serial=0;bool revoked=false;};
struct AIRound52RangyiAccess{
 static AIRound52RangyiQuestion::Data*question(Room&r){auto i=questions.find(&r);return i==questions.end()||i->isEmpty()?nullptr:i->last()->d.get();}
 static bool current(const AIRequest&q,Room&r,ServerPlayer*p){
  auto*d=question(r);if(!d||d->revoked||!d->callback||!d->serial||q.round52RangyiQuestionSerial!=d->serial||d->viewer!=p||!p)return false;
  auto c=callbacks.constFind(&r);auto u=uses.constFind(&r);if(c==callbacks.cend()||c->isEmpty()||c->last()!=d->callback||u==uses.cend()||u->isEmpty())return false;
  auto*b=d->callback->d.get();if(!b->valid||u->last()!=b->use||!b->use->d->valid||b->to!=p||!b->from||!b->use->d->liveCard())return false;
  auto*rt=r.roomRuntime();if(!rt||rt->isClosing()||rt->lua().rawState()!=b->game||rt->lua().generation()!=b->generation)return false;
  return p->getRoom()==&r&&r.findPlayerByObjectName(p->objectName(),true)==p&&p->isAlive()
   &&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&q.decisionId
   &&q.kind==AIRequest::UseCard&&q.pattern=="@@twyj_rangyi"&&q.prompt=="@twyj_rangyi"&&q.handlingMethod==Card::MethodUse
   &&q.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&r.roomState()->getCurrentCardUsePattern()==q.pattern
   &&r.roomState()->getCurrentCardUseReason()==q.reason&&aiRound44RangeRequestCurrent(r,q,p);
 }
};
void registerAIRound52RangyiSource(Room&r,lua_State*L){
 registerAIRound45PinnedSource(r,L,"lua/sgs_ex.lua",QByteArrayLiteral("26445717639068578986fefa44c396f40426b155410b47c3538e450794e4aece"));
 registerAIRound45PinnedSource(r,L,path,QByteArrayLiteral("2cc0e3e6386fdbbbd1d4ee4c4cbc9ae3e1479ca99050182edf1e40d14b779c3a"));
}
AIRound52RangyiUse::AIRound52RangyiUse(Room&r,const CardUseStruct&u):d(new Data){
 d->room=&r;uses[&r].append(this);
 if(!u.card||!u.from||u.from->getRoom()!=&r||u.to.size()!=1||!u.to.first()||u.to.first()==u.from||u.to.first()->getRoom()!=&r)return;
 if(u.card->objectName()!="twyj_rangyi"||u.card->getTypeId()!=Card::TypeSkill||u.activationRef.ownerObjectName!=u.from->objectName()
  ||u.activationRef.key.skillName!="twyj_rangyi"||!u.from->hasSkillInstance("twyj_rangyi",u.activationRef.key.instanceID))return;
 d->from=u.from;d->to=u.to.first();d->card=const_cast<Card*>(u.card);
 auto&manager=globalCardLifetimeManager();d->token=manager.liveToken(u.card);
 d->lease=CardLifetimeLease(manager,d->token);d->valid=d->liveCard()!=nullptr;
}
AIRound52RangyiUse::~AIRound52RangyiUse(){pop(uses,d->room,this);}
AIRound52RangyiCallback::AIRound52RangyiCallback(Room&r,lua_State*L,int fn,const Card*card,ServerPlayer*from,const QList<ServerPlayer*>&to):d(new Data){
 d->room=&r;callbacks[&r].append(this);auto i=uses.constFind(&r);
 if(i==uses.cend()||i->isEmpty()||!r.roomRuntime()||r.roomRuntime()->isClosing()||L!=r.roomRuntime()->lua().rawState())return;
 auto*u=i->last();if(!u->d->valid||u->d->from!=from||to.size()!=1||u->d->to!=to.first()||u->d->liveCard()!=card
  ||!sourceFunction(r,L,fn,2639,2656))return;
 d->use=u;d->from=from;d->to=to.first();d->game=L;d->function=lua_topointer(L,fn);d->generation=r.roomRuntime()->lua().generation();d->valid=true;
}
AIRound52RangyiCallback::~AIRound52RangyiCallback(){pop(callbacks,d->room,this);}
AIRound52RangyiQuestion::AIRound52RangyiQuestion(Room&r,ServerPlayer*p,const QString&pattern,const QString&prompt,int method):d(new Data){
 d->room=&r;d->viewer=p;
 if(auto*parent=AIRound52RangyiAccess::question(r))parent->revoked=true;
 auto i=callbacks.find(&r);if(i!=callbacks.end()&&!i->isEmpty()){
  auto*c=i->last();auto*b=c->d.get();bool reached=false;
  if(b->valid&&b->game&&lua_checkstack(b->game,8)){
   // The first Lua caller must be the real captured on_use, at its sole
   // source askForUseCard call. Earlier obtain/mark sibling callbacks cannot
   // mint a recipient question merely by reusing the same prompt.
   for(int level=0;;++level){lua_Debug ar{};if(!lua_getstack(b->game,level,&ar)||!lua_getinfo(b->game,"Slf",&ar))break;
    const void*fn=lua_topointer(b->game,-1);lua_pop(b->game,1);
    if(ar.what&&QString::fromLatin1(ar.what)=="C")continue;
    reached=fn==b->function&&ar.currentline==2649;break;
   }
  }
  if(reached&&b->valid&&!b->bound&&b->to==p&&pattern=="@@twyj_rangyi"&&prompt=="@twyj_rangyi"&&method==int(Card::MethodUse)){
   b->bound=true;d->callback=c;d->serial=++nextSerial;
  }
 }questions[&r].append(this);
}
AIRound52RangyiQuestion::~AIRound52RangyiQuestion(){pop(questions,d->room,this);}
void projectAIRound52Rangyi(AIRequest&q,Room&r,ServerPlayer*p){
 q.round52RangyiQuestionSerial=0;auto*d=AIRound52RangyiAccess::question(r);if(!d)return;q.round52RangyiQuestionSerial=d->serial;
 if(!AIRound52RangyiAccess::current(q,r,p)){q.round52RangyiQuestionSerial=0;return;}
 q.worldView.decisionContext.insert("round52_rangyi",QJsonObject{{"known",true}});
}
bool aiRound52RangyiCurrent(const AIRequest&q,Room&r,ServerPlayer*p,int id){
 if(!AIRound52RangyiAccess::current(q,r,p)||id<0||r.getCardOwner(id)!=p||r.getCardPlace(id)!=Player::PlaceHand||!p->handCards().contains(id))return false;
 const Card*c=r.getCard(id);if(!c||c->isVirtualCard()||c->getEffectiveId()!=id||!c->hasFlag("twyj_rangyi"))return false;
 for(const auto&v:q.worldView.handCards)if(v.effectiveId==id)return !v.legacyIdentityToken.isEmpty()&&v.legacyIdentityToken==aiLegacyCardIdentity(r,p,c)
  &&v.nativeIdentity==globalCardLifetimeManager().liveToken(c)
  &&v.cardId==c->getId()&&!v.virtualCard&&v.subcardIds==c->getSubcards()
  &&v.skillName==c->getSkillName(false)&&v.sourceSkillName==c->getSourceSkillName()
  &&v.typeId==int(c->getTypeId())&&v.handlingMethod==int(c->getHandlingMethod())
  &&v.objectName==c->objectName()&&v.className==c->getClassName()&&v.suit==int(c->getSuit())&&v.number==c->getNumber();
 return false;
}
Card*createAIRound52RangyiProduct(const AIRequest&q,Room&r,ServerPlayer*p,int id,bool selected,QString*error){
 auto fail=[&](const char*s)->Card*{if(error)*error=QString::fromLatin1(s);return nullptr;};
 if(!aiRound52RangyiCurrent(q,r,p,id))return fail("Rangyi exact recipient question/material changed");
 auto*rt=r.roomRuntime();lua_State*L=rt->lua().state();const auto*t=rt->luaSkillCard("twyj_rangyiUse");
 if(!productTemplate(r,L,t))return fail("Rangyi raw source template changed");
 // Source clones this exact registered object, with no V2 attribution.
 std::unique_ptr<LuaSkillCard> c(t->clone());if(!c)return fail("Rangyi native clone failed");c->addSubcard(id);
 if(c->getSkillName(false)!="twyj_rangyiuse"||c->objectName()!="twyj_rangyiUse"||c->getSubcards()!=QList<int>{id})return fail("Rangyi source product differs");
 if(selected&&!r.getCard(id)->isAvailable(p))return fail("Rangyi selected physical card is not available");
 if(!aiRound52RangyiCurrent(q,r,p,id)||rt->luaSkillCard("twyj_rangyiUse")!=t||!productTemplate(r,L,t))return fail("Rangyi source changed during construction");
 return c.release();
}
Card*rebuildAIRound52RangyiProduct(const AIRequest&q,Room&r,ServerPlayer*p,const AICardConversionView&t){
 if(!t.rangyiQuestion||t.rangyiQuestionSerial!=q.round52RangyiQuestionSerial||t.producerVersion!=0||t.policyClass!="@rangyi_question"
  ||t.producerReason!=q.reason||t.producerPattern!=q.pattern||t.subcardIds.size()!=1||t.costCount!=1||t.activationRef.isValid()||t.sourceRef.isValid())return nullptr;
 std::unique_ptr<Card>c(createAIRound52RangyiProduct(q,r,p,t.subcardIds.first(),true));
 if(!c||c->objectName()!=t.name||c->getClassName()!=t.className||int(c->getSuit())!=t.suit||c->getNumber()!=t.number||c->getSkillName(false)!=t.skillName||c->toString()!=t.legacyStringKey)return nullptr;
 return c.release();
}
