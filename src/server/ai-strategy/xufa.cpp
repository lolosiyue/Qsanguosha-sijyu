#include "ai-strategy/xufa.h"

// ---- xufa-event-scope ----
#include "ai-strategy/lua-seal.h"
#include "ai-strategy/context.h"
#include "room.h"
#include "room-runtime.h"
#include "lua-runtime.h"
#include "serverplayer.h"
#include <QHash>
#include <QList>
#include <QPointer>
#include <QVariant>
#include <cstring>
extern "C" {
#include "lua.h"
}
namespace {thread_local QHash<Room*,QList<const AIXufaEventScope*>> events;}
struct AIXufaEventScope::Data {
 Room*room=nullptr;int event=0;QPointer<ServerPlayer>actor,writer;
 const QVariant*payload=nullptr;quint64 writeSerial=0;bool valid=false,completed=false;
};
struct AIXufaEventAccess {
 static AIXufaEventScope::Data*top(Room&r){auto i=events.constFind(&r);return i==events.cend()||i->isEmpty()?nullptr:i->last()->d.get();}
};
AIXufaEventScope::AIXufaEventScope(Room&r,int e,ServerPlayer*p,const QVariant&v):d(new Data){
 d->room=&r;d->event=e;d->actor=p;d->payload=&v;d->writer=currentAIXufaContinuityWriter(r);d->writeSerial=currentAIXufaWriteSerial(r);
 // Nested/uncovered events never inherit an outer grant. There is no value
 // restoration, including when the exact same scalar is subsequently written.
 const bool nested=AIXufaEventAccess::top(r)!=nullptr;
 events[&r].append(this);
 try {
  // The complete pre-filter TriggerSkill and event snapshot enable unions
  // are not yet admitted. Keep this native event grant closed until those
  // fixed-source prerequisites are connected BEFORE private applicability.
  const bool completeNativeEventPreflight=false;
  if(completeNativeEventPreflight&&!nested&&d->writer&&matchesAIXufaMoveEvent(r,e,p,v)){
   auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();
   d->valid=L&&lua_checkstack(L,8192)&&proveAIXufaCorrectionUnion(r,L)
       &&d->writeSerial&&currentAIXufaWriteSerial(r)==d->writeSerial&&currentAIXufaContinuityWriter(r)==d->writer;
  }
 }catch(...){d->valid=false;}
 if(!d->valid)interruptAIXufaContinuity(r);
}
AIXufaEventScope::~AIXufaEventScope(){
 auto&r=*d->room;if(!d->valid||!d->completed)interruptAIXufaContinuity(r);
 auto i=events.find(&r);if(i!=events.end()){i->removeAll(this);if(i->isEmpty())events.erase(i);}
}
bool pendingAIXufaEvent(Room&r){
 auto*d=AIXufaEventAccess::top(r);return d&&d->valid&&!d->completed&&d->writer
  &&d->writeSerial&&currentAIXufaWriteSerial(r)==d->writeSerial&&currentAIXufaContinuityWriter(r)==d->writer;
}
bool prepareAIXufaFilterCallback(Room&r,const char*m){
 return m&&!std::strcmp(m,"filterEvent")&&pendingAIXufaEvent(r);
}
void completeAIXufaEventProof(Room&r,lua_State*L,int nargs){
 auto*d=AIXufaEventAccess::top(r);
 // Called ONLY after the common prefix+actual-self+full family/event callback
 // graph has passed in the sole protected-entry observer. This is not itself
 // a graph verifier and is never exported to Lua.
 if(!d||!d->valid||d->completed||!d->writer||!d->writeSerial||currentAIXufaWriteSerial(r)!=d->writeSerial||currentAIXufaContinuityWriter(r)!=d->writer)return;
 if(matchesAINativeFilterDispatch(r,L,nargs,d->event,d->actor,d->payload))d->completed=true;
}

// ---- xufa-query-scope ----
#include "ai-strategy/native-registry.h"
#include "ai-strategy/runtime.h"
#include <cstdio>
#ifdef Q_OS_LINUX
#include <execinfo.h>
#endif
#include "ai-strategy/source-slots.h"
#include "ai-strategy/official.h"
#include "ai-strategy/owner.h"
#include "card.h"
#include "wrapped-card.h"
#include "standard.h"
#include "maneuvering.h"
#include "ol.h"
#include "ai.h"
#include <QSet>
#include <typeinfo>
#include "ai-strategy/lua-raw-inspect.h"
namespace {
thread_local QHash<Room*,QList<const AIXufaQueryScope*>> queries;
using StampedCard=QPair<const Card*,quint64>;
thread_local QHash<Room*,QList<StampedCard>> freshCanSlash;
thread_local QHash<Room*,QList<StampedCard>> projectionCards;
bool sourceCard(Room&,ServerPlayer*,const Card*);
struct Stack{lua_State*L;int n;~Stack(){lua_settop(L,n);}};
// Exact finite native definitions. These virtual available/targetFixed paths
// have no authored callback except correction/Prohibit/CardLimit already in
// the complete union. Lua subclasses and native transforming SkillCards are
// outside this proof, regardless of current hasSkill/mark branch outcomes.
bool nativePolicy(const Card*c){
 const Card*inner=c->getRealCard();if(!inner)return false;
 const std::type_info&t=typeid(*inner);
 return t==typeid(XufaCard)||t==typeid(Slash)||t==typeid(FireSlash)||t==typeid(ThunderSlash)
  ||t==typeid(Jink)||t==typeid(Peach)||t==typeid(Analeptic)
  ||t==typeid(Duel)||t==typeid(Snatch)||t==typeid(Dismantlement)
  ||t==typeid(Nullification)||t==typeid(ExNihilo)||t==typeid(Collateral)
  ||t==typeid(AmazingGrace)||t==typeid(GodSalvation)
  ||t==typeid(SavageAssault)||t==typeid(ArcheryAttack)
  ||t==typeid(FireAttack)||t==typeid(IronChain)
  ||t==typeid(Indulgence)||t==typeid(SupplyShortage)||t==typeid(Lightning);
}
bool custody(Room&r,ServerPlayer*viewer,const Card*c){
 if(!viewer||!c)return false;
 // This exact compiled constructor is public by construction. It is admitted
 // only under an already proved native getter call tree, never by zero cost.
 const auto stamp=currentAIXufaWriteSerial(r);
 if(stamp&&(freshCanSlash.value(&r).contains({c,stamp})||projectionCards.value(&r).contains({c,stamp})||sourceCard(r,viewer,c)))return nativePolicy(c);
 const int id=c->getId();
 // Pointer membership is checked before virtual face/class inspection.
 if(id<0||r.getCard(id)!=c)return false;
 const auto place=r.getCardPlace(id);
 bool known=r.getCardOwner(id)==viewer&&(place==Player::PlaceHand||place==Player::PlaceEquip||place==Player::PlaceDelayedTrick);
 if(!known&&r.getCardOwner(id)==viewer&&place==Player::PlaceSpecial){
  for(const auto&pile:viewer->getPileNames())if(viewer->getPile(pile).contains(id)){known=true;break;}
 }
 if(!known){
  known=place==Player::DiscardPile||place==Player::PlaceEquip||place==Player::PlaceDelayedTrick;
  if(!known)known=readAIStrategyPublicDisclosure(r,viewer,id).value("known")==true;
 }
 return known&&nativePolicy(c);
}
}
struct AIXufaSourceCard::Data {
 Room*room=nullptr;QPointer<ServerPlayer>owner;QPointer<const Card>card;const AIRequest*request=nullptr;QList<int>ids;quint64 writeSerial=0;bool valid=false;
};
namespace {thread_local QHash<Room*,QList<const AIXufaSourceCard*>> sourceCards;}
struct AIXufaSourceAccess {
 static bool current(Room&r,ServerPlayer*p,const Card*c){
  const auto i=sourceCards.constFind(&r);if(i==sourceCards.cend()||i->isEmpty())return false;
  const auto*d=i->last()->d.get();return d->valid&&d->writeSerial&&d->writeSerial==currentAIXufaWriteSerial(r)&&d->owner==p&&d->card==c&&d->request
   &&d->request->stateRevision==r.roomRuntime()->stateRevision()&&c->getSubcards()==d->ids;
 }
};
namespace {bool sourceCard(Room&r,ServerPlayer*p,const Card*c){return AIXufaSourceAccess::current(r,p,c);}}
AIXufaSourceCard::AIXufaSourceCard(Room&r,const AIRequest&q,ServerPlayer*p,const Card*c,const QString&skill):d(new Data){
 d->room=&r;d->owner=p;d->card=c;d->request=&q;d->writeSerial=currentAIXufaWriteSerial(r);sourceCards[&r].append(this);
 if(skill!="xufa"||!p||!c||p->getRoom()!=&r||q.kind!=AIRequest::Activate
  ||q.viewerObjectName!=p->objectName()||q.worldView.self.objectName!=p->objectName()
  ||q.stateRevision!=r.roomRuntime()->stateRevision()||currentAIXufaContinuityWriter(r)!=p)return;
 d->ids=c->getSubcards();if(d->ids.isEmpty())return;QSet<int>seen;
 for(int id:d->ids){
  bool known=false;for(const auto&v:q.worldView.handCards)if(v.effectiveId==id)known=true;
  for(const auto&v:q.worldView.self.equips)if(v.effectiveId==id)known=true;
  for(const auto&pile:q.worldView.self.piles)if(pile.name=="xufa")for(const auto&v:pile.cards)if(v.effectiveId==id)known=true;
  if(id<0||seen.contains(id)||!known||r.getCardOwner(id)!=p)return;seen.insert(id);
  auto place=r.getCardPlace(id);if(place!=Player::PlaceHand&&place!=Player::PlaceEquip
   &&!(place==Player::PlaceSpecial&&p->getPile("xufa").contains(id)))return;
 }
 // The callable site has already authenticated its actual native producer.
 // Only after lawful material custody is established do we inspect the class.
 d->valid=typeid(*c)==typeid(XufaCard)&&c->getId()<0&&d->writeSerial&&d->writeSerial==currentAIXufaWriteSerial(r);
}
AIXufaSourceCard::~AIXufaSourceCard(){auto i=sourceCards.find(d->room);if(i==sourceCards.end())return;i->removeAll(this);if(i->isEmpty())sourceCards.erase(i);}
struct AIXufaQueryScope::Data{Room*room=nullptr;QPointer<ServerPlayer>writer;quint64 writeSerial=0;bool admitted=false;
 std::unique_ptr<AIXufaCorrectionScope>corrections;
};
struct AIXufaQueryAccess{
 static AIXufaQueryScope::Data*top(Room*r){auto i=queries.constFind(r);return i==queries.cend()||i->isEmpty()?nullptr:i->last()->d.get();}
};
AIXufaQueryScope::AIXufaQueryScope(Room*r,const Card*c,bool required,bool preserveAiNoTo,bool preservePriority,bool scalarCardOnly):d(new Data){
 d->room=r;if(!r)return;
 if(preserveAIProjectionNativeQuery(*r,c,required,scalarCardOnly)
   ||preserveAIPlayerCorrectionNativeQuery(*r,c,required))return;
 // R55: the separate priority flag is supplied only by the complete native-only
 // public skill-query proofs. It does not follow a callback-only aiNoTo proof.
 // No grant is created, refreshed or restamped; Lua/async entries still revoke.
 if(!preservePriority){
  // First default native revocation while the exact source ledger is still
  // live. Code addresses only; this diagnostic never changes admission.
  if(qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")
    &&hasAIPlayLoopSource(*r,r->roomRuntime()->lua().rawState())){
   std::fprintf(stderr,"R56_FIRST_NATIVE_PRIORITY_REVOKE required=%d scalar_only=%d\n",int(required),int(scalarCardOnly));std::fflush(stderr);
#ifdef Q_OS_LINUX
   std::fprintf(stderr,"R56_PRIORITY_REVOKE_NATIVE_STACK_BEGIN\n");std::fflush(stderr);
   void *frames[32];const int count=::backtrace(frames,32);::backtrace_symbols_fd(frames,count,2);
   std::fprintf(stderr,"R56_PRIORITY_REVOKE_NATIVE_STACK_END\n");std::fflush(stderr);
#endif
  }
  r->roomRuntime()->revokePrioritySource(); // before private selection
 }
 // The separate R54 native public-definition proof may preserve aiNoTo only.
 // Every other query retains preselection revocation; Xufa admission is unchanged.
 if(!preserveAiNoTo)retireAIAiNoTo(*r);
 auto*parent=AIXufaQueryAccess::top(r);
 d->writer=currentAIXufaContinuityWriter(*r);d->writeSerial=currentAIXufaWriteSerial(*r);
 // No current known row: this path must never create/revive one.
 if(!d->writer){observeAIUnprovedNativeQuery(r,preserveAiNoTo);queries[r].append(this);return;}
 bool ok=false;try {
 // v2 deliberately does not reuse a parent's proof until epoch, generation
 // and native registry/source witnesses are all installed. Never reuse based
 // solely on equality of the writer pointer after an interruption/rewrite.
 {
  auto*rt=r->roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();
  // This is normal synchronous native code, before observer/protectedCall.
  // Reservation here is allowed; the entry proof itself never allocates stack.
  if(L&&lua_checkstack(L,8192)){
   d->corrections.reset(new AIXufaCorrectionScope(*r,d->writer,L));
   ok=proveAIXufaCorrectionUnion(*r,L);
  }
 }
 // Complete source union precedes even the original card-custody selection.
 if(ok)ok=!required||custody(*r,d->writer,c);
 }catch(...){ok=false;}
 // Nested proof allocation or native reentry can only revoke, never reseed.
 ok=ok&&d->writeSerial&&currentAIXufaWriteSerial(*r)==d->writeSerial&&currentAIXufaContinuityWriter(*r)==d->writer;
 d->admitted=ok;
 if(ok){interruptAIChoiceContinuityExceptXufa(*r);clearAIFaqiPlans(*r);}
 else observeAIUnprovedNativeQuery(r,preserveAiNoTo);
 queries[r].append(this);
}
AIXufaQueryScope::~AIXufaQueryScope(){if(!d->room)return;auto i=queries.find(d->room);if(i==queries.end())return;i->removeAll(this);if(i->isEmpty())queries.erase(i);}
AIXufaCanSlashCard::AIXufaCanSlashCard(Room*r,const Card*c):room(r),card(c){
 if(r)recordAIProjectionSourceCard(*r,c,QStringLiteral("slash"));
 auto*q=AIXufaQueryAccess::top(r);
 if(r&&q&&q->admitted&&q->writeSerial&&q->writeSerial==currentAIXufaWriteSerial(*r)&&c&&typeid(*c)==typeid(Slash)&&c->getId()<0&&c->getSubcards().isEmpty()
    &&c->getSuit()==Card::NoSuit&&c->getNumber()==0&&c->getSkillName(false).isEmpty()){
  stamp=q->writeSerial;freshCanSlash[r].append({c,stamp});entered=true;
 }
}
AIXufaCanSlashCard::~AIXufaCanSlashCard(){if(!entered)return;auto i=freshCanSlash.find(room);if(i==freshCanSlash.end())return;i->removeOne({card,stamp});if(i->isEmpty())freshCanSlash.erase(i);}
AIXufaProjectionCard::AIXufaProjectionCard(Room&r,ServerPlayer*p,const Card*c,const QString&name):room(&r),card(c){
 recordAIProjectionSourceCard(r,c,name);
 if(!p||!c||p->getRoom()!=&r||currentAIXufaContinuityWriter(r)!=p)return;
 // Called only immediately after the fixed coordinator clone and before
 // addRules. Public registered constructor replacement is not silently taken
 // as equivalent: actual native type and all constructor scalars must match.
 bool exact=(name=="jink"&&typeid(*c)==typeid(Jink))
  ||(name=="slash"&&typeid(*c)==typeid(Slash))
  ||(name=="peach"&&typeid(*c)==typeid(Peach))
  ||(name=="fire_slash"&&typeid(*c)==typeid(FireSlash))
  ||(name=="thunder_slash"&&typeid(*c)==typeid(ThunderSlash));
 if(!exact||c->getId()>=0||!c->getSubcards().isEmpty()||c->objectName()!=name
  ||c->getSuit()!=Card::NoSuit||c->getNumber()!=0||!c->getSkillName(false).isEmpty())return;
 stamp=currentAIXufaWriteSerial(r);if(!stamp)return;projectionCards[&r].append({c,stamp});entered=true;
}
AIXufaProjectionCard::~AIXufaProjectionCard(){if(!entered)return;auto i=projectionCards.find(room);if(i==projectionCards.end())return;i->removeOne({card,stamp});if(i->isEmpty())projectionCards.erase(i);}
bool certifyAIXufaQueryEntry(Room&r,lua_State*L,int nargs,QString*why){
 auto fail=[&](const char*s){if(why)*why=QString::fromLatin1(s);return false;};
 auto*q=AIXufaQueryAccess::top(&r);
 if(!q||!q->admitted||!q->writer||!q->writeSerial||q->writeSerial!=currentAIXufaWriteSerial(r)||currentAIXufaContinuityWriter(r)!=q->writer)return fail("Xufa current proved native getter scope absent");
 if(!L||r.roomRuntime()->lua().state()!=L||nargs<1||nargs>5||!AILuaInspect::capacity(L,4096))return fail("Xufa getter native call shape changed");
 const int top=lua_gettop(L),fn=top-nargs;Stack st{L,top};if(fn<1)return fail("Xufa getter callable missing");
 // Native wrappers supply scalar/nil or their SWIG typed objects. No authored
 // table/metamethod arguments can enter the pinned getter body through this
 // route. Private object fields are not read by the certificate.
 for(int i=fn+1;i<=top;++i){int type=lua_type(L,i);
  if(type==LUA_TNIL||type==LUA_TNUMBER||type==LUA_TBOOLEAN||type==LUA_TSTRING)continue;
  if(type!=LUA_TUSERDATA)return fail("Xufa getter argument is authored capability");
  bool known=matchAINativeObjectMethods(r,L,i,"Player",{"objectName"})
   ||matchAINativeObjectMethods(r,L,i,"Skill",{"objectName"})
   ||matchAINativeObjectMethods(r,L,i,"Card",{"objectName"})
   ||matchAINativeObjectMethods(r,L,i,"CorrectSkillContext",{"getPrimary","getCard"})
   ||matchAINativeObjectMethods(r,L,i,"PlayerList",{"length","at"});
  if(!known)return fail("Xufa getter native argument class/binding changed");
 }
 return proveAIXufaGetterFunction(r,L,fn,why);
}

// ---- xufa-registry ----
#include "ai-runtime.h"
#include "card-lifetime-manager.h"
#include "engine.h"
#include <QByteArray>
#include <limits>
namespace {
struct XufaRegistryStack {lua_State*L;int n;explicit XufaRegistryStack(lua_State*s):L(s),n(lua_gettop(s)){}~XufaRegistryStack(){lua_settop(L,n);}};
class XufaRegistryState;
void observe(lua_State*,const lua_TableObservation*,void*)noexcept;
class XufaRegistryState final:public QObject {
public:
 explicit XufaRegistryState(Room&r):QObject(&r),room(r){}
 ~XufaRegistryState()override {
  // FREE removes each identity before reuse. Normal runtime clear is before
  // Lua closes; no dead table address is passed to the observer API.
  for(auto id:{globals,space,choices})if(id)lua_unobservetableidentity(L,id,observe,this);
 }
 Room&room;lua_State*L=nullptr;quint64 generation=0,epoch=1,stamp=0,transport=0;
 const void*globals=nullptr,*space=nullptr,*choices=nullptr;
 bool valid=false,coverage=false,exhausted=false,pending=false,expected=false;
 unsigned writes=0;
 QPointer<ServerPlayer>writer;
 void revoke()noexcept {stamp=0;if(epoch==std::numeric_limits<quint64>::max())exhausted=true;else ++epoch;}
};
XufaRegistryState*state(Room&r,bool create=false){for(auto*c:r.children())if(auto*s=dynamic_cast<XufaRegistryState*>(c))return s;return create?new XufaRegistryState(r):nullptr;}
bool key(const lua_TableObservation&e,const char*k){return e.string_key&&e.string_key_length==std::strlen(k)&&!std::memcmp(e.string_key,k,e.string_key_length);}
int assign(lua_State*);
void observe(lua_State*L,const lua_TableObservation*ep,void*ud)noexcept {
 auto*s=static_cast<XufaRegistryState*>(ud);const auto&e=*ep;
 // Coroutines share table identities and provenance with their global state.
 // The exact assigning C frame below is still checked on the calling thread.
 if(!L||!s->L||G(L)!=G(s->L))return;
 const bool root=e.table_identity==s->globals,ns=e.table_identity==s->space,cell=e.table_identity==s->choices;
 if(!root&&!ns&&!cell)return;
 const bool structural=e.event==LUA_TABLE_OBSERVER_FREE||e.event==LUA_TABLE_OBSERVER_METATABLE;
 if(structural||(e.event==LUA_TABLE_OBSERVER_WRITE&&((root&&key(e,"sgs"))||(ns&&key(e,"ai_skill_choice"))))){
  s->valid=false;s->revoke();
  if(e.event==LUA_TABLE_OBSERVER_FREE){if(root)s->globals=nullptr;if(ns)s->space=nullptr;if(cell)s->choices=nullptr;}
  return;
 }
 if(cell&&e.event==LUA_TABLE_OBSERVER_WRITE&&key(e,"xufa")){
  // Every write, including equal value/nil/restore, changes provenance. READ
  // does not revoke: the actual source value is synchronized, not shadowed.
  const TValue*f=s2v(L->ci->func.p);
  const bool own=s->pending&&s->expected&&ttislcf(f)&&fvalue(f)==assign;
  s->revoke();
  if(own&&s->writes==0)s->writes=1;else s->pending=false;
 }
}
bool live(XufaRegistryState*s){auto*rt=s?s->room.roomRuntime():nullptr;return rt&&!rt->isClosing()&&s->valid&&!s->exhausted
 &&rt->lua().isCurrentThreadOwner()&&rt->lua().lifecycle()==LuaRuntime::Lifecycle::Running
 &&rt->lua().rawState()==s->L&&rt->lua().generation()==s->generation;}
bool current(Room&r,ServerPlayer*p,const AIRequest&q){return p&&p->getRoom()==&r&&q.decisionId
 &&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()
 &&r.findPlayerByObjectName(p->objectName(),true)==p&&aiRangeRequestCurrent(r,q,p)
 &&r.roomRuntime()->ai().currentXufaChoiceRequest(q,p)
 &&Sanguosha&&Sanguosha->currentRoom()==&r;}
bool roots(XufaRegistryState*s){XufaRegistryStack st(s->L);auto*L=s->L;AILuaInspect::globals(L);if(lua_topointer(L,-1)!=s->globals)return false;
 AILuaInspect::field(L,-1,"sgs");if(!lua_istable(L,-1)||lua_topointer(L,-1)!=s->space)return false;
 AILuaInspect::field(L,-1,"ai_skill_choice");auto*t=AILuaInspect::table(L,-1);
 return t&&!t->metatable&&lua_topointer(L,-1)==s->choices;}
QJsonObject unknown(){return{{"known",false}};}
bool disclosedName(Room&r,ServerPlayer*p,const AIRequest&q,const QString&name){
 // Exact current own source HE/xufa-pile faces only. A printed ID by itself is
 // not a permit to look up a hidden current card or a foreign physical face.
 auto test=[&](const AICardView&v){if(v.objectName!=name||!v.nativeIdentity||!globalCardLifetimeManager().isLive(v.nativeIdentity))return false;
  const Card*c=r.getCard(v.effectiveId);if(!c||v.nativeIdentity->address!=c||c->isVirtualCard())return false;
  if(!p->handCards().contains(v.effectiveId)&&!p->getPile("xufa").contains(v.effectiveId)){
   bool equip=false;for(auto*e:p->getEquips())if(e==c)equip=true;if(!equip)return false;
  }
  return v.cardId==c->getId()&&v.effectiveId==c->getEffectiveId()&&v.className==c->getClassName()
   &&v.suit==int(c->getSuit())&&v.number==c->getNumber()&&v.skillName==c->getSkillName(false)
   &&v.subcardIds==c->getSubcards()&&v.virtualCard==c->isVirtualCard()&&c->objectName()==name&&c->isNDTrick();
 };
 for(const auto&v:q.worldView.handCards)if(test(v))return true;
 for(const auto&v:q.worldView.self.equips)if(test(v))return true;
 for(const auto&pile:q.worldView.self.piles)for(const auto&v:pile.cards)if(test(v))return true;
 return false;
}
struct Write {XufaRegistryState*s;Room*r;ServerPlayer*p;const AIRequest*q;QByteArray value;quint64 before=0,transport=0;bool reached=false;};
int assign(lua_State*L){
 if(!lua_checkstack(L,256))return luaL_error(L,"R53 registry assignment stack unavailable");
 auto*w=static_cast<Write*>(lua_touserdata(L,2));
 if(!w||!live(w->s)||!current(*w->r,w->p,*w->q)||!roots(w->s)
  ||lua_topointer(L,1)!=w->s->choices)
  return luaL_error(L,"R53 registry assignment context changed");
 // Entry revocation belongs to the reached prefix before this new source
 // assignment. Capture once here, before allocation; reads and continuation
 // never resample an interrupted source's transport generation.
 w->transport=w->r->roomRuntime()->xufaTransportEpoch();
 if(!w->transport)return luaL_error(L,"R53 source transport unavailable");
 // Any Lua allocation stays inside this game-VM protected C frame. No error
 // may jump from the game VM through the outer isolated host frame.
 lua_pushlstring(L,w->value.constData(),size_t(w->value.size()));
 lua_pushliteral(L,"xufa");lua_pushvalue(L,-2);
 if(!live(w->s)||w->r->roomRuntime()->xufaTransportEpoch()!=w->transport
  ||!current(*w->r,w->p,*w->q)||!roots(w->s))
  return luaL_error(L,"R53 registry assignment changed during allocation");
 // All allocation / protected-entry observers before this point belong to the
 // reached prefix. Arm only at the exact ordinary source assignment.
 w->s->revoke();w->before=w->s->epoch;w->s->pending=true;w->s->expected=true;w->s->writes=0;
 lua_settable(L,1); // equivalent source assignment; plain registry was proved

 w->s->expected=false;w->reached=true;return 0;
}
}
bool initializeAIXufaChoiceRegistry(Room&r,lua_State*L,QString*error){
 auto fail=[&](const char*s){if(error)*error=QString::fromLatin1(s);return false;};
 auto*rt=r.roomRuntime();if(!rt||rt->isClosing()||!L||L!=rt->lua().rawState()||!rt->lua().isCurrentThreadOwner())return fail("R53 source game VM unavailable");
 if(!lua_checkstack(L,32))return fail("R53 source roots stack unavailable");XufaRegistryStack st(L);
 if(auto*old=state(r)){if(live(old)&&roots(old)){if(error)error->clear();return true;}return fail("R53 source roots changed; no recapture after replacement");}
 AILuaInspect::globals(L);const void*g=lua_topointer(L,-1);
 AILuaInspect::field(L,-1,"sgs");if(!lua_istable(L,-1))return fail("R53 source namespace unavailable");const void*ns=lua_topointer(L,-1);
 AILuaInspect::field(L,-1,"ai_skill_choice");auto*t=AILuaInspect::table(L,-1);if(!t||t->metatable)return fail("R53 source choice registry is not plain");const void*c=lua_topointer(L,-1);
 auto*s=state(r,true);s->L=L;s->generation=rt->lua().generation();
 // Record only successfully registered identities so partial failure is safe.
 if(lua_observetableidentity(L,g,observe,s))s->globals=g;
 if(lua_observetableidentity(L,ns,observe,s))s->space=ns;
 if(lua_observetableidentity(L,c,observe,s))s->choices=c;
 s->valid=s->globals&&s->space&&s->choices;
 if(!s->valid)return fail("R53 complete source table observers unavailable");
 // No static absent/default grant here: source state is not public merely
 // because bootstrap can read it. Only a reached authorized own write grants.
 if(error)error->clear();return true;
}
void clearAIXufaChoiceRegistry(Room&r){delete state(r);}
void retireAIXufaChoiceRegistry(Room&r)noexcept{
 if(auto*s=state(r)){s->pending=false;s->revoke();}
}
ServerPlayer*currentAIXufaChoiceWriter(Room&r)noexcept{
 auto*s=state(r);
 return live(s)&&s->coverage&&s->stamp&&s->stamp==s->epoch&&s->writer
  &&s->writer->getRoom()==&r&&s->transport&&s->transport==r.roomRuntime()->xufaTransportEpoch()
  ?s->writer.data():nullptr;
}
quint64 currentAIXufaChoiceSerial(Room&r)noexcept{
 auto*s=state(r);return currentAIXufaChoiceWriter(r)&&s?s->stamp:0;
}
void enableAIXufaChoiceRegistryEntryCoverage(Room&r,bool complete){auto*s=state(r);if(!s)return;s->revoke();s->coverage=complete;}
void enterAIXufaChoiceRegistryPolicy(Room&r,ServerPlayer*actor,bool reentrant){auto*s=state(r);if(!s)return;
 if(reentrant||!actor||actor->getRoom()!=&r||(s->writer&&s->writer!=actor)){s->pending=false;s->revoke();}
}
QJsonObject writeAIXufaChoice(Room&r,ServerPlayer*p,const AIRequest&q,const QString&name){
 auto*s=state(r);if(!live(s)||!current(r,p,q)||q.kind!=AIRequest::Activate||q.reason!=CardUseStruct::CARD_USE_REASON_PLAY
  ||!p->hasSkill("xufa")||!current(r,p,q)||!disclosedName(r,p,q,name))return unknown();
 auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();
 if(!L||!lua_checkstack(L,512)||!live(s))return unknown();XufaRegistryStack st(L);
 if(!roots(s))return unknown();
 AILuaInspect::globals(L);AILuaInspect::field(L,-1,"sgs");AILuaInspect::field(L,-1,"ai_skill_choice");const int registry=lua_absindex(L,-1);
 const auto bytes=name.toUtf8();Write w{s,&r,p,&q,bytes};
 lua_pushcfunction(L,assign);lua_pushvalue(L,registry);lua_pushlightuserdata(L,&w);
 const int status=LuaRuntime::protectedCall(L,2,0,0);
 const bool own=w.reached&&s->pending&&s->writes==1&&w.before<std::numeric_limits<quint64>::max()&&s->epoch==w.before+1;
 s->pending=false;s->expected=false;
 if(status!=LUA_OK||!own||!live(s)||!w.transport||rt->xufaTransportEpoch()!=w.transport
  ||!current(r,p,q)||!roots(s))return unknown();
 AILuaInspect::field(L,registry,"xufa");size_t length=0;const char*value=lua_type(L,-1)==LUA_TSTRING?lua_tolstring(L,-1,&length):nullptr;
 if(!value||length!=size_t(bytes.size())||std::memcmp(value,bytes.constData(),length))return unknown();
 s->writer=p;s->stamp=s->epoch;s->transport=w.transport;
 // Successful actual prefix is independent of whether later continuity has a
 // complete entry proof. No value copy is retained in native or isolated VM.
 return{{"known",true},{"recorded",true}};
}
QJsonObject readAIXufaChoice(Room&r,ServerPlayer*p,const AIRequest&q){
 auto*s=state(r);if(currentAIXufaChoiceWriter(r)!=p||!p||!current(r,p,q)
  ||q.kind!=AIRequest::Choice||q.choiceOptions.reason!="xufa")return unknown();
 auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();if(!L||!lua_checkstack(L,64))return unknown();XufaRegistryStack st(L);
 if(!roots(s))return unknown();const auto stamp=s->stamp;
 AILuaInspect::globals(L);AILuaInspect::field(L,-1,"sgs");AILuaInspect::field(L,-1,"ai_skill_choice");AILuaInspect::field(L,-1,"xufa");
 if(lua_type(L,-1)!=LUA_TSTRING)return unknown();size_t n=0;const char*text=lua_tolstring(L,-1,&n);
 const QString value=QString::fromUtf8(text,int(n));
 if(currentAIXufaChoiceWriter(r)!=p||s->stamp!=stamp||s->epoch!=stamp||!current(r,p,q))return unknown();
 return{{"known",true},{"present",true},{"value",value}};
}

// ---- xufa-response ----
#include "ai-strategy/inovation-ai.h"
#include "skill.h"
#include <QJsonArray>
#include <QWeakPointer>
#include <cmath>
namespace {
struct Frame{
 Room*room=nullptr;QPointer<ServerPlayer>owner;QPointer<const Card>card;
 std::shared_ptr<const CardLifetimeToken>token;CardLifetimeLease lease;
 QString chosen;bool claimed=false;Frame*previous=nullptr;
 const Card*liveCard()const{
  // Identity custody for the exact accepted native outer SkillCard. This is
  // not a readonly observation and grants no access to its tags or graph.
  if(card.isNull()||!token||!token->live||!lease.isValid())return nullptr;
  return globalCardLifetimeManager().liveToken(card.data())==token?card.data():nullptr;
 }
};
thread_local Frame*active=nullptr;
struct Inquiry{Frame*origin=nullptr;Inquiry*previous=nullptr;quint64 decision=0,revision=0;
 QWeakPointer<AIConversionLedger>ledger;bool bound=false;};
thread_local Inquiry*inquiry=nullptr;
bool xufaResponseCurrent(const AIRequest&q,ServerPlayer*p){return p&&p->getRoom()&&q.decisionId>0
 &&p->getRoom()->getAllPlayers(true).contains(p)&&!p->getRoom()->roomRuntime()->isClosing()
 &&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()
 &&aiRangeRequestCurrent(*p->getRoom(),q,p);}
bool play(const AIRequest&q,ServerPlayer*p){if(!xufaResponseCurrent(q,p)||q.kind!=AIRequest::Activate
 ||q.reason!=CardUseStruct::CARD_USE_REASON_PLAY||!q.pattern.isEmpty()||p->getPhase()!=Player::Play)return false;
 // Presence is enough for a source readonly constructor. No extra hasSkill
 // invalidity/activation test is added to the reached source callback.
 for(const auto&i:p->getSkillInstances())if(i.skillName=="xufa")return true;return false;}
bool disclosed(const AIRequest&q,const Card*c){if(!c)return false;
 auto match=[&](const AICardView&v){return v.nativeIdentity&&globalCardLifetimeManager().isLive(v.nativeIdentity)
  &&v.nativeIdentity->address==c&&v.cardId==c->getId()&&v.effectiveId==c->getEffectiveId()
  &&v.objectName==c->objectName()&&v.className==c->getClassName()&&v.suit==int(c->getSuit())
  &&v.number==c->getNumber()&&v.skillName==c->getSkillName(false)&&v.subcardIds==c->getSubcards()
  &&v.virtualCard==c->isVirtualCard();};
 for(const auto&v:q.worldView.handCards)if(match(v))return true;
 for(const auto&v:q.worldView.self.equips)if(match(v))return true;
 for(const auto&pile:q.worldView.self.piles)for(const auto&v:pile.cards)if(match(v))return true;
 return false;}
bool material(const AIRequest&q,ServerPlayer*p,int id,bool pile){
 // Source getCards('he') returns inner equipment objects. Authenticate that
 // exact disclosed native object, never substitute outer wrapper metadata.
 for(const auto*c:p->getCards("he"))if(c&&c->getEffectiveId()==id&&disclosed(q,c))return true;
 return pile&&p->getPile("xufa").contains(id)&&disclosed(q,Sanguosha->getCard(id));
}
// The exact fixed ordinary V1 raw-wire path returns before GUI viewAs. Refuse
// private attachment resolution before it could select/inquire on a provider.
bool ordinaryRaw(ServerPlayer*p,const Card*c,QString&error){
 if(!c||c->getActivationSkillInstanceId()!=0){error="Xufa raw source unexpectedly carries an instance";return false;}
 const QString name=c->getActivationSkillName();const auto*vs=Sanguosha->getViewAsSkill(name);
 if(dynamic_cast<const ViewAsSkillV2*>(vs)){error="Xufa selected replacement reaches a V2 provider outside this fixed raw branch";return false;}
 bool ordinary=false,attached=false;
 for(const auto&i:p->getSkillInstances())if(i.skillName==name){
  if(i.source==SourceAttached&&p->getSkillInstanceStateValue(name,i.instanceID,"legacy_activation_lifecycle").toBool())attached=true;
  else ordinary=true;
 }
 if(attached&&!ordinary){error="Xufa solely attached provider requires separate native continuation";return false;}
 return true;
}
struct Receipt{QWeakPointer<AIConversionLedger>ledger;QPointer<ServerPlayer>owner;quint64 decision=0,revision=0;
 bool first=false,valuePlan=false;QPointer<const Card>source;QString wire;QStringList targets;AICardSpec spec;SkillInstanceRef activation,root;};
struct ConstructedSkillNames {std::weak_ptr<const CardLifetimeToken>token;QPointer<const Card>card;};
class Ledger final:public QObject{public:explicit Ledger(Room&r):QObject(&r){}QList<Receipt>rows;
 QHash<const Card*,ConstructedSkillNames>constructedSkills;};
Ledger*ledger(Room*r,bool create=false){if(!r)return nullptr;for(auto*c:r->children())if(auto*l=dynamic_cast<Ledger*>(c))return l;return create?new Ledger(*r):nullptr;}
// Called only on this operation's fresh Card::Parse product. Retain an origin
// for the fixed public value, never a copy of an arbitrary private skill chain.
void recordConstructedSkills(Room&r,const Card*c,bool first,bool valuePlan){
 if(!c||!c->isVirtualCard()||c->getSkillName(false)!="xufa"
  ||!(first?typeid(*c)==typeid(XufaCard):(valuePlan&&c->isNDTrick()))
  ||c->getSkillNames()!=QStringList{QStringLiteral("xufa")})return;
 const auto token=globalCardLifetimeManager().liveToken(c);if(!token||!token->live)return;
 auto&entries=ledger(&r,true)->constructedSkills;
 for(auto i=entries.begin();i!=entries.end();){const auto t=i->token.lock();
  if(!t||!t->live||i->card.isNull())i=entries.erase(i);else ++i;}
 if(!entries.contains(c))entries.insert(c,{token,c});
}
AICardSpec specOf(const Card*c,int id){AICardSpec s;s.name=c->objectName();s.suit=c->getSuit();s.number=c->getNumber();s.skillName=c->getSkillName(false);s.subcardIds=c->getSubcards();s.conversionId=id;return s;}
bool same(const AICardSpec&a,const AICardSpec&b){return a.name==b.name&&a.suit==b.suit&&a.number==b.number&&a.skillName==b.skillName&&a.subcardIds==b.subcardIds&&a.conversionId==b.conversionId;}
QJsonObject json(const AICardSpec&s){QJsonArray a;for(int id:s.subcardIds)a<<id;return{{"name",s.name},{"suit",s.suit},{"number",s.number},{"skill",s.skillName},{"subcards",a},{"conversion_id",s.conversionId}};}
bool prepare(const AIRequest&q,ServerPlayer*p,bool first,bool valuePlan,const AIReadonlyCardLease&lease,const QStringList&targets,
 const std::function<bool(CardUseStruct&)>&admit,CardUseStruct&use,QString&wire,QString&error){
 auto fail=[&](const char*s){error=QString::fromLatin1(s);return false;};
 if(!xufaResponseCurrent(q,p)||!admit||!lease)return fail("Xufa selected request or custody expired");
 const Card*saved=lease();if(!saved)return fail("Xufa exact selected source expired");
 if(first){
  if(!play(q,p)||typeid(*saved)!=typeid(XufaCard)||saved->getSkillName(false)!="xufa"||!targets.isEmpty())return fail("Xufa first selector differs from exact source SkillCard");
  if(saved->getSubcards().isEmpty())return fail("Xufa fixed first source never returns empty materials");
  for(int id:saved->getSubcards())if(!material(q,p,id,true))return fail("Xufa first source material is no longer disclosed owner HE/pile");
 }else{
  if(!aiXufaQuestion(q,p))return fail("Xufa saved selector is outside actual native inquiry");
  if(!valuePlan){
   auto actual=readAIXufaSlot(q,p);
   if(actual.kind!=AIXufaSlotRead::CardPlan||actual.targetsMissing||!actual.lease||actual.lease()!=saved||actual.targets!=targets)
    return fail("Xufa actual source card or ordered targets changed");
  }
  // This adapter closes the actual source xufa declaration and source-selected
  // physical replacements. Other fixed use-revise providers stay named debt.
  bool numeric=false;saved->toString().toInt(&numeric);
  if(!numeric&&(saved->getSkillName(false)!="xufa"||!saved->isNDTrick()||saved->getSubcards().size()>1))
   return fail("Xufa selected source replacement needs its own current provider adapter");
  if(valuePlan&&!numeric&&(saved->objectName()!=active->chosen||saved->getSkillName(false)!="xufa"))
   return fail("Xufa remembered conversion differs from the current native choice");
  if(numeric&&!material(q,p,saved->getEffectiveId(),false))return fail("Xufa physical replacement left disclosed owner HE");
  QSet<int>seen;
  for(int id:saved->getSubcards()){
   if(seen.contains(id)||!material(q,p,id,false))return fail("Xufa later trick material is duplicate or left disclosed owner HE");
   seen.insert(id);
  }
  if(!valuePlan&&saved->canRecast()&&targets.isEmpty()&&q.handlingMethod!=Card::MethodRecast)return fail("Xufa source declined recast before selection");
 }
 wire=saved->toString();
 // Parse schedules virtual products before returning. Protect that interval
 // until the following CardUseStruct acquires its event-payload native lease;
 // otherwise final native reconstruction can retire the product inside Parse.
 CardLifetimeScope parseCustody(globalCardLifetimeManager());
 const Card*parsed=Card::Parse(wire);if(!parsed)return fail("Xufa exact source wire did not parse");
 // Parse already scheduled virtual returns; normal event-payload custody only.
 use=CardUseStruct(parsed,p);
 recordConstructedSkills(*p->getRoom(),parsed,first,valuePlan);
 // This is the new native parse product of the current value continuation,
 // never the remembered card or an old receipt. Capture only its constructor
 // absence of nosjiefan-slash; later flag writes invalidate the existing epoch.
 if(valuePlan&&!first)recordAIFreshXufaValueDuel(*p->getRoom(),parsed);
 for(const auto&name:targets){auto*t=p->getRoom()->findPlayerByObjectName(name,true);if(!t)return fail("Xufa ordered source target left roster");use.to<<t;}
 if(!ordinaryRaw(p,parsed,error)||!admit(use))return fail(error.isEmpty()?"Xufa original native raw provider rejected":error.toUtf8().constData());
 if(!xufaResponseCurrent(q,p)||lease()!=saved||saved->toString()!=wire)return fail("Xufa source changed during raw reconstruction");
 if(!first&&!valuePlan){auto actual=readAIXufaSlot(q,p);if(actual.kind!=AIXufaSlotRead::CardPlan||actual.targetsMissing||!actual.lease||actual.lease()!=saved||actual.targets!=targets)return fail("Xufa actual slot changed during raw reconstruction");}
 return true;
}
QJsonObject issue(const AIRequest&q,ServerPlayer*p,bool first,bool valuePlan,AIReadonlyCardLease source,const QStringList&targets,const std::function<bool(CardUseStruct&)>&admit){
 CardUseStruct use;QString wire,error;if(!prepare(q,p,first,valuePlan,source,targets,admit,use,wire,error))return{{"error",error}};
 if(!q.conversionLedger)return{{"error","Xufa current conversion ledger unavailable"}};
 const int id=q.cardConversions.size()+q.conversionLedger->tickets.size()+1;
 Receipt receipt;receipt.ledger=q.conversionLedger;receipt.owner=p;receipt.decision=q.decisionId;receipt.revision=q.stateRevision;
 receipt.first=first;receipt.valuePlan=valuePlan;receipt.source=source();q.conversionLedger->observedProducts.insert(id,std::move(source));receipt.wire=wire;receipt.targets=targets;receipt.spec=specOf(use.card,id);receipt.activation=use.activationRef;receipt.root=use.sourceRef;
 AICardConversionView marker;marker.conversionId=id;marker.policyClass="@xufa_raw_source";marker.name=receipt.spec.name;marker.available=false;
 q.conversionLedger->tickets<<marker;p->getRoom()->roomRuntime()->ai().trackObservationLedger(q.conversionLedger);auto*l=ledger(p->getRoom(),true);
 for(auto i=l->rows.begin();i!=l->rows.end();)if(i->ledger.isNull())i=l->rows.erase(i);else ++i;l->rows<<receipt;
 return{{"card_spec",json(receipt.spec)},{"targets",QJsonArray::fromStringList(targets)}};
}
}
QJsonObject readAIXufaConstructedSkills(Room&r,ServerPlayer*viewer,const Card*c){
 if(!viewer||viewer->getRoom()!=&r||!c)return{};
 auto*l=ledger(&r);if(!l)return{};const auto i=l->constructedSkills.constFind(c);
 if(i==l->constructedSkills.cend()||i->card.isNull()||i->card.data()!=c)return{};
 const auto token=i->token.lock();
 if(!token||!token->live||globalCardLifetimeManager().liveToken(c)!=token
  ||c->getSkillNames()!=QStringList{QStringLiteral("xufa")})return{};
 return{{"skill_names",QJsonArray{QStringLiteral("xufa")}},{"skill_names_complete",true}};
}
struct AIXufaQuestionScope::State{Frame f;};
AIXufaQuestionScope::AIXufaQuestionScope(Room&r,ServerPlayer*p,const Card*c,const QString&chosen):d(new State){
 d->f.previous=active;active=&d->f; // Invalid/nested entries shadow the outer scope.
 if(p&&p->getRoom()==&r&&c&&typeid(*c)==typeid(XufaCard)&&!chosen.isEmpty()){
  d->f.room=&r;d->f.owner=p;d->f.card=c;d->f.chosen=chosen;
  // This compiled on-use scope borrows its actual live `this` object. The
  // native factory need not have registered it yet: observeCard registers
  // lifetime/destruction tracking without taking ownership or disclosing face.
  d->f.token=globalCardLifetimeManager().observeCard(const_cast<Card*>(c));
  d->f.lease=CardLifetimeLease(globalCardLifetimeManager(),d->f.token);
 }
}
AIXufaQuestionScope::~AIXufaQuestionScope(){active=d->f.previous;}
struct AIXufaInquiryScope::State{Inquiry i;};
AIXufaInquiryScope::AIXufaInquiryScope(Room&r,ServerPlayer*p,const QString&pattern,const QString&prompt,int method):d(new State){
 d->i.previous=inquiry;inquiry=&d->i; // Every native inquiry shadows the outer one.
 if(!active||active->claimed)return;
 active->claimed=true; // The compiled origin may authorize only its immediate first inquiry.
 if(active->room==&r&&active->owner==p&&active->liveCard()
  &&pattern=="@@xufa"&&prompt=="xufa0:"+active->chosen&&method==int(Card::MethodUse))d->i.origin=active;
}
AIXufaInquiryScope::~AIXufaInquiryScope(){inquiry=d->i.previous;}
namespace {
bool questionShape(const AIRequest&q,ServerPlayer*p){return xufaResponseCurrent(q,p)&&inquiry&&inquiry->origin
 &&inquiry->origin==active&&active->room==p->getRoom()&&active->owner==p&&active->liveCard()
 &&q.kind==AIRequest::UseCard&&q.pattern=="@@xufa"&&q.prompt=="xufa0:"+active->chosen
 &&q.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&q.handlingMethod==Card::MethodUse;}
}
void bindAIXufaQuestion(const AIRequest&q,ServerPlayer*p){
 if(!questionShape(q,p)||inquiry->bound||!q.conversionLedger)return;
 inquiry->bound=true;inquiry->decision=q.decisionId;inquiry->revision=q.stateRevision;inquiry->ledger=q.conversionLedger;
}
bool aiXufaQuestion(const AIRequest&q,ServerPlayer*p){return questionShape(q,p)&&inquiry->bound
 &&inquiry->decision==q.decisionId&&inquiry->revision==q.stateRevision
 &&q.conversionLedger&&inquiry->ledger.toStrongRef()==q.conversionLedger;}
std::unique_ptr<Card> constructAIXufaFirst(const AIRequest&q,ServerPlayer*p,const QList<int>&costs,QString*error){
 auto fail=[&](const char*s)->std::unique_ptr<Card>{if(error)*error=QString::fromLatin1(s);return{};};
 if(!play(q,p)||costs.isEmpty())return fail("Xufa first source requires reached owner Play and nonempty selected list");
 for(int id:costs)if(!material(q,p,id,true))return fail("Xufa initial source material is not current disclosed owner HE/pile");
 std::unique_ptr<Card>c(new XufaCard);c->setSkillName("xufa");c->addSubcards(costs);
 if(error)error->clear();return c;
}
QJsonObject issueAIXufaFirst(const AIRequest&q,ServerPlayer*p,AIReadonlyCardLease c,const std::function<bool(CardUseStruct&)>&f){return issue(q,p,true,false,std::move(c),{},f);}
QJsonObject issueAIXufaSaved(const AIRequest&q,ServerPlayer*p,AIReadonlyCardLease c,const QStringList&t,const std::function<bool(CardUseStruct&)>&f){return issue(q,p,false,false,std::move(c),t,f);}
QJsonObject issueAIXufaValue(const AIRequest&q,ServerPlayer*p,const QJsonObject&v,const QStringList&t,
 const std::function<bool(CardUseStruct&)>&admit){
 auto fail=[](const char*s){return QJsonObject{{"error",QString::fromLatin1(s)}};};
 if(!aiXufaQuestion(q,p))return fail("Xufa pure plan requires the current native inquiry");
 auto integer=[](const QJsonValue&v,int low,int high){const double n=v.toDouble();return v.isDouble()&&std::isfinite(n)&&n==std::floor(n)&&n>=low&&n<=high;};
 if(v.size()!=9||!v.value("virtual").isBool()||!integer(v.value("id"),-1,std::numeric_limits<int>::max())
  ||!v.value("name").isString()||!integer(v.value("suit"),0,6)||!integer(v.value("number"),0,13)
  ||!v.value("skill").isString()||!v.value("can_recast").isBool()||!v.value("wire").isString())
  return fail("Xufa remembered card descriptor malformed");
 QJsonValue ids=v.value("materials");
 if(ids.isObject()&&ids.toObject().isEmpty())ids=QJsonArray();
 if(!ids.isArray())return fail("Xufa remembered materials malformed");
 QList<int>materials;for(const auto&id:ids.toArray()){
  if(!integer(id,0,std::numeric_limits<int>::max()))return fail("Xufa remembered material id malformed");materials<<id.toInt();}
 AIReadonlyCardLease lease;
 if(v.value("virtual").toBool()){
  if(v.value("skill").toString()!="xufa"||v.value("name").toString()!=active->chosen||materials.size()>1)
   return fail("Xufa remembered virtual replacement needs a current provider adapter");
  for(int id:materials)if(!material(q,p,id,false))return fail("Xufa remembered material left disclosed owner HE");
  std::unique_ptr<Card>rebuilt(Sanguosha->cloneCard(v.value("name").toString(),Card::Suit(v.value("suit").toInt()),v.value("number").toInt()));
  if(!rebuilt||!rebuilt->isNDTrick())return fail("Xufa current trick constructor unavailable");
  rebuilt->setSkillName("xufa");rebuilt->addSubcards(materials);
  lease=AIOwnerReadonlyCardStore::own(std::move(rebuilt));
 }else{
  const int id=v.value("id").toInt();
  if(!material(q,p,id,false))return fail("Xufa physical replacement left disclosed owner HE");
  lease=AIOwnerReadonlyCardStore::observe(Sanguosha->getCard(id));
 }
 const Card*c=lease?lease():nullptr;
 if(!c||c->isVirtualCard()!=v.value("virtual").toBool()||c->toString()!=v.value("wire").toString())
  return fail("Xufa remembered card no longer reconstructs its source wire");
 if(c->isVirtualCard()&&(c->getEffectiveId()!=v.value("id").toInt()
  ||c->objectName()!=v.value("name").toString()||int(c->getSuit())!=v.value("suit").toInt()
  ||c->getNumber()!=v.value("number").toInt()||c->getSkillName(false)!=v.value("skill").toString()
  ||c->getSubcards()!=materials))
  return fail("Xufa virtual description does not match its current reconstruction");
 // A physical donor card may be an inner equip; toString() still selects its
 // numeric ID. Native Parse uses the current outer wrapper, just as the donor.
 // can_recast belongs to the remembered Lua branch, not to reconstructed flags.
 // Retain only this CURRENT-request reconstruction until native acceptance.
 // No old native pointer, recipe, conversion ticket or payment authority is read.
 return issue(q,p,false,true,std::move(lease),t,admit);
}
bool hasAIXufaReceipt(const AIRequest&q,int id){if(!q.conversionLedger)return false;for(const auto&t:q.conversionLedger->tickets)if(t.conversionId==id&&t.policyClass=="@xufa_raw_source")return true;return false;}
bool applyAIXufaReceipt(const AIRequest&q,ServerPlayer*p,const AICardSpec&spec,const QStringList&t,const std::function<bool(CardUseStruct&)>&admit,CardUseStruct&use){
 if(!xufaResponseCurrent(q,p)||!hasAIXufaReceipt(q,spec.conversionId))return false;auto*l=ledger(p->getRoom());if(!l)return false;
 for(const auto&e:l->rows)if(e.ledger.toStrongRef()==q.conversionLedger&&e.owner==p&&e.decision==q.decisionId&&e.revision==q.stateRevision&&same(e.spec,spec)&&e.targets==t){
  const Receipt copy=e;QString wire,error;CardUseStruct actual;
  const auto source=q.conversionLedger->observedProducts.value(spec.conversionId);
  if(!source||!copy.source||source()!=copy.source.data())return false;
  if(!prepare(q,p,copy.first,copy.valuePlan,source,t,admit,actual,wire,error)||wire!=copy.wire||!same(specOf(actual.card,spec.conversionId),spec)||actual.activationRef!=copy.activation||actual.sourceRef!=copy.root)return false;
  actual.m_validateTargets=true;use=actual;return true;
 }return false;
}

// ---- xufa-slot ----
#include "lua.hpp"
#include <QUuid>
#include <memory>
namespace {
struct XufaSlotStack{lua_State*L;int n;explicit XufaSlotStack(lua_State*s):L(s),n(lua_gettop(s)){}~XufaSlotStack(){lua_settop(L,n);}};
struct Row{
 QPointer<ServerPlayer>owner;AIReadonlyCardLease lease;QString token;
 int tableRef=LUA_NOREF,offered=-1;quint64 producerDecision=0,producerRevision=0;
 QList<int>materials;int physical=-1;
};
class XufaSlotLedger final:public QObject{
public:explicit XufaSlotLedger(Room&r):QObject(&r){}lua_State*L=nullptr;quint64 generation=0;
 QHash<const ServerPlayer*,Row>rows;
};
XufaSlotLedger*xufaSlotLedger(Room&r,bool create=false){for(auto*c:r.children())if(auto*l=dynamic_cast<XufaSlotLedger*>(c))return l;return create?new XufaSlotLedger(r):nullptr;}
bool xufaSlotCurrent(const AIRequest&q,ServerPlayer*p){return p&&p->getRoom()&&q.decisionId>0
 &&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()
 &&aiRangeRequestCurrent(*p->getRoom(),q,p)&&Sanguosha&&Sanguosha->currentRoom()==p->getRoom();}
bool shown(const AIRequest&q,int id,const Card*exact=nullptr){
 // IDs in printed notification DTOs are not evidence of current wrapped face.
 // Authenticate the actually projected object BEFORE consulting its fields.
 const Card*outer=Sanguosha->getCard(id);if(!outer)return false;
 auto matches=[&](const AICardView&v){if(v.effectiveId!=id||!v.nativeIdentity
   ||!globalCardLifetimeManager().isLive(v.nativeIdentity))return false;
  // Owner HE exposes inner equipment, hand exposes the wrapper. Decide by
  // already-issued identity before comparing any current face fields.
  const Card*observed=exact;
  if(!observed){if(v.nativeIdentity->address==outer)observed=outer;
   else if(v.nativeIdentity->address==outer->getRealCard())observed=outer->getRealCard();}
  if(!observed||v.nativeIdentity->address!=observed)return false;
  return v.cardId==observed->getId()&&v.effectiveId==observed->getEffectiveId()
   &&v.objectName==observed->objectName()&&v.className==observed->getClassName()
   &&v.suit==int(observed->getSuit())&&v.number==observed->getNumber()
   &&v.skillName==observed->getSkillName(false)&&v.subcardIds==observed->getSubcards()
   &&v.virtualCard==observed->isVirtualCard();
 };
 for(const auto&c:q.worldView.handCards)if(matches(c))return true;
 for(const auto&c:q.worldView.discardPile)if(matches(c))return true;
 for(const auto&c:q.choiceOptions.cards)if(matches(c))return true;
 auto ps=q.worldView.players;ps.prepend(q.worldView.self);
 for(const auto&p:ps){for(const auto&c:p.knownCards)if(matches(c))return true;for(const auto&c:p.equips)if(matches(c))return true;
  for(const auto&c:p.judgingArea)if(matches(c))return true;for(const auto&pile:p.piles)for(const auto&c:pile.cards)if(matches(c))return true;}
 return false;
}
void xufaSlotField(lua_State*L,int i,const char*k){AILuaInspect::field(L,i,k);}
bool self(lua_State*L,Room&r,ServerPlayer*p){return pushAIActualAISelf(r,p,L);}
bool absentNewIndex(lua_State*L,int t){XufaSlotStack st(L);if(!lua_getmetatable(L,t))return true;xufaSlotField(L,-1,"__newindex");return lua_isnil(L,-1);}
bool plain(lua_State*L,int n){auto*t=AILuaInspect::table(L,n);return t&&!t->metatable;}
bool xufaSlotEnv(lua_State*L,int f){f=lua_absindex(L,f);for(int n=1;;++n){const char*k=lua_getupvalue(L,f,n);if(!k)break;bool ok=false;if(!std::strcmp(k,"_ENV")){AILuaInspect::globals(L);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}lua_pop(L,1);if(!ok)return false;}return true;}
bool xufaSlotCode(Room&r,lua_State*L,int f,const char*path,int first,int last){return matchAIPinnedFunction(r,L,f,path,first,last)&&xufaSlotEnv(L,f);}
bool sourceCallback(Room&r,lua_State*L,QString*why){
 auto fail=[&](const char*text){if(why)*why=QString::fromLatin1(text);return false;};
 XufaSlotStack stack(L);AILuaInspect::globals(L);xufaSlotField(L,-1,"sgs");if(!lua_istable(L,-1))return fail("Xufa source sgs table unavailable");
 xufaSlotField(L,-1,"ai_skill_use");if(!plain(L,-1))return fail("Xufa source use registry is not plain");xufaSlotField(L,-1,"@@xufa");
 if(!matchAIPinnedFunction(r,L,-1,"lua/ai/ol_qifu-ai.lua",1203,1215,why))return false;
 if(!xufaSlotEnv(L,-1))return fail("Xufa pinned source reader environment changed");
 if(why)why->clear();return true;
}
// Exact table-valued __index lookup. Never execute authored getter functions.
bool lookup(lua_State*L,int value,const char*key){
 value=lua_absindex(L,value);QSet<const void*>seen;
 for(int depth=0;depth<16;++depth){if(!lua_istable(L,value)||seen.contains(lua_topointer(L,value)))return false;
  seen.insert(lua_topointer(L,value));xufaSlotField(L,value,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);
  if(!lua_getmetatable(L,value)){lua_pushnil(L);return true;}
  xufaSlotField(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;value=lua_absindex(L,-1);
 }return false;
}
bool sourceReadBindings(Room&r,lua_State*L){
 XufaSlotStack stack(L);AILuaInspect::globals(L);const int globals=lua_absindex(L,-1);
 xufaSlotField(L,globals,"sgs");if(!lua_istable(L,-1))return false;const int sgs=lua_absindex(L,-1);
 xufaSlotField(L,sgs,"ai_skill_use");if(!plain(L,-1))return false;xufaSlotField(L,-1,"@@xufa");
 if(!xufaSlotCode(r,L,-1,"lua/ai/ol_qifu-ai.lua",1203,1215))return false;
 xufaSlotField(L,sgs,"list");if(!xufaSlotCode(r,L,-1,"lua/utilities.lua",38,41))return false;
 xufaSlotField(L,sgs,"qlist");const int qlist=lua_absindex(L,-1);
 if(!matchAIPinnedFunction(r,L,qlist,"lua/utilities.lua",19,35))return false;
 for(int n=1;;++n){const char*k=lua_getupvalue(L,qlist,n);if(!k)break;bool ok=false;
  if(!std::strcmp(k,"_ENV")){lua_pushvalue(L,globals);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}
  else if(!std::strcmp(k,"qlist_userdata_iterator"))ok=xufaSlotCode(r,L,-1,"lua/utilities.lua",7,11);
  else if(!std::strcmp(k,"qlist_table_iterator"))ok=xufaSlotCode(r,L,-1,"lua/utilities.lua",13,17);
  lua_pop(L,1);if(!ok)return false;
 }
 for(const char*k:{"type","ipairs"}){xufaSlotField(L,globals,k);if(!matchAINativeBinding(r,L,-1,k))return false;lua_pop(L,1);}
 xufaSlotField(L,globals,"table");if(!plain(L,-1))return false;const int table=lua_absindex(L,-1);
 for(const char*k:{"insert","concat"}){xufaSlotField(L,table,k);if(!matchAINativeBinding(r,L,-1,QString("table.")+k))return false;lua_pop(L,1);}
 return matchAINativeClassMethod(r,L,"SPlayerList","length")&&matchAINativeClassMethod(r,L,"SPlayerList","at")
  &&matchAINativeDerivedMethods(r,L,"ServerPlayer",{"objectName"});
}
// Executes only our fixed native assignment thunk. No legacy AI strategy call.
// The caller proved source-equivalent __newindex behavior before entering it.
struct XufaSlotWrite {const AIRequest*request;ServerPlayer*owner;AIReadonlyCardLease lease;const Card*card;QList<ServerPlayer*>targets;int extra=0;};
int xufaSlotAssign(lua_State*L){
 // This is a new protected C call frame; the outer host reserve is not inherited.
 if(!lua_checkstack(L,4096))return luaL_error(L,"Xufa assignment frame capacity unavailable");
 auto*w=static_cast<XufaSlotWrite*>(lua_touserdata(L,2));
 lua_newtable(L);const int d=lua_gettop(L);
 lua_pushboolean(L,1);lua_setfield(L,d,"isDummy");
 lua_pushinteger(L,w->extra);lua_setfield(L,d,"extra_target");
 lua_newtable(L);lua_setfield(L,d,"current_targets");
 if(!pushAISlotCard(L,w->card))return luaL_error(L,"Xufa native Card wrapper unavailable");lua_setfield(L,d,"card");
 if(!pushAISlotTargets(L,w->targets))return luaL_error(L,"Xufa native target list unavailable");lua_setfield(L,d,"to");
 if(!xufaSlotCurrent(*w->request,w->owner)||w->lease()!=w->card||!self(L,*w->owner->getRoom(),w->owner)||!lua_rawequal(L,1,-1))
  return luaL_error(L,"Xufa actual owner/request changed during allocation");
 lua_pop(L,1);
 // Preserve source assignment (not rawset). If GC changed the exact receiver's
 // absent-key metatable after preflight, reject before dispatching that hook.
 xufaSlotField(L,1,"xufaUse");const bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(!present&&!absentNewIndex(L,1))return luaL_error(L,"Xufa source __newindex changed during allocation");
 lua_pushvalue(L,d);lua_setfield(L,1,"xufaUse");
 lua_pushvalue(L,d);return 1;
}
}
bool registerAIXufaSlotSources(Room&r,lua_State*L,QString*error){
 // Independently fixed source bytes, not a hash derived from the installed
 // effective callback. The common seal verifies runtime generation and compiles
 // metadata without executing any package policy. Never reseal on a reader.
 if(!registerAIPinnedSource(r,L,"lua/ai/ol_qifu-ai.lua",
     QByteArray("b7dbe8d9686dc3cda25fff81a5e6aa428c560262b603f42cb3bac98764c0952f"),error))return false;
 return registerAIPinnedSource(r,L,"lua/utilities.lua",
     QByteArray("b76cdc44c0fec46f1e10d918c6e0676c5e301ed92cd5b0e35da882606a7d4d3a"),error);
}
void clearAIXufaSlots(Room&r){auto*l=xufaSlotLedger(r);if(!l)return;
 auto*rt=r.roomRuntime();if(rt&&rt->lua().rawState()&&rt->lua().generation()==l->generation){
  LuaRuntime::Binding b(rt->lua(),false);auto*L=rt->lua().state();if(L==l->L)for(const auto&row:l->rows)if(row.tableRef!=LUA_NOREF)luaL_unref(L,LUA_REGISTRYINDEX,row.tableRef);
 }delete l;
}
QString writeAIXufaSlot(const AIRequest&q,ServerPlayer*p,AIReadonlyCardLease lease,const QStringList&names,const QJsonObject&fields,QString*error){
 auto fail=[&](const QString&s){if(error)*error=s;return QString();};
 if(!xufaSlotCurrent(q,p)||q.kind!=AIRequest::Activate||q.reason!=CardUseStruct::CARD_USE_REASON_PLAY||p->getPhase()!=Player::Play||!p->hasSkill("xufa")||!lease)
  return fail("Xufa source assignment requires current owner Play producer");
 if(!xufaSlotCurrent(q,p))return fail("Xufa skill lookup invalidated producer request");
 if(fields.size()!=3||fields.value("isDummy")!=QJsonValue(true)||!fields.value("extra_target").isDouble()
  ||fields.value("extra_target").toDouble()!=fields.value("extra_target").toInt()||fields.value("current_targets_empty")!=QJsonValue(true))return fail("Xufa full dummy field envelope unsupported");
 const Card*card=lease();if(!card)return fail("Xufa exact producer custody expired");
 const int physical=card->isVirtualCard()?-1:card->getEffectiveId();if(physical>=0&&!shown(q,physical,card))return fail("Xufa selected physical replacement undisclosed");
 const auto costs=card->getSubcards();for(int id:costs)if(!shown(q,id))return fail("Xufa selected source material undisclosed");
 Room&r=*p->getRoom();auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();
 if(!L||!lua_checkstack(L,4096))return fail("Xufa game runtime unavailable");XufaSlotStack stack(L);
 if(!self(L,r,p))return fail("Xufa actual LuaAI self authentication failed");const int receiver=lua_absindex(L,-1);
 xufaSlotField(L,receiver,"xufaUse");const bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(!present&&!absentNewIndex(L,receiver))return fail("Xufa source __newindex is not direct assignment");
 XufaSlotWrite w;w.request=&q;w.owner=p;w.lease=lease;w.card=card;w.extra=fields.value("extra_target").toInt();
 const auto roster=r.getAllPlayers(true);for(const auto&name:names){ServerPlayer*target=nullptr;for(auto*t:roster)if(t&&t->objectName()==name){target=t;break;}if(!target)return fail("Xufa source target outside roster");w.targets<<target;}
 lua_pushcfunction(L,xufaSlotAssign);lua_pushvalue(L,receiver);lua_pushlightuserdata(L,&w);
 if(LuaRuntime::protectedCall(L,2,1,0)!=LUA_OK)return fail("Xufa native source assignment did not complete");
 if(!xufaSlotCurrent(q,p)||!lease())return fail("Xufa assignment changed current request or custody");
 xufaSlotField(L,receiver,"xufaUse");const bool exact=lua_rawequal(L,-1,-2);lua_pop(L,1);if(!exact||!plain(L,-1))return fail("Xufa source assignment was replaced");
 auto*l=xufaSlotLedger(r,true);if(l->L&&l->generation!=rt->lua().generation())return fail("Xufa source runtime generation changed");l->L=L;l->generation=rt->lua().generation();
 Row row;row.owner=p;row.lease=std::move(lease);row.token="xufa-actual-slot:"+QUuid::createUuid().toString(QUuid::WithoutBraces);row.materials=costs;row.physical=physical;row.producerDecision=q.decisionId;row.producerRevision=q.stateRevision;
 lua_pushvalue(L,-1);row.tableRef=luaL_ref(L,LUA_REGISTRYINDEX);
 auto old=l->rows.find(p);if(old!=l->rows.end()&&old->tableRef!=LUA_NOREF)luaL_unref(L,LUA_REGISTRYINDEX,old->tableRef);
 l->rows[p]=row; // Retain reached assignment even if the later global choice write / Parse errors.
 if(!xufaSlotCurrent(q,p)||!row.lease())return fail("Xufa source publication changed request or custody");
 if(error)error->clear();return row.token;
}
AIXufaSlotRead readAIXufaSlot(const AIRequest&q,ServerPlayer*p){
 AIXufaSlotRead out;auto fail=[&](const QString&s){out.error=s;return out;};
 if(!xufaSlotCurrent(q,p)||q.kind!=AIRequest::UseCard||q.pattern!="@@xufa"||!p->hasSkill("xufa"))return fail("Xufa slot read requires exact current owner response");
 if(!xufaSlotCurrent(q,p))return fail("Xufa skill lookup invalidated reader request");
 auto finish=[&](){if(!xufaSlotCurrent(q,p)){out=AIXufaSlotRead();out.error="Xufa source read invalidated current request";}return out;};
 Room&r=*p->getRoom();auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();
 if(!L||!lua_checkstack(L,4096))return fail("Xufa actual source runtime unavailable");XufaSlotStack stack(L);
 QString readerWhy;
 if(!sourceCallback(r,L,&readerWhy))return fail("Xufa source reader: "+readerWhy);
 if(!self(L,r,p))return fail("Xufa actual owner self authentication changed");
 if(!lookup(L,-1,"xufaUse"))return fail("Xufa actual slot lookup invokes unproved getter");
 // Exact own source short circuits, without reading a hidden Card or targets.
 if(lua_isnil(L,-1)){if(lua_getmetatable(L,-1))return fail("Xufa nil indexing metatable changed");out.kind=AIXufaSlotRead::NilPlan;return finish();}
 if(lua_isboolean(L,-1)&&!lua_toboolean(L,-1)){if(lua_getmetatable(L,-1))return fail("Xufa boolean indexing metatable changed");out.kind=AIXufaSlotRead::FalsePlan;return finish();}
 if(!lua_istable(L,-1))return fail("Xufa actual plan is outside source table domain");const int d=lua_absindex(L,-1);
 if(!lookup(L,d,"card"))return fail("Xufa card lookup invokes unproved getter");
 if(lua_isnil(L,-1)||(lua_isboolean(L,-1)&&!lua_toboolean(L,-1))){out.kind=AIXufaSlotRead::EmptyCard;return finish();}
 auto*l=xufaSlotLedger(r);if(!l||l->L!=L||l->generation!=rt->lua().generation())return fail("Xufa actual selected Card has no retained observation");
 const auto it=l->rows.constFind(p);if(it==l->rows.cend()||it->owner!=p)return fail("Xufa current owner has no retained producer");const Row row=*it;
 const Card*card=row.lease?row.lease():nullptr;if(!card)return fail("Xufa actual source Card lifetime expired");
 if(!matchAISlotCard(L,-1,card)||!matchAINativeObjectMethods(r,L,-1,"Card",{"canRecast","toString"}))return fail("Xufa source Card changed beyond retained identity");
 if(row.physical>=0&&!shown(q,row.physical,card))return fail("Xufa physical source is no longer lawfully disclosed");
 for(int id:row.materials)if(!shown(q,id))return fail("Xufa material observation expired");
 if(card->getSubcards()!=row.materials)return fail("Xufa retained material provenance changed");lua_pop(L,1);
 if(!sourceReadBindings(r,L))return fail("Xufa reached reader dependencies changed");
 if(!lookup(L,d,"to"))return fail("Xufa target lookup invokes unproved getter");
 if(lua_isnil(L,-1)||(lua_isboolean(L,-1)&&!lua_toboolean(L,-1))){out.kind=AIXufaSlotRead::CardPlan;out.targetsMissing=true;out.targetsFalse=lua_isboolean(L,-1);out.lease=row.lease;return finish();}
 QList<ServerPlayer*>actual;if(!readAISlotTargets(L,-1,actual))return fail("Xufa actual source target list custody changed");
 const auto roster=r.getAllPlayers(true);for(auto*t:actual){if(!t||!roster.contains(t))return fail("Xufa source target left roster");out.targets<<t->objectName();}
 if(!xufaSlotCurrent(q,p)||row.lease()!=card)return fail("Xufa source read changed request or custody");
 out.kind=AIXufaSlotRead::CardPlan;out.lease=row.lease;return finish();
}
std::unique_ptr<Card> constructAIXufaPolicy(const AIRequest&q,ServerPlayer*p,const QString&name,const QList<int>&costs,QString*error){
 auto fail=[&](const char*s)->std::unique_ptr<Card>{if(error)*error=s;return {};};
 if(!xufaSlotCurrent(q,p)||q.kind!=AIRequest::Activate||q.reason!=CardUseStruct::CARD_USE_REASON_PLAY||p->getPhase()!=Player::Play||!p->hasSkill("xufa")||costs.size()>1)
  return fail("Xufa dummy constructor requires exact owner Play source");
 if(!xufaSlotCurrent(q,p))return fail("Xufa skill lookup invalidated constructor request");
 QList<int>he=p->handCards();for(auto*c:p->getEquips())he<<c->getEffectiveId();auto names=he;names<<p->getPile("xufa");bool found=false;
 for(int id:names){if(!shown(q,id))continue;const Card*c=Sanguosha->getCard(id);if(c&&c->isNDTrick()&&c->objectName()==name){found=true;break;}}
 if(!found)return fail("Xufa dummy name is not an actual disclosed source NDTrick");
 for(int id:costs)if(!he.contains(id)||!shown(q,id))return fail("Xufa dummy material is not exact disclosed owner HE");
 std::unique_ptr<Card>card(Sanguosha->cloneCard(name,Card::NoSuit,0));
 if(!card)return fail("Xufa source dummy clone is absent");card->setSkillName("xufa");for(int id:costs)card->addSubcard(id);
 if(!xufaSlotCurrent(q,p))return fail("Xufa source clone invalidated constructor request");
 if(error)error->clear();return card;
}

// ---- xufa-filter ----
#include "room-state.h"
#include "structs.h"
// Lua internals define macros conflicting with Qt headers: include last.
namespace {
thread_local QHash<Room*,QList<const AIXufaMoveBodyScope*>> frames;
thread_local QHash<Room*,QList<const AIXufaMoveEntry*>> entries;
using namespace AILuaInspect;
struct XufaFilterStack {lua_State*L;int top;explicit XufaFilterStack(lua_State*p):L(p),top(lua_gettop(p)){}~XufaFilterStack(){lua_settop(L,top);}};
bool xufaFilterPlain(lua_State*L,int n){auto*t=table(L,n);return t&&!t->metatable;}
struct Source{const char*path;const char*sha;};
const Source sources[]={
 {"lua/ai/chat-ai.lua","29e507189bc261e46ec9ab0fa327781833ccf9c3e7962521eddc471d478ba98e"},
 {"lua/ai/original-hegemony/heg-common-ai.lua","9ad128880b7876a95d022551533a8168ef924173a29d704af837cd5edb98ff7f"},
 {"extensions/addFunction.lua","32938f895048d1abc62a119f179ed356fda155d1ccc8ba40664ff81c2ecba046"},
 {"lua/utilities.lua","b76cdc44c0fec46f1e10d918c6e0676c5e301ed92cd5b0e35da882606a7d4d3a"}
};
struct Graph {
 Room&r;lua_State*L;int sgs=0;QString why;
 bool fail(const QString&s){if(why.isEmpty())why=s;return false;}
 bool env(int f,const char*unused=nullptr){f=lua_absindex(L,f);
  for(int i=1;;++i){const char*n=lua_getupvalue(L,f,i);if(!n)break;bool ok=false;
   if(!std::strcmp(n,"_ENV")){globals(L);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}
   else if(unused&&!std::strcmp(n,unused))ok=true;
   lua_pop(L,1);if(!ok)return fail("Xufa reached closure binding differs");
  }return true;
 }
 bool code(int f,const char*p,int a,int b,const char*unused=nullptr){
  return (matchAIPinnedFunction(r,L,f,p,a,b)&&env(f,unused))||fail(QString("Xufa source closure: ")+p+':'+QString::number(a));
 }
 bool globalCode(const char*key,const char*p,int a,int b){XufaFilterStack st(L);globals(L);field(L,-1,key);return code(-1,p,a,b);}
 bool native(const char*space,const char*key){XufaFilterStack st(L);globals(L);if(space){field(L,-1,space);if(!xufaFilterPlain(L,-1))return fail("Xufa native library table is indirect");}field(L,-1,key);
  return matchAINativeBinding(r,L,-1,space?QString::fromLatin1(space)+'.'+key:QString::fromLatin1(key))||fail(QString("Xufa native binding: ")+(space?QString::fromLatin1(space)+'.':QString())+key);
 }
 bool method(const char*cls,const char*key){return matchAINativeClassMethod(r,L,cls,key)||fail(QString("Xufa method: ")+cls+'.'+key);}
 bool property(const char*cls,const char*key){return matchAINativeClassProperty(r,L,cls,key)||fail(QString("Xufa property: ")+cls+'.'+key);}
 bool constant(const char*key){XufaFilterStack st(L);field(L,sgs,key);return matchAINativeConstant(r,L,-1,QStringLiteral("sgs.")+key)||fail(QString("Xufa constant: ")+key);}
 bool qlist(){XufaFilterStack st(L);field(L,sgs,"qlist");const int f=lua_absindex(L,-1);
  if(!matchAIPinnedFunction(r,L,f,"lua/utilities.lua",19,35))return fail("Xufa qlist source differs");
  for(int i=1;;++i){const char*n=lua_getupvalue(L,f,i);if(!n)break;bool ok=false;
   if(!std::strcmp(n,"_ENV")){globals(L);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}
   else if(!std::strcmp(n,"qlist_userdata_iterator"))ok=code(-1,"lua/utilities.lua",7,11);
   else if(!std::strcmp(n,"qlist_table_iterator"))ok=code(-1,"lua/utilities.lua",13,17);
   lua_pop(L,1);if(!ok)return fail("Xufa qlist captured iterator differs");
  }
  for(const char*cls:{"SPlayerList","IntList"})for(const char*key:{"length","at"})if(!method(cls,key))return false;
  return true;
 }
 bool callbacks(int event){XufaFilterStack st(L);field(L,sgs,"ai_event_callback");if(!xufaFilterPlain(L,-1))return fail("Xufa callback root is indirect");rawGeti(L,-1,event);
  if(!lua_istable(L,-1))return true; // source type guard, not invented default
  if(!xufaFilterPlain(L,-1))return fail("Xufa callback row is indirect");const int row=lua_absindex(L,-1);int count=0;lua_pushnil(L);
  while(next(L,row)){if(++count>4096)return fail("Xufa callback row bound");
   if(lua_isfunction(L,-1)){
    if(event!=int(CardsMoveOneTime))return fail("Xufa non-switch movement event has a callable sibling");
    bool ok=false;
    if(matchAIPinnedFunction(r,L,-1,"lua/ai/chat-ai.lua",1172,1184)||matchAIPinnedFunction(r,L,-1,"lua/ai/chat-ai.lua",1230,1260))ok=env(-1);
    else if(matchAIPinnedFunction(r,L,-1,"lua/ai/original-hegemony/heg-common-ai.lua",269,297))ok=env(-1,"public_card_lack");
    if(!ok)return fail("Xufa unknown CardsMoveOneTime sibling");
   }lua_pop(L,1);
  }return true;
 }
 bool handPileBody(){
  // This delegate is not a native event admission root. The complete event
  // preselection scope must already hold its correction/notification lifetime;
  // a momentary metadata result or matching end value supplies no authority.
  QString why;
  if(!proveAIXufaCorrectionUnion(r,L,&why))
   return fail("Xufa hand-pile notification scope unavailable: "+why);
  // Full hand->pile branch: never inspect PresentCard or any individual flag.
  // Both give outcomes have m_playerId=="", and native roster identities are
  // nonempty, so BeMan returns nil before poisonCards/getUseValue/intention.
  if(!discardBody()||!native(nullptr,"ipairs")||!native("string","find"))return false;
  {
   XufaFilterStack st(L);globals(L);field(L,-1,"string");
   if(!xufaFilterPlain(L,-1))return fail("Xufa string library redirected");
   const int strings=lua_absindex(L,-1);field(L,strings,"contains");
   // Both exact installed implementations are read-only native string.find
   // graphs. Preserve their distinct plain/pattern result semantics.
   if(!(matchAIPinnedFunction(r,L,-1,"lua/utilities.lua",91,93)
      ||matchAIPinnedFunction(r,L,-1,"extensions/addFunction.lua",1557,1559))
      ||!env(-1))return fail("Xufa string.contains source differs");lua_pop(L,1);
   // Lua 5.4's trusted compatibility metatable also contains native
   // arithmetic entries. Match the whole sealed bootstrap metatable rather
   // than inventing a one-field shape incompatible with the actual runtime.
   if(!matchAINativeStringMetatable(r,L))return fail("Xufa string bootstrap metamethod bindings changed");
  }
  for(const char*k:{"getCurrent","setCardFlag"})if(!method("Room",k))return false;
  // getFlags is a copied QStringList. Fixed source clears only visible_*;
  // native CardStateService -> Card/WrappedCard::setFlags -> notifier also
  // reaches controller cleanup. Its complete reentry union is required.
  if(!matchAINativeDerivedMethods(r,L,"Card",{"getFlags","toString"})
    ||!matchAINativeDerivedMethods(r,L,"ServerPlayer",{"getTag"})
    ||!method("QVariant","toString"))return fail("Xufa hand/pile scalar or transport binding changed");
  return true;
 }
 bool discardBody(){
  // Never inspect PresentCard, role, hidden hand, card flags, AIChat outcome or
  // callback branch results. This envelope has null to / all Special origins.
  if(!globalCode("BeMan","extensions/addFunction.lua",246,258)
   ||!globalCode("AIChat","lua/ai/chat-ai.lua",3,6)||!qlist()||!native(nullptr,"type")||!native("bit32","band"))return false;
  {XufaFilterStack st(L);globals(L);field(L,-1,"global_delay");if(lua_type(L,-1)!=LUA_TNUMBER||!std::isfinite(lua_tonumber(L,-1)))return fail("Xufa configured chat delay is not scalar");}
  for(const char*k:{"Player_PlaceHand","Player_PlaceEquip","Player_PlaceDelayedTrick","Player_DiscardPile","Player_Discard","CardMoveReason_S_MASK_BASIC_REASON","CardMoveReason_S_REASON_GIVE","CardMoveReason_S_REASON_DISCARD","CardMoveReason_S_REASON_RULEDISCARD"})if(!constant(k))return false;
  for(const char*k:{"from","to","to_place","card_ids","from_places","reason"})if(!property("CardsMoveOneTimeStruct",k))return false;
  for(const char*k:{"m_reason","m_playerId","m_skillName"})if(!property("CardMoveReason",k))return false;
  if(!method("QVariant","toMoveOneTime")||!method("PlaceList","at")||!method("PlaceList","contains")
    ||!method("Room","getPlayers")||!method("Engine","getCard"))return false;
  if(!matchAINativeNamespaceProperty(r,L,"sgs","Sanguosha"))return fail("Xufa native Engine namespace getter differs");
  if(!matchAINativeDerivedMethods(r,L,"ServerPlayer",{"objectName","getPhase"}))return fail("Xufa player scalar methods differ");
  // In this discard envelope the discarded actor is empty; BeMan returns nil
  // for either event-target relation. No Card method is then reached. The
  // qiaobian elseif is bypassed (THROW has DISCARD basic reason).
  return true;
 }
};
}
struct AIXufaMoveBodyScope::Data {
 Room*room=nullptr;QPointer<ServerPlayer>owner;QPointer<const Card>card;
 QList<int>ids;QList<Player::Place>places;QHash<int,QPointer<const WrappedCard>>physical;QHash<int,QPointer<const Card>>faces;bool discard=false,valid=false,entryConsumed=false;
};
struct AIXufaMoveEntry::Data {
 Room*room=nullptr;const AIXufaMoveBodyScope*scope=nullptr;bool valid=false;
};
struct AIXufaBodyAccess {
 static AIXufaMoveBodyScope::Data*top(Room&r){auto i=frames.constFind(&r);return i==frames.cend()||i->isEmpty()?nullptr:i->last()->d.get();}
 static bool cardOrigins(Room&r){
  const auto*d=top(r);if(!d||!d->valid)return false;
  for(int id:d->ids){
   const auto wrapper=d->physical.value(id);const auto face=d->faces.value(id);
   if(!wrapper||!face||r.getCard(id)!=wrapper.data()
    ||!r.roomState()->aiPhysicalIdSourceContains(wrapper.data())
    ||wrapper->aiPhysicalCloneOwnedFaceOrigin()!=face.data())return false;
  }return true;
 }
 static bool currentEntry(Room&r){auto e=entries.constFind(&r);auto f=frames.constFind(&r);return e!=entries.cend()&&!e->isEmpty()&&f!=frames.cend()&&!f->isEmpty()&&e->last()->d->valid&&e->last()->d->scope==f->last();}
};
AIXufaMoveBodyScope::AIXufaMoveBodyScope(Room&r,ServerPlayer*p,const Card*c,bool discard):d(new Data){
 d->room=&r;d->owner=p;d->card=c;d->discard=discard;frames[&r].append(this);
 if(!p||p->getRoom()!=&r||!c||typeid(*c)!=typeid(XufaCard)||p->getPhase()!=Player::Play)return;
 d->ids=c->getSubcards();QSet<int>seen;if(d->ids.isEmpty())return;
 for(int id:d->ids){if(id<0||seen.contains(id)||r.getCardOwner(id)!=p)return;seen.insert(id);
  const Card*physical=r.getCard(id);
  if(!physical||!r.roomState()->aiPhysicalIdSourceContains(physical)
   ||typeid(*physical)!=typeid(WrappedCard))return;
  const auto*wrapper=static_cast<const WrappedCard*>(physical);
  const Card*face=wrapper->aiPhysicalCloneOwnedFaceOrigin();
  if(!face||!aiOrdinaryCardPolicyType(face))return;
  d->physical.insert(id,wrapper);d->faces.insert(id,face);
  if(discard){if(r.getCardPlace(id)!=Player::PlaceSpecial||!p->getPile("xufa").contains(id))return;}
  else if(r.getCardPlace(id)!=Player::PlaceHand&&r.getCardPlace(id)!=Player::PlaceEquip)return;
  d->places.append(r.getCardPlace(id));
 }d->valid=true;
}
AIXufaMoveBodyScope::~AIXufaMoveBodyScope(){auto i=frames.find(d->room);if(i==frames.end())return;i->removeAll(this);if(i->isEmpty())frames.erase(i);}
AIXufaMoveEntry::AIXufaMoveEntry(Room&r,const QList<CardsMoveStruct>&moves,bool visible,bool guanxing):d(new Data){
 d->room=&r;entries[&r].append(this);auto*scope=AIXufaBodyAccess::top(r);
 if(!scope||scope->entryConsumed)return;scope->entryConsumed=true;
 if(!scope->valid||!scope->owner||!scope->card||!visible||guanxing||moves.size()!=1)return;
 const auto&move=moves.first();if(move.card_ids!=scope->ids||move.from||!move.reason.m_playerId.isEmpty()||!move.reason.m_eventName.isEmpty())return;
 if(scope->discard){
  if(move.to||move.to_place!=Player::DiscardPile||move.reason.m_reason!=CardMoveReason::S_REASON_THROW||move.reason.m_skillName!="xufa"
   ||move.reason.m_extraData.metaType().id()!=qMetaTypeId<const Card*>()||move.reason.m_extraData.value<const Card*>()!=scope->card)return;
 }else if(move.to!=scope->owner||move.to_place!=Player::PlaceSpecial||move.to_pile_name!="xufa"||move.reason.m_reason!=CardMoveReason::S_REASON_UNKNOWN||!move.reason.m_skillName.isEmpty())return;
 d->scope=frames.value(&r).last();d->valid=true;
}
AIXufaMoveEntry::~AIXufaMoveEntry(){auto i=entries.find(d->room);if(i==entries.end())return;i->removeAll(this);if(i->isEmpty())entries.erase(i);}
bool registerAIXufaBodySources(Room&r,lua_State*L,QString*why){for(const auto&s:sources)if(!registerAIPinnedSource(r,L,s.path,QByteArray(s.sha),why))return false;return true;}
bool certifyAIXufaFilterBody(Room&r,lua_State*L,int selfIndex,const AINaturalFilterProof&proof,QString*reason){
 if(reason)reason->clear();const auto*scope=AIXufaBodyAccess::top(r);
 const auto fail=[&](const QString&s){if(reason)*reason=s;return false;};
 if(!AIXufaBodyAccess::currentEntry(r)||!AIXufaBodyAccess::cardOrigins(r)||!scope||!scope->valid||!scope->owner||!scope->card||scope->owner->getRoom()!=&r||scope->card->getSubcards()!=scope->ids)return fail("Xufa exact compiled move custody absent/stale");
 // Only the exact compiled, one-use move entry admits these two envelopes.
 // No private tag/flag result can determine whether the proof succeeds.
 if(!AILuaInspect::capacity(L,512))return fail("Xufa metadata stack unavailable");XufaFilterStack stack(L);selfIndex=lua_absindex(L,selfIndex);
 if(!pushAINaturalFilterSelf(r,L,4)||!lua_rawequal(L,selfIndex,-1))return fail("Xufa observer differs from actual native filter dispatch");lua_pop(L,1);
 if(scope->owner->getPhase()!=Player::Play)return fail("Xufa source public phase changed");
 const auto roster=r.getPlayers();for(const auto*p:roster)if(!p||p->objectName().isEmpty())return fail("Xufa empty roster identity invalidates BeMan exclusion");
 bool matched=inspectAINaturalFilterEntry(r,L,4,[&](int event,ServerPlayer*target,const QVariant&payload){
  return event==proof.event&&matchesAIXufaMoveEvent(r,event,target,payload);
 });
 if(!matched)return fail("Xufa current authenticated public move envelope differs");
 Graph g{r,L};globals(L);field(L,-1,"sgs");g.sgs=lua_absindex(L,-1);
 if(!table(L,g.sgs)||!g.callbacks(proof.event)||(proof.event==int(CardsMoveOneTime)&&!(scope->discard?g.discardBody():g.handPileBody())))return fail(g.why.isEmpty()?QStringLiteral("Xufa graph metadata incomplete"):g.why);
 return true;
}

// This matches public source metadata only. Native notification transport
// additionally traverses controller cleanup before bot/no-wire suppression;
// it is NOT independently a nonwriter proof. R47 event preflight remains
// false until that transport union and the trigger roots are certified.
bool matchesAIXufaMoveEvent(Room&r,int event,ServerPlayer*target,const QVariant&payload){
 const auto*scope=AIXufaBodyAccess::top(r);
 if(!AIXufaBodyAccess::currentEntry(r)||!AIXufaBodyAccess::cardOrigins(r)||!scope||!scope->valid||!scope->owner||!scope->card
  ||scope->owner->getRoom()!=&r||scope->card->getSubcards()!=scope->ids||scope->owner->getPhase()!=Player::Play)return false;
 const auto roster=r.getPlayers();for(const auto*p:roster)if(!p||p->objectName().isEmpty())return false;

  if(!target||!roster.contains(target))return false;
  const bool single=event==int(CardsMoveOneTime)||event==int(BeforeCardsMove);
  const bool batch=event==int(BeforeCardsMoveBatch)||event==int(PreCardsMoveBatch)||event==int(CardsMoveBatch);
  if(!single&&!batch)return false;
  QVariantList list;
  if(single){if(payload.metaType().id()!=qMetaTypeId<CardsMoveOneTimeStruct>())return false;list<<payload;}
  else {if(payload.metaType().id()!=QMetaType::QVariantList)return false;list=payload.toList();}
  if(list.isEmpty())return false;QSet<int>allIds;
  for(const auto&value:list){
   if(value.metaType().id()!=qMetaTypeId<CardsMoveOneTimeStruct>())return false;
   const auto move=value.value<CardsMoveOneTimeStruct>();
   if(move.from!=scope->owner||!move.reason.m_playerId.isEmpty()||!move.reason.m_eventName.isEmpty()
      ||move.card_ids.isEmpty()||move.card_ids.size()!=move.from_places.size())return false;
   if(scope->discard){
    if(move.to||move.to_place!=Player::DiscardPile||move.reason.m_reason!=CardMoveReason::S_REASON_THROW||move.reason.m_skillName!="xufa")return false;
   }else if(move.to!=scope->owner||move.to_place!=Player::PlaceSpecial||move.to_pile_name!="xufa"
      ||move.reason.m_reason!=CardMoveReason::S_REASON_UNKNOWN||!move.reason.m_skillName.isEmpty())return false;
   int prior=-1;for(int i=0;i<move.card_ids.size();++i){int id=move.card_ids[i],at=scope->ids.indexOf(id);
    if(at<=prior||allIds.contains(id)||at>=scope->places.size()||move.from_places[i]!=scope->places[at])return false;prior=at;allIds.insert(id);
   }
  }return true;
}

// ---- xufa-proof ----
#include "lua-wrapper.h"
#include <QStringList>
extern "C" {
#include "lopcodes.h"
}
namespace {
using namespace AILuaInspect;
struct XufaProofRow {const char*path;const char*sha;int first,last;const char*methods;const char*sgsFields;};
const XufaProofRow rows[]={
#include "ai-strategy/xufa-getter-rows.inc"
};
// Separate source domain: existing correction callers never consult these.
const XufaProofRow activateRows[]={
#include "ai-strategy/xufa-activate-getter-rows.inc"
};
struct ActivateSelectionRow { XufaProofRow body; const char *role; };
const ActivateSelectionRow activateSelectionRows[]={
#include "ai-strategy/xufa-activate-selection-rows.inc"
};
struct XufaProofStack{lua_State*L;int n;explicit XufaProofStack(lua_State*s):L(s),n(lua_gettop(s)){}~XufaProofStack(){lua_settop(L,n);}};
bool xufaProofPlain(lua_State*L,int i){const auto*t=table(L,i);return t&&!t->metatable;}
bool scalar(lua_State*L,int i){int t=lua_type(L,i);return t==LUA_TNIL||t==LUA_TBOOLEAN||t==LUA_TNUMBER||t==LUA_TSTRING;}
// Resolve ordinary module(...,package.seeall) table chains WITHOUT calling an
// authored __index. A function/metatable cycle is unsupported, not executed.
bool xufaProofLookup(lua_State*L,int index,const char*key){
 index=lua_absindex(L,index);QSet<const void*> seen;
 for(int n=0;n<16;++n){
  auto*t=table(L,index);if(!t||seen.contains(t))return false;seen.insert(t);
  field(L,index,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);
  if(!t->metatable){push(L,nullptr);return true;}
  if(!lua_getmetatable(L,index))return false;
  if(!xufaProofPlain(L,-1))return false;field(L,-1,"__index");
  if(!lua_istable(L,-1))return false;index=lua_absindex(L,-1);
 }return false;
}
struct Check {
 Room&r;lua_State*L;QString why;int budget=100000;
 QSet<const void*> functions,tables,protectedRoots;QSet<QString> sealedMethods;bool rootsReady=false,requireCaptureOrigin=false,includeActivateRows=false,includeActivateSelectionRows=false;
 bool no(const QString&s){if(why.isEmpty())why=s;return false;}
 bool native(int i,const QString&p){return matchAINativeBinding(r,L,i,p)||no("native binding: "+p);}
 bool nativeField(int lib,const char*k,const QString&path){XufaProofStack s(L);field(L,lib,k);return native(-1,path);}
 bool primitiveMeta(){
  XufaProofStack s(L);for(int t:{LUA_TNIL,LUA_TBOOLEAN,LUA_TNUMBER,LUA_TFUNCTION})if(primitiveMetatable(L,t))return no("primitive metatable changed");
  // Preserve the actual trusted Lua 5.4 compatibility metatable. It has
  // native arithmetic entries as well as __index; no names-only exception.
  // Nonstring operand delegation is closed by the finite getter argument,
  // captured-plain-table and exact SWIG value-metamethod proofs.
  return matchAINativeStringMetatable(r,L)||no("string bootstrap metamethod bindings changed");
 }
 bool protectRoots(){
  if(rootsReady)return true;XufaProofStack st(L);globals(L);protectedRoots.insert(lua_topointer(L,-1));
  field(L,-1,"sgs");if(!table(L,-1))return no("getter protected namespace absent");
  protectedRoots.insert(lua_topointer(L,-1));int ns=lua_absindex(L,-1);
  // These are actual registry identities, not table-shape guesses. value()
  // checks every nested capture before its cycle shortcut. Full pinned source
  // separately proves fresh local mutation sinks and removeOne caller origin.
  for(const char*k:{"ai_skill_choice","ai_use_priority","ai_use_value","ai_role","role_evaluation"}){
   field(L,ns,k);if(lua_istable(L,-1))protectedRoots.insert(lua_topointer(L,-1));lua_pop(L,1);
  }
  rootsReady=true;return true;
 }
 bool value(int index,int depth=0){
  if(--budget<0||depth>48)return no("getter capture graph bound");index=lua_absindex(L,index);
  if(scalar(L,index))return true;if(lua_isfunction(L,index))return function(index,depth+1);
  if(requireCaptureOrigin&&!isAIProjectionCaptureOrigin(r,L,lua_topointer(L,index)))
   return no("getter capture lacks exact source allocation receipt");
  if(!xufaProofPlain(L,index))return no("getter captured capability/metatable");
  const void*p=lua_topointer(L,index);if(hasAIAsyncUniverse(r,L)&&isAIAsyncProtectedTable(r,L,index))return no("getter capture aliases asynchronous capability");if(!protectRoots()||protectedRoots.contains(p))return no("getter capture aliases protected registry root");if(tables.contains(p))return true;tables.insert(p);
  lua_pushnil(L);while(next(L,index)){if(!scalar(L,-2)||!value(-1,depth+1))return false;lua_pop(L,1);}return true;
 }
 bool library(int env,const QString&name){
  XufaProofStack s(L);if(!xufaProofLookup(L,env,name.toUtf8().constData())||!xufaProofPlain(L,-1))return no("getter library redirected: "+name);int lib=lua_absindex(L,-1);
  static const QHash<QString,QStringList> leaves={
   {"math",{"abs","min","max","floor","ceil","fmod"}},
   {"string",{"find","sub","gmatch","gsub","format","len"}},
   {"table",{"insert","remove","concat"}}, {"debug",{"traceback"}}};
  for(const auto&k:leaves.value(name))if(!nativeField(lib,k.toUtf8().constData(),name+'.'+k))return false;
  if(name=="math"){
   field(L,lib,"mod");
   if(!lua_isnil(L,-1)){
    if(lua_iscfunction(L,-1)){if(!native(-1,"math.mod"))return false;}
    else {
     // Original utilities replaces the compatibility slot with this exact
     // Lua wrapper. Authenticate its body/environment; never adopt a current
     // function as a new native seal or accept another row in this role.
     if(!matchAIPinnedFunction(r,L,-1,"lua/utilities.lua",176,178))
      return no("math.mod outside fixed utilities wrapper");
     if(!function(-1,1))return false;
    }
   }
   lua_pop(L,1);
  }
  for(const auto&k:name=="table"?QStringList{"contains","removeOne"}:name=="string"?QStringList{"contains","split","startsWith","endsWith"}:QStringList{}){
   field(L,lib,k.toUtf8().constData());bool ok=function(-1,1);lua_pop(L,1);if(!ok)return false;
  }
  return true;
 }
 bool global(int env,const QString&key,const XufaProofRow&row){
  XufaProofStack s(L);if(!xufaProofLookup(L,env,key.toUtf8().constData()))return no("indirect getter environment: "+key);int v=lua_absindex(L,-1);
  static const QSet<QString> prim={"type","tonumber","tostring","ipairs","pairs","next","error","assert","select"};
  if(prim.contains(key))return native(v,key);
  if(key=="table"||key=="string"||key=="math"||key=="debug")return library(env,key);
  if(key=="sgs"){
   if(!lua_istable(L,v))return no("sgs namespace missing");{XufaProofStack sg(L);globals(L);field(L,-1,"sgs");if(!lua_rawequal(L,v,-1))return no("getter sgs namespace redirected");}
   for(const auto&f:QString::fromLatin1(row.sgsFields).split('|',Qt::SkipEmptyParts)){
    if(f=="Sanguosha"){if(!matchAINativeNamespaceProperty(r,L,"sgs",f))return no("engine namespace getter changed");continue;}
    field(L,v,f.toUtf8().constData());bool ok=false;
    if(includeActivateRows&&(f=="Slash_IsAvailable"||f=="Analeptic_IsAvailable")){
     // Every admitted row calls these with exactly one Player argument.
     // Their fixed native defaults construct fresh Slash/Analeptic products;
     // caller still owes complete projection/correction/allocation scope.
     ok=native(-1,"sgs."+f);
    }
    else if(f=="qlist"||f=="list")ok=function(-1,1);
    else if(f=="QVariant"||f=="IntList"||f=="PlayerList"||f=="SPlayerList")ok=matchAINativeScratchConstructor(r,L,-1,f);
    else if(f=="CorrectSkillResult"){
     // Fixed callers use only this raw static C factory; do not call an
     // arbitrary class table or resolve its metamethods during inspection.
     if(lua_istable(L,-1)){field(L,-1,"unlimitedResidue");ok=native(-1,"sgs.CorrectSkillResult_unlimitedResidue");lua_pop(L,1);}
    }
    else ok=matchAINativeConstant(r,L,-1,"sgs."+f)||(lua_isnil(L,-1)&&matchAINativeNamespaceRead(r,L,"sgs",f));
    lua_pop(L,1);if(!ok)return no("getter namespace binding: "+f);
   }return true;
  }
  // The finite manifests name the actual fixed helper roots. Nil/scalar is
  // also nonwriter: attempting to call it raises the source ordinary error.
  if(scalar(L,v))return true;
  return function(v,1);
 }
 bool environment(const Proto*p,int up,int env,const XufaProofRow&row){
  for(int i=0;i<p->sizecode;++i){auto in=p->code[i];auto op=GET_OPCODE(in);
   if((op==OP_GETUPVAL&&GETARG_B(in)==up)||(op==OP_SETUPVAL&&GETARG_B(in)==up)||(op==OP_SETTABUP&&GETARG_A(in)==up))return no("getter environment escape/write");
   if(op==OP_GETTABUP&&GETARG_B(in)==up){int k=GETARG_C(in);if(k<0||k>=p->sizek||!ttisstring(&p->k[k]))return no("nonliteral getter global");
    auto*t=tsvalue(&p->k[k]);if(!global(env,QString::fromUtf8(getstr(t),qsizetype(tsslen(t))),row))return false;
   }
  }
  for(int c=0;c<p->sizep;++c)for(int u=0;u<p->p[c]->sizeupvalues;++u)
   if(p->p[c]->upvalues[u].name&&!std::strcmp(getstr(p->p[c]->upvalues[u].name),"_ENV")&&!environment(p->p[c],u,env,row))return false;
  return true;
 }
 bool noCaptureWrite(const Proto*p){
  for(int pc=0;pc<p->sizecode;++pc)if(GET_OPCODE(p->code[pc])==OP_SETUPVAL)return no("getter writes captured state");
  for(int c=0;c<p->sizep;++c)if(!noCaptureWrite(p->p[c]))return false;return true;
 }
 bool methods(const XufaProofRow&row); // finite audited native leaves, defined below
 bool function(int i,int depth=0){
  if(--budget<0||depth>48||!lua_isfunction(L,i)||lua_iscfunction(L,i))return no("getter function outside pinned Lua graph");i=lua_absindex(L,i);
  const auto*actual=static_cast<const LClosure*>(lua_topointer(L,i));const XufaProofRow*row=nullptr;
  for(const auto&v:rows)if(actual->p->linedefined==v.first&&actual->p->lastlinedefined==v.last
    &&matchAIPinnedFunction(r,L,i,v.path,v.first,v.last)){row=&v;break;}
  if(!row&&includeActivateRows)for(const auto&v:activateRows)
   if(actual->p->linedefined==v.first&&actual->p->lastlinedefined==v.last
    &&matchAIPinnedFunction(r,L,i,v.path,v.first,v.last)){row=&v;break;}
  if(!row&&includeActivateSelectionRows)for(const auto&v:activateSelectionRows)
   if(actual->p->linedefined==v.body.first&&actual->p->lastlinedefined==v.body.last
    &&matchAIPinnedFunction(r,L,i,v.body.path,v.body.first,v.body.last)){row=&v.body;break;}
  if(!row){
   // Route diagnostic labels only through the finite public source rows. Never
   // print an arbitrary Proto source (which can contain a load-string body).
   QString descriptor=QStringLiteral("unknown-fixed-descriptor"),candidateReason;
   if(actual->p->source){
    QByteArray hint(getstr(actual->p->source),qsizetype(tsslen(actual->p->source)));
    if(hint.startsWith('@'))hint.remove(0,1);
    while(hint.startsWith("./"))hint.remove(0,2);
    for(const auto&v:rows)if(hint==v.path){descriptor=QString::fromLatin1(v.path);break;}
   }
   for(const auto&v:rows)if(descriptor==QString::fromLatin1(v.path)
     &&actual->p->linedefined==v.first&&actual->p->lastlinedefined==v.last){
    matchAIPinnedFunction(r,L,i,v.path,v.first,v.last,&candidateReason);break;
   }
   return no(QStringLiteral("getter full Proto outside fixed source union: descriptor=%1 first=%2 last=%3 seal=%4")
    .arg(descriptor).arg(actual->p->linedefined).arg(actual->p->lastlinedefined)
    .arg(candidateReason.isEmpty()?QStringLiteral("no-exact-row"):candidateReason));
  }
  const void*id=lua_topointer(L,i);if(functions.contains(id))return true;functions.insert(id);
  const Proto*p=static_cast<const LClosure*>(id)->p;
  if(!methods(*row))return false;
  // Captured state replacement is not part of these getter bodies. Scratch
  // assignments in child functions remain in their actual pinned body.
  if(!noCaptureWrite(p))return false;
  for(const char*k:{"ownerObjectName","key"})if(!matchAINativeClassProperty(r,L,"SkillInstanceRef",k))return no("SkillInstanceRef property changed");
  for(const char*k:{"skillName","instanceID"})if(!matchAINativeClassProperty(r,L,"SkillInstanceKey",k))return no("SkillInstanceKey property changed");
  for(int u=1;;++u){const char*k=lua_getupvalue(L,i,u);if(!k)break;
   bool ok=!std::strcmp(k,"_ENV")?environment(p,u-1,lua_absindex(L,-1),*row):value(-1,depth+1);
   lua_pop(L,1);if(!ok)return false;
  }return true;
 }
};
bool Check::methods(const XufaProofRow&row){
 const QStringList ms=QString::fromLatin1(row.methods).split('|',Qt::SkipEmptyParts);
 static const QSet<QString> context={"getPrimary","getSecondary","getHolder","getModType","includesWeapon","getCurrentAmount","getStateValue","getInstanceRef"};
 static const QSet<QString> card={"getActivationSkillName","getClassName","getEffectiveId","getId","getType","targetFixed","isAvailable","getNumber","getRealCard","getSkillName","getSkillNames","getSubcards","getSuit","getSuitString","getSubtype","getTypeId","hasSuit","hasTip","isBlack","isDamageCard","isKindOf","isNDTrick","isRed","isVirtualCard","sameColorWith","subcardsLength","toWeapon"};
 static const QSet<QString> player={"aliveCount","containsTrick","handCards","getEquipsId","isSkillInstanceEffectAvailable","canSlash","isProhibited","distanceTo","faceUp","getAliveSiblings","getArmor","getAttackRange","getChangeSkillState","getDefensiveHorse","getEquip","getEquips","getGeneral2Name","getGeneralName","getHandcardNum","getHandcards","getHp","getJudgingArea","getKingdom","getLostHp","getMark","getMarkNames","getMaxHp","getOffensiveHorse","getPhase","getPile","getRole","getSeat","getSiblings","getSkillInstanceCorrectStateValue","getSkillInstanceIds","getSkillInstanceParentRef","getSkillInstanceStateValue","getTreasure","getValidSkillInstanceIds","getVisibleSkillList","getWeapon","hasArmorEffect","hasDefensiveHorse","hasEquip","hasEquipArea","hasEquipSkill","hasLordSkill","hasOffensiveHorse","hasSkill","hasTreasure","hasUsed","hasWeapon","inMyAttackRange","isAlive","isChained","isDead","isFemale","isKongcheng","isLastHandCard","isLord","isMale","isWounded","isYourFriend"};
 static const QSet<QString> skill={"getFrequency","isAttachedLordSkill","isLordSkill","isChangeSkill"};
 for(const auto&m:ms){
  QStringList classes;bool derived=false;
  if(includeActivateSelectionRows&&m=="getSelectedCardIds"){
   // ActiveSkillRequest::getSelectedCardIds returns its value-list copy.
   // SWIG scratch allocation/lifetime and receiver custody remain separate.
   classes<<"ActiveSkillRequest";
  }else if(includeActivateSelectionRows&&m=="isEquipped"){
   // The canonical QSAN_ENGINE_BUILD Card body is false, but exact virtual
   // receiver/product provenance remains an independent native obligation.
   classes<<"Card";derived=true;
  }else if(includeActivateRows&&(m=="getInitiator"||m=="getReason"||m=="getPattern"||m=="getActivationInstanceId"))classes<<"ActiveSkillRequest";
  else if(includeActivateRows&&(m=="isNude"||m=="isAllNude")){classes<<"Player";derived=true;}
  else if(includeActivateRows&&(m=="usedTimes"||m=="hasTurn")){classes<<"Player";derived=true;}
  else if(includeActivateRows&&m=="getCardCount"){
   // Both same-named public native leaf alternatives are readonly. Seal both
   // rather than infer the class from the spelling of a private receiver.
   if(!matchAINativeClassMethod(r,L,"Engine",m))return no("Activate Engine card count binding changed");
   classes<<"Player";derived=true;
  }
  else if(context.contains(m))classes<<"CorrectSkillContext";
  else if(card.contains(m)){classes<<"Card";derived=true;}
  else if(player.contains(m)){classes<<"Player";derived=true;}
  else if(skill.contains(m)){classes<<"Skill";derived=true;}
  else if(m=="getCard")classes<<"Engine"<<"CorrectSkillContext";
  else if(m=="getRoleList"||m=="getEngineCard"||m=="correctCardTarget"||m=="matchExpPattern"||m=="getBanPackages"||m=="isPackageBanned")classes<<"Engine";
  else if(m=="getOtherPlayers")classes<<"Room";
  else if(m=="getGameMode"){classes<<"Player";derived=true;}
  else if(m=="getRange")classes<<"Weapon";
  else if(m=="isValid")classes<<"SkillInstanceRef";
  else if(m=="toInt"||m=="toBool"||m=="toStringList")classes<<"QVariant";
  else if(m=="toString"){classes<<"QVariant"<<"Card";derived=true;}
  else if(m=="objectName"){classes<<"Player"<<"Card"<<"Skill";derived=true;}
  else if(m=="property"){classes<<"Player"<<"Card";derived=true;}
  else if(m=="hasFlag"){classes<<"Player"<<"Card";derived=true;}
  else if(m=="setObjectName"){classes<<"Skill";derived=true;}
  else if(m=="length"||m=="at"||m=="first"||m=="contains"||m=="isEmpty"||m=="append"||m=="prepend"||m=="removeOne"){
   // QList values are copies; append/prepend affect the callback's scratch.
   if(m=="append"){
    // All six manifest append receivers are actual fresh IntList or borrowed
    // Player/SPlayer list copies (Loong234, excard2014:385, hezongkangqin89,
    // htms5349, kearjsrg7867, luaoldenemy_lib27). No SkillList append occurs;
    // its real sgs_ex wrapper is a different provider graph, not this leaf.
    classes<<"IntList"<<"PlayerList"<<"SPlayerList";
   }else classes<<"IntList"<<"PlayerList"<<"SPlayerList"<<"CardList"<<"SkillList"<<"EquipCardList"<<"SkillInstanceKeyList";
  }else if(m=="split"||m=="startsWith"||m=="endsWith")continue; // primitive string library sealed separately
  else return no("unproved native leaf: "+m);
  for(const auto&c:classes){const QString seal=c+"."+m+(derived?".derived":"");if(sealedMethods.contains(seal))continue;bool ok=derived?matchAINativeDerivedMethods(r,L,c,{m}):matchAINativeClassMethod(r,L,c,m);
   if(!ok)return no("native getter method changed: "+c+'.'+m);sealedMethods.insert(seal);
  }
 }
 // Method-syntax string lookups bypass _ENV. Always seal the primitive
 // string table/helper graph, including contains (also used on QList).
 XufaProofStack st(L);globals(L);return library(lua_absindex(L,-1),"string");
}
} // namespace
bool registerAIXufaGetterSources(Room&r,lua_State*L,QString*why){
 QSet<QString> done;
 const auto registerRows=[&](const auto&domain){for(const auto&row:domain){
  QString p=QString::fromLatin1(row.path);if(done.contains(p))continue;
  if(!registerAIPinnedSource(r,L,p,row.sha,why))return false;done.insert(p);
 }return true;};
 if(!registerRows(rows)||!registerRows(activateRows))return false;
 for(const auto&entry:activateSelectionRows){const auto&row=entry.body;
  QString p=QString::fromLatin1(row.path);if(done.contains(p))continue;
  if(!registerAIPinnedSource(r,L,p,row.sha,why))return false;done.insert(p);
 }return true;
}
bool proveAIXufaGetterFunction(Room&r,lua_State*L,int i,QString*why){
 if(!L||!capacity(L,4096)){if(why)*why="Xufa getter inspection stack unavailable";return false;}
 XufaProofStack s(L);Check c{r,L};bool ok=c.primitiveMeta()&&c.function(i);if(why)*why=c.why;return ok;
}
bool proveAIActivateGetterFunction(Room&r,lua_State*L,int i,QString*why){
 if(!L||!r.roomRuntime()||r.roomRuntime()->lua().rawState()!=L||!capacity(L,4096)){
  if(why)*why="Activate getter inspection frame unavailable";return false;
 }
 XufaProofStack stack(L);Check c{r,L};c.includeActivateRows=true;c.requireCaptureOrigin=true;
 QString metaWhy;
 const bool ok=c.primitiveMeta()&&matchAINativeValueMetamethods(r,L,&metaWhy)&&c.function(i);
 if(why)*why=c.why.isEmpty()?metaWhy:c.why;return ok;
}
bool proveAIActivateGetterFamily(Room&r,lua_State*L,const QList<LuaCallbackRef>&family,QString*why){
 if(!L||!r.roomRuntime()||r.roomRuntime()->lua().rawState()!=L||!capacity(L,4096)){
  if(why)*why="Activate getter family inspection frame unavailable";return false;
 }
 // Same complete Check as the single-root API and original bootstrap union.
 // Its graph budget is unchanged (100000 for this whole family), with no
 // outcome memoization and no visitor surviving this one preflight call.
 XufaProofStack stack(L);Check c{r,L};c.includeActivateRows=true;c.requireCaptureOrigin=true;
 QString metaWhy;
 bool ok=c.primitiveMeta()&&matchAINativeValueMetamethods(r,L,&metaWhy);
 const auto generation=r.roomRuntime()->lua().generation();
 if(ok)for(const LuaCallbackRef &fn:family){
  if(fn==0)continue;
  if(fn.state()!=L||fn.generation()!=generation){ok=c.no("getter family callback VM changed");break;}
  XufaProofStack entry(L);AILuaInspect::rawGeti(L,LUA_REGISTRYINDEX,fn.registryRef());
  if(!c.function(-1)){ok=false;break;}
 }
 if(why)*why=c.why.isEmpty()?metaWhy:c.why;return ok;
}
bool proveAIActivateSelectionFunction(Room&r,lua_State*L,int i,const char*role,QString*why){
 if(!L||!role||!r.roomRuntime()||r.roomRuntime()->lua().rawState()!=L||!capacity(L,4096)){
  if(why)*why="Activate selection source inspection frame unavailable";return false;
 }
 XufaProofStack stack(L);bool root=false;
 // A selected field cannot borrow a descriptor from an unrelated callback
 // role. This remains source inspection only, without dispatch admission.
 for(const auto&entry:activateSelectionRows)if(!std::strcmp(role,entry.role)
  &&matchAIPinnedFunction(r,L,i,entry.body.path,entry.body.first,entry.body.last)){root=true;break;}
 if(!root){if(why)*why="selection callback role/full Proto outside finite nonwriting source union";return false;}
 Check c{r,L};c.includeActivateRows=true;c.includeActivateSelectionRows=true;c.requireCaptureOrigin=true;
 QString metaWhy;
 const bool ok=c.primitiveMeta()&&matchAINativeValueMetamethods(r,L,&metaWhy)&&c.function(i);
 if(why)*why=c.why.isEmpty()?metaWhy:c.why;return ok;
}
static bool inspectAICorrectionBindings(Room&r,lua_State*L,QSet<const void*>*captures,QString*why){
 if(!L||r.roomRuntime()->lua().state()!=L||!capacity(L,4096)){if(why)*why="Xufa union inspection frame unavailable";return false;}
 XufaProofStack st(L);Check c{r,L};c.requireCaptureOrigin=captures!=nullptr;QString metaWhy;
 if(!c.primitiveMeta()||!matchAINativeValueMetamethods(r,L,&metaWhy)){
  if(why)*why=c.why.isEmpty()?metaWhy:c.why;return false;
 }
 auto f=[&](const LuaFunction&fn){if(fn==0)return true;fn.push(L);bool ok=c.function(-1);lua_pop(L,1);return ok;};
 bool ok=true;
 for(const auto*s:Sanguosha->getDistanceSkills()){
  if(auto*x=dynamic_cast<const LuaDistanceSkillV2*>(s))ok=f(x->correct_func)&&f(x->fixed_func);
  else if(auto*x=dynamic_cast<const LuaDistanceSkill*>(s))ok=f(x->correct_func)&&f(x->fixed_func);
  if(!ok)break;
 }
 if(ok)for(const auto*s:Sanguosha->getMaxCardsSkills()){
  if(auto*x=dynamic_cast<const LuaMaxCardsSkillV2*>(s))ok=f(x->correct_func)&&f(x->fixed_func);
  else if(auto*x=dynamic_cast<const LuaMaxCardsSkill*>(s))ok=f(x->extra_func)&&f(x->fixed_func);
  if(!ok)break;
 }
 if(ok)for(const auto*s:Sanguosha->getTargetModSkills()){
  if(auto*x=dynamic_cast<const LuaTargetModSkillV2*>(s))ok=f(x->correct_func);
  else if(auto*x=dynamic_cast<const LuaTargetModSkill*>(s))ok=f(x->residue_func)&&f(x->distance_limit_func)&&f(x->extra_target_func);
  if(!ok)break;
 }
 if(ok)for(const auto*s:Sanguosha->getAttackRangeSkills()){
  if(auto*x=dynamic_cast<const LuaAttackRangeSkillV2*>(s))ok=f(x->correct_func)&&f(x->fixed_func);
  else if(auto*x=dynamic_cast<const LuaAttackRangeSkill*>(s))ok=f(x->extra_func)&&f(x->fixed_func);
  if(!ok)break;
 }
 if(ok)for(const auto*s:Sanguosha->getInvaliditySkills())if(auto*x=dynamic_cast<const LuaInvaliditySkill*>(s)){if(!(ok=f(x->skill_valid)))break;}
 if(ok)for(const auto*s:Sanguosha->getViewAsEquipSkills())if(auto*x=dynamic_cast<const LuaViewAsEquipSkill*>(s)){if(!(ok=f(x->view_as_equip)))break;}
 if(ok)for(const auto*s:Sanguosha->getProhibitSkills())if(auto*x=dynamic_cast<const LuaProhibitSkill*>(s)){if(!(ok=f(x->is_prohibited)))break;}
 if(ok)for(const auto*s:Sanguosha->getCardLimitSkills())if(auto*x=dynamic_cast<const LuaCardLimitSkill*>(s)){
  if(!(ok=f(x->limit_list)&&f(x->limit_pattern)&&f(x->limit_reason)))break;
 }
 // Public engine definition domain, not the privately equipped horse. This
 // union is proved before any ownership/effect filter and never calls a horse.
 if(ok)for(int id=0;id<Sanguosha->getCardCount();++id){const Card*card=Sanguosha->getEngineCard(id);
  if(auto*x=dynamic_cast<const LuaHorse*>(card))ok=f(x->correct_func);
  else if(auto*x=dynamic_cast<const LuaOffensiveHorse*>(card))ok=f(x->correct_func);
  else if(auto*x=dynamic_cast<const LuaDefensiveHorse*>(card))ok=f(x->correct_func);
  if(!ok)break;
 }
 // No current player/holder, role/skill ownership, mark, flag or query result
 // was read. Native Card virtual dispatch is separately guarded by custody.
 if(ok&&captures)*captures=c.tables;
 if(why)*why=c.why;return ok;
}
bool inspectAIXufaCorrectionBindings(Room&r,lua_State*L,QString*why){return inspectAICorrectionBindings(r,L,nullptr,why);}
bool collectAICorrectionCaptureTables(Room&r,lua_State*L,QSet<const void*>&out,QString*why){return inspectAICorrectionBindings(r,L,&out,why);}
bool certifyAIXufaPatternsEntry(Room&r,lua_State*L,int nargs,QString*why){
 const auto fail=[&](const QString&s){if(why)*why=s;return false;};
 if(!currentAIXufaContinuityWriter(r)||!L||r.roomRuntime()->lua().state()!=L
  ||nargs<0||nargs>1||!AILuaInspect::capacity(L,4096))return fail("Xufa effective patterns entry unavailable");
 XufaProofStack stack(L);const int fn=lua_gettop(L)-nargs;
 if(fn<1||!matchAIPinnedFunction(r,L,fn,"extensions/addFunction.lua",1809,1838))return fail("Xufa effective patterns full source differs");
 if(nargs==1&&lua_type(L,fn+1)!=LUA_TNIL&&lua_type(L,fn+1)!=LUA_TBOOLEAN&&lua_type(L,fn+1)!=LUA_TSTRING)return fail("patterns authored argument outside fixed source");
 Check check{r,L};if(!check.primitiveMeta()||!matchAINativeValueMetamethods(r,L))return fail("patterns scalar dispatch changed");
 globals(L);field(L,-1,"sgs");if(!table(L,-1))return fail("patterns native namespace absent");int ns=lua_absindex(L,-1);
 field(L,ns,"ai_skill_choice");const void*choice=lua_topointer(L,-1);lua_pop(L,1);
 const void*globalsIdentity=lua_topointer(L,-2),*namespaceIdentity=lua_topointer(L,ns);
 auto cache=[&](int at){
  // Neither captured primitive cache may alias any retained Xufa root. Other
  // priority/role ledgers are unconditionally invalidated by this entry path.
  if(!xufaProofPlain(L,at))return false;const void*p=lua_topointer(L,at);
  if(p==choice||p==namespaceIdentity||p==globalsIdentity)return false;
  at=lua_absindex(L,at);lua_pushnil(L);int n=0;
  while(next(L,at)){if(++n>65536||!scalar(L,-2)||!scalar(L,-1))return false;lua_pop(L,1);}return true;
 };
 int env=0,seen=0;
 for(int u=1;;++u){const char*key=lua_getupvalue(L,fn,u);if(!key)break;
  if(!std::strcmp(key,"_ENV")){
   if(env||!table(L,-1))return fail("patterns environment absent/duplicate");env=lua_absindex(L,-1);
   // Lua ordinary assignment writes equip_patterns to this environment, never
   // a namespace setter or an authored __newindex. __index is separately
   // resolved as a native table chain by the exact binding checks below.
   if(lua_getmetatable(L,env)){
    if(!xufaProofPlain(L,-1))return fail("patterns environment metatable redirected");
    field(L,-1,"__newindex");bool ok=lua_isnil(L,-1);lua_pop(L,2);if(!ok)return fail("patterns environment write callback");
   }
  }else{
   if(std::strcmp(key,"banPs")&&std::strcmp(key,"cardNames")&&std::strcmp(key,"class2Names"))return fail("patterns extra capture");
   if(!cache(-1))return fail("patterns primitive cache aliases a retained root or capability");++seen;lua_pop(L,1);
  }
 }
 if(!env||seen!=3)return fail("patterns exact cache capture set differs");
 for(const char*k:{"type","ipairs"}){
  if(!xufaProofLookup(L,env,k)||!check.native(-1,QString::fromLatin1(k)))return fail(check.why);lua_pop(L,1);
 }
 for(const char*lib:{"table","string"})if(!xufaProofLookup(L,env,lib)||!xufaProofPlain(L,-1))return fail("patterns library indirection");else {
  int at=lua_absindex(L,-1);const char*k=!std::strcmp(lib,"table")?"insert":"sub";
  if(!check.nativeField(at,k,QString::fromLatin1(lib)+'.'+k))return fail(check.why);
  if(!std::strcmp(lib,"table")){
   field(L,at,"contains");if(!check.function(-1))return fail(check.why);lua_pop(L,1);
  }lua_pop(L,1);
 }
 if(!xufaProofLookup(L,env,"sgs")||!lua_rawequal(L,ns,-1))return fail("patterns engine namespace redirected");lua_pop(L,1);
 if(!matchAINativeNamespaceProperty(r,L,"sgs","Sanguosha"))return fail("patterns engine property changed");
 for(const char*k:{"getBanPackages","getCardCount","getEngineCard"})if(!matchAINativeClassMethod(r,L,"Engine",k))return fail("patterns native Engine leaf changed");
 if(!matchAINativeDerivedMethods(r,L,"Card",{"objectName","getClassName","getPackage","getTypeId"}))return fail("patterns public definition metadata leaf changed");
 return currentAIXufaContinuityWriter(r)!=nullptr;
}

namespace {
thread_local AIXufaCorrectionScope *xufaCorrection=nullptr;
bool xufaNotificationMetadata(Room&r,lua_State*L,QString*why){
 const auto fail=[&](const char*s){if(why)*why=QString::fromLatin1(s);return false;};
 auto*rt=r.roomRuntime();
 if(!rt||rt->isClosing()||!L||rt->lua().rawState()!=L
  ||LuaRuntime::current()!=&rt->lua()||!rt->lua().isCurrentThreadOwner()
  ||!Sanguosha||Sanguosha->currentRoom()!=&r)
  return fail("Xufa notification current Game VM unavailable");
 // Complete public metadata domain BEFORE a controller, holder, mark or UI
 // visibility branch. Bit2 closes the fixed frequency/equipment/quota bodies;
 // the independently compiler-bound amount row closes describeSkillUsage.
 for(const QString&name:Sanguosha->getSkillNames()){
  const Skill*s=Sanguosha->getSkill(name);
  if(!s||(aiNativeEffectClass(s)&2u)==0)
   return fail("Xufa notification native metadata body unreviewed");
  if(dynamic_cast<const AmountSkillV2*>(s)){
   const auto*source=aiActivateNativeTypeSource(s);bool amount=false;
   if(source)for(unsigned i=0;i<source->methodCount;++i)
    if(!std::strcmp(source->methods[i].method,"getBaseAmount"))amount=source->methods[i].scalarBodyProved||source->methods[i].notificationBodyProved;
   if(!amount)return fail("Xufa notification amount body unreviewed");
  }
  if(const auto*x=dynamic_cast<const LuaTriggerSkillV2*>(s))if(x->get_usage_ref!=0)
   return fail("Xufa notification trigger usage-ref callback unproved");
  if(const auto*x=dynamic_cast<const LuaViewAsSkillV2*>(s))if(x->get_usage_ref!=0)
   return fail("Xufa notification active usage-ref callback unproved");
 }
 return true;
}
}
struct AIXufaCorrectionScope::Data {
 Room&room;lua_State*L;ServerPlayer*writer;AIXufaCorrectionScope*previous;
 quint64 generation=0,transport=0,serial=0;QString why;
 std::unique_ptr<AIOwnerCardRulesScope>corrections;
 Data(Room&r,ServerPlayer*p,lua_State*l):room(r),L(l),writer(p),previous(xufaCorrection){}
};
AIXufaCorrectionScope::AIXufaCorrectionScope(Room&r,ServerPlayer*p,lua_State*L):d(new Data(r,p,L)){
 xufaCorrection=this;
 try{
  if(!p||currentAIXufaContinuityWriter(r)!=p||!xufaNotificationMetadata(r,L,&d->why))return;
  auto*rt=r.roomRuntime();d->generation=rt->lua().generation();
  d->transport=rt->xufaTransportEpoch();d->serial=currentAIXufaWriteSerial(r);
  // Reuse R56's complete correction/native-type/capture-origin/factory and
  // canonical receiver proof, plus its full lexical Card lifetime and async
  // scope. A momentary metadata boolean must not outlive these capabilities.
  d->corrections.reset(new AIOwnerCardRulesScope(r,p,L));
  if(!accepted())d->why=QStringLiteral("Xufa complete correction source lifetime unavailable");
 }catch(...){d->why=QStringLiteral("Xufa correction preflight allocation failure");}
}
AIXufaCorrectionScope::~AIXufaCorrectionScope(){
 if(d->serial&&!accepted())interruptAIXufaContinuity(d->room);
 d->corrections.reset();xufaCorrection=d->previous;
}
bool AIXufaCorrectionScope::accepted()const noexcept{
 auto*rt=d->room.roomRuntime();
 return xufaCorrection==this&&d->corrections&&d->corrections->accepted()
  &&rt&&rt->lua().rawState()==d->L&&rt->lua().generation()==d->generation
  &&d->transport&&rt->xufaTransportEpoch()==d->transport
  &&d->serial&&currentAIXufaWriteSerial(d->room)==d->serial
  &&currentAIXufaContinuityWriter(d->room)==d->writer;
}
// The fixed correction setters only publish seven named dynamic properties
// and fixed distance. Their native notification descendants reach controller
// cleanup, whose constructed marks always contain sys_; setPlayerMark therefore
// cannot dispatch MarkChange/MarkChanged. Player::removeTag is not Room::setTag
// and cannot call Scenario. UI refresh rejoins the complete correction and
// quota graph above. Disconnection revokes the atomic transport generation
// before controller/socket selection. None of these facts uses a current tag,
// flag, mark, socket or private applicability result as proof authority.
bool proveAIXufaCorrectionUnion(Room&r,lua_State*L,QString*why){
 const auto*scope=xufaCorrection;
 if(!scope||&scope->d->room!=&r||scope->d->L!=L||!scope->accepted()){
  if(why)*why=scope&&!scope->d->why.isEmpty()?scope->d->why:
   QStringLiteral("Xufa exact correction source scope absent or retired");
  return false;
 }
 if(why)why->clear();return true;
}
