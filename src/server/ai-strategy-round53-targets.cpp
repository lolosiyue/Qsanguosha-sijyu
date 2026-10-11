#include "ai-strategy-round56-projection.h"
#include "ai-strategy-round53-ainoto.h"
#include "ai-strategy-round53-targets.h"
#include "ai-strategy-round45-slot-bindings.h"
#include "ai-strategy-round45-inovation-providers.h"
#include "ai-strategy-round45-native-seal.h"
#include "ai-strategy-round44-certificate.h"
#include "ai.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "lua-runtime.h"
#include <QJsonArray>
#include <QSet>
#include <cmath>
// Must follow Qt headers: the private Lua implementation defines cast macros.
#include "ai-strategy-round45-lua-inspect.h"
namespace {
QJsonObject unknown(const char*s){return{{"known",false},{"reason",QString::fromLatin1(s)}};}
bool current(Room&r,const AIRequest&q,ServerPlayer*p){return p&&r.getAllPlayers(true).contains(p)&&p->getRoom()==&r&&r.roomRuntime()&&!r.roomRuntime()->isClosing()&&q.decisionId&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&aiRound44RangeRequestCurrent(r,q,p)&&Sanguosha->currentRoom()==&r;}
bool question(const AIRequest&q,AIRequest::DecisionKind k,const char*reason,const char*method){return q.kind==k&&q.choiceOptions.reason==reason&&q.choiceOptions.question==method;}
bool playerSlot(const QString&key){return key=="luochong_to"||key=="diting_to"||key=="xiaoxinf_to"||key=="tj_to"||key=="shouxingTo"||key=="geassTarget"||key=="tychendeTo";}
bool scope(const AIRequest&q,ServerPlayer*p,const QString&key,const QString&op){
 if(key=="tychendeTo")return(op=="write"||op=="truth"||op=="read")&&q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY&&q.pattern.isEmpty()&&q.handlingMethod==Card::MethodUse&&p->getPhase()==Player::Play&&p->getRoom()->getCurrent()==p&&p->hasSkill("tychende");
 if(key=="geassTarget"){
  if(op=="write")return question(q,AIRequest::AmazingGrace,"geass","askForAG")||question(q,AIRequest::AmazingGrace,"xinshengll","askForAG");
  return (op=="read"||op=="truth")&&q.kind==AIRequest::UseCard&&q.pattern=="@@geass"&&q.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&q.handlingMethod==Card::MethodUse;
 }
 if(key=="tj_to"||key=="shouxingTo"){
  const QString skill=key=="tj_to"?QStringLiteral("tianjiang"):QStringLiteral("shouxing");
  return (op=="write"||op=="read"||(op=="truth"&&key=="shouxingTo"))&&q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY&&q.pattern.isEmpty()&&q.handlingMethod==Card::MethodUse&&p->getPhase()==Player::Play&&p->getRoom()->getCurrent()==p&&p->hasSkill(skill);
 }
 if(key=="fuhui_to"||key=="jg_to"||key=="mx_to"||key=="shouli_to"){
  const QString skill=key=="fuhui_to"?QStringLiteral("fuhui"):(key=="jg_to"?QStringLiteral("juguan"):(key=="mx_to"?QStringLiteral("miaoxian"):QStringLiteral("shouli")));
  return (op=="write"||op=="read"||(op=="truth"&&key=="shouli_to"))&&q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY&&q.pattern.isEmpty()&&q.handlingMethod==Card::MethodUse&&p->getPhase()==Player::Play&&p->getRoom()->getCurrent()==p&&p->hasSkill(skill);
 }
 if(key=="xiaoxinf_to")return op=="write"?question(q,AIRequest::Choice,"xiaoxinf","askForChoice"):(op=="read"&&question(q,AIRequest::PlayerChosen,"xiaoxinf_slash","askForPlayerChosen"));
 if(key=="diting_to")return op=="write"?question(q,AIRequest::SkillInvoke,"diting","askForSkillInvoke"):(op=="read"&&question(q,AIRequest::AmazingGrace,"diting","askForAG"));
 if(key=="fengying_to"||key=="fuping_to"||key=="jiusiTo"){
  const QString skill=key=="fengying_to"?QStringLiteral("fengying"):(key=="fuping_to"?QStringLiteral("fuping"):QStringLiteral("jiusi"));
  return (op=="write"||op=="read"||(op=="truth"&&key!="fengying_to"))&&q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY&&q.pattern.isEmpty()&&q.handlingMethod==Card::MethodUse&&p->getPhase()==Player::Play&&p->getRoom()->getCurrent()==p&&p->hasSkill(skill);
 }
 if(key=="youlong_to"||key=="secondjinzhi_to"){
  const QString skill=key=="youlong_to"?QStringLiteral("youlong"):QStringLiteral("secondjinzhi");
  return (op=="write"||op=="read")&&q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY&&q.pattern.isEmpty()&&q.handlingMethod==Card::MethodUse&&p->getPhase()==Player::Play&&p->getRoom()->getCurrent()==p&&p->hasSkill(skill);
 }
 if(key=="luochong_to")return op=="write"?question(q,AIRequest::Choice,"luochong","askForChoice"):(op=="read"&&question(q,AIRequest::PlayerChosen,"luochong","askForPlayerChosen"));
 if(key!="cl_to")return false;
 if(op=="write")return question(q,AIRequest::PlayerChosen,"choulve","askForPlayerChosen");
 return (op=="truth"||op=="read"||op=="clear")&&q.kind==AIRequest::UseCard&&q.pattern=="@@choulve!"&&q.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&q.handlingMethod==Card::MethodUse;
}
bool field(lua_State*L,int self,const char*key){
 QSet<const void*>seen;lua_pushvalue(L,self);
 for(int n=0;n<16;++n){if(!lua_istable(L,-1)||seen.contains(lua_topointer(L,-1)))return false;seen.insert(lua_topointer(L,-1));const int row=lua_absindex(L,-1);AIRound45Inspect::field(L,row,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);if(!lua_getmetatable(L,row)){lua_pushnil(L);return true;}AIRound45Inspect::field(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;}return false;
}
bool assignment(lua_State*L,int self,const char*key){AIRound45Inspect::field(L,self,key);const bool present=!lua_isnil(L,-1);lua_pop(L,1);if(present||!lua_getmetatable(L,self))return true;AIRound45Inspect::field(L,-1,"__newindex");const bool absent=lua_isnil(L,-1);lua_pop(L,2);return absent;}
ServerPlayer*roster(Room&r,const AIRequest&q,const QString&name){
 bool projected=q.worldView.self.objectName==name;for(const auto&v:q.worldView.players)projected=projected||v.objectName==name;
 if(!projected)return nullptr;for(auto*p:r.getAllPlayers(true))if(p->objectName()==name)return p;return nullptr;
}
ServerPlayer*actualPlayer(Room&r,lua_State*L,int at){for(auto*p:r.getAllPlayers(true))if(matchAIRound45SlotPlayer(L,at,p))return p;return nullptr;}
struct Call{Room*r;const AIRequest*q;ServerPlayer*p;QString key,op;QList<ServerPlayer*>targets;bool nil=false;QJsonObject result;const char*error=nullptr;};
thread_local Call*round56TargetWrite=nullptr;
struct TargetWriteFrame {Call*previous;explicit TargetWriteFrame(Call*w):previous(round56TargetWrite){round56TargetWrite=w;}~TargetWriteFrame(){round56TargetWrite=previous;}};
int fail(lua_State*L,Call*w,const char*s){w->error=s;return luaL_error(L,"%s",s);}
int run(lua_State*L){
 auto*w=static_cast<Call*>(lua_touserdata(L,1));if(!lua_checkstack(L,4096))return fail(L,w,"source target slot stack unavailable");const auto k=w->key.toUtf8();
 const bool write=w->op=="write"||w->op=="clear";int value=0;
 if(write){
  if(w->nil||w->op=="clear")lua_pushnil(L);
  else if(playerSlot(w->key)){if(!pushAIRound45SlotPlayer(L,w->targets.first()))return fail(L,w,"source Player wrapper unavailable");}
  else if(!pushAIRound45SlotTargets(L,w->targets))return fail(L,w,"source SPlayerList wrapper unavailable");
  value=lua_absindex(L,-1);
 }
 if(!current(*w->r,*w->q,w->p)||!pushAIRound45ActualAISelf(*w->r,w->p,L))return fail(L,w,"current actual gameplay Self unavailable");const int self=lua_absindex(L,-1);
 if(write){
  if(!assignment(L,self,k.constData())||!current(*w->r,*w->q,w->p))return fail(L,w,"source assignment dispatch changed");
  lua_pushvalue(L,value);lua_setfield(L,self,k.constData());
 }else{
  if(!field(L,self,k.constData()))return fail(L,w,"actual Self slot getter changed");value=lua_absindex(L,-1);
  if(w->op=="truth")w->result={{"known",true},{"type","boolean"},{"value",bool(lua_toboolean(L,value))}};
  else if(lua_isnil(L,value))w->result={{"known",true},{"type","nil"}};
  else if(playerSlot(w->key)){
   // Scalars retain the source's ordinary :objectName error in the Lua caller.
   const int t=lua_type(L,value);
   if(t==LUA_TBOOLEAN)w->result={{"known",true},{"type","boolean"},{"value",bool(lua_toboolean(L,value))}};
   else if(t==LUA_TNUMBER){double d=lua_tonumber(L,value);if(!std::isfinite(d))return fail(L,w,"nonfinite source scalar unsupported");w->result={{"known",true},{"type","number"},{"value",d}};}
   else if(t==LUA_TSTRING){size_t n;const char*s=lua_tolstring(L,value,&n);if(n>4096)return fail(L,w,"source scalar exceeds finite domain");w->result={{"known",true},{"type","string"},{"value",QString::fromUtf8(s,int(n))}};}
   else{
    auto*p=actualPlayer(*w->r,L,value);if(!p||!roster(*w->r,*w->q,p->objectName())||!matchAIRound45NativeObjectMethods(*w->r,L,value,"ServerPlayer",{"objectName"}))return fail(L,w,"actual source target or objectName binding unavailable");
    w->result={{"known",true},{"type","player"},{"value",p->objectName()}};
   }
  }else{
   // Read the *current* native list, including lists written by legacy AI and
   // later genuine source mutations. No hidden snapshot or remembered copy.
   auto certified=[&](){
    QList<ServerPlayer*>owned;
    if(readAIRound45SlotTargets(L,value,owned)){
     // Tracked wrappers have a private GC metatable; this native bridge checks
     // its exact shape and all compiled methods, including mutable backing.
     return matchAIRound45NativeClassMethod(*w->r,L,"SPlayerList","length")&&matchAIRound45NativeClassMethod(*w->r,L,"SPlayerList","at");
    }
    return matchAIRound45NativeObjectMethods(*w->r,L,value,"SPlayerList",{"length","at"});
   };
   QJsonArray names;
   // Match sgs.qlist's recomputed native length on every iteration, including
   // the terminal check. The list stays rooted across native wrapper creation.
   for(int i=0;;++i){
    if(!current(*w->r,*w->q,w->p)||!certified())return fail(L,w,"source list dispatch changed during iteration");
    lua_getfield(L,value,"length");lua_pushvalue(L,value);lua_call(L,1,1);
    if(lua_type(L,-1)!=LUA_TNUMBER)return fail(L,w,"source list length ABI changed");const double d=lua_tonumber(L,-1);lua_pop(L,1);
    if(!std::isfinite(d)||d<0||d>4096||d!=std::floor(d))return fail(L,w,"source list exceeds finite target domain");
    if(i>=int(d))break;if(i>=4096)return fail(L,w,"source list iteration exceeds finite target domain");
    lua_getfield(L,value,"at");lua_pushvalue(L,value);lua_pushinteger(L,i);lua_call(L,2,1);
    auto*p=actualPlayer(*w->r,L,-1);if(!p||!roster(*w->r,*w->q,p->objectName())||!matchAIRound45NativeObjectMethods(*w->r,L,-1,"ServerPlayer",{"objectName"}))return fail(L,w,"source list element is not a live projected Player");names.append(p->objectName());lua_pop(L,1);
   }
   w->result={{"known",true},{"type","players"},{"value",names}};
  }
 }
 if(!current(*w->r,*w->q,w->p)||!pushAIRound45ActualAISelf(*w->r,w->p,L)||!lua_rawequal(L,self,-1))return fail(L,w,"actual source receiver changed after operation");
 if(write){AIRound45Inspect::field(L,self,k.constData());if(!lua_rawequal(L,value,-1))return fail(L,w,"source slot assignment changed");}
 if(w->result.isEmpty())w->result={{"known",true}};return 0;
}
}
bool acceptAIRound56ChoulveTargetWriteEntry(Room&r,lua_State*L,int n) noexcept {
 auto*w=round56TargetWrite;
 return w&&w->r==&r&&w->key=="cl_to"&&w->op=="write"&&!w->nil
  &&inAIRound56ProjectionScope(r,L)&&n==1&&lua_gettop(L)>=2
  &&lua_iscfunction(L,-2)&&lua_tocfunction(L,-2)==run
  &&lua_type(L,-1)==LUA_TLIGHTUSERDATA&&lua_touserdata(L,-1)==w;
}
QJsonObject queryAIRound53TargetSlot(Room&r,const AIRequest&q,ServerPlayer*p,const QString&key,const QString&op,const QJsonObject&payload){
 if(!current(r,q,p)||!scope(q,p,key,op))return unknown("target slot requires exact current own source question");
 Call w{&r,&q,p,key,op};
 if(op=="write"){
  const auto t=payload.value("type").toString();if(t=="nil"&&payload.size()==1&&(!playerSlot(key)||key=="shouxingTo"||key=="geassTarget"||key=="tychendeTo"))w.nil=true;
  else if(playerSlot(key)&&t=="player"&&payload.size()==2&&payload.value("value").isString()){
   auto*target=roster(r,q,payload.value("value").toString());if(!target)return unknown("source target is not projected live roster");w.targets<<target;
  }else if((key=="cl_to"||key=="youlong_to"||key=="secondjinzhi_to"||key=="fengying_to"||key=="fuping_to"||key=="jiusiTo"||key=="fuhui_to"||key=="jg_to"||key=="mx_to"||key=="shouli_to")&&t=="players"&&payload.size()==2&&payload.value("value").isArray()){
   const auto names=payload.value("value").toArray();if(names.size()>4096)return unknown("source target list too large");
   for(const auto&v:names){if(!v.isString())return unknown("source target list element malformed");auto*target=roster(r,q,v.toString());if(!target)return unknown("source target list element not projected live roster");w.targets<<target;}
  }else return unknown("source target assignment shape unsupported");
 }else if(!payload.isEmpty())return unknown("source target read/clear has extra arguments");
 auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();if(!L||!lua_checkstack(L,4096))return unknown("source target game VM unavailable");const int top=lua_gettop(L);struct Restore{lua_State*L;int top;~Restore(){lua_settop(L,top);}}restore{L,top};
 // The complete projection union was admitted before private strategy gates.
 // This frame identifies the exact existing native source store; it grants no
 // permission to read a prior slot or to a different target-list operation.
 TargetWriteFrame round56Frame(inAIRound56ProjectionScope(r,L)&&key=="cl_to"&&op=="write"&&!w.nil?&w:nullptr);
 lua_pushcfunction(L,run);lua_pushlightuserdata(L,&w);if(LuaRuntime::protectedCall(L,1,0,0)!=LUA_OK)return unknown(w.error?w.error:"protected source target operation failed");
 if(key=="cl_to"&&op=="write"&&!w.nil&&w.result.value("known").toBool())recordAIRound56ChoulveTargetWrite(r);
 return w.result;
}
