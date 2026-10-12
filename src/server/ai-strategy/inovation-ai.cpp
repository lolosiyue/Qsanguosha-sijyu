#include "ai-strategy/inovation-ai.h"

// ---- inovation-ai-common ----
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
namespace {
bool ownPlay(const AIRequest &r,const ServerPlayer *p)
{
    return p && p->getRoom() && r.viewerObjectName==p->objectName()
        && r.stateRevision==p->getRoom()->roomRuntime()->stateRevision()
        && r.kind==AIRequest::Activate && r.reason==CardUseStruct::CARD_USE_REASON_PLAY
        && p->hasSkill("jizhanshiz");
}
}
int aiJizhanshizPrototype(const AIRequest &r,const ServerPlayer *p,
                              const QString &skill,const QString &prototype)
{
    // Fixed AI570 constructs the zero-cost historical SkillCard. This is only
    // the source prototype declaration; real V2 BaonuEffects, phase history,
    // target filters and final selected admission remain the native pipeline's.
    return ownPlay(r,p) && skill=="jizhanshiz" && prototype=="JizhanCard" ? 0 : -1;
}
bool emitAIJizhanshizWarning(Room &room,const AIRequest &r,const ServerPlayer *p,
                                 const QString &source,const QString &message)
{
    if(!ownPlay(r,p) || p->getRoom()!=&room || source!="inovation.jizhanshiz"
        || message!="here???") return false;
    // Exact reached source writeToConsole sink, after use.card assignment and
    // before use.to replacement. No game state or private value is exported.
    room.output(message);
    qWarning("%s",qPrintable(message));
    return true;
}

// ---- inovation-ai-goutong ----
#include "ai-strategy/lua-seal.h"
#include "ai-strategy/runtime.h"
#include "lua-runtime.h"
#include <QObject>
#include <QPointer>
#include <QHash>
#include <memory>
#include "ai-strategy/lua-raw-inspect.h"
namespace {
constexpr const char*name="AI.R45.GoutongMove";
struct Input {QPointer<ServerPlayer>owner;quint64 decision=0,revision=0;std::shared_ptr<const CardsMoveOneTimeStruct>move;bool attempted=false,reached=false;};
class Ledger final:public QObject{public:explicit Ledger(Room&r):QObject(&r){setObjectName(name);}QHash<QString,Input>rows;};
Ledger*ledger(Room&r,bool create=false){for(auto*c:r.children())if(c->objectName()==name)return static_cast<Ledger*>(c);return create?new Ledger(r):nullptr;}
struct Stack{lua_State*L;int top;~Stack(){lua_settop(L,top);}};
ServerPlayer*rosterPlayer(Room&r,const Player*p){for(auto*v:r.getAllPlayers(true))if(static_cast<const Player*>(v)==p)return v;return nullptr;}
bool current(Room&r,const AIRequest&q,ServerPlayer*p){return p&&p->getRoom()==&r&&q.viewerObjectName==p->objectName()&&q.kind==AIRequest::SkillInvoke&&q.choiceOptions.reason=="goutong"&&aiRangeRequestCurrent(r,q,p);}
QJsonObject fail(const char*why){return{{"known",false},{"reason",QString::fromLatin1(why)}};}
struct Write{Room*room;const AIRequest*request;ServerPlayer*owner;const CardsMoveOneTimeStruct*move;const void*self;Player*from=nullptr;Player*to=nullptr;};
bool sameSelf(Write*w,lua_State*L){Stack restore{L,lua_gettop(L)};return current(*w->room,*w->request,w->owner)&&pushAIActualAISelf(*w->room,w->owner,L)&&lua_topointer(L,-1)==w->self;}
int assign(lua_State*L){
 if(!lua_checkstack(L,2048))return luaL_error(L,"Goutong assignment stack unavailable");
 auto*w=static_cast<Write*>(lua_touserdata(L,1));if(!w)return luaL_error(L,"Goutong source write context absent");
 if(!pushAIGoutongMove(L,*w->move))return luaL_error(L,"Goutong authentic native move copy unavailable");
 const int move=lua_absindex(L,-1);lua_pushliteral(L,"move");const int key=lua_absindex(L,-1);
 if(!sameSelf(w,L))return luaL_error(L,"Goutong actual owner binding changed before source prefix");
 AILuaInspect::globals(L);const int globals=lua_absindex(L,-1);AILuaInspect::field(L,globals,"move");const bool exists=!lua_isnil(L,-1);lua_pop(L,1);
 if(!exists&&lua_getmetatable(L,globals)){
  AILuaInspect::field(L,-1,"__newindex");const bool plain=lua_isnil(L,-1);lua_pop(L,2);if(!plain)return luaL_error(L,"Goutong global assignment has unknown __newindex");
 }
 lua_pushvalue(L,key);lua_pushvalue(L,move);lua_settable(L,globals);
 if(!sameSelf(w,L))return luaL_error(L,"Goutong source global prefix committed before binding changed");
 AILuaInspect::field(L,globals,"move");if(!lua_rawequal(L,move,-1))return luaL_error(L,"Goutong global replaced during assignment");
 return 0;
}
}
void clearAIGoutong(Room&r){delete ledger(r);}
void projectAIGoutong(AIRequest&q,Room&r,ServerPlayer*p,const QVariant&data){
 if(!current(r,q,p))return;auto*l=ledger(r,true);l->rows.remove(p->objectName());
 if(!p->hasSkill("goutong")||!current(r,q,p)||!data.canConvert<CardsMoveOneTimeStruct>())return;
 auto move=std::make_shared<CardsMoveOneTimeStruct>(data.value<CardsMoveOneTimeStruct>());
 if(!move->from||!move->to||move->from==move->to||(p!=move->from&&p!=move->to)
  ||(!move->from_places.contains(Player::PlaceHand)&&!move->from_places.contains(Player::PlaceEquip))
  ||(move->to_place!=Player::PlaceHand&&move->to_place!=Player::PlaceEquip))return;
 for(Player*endpoint:{move->from,move->to})if(endpoint&&!rosterPlayer(r,endpoint))return;
 // Only public endpoints enter the isolated view. Card IDs/reason/private open
 // flags stay in the native copy and actual game-VM global assigned at source.
 Input input;input.owner=p;input.decision=q.decisionId;input.revision=q.stateRevision;input.move=std::move(move);l->rows.insert(p->objectName(),input);
}
QJsonObject writeAIGoutongMove(Room&r,const AIRequest&q,ServerPlayer*p){
 if(!current(r,q,p)||!p->hasSkill("goutong")||!current(r,q,p))return fail("Goutong exact current owner question unavailable");
 auto*l=ledger(r);if(!l)return fail("Goutong actual native input not captured");auto i=l->rows.find(p->objectName());
 if(i==l->rows.end()||i->owner!=p||i->decision!=q.decisionId||i->revision!=q.stateRevision||i->attempted||!i->move)return fail("Goutong source input is stale or already consumed");
 const auto value=i->move;i->attempted=true;
 auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();if(!L||!lua_checkstack(L,2048))return fail("Goutong game VM unavailable");Stack restore{L,lua_gettop(L)};
 if(!pushAIActualAISelf(r,p,L))return fail("Goutong actual own LuaAI unavailable");const void*self=lua_topointer(L,-1);
 Write write{&r,&q,p,value.get(),self};lua_pushcfunction(L,assign);lua_pushlightuserdata(L,&write);
 if(LuaRuntime::protectedCall(L,1,0,0)!=LUA_OK)return fail("Goutong native global source assignment failed; reached prefix retained");
 if(!sameSelf(&write,L))return fail("Goutong current session changed after source global assignment");
 l=ledger(r);if(!l)return fail("Goutong input reset during source assignment");i=l->rows.find(p->objectName());
 if(i==l->rows.end()||i->move!=value||i->decision!=q.decisionId||i->revision!=q.stateRevision)return fail("Goutong input replaced during source assignment");
 i->reached=true;return{{"known",true}};
}

QJsonObject readAIGoutongMove(Room&r,const AIRequest&q,ServerPlayer*p){
 if(!current(r,q,p))return fail("Goutong source read is not the current owner question");
 auto*l=ledger(r);if(!l)return fail("Goutong reached source assignment absent");const auto i=l->rows.constFind(p->objectName());
 if(i==l->rows.cend()||i->owner!=p||i->decision!=q.decisionId||i->revision!=q.stateRevision||!i->reached)return fail("Goutong source read has no current reached prefix");
 auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();if(!L||!lua_checkstack(L,2048))return fail("Goutong game VM unavailable");Stack restore{L,lua_gettop(L)};
 if(!pushAIActualAISelf(r,p,L))return fail("Goutong actual own LuaAI unavailable");const void*self=lua_topointer(L,-1);
 AILuaInspect::globals(L);AILuaInspect::field(L,-1,"move");
 if(lua_isnil(L,-1)){
  if(AILuaInspect::primitiveMetatable(L,LUA_TNIL))return fail("Goutong nil global has an unproved primitive metatable");
  const int globals=lua_absindex(L,-2);if(lua_getmetatable(L,globals)){AILuaInspect::field(L,-1,"__index");if(!lua_isnil(L,-1))return fail("Goutong missing global has unknown __index");}
  Write context{&r,&q,p,nullptr,self};if(!sameSelf(&context,L))return fail("Goutong nil source read binding changed");
  return{{"known",true},{"source_nil",true}};
 }
 Player*from=nullptr,*to=nullptr;
 if(!readAIGoutongEndpoints(L,-1,from,to)||!matchAINativeClassMethod(r,L,"Player","objectName"))return fail("Goutong live source global or property binding changed");
 Write context{&r,&q,p,nullptr,self};if(!sameSelf(&context,L))return fail("Goutong source read binding changed");
 QJsonObject result{{"known",true}};
 for(const auto&field:QList<QPair<QString,Player*>>{{"from",from},{"to",to}}){
  auto*target=rosterPlayer(r,field.second);if(field.second&&!target)return fail("Goutong live endpoint is not in current public roster");
  result.insert(field.first,target?QJsonValue(target->objectName()):QJsonValue(false));
 }
 return result;
}

// ---- inovation-ai-inovation-2 ----
#include <QSet>
#include <QWeakPointer>
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
QJsonObject inovationAiInovation2Fail(const char*m){return{{"known",false},{"reason",QString::fromLatin1(m)}};}
bool inovationAiInovation2Current(Room&r,const AIRequest&q,ServerPlayer*p){
 return p&&p->getRoom()==&r&&q.viewerObjectName==p->objectName()&&aiRangeRequestCurrent(r,q,p);
}
struct InovationAiInovation2Stack{lua_State*L;int top;explicit InovationAiInovation2Stack(lua_State*l):L(l),top(lua_gettop(l)){}~InovationAiInovation2Stack(){lua_settop(L,top);}};
bool readField(lua_State*L,int index,const char*key){
 lua_pushvalue(L,index);QSet<const void*>seen;
 for(int depth=0;depth<16;++depth){
  if(!lua_istable(L,-1)||seen.contains(lua_topointer(L,-1)))return false;
  seen.insert(lua_topointer(L,-1));const int table=lua_absindex(L,-1);
  AILuaInspect::field(L,table,key);if(!lua_isnil(L,-1))return true;
  lua_pop(L,1);if(!lua_getmetatable(L,table)){lua_pushnil(L);return true;}
  AILuaInspect::field(L,-1,"__index");if(lua_isnil(L,-1))return true;
  if(!lua_istable(L,-1))return false;
 }
 return false;
}
bool fixedAGScalar(Room&r){
 auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();
 if(!L||!lua_checkstack(L,128))return false;InovationAiInovation2Stack stack(L);
 AILuaInspect::globals(L);if(!readField(L,-1,"sgs")||!lua_istable(L,-1))return false;
 const int sgs=lua_absindex(L,-1);
 // Prove the actual fixed callback, without invoking it. The full source file
 // was independently pinned at native trusted bootstrap by the existing seal.
 if(!readField(L,sgs,"ai_skill_askforag")||!lua_istable(L,-1)||!readField(L,-1,"kaiqi")
  ||!matchAIPinnedFunction(r,L,-1,"lua/ai/inovation-ai.lua",1411,1424))return false;
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
QJsonObject queryAIInovationKaiqi(Room&r,const AIRequest&q,ServerPlayer*p,const QString&op,const QString&name){
 if(!inovationAiInovation2Current(r,q,p))return inovationAiInovation2Fail("Inovation current request unavailable");
 if(q.choiceOptions.reason=="ningju"){
  if(q.kind!=AIRequest::CardChosen||q.choiceOptions.playerNames!=QStringList{p->objectName()}
   ||q.choiceOptions.choices!=QStringList{"he"}||!name.isEmpty()
   ||(q.handlingMethod!=Card::MethodNone&&q.handlingMethod!=Card::MethodDiscard))return inovationAiInovation2Fail("Ningju requires the actual self CardChosen question");
  auto*s=sequence(r,q,p);
  if(op=="ningju_tag"){
   const QVariant value=r.getTag("ningju_color");
   // Current fixed V2 never sets this key. Legacy NingjuCard::use is not the
   // V2 product; a restored/externally written value has no current disclosure.
   if(value.isValid())return inovationAiInovation2Fail("Ningju nonempty legacy tag has no current public writer witness");
   s->tagRead=true;return{{"known",true},{"value",value.toString()}};
  }
  if((op!="ningju_red"&&op!="ningju_black")||!s->tagRead||s->diagnosed)return inovationAiInovation2Fail("Ningju diagnostic outside reached source tag read");
  s->diagnosed=true;
  const QString message=p->getGeneralName()+(op=="ningju_red"?" need to use red":" need to use black");
  r.output(message);qWarning("%s",qPrintable(message));
  if(!inovationAiInovation2Current(r,q,p))return inovationAiInovation2Fail("Ningju diagnostic changed current request; source prefix retained");
  return{{"known",true}};
 }
 if(q.choiceOptions.reason!="kaiqi")return inovationAiInovation2Fail("Inovation operation has no matching question");
 if(op=="kaiqi_ag_scalar"){
  if(q.kind!=AIRequest::AmazingGrace||!name.isEmpty()||!fixedAGScalar(r)||!inovationAiInovation2Current(r,q,p))return inovationAiInovation2Fail("Kaiqi fixed scalar initialization is not attested");
  return{{"known",true},{"value",QString()}};
 }
 if(q.kind!=AIRequest::PlayerChosen||!p->hasSkill("kaiqi")||!inovationAiInovation2Current(r,q,p))return inovationAiInovation2Fail("Kaiqi requires the current owner PlayerChosen question");
 auto*s=sequence(r,q,p);
 if(op=="kaiqi_self_flag"){
  if(!name.isEmpty()||s->flagRead)return inovationAiInovation2Fail("Kaiqi owner flag read is not the first source read");
  s->flagRead=true;s->flagValue=p->hasFlag("kaiqi_self_used");
  return{{"known",true},{"value",s->flagValue}};
 }
 if(!s->flagRead||s->written)return inovationAiInovation2Fail("Kaiqi write has no unmatched source read");
 ServerPlayer*target=nullptr;QString flag;
 if(op=="kaiqi_set_self"&&!s->flagValue&&name.isEmpty()){target=p;flag="kaiqi_self_used";}
 else if(op=="kaiqi_set_target"&&s->flagValue&&q.choiceOptions.playerNames.contains(name)){
  target=r.findPlayerByObjectName(name);flag="kaiqi_used";
 }
 if(!target||target->getRoom()!=&r)return inovationAiInovation2Fail("Kaiqi source-selected flag target invalid");
 s->written=true; // No retry/reselection after a reached native write/error.
 AIKaiqiFlagPermit permit(r,q,p,target);QString error;
 if(!writeAIKaiqiFlag(r,q,p,target,flag,permit,&error))return{{"known",false},{"reason",error}};
 return{{"known",true}};
}

// ---- inovation-ai-inovation-providers ----
#include "lua.hpp"
#include <QJsonObject>
#include <QJsonArray>
#include <cstring>
namespace {
constexpr const char *ledgerName="AI.R45.InovationProviders";
constexpr const char *source="lua/ai/inovation-ai.lua";
struct Anchor {const char*name;int first,last;};
const Anchor methods[]={
 {"willSkipPlayPhase",1450,1486},{"willSkipDrawPhase",1488,1513},
 {"resetCards",1515,1523},{"shouldUseRende",1525,1583},
 {"getJijiangSlashNum",1585,1595},{"isValuableCard",1598,1612},
 {"getWoundedFriend",1614,1672},{"hasLiyuEffect",1674,1728},
 {"getLijianCard",1730,1773},{"findLijianTarget",1775,2033},
 {"canUseJieyuanDecrease",2035,2047}};
struct InovationAiInovationProvidersStack {lua_State*L;int n;explicit InovationAiInovationProvidersStack(lua_State*s):L(s),n(lua_gettop(s)){}~InovationAiInovationProvidersStack(){lua_settop(L,n);}};
void raw(lua_State*L,int i,const char*k){AILuaInspect::field(L,i,k);}
bool environment(lua_State*L,int index){
 index=lua_absindex(L,index);bool saw=false;
 for(int n=1;;++n){const char*key=lua_getupvalue(L,index,n);if(!key)break;
  if(strcmp(key,"_ENV")||saw){lua_pop(L,1);return false;}
  AILuaInspect::globals(L);const bool same=lua_rawequal(L,-1,-2);lua_pop(L,2);
  if(!same)return false;saw=true;
 }
 return true; // resetCards has no global reads and consequently no _ENV.
}
struct GameBinding {
 RoomRuntime*runtime;std::unique_ptr<LuaRuntime::Binding> binding;
 explicit GameBinding(Room&r):runtime(r.roomRuntime()){
  if(runtime&&runtime->lua().rawState())binding.reset(new LuaRuntime::Binding(runtime->lua(),false));
 }
};
bool pushCallback(lua_State*L,LuaAI*ai){
 if(!ai||!ai->callback.isValid()||ai->callback.state()!=L)return false;
 AILuaInspect::rawGeti(L,LUA_REGISTRYINDEX,ai->callback.registryRef());
 return lua_isfunction(L,-1)&&!lua_iscfunction(L,-1);
}
struct Row {QPointer<LuaAI> native;const void*callback=nullptr;const void*self=nullptr;const void*cls=nullptr;const void*dict=nullptr;const void*dictMeta=nullptr;QHash<QString,const void*> providers;};
class InovationAiInovationProvidersLedger final:public QObject{
public: explicit InovationAiInovationProvidersLedger(Room&r):QObject(&r){setObjectName(ledgerName);}
 lua_State*state=nullptr;quint64 generation=0;bool initialized=false,valid=false;
 const void*cls=nullptr;const void*dict=nullptr;const void*dictMeta=nullptr;
 QHash<QString,const void*> providers;QHash<const ServerPlayer*,Row> rows;
};
InovationAiInovationProvidersLedger*get(Room&r,bool create=false){for(auto*c:r.children())if(c->objectName()==ledgerName)return static_cast<InovationAiInovationProvidersLedger*>(c);return create?new InovationAiInovationProvidersLedger(r):nullptr;}
bool inovationAiInovationProvidersCurrent(Room&r,InovationAiInovationProvidersLedger*l,lua_State*&L){
 auto*runtime=r.roomRuntime();L=runtime?runtime->lua().state():nullptr;
 return L&&l&&l->valid&&l->state==L&&l->generation==runtime->lua().generation();
}
bool classShape(lua_State*L,InovationAiInovationProvidersLedger*l,int&dict){
 AILuaInspect::globals(L);raw(L,-1,"SmartAI");if(!lua_istable(L,-1)||lua_topointer(L,-1)!=l->cls)return false;
 raw(L,-1,"__instanceDict");if(!lua_istable(L,-1)||lua_topointer(L,-1)!=l->dict)return false;dict=lua_absindex(L,-1);
 const void*mt=nullptr;if(lua_getmetatable(L,dict)){mt=lua_topointer(L,-1);lua_pop(L,1);}if(mt!=l->dictMeta)return false;
 raw(L,dict,"__index");const bool same=lua_rawequal(L,-1,dict);lua_pop(L,1);return same;
}
bool callbackSelf(Room&r,lua_State*L,LuaAI*ai,const ServerPlayer*p,int&self){
 if(!ai||!pushCallback(L,ai)||!matchAIPinnedFunction(r,L,-1,"lua/ai/smart-ai.lua",344,375))return false;
 const int fn=lua_absindex(L,-1);bool sawSelf=false,sawEnv=false;
 for(int n=1;;++n){const char*k=lua_getupvalue(L,fn,n);if(!k)break;
  if(!strcmp(k,"self")){
   if(sawSelf||!lua_istable(L,-1))return false;sawSelf=true;self=lua_absindex(L,-1);
  }else if(!strcmp(k,"_ENV")){
   if(sawEnv)return false;sawEnv=true;AILuaInspect::globals(L);const bool same=lua_rawequal(L,-1,-2);lua_pop(L,2);if(!same)return false;
  }else return false;
 }
 if(!sawSelf||!sawEnv)return false;
 raw(L,self,"player");const bool own=aiInovationPlayerMatches(L,-1,p);lua_pop(L,1);
 raw(L,self,"room");const bool room=aiInovationRoomMatches(L,-1,&r);lua_pop(L,1);return own&&room;
}
}
void initializeAIInovationProviders(Room&r){
 GameBinding binding(r);
 auto*runtime=r.roomRuntime();auto*L=runtime?runtime->lua().state():nullptr;if(!L)return;
 if(!lua_checkstack(L,128))return;
 auto*l=get(r,true);if(l->initialized&&l->state==L&&l->generation==runtime->lua().generation())return;
 l->initialized=true;l->valid=false;l->rows.clear();l->providers.clear();l->state=L;l->generation=runtime->lua().generation();
 if(!registerAIPinnedSource(r,L,source,QByteArray("7fb80aef6edb15d836f9c0afeb7808919fa737d05c396a0c472844638c2494c4")))return;
 // Fixed current game SmartAI (including accepted callback diagnostics); the
 // independently compiled file is NEVER executed by this metadata helper.
 if(!registerAIPinnedSource(r,L,"lua/ai/smart-ai.lua",QByteArray("f92033319db3c6aac3504b4b6ddccf5edb4403fb120679b303dedfe673a3efe5")))return;
 InovationAiInovationProvidersStack stack(L);AILuaInspect::globals(L);raw(L,-1,"SmartAI");if(!lua_istable(L,-1))return;l->cls=lua_topointer(L,-1);
 raw(L,-1,"__instanceDict");if(!lua_istable(L,-1))return;const int dict=lua_absindex(L,-1);l->dict=lua_topointer(L,dict);
 l->dictMeta=nullptr;if(lua_getmetatable(L,dict)){l->dictMeta=lua_topointer(L,-1);lua_pop(L,1);}
 raw(L,dict,"__index");if(!lua_rawequal(L,-1,dict))return;lua_pop(L,1);
 for(const auto&m:methods){raw(L,dict,m.name);
  if(matchAIPinnedFunction(r,L,-1,source,m.first,m.last)&&environment(L,-1))l->providers.insert(m.name,lua_topointer(L,-1));
  lua_pop(L,1);
 }
 l->valid=true;
 // Existing native objects, if initialization follows constructor creation.
 for(auto*p:r.getAllPlayers(true))captureAIInovationProviders(r,p,p->getSmartAI());
}
void captureAIInovationProviders(Room&r,ServerPlayer*p,AI*created){
 GameBinding binding(r);
 auto*l=get(r);lua_State*L=nullptr;if(!p||p->getRoom()!=&r||!inovationAiInovationProvidersCurrent(r,l,L)||!lua_checkstack(L,128))return;l->rows.remove(p);
 auto*ai=dynamic_cast<LuaAI*>(created);InovationAiInovationProvidersStack stack(L);int dict=0,self=0;
 if(!classShape(L,l,dict)||!callbackSelf(r,L,ai,p,self))return;
 if(!lua_getmetatable(L,self)||!lua_rawequal(L,-1,dict))return;lua_pop(L,1);
 raw(L,self,"class");const bool same=lua_topointer(L,-1)==l->cls;lua_pop(L,1);if(!same)return;
 Row row;row.native=ai;row.self=lua_topointer(L,self);row.cls=l->cls;row.dict=l->dict;row.dictMeta=l->dictMeta;
 if(!pushCallback(L,ai))return;row.callback=lua_topointer(L,-1);lua_pop(L,1);
 for(const auto&m:methods){raw(L,self,m.name);if(lua_isnil(L,-1)){lua_pop(L,1);raw(L,dict,m.name);}
  if(l->providers.contains(m.name)&&lua_topointer(L,-1)==l->providers.value(m.name)
     &&matchAIPinnedFunction(r,L,-1,source,m.first,m.last)&&environment(L,-1))row.providers.insert(m.name,lua_topointer(L,-1));
  lua_pop(L,1);
 }
 l->rows.insert(p,row);QObject::connect(p,&QObject::destroyed,l,[l,p]{l->rows.remove(p);});
}
void projectAIInovationProviders(AIWorldView&w,Room&r,const ServerPlayer*p){
 GameBinding binding(r);
 if(!p||p->getRoom()!=&r||w.self.objectName!=p->objectName())return;
 QJsonObject rows;for(const auto&m:methods)rows.insert(m.name,QJsonObject{{"known",false}});
 auto publish=[&]{auto facts=w.strategyFacts.value("inovation").toObject();facts.insert("providers",rows);w.strategyFacts.insert("inovation",facts);};
 auto*l=get(r);lua_State*L=nullptr;if(!inovationAiInovationProvidersCurrent(r,l,L)||!lua_checkstack(L,128)){publish();return;}
 const auto revision=r.roomRuntime()->stateRevision();const auto generation=l->generation;
 const auto it=l->rows.constFind(p);if(it==l->rows.cend()||!it->native||it->native!=p->getSmartAI()){publish();return;}
 InovationAiInovationProvidersStack stack(L);int dict=0,self=0;auto*ai=it->native.data();
 if(!classShape(L,l,dict)||!callbackSelf(r,L,ai,p,self)||lua_topointer(L,self)!=it->self){publish();return;}
 if(!pushCallback(L,ai)||lua_topointer(L,-1)!=it->callback){publish();return;}lua_pop(L,1);
 if(!lua_getmetatable(L,self)||lua_topointer(L,-1)!=it->dict){publish();return;}lua_pop(L,1);
 raw(L,self,"class");const bool same=lua_topointer(L,-1)==it->cls;lua_pop(L,1);if(!same){publish();return;}
 for(const auto&m:methods){raw(L,self,m.name);if(lua_isnil(L,-1)){lua_pop(L,1);raw(L,dict,m.name);}
  if(it->providers.contains(m.name)&&lua_topointer(L,-1)==it->providers.value(m.name)
   &&matchAIPinnedFunction(r,L,-1,source,m.first,m.last)&&environment(L,-1))
   {QJsonObject result{{"known",true},{"source","inovation"},{"first",m.first},{"last",m.last}};
    if(!strcmp(m.name,"hasLiyuEffect")){AILuaInspect::globals(L);raw(L,-1,"getPriorFriendsOfLiyu");result.insert("prior_friends_function_absent",lua_isnil(L,-1));lua_pop(L,2);}
    rows.insert(m.name,result);}
  lua_pop(L,1);
 }
 // Everything above is allocation-free with respect to Lua, including raw
 // metadata reads. Also reject a native lifecycle/state change before publish.
 if(r.roomRuntime()->stateRevision()!=revision||r.roomRuntime()->lua().generation()!=generation||p->getSmartAI()!=ai){
  for(const auto&m:methods)rows.insert(m.name,QJsonObject{{"known",false}});
 }
 publish();
}
bool aiInovationProviderKnown(Room&r,const ServerPlayer*p,const QString&method){
 AIWorldView w;if(!p)return false;w.self.objectName=p->objectName();projectAIInovationProviders(w,r,p);
 return w.strategyFacts.value("inovation").toObject().value("providers").toObject().value(method).toObject().value("known").toBool();
}
bool aiInovationPhaseConstruction(const AIRequest&q,const ServerPlayer*p,const QJsonObject&s){
 if(!p||!p->getRoom()||q.viewerObjectName!=p->objectName()||!aiRangeRequestCurrent(*p->getRoom(),q,p)
  ||s.size()!=6||s.value("source_key")!=QJsonValue("inovation.skip_phase")||s.value("skill")!=QJsonValue("")
  ||!s.value("subcards").isArray()||!s.value("subcards").toArray().isEmpty())return false;
 if(!aiInovationProviderKnown(*p->getRoom(),p,"willSkipPlayPhase")
  &&!aiInovationProviderKnown(*p->getRoom(),p,"willSkipDrawPhase"))return false;
 for(const Card*c:p->getCards("he"))if(c&&s.value("name")==QJsonValue(c->objectName())
  &&s.value("suit")==QJsonValue(int(c->getSuit()))&&s.value("number")==QJsonValue(c->getNumber()))return true;
 return false;
}
// Shared native-only binding proof for the independent continuity verifier.
// Success leaves exactly the current native owner's captured self table on L;
// failure leaves the stack unchanged. This grants no source effect/call rights.
bool pushAIActualAISelf(Room&r,const ServerPlayer*p,lua_State*L){
 if(!p||p->getRoom()!=&r||!L||!r.roomRuntime()||r.roomRuntime()->lua().state()!=L||!AILuaInspect::capacity(L,128))return false;
 auto*l=get(r);lua_State*bound=nullptr;if(!inovationAiInovationProvidersCurrent(r,l,bound)||bound!=L)return false;
 const auto it=l->rows.constFind(p);if(it==l->rows.cend()||!it->native||it->native!=p->getSmartAI())return false;
 InovationAiInovationProvidersStack stack(L);int dict=0,self=0;auto*ai=it->native.data();
 if(!classShape(L,l,dict)||!callbackSelf(r,L,ai,p,self)||lua_topointer(L,self)!=it->self)return false;
 if(!pushCallback(L,ai)||lua_topointer(L,-1)!=it->callback)return false;lua_pop(L,1);
 if(!lua_getmetatable(L,self)||lua_topointer(L,-1)!=it->dict)return false;lua_pop(L,1);
 raw(L,self,"class");if(lua_topointer(L,-1)!=it->cls)return false;lua_pop(L,1);
 lua_pushvalue(L,self);lua_replace(L,stack.n+1);lua_settop(L,stack.n+1);++stack.n;return true;
}

// ---- inovation-ai-inovation-state ----
#include "ai-strategy/owner.h"
#include "card.h"
#include <QMap>
#include <limits>
#include <cmath>
#include <map>
#include <mutex>
namespace {
class State;
std::mutex statesMutex;
std::map<Room*,State*> states;
struct Slot {bool known=false;QString name;bool sourceFalse=false;};
struct InovationAiInovationStateInput {
 QPointer<ServerPlayer> owner;quint64 decision=0,revision=0;
 AIReadonlyCardLease card;
 QString name;int suit=0,number=0;
};
struct YohaneTag {QPointer<ServerPlayer>owner,target;int card=-1;bool known=false;quint64 generation=0;};
class State final:public QObject {
public:
 explicit State(Room&r):QObject(&r),room(&r){std::lock_guard<std::mutex>lock(statesMutex);states.emplace(&r,this);}
 ~State()override{std::lock_guard<std::mutex>lock(statesMutex);states.erase(room);}
 Room *room;
 QMap<QString,InovationAiInovationStateInput>inputs;
 YohaneTag yohaneCard,yohanePlayer;bool yohaneWriting=false;
 QPointer<ServerPlayer>goodOwner;QString goodValue;bool goodKnown=false,goodWriting=false;quint64 goodEpoch=0,goodTagGeneration=0;
 QPointer<ServerPlayer>xinyangOwner;quint64 tagGeneration=0;
 int xinyangValue=-1;bool xinyangKnown=false,writing=false,entered=false;
};
State*lookup(Room&r){std::lock_guard<std::mutex>lock(statesMutex);auto i=states.find(&r);return i==states.end()?nullptr:i->second;}
State*state(Room&r){if(auto*s=lookup(r))return s;return new State(r);}
QJsonObject inovationAiInovationStateFail(const char*m){return{{"known",false},{"reason",QString::fromLatin1(m)}};}
bool inovationAiInovationStateCurrent(Room&r,const AIRequest&q,ServerPlayer*p,const QString&){
 return p&&p->getRoom()==&r&&q.viewerObjectName==p->objectName()
  &&aiRangeRequestCurrent(r,q,p);
}
bool authorized(Room&r,const AIRequest&q,ServerPlayer*p,const QString&skill){
 // Exactly one real native ownership query. Its mandatory preselection barrier
 // may revoke prior observations; never exempt it based on the returned value.
 return inovationAiInovationStateCurrent(r,q,p,skill)&&p->hasSkill(skill)&&inovationAiInovationStateCurrent(r,q,p,skill);
}
struct InovationAiInovationStateStack {lua_State*L;int n;explicit InovationAiInovationStateStack(lua_State*s):L(s),n(lua_gettop(s)){}~InovationAiInovationStateStack(){lua_settop(L,n);}};
bool pushSlot(lua_State*L,int self,const char*key){
 lua_pushvalue(L,self);QSet<const void*>visited;
 for(int depth=0;depth<16;++depth){
  if(!lua_istable(L,-1)||visited.contains(lua_topointer(L,-1)))return false;
  visited.insert(lua_topointer(L,-1));const int table=lua_absindex(L,-1);
  AILuaInspect::field(L,table,key);if(!lua_isnil(L,-1))return true;
  lua_pop(L,1);if(!lua_getmetatable(L,table)){lua_pushnil(L);return true;}
  AILuaInspect::field(L,-1,"__index");if(lua_isnil(L,-1))return true;
  if(!lua_istable(L,-1))return false; // Never execute an unknown getter.
 }
 return false;
}
Slot readSlot(Room&r,lua_State*L,int self,const char*key){
 InovationAiInovationStateStack stack(L);Slot result;if(!pushSlot(L,self,key))return result;
 if(lua_isnil(L,-1)||(lua_isboolean(L,-1)&&!lua_toboolean(L,-1))){result.known=true;result.sourceFalse=lua_isboolean(L,-1);return result;}
 for(auto*target:r.getAllPlayers(true))if(matchAISlotPlayer(L,-1,target)){
  if(!matchAINativeObjectMethods(r,L,-1,"ServerPlayer",QStringList{"objectName"}))return result;
  result.known=true;result.name=target->objectName();return result;
 }
 return result; // non-Player/foreign-room values have no public name projection.
}
bool inovationAiInovationStateSameSelf(Room&r,const AIRequest&q,ServerPlayer*p,lua_State*L,const void*identity){
 if(!inovationAiInovationStateCurrent(r,q,p,"shoushi"))return false;InovationAiInovationStateStack stack(L);
 return pushAIActualAISelf(r,p,L)&&lua_topointer(L,-1)==identity&&inovationAiInovationStateCurrent(r,q,p,"shoushi");
}
struct SlotWrite {Room*room;const AIRequest*request;ServerPlayer*owner;ServerPlayer*target;const char*key;const void*self;};
// Light C function + lightuserdata argument require no Lua allocation outside
// protectedCall. All allocating Player/string/table work runs in the game VM's
// own protected frame, never long-jumps through the isolated VM host.
int assignSlot(lua_State*L){
 if(!lua_checkstack(L,2048))return luaL_error(L,"Shoushi assignment stack unavailable");
 auto*w=static_cast<SlotWrite*>(lua_touserdata(L,1));
 if(!w)return luaL_error(L,"Shoushi native assignment context absent");
 lua_pushstring(L,w->key);const int key=lua_absindex(L,-1);
 if(w->target){if(!pushAISlotPlayer(L,w->target))return luaL_error(L,"Shoushi native Player wrapper unavailable");}else lua_pushnil(L);
 const int value=lua_absindex(L,-1);
 if(!inovationAiInovationStateSameSelf(*w->room,*w->request,w->owner,L,w->self)||!pushAIActualAISelf(*w->room,w->owner,L))
  return luaL_error(L,"Shoushi binding changed before assignment");
 const int self=lua_absindex(L,-1);
 AILuaInspect::field(L,self,w->key);const bool existing=!lua_isnil(L,-1);lua_pop(L,1);
 if(!existing&&lua_getmetatable(L,self)){
  AILuaInspect::field(L,-1,"__newindex");const bool plain=lua_isnil(L,-1);lua_pop(L,2);
  if(!plain)return luaL_error(L,"Shoushi source assignment has an unproved __newindex continuation");
 }
 lua_pushvalue(L,key);lua_pushvalue(L,value);lua_settable(L,self);
 // The source prefix is never rolled back if a reentrant effect/error changes
 // identity. A successful reply additionally confirms the actual written slot.
 if(!inovationAiInovationStateSameSelf(*w->room,*w->request,w->owner,L,w->self))return luaL_error(L,"Shoushi source assignment committed but binding changed");
 AILuaInspect::field(L,self,w->key);
 if(w->target?!matchAISlotPlayer(L,-1,w->target):!lua_isnil(L,-1))return luaL_error(L,"Shoushi assigned slot changed during write");
 return 0;
}
bool disclosed(const AIRequest&q,int id){for(const auto&pile:q.worldView.self.piles)if(pile.name=="xinyang")for(const auto&c:pile.cards)if(c.effectiveId==id)return true;return false;}
}
// R53: the source writes one shared Room tag, not per-viewer strategy memory.
// Unknown entries revoke without consulting the tag or a private branch result.
void invalidateAIGoodatwep(Room&r) noexcept {
 if(auto*s=lookup(r)){s->goodKnown=false;s->yohaneCard.known=false;s->yohanePlayer.known=false;if(s->goodEpoch!=std::numeric_limits<quint64>::max())++s->goodEpoch;}
}
void enterAIGoodatwep(Room&r,const QString&viewer){
 if(auto*s=lookup(r))if((s->goodKnown&&(!s->goodOwner||viewer!=s->goodOwner->objectName()))
  ||(s->yohaneCard.known&&(!s->yohaneCard.owner||viewer!=s->yohaneCard.owner->objectName()))
  ||(s->yohanePlayer.known&&(!s->yohanePlayer.owner||viewer!=s->yohanePlayer.owner->objectName())))invalidateAIGoodatwep(r);
}
QJsonObject queryAIGoodatwep(Room&r,const AIRequest&q,ServerPlayer*p,const QJsonObject&in){
 if(!p||!r.getAllPlayers(true).contains(p)||!r.roomRuntime()||r.roomRuntime()->isClosing()
  ||!r.roomRuntime()->lua().hasProtectedEntryObserver()||!inovationAiInovationStateCurrent(r,q,p,"goodatwep")||!q.decisionId||in.size()!=3
  ||in.value("source")!="goodatwep"||!in.value("op").isString()||!in.value("target").isString())return inovationAiInovationStateFail("goodatwep current source shape unavailable");
 const auto op=in.value("op").toString(),name=in.value("target").toString();
 QPointer<State>s=state(r);
 if(op=="read"){
  if(q.kind!=AIRequest::Choice||q.choiceOptions.reason!="goodatwep"||q.choiceOptions.question!="askForChoice"
   ||q.choiceOptions.choices!=QStringList{"gogetit","cancel"}||!name.isEmpty())return inovationAiInovationStateFail("goodatwep reader is outside exact native choice");
  if(!s->goodKnown||s->goodOwner!=p)return inovationAiInovationStateFail("goodatwep shared tag has no uninterrupted owner source witness");
  const auto actual=r.getTag("goodatwep");
  if(actual.metaType().id()!=QMetaType::QString||actual.toString()!=s->goodValue){s->goodKnown=false;return inovationAiInovationStateFail("goodatwep actual tag changed");}
  if(!inovationAiInovationStateCurrent(r,q,p,"goodatwep"))return inovationAiInovationStateFail("goodatwep reader became stale");
  return{{"known",true},{"value",actual.toString()}};
 }
 if(op!="write"||q.kind!=AIRequest::Activate||q.reason!=CardUseStruct::CARD_USE_REASON_PLAY||!q.pattern.isEmpty())return inovationAiInovationStateFail("goodatwep source writer requires Play");
 bool own=false;for(const auto&i:p->getSkillInstances())if(i.skillName=="goodatwep")own=true;
 ServerPlayer*target=r.findPlayerByObjectName(name);
 if(!own||!target||!target->isAlive()||!q.worldView.playerOrder.contains(name))return inovationAiInovationStateFail("goodatwep writer target is not a current public player");
 if(s->goodWriting||s->goodEpoch==std::numeric_limits<quint64>::max()||s->goodTagGeneration==std::numeric_limits<quint64>::max())return inovationAiInovationStateFail("goodatwep write already active or witness exhausted");
 const auto epoch=s->goodEpoch,generation=s->goodTagGeneration;s->goodKnown=false;s->goodWriting=true;
 AIGoodatwepPermit permit(r,q,p);QString error;bool ok=false;
 try{ok=writeAIGoodatwepTag(r,q,p,name,permit,&error);}catch(...){if(s){s->goodWriting=false;s->goodKnown=false;}throw;}
 if(!s)return inovationAiInovationStateFail("goodatwep Room or VM ended during source write");s->goodWriting=false;
 if(!ok||s->goodEpoch!=epoch||s->goodTagGeneration!=generation+1)return inovationAiInovationStateFail("goodatwep source prefix retained but continuation changed");
 s->goodOwner=p;s->goodValue=name;s->goodKnown=true;return{{"known",true}};
}

QJsonObject queryAIYohane(Room&r,const AIRequest&q,ServerPlayer*p,const QJsonObject&in){
 if(!p||!r.getAllPlayers(true).contains(p)||!r.roomRuntime()||r.roomRuntime()->isClosing()
  ||!r.roomRuntime()->lua().hasProtectedEntryObserver()||!inovationAiInovationStateCurrent(r,q,p,"yohane")||!q.decisionId
  ||in.value("source")!="yohane"||!in.value("op").isString()||!in.value("key").isString())return inovationAiInovationStateFail("Yohane actual source shape unavailable");
 const auto key=in.value("key").toString(),op=in.value("op").toString();
 const bool card=key=="ai_yohane_card_id",player=key=="ai_yohane_player",take=op=="take";
 if((!card&&!player)||(op!="write"&&!take)||in.size()!=(take?3:4))return inovationAiInovationStateFail("Yohane tag operation outside finite source");
 const bool pc=q.kind==AIRequest::PlayerChosen&&q.choiceOptions.reason=="yohane"&&q.choiceOptions.question=="askForPlayerChosen";
 if(card){
  if(take?!pc:!(q.kind==AIRequest::RespondCard&&q.pattern==".!"&&q.prompt.startsWith("#yohane-distribute:")
   &&q.handlingMethod==Card::MethodNone&&q.choiceOptions.question=="askForCard"))return inovationAiInovationStateFail("Yohane card tag requires exact distribute or player choice");
 }else{
  if(take?!(q.kind==AIRequest::Choice&&q.choiceOptions.reason=="yohane"&&q.choiceOptions.question=="askForChoice"
   &&(q.choiceOptions.choices==QStringList{"ph","pe"}||q.choiceOptions.choices==QStringList{"ph","pj"})):!pc)return inovationAiInovationStateFail("Yohane player tag requires exact source choice");
 }
 QPointer<State>s=state(r);auto&row=card?s->yohaneCard:s->yohanePlayer;QVariant value;QJsonValue answer;
 if(take){
  if(!row.known||row.owner!=p)return inovationAiInovationStateFail("Yohane shared tag has no uninterrupted owner source witness");
  const auto actual=r.getTag(key);
  if(card){if(actual.metaType().id()!=QMetaType::Int||actual.toInt()!=row.card){row.known=false;return inovationAiInovationStateFail("Yohane actual ID tag changed");}answer=actual.toInt();}
  else{auto*t=actual.value<ServerPlayer*>();if(!row.target||t!=row.target||!r.getAllPlayers(true).contains(t)){row.known=false;return inovationAiInovationStateFail("Yohane actual Player tag changed");}answer=t->objectName();}
 }else if(card){
  const auto v=in.value("value");double d=v.toDouble();if(!v.isDouble()||!std::isfinite(d)||d<0||d>2147483647.0||d!=std::floor(d))return inovationAiInovationStateFail("Yohane source ID malformed");
  const int id=int(d);if(r.getCardOwner(id)!=p||r.getCardPlace(id)!=Player::PlaceHand)return inovationAiInovationStateFail("Yohane source ID is not current own hand");
  bool disclosed=false;for(const auto&c:q.worldView.handCards)if(c.effectiveId==id)disclosed=true;
  if(!disclosed)return inovationAiInovationStateFail("Yohane source ID lacks owner snapshot disclosure");value=id;
 }else{
  if(!in.value("value").isString())return inovationAiInovationStateFail("Yohane source player name malformed");
  const auto name=in.value("value").toString();auto*t=r.findPlayerByObjectName(name);
  if(!t||!q.choiceOptions.playerNames.contains(name)||!q.worldView.playerOrder.contains(name))return inovationAiInovationStateFail("Yohane target outside actual offered public players");
  value=QVariant::fromValue(t);
 }
 if(!inovationAiInovationStateCurrent(r,q,p,"yohane")||s->yohaneWriting||s->goodEpoch==std::numeric_limits<quint64>::max()||row.generation==std::numeric_limits<quint64>::max())return inovationAiInovationStateFail("Yohane source witness stale or exhausted");
 const auto epoch=s->goodEpoch,generation=row.generation;row.known=false;s->yohaneWriting=true;
 const AIYohanePermit permit(r,q,p,key);QString error;bool ok=false;
 try{ok=writeAIYohaneTag(r,q,p,key,value,take,permit,&error);}catch(...){if(s){s->yohaneWriting=false;s->yohaneCard.known=false;s->yohanePlayer.known=false;}throw;}
 if(!s)return inovationAiInovationStateFail("Yohane Room or VM ended during prefix");s->yohaneWriting=false;
 auto&after=card?s->yohaneCard:s->yohanePlayer;
 if(!ok||s->goodEpoch!=epoch||after.generation!=generation+1)return inovationAiInovationStateFail("Yohane actual source prefix retained but continuation changed");
 if(take)return{{"known",true},{"value",answer}}; // removal stays committed before caller's next face/relation operation
 after.owner=p;after.card=card?value.toInt():-1;after.target=card?nullptr:value.value<ServerPlayer*>();after.known=true;return{{"known",true}};
}

void clearAIInovationState(Room&r){delete lookup(r);}
void observeAIXinyangTagWrite(Room&r,const QString&name){
 if(name=="ai_yohane_card_id"||name=="ai_yohane_player"){if(auto*s=lookup(r)){auto&row=name=="ai_yohane_card_id"?s->yohaneCard:s->yohanePlayer;row.known=false;if(row.generation!=std::numeric_limits<quint64>::max())++row.generation;}return;}
 if(name=="goodatwep"){if(auto*s=lookup(r)){s->goodKnown=false;if(s->goodTagGeneration!=std::numeric_limits<quint64>::max())++s->goodTagGeneration;}return;}
 if(name!="xinyang_judge_card")return;
 if(auto*s=lookup(r)){
  if(s->tagGeneration!=std::numeric_limits<quint64>::max())++s->tagGeneration;
  s->xinyangKnown=false;
  if(s->writing&&!s->entered)s->entered=true;
 }
}
void observeAIXinyangTagRestore(Room&r){
 // Whole-map restore is a genuine writer even if the restored scalar compares
 // equal. Never resurrect prior owner provenance from that equality.
 observeAIXinyangTagWrite(r,QStringLiteral("xinyang_judge_card"));
 observeAIXinyangTagWrite(r,QStringLiteral("goodatwep"));
 observeAIXinyangTagWrite(r,QStringLiteral("ai_yohane_card_id"));
 observeAIXinyangTagWrite(r,QStringLiteral("ai_yohane_player"));
}
void projectAIShoushiInput(AIRequest&q,Room&r,ServerPlayer*p,const QVariant&data){
 if(!p||q.kind!=AIRequest::Choice||q.choiceOptions.reason!="shoushi")return;
 auto*s=state(r);s->inputs.remove(p->objectName());
 if(!authorized(r,q,p,"shoushi")||!data.canConvert<CardUseStruct>())return;
 const auto use=data.value<CardUseStruct>();if(!use.card)return;
 // This finite native Choice follows the public PreCardUsed Shoushi branch. It clones
 // only the three printed scalars, never the live card's flags/tags/materials.
 if(!use.card->isKindOf("Slash")&&!use.card->isKindOf("Snatch")&&!use.card->isKindOf("Dismantlement"))return;
 InovationAiInovationStateInput i;i.owner=p;i.decision=q.decisionId;i.revision=q.stateRevision;
 i.card=AIOwnerReadonlyCardStore::observe(use.card);if(!i.card||!i.card())return;
 i.name=use.card->objectName();i.suit=int(use.card->getSuit());i.number=use.card->getNumber();
 s->inputs.insert(p->objectName(),i);
}
bool aiShoushiConstruction(const AIRequest&q,const ServerPlayer*owner,const QJsonObject&spec){
 auto*p=const_cast<ServerPlayer*>(owner);if(!p||!p->getRoom())return false;auto&r=*p->getRoom();
 if(!authorized(r,q,p,"shoushi")||q.kind!=AIRequest::Choice||q.choiceOptions.reason!="shoushi"
  ||spec.size()!=6||spec.value("source_key")!=QJsonValue("shoushi")||spec.value("skill")!=QJsonValue("shoushi")
  ||!spec.value("subcards").isArray()||!spec.value("subcards").toArray().isEmpty())return false;
 auto*s=lookup(r);if(!s)return false;auto it=s->inputs.constFind(p->objectName());if(it==s->inputs.cend())return false;
 const auto&i=*it;const Card*c=i.card?i.card():nullptr;
 return i.owner==p&&i.decision==q.decisionId&&i.revision==q.stateRevision&&c&&c->objectName()==i.name
  &&int(c->getSuit())==i.suit&&c->getNumber()==i.number&&spec.value("name")==QJsonValue(i.name)
  &&spec.value("suit")==QJsonValue(i.suit)&&spec.value("number")==QJsonValue(i.number);
}
QJsonObject queryAIShoushi(Room&r,const AIRequest&q,ServerPlayer*p,const QString&op,const QString&name){
 if(!authorized(r,q,p,"shoushi")||q.choiceOptions.reason!="shoushi")return inovationAiInovationStateFail("Shoushi current owner question unavailable");
 const bool read=op=="read";
 if(read?(q.kind!=AIRequest::PlayerChosen||!name.isEmpty()):(q.kind!=AIRequest::Choice||(op!="extra"&&op!="remove")))
  return inovationAiInovationStateFail("Shoushi operation is not the current source question");
 auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();
 if(!L||!lua_checkstack(L,2048))return inovationAiInovationStateFail("Shoushi actual game VM unavailable");
 InovationAiInovationStateStack stack(L);if(!pushAIActualAISelf(r,p,L))return inovationAiInovationStateFail("Shoushi actual own AI binding unavailable");
 const int self=lua_absindex(L,-1);const void*identity=lua_topointer(L,self);
 if(read){
  const Slot extra=readSlot(r,L,self,"shoushi_extra_target");
  if(!extra.known)return inovationAiInovationStateFail("Shoushi actual extra slot is not a witnessed Player, nil, or false");
  // Preserve the actual source short circuit: do not inspect remove when the
  // first slot is truthy. No shadow/VM-reset/legacy-write continuity inference.
  const Slot value=extra.name.isEmpty()?readSlot(r,L,self,"shoushi_remove_target"):extra;
  if(!value.known||!inovationAiInovationStateSameSelf(r,q,p,L,identity))return inovationAiInovationStateFail("Shoushi actual slot read lost current binding");
  return{{"known",true},{"value",value.name.isEmpty()?QJsonValue(false):QJsonValue(value.name)},{"source_false",value.sourceFalse}};
 }
 ServerPlayer*target=nullptr;
 if(!name.isEmpty()){
  bool visible=q.worldView.self.objectName==name;for(const auto&v:q.worldView.players)if(v.objectName==name)visible=true;
  target=r.findPlayerByObjectName(name,true);
  if(!visible||!target||target->getRoom()!=&r)return inovationAiInovationStateFail("Shoushi source target is not a current public player");
 }
 const char*key=op=="extra"?"shoushi_extra_target":"shoushi_remove_target";
 SlotWrite write{&r,&q,p,target,key,identity};
 lua_pushcfunction(L,assignSlot);lua_pushlightuserdata(L,&write);
 if(LuaRuntime::protectedCall(L,1,0,0)!=LUA_OK)return inovationAiInovationStateFail("Shoushi native source assignment did not complete; reached prefix retained");
 if(!inovationAiInovationStateSameSelf(r,q,p,L,identity))return inovationAiInovationStateFail("Shoushi native source assignment lost current binding");
 return{{"known",true}};
}
QJsonObject queryAIXinyang(Room&r,const AIRequest&q,ServerPlayer*p,const QString&op,int id){
 if(!authorized(r,q,p,"xinyang"))return inovationAiInovationStateFail("Xinyang owner/current question unavailable");
 QPointer<State>s=state(r);int answer=-1,write=-1;
 if(op=="write"){
  if(q.kind!=AIRequest::SkillInvoke||q.choiceOptions.reason!="xinyang_judge"||id<0
   ||!p->getPile("xinyang").contains(id)||!disclosed(q,id)||!q.choiceOptions.context.value("judge").isObject())
   return inovationAiInovationStateFail("Xinyang selected material is not current disclosed owner judge input");
  write=id;
 }else if(op=="take"){
  if(q.kind!=AIRequest::AmazingGrace||q.choiceOptions.reason!="xinyang"||id!=-1)
   return inovationAiInovationStateFail("Xinyang reader requires current AG question");
  if(!s->xinyangKnown||s->xinyangOwner!=p)
   return inovationAiInovationStateFail("Xinyang actual tag lacks this owner's witnessed source write");
  // Every actual named writer/whole-map restore revokes provenance. Equality
  // is only an integrity check, NEVER a way to resurrect a cleared record.
  const auto actual=r.getTag("xinyang_judge_card");
  if(actual.metaType().id()!=QMetaType::Int||actual.toInt()!=s->xinyangValue){s->xinyangKnown=false;return inovationAiInovationStateFail("Xinyang actual tag no longer matches witnessed write");}
  answer=s->xinyangValue;
 }else return inovationAiInovationStateFail("unknown Xinyang source operation");
 if(s->writing||s->tagGeneration==std::numeric_limits<quint64>::max())return inovationAiInovationStateFail("Xinyang setter is already active or generation exhausted");
 const auto generation=s->tagGeneration;s->xinyangKnown=false;s->writing=true;s->entered=false;
 const AIXinyangWritePermit permit(r,q,p);
 QString error;bool accepted=false;
 try{accepted=writeAIXinyangTag(r,q,p,write,permit,&error);}catch(...){if(s){s->writing=false;s->xinyangKnown=false;}throw;}
 if(!s)return inovationAiInovationStateFail("Xinyang source setter outlived its Room/VM state");
 s->writing=false;
 if(!accepted||generation+1!=s->tagGeneration||!s->entered)return inovationAiInovationStateFail("Xinyang setter was replaced or invalidated during its actual prefix");
 s->xinyangKnown=true;s->xinyangOwner=p;s->xinyangValue=write;
 return op=="take"?QJsonObject{{"known",true},{"value",answer}}:QJsonObject{{"known",true}};
}

// ---- inovation-ai-inovation ----
#include "engine.h"
namespace {
bool inovationMaterialDisclosed(const AIRequest&r,int id,bool expanded)
{
 for(const auto&c:r.worldView.handCards)if(c.effectiveId==id)return true;
 if(!expanded)return false;
 for(const auto&c:r.worldView.self.equips)if(c.effectiveId==id)return true;
 for(const auto&c:r.worldView.self.judgingArea)if(c.effectiveId==id)return true;
 for(const auto&pile:r.worldView.self.piles)for(const auto&c:pile.cards)if(c.effectiveId==id)return true;
 return false;
}
bool inovationMaterialSpec(const AIRequest&r,const ServerPlayer*p,const QJsonObject&s)
{
 if(!p||!p->getRoom()||r.viewerObjectName!=p->objectName()||!aiRangeRequestCurrent(*p->getRoom(),r,p)
  ||s.size()!=6||!s.value("source_key").isString()||s.value("skill")!=s.value("source_key")
  ||!s.value("subcards").isArray()||s.value("subcards").toArray().size()!=1)return false;
 const auto key=s.value("source_key").toString();
 if(key=="touzhi"){
  if(r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY||!p->hasSkill(key)||s.value("name")!=QJsonValue("slash"))return false;
 }else if(key=="youjiao"||key=="guangyu"){
  if(r.kind!=AIRequest::UseCard||r.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE||r.pattern!="@@"+key
   ||!p->hasSkill(key)||s.value("name")!=QJsonValue("key_trick"))return false;
 }else return false;
 const auto idValue=s.value("subcards").toArray().first();if(!idValue.isDouble())return false;
 const double raw=idValue.toDouble();if(!std::isfinite(raw)||raw<0||raw>std::numeric_limits<int>::max()||raw!=std::floor(raw))return false;
 const int id=int(raw);
 if(!inovationMaterialDisclosed(r,id,key=="guangyu")||p->getRoom()->getCardOwner(id)!=p)return false;
 if(key=="guangyu"){
  if(!p->property("guangyu").toString().split('+').contains(QString::number(id)))return false;
 }else if(!p->handCards().contains(id))return false;
 const Card*c=Sanguosha->getCard(id);if(!c)return false;
 // Source class predicates are not native payment checks: notably do not
 // reject using here. Final V2 receipt will reject that selected cost.
 if(key=="touzhi"&&(!c->isKindOf("TrickCard")||c->isKindOf("AOE")||c->isKindOf("GodSalvation")||c->isKindOf("AmazingGrace")||c->isKindOf("Collateral")))return false;
 if(key=="youjiao"&&(!c->isKindOf("BasicCard")||c->isKindOf("Peach")))return false;
 const int printedNumber=c->getNumber()>13?0:c->getNumber();
 return s.value("suit")==QJsonValue(int(c->getSuit()))&&s.value("number")==QJsonValue(printedNumber);
}
}

void projectAIInovationWorld(AIWorldView &world,Room &room,const ServerPlayer *viewer)
{
    if (!viewer || viewer->getRoom()!=&room || world.self.objectName!=viewer->objectName()) return;
    QJsonObject observed;
    // PlayerStateService::akarinPlayer/removeAkarinEffect send the exact
    // membership Boolean to this observer (targeted notifyAkarinVisibility).
    // Never serialize the underlying tag/list or another observer's value.
    for (ServerPlayer *target:room.getAllPlayers(true))
        observed.insert(target->objectName(),room.isAkarin(target,const_cast<ServerPlayer *>(viewer)));
    auto row=world.strategyFacts.value("inovation").toObject();
    row.insert("akarin",observed);world.strategyFacts.insert("inovation",row);
}
void projectAIInovationContext(AIRequest &request,Room &room,ServerPlayer *viewer,const QVariant &data)
{
    projectAIGoutong(request,room,viewer,data);
    projectAIShoushiInput(request,room,viewer,data);
    if(viewer&&viewer->getRoom()==&room&&request.viewerObjectName==viewer->objectName()
        &&aiRangeRequestCurrent(room,request,viewer)&&request.kind==AIRequest::UseCard
        &&request.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&request.pattern=="@@guangyu"&&viewer->hasSkill("guangyu")){
        QJsonArray ids;bool known=true;
        for(const QString&word:viewer->property("guangyu").toString().split('+')){
            bool ok=false;const int id=word.toInt(&ok);const bool disclosed=inovationMaterialDisclosed(request,id,true);
            if(!ok||id<0||word!=QString::number(id)||!disclosed||room.getCardOwner(id)!=viewer){known=false;break;}
            ids.append(id);
        }
        // Native writer ListI2S never supplies empty/malformed lists to this
        // question. Restored/unwitnessed shapes stay unknown, not fabricated [].
        request.choiceOptions.context.insert("inovation_guangyu",known?QJsonObject{{"known",true},{"ids",ids}}:QJsonObject{{"known",false}});
    }

    if (!viewer || viewer->getRoom()!=&room || request.viewerObjectName!=viewer->objectName()
        || request.stateRevision!=room.roomRuntime()->stateRevision()
        || request.kind!=AIRequest::SkillInvoke || request.choiceOptions.reason!="kuisi") return;
    // QVariant::value<DeathStruct>() has a null damage pointer for other types;
    // this is source cast behavior, not an inferred zero/false strategy result.
    const DeathStruct death=data.value<DeathStruct>();
    QJsonObject row{{"known",true},{"has_damage",death.damage!=nullptr}};
    if (death.damage) {
        auto *from=death.damage->from;
        if (from && (from->getRoom()!=&room || !room.getAllPlayers(true).contains(from))) return;
        row.insert("from",from?QJsonValue(from->objectName()):QJsonValue(false));
    }
    request.choiceOptions.context.insert("inovation_kuisi",row);
}
namespace {
bool currentOwner(const AIRequest &r,const ServerPlayer *p)
{
    return p && p->getRoom() && r.viewerObjectName==p->objectName()
        && r.stateRevision==p->getRoom()->roomRuntime()->stateRevision();
}
}
int aiInovationPlayPrototype(const AIRequest &r,const ServerPlayer *p,
                               const QString &skill,const QString &prototype)
{
    if (!currentOwner(r,p) || r.kind!=AIRequest::Activate
        || r.reason!=CardUseStruct::CARD_USE_REASON_PLAY || !p->hasSkill(skill)) return -1;
    // Readonly source prototypes only. Neither cost feasibility nor targets
    // are paid here; the existing selected V2 path performs final admission.
    static const QMap<QString,QString> names{{"inovation_zhuren","InovationZhurenCard"},
        {"diangong","DiangongCard"},{"zhiling","ZhilingCard"},{"taxian","TaxianCard"},
        {"ningju","NingjuCard"},{"jiguan","JiguanCard"}};
    if (!names.contains(skill) || names.value(skill)!=prototype) return -1;
    return (skill=="inovation_zhuren" || skill=="diangong") ? 1 : 0;
}
bool aiInovationMaterialConstruction(const AIRequest &r,const ServerPlayer *p,const QJsonObject &s)
{
    if(inovationMaterialSpec(r,p,s))return true;
    // Exact source778–786 Slash(NoSuit,0) before the target-loop. No inferred
    // availability and no early cost validation; this unskilled clone cannot pay.
    return currentOwner(r,p) && r.kind==AIRequest::Activate
        && r.reason==CardUseStruct::CARD_USE_REASON_PLAY && p->hasSkill("ningju")
        && s.size()==6 && s.value("source_key")==QJsonValue("inovation.ningju")
        && s.value("name")==QJsonValue("slash") && s.value("skill")==QJsonValue("")
        && s.value("suit")==QJsonValue(int(Card::NoSuit)) && s.value("number")==QJsonValue(0)
        && s.value("subcards").isArray() && s.value("subcards").toArray().isEmpty();
}

// ---- xiedou ----
bool aiXiedouConstruction(const AIRequest &r,const ServerPlayer *p,const QJsonObject &s)
{
 if(!p||!p->getRoom()||r.viewerObjectName!=p->objectName()
  ||!aiRangeRequestCurrent(*p->getRoom(),r,p)
  ||r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY
  ||s.size()!=6||s.value("source_key")!=QJsonValue("xiedou")
  ||s.value("name")!=QJsonValue("duel")||s.value("skill")!=QJsonValue("xiedou")
  ||!s.value("subcards").isArray()||!p->hasSkill("xiedou"))return false;
 const auto ids=s.value("subcards").toArray();
 const int equips=p->getEquips().size(),count=p->getHandcardNum()-equips;
 if(equips==0||count<=0||ids.size()!=count)return false;
 QSet<int> seen;const Card *first=nullptr;
 for(const auto &value:ids){
  const double n=value.toDouble(-1);
  if(!value.isDouble()||!std::isfinite(n)||n<0||n>std::numeric_limits<int>::max()||n!=std::floor(n))return false;
  const int id=int(n);bool disclosed=false;
  for(const auto &card:r.worldView.handCards)if(card.effectiveId==id){disclosed=true;break;}
  if(!disclosed||seen.contains(id)||!p->handCards().contains(id)||p->getRoom()->getCardOwner(id)!=p)return false;
  seen.insert(id);const Card *material=Sanguosha->getCard(id);if(!material)return false;
  if(!first)first=material;
 }
 // This is the reached readonly Card_Parse, NOT native payment. In particular
 // using/jilei/quota/native target admission must not reorder source selection.
 const int number=first->getNumber()>13?0:first->getNumber();
 return s.value("suit")==QJsonValue(int(first->getSuit()))&&s.value("number")==QJsonValue(number)
  &&aiRangeRequestCurrent(*p->getRoom(),r,p);
}


#include "card-lifetime-manager.h"
namespace {
struct XiedouFlagEntry {
 std::weak_ptr<const CardLifetimeToken> token;
 QPointer<const Card> card;
 quint64 epoch=0;
};
class XiedouFlagLedger final:public QObject {
public:
 explicit XiedouFlagLedger(Room &room):QObject(&room){}
 QHash<const Card*,XiedouFlagEntry> entries;
};
XiedouFlagLedger *xiedouFlagLedger(Room &room,bool create)
{
 for(QObject *child:room.children())if(auto *l=dynamic_cast<XiedouFlagLedger*>(child))return l;
 return create?new XiedouFlagLedger(room):nullptr;
}
}
class AIXiedouObservation {
public:
 static void invalidate(const Card *card) noexcept
 {
  if(card && card->m_aiXiedouNosJiefanEpoch != ~quint64(0))
   ++card->m_aiXiedouNosJiefanEpoch;
 }
 static void record(Room &room,const Card *card,bool xufaValue=false)
 {
  if(!card||!card->isVirtualCard()||card->getClassName()!=QStringLiteral("Duel")
    ||card->hasFlag("nosjiefan-slash"))return;
  if(xufaValue){
   // Only the fresh zero/one-material Duel parsed by the actual Xufa value
   // continuation calls this issuer. A class/skill match alone grants nothing.
   if(card->getSkillName(false)!=QStringLiteral("xufa")
     ||card->getSubcards().size()>1
     ||(!card->getSubcards().isEmpty()&&card->getSubcards().first()<0))return;
  }else if(!card->getSubcards().isEmpty()||card->getSkillName(false)!=QStringLiteral("xiedou")
    ||card->getSuit()!=Card::NoSuit||card->getNumber()!=0)return;
  const auto token=globalCardLifetimeManager().liveToken(card);
  if(!token||!token->live||card->m_aiXiedouNosJiefanEpoch==~quint64(0))return;
  auto *l=xiedouFlagLedger(room,true);
  for(auto i=l->entries.begin();i!=l->entries.end();) {
   const auto old=i->token.lock();if(!old||!old->live||i->card.isNull())i=l->entries.erase(i);else ++i;
  }
  // Dead-address reuse may issue a new object; the same live object never revives.
  if(l->entries.contains(card))return;
  l->entries.insert(card,{token,card,card->m_aiXiedouNosJiefanEpoch});
 }
 static QJsonObject read(Room &room,ServerPlayer *viewer,const Card *card)
 {
  if(!viewer||viewer->getRoom()!=&room||!card)return{};
  auto *l=xiedouFlagLedger(room,false);if(!l)return{};
  const auto i=l->entries.constFind(card);if(i==l->entries.cend()||i->card.isNull()||i->card.data()!=card)return{};
  const auto token=i->token.lock();
  if(!token||!token->live||globalCardLifetimeManager().liveToken(card)!=token
    ||card->m_aiXiedouNosJiefanEpoch==~quint64(0)
    ||i->epoch!=card->m_aiXiedouNosJiefanEpoch)return{};
  return{{QStringLiteral("nosjiefan-slash"),false}};
 }
};
void recordAIFreshXiedouDuel(Room &room,const Card *card)
{AIXiedouObservation::record(room,card);}
void recordAIFreshXufaValueDuel(Room &room,const Card *card)
{AIXiedouObservation::record(room,card,true);}
void invalidateAIXiedouFlags(const Card *card) noexcept
{AIXiedouObservation::invalidate(card);}
QJsonObject readAIXiedouFlags(Room &room,ServerPlayer *viewer,const Card *card)
{return AIXiedouObservation::read(room,viewer,card);}
