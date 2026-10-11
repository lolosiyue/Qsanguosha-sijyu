#include "ai-strategy-round53-zhuning.h"
#include "ai-strategy-round45-slot-bindings.h"
#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-round45-inovation-providers.h"
#include "ai-strategy-round45-native-seal.h"
#include "ai-strategy-round45-code-seal.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "engine.h"
#include "serverplayer.h"
#include "card.h"
#include "wrapped-card.h"
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
struct Row {QPointer<ServerPlayer>owner;AIReadonlyCardLease lease;int observedWrappedId=-1;};
class Ledger final:public QObject {public:explicit Ledger(Room&r):QObject(&r){} quint64 generation=0;QHash<const ServerPlayer*,QHash<QString,Row>>rows;};
Ledger*ledger(Room&r,bool create=false){for(auto*c:r.children())if(auto*l=dynamic_cast<Ledger*>(c))return l;return create?new Ledger(r):nullptr;}
bool current(const AIRequest&q,ServerPlayer*p){return p&&p->getRoom()&&q.decisionId>0
 &&!p->getRoom()->roomRuntime()->isClosing()&&q.viewerObjectName==p->objectName()
 &&q.worldView.self.objectName==p->objectName()&&aiRound44RangeRequestCurrent(*p->getRoom(),q,p)
 &&Sanguosha&&Sanguosha->currentRoom()==p->getRoom();}
bool scope(const AIRequest&q,ServerPlayer*,bool write){
 if(write)return q.kind==AIRequest::AmazingGrace&&q.choiceOptions.reason=="zhuning"
  &&q.choiceOptions.question=="askForAG";
 return q.kind==AIRequest::UseCard&&q.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE
  &&q.pattern=="@@zhuning"&&q.handlingMethod==Card::MethodUse;
}
const Card*equippedMaterial(const AIRequest&q,ServerPlayer*p,int id){
 Room&r=*p->getRoom();const auto*owner=r.getCardOwner(id);
 if(!owner||r.getCardPlace(id)!=Player::PlaceEquip)return nullptr;
 const Card*outer=Sanguosha->getCard(id);if(!outer)return nullptr;
 const Card*inner=nullptr;for(const Card*c:owner->getEquips())if(c->getEffectiveId()==id)inner=c;
 if(!inner||outer->getRealCard()!=inner)return nullptr;
 auto players=q.worldView.players;players.prepend(q.worldView.self);
 for(const auto&v:players)if(v.objectName==owner->objectName())for(const auto&e:v.equips){
  if(e.effectiveId!=id||!e.nativeIdentity||!globalCardLifetimeManager().isLive(e.nativeIdentity)
   ||e.nativeIdentity->address!=inner)continue;
  if(owner==p)return outer; // Current owner knows its wrapped equipment face.
  // Foreign public inner equipment alone does not disclose a private transform.
  if(e.cardId==outer->getId()&&e.objectName==outer->objectName()&&e.className==outer->getClassName()
   &&e.suit==int(outer->getSuit())&&e.number==outer->getNumber()&&e.skillName==outer->getSkillName(false)
   &&e.subcardIds==outer->getSubcards()&&e.virtualCard==outer->isVirtualCard())return outer;
 }
 return nullptr;
}
bool shown(const AIRequest&q,ServerPlayer*p,int id,const Card*exact=nullptr){
 // IDs in printed notification DTOs are not evidence of current wrapped face.
 // Authenticate the actually projected object BEFORE consulting its fields.
 if(!exact){
  if(p&&equippedMaterial(q,p,id))return true;
  exact=Sanguosha->getCard(id);
 }if(!exact)return false;
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
 field(L,sgs,"ai_skill_use");if(!plain(L,-1))return false;
 field(L,-1,"@@zhuning");
 if(!code(r,L,-1,"lua/ai/tenyear_hc-ai.lua",7973,7981))return false;
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
// Only this native thunk allocates game-VM values. LuaAI policy is never called.
struct Write {const AIRequest*q;ServerPlayer*owner;QByteArray key;const Card*card=nullptr;
 AIReadonlyCardLease lease;QList<ServerPlayer*>targets;int extra=0;QString cardState;};
int assign(lua_State*L){
 if(!lua_checkstack(L,4096))return luaL_error(L,"saved plan assignment stack unavailable");
 auto*w=static_cast<Write*>(lua_touserdata(L,2));
 lua_newtable(L);const int d=lua_absindex(L,-1);
 lua_pushboolean(L,1);lua_setfield(L,d,"isDummy");lua_pushinteger(L,w->extra);lua_setfield(L,d,"extra_target");
 lua_newtable(L);lua_setfield(L,d,"current_targets");
 if(w->cardState=="card"){if(!pushAIRound45SlotCard(L,w->card))return luaL_error(L,"saved card wrapper unavailable");}
 else if(w->cardState=="false")lua_pushboolean(L,0);else lua_pushnil(L);
 lua_setfield(L,d,"card");
 if(!pushAIRound45SlotTargets(L,w->targets))return luaL_error(L,"saved target list unavailable");lua_setfield(L,d,"to");
 const int value=lua_absindex(L,-1);
 if(!current(*w->q,w->owner)||(w->cardState=="card"&&w->lease()!=w->card)
  ||!self(L,*w->owner->getRoom(),w->owner)||!lua_rawequal(L,1,-1))return luaL_error(L,"saved plan actual receiver changed during allocation");lua_pop(L,1);
 field(L,1,w->key.constData());const bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(!present&&!absentNewIndex(L,1))return luaL_error(L,"saved plan assignment has dynamic __newindex");
 lua_pushvalue(L,value);lua_setfield(L,1,w->key.constData());
 return 0;
}
bool target(Room&r,lua_State*L,int index,ServerPlayer*&out){
 for(auto*p:r.getAllPlayers(true))if(matchAIRound45SlotPlayer(L,index,p)){
  if(!matchAIRound45NativeObjectMethods(r,L,index,"ServerPlayer",{"objectName"}))return false;out=p;return true;
 }return false;
}
// Match the R53 target-slot reader: genuine legacy SPlayerList and the tracked
// native lease list both remain live source objects, never copied descriptors.
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
  ServerPlayer*p=nullptr;if(!target(r,L,-1,p))return fail("saved native target identity or method changed");
  w->out->append(p);lua_pop(L,1);
 }
 if(!current(*w->q,w->owner))return fail("saved target iteration changed request");
 return 0;
}
}
bool registerAIRound53ZhuningSources(Room&r,lua_State*L,QString*error){
 return registerAIRound45PinnedSource(r,L,"lua/ai/tenyear_hc-ai.lua",QByteArray("94a38f49a220eeb0f0816dc9737c868027cc32ebba6001edfab1c304364799f5"),error)
  &&registerAIRound45PinnedSource(r,L,"lua/utilities.lua",QByteArray("b76cdc44c0fec46f1e10d918c6e0676c5e301ed92cd5b0e35da882606a7d4d3a"),error);
}
void clearAIRound53ZhuningSlots(Room&r){if(auto*l=ledger(r))delete l;}
QJsonObject queryAIRound53ZhuningSlot(const AIRequest&q,ServerPlayer*p,const QString&op,const QJsonObject&payload,AIReadonlyCardLease lease){
 const QString slot=QStringLiteral("zhuning_d");
 auto fail=[](const QString&s){return QJsonObject{{"error",s},{"key","zhuning.saved_plan"}};};
 if(op!="write"&&op!="read")return fail("unknown saved-plan operation");const bool writing=op=="write";
 if(!current(q,p)||!scope(q,p,writing))return fail("saved plan requires exact current owner source inquiry");
 Room&r=*p->getRoom();auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();
 if(!L||!lua_checkstack(L,4096))return fail("saved plan game runtime unavailable");Stack stack(L);
 if(!self(L,r,p))return fail("saved plan actual LuaAI self unavailable");const int receiver=lua_absindex(L,-1);
 const QByteArray key=slot.toUtf8();
 if(writing){
  Write w;w.q=&q;w.owner=p;w.key=key;
  if(payload.size()!=4||!payload.value("selector").isObject()||!payload.value("targets").isArray()||!payload.value("source_fields").isObject())return fail("saved source plan shape malformed");
  const auto f=payload.value("source_fields").toObject();
  if(f.size()!=3||f.value("isDummy")!=QJsonValue(true)||f.value("current_targets_empty")!=QJsonValue(true)
   ||!f.value("extra_target").isDouble()||f.value("extra_target").toDouble()!=f.value("extra_target").toInt())return fail("saved dummy fields malformed");
   w.extra=f.value("extra_target").toInt();w.cardState=payload.value("card_state").toString();
   if(w.cardState!="card"&&w.cardState!="nil"&&w.cardState!="false")return fail("saved source card state malformed");
   if(w.cardState=="card"){
    w.lease=std::move(lease);w.card=w.lease?w.lease():nullptr;if(!w.card)return fail("saved source card custody expired");
    if(!w.card->isVirtualCard()&&!shown(q,p,w.card->getEffectiveId(),w.card))return fail("saved physical source card not currently disclosed");
    for(int id:w.card->getSubcards())if(!shown(q,p,id))return fail("saved source material not currently disclosed");
   }else if(lease||!payload.value("selector").toObject().isEmpty())return fail("absent source card cannot carry an authority selector");
   if(payload.value("targets").toArray().size()>128)return fail("saved target budget exceeded");
   for(const auto&n:payload.value("targets").toArray()){
    if(!n.isString())return fail("saved target is not a name");ServerPlayer*found=nullptr;
    for(auto*t:r.getAllPlayers(true))if(t->objectName()==n.toString())found=t;
    if(!found)return fail("saved target outside roster");w.targets<<found;
   }
  // Stack function and arguments do not allocate a captured C closure.
  lua_pushcfunction(L,assign);lua_pushvalue(L,receiver);lua_pushlightuserdata(L,&w);
  if(LuaRuntime::protectedCall(L,2,0,0)!=LUA_OK)return fail("exact native saved-plan assignment failed");
  // Reached assignment is not rolled back on errors.
  if(!current(q,p)||(w.cardState=="card"&&w.lease()!=w.card))return fail("saved-plan assignment changed request or custody");
  {auto*l=ledger(r,true);if(l->generation&&l->generation!=rt->lua().generation())return fail("saved plan game generation changed");
   l->generation=rt->lua().generation();l->rows[p][slot]=Row{p,std::move(w.lease),-1};}
  return{{"written",true}};
 }
 if(!payload.isEmpty())return fail("saved read accepts no descriptor or prior authority");
 if(!lookup(L,receiver,key.constData()))return fail("saved slot requires unproved dynamic getter");const int saved=lua_absindex(L,-1);
 if(lua_isnil(L,saved)||(lua_isboolean(L,saved)&&!lua_toboolean(L,saved)))return{{"status","absent"}};
 if(!sourceReadBindings(r,L))return fail("saved consumer serializer/iterator bindings changed");
 if(lua_isboolean(L,saved)){
  if(AIRound45Inspect::primitiveMetatable(L,lua_type(L,saved)))return fail("saved primitive index metamethod unproved");
  return{{"source_error",QString("attempt to index a %1 value (saved source plan)").arg(lua_isnil(L,saved)?"nil":"boolean")}};
 }
 if(!lua_istable(L,saved)||!lookup(L,saved,"to"))return fail("saved targets require unproved getter");const int list=lua_absindex(L,-1);
 if(lua_isnil(L,list)||lua_isboolean(L,list)){
  if(AIRound45Inspect::primitiveMetatable(L,lua_type(L,list)))return fail("saved primitive target iteration has unproved metatable");
  return{{"source_error","bad argument #1 to 'ipairs' (table expected)"}};
 }
 // Source iterates targets before calling card:toString. Preserve that order.
 QList<ServerPlayer*>targets;
 if(lua_istable(L,list)){
  if(!plain(L,list))return fail("saved plain target list has unproved metatable");
  for(int n=1;n<=129;++n){lua_rawgeti(L,list,n);if(lua_isnil(L,-1)){lua_pop(L,1);break;}
   if(n==129)return fail("saved target budget exceeded");ServerPlayer*t=nullptr;
   if(!target(r,L,-1,t))return fail("saved table target identity or method changed");targets<<t;lua_pop(L,1);
  }
 }else{
  ReadTargets read{&q,p,&targets};
  lua_pushcfunction(L,readTargets);lua_pushvalue(L,list);lua_pushlightuserdata(L,&read);
  if(LuaRuntime::protectedCall(L,2,0,0)!=LUA_OK)return fail(read.error?read.error:"saved native target list read failed");
 }
 // Only after the complete ordered target loop does the source read card.
 if(!lookup(L,saved,"card"))return fail("saved card field unavailable");const int cardIndex=lua_absindex(L,-1);
 if(lua_isnil(L,cardIndex)||lua_isboolean(L,cardIndex)){
  if(AIRound45Inspect::primitiveMetatable(L,lua_type(L,cardIndex)))return fail("saved card primitive index metamethod unproved");
  return{{"source_error",QString("attempt to index a %1 value (saved source card)").arg(lua_isnil(L,cardIndex)?"nil":"boolean")}};
 }
 auto*l=ledger(r);if(!l||l->generation!=rt->lua().generation())return fail("saved actual Card custody unavailable");
 const auto row=l->rows.value(p).value(slot);const Card*c=row.lease?row.lease():nullptr;
 if(!c||row.owner!=p||!matchAIRound45SlotCard(L,cardIndex,c)
  ||!matchAIRound45NativeObjectMethods(r,L,cardIndex,"Card",{"toString"}))return fail("saved actual Card identity or native serializer changed");
 if(!c->isVirtualCard()&&!shown(q,p,c->getEffectiveId(),c))return fail("saved physical face no longer disclosed");
 for(int id:c->getSubcards())if(!shown(q,p,id))return fail("saved current material printing not lawfully observable");
 QJsonArray names;const auto roster=r.getAllPlayers(true);for(auto*t:targets){if(!t||!roster.contains(t))return fail("saved target left roster");names.append(t->objectName());}
 const QString wire=c->toString();
 if(!current(q,p)||row.lease()!=c)return fail("saved observation changed request or custody");
 return{{"status","plan"},{"wire",wire},{"targets",names},{"evaluation_only",true}};
}

bool aiRound53ZhuningConstruction(const AIRequest&q,Room&r,const ServerPlayer*p,const QJsonObject&s){
 if(!current(q,const_cast<ServerPlayer*>(p))||!scope(q,const_cast<ServerPlayer*>(p),true))return false;
 if(s.size()!=6||s.value("source_key")!="zhuning"||s.value("skill")!="_zhuning"
  ||s.value("suit")!=int(Card::NoSuit)||s.value("number")!=0||!s.value("name").isString()
  ||!s.value("subcards").isArray()||!s.value("subcards").toArray().isEmpty())return false;
 // The current AG question discloses the offered wrapped face. Read only
 // that exact current object/name; its future native factory uses EngineCard
 // and independently rejects any source/native printing discrepancy.
 for(const auto&v:q.choiceOptions.cards)if(q.choiceOptions.cardIds.contains(v.effectiveId)){
  const Card*c=r.getCard(v.effectiveId);
  if(c&&c->objectName()==s.value("name").toString()&&shown(q,const_cast<ServerPlayer*>(p),v.effectiveId,c))return true;
 }
 return false;
}
