#include "ai-strategy-round45-faqi-slot.h"
#include "ai-strategy-room-ledger.h"
#include "ai-strategy-round45-inovation-providers.h"
#include "ai-strategy-round45-native-seal.h"
#include "ai-strategy-round45-code-seal.h"
#include "ai.h"
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
#include "ai-strategy-round45-lua-inspect.h"
namespace {
struct Stack{lua_State*L;int n;explicit Stack(lua_State*s):L(s),n(lua_gettop(s)){}~Stack(){lua_settop(L,n);}};
struct Row{
 QPointer<ServerPlayer>owner;AIReadonlyCardLease lease;QString token;
 int tableRef=LUA_NOREF,offered=-1;quint64 producerDecision=0,producerRevision=0;
 QList<int>materials;int physical=-1;
};
class Ledger final:public AIRoomLedger{
public:explicit Ledger(Room&r):AIRoomLedger(r){}lua_State*L=nullptr;quint64 generation=0;
 QHash<const ServerPlayer*,Row>rows;
};
Ledger*ledger(Room&r,bool create=false){for(auto*c:r.children())if(auto*l=dynamic_cast<Ledger*>(c))return l;return create?new Ledger(r):nullptr;}
bool current(const AIRequest&q,ServerPlayer*p){return p&&p->getRoom()&&q.decisionId>0
 &&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()
 &&q.stateRevision==p->getRoom()->roomRuntime()->stateRevision()&&Sanguosha&&Sanguosha->currentRoom()==p->getRoom();}
bool shown(const AIRequest&q,int id,const Card*exact=nullptr){
 // IDs in printed notification DTOs are not evidence of current wrapped face.
 // Authenticate the actually projected object BEFORE consulting its fields.
 if(!exact)exact=Sanguosha->getCard(id);if(!exact)return false;
 auto matches=[&](const AICardView&v){if(v.effectiveId!=id||!v.nativeIdentity
   ||!globalCardLifetimeManager().isLive(v.nativeIdentity)||v.nativeIdentity->address!=exact)return false;
  return v.cardId==exact->getId()&&v.effectiveId==exact->getEffectiveId()
   &&v.objectName==exact->objectName()&&v.className==exact->getClassName()
   &&v.suit==int(exact->getSuit())&&v.number==exact->getNumber()
   &&v.skillName==exact->getSkillName(false)&&v.subcardIds==exact->getSubcards()
   &&v.virtualCard==exact->isVirtualCard();
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
bool sourceCallback(Room&r,lua_State*L){
 Stack stack(L);AIRound45Inspect::globals(L);field(L,-1,"sgs");if(!lua_istable(L,-1))return false;
 field(L,-1,"ai_skill_use");if(!plain(L,-1))return false;field(L,-1,"@@faqi");
 return code(r,L,-1,"lua/ai/doudizhu-ai.lua",158,167);
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
 field(L,sgs,"ai_skill_use");if(!plain(L,-1))return false;field(L,-1,"@@faqi");
 if(!code(r,L,-1,"lua/ai/doudizhu-ai.lua",158,167))return false;
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
 if(!lua_checkstack(L,4096))return luaL_error(L,"Faqi assignment inspection stack unavailable");
 auto*w=static_cast<Write*>(lua_touserdata(L,2));
 lua_newtable(L);const int d=lua_gettop(L);
 lua_pushboolean(L,1);lua_setfield(L,d,"isDummy");
 lua_pushinteger(L,w->extra);lua_setfield(L,d,"extra_target");
 lua_newtable(L);lua_setfield(L,d,"current_targets");
 if(!pushAIRound45SlotCard(L,w->card))return luaL_error(L,"Faqi native Card wrapper unavailable");lua_setfield(L,d,"card");
 if(!pushAIRound45SlotTargets(L,w->targets))return luaL_error(L,"Faqi native target list unavailable");lua_setfield(L,d,"to");
 if(!current(*w->request,w->owner)||w->lease()!=w->card||!self(L,*w->owner->getRoom(),w->owner)||!lua_rawequal(L,1,-1))
  return luaL_error(L,"Faqi actual owner/request changed during allocation");
 lua_pop(L,1);
 // Preserve source assignment (not rawset). If GC changed the exact receiver's
 // absent-key metatable after preflight, reject before dispatching that hook.
 field(L,1,"faqiUse");const bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(!present&&!absentNewIndex(L,1))return luaL_error(L,"Faqi source __newindex changed during allocation");
 lua_pushvalue(L,d);lua_setfield(L,1,"faqiUse");
 lua_pushvalue(L,d);return 1;
}
}
void clearAIRound45FaqiSlots(Room&r){auto*l=ledger(r);if(!l)return;
 auto*rt=r.roomRuntime();if(rt&&rt->lua().rawState()&&rt->lua().generation()==l->generation){
  LuaRuntime::Binding b(rt->lua(),false);auto*L=rt->lua().state();if(L==l->L)for(const auto&row:l->rows)if(row.tableRef!=LUA_NOREF)luaL_unref(L,LUA_REGISTRYINDEX,row.tableRef);
 }delete l;
}
bool hasAIRound45FaqiSlot(Room&r,ServerPlayer*p,const QString&token){auto*l=ledger(r);if(!l||!p||token.isEmpty())return false;auto i=l->rows.constFind(p);return i!=l->rows.cend()&&i->owner==p&&i->token==token;}
QString writeAIRound45FaqiSlot(const AIRequest&q,ServerPlayer*p,int offered,AIReadonlyCardLease lease,const QStringList&names,const QJsonObject&fields,QString*error){
 auto fail=[&](const QString&s){if(error)*error=s;return QString();};
 if(!current(q,p)||q.kind!=AIRequest::AmazingGrace||q.choiceOptions.reason!="faqi"||!q.choiceOptions.cardIds.contains(offered)||!lease)return fail("Faqi source assignment requires actual current offered AG");
 if(fields.size()!=3||fields.value("isDummy")!=QJsonValue(true)||!fields.value("extra_target").isDouble()
  ||fields.value("extra_target").toDouble()!=fields.value("extra_target").toInt()||fields.value("current_targets_empty")!=QJsonValue(true))return fail("Faqi full dummy field envelope unsupported");
 const Card*card=lease();if(!card)return fail("Faqi exact producer custody expired");
 const int physical=card->isVirtualCard()?-1:card->getEffectiveId();if(physical>=0&&!shown(q,physical,card))return fail("Faqi source physical card undisclosed");
 const auto costs=card->getSubcards();for(int id:costs)if(!shown(q,id))return fail("Faqi source material undisclosed");
 Room&r=*p->getRoom();auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();
 if(!L||!lua_checkstack(L,4096))return fail("Faqi game runtime unavailable");Stack stack(L);
 if(!self(L,r,p))return fail("Faqi actual LuaAI self authentication failed");const int receiver=lua_absindex(L,-1);
 field(L,receiver,"faqiUse");const bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(!present&&!absentNewIndex(L,receiver))return fail("Faqi source __newindex is not the direct assignment domain");
 Write w;w.request=&q;w.owner=p;w.lease=lease;w.card=card;w.extra=fields.value("extra_target").toInt();
 const auto roster=r.getAllPlayers(true);for(const auto&name:names){ServerPlayer*target=nullptr;for(auto*t:roster)if(t&&t->objectName()==name){target=t;break;}if(!target)return fail("Faqi source target is outside current roster");w.targets<<target;}
 lua_pushcfunction(L,assign);lua_pushvalue(L,receiver);lua_pushlightuserdata(L,&w);
 if(LuaRuntime::protectedCall(L,2,1,0)!=LUA_OK)return fail("Faqi native source assignment failed before completion");
 if(!current(q,p)||!lease())return fail("Faqi source assignment changed current request or custody");
 field(L,receiver,"faqiUse");const bool exact=lua_rawequal(L,-1,-2);lua_pop(L,1);if(!exact||!plain(L,-1))return fail("Faqi assignment no longer names its actual source table");
 auto*l=ledger(r,true);if(l->L&&l->generation!=rt->lua().generation())return fail("Faqi synchronized runtime generation changed");l->L=L;l->generation=rt->lua().generation();
 Row row;row.owner=p;row.lease=std::move(lease);row.token="faqi-actual-slot:"+QUuid::createUuid().toString(QUuid::WithoutBraces);row.offered=offered;row.materials=costs;row.physical=physical;row.producerDecision=q.decisionId;row.producerRevision=q.stateRevision;
 lua_pushvalue(L,-1);row.tableRef=luaL_ref(L,LUA_REGISTRYINDEX);
 auto old=l->rows.find(p);if(old!=l->rows.end()&&old->tableRef!=LUA_NOREF)luaL_unref(L,LUA_REGISTRYINDEX,old->tableRef);
 l->rows[p]=row;
 if(!current(q,p)||!row.lease())return fail("Faqi source assignment publication changed current request/custody");
 if(error)error->clear();return row.token;
}
const Card*readAIRound45FaqiSlot(const AIRequest&q,ServerPlayer*p,const QString&token,QStringList*targets,int*offered,QString*error,AIRound45FaqiSlotStatus*status){
 if(status)*status=AIRound45FaqiSlotStatus::Unknown;
 auto fail=[&](const QString&s)->const Card*{if(error)*error=s;return nullptr;};
 if(!current(q,p))return fail("Faqi actual source read requires current owner request");Room&r=*p->getRoom();auto*l=ledger(r);auto*rt=r.roomRuntime();
 if(!l||l->generation!=rt->lua().generation()||!hasAIRound45FaqiSlot(r,p,token))return fail("Faqi actual slot provenance unavailable");
 const Row row=l->rows.value(p);
 LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();if(!L||L!=l->L||!lua_checkstack(L,4096))return fail("Faqi actual source runtime unavailable");Stack stack(L);
 if(!sourceCallback(r,L))return fail("Faqi actual source callback changed");
 if(!self(L,r,p))return fail("Faqi actual LuaAI self changed");if(!lookup(L,-1,"faqiUse"))return fail("Faqi own slot lookup requires unproved dynamic getter");
 // The source indexes dummy.card before any truth test. Nil/boolean dummy
 // therefore is an ordinary source error, never a successful decline.
 if(lua_isnil(L,-1)||lua_isboolean(L,-1)){
  const int type=lua_type(L,-1);if(AIRound45Inspect::primitiveMetatable(L,type))return fail("Faqi dummy primitive has an unproved index metamethod");
  if(status)*status=AIRound45FaqiSlotStatus::SourceError;
  return fail(type==LUA_TNIL?"attempt to index a nil value (local 'dummy')":"attempt to index a boolean value (local 'dummy')");
 }
 if(!lua_istable(L,-1))return fail("Faqi source dummy is outside table/nil/boolean observation");const int d=lua_absindex(L,-1);
 if(!lookup(L,d,"card"))return fail("Faqi card field requires unproved dynamic getter");const int value=lua_absindex(L,-1);
 const auto pass=[&]()->const Card*{if(!current(q,p))return fail("Faqi source decline changed current request");if(offered)*offered=row.offered;if(targets)targets->clear();if(error)error->clear();if(status)*status=AIRound45FaqiSlotStatus::SourcePass;return nullptr;};
 if(!lua_toboolean(L,value))return pass(); // do not read dummy.to on this branch
 if(!lookup(L,d,"to"))return fail("Faqi target field requires unproved dynamic getter");const int list=lua_absindex(L,-1);
 if(!lua_toboolean(L,list))return pass(); // do not inspect truthy card userdata yet
 // A new plain source table is lawful if both actually consumed fields retain
 // independently authenticated custody. The old tableRef is NOT authority.
 if(!plain(L,d))return fail("Faqi truthy source fields require a plain observed plan");
 const Card*card=row.lease?row.lease():nullptr;if(!card)return fail("Faqi actual source Card custody expired");
 if(!matchAIRound45SlotCard(L,value,card)||!matchAIRound45NativeObjectMethods(r,L,value,"Card",{"toString"}))return fail("Faqi actual source Card identity or native serializer changed");
 QList<ServerPlayer*>actual;if(!readAIRound45SlotTargets(L,list,actual))return fail("Faqi actual source target list custody changed");
 if(!sourceReadBindings(r,L))return fail("Faqi actual source serializer/list bindings changed");
 // Only now does source consume a Card face/wire. Printed IDs cannot grant it.
 if(row.physical>=0&&!shown(q,row.physical,card))return fail("Faqi actual source physical observation expired");
 for(int id:row.materials)if(!shown(q,id))return fail("Faqi actual source material observation expired");
 if(card->getSubcards()!=row.materials)return fail("Faqi actual source material provenance changed");
 const auto roster=r.getAllPlayers(true);QStringList names;for(auto*t:actual){if(!t||!roster.contains(t))return fail("Faqi actual source target is not a live roster identity");names<<t->objectName();}
 if(!current(q,p)||row.lease()!=card)return fail("Faqi actual source read changed current request/custody");
 if(targets)*targets=names;if(offered)*offered=row.offered;if(error)error->clear();if(status)*status=AIRound45FaqiSlotStatus::Card;return card;
}
