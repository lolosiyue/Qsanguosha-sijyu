#include "ai-strategy-round56-maxcards.h"
#include "ai-strategy-round56-playloop-filter.h"
#include <cstdio>
#ifdef Q_OS_LINUX
#include <execinfo.h>
#endif
#include "ai-strategy-round56-projection.h"
#include "ai-strategy-round47-xufa-query-scope.h"
#include "ai-strategy-round53-ainoto.h"
#include "ai-strategy-round47-xufa-proof.h"
#include "ai-strategy-choice-continuity.h"
#include "ai-strategy-round45-continuity.h"
#include "ai-strategy-round41-response.h"
#include "ai-strategy-round45-native-seal.h"
#include "ai-strategy-round45-slot-bindings.h"
#include "ai-strategy-public-disclosure.h"
#include "room.h"
#include "serverplayer.h"
#include "room-runtime.h"
#include "lua-runtime.h"
#include "card.h"
#include "wrapped-card.h"
#include "standard.h"
#include "maneuvering.h"
#include "ol.h"
#include "ai.h"
#include <QPointer>
#include <QHash>
#include <QList>
#include <QSet>
#include <typeinfo>
#include "ai-strategy-round45-lua-inspect.h"
namespace {
thread_local QHash<Room*,QList<const AIRound47XufaQueryScope*>> queries;
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
 const auto stamp=currentAIRound47XufaWriteSerial(r);
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
struct AIRound47XufaSourceCard::Data {
 Room*room=nullptr;QPointer<ServerPlayer>owner;QPointer<const Card>card;const AIRequest*request=nullptr;QList<int>ids;quint64 writeSerial=0;bool valid=false;
};
namespace {thread_local QHash<Room*,QList<const AIRound47XufaSourceCard*>> sourceCards;}
struct AIRound47XufaSourceAccess {
 static bool current(Room&r,ServerPlayer*p,const Card*c){
  const auto i=sourceCards.constFind(&r);if(i==sourceCards.cend()||i->isEmpty())return false;
  const auto*d=i->last()->d.get();return d->valid&&d->writeSerial&&d->writeSerial==currentAIRound47XufaWriteSerial(r)&&d->owner==p&&d->card==c&&d->request
   &&d->request->stateRevision==r.roomRuntime()->stateRevision()&&c->getSubcards()==d->ids;
 }
};
namespace {bool sourceCard(Room&r,ServerPlayer*p,const Card*c){return AIRound47XufaSourceAccess::current(r,p,c);}}
AIRound47XufaSourceCard::AIRound47XufaSourceCard(Room&r,const AIRequest&q,ServerPlayer*p,const Card*c,const QString&skill):d(new Data){
 d->room=&r;d->owner=p;d->card=c;d->request=&q;d->writeSerial=currentAIRound47XufaWriteSerial(r);sourceCards[&r].append(this);
 if(skill!="xufa"||!p||!c||p->getRoom()!=&r||q.kind!=AIRequest::Activate
  ||q.viewerObjectName!=p->objectName()||q.worldView.self.objectName!=p->objectName()
  ||q.stateRevision!=r.roomRuntime()->stateRevision()||currentAIRound47XufaContinuityWriter(r)!=p)return;
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
 d->valid=typeid(*c)==typeid(XufaCard)&&c->getId()<0&&d->writeSerial&&d->writeSerial==currentAIRound47XufaWriteSerial(r);
}
AIRound47XufaSourceCard::~AIRound47XufaSourceCard(){auto i=sourceCards.find(d->room);if(i==sourceCards.end())return;i->removeAll(this);if(i->isEmpty())sourceCards.erase(i);}
struct AIRound47XufaQueryScope::Data{Room*room=nullptr;QPointer<ServerPlayer>writer;quint64 writeSerial=0;bool admitted=false;};
struct AIRound47XufaQueryAccess{
 static AIRound47XufaQueryScope::Data*top(Room*r){auto i=queries.constFind(r);return i==queries.cend()||i->isEmpty()?nullptr:i->last()->d.get();}
};
AIRound47XufaQueryScope::AIRound47XufaQueryScope(Room*r,const Card*c,bool required,bool preserveAiNoTo,bool preservePriority,bool scalarCardOnly):d(new Data){
 d->room=r;if(!r)return;
 if(preserveAIRound56ProjectionNativeQuery(*r,c,required,scalarCardOnly)
   ||preserveAIRound56PlayerCorrectionNativeQuery(*r,c,required))return;
 // R55: the separate priority flag is supplied only by the complete native-only
 // public skill-query proofs. It does not follow a callback-only aiNoTo proof.
 // No grant is created, refreshed or restamped; Lua/async entries still revoke.
 if(!preservePriority){
  // First default native revocation while the exact source ledger is still
  // live. Code addresses only; this diagnostic never changes admission.
  if(qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")
    &&hasAIRound56PlayLoopSource(*r,r->roomRuntime()->lua().rawState())){
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
 if(!preserveAiNoTo)retireAIRound53AiNoTo(*r);
 auto*parent=AIRound47XufaQueryAccess::top(r);
 d->writer=currentAIRound47XufaContinuityWriter(*r);d->writeSerial=currentAIRound47XufaWriteSerial(*r);
 // No current known row: this path must never create/revive one.
 if(!d->writer){observeAIRound45UnprovedNativeQuery(r,preserveAiNoTo);queries[r].append(this);return;}
 bool ok=false;try {ok=!required||custody(*r,d->writer,c);
 // v2 deliberately does not reuse a parent's proof until epoch, generation
 // and native registry/source witnesses are all installed. Never reuse based
 // solely on equality of the writer pointer after an interruption/rewrite.
 if(ok){
  auto*rt=r->roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();
  // This is normal synchronous native code, before observer/protectedCall.
  // Reservation here is allowed; the entry proof itself never allocates stack.
  ok=L&&lua_checkstack(L,8192)&&proveAIRound47XufaCorrectionUnion(*r,L);
 }
 }catch(...){ok=false;}
 // Nested proof allocation or native reentry can only revoke, never reseed.
 ok=ok&&d->writeSerial&&currentAIRound47XufaWriteSerial(*r)==d->writeSerial&&currentAIRound47XufaContinuityWriter(*r)==d->writer;
 d->admitted=ok;
 if(ok){interruptAIChoiceContinuityExceptXufa(*r);clearAIRound41FaqiPlans(*r);}
 else observeAIRound45UnprovedNativeQuery(r,preserveAiNoTo);
 queries[r].append(this);
}
AIRound47XufaQueryScope::~AIRound47XufaQueryScope(){if(!d->room)return;auto i=queries.find(d->room);if(i==queries.end())return;i->removeAll(this);if(i->isEmpty())queries.erase(i);}
AIRound47XufaCanSlashCard::AIRound47XufaCanSlashCard(Room*r,const Card*c):room(r),card(c){
 if(r)recordAIRound56ProjectionSourceCard(*r,c,QStringLiteral("slash"));
 auto*q=AIRound47XufaQueryAccess::top(r);
 if(r&&q&&q->admitted&&q->writeSerial&&q->writeSerial==currentAIRound47XufaWriteSerial(*r)&&c&&typeid(*c)==typeid(Slash)&&c->getId()<0&&c->getSubcards().isEmpty()
    &&c->getSuit()==Card::NoSuit&&c->getNumber()==0&&c->getSkillName(false).isEmpty()){
  stamp=q->writeSerial;freshCanSlash[r].append({c,stamp});entered=true;
 }
}
AIRound47XufaCanSlashCard::~AIRound47XufaCanSlashCard(){if(!entered)return;auto i=freshCanSlash.find(room);if(i==freshCanSlash.end())return;i->removeOne({card,stamp});if(i->isEmpty())freshCanSlash.erase(i);}
AIRound47XufaProjectionCard::AIRound47XufaProjectionCard(Room&r,ServerPlayer*p,const Card*c,const QString&name):room(&r),card(c){
 recordAIRound56ProjectionSourceCard(r,c,name);
 if(!p||!c||p->getRoom()!=&r||currentAIRound47XufaContinuityWriter(r)!=p)return;
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
 stamp=currentAIRound47XufaWriteSerial(r);if(!stamp)return;projectionCards[&r].append({c,stamp});entered=true;
}
AIRound47XufaProjectionCard::~AIRound47XufaProjectionCard(){if(!entered)return;auto i=projectionCards.find(room);if(i==projectionCards.end())return;i->removeOne({card,stamp});if(i->isEmpty())projectionCards.erase(i);}
bool certifyAIRound47XufaQueryEntry(Room&r,lua_State*L,int nargs,QString*why){
 auto fail=[&](const char*s){if(why)*why=QString::fromLatin1(s);return false;};
 auto*q=AIRound47XufaQueryAccess::top(&r);
 if(!q||!q->admitted||!q->writer||!q->writeSerial||q->writeSerial!=currentAIRound47XufaWriteSerial(r)||currentAIRound47XufaContinuityWriter(r)!=q->writer)return fail("Xufa current proved native getter scope absent");
 if(!L||r.roomRuntime()->lua().state()!=L||nargs<1||nargs>5||!AIRound45Inspect::capacity(L,4096))return fail("Xufa getter native call shape changed");
 const int top=lua_gettop(L),fn=top-nargs;Stack st{L,top};if(fn<1)return fail("Xufa getter callable missing");
 // Native wrappers supply scalar/nil or their SWIG typed objects. No authored
 // table/metamethod arguments can enter the pinned getter body through this
 // route. Private object fields are not read by the certificate.
 for(int i=fn+1;i<=top;++i){int type=lua_type(L,i);
  if(type==LUA_TNIL||type==LUA_TNUMBER||type==LUA_TBOOLEAN||type==LUA_TSTRING)continue;
  if(type!=LUA_TUSERDATA)return fail("Xufa getter argument is authored capability");
  bool known=matchAIRound45NativeObjectMethods(r,L,i,"Player",{"objectName"})
   ||matchAIRound45NativeObjectMethods(r,L,i,"Skill",{"objectName"})
   ||matchAIRound45NativeObjectMethods(r,L,i,"Card",{"objectName"})
   ||matchAIRound45NativeObjectMethods(r,L,i,"CorrectSkillContext",{"getPrimary","getCard"})
   ||matchAIRound45NativeObjectMethods(r,L,i,"PlayerList",{"length","at"});
  if(!known)return fail("Xufa getter native argument class/binding changed");
 }
 return proveAIRound47XufaGetterFunction(r,L,fn,why);
}
