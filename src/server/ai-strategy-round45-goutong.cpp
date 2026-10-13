#include "ai-strategy-round45-goutong.h"
#include "ai-strategy-room-ledger.h"
#include "ai-strategy-round45-inovation-providers.h"
#include "ai-strategy-round45-native-seal.h"
#include "ai-strategy-round44-certificate.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "lua-runtime.h"
#include <QObject>
#include <QPointer>
#include <QHash>
#include <memory>
#include "ai-strategy-round45-lua-inspect.h"
namespace {
constexpr const char*name="AI.R45.GoutongMove";
struct Input {QPointer<ServerPlayer>owner;quint64 decision=0,revision=0;std::shared_ptr<const CardsMoveOneTimeStruct>move;bool attempted=false,reached=false;};
class Ledger final:public AIRoomLedger{public:explicit Ledger(Room&r):AIRoomLedger(r){setObjectName(name);}QHash<QString,Input>rows;};
Ledger*ledger(Room&r,bool create=false){for(auto*c:r.children())if(c->objectName()==name)return static_cast<Ledger*>(c);return create?new Ledger(r):nullptr;}
struct Stack{lua_State*L;int top;~Stack(){lua_settop(L,top);}};
ServerPlayer*rosterPlayer(Room&r,const Player*p){for(auto*v:r.getAllPlayers(true))if(static_cast<const Player*>(v)==p)return v;return nullptr;}
bool current(Room&r,const AIRequest&q,ServerPlayer*p){return p&&p->getRoom()==&r&&q.viewerObjectName==p->objectName()&&q.kind==AIRequest::SkillInvoke&&q.choiceOptions.reason=="goutong"&&aiRound44RangeRequestCurrent(r,q,p);}
QJsonObject fail(const char*why){return{{"known",false},{"reason",QString::fromLatin1(why)}};}
struct Write{Room*room;const AIRequest*request;ServerPlayer*owner;const CardsMoveOneTimeStruct*move;const void*self;Player*from=nullptr;Player*to=nullptr;};
bool sameSelf(Write*w,lua_State*L){Stack restore{L,lua_gettop(L)};return current(*w->room,*w->request,w->owner)&&pushAIRound45ActualAISelf(*w->room,w->owner,L)&&lua_topointer(L,-1)==w->self;}
int assign(lua_State*L){
 if(!lua_checkstack(L,2048))return luaL_error(L,"Goutong assignment stack unavailable");
 auto*w=static_cast<Write*>(lua_touserdata(L,1));if(!w)return luaL_error(L,"Goutong source write context absent");
 if(!pushAIRound45GoutongMove(L,*w->move))return luaL_error(L,"Goutong authentic native move copy unavailable");
 const int move=lua_absindex(L,-1);lua_pushliteral(L,"move");const int key=lua_absindex(L,-1);
 if(!sameSelf(w,L))return luaL_error(L,"Goutong actual owner binding changed before source prefix");
 AIRound45Inspect::globals(L);const int globals=lua_absindex(L,-1);AIRound45Inspect::field(L,globals,"move");const bool exists=!lua_isnil(L,-1);lua_pop(L,1);
 if(!exists&&lua_getmetatable(L,globals)){
  AIRound45Inspect::field(L,-1,"__newindex");const bool plain=lua_isnil(L,-1);lua_pop(L,2);if(!plain)return luaL_error(L,"Goutong global assignment has unknown __newindex");
 }
 lua_pushvalue(L,key);lua_pushvalue(L,move);lua_settable(L,globals);
 if(!sameSelf(w,L))return luaL_error(L,"Goutong source global prefix committed before binding changed");
 AIRound45Inspect::field(L,globals,"move");if(!lua_rawequal(L,move,-1))return luaL_error(L,"Goutong global replaced during assignment");
 return 0;
}
}
void clearAIRound45Goutong(Room&r){delete ledger(r);}
void projectAIRound45Goutong(AIRequest&q,Room&r,ServerPlayer*p,const QVariant&data){
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
QJsonObject writeAIRound45GoutongMove(Room&r,const AIRequest&q,ServerPlayer*p){
 if(!current(r,q,p)||!p->hasSkill("goutong")||!current(r,q,p))return fail("Goutong exact current owner question unavailable");
 auto*l=ledger(r);if(!l)return fail("Goutong actual native input not captured");auto i=l->rows.find(p->objectName());
 if(i==l->rows.end()||i->owner!=p||i->decision!=q.decisionId||i->revision!=q.stateRevision||i->attempted||!i->move)return fail("Goutong source input is stale or already consumed");
 const auto value=i->move;i->attempted=true;
 auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();if(!L||!lua_checkstack(L,2048))return fail("Goutong game VM unavailable");Stack restore{L,lua_gettop(L)};
 if(!pushAIRound45ActualAISelf(r,p,L))return fail("Goutong actual own LuaAI unavailable");const void*self=lua_topointer(L,-1);
 Write write{&r,&q,p,value.get(),self};lua_pushcfunction(L,assign);lua_pushlightuserdata(L,&write);
 if(LuaRuntime::protectedCall(L,1,0,0)!=LUA_OK)return fail("Goutong native global source assignment failed; reached prefix retained");
 if(!sameSelf(&write,L))return fail("Goutong current session changed after source global assignment");
 l=ledger(r);if(!l)return fail("Goutong input reset during source assignment");i=l->rows.find(p->objectName());
 if(i==l->rows.end()||i->move!=value||i->decision!=q.decisionId||i->revision!=q.stateRevision)return fail("Goutong input replaced during source assignment");
 i->reached=true;return{{"known",true}};
}

QJsonObject readAIRound45GoutongMove(Room&r,const AIRequest&q,ServerPlayer*p){
 if(!current(r,q,p))return fail("Goutong source read is not the current owner question");
 auto*l=ledger(r);if(!l)return fail("Goutong reached source assignment absent");const auto i=l->rows.constFind(p->objectName());
 if(i==l->rows.cend()||i->owner!=p||i->decision!=q.decisionId||i->revision!=q.stateRevision||!i->reached)return fail("Goutong source read has no current reached prefix");
 auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();if(!L||!lua_checkstack(L,2048))return fail("Goutong game VM unavailable");Stack restore{L,lua_gettop(L)};
 if(!pushAIRound45ActualAISelf(r,p,L))return fail("Goutong actual own LuaAI unavailable");const void*self=lua_topointer(L,-1);
 AIRound45Inspect::globals(L);AIRound45Inspect::field(L,-1,"move");
 if(lua_isnil(L,-1)){
  if(AIRound45Inspect::primitiveMetatable(L,LUA_TNIL))return fail("Goutong nil global has an unproved primitive metatable");
  const int globals=lua_absindex(L,-2);if(lua_getmetatable(L,globals)){AIRound45Inspect::field(L,-1,"__index");if(!lua_isnil(L,-1))return fail("Goutong missing global has unknown __index");}
  Write context{&r,&q,p,nullptr,self};if(!sameSelf(&context,L))return fail("Goutong nil source read binding changed");
  return{{"known",true},{"source_nil",true}};
 }
 Player*from=nullptr,*to=nullptr;
 if(!readAIRound45GoutongEndpoints(L,-1,from,to)||!matchAIRound45NativeClassMethod(r,L,"Player","objectName"))return fail("Goutong live source global or property binding changed");
 Write context{&r,&q,p,nullptr,self};if(!sameSelf(&context,L))return fail("Goutong source read binding changed");
 QJsonObject result{{"known",true}};
 for(const auto&field:QList<QPair<QString,Player*>>{{"from",from},{"to",to}}){
  auto*target=rosterPlayer(r,field.second);if(field.second&&!target)return fail("Goutong live endpoint is not in current public roster");
  result.insert(field.first,target?QJsonValue(target->objectName()):QJsonValue(false));
 }
 return result;
}
