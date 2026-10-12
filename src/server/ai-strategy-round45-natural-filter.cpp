#include "ai-strategy-round45-natural-filter.h"
#include "ai-strategy-round45-natural-event.h"
#include "ai-strategy-round44-focus.h"
#include "ai-strategy-round45-code-seal.h"
#include "ai-strategy-round45-native-seal.h"
#include "structs.h"
// Independent finite family modules. No runtime callback/plugin registration.
bool certifyAIRound45PoxiFilterBody(Room&,lua_State*,int,const AIRound45NaturalFilterProof&,QString*);
bool certifyAIRound45XufaFilterBody(Room&,lua_State*,int,const AIRound45NaturalFilterProof&,QString*);
#include <QSet>
#include <QScopeGuard>
#include <cmath>
#include <cstring>
#include "ai-strategy-round45-lua-inspect.h"
namespace {
using namespace AIRound45Inspect;
struct Stack {lua_State*L;int top;explicit Stack(lua_State*s):L(s),top(lua_gettop(s)){}~Stack(){lua_settop(L,top);}};
bool plain(lua_State*L,int i){auto*t=table(L,i);return t&&!t->metatable;}
struct Source {const char*path;const char*sha;};
const Source sources[]={
 {"lua/ai/doudizhu-ai.lua","ffcf7174fe43e481d48c08d3482580a3a049915078e9f13756ace23e711aea2b"},
 {"lua/ai/smart-ai.lua","f92033319db3c6aac3504b4b6ddccf5edb4403fb120679b303dedfe673a3efe5"},
 {"lua/ai/AIgeneral-ai.lua","c4b0ee3cbbde098ac3a9e8cad6563fe63ace06e7816cb95dcadf0c5ce430e767"},
 {"lua/ai/wisdom-ai.lua","77e7fb67978c5e4f2bc31ca343329fe79866de92ec5acf71fa71f8ce12fba19b"},
 {"lua/ai/du-ai.lua","b0635b44fb918ed716a07f49928d4ec55d773062300a69797c0ced7b3db0c205"},
 {"lua/ai/yitian-ai.lua","88ce495f28c0a4622f99386cfca10063288ff15559c635e04476b384868ec126"},
 {"lua/ai/original-hegemony/heg-common-ai.lua","9ad128880b7876a95d022551533a8168ef924173a29d704af837cd5edb98ff7f"},
 {"lua/ai/chat-ai.lua","29e507189bc261e46ec9ab0fa327781833ccf9c3e7962521eddc471d478ba98e"},
 {"lua/utilities.lua","b76cdc44c0fec46f1e10d918c6e0676c5e301ed92cd5b0e35da882606a7d4d3a"},
};
struct Graph {
 Room&r;lua_State*L;QString failure;int sgs=0,self=0,actor=0,data=0;
 explicit Graph(Room&room,lua_State*state):r(room),L(state){}
 bool no(const QString&text){if(failure.isEmpty())failure=text;return false;}
 bool native(int index,const QString&path){return matchAIRound45NativeBinding(r,L,index,path)||no("native binding: "+path);}
 bool globalNative(const char*key){Stack s(L);globals(L);field(L,-1,key);return native(-1,QString::fromLatin1(key));}
 bool memberNative(const char*space,const char*key){Stack s(L);globals(L);field(L,-1,space);if(!plain(L,-1))return no("library table metamethod");field(L,-1,key);return native(-1,QString::fromLatin1(space)+'.'+key);}
 bool env(int index,const char*excluded=nullptr){
  index=lua_absindex(L,index);if(!lua_isfunction(L,index)||lua_iscfunction(L,index))return no("Lua closure required");
  for(int u=1;;++u){const char*name=lua_getupvalue(L,index,u);if(!name)break;
   bool good=false;
   if(!std::strcmp(name,"_ENV")){globals(L);good=lua_rawequal(L,-1,-2);lua_pop(L,1);}
   else if(excluded&&!std::strcmp(name,excluded))good=true; // proven never read under public guard
   lua_pop(L,1);if(!good)return no("unexpected reached closure upvalue");
  }return true;
 }
 bool code(int index,const char*path,int first,int last,const char*excluded=nullptr){return matchAIRound45PinnedFunction(r,L,index,QString::fromLatin1(path),first,last)&&env(index,excluded)||no(QString("source/binding mismatch: ")+path+':'+QString::number(first));}
 bool sgsFunction(const char*name,const char*path,int first,int last){Stack s(L);field(L,sgs,name);return code(-1,path,first,last);}
 // Resolve only native table __index chains. No Lua lookup or __index function
 // is executed, including middleclass instance dictionary/super tables.
 bool lookup(int object,const char*key){
  object=lua_absindex(L,object);QSet<const void*>seen;
  for(int n=0;n<16;++n){if(!table(L,object))return no("self lookup is not a table");
   if(seen.contains(lua_topointer(L,object)))return no("cyclic __index table");seen.insert(lua_topointer(L,object));
   field(L,object,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);
   if(!lua_getmetatable(L,object))return no(QString("missing member: ")+key);
   if(!plain(L,-1))return no("metatable has a metatable");field(L,-1,"__index");
   if(!table(L,-1))return no("function/absent __index outside finite instance lookup");object=lua_absindex(L,-1);
  }return no("instance lookup budget");
 }
 bool objectMethods(int index,const char*base,const QStringList&methods){return matchAIRound45NativeObjectMethods(r,L,index,QString::fromLatin1(base),methods)||no(QString("native object/method mismatch: ")+base);}
 bool selfRoom(){Stack s(L);if(!lookup(self,"room"))return false;return objectMethods(-1,"Room",{"getAlivePlayers","writeToConsole","outputEventStack"});}
 bool callbackRoot(int index){
  index=lua_absindex(L,index);
  if(!matchAIRound45PinnedFunction(r,L,index,"lua/ai/smart-ai.lua",344,375))return no("actual LuaAI callback code mismatch");
  int captured=0;
  for(int u=1;;++u){const char*name=lua_getupvalue(L,index,u);if(!name)break;
   if(!std::strcmp(name,"self")){if(captured||!table(L,-1))return no("callback self invalid");self=lua_absindex(L,-1);captured=1;}
   else if(!std::strcmp(name,"_ENV")){globals(L);const bool same=lua_rawequal(L,-1,-2);lua_pop(L,2);if(!same)return no("callback environment replaced");}
   else return no("callback unknown capture");
  }
  if(!captured)return no("callback self absent");
  {Stack s(L);if(!pushAIRound45NaturalFilterSelf(r,L,4)||!lua_rawequal(L,self,-1))return no("callback self differs from current native observer");}
  {Stack s(L);if(!lookup(self,"filterEvent")||!code(-1,"lua/ai/smart-ai.lua",2477,2867,"getCardIntention"))return false;}
  return selfRoom();
 }
 bool primitiveMetatables(){
  for(int k:{LUA_TNIL,LUA_TBOOLEAN,LUA_TNUMBER,LUA_TFUNCTION})
   if(primitiveMetatable(L,k))return no("primitive metamethod capability");
  return matchAIRound47NativeStringMetatable(r,L)
      ||no("string bootstrap metamethod bindings changed");
 }
 bool libraries(){
  for(const char*k:{"type","pcall","pairs","ipairs","tostring"})if(!globalNative(k))return false;
  for(const char*k:{"find","sub"})if(!memberNative("string",k))return false;
  for(const char*k:{"insert","concat"})if(!memberNative("table",k))return false;
  if(!memberNative("math","random"))return false;
  Stack s(L);globals(L);field(L,-1,"string");if(!plain(L,-1))return no("string library indirect");const int strings=lua_absindex(L,-1);
  for(const auto&row:{QPair<const char*,QPair<int,int>>{"split",{77,88}}, {"contains",{91,93}}, {"startsWith",{164,168}}}){
   field(L,strings,row.first);
   if(!std::strcmp(row.first,"contains")) {
    // Installed addFunction keeps Lua pattern semantics; utilities uses plain
    // search. Both source bodies close the same native string.find effect graph.
    if(!(matchAIRound45PinnedFunction(r,L,-1,"lua/utilities.lua",91,93)
       ||matchAIRound45PinnedFunction(r,L,-1,"extensions/addFunction.lua",1557,1559))
       ||!env(-1))return no("string.contains source differs");
   } else if(!code(-1,"lua/utilities.lua",row.second.first,row.second.second))return false;
   lua_pop(L,1);
  }return primitiveMetatables();
 }
 bool qlist(){
  Stack s(L);field(L,sgs,"qlist");const int f=lua_absindex(L,-1);
  if(!matchAIRound45PinnedFunction(r,L,f,"lua/utilities.lua",19,35))return no("qlist source");
  for(int u=1;;++u){const char*name=lua_getupvalue(L,f,u);if(!name)break;bool good=false;
   if(!std::strcmp(name,"_ENV")){globals(L);good=lua_rawequal(L,-1,-2);lua_pop(L,1);}
   else if(!std::strcmp(name,"qlist_userdata_iterator"))good=code(-1,"lua/utilities.lua",7,11);
   else if(!std::strcmp(name,"qlist_table_iterator"))good=code(-1,"lua/utilities.lua",13,17);
   lua_pop(L,1);if(!good)return no("qlist iterator capture");
  }
  return matchAIRound45NativeClassMethod(r,L,"SPlayerList","length")
      &&matchAIRound45NativeClassMethod(r,L,"SPlayerList","at")||no("SPlayerList iterator native methods");
 }
 bool namespaceRead(const char*key){
  Stack s(L);field(L,sgs,key);if(!lua_isnil(L,-1))return true;
  return matchAIRound45NativeNamespaceRead(r,L,"sgs",QString::fromLatin1(key))||no(QString("unproved namespace nil lookup: ")+key);
 }
 bool prefix(){
  for(const char*k:{"aiHandCardVisible","ai_humanized","qlist_cache_gen"})if(!namespaceRead(k))return false;
  if(!sgsFunction("invalidate_qlist_cache","lua/ai/smart-ai.lua",433,441)
     ||!sgsFunction("flushDeferredDeleteCards","lua/ai/smart-ai.lua",5481,5486))return false;
  for(const char*k:{"qlist_cache","ai_connect_cache","ai_active_recommends_cache","ai_crossbow_cache","ai_slash_query_cache","qlist_cache_gen","_deferred_delete_cards"})
   if(!matchAIRound45NativeNamespaceWrite(r,L,"sgs",QString::fromLatin1(k)))return no(QString("namespace setter: ")+k);
  {Stack s(L);field(L,sgs,"qlist_cache_gen");if(!lua_isnil(L,-1)&&(lua_type(L,-1)!=LUA_TNUMBER||!std::isfinite(lua_tonumber(L,-1))))return no("cache generation nonnumeric");}
  {Stack s(L);field(L,sgs,"filterData");if(!plain(L,-1))return no("filterData write metamethod/absence");}
  {Stack s(L);field(L,sgs,"_deferred_delete_cards");if(!plain(L,-1))return no("delete queue nonplain");const int list=lua_absindex(L,-1);
   bool end=false;for(int i=1;i<=4096;++i){rawGeti(L,list,i);if(lua_isnil(L,-1)){end=true;break;}
    if(!objectMethods(-1,"Card",{"deleteLater"}))return false;lua_pop(L,1);
   }if(!end)return no("delete queue budget");
  }
  // Both diagnostic outcomes remain in the union, regardless of the current
  // private aiHandCardVisible/turncount branch. No read-outcome-based pruning.
  {Stack s(L);field(L,sgs,"turncount");if(lua_type(L,-1)!=LUA_TNUMBER||!std::isfinite(lua_tonumber(L,-1)))return no("turncount nonnumeric");}
  if(!memberNative("io","open")||!matchAIRound45NativeFileMethods(r,L,{"write","close"}))return no("diagnostic file native methods");
  return objectMethods(data,"QVariant",{"toString"})
      &&objectMethods(actor,"ServerPlayer",{"getLogName"})
      &&matchAIRound45NativeDerivedMethods(r,L,"ServerPlayer",{"getLogName","getState","speak"})
      ||no("event/player native leaves");
 }
 bool enums(){
  for(const char*k:{"AskForPeaches","AskForRetrial","BeforeGameOverJudge","CardEffect","CardsMoveOneTime","ChoiceMade","DamageInflicted","Death","GameReady","HpRecover","PreCardUsed","Revived","RoundStart","ShowCards","StartJudge","TargetSpecified"}){
   Stack s(L);field(L,sgs,k);if(!matchAIRound45NativeConstant(r,L,-1,QStringLiteral("sgs.")+k))return no(QString("event enum replaced: ")+k);
  }return true;
 }
 bool chat(){
  Stack s(L);globals(L);const int g=lua_absindex(L,-1);field(L,g,"AIChat");if(!code(-1,"lua/ai/chat-ai.lua",3,6))return false;lua_pop(L,1);
  field(L,g,"global_delay");if(lua_type(L,-1)!=LUA_TNUMBER||!std::isfinite(lua_tonumber(L,-1)))return no("chat delay nonnumeric");
  return qlist(); // speak exact binding's native QString fails structured parser before reentry
 }
 bool faqiCallback(int f){
  f=lua_absindex(L,f);
  struct Row{const char*path;int first,last;const char*unused;};
  const Row rows[]={{"lua/ai/AIgeneral-ai.lua",26,45,nullptr},{"lua/ai/wisdom-ai.lua",392,401,nullptr},
   {"lua/ai/du-ai.lua",59,64,nullptr},{"lua/ai/du-ai.lua",582,604,nullptr},{"lua/ai/yitian-ai.lua",1037,1042,nullptr},
   {"lua/ai/original-hegemony/heg-common-ai.lua",257,268,"public_card_lack"},{"lua/ai/chat-ai.lua",528,533,nullptr}};
  for(const auto&row:rows)if(matchAIRound45PinnedFunction(r,L,f,row.path,row.first,row.last)){
   if(!env(f,row.unused))return false;return row.first==528?chat():true;
  }return no("unknown ChoiceMade callable sibling");
 }
 bool eventRegistry(int event,bool allowFaqi){
  Stack s(L);field(L,sgs,"ai_event_callback");if(!plain(L,-1))return no("event callback registry metamethod");
  rawGeti(L,-1,event);
  if(!lua_istable(L,-1))return true; // Source type guard skips every non-table.
  if(!plain(L,-1))return no("event row metatable");
  const int row=lua_absindex(L,-1);lua_pushnil(L);int size=0;
  while(next(L,row)){if(++size>4096)return no("event row budget");
   if(lua_isfunction(L,-1)&&(!allowFaqi||!faqiCallback(-1)))return no("unproved event-row callable sibling");
   lua_pop(L,1);
  }return true;
 }
 bool faqiRegistry(){
  Stack s(L);if(!eventRegistry(int(ChoiceMade),true))return false;
  field(L,sgs,"ai_choicemade_filter");if(!plain(L,-1))return no("choice filter registry metamethod");
  field(L,-1,"AGChosen");if(lua_istable(L,-1)){if(!plain(L,-1))return no("AGChosen table metatable");field(L,-1,"faqi");}
  // Fixed source has no AGChosen.faqi callable. Do not infer absence: inspect
  // the current complete raw table, and refuse any added function.
  if(lua_isfunction(L,-1))return no("uncertified dynamic AGChosen callable");return true;
 }
};
}
bool registerAIRound45NaturalFilterSources(Room&r,lua_State*L,QString*reason){
 for(const auto&s:sources)if(!registerAIRound45PinnedSource(r,L,s.path,QByteArray(s.sha),reason))return false;
 return true;
}
bool certifyAIRound45NaturalFilterGraph(Room&r,lua_State*L,int nargs,QString*reason){
 if(reason)reason->clear();AIRound45NaturalFilterProof proof;
 if(!readAIRound45NaturalFilterEntry(r,L,nargs,&proof)){if(reason)*reason="no authenticated native filterEvent frame";return false;}
 // The fixed filter's captured getCardIntention is called only in these three
 // exact switch arms. No family below admits them; make that source premise
 // explicit before ignoring the otherwise-unreached capture in callbackRoot.
 if(proof.event==int(TargetSpecified)||proof.event==int(DamageInflicted)||proof.event==int(HpRecover)){
  if(reason)*reason="natural filter intention-helper arm outside finite event domain";return false;
 }
 // The trusted bootstrap/central observer prepares this capacity. Admission
 // never grows the Lua stack or invokes a Lua allocator/finalizer.
 if(!AIRound45Inspect::capacity(L,4096)){if(reason)*reason="preallocated native inspection stack unavailable";return false;}
 Stack stack(L);Graph g(r,L);const int callback=lua_gettop(L)-nargs;
 g.actor=callback+3;g.data=callback+4;
 globals(L);if(!plain(L,-1)){if(reason)*reason="global table metamethod";return false;}
 field(L,-1,"sgs");g.sgs=lua_absindex(L,-1);
 bool accepted=table(L,g.sgs)&&g.callbackRoot(callback)&&g.libraries()&&g.prefix()&&g.enums()
     &&aiRound44FocusContinuityNoop(r,L,nargs);
 if(accepted){
  if(proof.event==int(ChoiceMade)&&proof.faqiAGFormat)accepted=g.faqiRegistry();
  else switch(proof.event){
  // These enum values have no case in the fixed filterEvent switch. All
  // callable event-row siblings must still be proved; none are silently skipped.
  case EventSkillWillInvoke:case EventSkillPay:case EventSkillTargetConfirming:
  case EventSkillInvoking:case EventSkillEffect:case EventSkillEffectTarget:
  case EventSkillEffectFinished:case CardAsked:case MarkChange:case MarkChanged:
   accepted=g.eventRegistry(proof.event,false);break;
  default:{
   // Each delegate is a complete body+registry proof, not a private branch
   // trace or a saved-plan replacement. Missing services remain fail-closed.
   QString poxi,xufa;
   // Poxi's body delegate does not own arbitrary sibling registrations. It
   // receives a row already proved callback-free; a populated row needs a
   // future complete sibling validator, never an observed-branch exemption.
   const QString earlier=g.failure;
   accepted=g.eventRegistry(proof.event,false)
         &&certifyAIRound45PoxiFilterBody(r,L,g.self,proof,&poxi);
   if(!accepted){g.failure=earlier;
    // Xufa's finite body validator explicitly owns its full move/choice row.
    accepted=certifyAIRound45XufaFilterBody(r,L,g.self,proof,&xufa);
   }
   if(!accepted)g.no("family body/registry union unavailable: "+poxi+"; "+xufa);
   break;
  }}
 }
 // Async finalizer/debug-hook protection is a mandatory premise checked by
 // the sole native continuity entry observer before calling this module. This
 // return certifies only the finite graph; it is not independently a lease.
 if(!accepted&&reason)*reason=g.failure.isEmpty()?QStringLiteral("natural filter graph prerequisite unavailable"):g.failure;
 return accepted;
}
