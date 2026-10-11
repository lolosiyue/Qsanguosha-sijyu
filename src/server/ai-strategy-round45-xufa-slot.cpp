#include "ai-strategy-round45-xufa-slot.h"
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
#include "ai-strategy-round45-lua-inspect.h"
namespace {
struct Stack{lua_State*L;int n;explicit Stack(lua_State*s):L(s),n(lua_gettop(s)){}~Stack(){lua_settop(L,n);}};
struct Row{
 QPointer<ServerPlayer>owner;AIReadonlyCardLease lease;QString token;
 int tableRef=LUA_NOREF,offered=-1;quint64 producerDecision=0,producerRevision=0;
 QList<int>materials;int physical=-1;
};
class Ledger final:public QObject{
public:explicit Ledger(Room&r):QObject(&r){}lua_State*L=nullptr;quint64 generation=0;
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
void field(lua_State*L,int i,const char*k){AIRound45Inspect::field(L,i,k);}
bool self(lua_State*L,Room&r,ServerPlayer*p){return pushAIRound45ActualAISelf(r,p,L);}
bool absentNewIndex(lua_State*L,int t){Stack st(L);if(!lua_getmetatable(L,t))return true;field(L,-1,"__newindex");return lua_isnil(L,-1);}
bool plain(lua_State*L,int n){auto*t=AIRound45Inspect::table(L,n);return t&&!t->metatable;}
bool env(lua_State*L,int f){f=lua_absindex(L,f);for(int n=1;;++n){const char*k=lua_getupvalue(L,f,n);if(!k)break;bool ok=false;if(!std::strcmp(k,"_ENV")){AIRound45Inspect::globals(L);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}lua_pop(L,1);if(!ok)return false;}return true;}
bool code(Room&r,lua_State*L,int f,const char*path,int first,int last){return matchAIRound45PinnedFunction(r,L,f,path,first,last)&&env(L,f);}
bool sourceCallback(Room&r,lua_State*L,QString*why){
 auto fail=[&](const char*text){if(why)*why=QString::fromLatin1(text);return false;};
 Stack stack(L);AIRound45Inspect::globals(L);field(L,-1,"sgs");if(!lua_istable(L,-1))return fail("Xufa source sgs table unavailable");
 field(L,-1,"ai_skill_use");if(!plain(L,-1))return fail("Xufa source use registry is not plain");field(L,-1,"@@xufa");
 if(!matchAIRound45PinnedFunction(r,L,-1,"lua/ai/ol_qifu-ai.lua",1203,1215,why))return false;
 if(!env(L,-1))return fail("Xufa pinned source reader environment changed");
 if(why)why->clear();return true;
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
 field(L,sgs,"ai_skill_use");if(!plain(L,-1))return false;field(L,-1,"@@xufa");
 if(!code(r,L,-1,"lua/ai/ol_qifu-ai.lua",1203,1215))return false;
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
 if(!lua_checkstack(L,4096))return luaL_error(L,"Xufa assignment frame capacity unavailable");
 auto*w=static_cast<Write*>(lua_touserdata(L,2));
 lua_newtable(L);const int d=lua_gettop(L);
 lua_pushboolean(L,1);lua_setfield(L,d,"isDummy");
 lua_pushinteger(L,w->extra);lua_setfield(L,d,"extra_target");
 lua_newtable(L);lua_setfield(L,d,"current_targets");
 if(!pushAIRound45SlotCard(L,w->card))return luaL_error(L,"Xufa native Card wrapper unavailable");lua_setfield(L,d,"card");
 if(!pushAIRound45SlotTargets(L,w->targets))return luaL_error(L,"Xufa native target list unavailable");lua_setfield(L,d,"to");
 if(!current(*w->request,w->owner)||w->lease()!=w->card||!self(L,*w->owner->getRoom(),w->owner)||!lua_rawequal(L,1,-1))
  return luaL_error(L,"Xufa actual owner/request changed during allocation");
 lua_pop(L,1);
 // Preserve source assignment (not rawset). If GC changed the exact receiver's
 // absent-key metatable after preflight, reject before dispatching that hook.
 field(L,1,"xufaUse");const bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(!present&&!absentNewIndex(L,1))return luaL_error(L,"Xufa source __newindex changed during allocation");
 lua_pushvalue(L,d);lua_setfield(L,1,"xufaUse");
 lua_pushvalue(L,d);return 1;
}
}
bool registerAIRound45XufaSlotSources(Room&r,lua_State*L,QString*error){
 // Independently fixed source bytes, not a hash derived from the installed
 // effective callback. The common seal verifies runtime generation and compiles
 // metadata without executing any package policy. Never reseal on a reader.
 if(!registerAIRound45PinnedSource(r,L,"lua/ai/ol_qifu-ai.lua",
     QByteArray("b7dbe8d9686dc3cda25fff81a5e6aa428c560262b603f42cb3bac98764c0952f"),error))return false;
 return registerAIRound45PinnedSource(r,L,"lua/utilities.lua",
     QByteArray("b76cdc44c0fec46f1e10d918c6e0676c5e301ed92cd5b0e35da882606a7d4d3a"),error);
}
void clearAIRound45XufaSlots(Room&r){auto*l=ledger(r);if(!l)return;
 auto*rt=r.roomRuntime();if(rt&&rt->lua().rawState()&&rt->lua().generation()==l->generation){
  LuaRuntime::Binding b(rt->lua(),false);auto*L=rt->lua().state();if(L==l->L)for(const auto&row:l->rows)if(row.tableRef!=LUA_NOREF)luaL_unref(L,LUA_REGISTRYINDEX,row.tableRef);
 }delete l;
}
QString writeAIRound45XufaSlot(const AIRequest&q,ServerPlayer*p,AIReadonlyCardLease lease,const QStringList&names,const QJsonObject&fields,QString*error){
 auto fail=[&](const QString&s){if(error)*error=s;return QString();};
 if(!current(q,p)||q.kind!=AIRequest::Activate||q.reason!=CardUseStruct::CARD_USE_REASON_PLAY||p->getPhase()!=Player::Play||!p->hasSkill("xufa")||!lease)
  return fail("Xufa source assignment requires current owner Play producer");
 if(!current(q,p))return fail("Xufa skill lookup invalidated producer request");
 if(fields.size()!=3||fields.value("isDummy")!=QJsonValue(true)||!fields.value("extra_target").isDouble()
  ||fields.value("extra_target").toDouble()!=fields.value("extra_target").toInt()||fields.value("current_targets_empty")!=QJsonValue(true))return fail("Xufa full dummy field envelope unsupported");
 const Card*card=lease();if(!card)return fail("Xufa exact producer custody expired");
 const int physical=card->isVirtualCard()?-1:card->getEffectiveId();if(physical>=0&&!shown(q,physical,card))return fail("Xufa selected physical replacement undisclosed");
 const auto costs=card->getSubcards();for(int id:costs)if(!shown(q,id))return fail("Xufa selected source material undisclosed");
 Room&r=*p->getRoom();auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();
 if(!L||!lua_checkstack(L,4096))return fail("Xufa game runtime unavailable");Stack stack(L);
 if(!self(L,r,p))return fail("Xufa actual LuaAI self authentication failed");const int receiver=lua_absindex(L,-1);
 field(L,receiver,"xufaUse");const bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(!present&&!absentNewIndex(L,receiver))return fail("Xufa source __newindex is not direct assignment");
 Write w;w.request=&q;w.owner=p;w.lease=lease;w.card=card;w.extra=fields.value("extra_target").toInt();
 const auto roster=r.getAllPlayers(true);for(const auto&name:names){ServerPlayer*target=nullptr;for(auto*t:roster)if(t&&t->objectName()==name){target=t;break;}if(!target)return fail("Xufa source target outside roster");w.targets<<target;}
 lua_pushcfunction(L,assign);lua_pushvalue(L,receiver);lua_pushlightuserdata(L,&w);
 if(LuaRuntime::protectedCall(L,2,1,0)!=LUA_OK)return fail("Xufa native source assignment did not complete");
 if(!current(q,p)||!lease())return fail("Xufa assignment changed current request or custody");
 field(L,receiver,"xufaUse");const bool exact=lua_rawequal(L,-1,-2);lua_pop(L,1);if(!exact||!plain(L,-1))return fail("Xufa source assignment was replaced");
 auto*l=ledger(r,true);if(l->L&&l->generation!=rt->lua().generation())return fail("Xufa source runtime generation changed");l->L=L;l->generation=rt->lua().generation();
 Row row;row.owner=p;row.lease=std::move(lease);row.token="xufa-actual-slot:"+QUuid::createUuid().toString(QUuid::WithoutBraces);row.materials=costs;row.physical=physical;row.producerDecision=q.decisionId;row.producerRevision=q.stateRevision;
 lua_pushvalue(L,-1);row.tableRef=luaL_ref(L,LUA_REGISTRYINDEX);
 auto old=l->rows.find(p);if(old!=l->rows.end()&&old->tableRef!=LUA_NOREF)luaL_unref(L,LUA_REGISTRYINDEX,old->tableRef);
 l->rows[p]=row; // Retain reached assignment even if the later global choice write / Parse errors.
 if(!current(q,p)||!row.lease())return fail("Xufa source publication changed request or custody");
 if(error)error->clear();return row.token;
}
AIRound45XufaSlotRead readAIRound45XufaSlot(const AIRequest&q,ServerPlayer*p){
 AIRound45XufaSlotRead out;auto fail=[&](const QString&s){out.error=s;return out;};
 if(!current(q,p)||q.kind!=AIRequest::UseCard||q.pattern!="@@xufa"||!p->hasSkill("xufa"))return fail("Xufa slot read requires exact current owner response");
 if(!current(q,p))return fail("Xufa skill lookup invalidated reader request");
 auto finish=[&](){if(!current(q,p)){out=AIRound45XufaSlotRead();out.error="Xufa source read invalidated current request";}return out;};
 Room&r=*p->getRoom();auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();
 if(!L||!lua_checkstack(L,4096))return fail("Xufa actual source runtime unavailable");Stack stack(L);
 QString readerWhy;
 if(!sourceCallback(r,L,&readerWhy))return fail("Xufa source reader: "+readerWhy);
 if(!self(L,r,p))return fail("Xufa actual owner self authentication changed");
 if(!lookup(L,-1,"xufaUse"))return fail("Xufa actual slot lookup invokes unproved getter");
 // Exact own source short circuits, without reading a hidden Card or targets.
 if(lua_isnil(L,-1)){if(lua_getmetatable(L,-1))return fail("Xufa nil indexing metatable changed");out.kind=AIRound45XufaSlotRead::NilPlan;return finish();}
 if(lua_isboolean(L,-1)&&!lua_toboolean(L,-1)){if(lua_getmetatable(L,-1))return fail("Xufa boolean indexing metatable changed");out.kind=AIRound45XufaSlotRead::FalsePlan;return finish();}
 if(!lua_istable(L,-1))return fail("Xufa actual plan is outside source table domain");const int d=lua_absindex(L,-1);
 if(!lookup(L,d,"card"))return fail("Xufa card lookup invokes unproved getter");
 if(lua_isnil(L,-1)||(lua_isboolean(L,-1)&&!lua_toboolean(L,-1))){out.kind=AIRound45XufaSlotRead::EmptyCard;return finish();}
 auto*l=ledger(r);if(!l||l->L!=L||l->generation!=rt->lua().generation())return fail("Xufa actual selected Card has no retained observation");
 const auto it=l->rows.constFind(p);if(it==l->rows.cend()||it->owner!=p)return fail("Xufa current owner has no retained producer");const Row row=*it;
 const Card*card=row.lease?row.lease():nullptr;if(!card)return fail("Xufa actual source Card lifetime expired");
 if(!matchAIRound45SlotCard(L,-1,card)||!matchAIRound45NativeObjectMethods(r,L,-1,"Card",{"canRecast","toString"}))return fail("Xufa source Card changed beyond retained identity");
 if(row.physical>=0&&!shown(q,row.physical,card))return fail("Xufa physical source is no longer lawfully disclosed");
 for(int id:row.materials)if(!shown(q,id))return fail("Xufa material observation expired");
 if(card->getSubcards()!=row.materials)return fail("Xufa retained material provenance changed");lua_pop(L,1);
 if(!sourceReadBindings(r,L))return fail("Xufa reached reader dependencies changed");
 if(!lookup(L,d,"to"))return fail("Xufa target lookup invokes unproved getter");
 if(lua_isnil(L,-1)||(lua_isboolean(L,-1)&&!lua_toboolean(L,-1))){out.kind=AIRound45XufaSlotRead::CardPlan;out.targetsMissing=true;out.targetsFalse=lua_isboolean(L,-1);out.lease=row.lease;return finish();}
 QList<ServerPlayer*>actual;if(!readAIRound45SlotTargets(L,-1,actual))return fail("Xufa actual source target list custody changed");
 const auto roster=r.getAllPlayers(true);for(auto*t:actual){if(!t||!roster.contains(t))return fail("Xufa source target left roster");out.targets<<t->objectName();}
 if(!current(q,p)||row.lease()!=card)return fail("Xufa source read changed request or custody");
 out.kind=AIRound45XufaSlotRead::CardPlan;out.lease=row.lease;return finish();
}
std::unique_ptr<Card> constructAIRound45XufaPolicy(const AIRequest&q,ServerPlayer*p,const QString&name,const QList<int>&costs,QString*error){
 auto fail=[&](const char*s)->std::unique_ptr<Card>{if(error)*error=s;return {};};
 if(!current(q,p)||q.kind!=AIRequest::Activate||q.reason!=CardUseStruct::CARD_USE_REASON_PLAY||p->getPhase()!=Player::Play||!p->hasSkill("xufa")||costs.size()>1)
  return fail("Xufa dummy constructor requires exact owner Play source");
 if(!current(q,p))return fail("Xufa skill lookup invalidated constructor request");
 QList<int>he=p->handCards();for(auto*c:p->getEquips())he<<c->getEffectiveId();auto names=he;names<<p->getPile("xufa");bool found=false;
 for(int id:names){if(!shown(q,id))continue;const Card*c=Sanguosha->getCard(id);if(c&&c->isNDTrick()&&c->objectName()==name){found=true;break;}}
 if(!found)return fail("Xufa dummy name is not an actual disclosed source NDTrick");
 for(int id:costs)if(!he.contains(id)||!shown(q,id))return fail("Xufa dummy material is not exact disclosed owner HE");
 std::unique_ptr<Card>card(Sanguosha->cloneCard(name,Card::NoSuit,0));
 if(!card)return fail("Xufa source dummy clone is absent");card->setSkillName("xufa");for(int id:costs)card->addSubcard(id);
 if(!current(q,p))return fail("Xufa source clone invalidated constructor request");
 if(error)error->clear();return card;
}
