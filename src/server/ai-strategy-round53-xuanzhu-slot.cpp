#include "ai-strategy-round53-xuanzhu-slot.h"
#include "ai-strategy-room-ledger.h"
#include "ai-strategy-round45-inovation-providers.h"
#include "ai-strategy-round45-native-seal.h"
#include "ai-strategy-round45-code-seal.h"
#include "ai.h"
#include "ai-strategy-round45-slot-bindings.h"
#include "ai-strategy-round44-certificate.h"
#include "room.h"
#include "room-runtime.h"
#include "engine.h"
#include "serverplayer.h"
#include "card.h"
#include "card-lifetime-manager.h"
#include "lua-runtime.h"
#include "lua.hpp"
#include <QPointer>
#include <QHash>
#include <QSet>
#include <QJsonArray>
#include <QUuid>
#include <memory>
#include <cstring>
#include <cmath>
#include "ai-strategy-round45-lua-inspect.h"
namespace {
struct Stack{lua_State*L;int n;explicit Stack(lua_State*s):L(s),n(lua_gettop(s)){}~Stack(){lua_settop(L,n);}};
struct Row{
 QPointer<ServerPlayer>owner;AIReadonlyCardLease lease;QString token;
 int offered=-1;quint64 producerDecision=0,producerRevision=0;
 QList<int>materials;int physical=-1;
};
class Ledger final:public AIRoomLedger{
public:explicit Ledger(Room&r):AIRoomLedger(r){}lua_State*L=nullptr;quint64 generation=0;
 QHash<const ServerPlayer*,Row>rows;
};
Ledger*ledger(Room&r,bool create=false){for(auto*c:r.children())if(auto*l=dynamic_cast<Ledger*>(c))return l;return create?new Ledger(r):nullptr;}
bool current(const AIRequest&q,ServerPlayer*p){return p&&p->getRoom()&&q.decisionId>0
 &&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()
 &&aiRound44RangeRequestCurrent(*p->getRoom(),q,p)&&Sanguosha&&Sanguosha->currentRoom()==p->getRoom();}
bool shown(const AIRequest&q,int id,const Card*exact=nullptr){
 // IDs in printed notification DTOs are not evidence of current wrapped face.
 // Authenticate the actually projected object BEFORE consulting its fields.
 const Card*outer=Sanguosha->getCard(id);if(!outer)return false;
 auto matches=[&](const AICardView&v){if(v.effectiveId!=id||!v.nativeIdentity||!globalCardLifetimeManager().isLive(v.nativeIdentity))return false;
  const Card*c=exact;if(!c){if(v.nativeIdentity->address==outer)c=outer;else if(v.nativeIdentity->address==outer->getRealCard())c=outer->getRealCard();}
  if(!c||v.nativeIdentity->address!=c)return false;
  return v.cardId==c->getId()&&v.effectiveId==c->getEffectiveId()&&v.objectName==c->objectName()
   &&v.className==c->getClassName()&&v.suit==int(c->getSuit())&&v.number==c->getNumber()
   &&v.skillName==c->getSkillName(false)&&v.subcardIds==c->getSubcards()&&v.virtualCard==c->isVirtualCard();
 };
 for(const auto&c:q.worldView.handCards)if(matches(c))return true;
 for(const auto&c:q.worldView.discardPile)if(matches(c))return true;
 for(const auto&c:q.choiceOptions.cards)if(matches(c))return true;
 auto ps=q.worldView.players;ps.prepend(q.worldView.self);
 for(const auto&p:ps){for(const auto&c:p.knownCards)if(matches(c))return true;for(const auto&c:p.equips)if(matches(c))return true;
  for(const auto&c:p.judgingArea)if(matches(c))return true;for(const auto&pile:p.piles)for(const auto&c:pile.cards)if(matches(c))return true;}
 return false;
}
void field(lua_State*L,int i,const char*k){AIRound45Inspect::field(L,i,k);}
bool self(lua_State*L,Room&r,ServerPlayer*p){return pushAIRound45ActualAISelf(r,p,L);}
bool absentNewIndex(lua_State*L,int t){Stack st(L);if(!lua_getmetatable(L,t))return true;field(L,-1,"__newindex");return lua_isnil(L,-1);}
bool plain(lua_State*L,int n){auto*t=AIRound45Inspect::table(L,n);return t&&!t->metatable;}
bool env(lua_State*L,int f){f=lua_absindex(L,f);for(int n=1;;++n){const char*k=lua_getupvalue(L,f,n);if(!k)break;bool ok=false;if(!std::strcmp(k,"_ENV")){AIRound45Inspect::globals(L);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}lua_pop(L,1);if(!ok)return false;}return true;}
bool code(Room&r,lua_State*L,int f,const char*path,int first,int last){return matchAIRound45PinnedFunction(r,L,f,path,first,last)&&env(L,f);}
bool sourceCallback(Room&r,lua_State*L,QString*why){
 auto fail=[&](const char*text){if(why)*why=QString::fromLatin1(text);return false;};
 Stack stack(L);AIRound45Inspect::globals(L);field(L,-1,"sgs");if(!lua_istable(L,-1))return fail("Xuanzhu source sgs table unavailable");
 field(L,-1,"ai_skill_use");if(!plain(L,-1))return fail("Xuanzhu source use registry is not plain");field(L,-1,"@@xuanzhu");
 if(!matchAIRound45PinnedFunction(r,L,-1,"lua/ai/ol_sp-ai.lua",2455,2471,why))return false;
 if(!env(L,-1))return fail("Xuanzhu pinned source reader environment changed");
 if(why)why->clear();return true;
}
bool sourceProducer(Room&r,lua_State*L){
 Stack st(L);AIRound45Inspect::globals(L);field(L,-1,"sgs");if(!lua_istable(L,-1))return false;
 field(L,-1,"ai_skills");if(!plain(L,-1))return false;const int rows=lua_absindex(L,-1);int matches=0;
 lua_pushnil(L);while(AIRound45Inspect::next(L,rows)){
  if(lua_istable(L,-1)){const int row=lua_absindex(L,-1);field(L,row,"name");const bool named=lua_type(L,-1)==LUA_TSTRING&&QString::fromUtf8(lua_tostring(L,-1))=="xuanzhu";lua_pop(L,1);
   if(named){if(!plain(L,row))return false;field(L,row,"getTurnUseCard");if(!code(r,L,-1,"lua/ai/ol_sp-ai.lua",2420,2446))return false;lua_pop(L,1);++matches;}}
  lua_pop(L,1);
 }return matches==1;
}
// Exact table-valued __index lookup. Never execute authored getter functions.
bool lookup(lua_State*L,int value,const char*key){
 value=lua_absindex(L,value);QSet<const void*>seen;
 for(int depth=0;depth<16;++depth){if(!lua_istable(L,value)||seen.contains(lua_topointer(L,value)))return false;
  seen.insert(lua_topointer(L,value));field(L,value,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);
  if(!lua_getmetatable(L,value)){lua_pushnil(L);return true;}
  field(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;value=lua_absindex(L,-1);
 }return false;
}
bool sourceReadBindings(Room&r,lua_State*L){
 Stack stack(L);AIRound45Inspect::globals(L);const int globals=lua_absindex(L,-1);
 field(L,globals,"sgs");if(!lua_istable(L,-1))return false;const int sgs=lua_absindex(L,-1);
 field(L,sgs,"ai_skill_use");if(!plain(L,-1))return false;field(L,-1,"@@xuanzhu");
 if(!code(r,L,-1,"lua/ai/ol_sp-ai.lua",2455,2471))return false;
 field(L,sgs,"list");if(!code(r,L,-1,"lua/utilities.lua",38,41))return false;
 field(L,sgs,"qlist");const int qlist=lua_absindex(L,-1);
 if(!matchAIRound45PinnedFunction(r,L,qlist,"lua/utilities.lua",19,35))return false;
 for(int n=1;;++n){const char*k=lua_getupvalue(L,qlist,n);if(!k)break;bool ok=false;
  if(!std::strcmp(k,"_ENV")){lua_pushvalue(L,globals);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}
  else if(!std::strcmp(k,"qlist_userdata_iterator"))ok=code(r,L,-1,"lua/utilities.lua",7,11);
  else if(!std::strcmp(k,"qlist_table_iterator"))ok=code(r,L,-1,"lua/utilities.lua",13,17);
  lua_pop(L,1);if(!ok)return false;
 }
 for(const char*k:{"type","ipairs"}){field(L,globals,k);if(!matchAIRound45NativeBinding(r,L,-1,k))return false;lua_pop(L,1);}
 field(L,globals,"table");if(!plain(L,-1))return false;const int table=lua_absindex(L,-1);
 for(const char*k:{"insert","concat"}){field(L,table,k);if(!matchAIRound45NativeBinding(r,L,-1,QString("table.")+k))return false;lua_pop(L,1);}
 return matchAIRound45NativeClassMethod(r,L,"SPlayerList","length")&&matchAIRound45NativeClassMethod(r,L,"SPlayerList","at")
  &&matchAIRound45NativeDerivedMethods(r,L,"ServerPlayer",{"objectName"});
}
// Executes only our fixed native assignment thunk. No legacy AI strategy call.
// The caller proved source-equivalent __newindex behavior before entering it.
struct Write {const AIRequest*request;ServerPlayer*owner;AIReadonlyCardLease lease;const Card*card;QList<ServerPlayer*>targets;int extra=0;};
int assign(lua_State*L){
 // This is a new protected C call frame; the outer host reserve is not inherited.
 if(!lua_checkstack(L,4096))return luaL_error(L,"Xuanzhu assignment frame capacity unavailable");
 auto*w=static_cast<Write*>(lua_touserdata(L,2));
 lua_newtable(L);const int d=lua_gettop(L);
 lua_pushboolean(L,1);lua_setfield(L,d,"isDummy");
 lua_pushinteger(L,w->extra);lua_setfield(L,d,"extra_target");
 lua_newtable(L);lua_setfield(L,d,"current_targets");
 if(!pushAIRound45SlotCard(L,w->card))return luaL_error(L,"Xuanzhu native Card wrapper unavailable");lua_setfield(L,d,"card");
 if(!pushAIRound45SlotTargets(L,w->targets))return luaL_error(L,"Xuanzhu native target list unavailable");lua_setfield(L,d,"to");
 if(!current(*w->request,w->owner)||w->lease()!=w->card||!self(L,*w->owner->getRoom(),w->owner)||!lua_rawequal(L,1,-1))
  return luaL_error(L,"Xuanzhu actual owner/request changed during allocation");
 lua_pop(L,1);
 // Preserve source assignment (not rawset). If GC changed the exact receiver's
 // absent-key metatable after preflight, reject before dispatching that hook.
 field(L,1,"xuanzhuUse");const bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(!present&&!absentNewIndex(L,1))return luaL_error(L,"Xuanzhu source __newindex changed during allocation");
 lua_pushvalue(L,d);lua_setfield(L,1,"xuanzhuUse");
 lua_pushvalue(L,d);return 1;
}
struct ReadTargets {const AIRequest*q;ServerPlayer*owner;QList<ServerPlayer*>*out;const char*error=nullptr;};
int readTargets(lua_State*L){
 auto*w=static_cast<ReadTargets*>(lua_touserdata(L,2));
 auto fail=[&](const char*s){w->error=s;return luaL_error(L,"%s",s);};
 if(!lua_checkstack(L,4096))return fail("saved native target reader stack unavailable");
 Room&r=*w->owner->getRoom();const int list=1;
 auto certified=[&](){QList<ServerPlayer*>tracked;
  if(readAIRound45SlotTargets(L,list,tracked))return matchAIRound45NativeClassMethod(r,L,"SPlayerList","length")
   &&matchAIRound45NativeClassMethod(r,L,"SPlayerList","at");
  return matchAIRound45NativeObjectMethods(r,L,list,"SPlayerList",{"length","at"});
 };
 for(int i=0;;++i){
  if(!current(*w->q,w->owner)||!certified())return fail("saved native target dispatch changed during iteration");
  lua_getfield(L,list,"length");lua_pushvalue(L,list);lua_call(L,1,1);
  if(lua_type(L,-1)!=LUA_TNUMBER)return fail("saved native target length ABI changed");
  const double n=lua_tonumber(L,-1);lua_pop(L,1);
  if(!std::isfinite(n)||n<0||n>128||n!=std::floor(n))return fail("saved target budget exceeded");
  if(i>=int(n))break;
  lua_getfield(L,list,"at");lua_pushvalue(L,list);lua_pushinteger(L,i);lua_call(L,2,1);
  ServerPlayer*p=nullptr;
  for(auto*t:r.getAllPlayers(true))if(t&&matchAIRound45SlotPlayer(L,-1,t)){p=t;break;}
  if(!p||!matchAIRound45NativeObjectMethods(r,L,-1,"ServerPlayer",{"objectName"}))return fail("saved native target identity or method changed");
  auto players=w->q->worldView.players;players.prepend(w->q->worldView.self);
  bool disclosed=false;for(const auto&v:players)if(v.objectName==p->objectName()){disclosed=true;break;}
  if(!disclosed)return fail("saved native target absent from current projected roster");
  w->out->append(p);lua_pop(L,1);
 }
 if(!current(*w->q,w->owner))return fail("saved target iteration changed request");
 return 0;
}
}
bool registerAIRound53XuanzhuSlotSources(Room&r,lua_State*L,QString*error){
 // Independently fixed source bytes, not a hash derived from the installed
 // effective callback. The common seal verifies runtime generation and compiles
 // metadata without executing any package policy. Never reseal on a reader.
 if(!registerAIRound45PinnedSource(r,L,"lua/ai/ol_sp-ai.lua",
     QByteArray("d8cc0c6d06b58c3d7306e5d742e44247e88b2050f94df886600f840e68b4205a"),error))return false;
 return registerAIRound45PinnedSource(r,L,"lua/utilities.lua",
     QByteArray("b76cdc44c0fec46f1e10d918c6e0676c5e301ed92cd5b0e35da882606a7d4d3a"),error);
}
void clearAIRound53XuanzhuSlots(Room&r){if(auto*l=ledger(r))delete l;}
QString writeAIRound53XuanzhuSlot(const AIRequest&q,ServerPlayer*p,AIReadonlyCardLease lease,const QStringList&names,const QJsonObject&fields,QString*error){
 auto fail=[&](const QString&s){if(error)*error=s;return QString();};
 if(!current(q,p)||q.kind!=AIRequest::Activate||q.reason!=CardUseStruct::CARD_USE_REASON_PLAY||p->getPhase()!=Player::Play||!lease)
  return fail("Xuanzhu source assignment requires current owner Play producer");
 if(!current(q,p))return fail("Xuanzhu skill lookup invalidated producer request");
 if(fields.size()!=3||fields.value("isDummy")!=QJsonValue(true)||!fields.value("extra_target").isDouble()
  ||fields.value("extra_target").toDouble()!=fields.value("extra_target").toInt()||fields.value("current_targets_empty")!=QJsonValue(true))return fail("Xuanzhu full dummy field envelope unsupported");
 const Card*card=lease();if(!card)return fail("Xuanzhu exact producer custody expired");
 const int physical=card->isVirtualCard()?-1:card->getEffectiveId();if(physical>=0&&!shown(q,physical,card))return fail("Xuanzhu selected physical replacement undisclosed");
 const auto costs=card->getSubcards();for(int id:costs)if(!shown(q,id))return fail("Xuanzhu selected source material undisclosed");
 Room&r=*p->getRoom();auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();
 if(!L||!lua_checkstack(L,4096))return fail("Xuanzhu game runtime unavailable");Stack stack(L);
 if(!sourceProducer(r,L))return fail("Xuanzhu actual complete source producer changed");
 if(!self(L,r,p))return fail("Xuanzhu actual LuaAI self authentication failed");const int receiver=lua_absindex(L,-1);
 field(L,receiver,"xuanzhuUse");const bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(!present&&!absentNewIndex(L,receiver))return fail("Xuanzhu source __newindex is not direct assignment");
 Write w;w.request=&q;w.owner=p;w.lease=lease;w.card=card;w.extra=fields.value("extra_target").toInt();
 const auto roster=r.getAllPlayers(true);for(const auto&name:names){ServerPlayer*target=nullptr;for(auto*t:roster)if(t&&t->objectName()==name){target=t;break;}if(!target)return fail("Xuanzhu source target outside roster");w.targets<<target;}
 lua_pushcfunction(L,assign);lua_pushvalue(L,receiver);lua_pushlightuserdata(L,&w);
 if(LuaRuntime::protectedCall(L,2,1,0)!=LUA_OK)return fail("Xuanzhu native source assignment did not complete");
 if(!current(q,p)||!lease())return fail("Xuanzhu assignment changed current request or custody");
 field(L,receiver,"xuanzhuUse");const bool exact=lua_rawequal(L,-1,-2);lua_pop(L,1);if(!exact||!plain(L,-1))return fail("Xuanzhu source assignment was replaced");
 auto*l=ledger(r,true);if(l->L&&l->generation!=rt->lua().generation())return fail("Xuanzhu source runtime generation changed");l->L=L;l->generation=rt->lua().generation();
 Row row;row.owner=p;row.lease=std::move(lease);row.token="xuanzhu-actual-slot:"+QUuid::createUuid().toString(QUuid::WithoutBraces);row.materials=costs;row.physical=physical;row.producerDecision=q.decisionId;row.producerRevision=q.stateRevision;
 l->rows[p]=row; // Source writes before its following first-card Parse; no rollback.
 if(!current(q,p)||!row.lease())return fail("Xuanzhu source publication changed request or custody");
 if(error)error->clear();return row.token;
}
AIRound53XuanzhuSlotRead readAIRound53XuanzhuSlot(const AIRequest&q,ServerPlayer*p,bool includeCard){
 AIRound53XuanzhuSlotRead out;auto fail=[&](const QString&s){out=AIRound53XuanzhuSlotRead();out.error=s;return out;};
 if(!current(q,p)||q.kind!=AIRequest::UseCard||q.pattern!="@@xuanzhu"||q.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE||q.handlingMethod!=Card::MethodUse)return fail("Xuanzhu source read requires actual current response");
 auto finish=[&](){if(!current(q,p)){out=AIRound53XuanzhuSlotRead();out.error="Xuanzhu source read invalidated current request";}return out;};
 Room&r=*p->getRoom();auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();
 if(!L||!lua_checkstack(L,4096))return fail("Xuanzhu actual source runtime unavailable");Stack stack(L);QString why;
 if(!sourceCallback(r,L,&why))return fail("Xuanzhu source reader: "+why);
 if(!self(L,r,p)||!lookup(L,-1,"xuanzhuUse"))return fail("Xuanzhu actual slot receiver/getter unproved");
 if(lua_isnil(L,-1)||lua_isboolean(L,-1)){
  if(AIRound45Inspect::primitiveMetatable(L,lua_type(L,-1)))return fail("Xuanzhu primitive indexing metatable changed");
  out.kind=lua_isnil(L,-1)?AIRound53XuanzhuSlotRead::NilPlan:AIRound53XuanzhuSlotRead::BooleanPlan;return finish();
 }
 if(!lua_istable(L,-1))return fail("Xuanzhu source plan is outside table domain");const int d=lua_absindex(L,-1);
 // Source enumerates d.to BEFORE it ever reads d.card. No hand means it never reads card.
 if(!lookup(L,d,"to"))return fail("Xuanzhu source target getter unproved");
 if(!sourceReadBindings(r,L))return fail("Xuanzhu reached source list bindings changed");
 if(lua_isnil(L,-1)||lua_isboolean(L,-1)){out.kind=AIRound53XuanzhuSlotRead::TargetsError;out.targetsNil=lua_isnil(L,-1);out.targetsBoolean=lua_isboolean(L,-1);return finish();}
 QList<ServerPlayer*>actual;ReadTargets read{&q,p,&actual};
 const int sourceList=lua_absindex(L,-1);
 lua_pushcfunction(L,readTargets);lua_pushvalue(L,sourceList);lua_pushlightuserdata(L,&read);
 if(LuaRuntime::protectedCall(L,2,0,0)!=LUA_OK)return fail(read.error?QString::fromLatin1(read.error):QStringLiteral("Xuanzhu actual target iteration failed"));
 lua_pop(L,1);
 const auto roster=r.getAllPlayers(true);for(auto*t:actual){if(!t||!roster.contains(t))return fail("Xuanzhu actual target left roster");out.targets<<t->objectName();}
 out.kind=AIRound53XuanzhuSlotRead::Targets;
 if(!includeCard)return finish();
 if(p->getCards("he").isEmpty())return fail("Xuanzhu source has no material and does not read card");
 if(!lookup(L,d,"card"))return fail("Xuanzhu source card getter unproved");
 if(lua_isnil(L,-1)||lua_isboolean(L,-1)){
  if(AIRound45Inspect::primitiveMetatable(L,lua_type(L,-1)))return fail("Xuanzhu primitive Card indexing changed");
  out.kind=AIRound53XuanzhuSlotRead::CardError;out.cardNil=lua_isnil(L,-1);return finish();
 }
 auto*l=ledger(r);if(!l||l->L!=L||l->generation!=rt->lua().generation())return fail("Xuanzhu actual Card has no retained producer custody");
 const auto it=l->rows.constFind(p);if(it==l->rows.cend()||it->owner!=p)return fail("Xuanzhu current owner has no retained producer");const Row row=*it;
 const Card*card=row.lease?row.lease():nullptr;if(!card||!matchAIRound45SlotCard(L,-1,card)||!matchAIRound45NativeObjectMethods(r,L,-1,"Card",{"objectName"}))return fail("Xuanzhu actual Card identity/objectName changed");
 if(row.physical>=0&&!shown(q,row.physical,card))return fail("Xuanzhu physical current face not disclosed");
 for(int id:row.materials)if(!shown(q,id))return fail("Xuanzhu material observation expired");
 if(card->getSubcards()!=row.materials)return fail("Xuanzhu retained material provenance changed");
 out.name=card->objectName();out.lease=row.lease;out.kind=AIRound53XuanzhuSlotRead::Card;
 if(!current(q,p)||row.lease()!=card)return fail("Xuanzhu source read changed custody/request");return finish();
}
std::unique_ptr<Card> constructAIRound53XuanzhuPolicy(const AIRequest&q,ServerPlayer*p,const QString&name,QString*error){
 auto fail=[&](const char*s)->std::unique_ptr<Card>{if(error)*error=QString::fromLatin1(s);return{};};
 if(!current(q,p)||q.kind!=AIRequest::Activate||q.reason!=CardUseStruct::CARD_USE_REASON_PLAY||p->getPhase()!=Player::Play)return fail("Xuanzhu dummy requires current owner Play");
 bool own=false;for(const auto&i:p->getSkillInstances())if(i.skillName=="xuanzhu")own=true;if(!own)return fail("Xuanzhu source skill is not owned");
 const auto names=q.worldView.strategyFacts.value("pattern_names").toArray();if(!names.contains(name))return fail("Xuanzhu source name is outside issued patterns");
 std::unique_ptr<Card>c(Sanguosha->cloneCard(name,Card::NoSuit,0));if(error)error->clear();return c;
}
