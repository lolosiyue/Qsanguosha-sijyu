#include "ai-strategy-round56-connection-cache.h"
#include "ai-strategy-round56-playloop-filter.h"
#include "ai-strategy-round56-clone-lineage.h"
#include "ai-strategy-round56-clone-scope.h"
#include "ai-strategy-round56-priority-bootstrap.h"
#include "ai-strategy-round56-projection.h"
#include "ai-strategy-round56-async.h"
#include "ai-strategy-round56-bootstrap-write.h"
#include "ai-strategy-round45-code-seal.h"
#include "ai-strategy-round45-native-seal.h"
#include "ai-strategy-round45-slot-bindings.h"
#include "ai-strategy-round45-inovation-providers.h"
#include "ai-strategy-round44-certificate.h"
#include "player-initial-source-origin.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "engine.h"
#include "skill.h"
#include <typeinfo>
#include "settings.h"
#include <QHash>
#include <QSet>
#include <QPointer>
#include <QJsonArray>
#include <cstring>
#include "ai-strategy-round45-lua-inspect.h"
extern bool matchAIRound56NativeRoom(lua_State*,int,const Room*);
namespace {
constexpr const char*smart="lua/ai/smart-ai.lua";
constexpr const char*utility="lua/utilities.lua";
struct State;struct Call;thread_local Call*active=nullptr;
void observe(lua_State*,const lua_TableObservation*,void*)noexcept;
struct Stack{lua_State*L;int top;explicit Stack(lua_State*s):L(s),top(lua_gettop(s)){}~Stack(){lua_settop(L,top);}};
enum Kind{Namespace,Qlist,Connect,Array};
struct State final:QObject{
 Room&r;lua_State*L;quint64 generation;bool live=true,resetPending=false;
 const void*space=nullptr,*qlist=nullptr,*connect=nullptr;
 QHash<const void*,Kind>watches;QHash<QString,const void*>lists,connections;QSet<QString>skillKeys;
 State(Room&room,lua_State*l):QObject(&room),r(room),L(l),generation(room.roomRuntime()->lua().generation()){}
 ~State()override{for(auto i=watches.cbegin();i!=watches.cend();++i)lua_unobservetableidentity(L,i.key(),observe,this);}
 void retire()noexcept{live=false;retireAIRound56Projection(r,L);}
 bool watch(const void*p,Kind kind){
  if(!p||watches.contains(p))return false;
  if(!lua_observetableidentity(L,p,observe,this))return false;
  try{watches.insert(p,kind);return true;}catch(...){lua_unobservetableidentity(L,p,observe,this);throw;}
 }
};
State*find(const Room&r){for(auto*c:r.children())if(auto*s=dynamic_cast<State*>(c))return s;return nullptr;}
bool live(const State&s){auto*rt=s.r.roomRuntime();return s.live&&!s.resetPending&&rt&&!rt->isClosing()&&rt->lua().rawState()==s.L&&rt->lua().generation()==s.generation&&inAIRound56ProjectionScope(s.r,s.L)&&hasAIRound56AsyncUniverse(s.r,s.L);}
bool key(const lua_TableObservation&e,const QByteArray&k){return e.string_key&&e.string_key_length==size_t(k.size())&&!std::memcmp(e.string_key,k.constData(),size_t(k.size()));}
bool at(State&s,lua_State*L,const lua_TableObservation&e,int first,int last,int line){return e.lua_function_identity&&(e.flags&(LUA_TABLE_OBSERVER_VM_FAST|LUA_TABLE_OBSERVER_VM_SLOW))&&matchAIRound56PinnedInstruction(s.r,L,e.lua_function_identity,e.lua_instruction_index,smart,first,last,line);}
struct Call{
 State*s;const AIRequest*q;ServerPlayer*viewer,*player;QString op,key;
 int depth;bool entered=false;QJsonObject result;const char*error=nullptr;
};
bool permitted(State&s,lua_State*L,const lua_TableObservation&e){
 auto*w=active;if(!w||w->s!=&s||!w->entered||!live(s)||L!=s.L||e.event!=LUA_TABLE_OBSERVER_WRITE||e.value_type!=LUA_TTABLE||!e.value_table_identity)return false;
 if(e.table_identity==s.space)return w->op=="source_connections"&&key(e,"ai_connect_cache")
  &&(!s.connect||s.connect==e.value_table_identity)
  &&matchAIRound56BootstrapWrite(s.r,L,e,smart,3316,"ai_connect_cache",true,3209,3319);
 if(e.table_identity==s.qlist){
  if(!e.string_key||e.string_key_length>4096)return false;
  const QString written=QString::fromUtf8(e.string_key,int(e.string_key_length));
  // An admitted correction can nest an original helper. Authenticate that
  // actual producer and its whole-roster key, not the outer bridge operation.
  return (written=="global_alive_players"&&at(s,L,e,443,454,451))
   ||(s.skillKeys.contains(written)&&at(s,L,e,470,534,531));
 }
 return s.connect&&e.table_identity==s.connect&&w->op=="source_connections"
  &&key(e,w->key.toUtf8())&&at(s,L,e,3209,3319,3317);
}
void observe(lua_State*L,const lua_TableObservation*ep,void*u)noexcept{
 auto&s=*static_cast<State*>(u);if(!ep||!L||!s.L||G(L)!=G(s.L))return;const auto&e=*ep;auto i=s.watches.constFind(e.table_identity);if(i==s.watches.cend())return;
 if(e.event==LUA_TABLE_OBSERVER_FREE){s.watches.remove(e.table_identity);if(e.table_identity==s.space)s.space=nullptr;if(e.table_identity==s.qlist)s.qlist=nullptr;if(e.table_identity==s.connect)s.connect=nullptr;s.retire();return;}
 if(!s.live||e.event==LUA_TABLE_OBSERVER_READ||e.event==LUA_TABLE_OBSERVER_ENUMERATE)return;
 if(e.event==LUA_TABLE_OBSERVER_METATABLE){s.retire();return;}if(e.event!=LUA_TABLE_OBSERVER_WRITE)return;
 try{
  const Kind kind=*i;
  if(kind==Namespace&&(key(e,"qlist_cache")||key(e,"ai_connect_cache"))
    &&allowAIRound56PlayLoopWrite(s.r,L,e)){
   // Actual original reset publication. Drop every old array subscription and
   // receipt immediately; a later FREE/address reuse cannot touch the new map.
   // No stale epoch/source is repaired, and no current table is sampled.
   if(key(e,"qlist_cache")){
    for(auto w=s.watches.begin();w!=s.watches.end();){
     if(w.key()==s.space){++w;continue;}
     lua_unobservetableidentity(L,w.key(),observe,&s);w=s.watches.erase(w);
    }
    s.lists.clear();s.connections.clear();s.skillKeys.clear();
    s.qlist=nullptr;s.connect=nullptr;s.resetPending=true;
    if(!s.watch(e.value_table_identity,Qlist)){s.retire();return;}
    s.qlist=e.value_table_identity;
   }else{
    if(!s.resetPending||!s.qlist||s.connect||!s.watch(e.value_table_identity,Connect)){s.retire();return;}
    s.connect=e.value_table_identity;s.resetPending=false;
   }
   return;
  }
  if(kind==Namespace&&!key(e,"ai_connect_cache"))return; // Clone namespace watcher owns every other key.
  if(kind==Qlist&&inAIRound56CloneScope(s.r,L))return; // Exact full Clone proof excludes every requested key.
  if(!permitted(s,L,e)){s.retire();return;}
  if(kind==Namespace){if(!s.connect){if(!s.watch(e.value_table_identity,Connect)){s.retire();return;}s.connect=e.value_table_identity;}return;}
  // A fresh original producer publishes a plain array exactly once per key.
  // No current occupancy/value is used to establish that source origin.
  auto&rows=kind==Qlist?s.lists:s.connections;
  const QString written=QString::fromUtf8(e.string_key,int(e.string_key_length));
  if(rows.contains(written)||!s.watch(e.value_table_identity,Array)){s.retire();return;}
  rows.insert(written,e.value_table_identity);
 }catch(...){s.retire();}
}
bool current(Room&r,const AIRequest&q,ServerPlayer*p){return p&&p->getRoom()==&r&&q.decisionId&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&Sanguosha->currentRoom()==&r&&aiRound44RangeRequestCurrent(r,q,p);}
bool closure(Room&r,lua_State*L,int i,int env,const char*path,int first,int last){
 i=lua_absindex(L,i);if(!matchAIRound45PinnedFunction(r,L,i,path,first,last))return false;
 for(int n=1;;++n){const char*name=lua_getupvalue(L,i,n);if(!name)break;const bool ok=!std::strcmp(name,"_ENV")&&lua_rawequal(L,-1,env);lua_pop(L,1);if(!ok)return false;}return true;
}
bool sources(Room&r,lua_State*L,ServerPlayer*viewer){
 using namespace AIRound45Inspect;Stack stack(L);globals(L);const int env=lua_absindex(L,-1);auto*g=table(L,env);if(!g||g->metatable)return false;
 field(L,env,"sgs");const int ns=lua_absindex(L,-1);if(!lua_istable(L,ns))return false;
 struct F{const char*name;int first,last;bool global;};
 for(const F&f: {F{"aiConnect",3209,3319,true},F{"getCachedAlivePlayers",457,460,false},F{"getPlayerSkillList",470,534,false},F{"qlist_cached",443,454,false}}){Stack z(L);field(L,f.global?env:ns,f.name);if(!closure(r,L,-1,env,smart,f.first,f.last))return false;}
 {Stack z(L);field(L,ns,"QList2Table");if(!closure(r,L,-1,env,utility,44,51))return false;}
 {Stack z(L);field(L,ns,"qlist");int f=lua_absindex(L,-1);if(!matchAIRound45PinnedFunction(r,L,f,utility,19,35))return false;
  int seen=0;for(int n=1;;++n){const char*k=lua_getupvalue(L,f,n);if(!k)break;bool ok=false;
   if(!std::strcmp(k,"_ENV"))ok=lua_rawequal(L,-1,env);
   else if(!std::strcmp(k,"qlist_userdata_iterator")){ok=closure(r,L,-1,env,utility,7,11);seen|=1;}
   else if(!std::strcmp(k,"qlist_table_iterator")){ok=closure(r,L,-1,env,utility,13,17);seen|=2;}
   lua_pop(L,1);if(!ok)return false;}if(seen!=3)return false;
 }
 // Public original getenv source at smart468; every later namespace write is
 // observed. This finite domain does not execute the optional diagnostic arm.
 {Stack z(L);field(L,ns,"ai_cache_verify");if(lua_type(L,-1)!=LUA_TBOOLEAN||lua_toboolean(L,-1))return false;}
 {Stack z(L);field(L,env,"global_room");if(!matchAIRound56NativeRoom(L,-1,&r))return false;}
 {Stack z(L);field(L,env,"current_self");if(!lua_istable(L,-1))return false;
  // Original aiConnect needs the source-created non-nil current_self only.
  // The last ordinary Clone may belong to a different roster player.
  bool found=false;for(auto*p:r.getAllPlayers(true)){Stack zz(L);if(pushAIRound45ActualAISelf(r,p,L)&&lua_rawequal(L,-1,-2)){found=true;break;}}if(!found)return false;
 }
 for(const char*k:{"type","ipairs","error","tostring"}){Stack z(L);field(L,env,k);if(!matchAIRound45NativeBinding(r,L,-1,k))return false;}
 const QHash<QString,QStringList> libraries={{"string",{"find","sub","gmatch"}},{"table",{"insert"}},{"debug",{"traceback"}}};
 for(auto i=libraries.cbegin();i!=libraries.cend();++i){Stack z(L);field(L,env,i.key().toUtf8().constData());auto*t=table(L,-1);if(!t||t->metatable)return false;int lib=lua_absindex(L,-1);for(const auto&k:i.value()){Stack zz(L);field(L,lib,k.toUtf8().constData());if(!matchAIRound45NativeBinding(r,L,-1,i.key()+'.'+k))return false;}}
 const QHash<QString,QStringList> methods={
  {"ServerPlayer",{"objectName","getSkillList","getVisibleSkillList","hasSkill","hasLordSkill","getMarkNames","getMark","getPileNames","getPile","getEquips"}},
  {"Room",{"getAlivePlayers","getLord"}},{"Engine",{"getViewAsEquipSkill"}},
  {"Skill",{"objectName","isLordSkill","inherits"}},{"ViewAsEquipSkill",{"viewAsEquip"}},
  {"Card",{"objectName"}},{"IntList",{"isEmpty"}},{"SPlayerList",{"length","at"}},
  {"SkillList",{"length","at"}},{"CardList",{"length","at"}}};
 for(auto i=methods.cbegin();i!=methods.cend();++i)for(const auto&k:i.value()){
  const bool derived=i.key()=="Skill"||i.key()=="Card"||i.key()=="ViewAsEquipSkill";
  if(!(derived?matchAIRound45NativeDerivedMethods(r,L,i.key(),{k}):matchAIRound45NativeClassMethod(r,L,i.key(),k)))return false;
 }
 return matchAIRound45NativeNamespaceProperty(r,L,"sgs","Sanguosha")&&matchAIRound45NativeNamespaceWrite(r,L,"sgs","ai_connect_cache")&&matchAIRound47NativeStringMetatable(r,L)&&matchAIRound47NativeValueMetamethods(r,L);
}
QJsonObject unknown(const char*reason){return{{"applicable",true},{"known",false},{"reason",reason}};}
int fail(lua_State*L,Call*w,const char*message){w->error=message;w->s->retire();return luaL_error(L,"%s",message);}
int run(lua_State*L){
 auto*w=static_cast<Call*>(lua_touserdata(L,1));auto&s=*w->s;
 if(!live(s)||!current(s.r,*w->q,w->viewer))return fail(L,w,"source cache scope changed before execution");
 AIRound45Inspect::globals(L);const int env=lua_absindex(L,-1);
 if(w->op=="source_connections")AIRound45Inspect::field(L,env,"aiConnect");
 else{AIRound45Inspect::field(L,env,"sgs");AIRound45Inspect::field(L,-1,w->op=="source_alive"?"getCachedAlivePlayers":"getPlayerSkillList");lua_remove(L,-2);}
 if(w->op!="source_alive"&&!pushAIRound45SlotPlayer(L,w->player))return fail(L,w,"source cache argument wrapper unavailable");
 lua_call(L,w->op=="source_alive"?0:1,1);
 const int result=lua_absindex(L,-1);const void*expected=(w->op=="source_connections"?s.connections:s.lists).value(w->key,nullptr);
 auto*t=AIRound45Inspect::table(L,result);
 if(!live(s)||!current(s.r,*w->q,w->viewer)||!expected||t!=expected||t->metatable)return fail(L,w,"actual cache result lacks its original write receipt");
 const size_t count=lua_rawlen(L,result);if(count>4096)return fail(L,w,"source cache exceeds finite roster bound");QJsonArray names,definitions;
 for(size_t n=1;n<=count;++n){
  lua_rawgeti(L,result,lua_Integer(n));
  if(w->op=="source_connections"){
   if(lua_type(L,-1)!=LUA_TSTRING)return fail(L,w,"source connection is not a string");
  }else if(w->op=="source_alive"){
   ServerPlayer*p=nullptr;for(auto*c:s.r.getAllPlayers(true))if(matchAIRound45SlotPlayer(L,-1,c)){p=c;break;}
   if(!p)return fail(L,w,"source alive cache contains a foreign player");
   lua_pop(L,1);lua_pushstring(L,p->objectName().toUtf8().constData());
  }else{
   if(!matchAIRound45NativeObjectMethods(s.r,L,-1,"Skill",{"objectName"}))return fail(L,w,"source skill metadata binding changed");
   lua_getfield(L,-1,"objectName");lua_pushvalue(L,-2);lua_call(L,1,1);lua_remove(L,-2);
   if(lua_type(L,-1)!=LUA_TSTRING)return fail(L,w,"source skill name is not a string");
  }
  size_t length=0;const char*name=lua_tolstring(L,-1,&length);if(length>4096)return fail(L,w,"source name exceeds finite bound");const QString text=QString::fromUtf8(name,int(length));names.append(text);lua_pop(L,1);
  if(w->op=="source_skills"){
   // The actual cached Skill came from getSkillList's exact Engine lookup.
   // Definition metadata is public; no SkillInstance state/ID is fabricated.
   const Skill*skill=Sanguosha->getSkill(text);if(!skill)return fail(L,w,"public cached skill definition disappeared");
   QJsonArray classes;for(const QMetaObject*m=skill->metaObject();m;m=m->superClass())classes.append(QString::fromLatin1(m->className()));
   QJsonArray events;if(const auto*trigger=dynamic_cast<const TriggerSkill*>(skill))for(const auto event:trigger->getTriggerEvents())events.append(int(event));
   definitions.append(QJsonObject{{"name",text},{"skill_classes",classes},{"lord_skill",skill->isLordSkill()},{"attached_lord_skill",skill->isAttachedLordSkill()},{"frequency",int(skill->getFrequency())},
    {"description",skill->getDescription()},{"limit_mark",skill->getLimitMark()},{"trigger_events",events},{"has_view_as_skill",ViewAsSkill::parseViewAsSkill(skill)!=nullptr}});
  }
 }
 if(!live(s)||!current(s.r,*w->q,w->viewer))return fail(L,w,"source cache continuity changed during result conversion");
 QJsonArray foreign;
 for(auto*p:s.r.getAllPlayers(true))if(p!=w->viewer)foreign.append(p->objectName());
 w->result={{"applicable",true},{"known",true},{"names",names},{"definitions",definitions},{"foreign_original_empty_maps",foreign}};return 0;
}
}
bool allowAIRound56ConnectionCacheWrite(Room&r,lua_State*L,const lua_TableObservation&e)noexcept{
 try{auto*s=find(r);return s&&permitted(*s,L,e);}catch(...){if(auto*s=find(r))s->retire();return false;}
}
bool acceptAIRound56ConnectionEntry(Room&r,lua_State*L,int arguments)noexcept{
 auto*w=active;if(!w||&w->s->r!=&r||w->s->L!=L||!live(*w->s)||arguments!=1||lua_gettop(L)<2
  ||r.roomRuntime()->lua().protectedCallDepth()!=w->depth)return false;
 const auto*f=AIRound45Inspect::value(L,-2);const auto*a=AIRound45Inspect::value(L,-1);
 if(!f||!ttislcf(f)||fvalue(f)!=run||!a||!ttislightuserdata(a)||pvalue(a)!=w)return false;
 w->entered=true;return true;
}
void clearAIRound56ConnectionCache(Room&r)noexcept{delete find(r);}
QJsonObject queryAIRound56ConnectionCache(Room&r,const AIRequest&q,ServerPlayer*viewer,const QJsonObject&proposal){
 // Applicability depends solely on the explicit public question, never on an
 // initial-origin bit or current private shape. Other routes retain behavior.
 const bool choulve=q.kind==AIRequest::PlayerChosen&&q.choiceOptions.reason=="choulve"&&q.choiceOptions.question=="askForPlayerChosen";
 const bool activate=q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY;
 if(!choulve&&!activate)return{{"applicable",false}};
 // This applicability adds no source/allocation authority: the independently
 // complete enclosing projection scope below remains mandatory for either kind.
 const QString op=proposal.value("op").toString();const bool alive=op=="source_alive";
 if((!alive&&op!="source_connections"&&op!="source_skills")||proposal.size()!=(alive?1:2)
  ||(!alive&&!proposal.value("player").isString()))return unknown("source cache proposal malformed");
 auto*rt=r.roomRuntime();if(!rt||!current(r,q,viewer))return unknown("source cache request stale");
 LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();
 if(!L||!inAIRound56ProjectionScope(r,L)||!hasAIRound56AsyncUniverse(r,L))return unknown("complete source request and allocation union unavailable");
 try{
  // This whole public roster is proved BEFORE choosing the requested player,
  // reading a cache, or allowing an original helper's private branch.
  const auto roster=r.getAllPlayers(true);ServerPlayer*player=nullptr;
  for(auto*p:roster){
   if(!p||typeid(*p)!=typeid(ServerPlayer)||p->getRoom()!=&r||!p->hasShownGeneral()||(p->getGeneral2()&&!p->hasShownGeneral2()))return unknown("public shown roster source domain unavailable");
   if(p!=viewer&&(!hasAIRound56PlayerEmptyMarksOrigin(*p)||!hasAIRound56PlayerEmptySkillsOrigin(*p)))return unknown("foreign original empty maps source unavailable");
   if(p->objectName()==proposal.value("player").toString())player=p;
  }
  if(!alive&&!player)return unknown("source cache player not in public roster");
  if(!lua_checkstack(L,8192)||!sources(r,L,viewer)){retireAIRound56Projection(r,L);return unknown("source cache full helper bindings changed");}
  auto*s=find(r);
  if(!s){
   // Two real producers: initial namespace absence, or actual observed fresh
   // filterEvent cache births. An empty/current-equal cache never supplies one.
   const void*space=nullptr,*qlist=nullptr,*connect=nullptr;quint64 resetSerial=0;
   const bool eventOrigin=captureAIRound56PlayLoopCacheOrigin(r,L,space,qlist,connect,resetSerial);
   if((!eventOrigin&&!hasAIRound56InitialConnectAbsence(r,L))
      ||!hasAIRound56InitialQlistKeyAbsence(r,L,"global_alive_players"))return unknown("original source cache absence unavailable");
   for(auto*p:roster)if(!hasAIRound56InitialQlistKeyAbsence(r,L,"skills_"+p->objectName()))return unknown("original skill-list cache absence unavailable");
   if(!eventOrigin&&!captureAIRound56InitialConnectionRoots(r,L,space,qlist))return unknown("original cache root custody unavailable");
   s=new State(r,L);s->space=space;s->qlist=qlist;s->connect=connect;
   if(!s->watch(s->space,Namespace)||!s->watch(s->qlist,Qlist)
      ||(eventOrigin&&(!resetSerial||!s->watch(s->connect,Connect)))){s->retire();return unknown("original cache root custody unavailable");}
  }
  if(!live(*s))return unknown("source cache lineage retired");
  // Source-proved full public roster, before requested-player execution. After
  // a genuine reset these keys are fresh; old cached arrays were detached.
  for(auto*p:roster)s->skillKeys.insert("skills_"+p->objectName());
  const QString key=alive?QStringLiteral("global_alive_players"):(op=="source_skills"?"skills_"+player->objectName():player->objectName());
  Call call{s,&q,viewer,player,op,key,rt->lua().protectedCallDepth()};
  struct Frame{Call*previous;explicit Frame(Call*c):previous(active){active=c;}~Frame(){active=previous;}}frame(&call);
  Stack restore(L);lua_pushcfunction(L,run);lua_pushlightuserdata(L,&call);
  if(LuaRuntime::protectedCall(L,1,0,0)!=LUA_OK){s->retire();return unknown(call.error?call.error:"original cache producer failed");}
  return call.result;
 }catch(...){retireAIRound56Projection(r,L);return unknown("source cache native allocation failure");}
}
