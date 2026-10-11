#include "ai-strategy-round50-slots.h"
#include "ai-strategy-round45-inovation-providers.h"
#include "ai-strategy-round44-certificate.h"
#include "ai.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "lua-runtime.h"
#include <QSet>
#include <QJsonArray>
#include "ai-strategy-round45-native-seal.h"
#include <cmath>
#include "ai-strategy-round45-lua-inspect.h"
namespace {
QJsonObject unknown(const char*s){return{{"known",false},{"reason",QString::fromLatin1(s)}};}
bool current(Room&r,const AIRequest&q,ServerPlayer*p){return p&&r.getAllPlayers(true).contains(p)&&p->getRoom()==&r&&r.roomRuntime()&&!r.roomRuntime()->isClosing()&&q.decisionId&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&aiRound44RangeRequestCurrent(r,q,p)&&Sanguosha->currentRoom()==&r;}
bool question(const AIRequest&q,AIRequest::DecisionKind k,const char*reason,const char*method){return q.kind==k&&q.choiceOptions.reason==reason&&q.choiceOptions.question==method;}
bool play(const AIRequest&q){
 if(q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY&&q.pattern.isEmpty())return true;
 const auto c=q.worldView.strategyFacts.value("readonly_play_context").toObject();const auto source=c.value("source").toString();
 if(c.value("known")!=true||c.value("readonly")!=true||c.value("viewer")!=q.viewerObjectName||c.value("decision_id")!=q.getDecisionId()||c.value("state_revision")!=q.getStateRevision()||c.value("kind")!="activate"||c.value("reason")!=int(CardUseStruct::CARD_USE_REASON_PLAY)||c.value("pattern")!=""||c.value("handling_method")!=int(Card::MethodUse))return false;
 if(q.kind==AIRequest::SkillInvoke)return (source=="olClan.zuheta"&&(q.choiceOptions.reason=="zuheta"||q.choiceOptions.reason=="zuheta$-1"))||(source=="OverseasVersion.ov_xiafeng"&&q.choiceOptions.reason=="ov_xiafeng");
 if(q.kind==AIRequest::Choice)return source=="OverseasVersion.ov_shelie"&&q.choiceOptions.reason=="ov_shelie"&&q.choiceOptions.choices==QStringList{"Player_Draw","Player_Play"};
 return source=="OverseasVersion.ov_niwo"&&q.kind==AIRequest::UseCard&&q.pattern=="@@ov_niwo"&&q.prompt=="ov_niwo0"&&q.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&q.handlingMethod==Card::MethodUse;
}
bool use(const AIRequest&q,const char*pattern){return q.kind==AIRequest::UseCard&&q.pattern==pattern;}
bool scope(const AIRequest&q,const QString&k,bool write){
 if(k=="wannawin")return write&&question(q,AIRequest::SkillInvoke,"wannawin","askForSkillInvoke");
 if(k=="jiewq_lose")return play(q)||(!write&&question(q,AIRequest::Choice,"jiewq","askForChoice"));
 if(k=="s3_fans_handou_card")return write?use(q,"@@s3_fans_handou"):question(q,AIRequest::RespondCard,"s3_fans_handou","askForPindian");
 if(k=="s3_jxwj_ganglichoice")return write?question(q,AIRequest::SkillInvoke,"s3_jxwj_gangli","askForSkillInvoke"):question(q,AIRequest::Choice,"s3_jxwj_gangli","askForChoice");
 if(k=="guanxu_friends")return play(q)||question(q,AIRequest::PlayerChosen,"yashi","askForPlayerChosen")||(!write&&use(q,"@@guanxu1"));
 if(k=="isfriend"||k=="thwuyanDp"||k=="thwuyanHs")return play(q);
 if(k=="qianlong_use")return use(q,"@@qianlong")||(!write&&question(q,AIRequest::PlayerChosen,"juetao","askForPlayerChosen"));
 return false;
}
bool valid(const QString&k,const QJsonObject&v){
 if(k=="wannawin")return v.size()==1&&v.value("type")=="nil";
 const auto t=v.value("type").toString();if(t=="nil")return k=="isfriend"&&v.size()==1;
 if(v.size()!=2||!v.contains("value"))return false;
 if(k=="jiewq_lose"||k=="thwuyanDp"||k=="thwuyanHs"||k=="s3_fans_handou_card")return t=="number"&&v.value("value").isDouble()&&std::isfinite(v.value("value").toDouble());
 if(k=="s3_jxwj_ganglichoice")return t=="string"&&(v.value("value")=="s3_jxwj_gangli_throwtohp"||v.value("value")=="s3_jxwj_gangli_throwhp");
 return t=="boolean"&&v.value("value").isBool()&&(k!="isfriend"||v.value("value").toBool());
}
bool field(lua_State*L,int self,const char*key){
 QSet<const void*>seen;lua_pushvalue(L,self);
 for(int n=0;n<16;++n){if(!lua_istable(L,-1)||seen.contains(lua_topointer(L,-1)))return false;seen.insert(lua_topointer(L,-1));int row=lua_absindex(L,-1);AIRound45Inspect::field(L,row,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);if(!lua_getmetatable(L,row)){lua_pushnil(L);return true;}AIRound45Inspect::field(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;}return false;
}
bool assignment(lua_State*L,int self,const char*key){AIRound45Inspect::field(L,self,key);bool present=!lua_isnil(L,-1);lua_pop(L,1);if(present||!lua_getmetatable(L,self))return true;AIRound45Inspect::field(L,-1,"__newindex");bool absent=lua_isnil(L,-1);lua_pop(L,2);return absent;}
struct Write{Room*r;const AIRequest*q;ServerPlayer*p;QByteArray key;QJsonObject value;const char*error=nullptr;};
int fail(lua_State*L,Write*w,const char*s){w->error=s;return luaL_error(L,"%s",s);}
int assign(lua_State*L){
 auto*w=static_cast<Write*>(lua_touserdata(L,1));if(!lua_checkstack(L,4096))return fail(L,w,"source slot assignment capacity unavailable");
 lua_pushlstring(L,w->key.constData(),size_t(w->key.size()));int key=lua_absindex(L,-1);const auto t=w->value.value("type").toString();
 if(t=="nil")lua_pushnil(L);else if(t=="boolean")lua_pushboolean(L,w->value.value("value").toBool());else if(t=="number")lua_pushnumber(L,w->value.value("value").toDouble());else{auto s=w->value.value("value").toString().toUtf8();lua_pushlstring(L,s.constData(),size_t(s.size()));}int value=lua_absindex(L,-1);
 if(!current(*w->r,*w->q,w->p)||!pushAIRound45ActualAISelf(*w->r,w->p,L))return fail(L,w,"source slot current Self unavailable before write");int self=lua_absindex(L,-1);
 if(!assignment(L,self,w->key.constData())||!current(*w->r,*w->q,w->p))return fail(L,w,"source slot assignment handler not certified");
 lua_pushvalue(L,key);lua_pushvalue(L,value);lua_settable(L,self);
 if(!current(*w->r,*w->q,w->p)||!pushAIRound45ActualAISelf(*w->r,w->p,L)||!lua_rawequal(L,self,-1))return fail(L,w,"source slot current Self changed after write");
 AIRound45Inspect::field(L,self,w->key.constData());if(!lua_rawequal(L,value,-1))return fail(L,w,"source slot write changed");return 0;
}
bool listScope(const AIRequest&q,const QString&key,const QString&op){
 if(key=="shilie_n")return (op=="reset"||op=="append"||op=="length")?play(q):(op=="contains"&&use(q,"@@shilie!"));
 if(key=="yuanrongIds")return op=="write"?question(q,AIRequest::AmazingGrace,"yuanrong","askForAG"):(op=="read"&&use(q,"@@yuanrong!"));
 return false;
}
struct ListCall{Room*r;const AIRequest*q;ServerPlayer*p;QString key,op;QJsonObject payload,result;const char*error=nullptr;};
int listFail(lua_State*L,ListCall*w,const char*s){w->error=s;return luaL_error(L,"%s",s);}
bool integer(const QJsonValue&v,int&out){double d=v.toDouble();if(!v.isDouble()||!std::isfinite(d)||d<0||d>2147483647.0||d!=std::floor(d))return false;out=int(d);return true;}
int listCall(lua_State*L){
 auto*w=static_cast<ListCall*>(lua_touserdata(L,1));if(!lua_checkstack(L,4096))return listFail(L,w,"source list frame capacity unavailable");
 const auto k=w->key.toUtf8();lua_pushlstring(L,k.constData(),size_t(k.size()));int key=lua_absindex(L,-1);
 const bool replace=w->op=="reset"||w->op=="write";int value=0;
 if(replace){
  if(w->key=="shilie_n"){
   lua_pushglobaltable(L);AIRound45Inspect::field(L,-1,"sgs");if(!lua_istable(L,-1))return listFail(L,w,"IntList namespace unavailable");AIRound45Inspect::field(L,-1,"IntList");
   if(!matchAIRound47NativeScratchConstructor(*w->r,L,-1,"IntList"))return listFail(L,w,"IntList source constructor changed");lua_call(L,0,1);
   if(!matchAIRound45NativeObjectMethods(*w->r,L,-1,"IntList",{"append","length","contains"}))return listFail(L,w,"IntList source methods changed");
  }else{
   lua_newtable(L);int table=lua_absindex(L,-1);int i=0;for(const auto&v:w->payload.value("ids").toArray()){lua_pushinteger(L,v.toInt());lua_rawseti(L,table,++i);}
  }
  value=lua_absindex(L,-1);
 }
 if(!current(*w->r,*w->q,w->p)||!pushAIRound45ActualAISelf(*w->r,w->p,L))return listFail(L,w,"source list current Self unavailable");int self=lua_absindex(L,-1);
 if(replace){
  if(!assignment(L,self,k.constData())||!current(*w->r,*w->q,w->p))return listFail(L,w,"source list assignment handler changed");
  lua_pushvalue(L,key);lua_pushvalue(L,value);lua_settable(L,self);
 }else{
  if(!field(L,self,k.constData()))return listFail(L,w,"source list lookup is not certified");value=lua_absindex(L,-1);
  if(lua_isnil(L,value)){w->result={{"known",true},{"type","nil"}};}
  else if(w->key=="yuanrongIds"){
   if(!lua_istable(L,value)||lua_getmetatable(L,value))return listFail(L,w,"Yuanrong current slot is not a plain source list");
   QJsonArray ids;const size_t n=lua_rawlen(L,value);if(n>4096)return listFail(L,w,"Yuanrong source list exceeds finite card domain");
   for(size_t i=1;i<=n;++i){lua_rawgeti(L,value,lua_Integer(i));if(lua_type(L,-1)!=LUA_TNUMBER)return listFail(L,w,"Yuanrong source list has a non-ID entry");double d=lua_tonumber(L,-1);if(!std::isfinite(d)||d<0||d>2147483647.0||d!=std::floor(d))return listFail(L,w,"Yuanrong source list has a non-ID entry");ids.append(int(d));lua_pop(L,1);}
   size_t entries=0;lua_pushnil(L);while(AIRound45Inspect::next(L,value)){if(lua_type(L,-2)!=LUA_TNUMBER)return listFail(L,w,"Yuanrong source list has non-array fields");double i=lua_tonumber(L,-2);if(i<1||i>double(n)||i!=std::floor(i))return listFail(L,w,"Yuanrong source list is sparse");++entries;lua_pop(L,1);}if(entries!=n)return listFail(L,w,"Yuanrong source list is sparse");
   w->result={{"known",true},{"type","ids"},{"value",ids}};
  }else{
   auto method=w->op.toUtf8();if(!matchAIRound45NativeObjectMethods(*w->r,L,value,"IntList",{w->op}))return listFail(L,w,"Shilie current IntList method changed");
   // This invokes only the sealed native QList method reached by the source.
   // Its userdata and mutable backing stay entirely in the gameplay VM.
   lua_getfield(L,value,method.constData());lua_pushvalue(L,value);int nargs=1;
   if(w->op!="length"){lua_pushinteger(L,w->payload.value("id").toInt());++nargs;}
   if(!current(*w->r,*w->q,w->p))return listFail(L,w,"Shilie request changed before source method");
   lua_call(L,nargs,1);
   if(w->op=="length"){if(lua_type(L,-1)!=LUA_TNUMBER)return listFail(L,w,"IntList length ABI changed");w->result={{"known",true},{"type","number"},{"value",double(lua_tonumber(L,-1))}};}
   else if(w->op=="contains"){if(lua_type(L,-1)!=LUA_TBOOLEAN)return listFail(L,w,"IntList contains ABI changed");w->result={{"known",true},{"type","boolean"},{"value",bool(lua_toboolean(L,-1))}};}
  }
 }
 if(!current(*w->r,*w->q,w->p)||!pushAIRound45ActualAISelf(*w->r,w->p,L)||!lua_rawequal(L,self,-1))return listFail(L,w,"source list current Self changed after operation");
 if(replace){AIRound45Inspect::field(L,self,k.constData());if(!lua_rawequal(L,value,-1))return listFail(L,w,"source list assignment changed");}
 if(w->result.isEmpty())w->result={{"known",true}};return 0;
}
QJsonObject listQuery(Room&r,const AIRequest&q,ServerPlayer*p,const QString&key,const QString&op,const QJsonObject&payload){
 if(!listScope(q,key,op)||!current(r,q,p))return unknown("source list exact question unsupported");
 if(key=="yuanrongIds"&&op=="write"){
  if(payload.size()!=1||!payload.value("ids").isArray())return unknown("Yuanrong assignment requires exact offered ID list");const auto ids=payload.value("ids").toArray();if(ids.size()!=q.choiceOptions.cardIds.size())return unknown("Yuanrong assignment changed offered list");
  for(int i=0;i<ids.size();++i){int id;if(!integer(ids[i],id)||id!=q.choiceOptions.cardIds[i])return unknown("Yuanrong assignment changed offered order");}
 }else if(op=="append"||op=="contains"){
  int id;if(payload.size()!=1||!integer(payload.value("id"),id))return unknown("Shilie source method requires exact ID");
  // append's producer enumerates this current owner's disclosed Shilie pile.
  if(op=="append"){bool known=false;for(const auto&pile:q.worldView.self.piles)if(pile.name=="shilie"&&pile.cardIds.contains(id))known=true;if(!known||!p->getPile("shilie").contains(id))return unknown("Shilie append material not current own disclosed pile");}
 }else if(!payload.isEmpty())return unknown("source list operation unexpected arguments");
 auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();if(!L||!lua_checkstack(L,4096))return unknown("source list gameplay VM unavailable");int top=lua_gettop(L);struct Restore{lua_State*L;int n;~Restore(){lua_settop(L,n);}}restore{L,top};
 ListCall w{&r,&q,p,key,op,payload};lua_pushcfunction(L,listCall);lua_pushlightuserdata(L,&w);if(LuaRuntime::protectedCall(L,1,0,0)!=LUA_OK)return unknown(w.error?w.error:"protected source list operation failed");return w.result;
}
}
QJsonObject queryAIRound50SourceSlot(Room&r,const AIRequest&q,ServerPlayer*p,const QString&key,const QString&op,const QJsonObject&v){
 if(key=="shilie_n"||key=="yuanrongIds")return listQuery(r,q,p,key,op,v);
 const bool write=op=="write";if((!write&&op!="read")||!current(r,q,p)||!scope(q,key,write)||(write?!valid(key,v):!v.isEmpty()))return unknown("source slot shape or exact current question unsupported");
 if(write&&key=="s3_fans_handou_card"){int id;if(!integer(v.value("value"),id)||!p->handCards().contains(id))return unknown("Handou saved ID must be current own hand");bool disclosed=false;for(const auto&c:q.worldView.handCards)if(c.effectiveId==id)disclosed=true;if(!disclosed)return unknown("Handou saved ID is not current disclosed hand");}
 auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();if(!L||!lua_checkstack(L,4096))return unknown("source slot gameplay VM unavailable");int top=lua_gettop(L);struct Restore{lua_State*L;int n;~Restore(){lua_settop(L,n);}}restore{L,top};
 if(write){Write w{&r,&q,p,key.toUtf8(),v};lua_pushcfunction(L,assign);lua_pushlightuserdata(L,&w);if(LuaRuntime::protectedCall(L,1,0,0)!=LUA_OK)return unknown(w.error?w.error:"protected source slot write failed");return{{"known",true}};}
 if(!current(r,q,p)||!pushAIRound45ActualAISelf(r,p,L))return unknown("source slot actual Self unavailable");int self=lua_absindex(L,-1);auto name=key.toUtf8();if(!field(L,self,name.constData()))return unknown("source slot dynamic lookup is unsupported");
 QJsonObject result{{"known",true}};switch(lua_type(L,-1)){case LUA_TNIL:result.insert("type","nil");break;case LUA_TBOOLEAN:result.insert("type","boolean");result.insert("value",bool(lua_toboolean(L,-1)));break;case LUA_TNUMBER:{double d=lua_tonumber(L,-1);if(!std::isfinite(d))return unknown("source slot nonfinite scalar unsupported");result.insert("type","number");result.insert("value",d);break;}case LUA_TSTRING:{size_t n;const char*s=lua_tolstring(L,-1,&n);if(n>256)return unknown("source slot scalar string exceeds finite domain");result.insert("type","string");result.insert("value",QString::fromUtf8(s,int(n)));break;}default:return unknown("source slot is not scalar");}
 if(!current(r,q,p))return unknown("source slot request changed after read");return result;
}
