#include "ai-strategy-round53-mingfa.h"
#include "ai-strategy-room-ledger.h"
#include "ai-strategy-round45-slot-bindings.h"
#include "ai-strategy-round45-inovation-providers.h"
#include "ai-strategy-round45-code-seal.h"
#include "ai-strategy-round45-native-seal.h"
#include "ai-strategy-round44-certificate.h"
#include "ai.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "lua-runtime.h"
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QJsonArray>
#include <cstring>
#include "ai-strategy-round45-lua-inspect.h"
namespace {
struct Row{QPointer<ServerPlayer>owner;AIReadonlyCardLease card;};
class Ledger final:public AIRoomLedger{public:explicit Ledger(Room&r):AIRoomLedger(r){}quint64 generation=0;QHash<const ServerPlayer*,Row>rows;};
Ledger*ledger(Room&r,bool create=false){for(auto*x:r.children())if(auto*l=dynamic_cast<Ledger*>(x))return l;return create?new Ledger(r):nullptr;}
bool current(const AIRequest&q,ServerPlayer*p){return p&&p->getRoom()&&p->getRoom()->roomRuntime()&&!p->getRoom()->roomRuntime()->isClosing()&&q.decisionId&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&Sanguosha->currentRoom()==p->getRoom()&&aiRound44RangeRequestCurrent(*p->getRoom(),q,p);}
bool scope(const AIRequest&q,bool write){return q.choiceOptions.reason=="tenyearmingfa"&&(write?(q.kind==AIRequest::SkillInvoke&&q.choiceOptions.question=="askForSkillInvoke"):(q.kind==AIRequest::PlayerChosen&&q.choiceOptions.question=="askForPlayerChosen"));}
bool field(lua_State*L,int index,const char*key){QSet<const void*>seen;lua_pushvalue(L,index);for(int depth=0;depth<16;++depth){if(!lua_istable(L,-1)||seen.contains(lua_topointer(L,-1)))return false;seen.insert(lua_topointer(L,-1));const int row=lua_absindex(L,-1);AIRound45Inspect::field(L,row,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);if(!lua_getmetatable(L,row)){lua_pushnil(L);return true;}AIRound45Inspect::field(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;}return false;}
bool assignment(lua_State*L,int self){AIRound45Inspect::field(L,self,"tenyearmingfa_c");const bool exists=!lua_isnil(L,-1);lua_pop(L,1);if(exists||!lua_getmetatable(L,self))return true;AIRound45Inspect::field(L,-1,"__newindex");const bool absent=lua_isnil(L,-1);lua_pop(L,2);return absent;}
bool reader(Room&r,lua_State*L){const int top=lua_gettop(L);struct Restore{lua_State*L;int top;~Restore(){lua_settop(L,top);}}restore{L,top};AIRound45Inspect::globals(L);const int env=lua_absindex(L,-1);AIRound45Inspect::field(L,env,"sgs");if(!lua_istable(L,-1))return false;AIRound45Inspect::field(L,-1,"ai_skill_playerchosen");if(!lua_istable(L,-1))return false;AIRound45Inspect::field(L,-1,"tenyearmingfa");const int f=lua_absindex(L,-1);if(!matchAIRound45PinnedFunction(r,L,f,"lua/ai/tenyear_hc-ai.lua",1843,1871))return false;for(int i=1;;++i){const char*k=lua_getupvalue(L,f,i);if(!k)break;const bool ok=!std::strcmp(k,"_ENV")&&lua_rawequal(L,env,-1);lua_pop(L,1);if(!ok)return false;}return true;}
struct Call{const AIRequest*q;ServerPlayer*p;QString op,method;ServerPlayer*target=nullptr;AIReadonlyCardLease lease;const Card*card=nullptr;QJsonObject result;QString error;};
int run(lua_State*L){auto*w=static_cast<Call*>(lua_touserdata(L,1));auto fail=[&](const char*s){w->error=QString::fromLatin1(s);return luaL_error(L,"%s",s);};if(!lua_checkstack(L,4096))return fail("Mingfa source slot stack unavailable");Room&r=*w->p->getRoom();int value=0;
 if(w->op=="write"){if(!pushAIRound45SlotCard(L,w->card))return fail("Mingfa source Card binding unavailable");value=lua_absindex(L,-1);}
 if(!current(*w->q,w->p)||!pushAIRound45ActualAISelf(r,w->p,L))return fail("Mingfa actual Self unavailable");const int self=lua_absindex(L,-1);
 if(w->op=="write"){
  if(!assignment(L,self)||!current(*w->q,w->p)||w->lease()!=w->card)return fail("Mingfa assignment dispatch or custody changed");lua_pushvalue(L,value);lua_setfield(L,self,"tenyearmingfa_c");
 }else{
  if(!field(L,self,"tenyearmingfa_c"))return fail("Mingfa Card field requires unproved getter");value=lua_absindex(L,-1);
  if(lua_isnil(L,value)||lua_isboolean(L,value)){if(AIRound45Inspect::primitiveMetatable(L,lua_type(L,value)))return fail("Mingfa primitive metatable changed");w->result={{"source_error","source tenyearmingfa_c is not a Card"}};return 0;}
  auto*l=ledger(r);if(!l||l->generation!=r.roomRuntime()->lua().generation())return fail("Mingfa retained Card unavailable");const auto row=l->rows.value(w->p);w->lease=row.card;w->card=w->lease?w->lease():nullptr;
  if(row.owner!=w->p||!w->card||!w->card->isVirtualCard()||!w->card->getSubcards().isEmpty()||!matchAIRound45SlotCard(L,value,w->card)||!reader(r,L))return fail("Mingfa current source Card identity/provider changed");
  // The fixed reader reaches only these two operations. No eager availability,
  // other-target scan, clone substitution, descriptor receipt or cached result.
  bool valueBool=false;
  if(w->method=="target_fixed"){
   if(!matchAIRound45NativeObjectMethods(r,L,value,"Card",{"targetFixed"}))return fail("Mingfa targetFixed dispatch changed");valueBool=w->card->targetFixed();
  }else{
   if(!field(L,self,"player")||!matchAIRound45SlotPlayer(L,-1,w->p)||!matchAIRound45NativeObjectMethods(r,L,-1,"ServerPlayer",{"canUse"}))return fail("Mingfa source owner canUse dispatch changed");
   valueBool=w->p->canUse(w->card,w->target,true);
  }
  w->result={{"known",true},{"value",valueBool}};
 }
 if(!current(*w->q,w->p)||w->lease()!=w->card||!pushAIRound45ActualAISelf(r,w->p,L)||!lua_rawequal(L,self,-1))return fail("Mingfa query changed request or retained identity");
 if(w->op=="write")w->result={{"written",true}};return 0;
}
}
void projectAIRound53Mingfa(AIRequest&q,const QVariant&data){q.choiceOptions.context.remove("round53_mingfa_name");if(!scope(q,true)||data.metaType().id()!=QMetaType::QString)return;const auto pieces=data.toString().split(':');if(pieces.size()==2&&pieces.first()=="tenyearmingfa")q.choiceOptions.context.insert("round53_mingfa_name",pieces.last());}
bool aiRound53MingfaConstruction(const AIRequest&q,Room&,const ServerPlayer*p,const QJsonObject&s){return current(q,const_cast<ServerPlayer*>(p))&&scope(q,true)&&s.size()==6&&s.value("source_key")=="tenyearmingfa"&&s.value("skill")=="tenyearmingfa"&&s.value("suit")==int(Card::NoSuit)&&s.value("number")==0&&s.value("subcards").isArray()&&s.value("subcards").toArray().isEmpty()&&q.choiceOptions.context.value("round53_mingfa_name").isString()&&!q.choiceOptions.context.value("round53_mingfa_name").toString().isEmpty()&&s.value("name")==q.choiceOptions.context.value("round53_mingfa_name");}
bool registerAIRound53MingfaSources(Room&r,lua_State*L,QString*why){return registerAIRound45PinnedSource(r,L,"lua/ai/tenyear_hc-ai.lua",QByteArray("94a38f49a220eeb0f0816dc9737c868027cc32ebba6001edfab1c304364799f5"),why);}
void clearAIRound53MingfaSlots(Room&r){if(auto*l=ledger(r))delete l;}
QJsonObject queryAIRound53MingfaSlot(const AIRequest&q,ServerPlayer*p,const QString&op,const QJsonObject&payload,AIReadonlyCardLease lease){auto fail=[](const QString&s){return QJsonObject{{"error",s},{"key","tenyearmingfa.saved_card"}};};if((op!="write"&&op!="query")||!current(q,p)||!scope(q,op=="write"))return fail("Mingfa exact current source question unavailable");Call w;w.q=&q;w.p=p;w.op=op;
 if(op=="write"){if(payload.size()!=1||!payload.value("selector").isObject()||!lease)return fail("Mingfa source selector malformed");w.lease=std::move(lease);w.card=w.lease();if(!w.card||!w.card->isVirtualCard()||!w.card->getSubcards().isEmpty())return fail("Mingfa fixed zero-material source Card unavailable");}
 else{w.method=payload.value("method").toString();if(w.method=="target_fixed"){if(payload.size()!=1)return fail("Mingfa targetFixed has extra arguments");}else if(w.method=="can_use"){if(payload.size()!=2||!payload.value("target").isString())return fail("Mingfa target malformed");const auto name=payload.value("target").toString();if(!q.choiceOptions.playerNames.contains(name))return fail("Mingfa target was not offered");for(auto*t:p->getRoom()->getAllPlayers(true))if(t->objectName()==name)w.target=t;if(!w.target)return fail("Mingfa target left roster");}else return fail("Mingfa source predicate unavailable");}
 Room&r=*p->getRoom();auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();if(!L||!lua_checkstack(L,4096))return fail("Mingfa game VM unavailable");const int top=lua_gettop(L);struct Restore{lua_State*L;int top;~Restore(){lua_settop(L,top);}}restore{L,top};lua_pushcfunction(L,run);lua_pushlightuserdata(L,&w);if(LuaRuntime::protectedCall(L,1,0,0)!=LUA_OK)return fail(w.error.isEmpty()?QStringLiteral("Mingfa protected operation failed"):w.error);if(op=="write"){auto*l=ledger(r,true);if(l->generation&&l->generation!=vm.generation())return fail("Mingfa game generation changed");l->generation=vm.generation();l->rows[p]={p,std::move(w.lease)};}return w.result;
}
