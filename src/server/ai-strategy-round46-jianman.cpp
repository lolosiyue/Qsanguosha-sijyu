#include "ai-strategy-round46-jianman.h"
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
 QString offered;quint64 producerDecision=0,producerRevision=0;
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
 field(L,-1,"ai_skill_use");if(!plain(L,-1))return false;field(L,-1,"@@jianman");
 return code(r,L,-1,"lua/ai/ol_sp-ai.lua",1881,1890);
}
bool sourceProducer(Room&r,lua_State*L){
 Stack stack(L);AIRound45Inspect::globals(L);field(L,-1,"sgs");if(!lua_istable(L,-1))return false;
 field(L,-1,"ai_skill_choice");if(!plain(L,-1))return false;field(L,-1,"jianman");
 return code(r,L,-1,"lua/ai/ol_sp-ai.lua",1866,1879);
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
 field(L,sgs,"ai_skill_use");if(!plain(L,-1))return false;field(L,-1,"@@jianman");
 if(!code(r,L,-1,"lua/ai/ol_sp-ai.lua",1881,1890))return false;
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
 if(!lua_checkstack(L,4096))return luaL_error(L,"Jianman assignment inspection stack unavailable");
 auto*w=static_cast<Write*>(lua_touserdata(L,2));
 lua_newtable(L);const int d=lua_gettop(L);
 lua_pushboolean(L,1);lua_setfield(L,d,"isDummy");
 lua_pushinteger(L,w->extra);lua_setfield(L,d,"extra_target");
 lua_newtable(L);lua_setfield(L,d,"current_targets");
 if(!pushAIRound45SlotCard(L,w->card))return luaL_error(L,"Jianman native Card wrapper unavailable");lua_setfield(L,d,"card");
 if(!pushAIRound45SlotTargets(L,w->targets))return luaL_error(L,"Jianman native target list unavailable");lua_setfield(L,d,"to");
 if(!current(*w->request,w->owner)||w->lease()!=w->card||!self(L,*w->owner->getRoom(),w->owner)||!lua_rawequal(L,1,-1))
  return luaL_error(L,"Jianman actual owner/request changed during allocation");
 lua_pop(L,1);
 // Preserve source assignment (not rawset). If GC changed the exact receiver's
 // absent-key metatable after preflight, reject before dispatching that hook.
 field(L,1,"jianmanUse");const bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(!present&&!absentNewIndex(L,1))return luaL_error(L,"Jianman source __newindex changed during allocation");
 lua_pushvalue(L,d);lua_setfield(L,1,"jianmanUse");
 lua_pushvalue(L,d);return 1;
}
}
void clearAIRound46JianmanSlots(Room&r){if(auto*l=ledger(r))delete l;}
bool hasAIRound46JianmanSlot(Room&r,ServerPlayer*p,const QString&token){auto*l=ledger(r);if(!l||!p||token.isEmpty())return false;auto i=l->rows.constFind(p);return i!=l->rows.cend()&&i->owner==p&&i->token==token;}
QString writeAIRound46JianmanSlot(const AIRequest&q,ServerPlayer*p,const QString&offered,AIReadonlyCardLease lease,const QStringList&names,const QJsonObject&fields,QString*error){
 auto fail=[&](const QString&s){if(error)*error=s;return QString();};
 if(!current(q,p)||q.kind!=AIRequest::Choice||q.choiceOptions.reason!="jianman"||!q.choiceOptions.choices.contains(offered)||!lease)return fail("Jianman source assignment requires actual current offered choice");
 if(fields.size()!=3||fields.value("isDummy")!=QJsonValue(true)||!fields.value("extra_target").isDouble()
  ||fields.value("extra_target").toDouble()!=fields.value("extra_target").toInt()||fields.value("current_targets_empty")!=QJsonValue(true))return fail("Jianman full dummy field envelope unsupported");
 const Card*card=lease();if(!card)return fail("Jianman exact producer custody expired");
 const int physical=card->isVirtualCard()?-1:card->getEffectiveId();if(physical>=0&&!shown(q,physical,card))return fail("Jianman source physical card undisclosed");
 const auto costs=card->getSubcards();for(int id:costs)if(!shown(q,id))return fail("Jianman source material undisclosed");
 Room&r=*p->getRoom();auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();
 if(!L||!lua_checkstack(L,4096))return fail("Jianman game runtime unavailable");Stack stack(L);
 if(!sourceProducer(r,L))return fail("Jianman actual producer callback changed");
 if(!self(L,r,p))return fail("Jianman actual LuaAI self authentication failed");const int receiver=lua_absindex(L,-1);
 field(L,receiver,"jianmanUse");const bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(!present&&!absentNewIndex(L,receiver))return fail("Jianman source __newindex is not the direct assignment domain");
 Write w;w.request=&q;w.owner=p;w.lease=lease;w.card=card;w.extra=fields.value("extra_target").toInt();
 const auto roster=r.getAllPlayers(true);for(const auto&name:names){ServerPlayer*target=nullptr;for(auto*t:roster)if(t&&t->objectName()==name){target=t;break;}if(!target)return fail("Jianman source target is outside current roster");w.targets<<target;}
 lua_pushcfunction(L,assign);lua_pushvalue(L,receiver);lua_pushlightuserdata(L,&w);
 if(LuaRuntime::protectedCall(L,2,1,0)!=LUA_OK)return fail("Jianman native source assignment failed before completion");
 if(!current(q,p)||!lease())return fail("Jianman source assignment changed current request or custody");
 field(L,receiver,"jianmanUse");const bool exact=lua_rawequal(L,-1,-2);lua_pop(L,1);if(!exact||!plain(L,-1))return fail("Jianman assignment no longer names its actual source table");
 auto*l=ledger(r,true);if(l->L&&l->generation!=rt->lua().generation())return fail("Jianman synchronized runtime generation changed");l->L=L;l->generation=rt->lua().generation();
 Row row;row.owner=p;row.lease=std::move(lease);row.token="jianman-actual-slot:"+QUuid::createUuid().toString(QUuid::WithoutBraces);row.offered=offered;row.materials=costs;row.physical=physical;row.producerDecision=q.decisionId;row.producerRevision=q.stateRevision;
 // Actual self owns the table/list wrappers. No extra Lua registry allocation
 // or old table identity is needed; the native lease retains only Card custody.
 l->rows[p]=row;
 if(!current(q,p)||!row.lease())return fail("Jianman source assignment publication changed current request/custody");
 if(error)error->clear();return row.token;
}
const Card*readAIRound46JianmanSlot(const AIRequest&q,ServerPlayer*p,const QString&token,QStringList*targets,QString*offered,QString*error,AIRound46JianmanSlotStatus*status){
 if(status)*status=AIRound46JianmanSlotStatus::Unknown;
 auto fail=[&](const QString&s)->const Card*{if(error)*error=s;return nullptr;};
 if(!current(q,p)||q.kind!=AIRequest::UseCard||q.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE||q.pattern!="@@jianman"||q.handlingMethod!=Card::MethodUse)return fail("Jianman actual source read requires current response-use question");Room&r=*p->getRoom();auto*l=ledger(r);auto*rt=r.roomRuntime();
 const bool retained=l&&l->generation==rt->lua().generation()&&hasAIRound46JianmanSlot(r,p,token);
 const Row row=retained?l->rows.value(p):Row();
 LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();if(!L||(retained&&L!=l->L)||!lua_checkstack(L,4096))return fail("Jianman actual source runtime unavailable");Stack stack(L);
 if(!sourceCallback(r,L))return fail("Jianman actual source callback changed");
 if(!self(L,r,p))return fail("Jianman actual LuaAI self changed");if(!lookup(L,-1,"jianmanUse"))return fail("Jianman own slot lookup requires unproved dynamic getter");
 // Fixed Jianman source tests d before d.card: nil/false decline without indexing.
 if(!lua_toboolean(L,-1)){if(status)*status=AIRound46JianmanSlotStatus::SourcePass;if(error)error->clear();return nullptr;}
 if(lua_isboolean(L,-1)){
  if(AIRound45Inspect::primitiveMetatable(L,LUA_TBOOLEAN))return fail("Jianman true plan index metamethod is unproved");
  if(status)*status=AIRound46JianmanSlotStatus::SourceError;return fail("attempt to index a boolean value (local 'd')");
 }
 if(!lua_istable(L,-1))return fail("Jianman source dummy is outside table/nil/boolean observation");const int d=lua_absindex(L,-1);
 if(!lookup(L,d,"card"))return fail("Jianman card field requires unproved dynamic getter");const int value=lua_absindex(L,-1);
 const auto pass=[&]()->const Card*{if(!current(q,p))return fail("Jianman source decline changed current request");if(offered)*offered=row.offered;if(targets)targets->clear();if(error)error->clear();if(status)*status=AIRound46JianmanSlotStatus::SourcePass;return nullptr;};
 if(!lua_toboolean(L,value))return pass(); // do not read dummy.to on this branch
 if(!lookup(L,d,"to"))return fail("Jianman target field requires unproved dynamic getter");const int list=lua_absindex(L,-1);
 if(!sourceReadBindings(r,L))return fail("Jianman actual source serializer/list bindings changed");
 // Source has no `and d.to` guard. sgs.list(nil/false) fails at qlist.length.
 if(!lua_toboolean(L,list)){
  if(AIRound45Inspect::primitiveMetatable(L,lua_type(L,list)))return fail("Jianman target primitive metatable is unproved");
  if(status)*status=AIRound46JianmanSlotStatus::SourceError;return fail("sgs.qlist: received nil list");
 }
 if(!retained)return fail("Jianman actual truthy plan has no retained producer custody");
 // A new plain source table is lawful if both actually consumed fields retain
 // independently authenticated custody. An old table identity is NOT authority.
 if(!plain(L,d))return fail("Jianman truthy source fields require a plain observed plan");
 const Card*card=row.lease?row.lease():nullptr;if(!card)return fail("Jianman actual source Card custody expired");
 if(!matchAIRound45SlotCard(L,value,card)||!matchAIRound45NativeObjectMethods(r,L,value,"Card",{"toString"}))return fail("Jianman actual source Card identity or native serializer changed");
 QList<ServerPlayer*>actual;if(!readAIRound45SlotTargets(L,list,actual))return fail("Jianman actual source target list custody changed");
 if(!sourceReadBindings(r,L))return fail("Jianman actual source serializer/list bindings changed");
 // Only now does source consume a Card face/wire. Printed IDs cannot grant it.
 if(row.physical>=0&&!shown(q,row.physical,card))return fail("Jianman actual source physical observation expired");
 for(int id:row.materials)if(!shown(q,id))return fail("Jianman actual source material observation expired");
 if(card->getSubcards()!=row.materials)return fail("Jianman actual source material provenance changed");
 const auto roster=r.getAllPlayers(true);QStringList names;for(auto*t:actual){if(!t||!roster.contains(t))return fail("Jianman actual source target is not a live roster identity");names<<t->objectName();}
 if(!current(q,p)||row.lease()!=card)return fail("Jianman actual source read changed current request/custody");
 if(targets)*targets=names;if(offered)*offered=row.offered;if(error)error->clear();if(status)*status=AIRound46JianmanSlotStatus::Card;return card;
}

bool registerAIRound46JianmanSources(Room&r,lua_State*L,QString*error){
 return registerAIRound45PinnedSource(r,L,"lua/ai/ol_sp-ai.lua",QByteArray("d8cc0c6d06b58c3d7306e5d742e44247e88b2050f94df886600f840e68b4205a"),error)
 &&registerAIRound45PinnedSource(r,L,"lua/utilities.lua",QByteArray("b76cdc44c0fec46f1e10d918c6e0676c5e301ed92cd5b0e35da882606a7d4d3a"),error);
}
std::unique_ptr<Card> constructAIRound46Jianman(const AIRequest&q,ServerPlayer*p,const QString&name,QString*error){
 auto fail=[&](const QString&s)->std::unique_ptr<Card>{if(error)*error=s;return {};};
 if(!current(q,p)||q.kind!=AIRequest::Choice||q.choiceOptions.reason!="jianman"||!q.choiceOptions.choices.contains(name))return fail("Jianman constructor requires an actual offered declaration");
 // Native Jianman builds choices only from the two recorded BasicCard names.
 // No history/tag read is needed: the current native choice is the disclosure.
 std::unique_ptr<Card>c(Sanguosha->cloneCard(name,Card::NoSuit,0));
 if(!c){if(error)error->clear();return {};}
 c->setSkillName("_jianman");if(error)error->clear();return c;
}
