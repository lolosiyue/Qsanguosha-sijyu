#include "ai-strategy-round46-inovation.h"
#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-round45-code-seal.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "lua-runtime.h"
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QWeakPointer>
#include "ai-strategy-round45-lua-inspect.h"
namespace {
struct Sequence final:QObject {
 explicit Sequence(Room&r):QObject(&r){}
 const AIRequest*request=nullptr;quint64 decision=0;QPointer<ServerPlayer>owner;
 QWeakPointer<AIConversionLedger>ledger;
 bool flagRead=false,flagValue=false,written=false,tagRead=false,diagnosed=false;
};
Sequence*sequence(Room&r,const AIRequest&q,ServerPlayer*p){
 Sequence*s=nullptr;for(auto*c:r.children())if((s=dynamic_cast<Sequence*>(c)))break;
 if(!s)s=new Sequence(r);
 if(s->request!=&q||s->decision!=q.decisionId||s->owner!=p||s->ledger.toStrongRef()!=q.conversionLedger){
  s->request=&q;s->decision=q.decisionId;s->owner=p;s->ledger=q.conversionLedger;s->flagRead=s->written=s->tagRead=s->diagnosed=false;
 }
 return s;
}
QJsonObject fail(const char*m){return{{"known",false},{"reason",QString::fromLatin1(m)}};}
bool current(Room&r,const AIRequest&q,ServerPlayer*p){
 return p&&p->getRoom()==&r&&q.viewerObjectName==p->objectName()&&aiRound44RangeRequestCurrent(r,q,p);
}
struct Stack{lua_State*L;int top;explicit Stack(lua_State*l):L(l),top(lua_gettop(l)){}~Stack(){lua_settop(L,top);}};
bool readField(lua_State*L,int index,const char*key){
 lua_pushvalue(L,index);QSet<const void*>seen;
 for(int depth=0;depth<16;++depth){
  if(!lua_istable(L,-1)||seen.contains(lua_topointer(L,-1)))return false;
  seen.insert(lua_topointer(L,-1));const int table=lua_absindex(L,-1);
  AIRound45Inspect::field(L,table,key);if(!lua_isnil(L,-1))return true;
  lua_pop(L,1);if(!lua_getmetatable(L,table)){lua_pushnil(L);return true;}
  AIRound45Inspect::field(L,-1,"__index");if(lua_isnil(L,-1))return true;
  if(!lua_istable(L,-1))return false;
 }
 return false;
}
bool fixedAGScalar(Room&r){
 auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();
 if(!L||!lua_checkstack(L,128))return false;Stack stack(L);
 AIRound45Inspect::globals(L);if(!readField(L,-1,"sgs")||!lua_istable(L,-1))return false;
 const int sgs=lua_absindex(L,-1);
 // Prove the actual fixed callback, without invoking it. The full source file
 // was independently pinned at native trusted bootstrap by the existing seal.
 if(!readField(L,sgs,"ai_skill_askforag")||!lua_istable(L,-1)||!readField(L,-1,"kaiqi")
  ||!matchAIRound45PinnedFunction(r,L,-1,"lua/ai/inovation-ai.lua",1411,1424))return false;
 const int callback=lua_absindex(L,-1);
 bool env=false;for(int i=1;;++i){const char*up=lua_getupvalue(L,callback,i);if(!up)break;
  const bool same=QString::fromLatin1(up)=="_ENV"&&lua_topointer(L,-1)==lua_topointer(L,stack.top+1);lua_pop(L,1);if(!same)return false;env=true;
 }if(!env)return false;
 if(!readField(L,sgs,"kaiqi_ag_type")||lua_type(L,-1)!=LUA_TSTRING)return false;
 size_t n=0;lua_tolstring(L,-1,&n);
 // The fixed initializer is ''. Every nonempty writer is the unmatched old
 // Discard callback. Unknown/external changes are not exported as strategy data.
 return n==0;
}
}
QJsonObject queryAIRound46Inovation(Room&r,const AIRequest&q,ServerPlayer*p,const QString&op,const QString&name){
 if(!current(r,q,p))return fail("Inovation current request unavailable");
 if(q.choiceOptions.reason=="ningju"){
  if(q.kind!=AIRequest::CardChosen||q.choiceOptions.playerNames!=QStringList{p->objectName()}
   ||q.choiceOptions.choices!=QStringList{"he"}||!name.isEmpty()
   ||(q.handlingMethod!=Card::MethodNone&&q.handlingMethod!=Card::MethodDiscard))return fail("Ningju requires the actual self CardChosen question");
  auto*s=sequence(r,q,p);
  if(op=="ningju_tag"){
   const QVariant value=r.getTag("ningju_color");
   // Current fixed V2 never sets this key. Legacy NingjuCard::use is not the
   // V2 product; a restored/externally written value has no current disclosure.
   if(value.isValid())return fail("Ningju nonempty legacy tag has no current public writer witness");
   s->tagRead=true;return{{"known",true},{"value",value.toString()}};
  }
  if((op!="ningju_red"&&op!="ningju_black")||!s->tagRead||s->diagnosed)return fail("Ningju diagnostic outside reached source tag read");
  s->diagnosed=true;
  const QString message=p->getGeneralName()+(op=="ningju_red"?" need to use red":" need to use black");
  r.output(message);qWarning("%s",qPrintable(message));
  if(!current(r,q,p))return fail("Ningju diagnostic changed current request; source prefix retained");
  return{{"known",true}};
 }
 if(q.choiceOptions.reason!="kaiqi")return fail("Inovation operation has no matching question");
 if(op=="kaiqi_ag_scalar"){
  if(q.kind!=AIRequest::AmazingGrace||!name.isEmpty()||!fixedAGScalar(r)||!current(r,q,p))return fail("Kaiqi fixed scalar initialization is not attested");
  return{{"known",true},{"value",QString()}};
 }
 if(q.kind!=AIRequest::PlayerChosen||!p->hasSkill("kaiqi")||!current(r,q,p))return fail("Kaiqi requires the current owner PlayerChosen question");
 auto*s=sequence(r,q,p);
 if(op=="kaiqi_self_flag"){
  if(!name.isEmpty()||s->flagRead)return fail("Kaiqi owner flag read is not the first source read");
  s->flagRead=true;s->flagValue=p->hasFlag("kaiqi_self_used");
  return{{"known",true},{"value",s->flagValue}};
 }
 if(!s->flagRead||s->written)return fail("Kaiqi write has no unmatched source read");
 ServerPlayer*target=nullptr;QString flag;
 if(op=="kaiqi_set_self"&&!s->flagValue&&name.isEmpty()){target=p;flag="kaiqi_self_used";}
 else if(op=="kaiqi_set_target"&&s->flagValue&&q.choiceOptions.playerNames.contains(name)){
  target=r.findPlayerByObjectName(name);flag="kaiqi_used";
 }
 if(!target||target->getRoom()!=&r)return fail("Kaiqi source-selected flag target invalid");
 s->written=true; // No retry/reselection after a reached native write/error.
 AIRound46KaiqiFlagPermit permit(r,q,p,target);QString error;
 if(!writeAIRound46KaiqiFlag(r,q,p,target,flag,permit,&error))return{{"known",false},{"reason",error}};
 return{{"known",true}};
}
