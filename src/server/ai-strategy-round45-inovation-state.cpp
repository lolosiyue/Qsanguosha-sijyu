#include "ai-strategy-round45-inovation-state.h"
#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-readonly-card-store.h"
#include "ai-strategy-round45-inovation-providers.h"
#include "ai-strategy-round45-slot-bindings.h"
#include "ai-strategy-round45-native-seal.h"
#include "lua-runtime.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "card.h"
#include <QObject>
#include <QPointer>
#include <QMap>
#include <QSet>
#include <QJsonArray>
#include <limits>
#include <cmath>
#include <map>
#include <mutex>
#include "ai-strategy-round45-lua-inspect.h"
namespace {
class State;
std::mutex statesMutex;
std::map<Room*,State*> states;
struct Slot {bool known=false;QString name;bool sourceFalse=false;};
struct Input {
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
 QMap<QString,Input>inputs;
 YohaneTag yohaneCard,yohanePlayer;bool yohaneWriting=false;
 QPointer<ServerPlayer>goodOwner;QString goodValue;bool goodKnown=false,goodWriting=false;quint64 goodEpoch=0,goodTagGeneration=0;
 QPointer<ServerPlayer>xinyangOwner;quint64 tagGeneration=0;
 int xinyangValue=-1;bool xinyangKnown=false,writing=false,entered=false;
};
State*lookup(Room&r){std::lock_guard<std::mutex>lock(statesMutex);auto i=states.find(&r);return i==states.end()?nullptr:i->second;}
State*state(Room&r){if(auto*s=lookup(r))return s;return new State(r);}
QJsonObject fail(const char*m){return{{"known",false},{"reason",QString::fromLatin1(m)}};}
bool current(Room&r,const AIRequest&q,ServerPlayer*p,const QString&){
 return p&&p->getRoom()==&r&&q.viewerObjectName==p->objectName()
  &&aiRound44RangeRequestCurrent(r,q,p);
}
bool authorized(Room&r,const AIRequest&q,ServerPlayer*p,const QString&skill){
 // Exactly one real native ownership query. Its mandatory preselection barrier
 // may revoke prior observations; never exempt it based on the returned value.
 return current(r,q,p,skill)&&p->hasSkill(skill)&&current(r,q,p,skill);
}
struct Stack {lua_State*L;int n;explicit Stack(lua_State*s):L(s),n(lua_gettop(s)){}~Stack(){lua_settop(L,n);}};
bool pushSlot(lua_State*L,int self,const char*key){
 lua_pushvalue(L,self);QSet<const void*>visited;
 for(int depth=0;depth<16;++depth){
  if(!lua_istable(L,-1)||visited.contains(lua_topointer(L,-1)))return false;
  visited.insert(lua_topointer(L,-1));const int table=lua_absindex(L,-1);
  AIRound45Inspect::field(L,table,key);if(!lua_isnil(L,-1))return true;
  lua_pop(L,1);if(!lua_getmetatable(L,table)){lua_pushnil(L);return true;}
  AIRound45Inspect::field(L,-1,"__index");if(lua_isnil(L,-1))return true;
  if(!lua_istable(L,-1))return false; // Never execute an unknown getter.
 }
 return false;
}
Slot readSlot(Room&r,lua_State*L,int self,const char*key){
 Stack stack(L);Slot result;if(!pushSlot(L,self,key))return result;
 if(lua_isnil(L,-1)||(lua_isboolean(L,-1)&&!lua_toboolean(L,-1))){result.known=true;result.sourceFalse=lua_isboolean(L,-1);return result;}
 for(auto*target:r.getAllPlayers(true))if(matchAIRound45SlotPlayer(L,-1,target)){
  if(!matchAIRound45NativeObjectMethods(r,L,-1,"ServerPlayer",QStringList{"objectName"}))return result;
  result.known=true;result.name=target->objectName();return result;
 }
 return result; // non-Player/foreign-room values have no public name projection.
}
bool sameSelf(Room&r,const AIRequest&q,ServerPlayer*p,lua_State*L,const void*identity){
 if(!current(r,q,p,"shoushi"))return false;Stack stack(L);
 return pushAIRound45ActualAISelf(r,p,L)&&lua_topointer(L,-1)==identity&&current(r,q,p,"shoushi");
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
 if(w->target){if(!pushAIRound45SlotPlayer(L,w->target))return luaL_error(L,"Shoushi native Player wrapper unavailable");}else lua_pushnil(L);
 const int value=lua_absindex(L,-1);
 if(!sameSelf(*w->room,*w->request,w->owner,L,w->self)||!pushAIRound45ActualAISelf(*w->room,w->owner,L))
  return luaL_error(L,"Shoushi binding changed before assignment");
 const int self=lua_absindex(L,-1);
 AIRound45Inspect::field(L,self,w->key);const bool existing=!lua_isnil(L,-1);lua_pop(L,1);
 if(!existing&&lua_getmetatable(L,self)){
  AIRound45Inspect::field(L,-1,"__newindex");const bool plain=lua_isnil(L,-1);lua_pop(L,2);
  if(!plain)return luaL_error(L,"Shoushi source assignment has an unproved __newindex continuation");
 }
 lua_pushvalue(L,key);lua_pushvalue(L,value);lua_settable(L,self);
 // The source prefix is never rolled back if a reentrant effect/error changes
 // identity. A successful reply additionally confirms the actual written slot.
 if(!sameSelf(*w->room,*w->request,w->owner,L,w->self))return luaL_error(L,"Shoushi source assignment committed but binding changed");
 AIRound45Inspect::field(L,self,w->key);
 if(w->target?!matchAIRound45SlotPlayer(L,-1,w->target):!lua_isnil(L,-1))return luaL_error(L,"Shoushi assigned slot changed during write");
 return 0;
}
bool disclosed(const AIRequest&q,int id){for(const auto&pile:q.worldView.self.piles)if(pile.name=="xinyang")for(const auto&c:pile.cards)if(c.effectiveId==id)return true;return false;}
}
// R53: the source writes one shared Room tag, not per-viewer strategy memory.
// Unknown entries revoke without consulting the tag or a private branch result.
void invalidateAIRound53Goodatwep(Room&r) noexcept {
 if(auto*s=lookup(r)){s->goodKnown=false;s->yohaneCard.known=false;s->yohanePlayer.known=false;if(s->goodEpoch!=std::numeric_limits<quint64>::max())++s->goodEpoch;}
}
void enterAIRound53Goodatwep(Room&r,const QString&viewer){
 if(auto*s=lookup(r))if((s->goodKnown&&(!s->goodOwner||viewer!=s->goodOwner->objectName()))
  ||(s->yohaneCard.known&&(!s->yohaneCard.owner||viewer!=s->yohaneCard.owner->objectName()))
  ||(s->yohanePlayer.known&&(!s->yohanePlayer.owner||viewer!=s->yohanePlayer.owner->objectName())))invalidateAIRound53Goodatwep(r);
}
QJsonObject queryAIRound53Goodatwep(Room&r,const AIRequest&q,ServerPlayer*p,const QJsonObject&in){
 if(!p||!r.getAllPlayers(true).contains(p)||!r.roomRuntime()||r.roomRuntime()->isClosing()
  ||!r.roomRuntime()->lua().hasProtectedEntryObserver()||!current(r,q,p,"goodatwep")||!q.decisionId||in.size()!=3
  ||in.value("source")!="goodatwep"||!in.value("op").isString()||!in.value("target").isString())return fail("goodatwep current source shape unavailable");
 const auto op=in.value("op").toString(),name=in.value("target").toString();
 QPointer<State>s=state(r);
 if(op=="read"){
  if(q.kind!=AIRequest::Choice||q.choiceOptions.reason!="goodatwep"||q.choiceOptions.question!="askForChoice"
   ||q.choiceOptions.choices!=QStringList{"gogetit","cancel"}||!name.isEmpty())return fail("goodatwep reader is outside exact native choice");
  if(!s->goodKnown||s->goodOwner!=p)return fail("goodatwep shared tag has no uninterrupted owner source witness");
  const auto actual=r.getTag("goodatwep");
  if(actual.metaType().id()!=QMetaType::QString||actual.toString()!=s->goodValue){s->goodKnown=false;return fail("goodatwep actual tag changed");}
  if(!current(r,q,p,"goodatwep"))return fail("goodatwep reader became stale");
  return{{"known",true},{"value",actual.toString()}};
 }
 if(op!="write"||q.kind!=AIRequest::Activate||q.reason!=CardUseStruct::CARD_USE_REASON_PLAY||!q.pattern.isEmpty())return fail("goodatwep source writer requires Play");
 bool own=false;for(const auto&i:p->getSkillInstances())if(i.skillName=="goodatwep")own=true;
 ServerPlayer*target=r.findPlayerByObjectName(name);
 if(!own||!target||!target->isAlive()||!q.worldView.playerOrder.contains(name))return fail("goodatwep writer target is not a current public player");
 if(s->goodWriting||s->goodEpoch==std::numeric_limits<quint64>::max()||s->goodTagGeneration==std::numeric_limits<quint64>::max())return fail("goodatwep write already active or witness exhausted");
 const auto epoch=s->goodEpoch,generation=s->goodTagGeneration;s->goodKnown=false;s->goodWriting=true;
 AIRound53GoodatwepPermit permit(r,q,p);QString error;bool ok=false;
 try{ok=writeAIRound53GoodatwepTag(r,q,p,name,permit,&error);}catch(...){if(s){s->goodWriting=false;s->goodKnown=false;}throw;}
 if(!s)return fail("goodatwep Room or VM ended during source write");s->goodWriting=false;
 if(!ok||s->goodEpoch!=epoch||s->goodTagGeneration!=generation+1)return fail("goodatwep source prefix retained but continuation changed");
 s->goodOwner=p;s->goodValue=name;s->goodKnown=true;return{{"known",true}};
}

QJsonObject queryAIRound53Yohane(Room&r,const AIRequest&q,ServerPlayer*p,const QJsonObject&in){
 if(!p||!r.getAllPlayers(true).contains(p)||!r.roomRuntime()||r.roomRuntime()->isClosing()
  ||!r.roomRuntime()->lua().hasProtectedEntryObserver()||!current(r,q,p,"yohane")||!q.decisionId
  ||in.value("source")!="yohane"||!in.value("op").isString()||!in.value("key").isString())return fail("Yohane actual source shape unavailable");
 const auto key=in.value("key").toString(),op=in.value("op").toString();
 const bool card=key=="ai_yohane_card_id",player=key=="ai_yohane_player",take=op=="take";
 if((!card&&!player)||(op!="write"&&!take)||in.size()!=(take?3:4))return fail("Yohane tag operation outside finite source");
 const bool pc=q.kind==AIRequest::PlayerChosen&&q.choiceOptions.reason=="yohane"&&q.choiceOptions.question=="askForPlayerChosen";
 if(card){
  if(take?!pc:!(q.kind==AIRequest::RespondCard&&q.pattern==".!"&&q.prompt.startsWith("#yohane-distribute:")
   &&q.handlingMethod==Card::MethodNone&&q.choiceOptions.question=="askForCard"))return fail("Yohane card tag requires exact distribute or player choice");
 }else{
  if(take?!(q.kind==AIRequest::Choice&&q.choiceOptions.reason=="yohane"&&q.choiceOptions.question=="askForChoice"
   &&(q.choiceOptions.choices==QStringList{"ph","pe"}||q.choiceOptions.choices==QStringList{"ph","pj"})):!pc)return fail("Yohane player tag requires exact source choice");
 }
 QPointer<State>s=state(r);auto&row=card?s->yohaneCard:s->yohanePlayer;QVariant value;QJsonValue answer;
 if(take){
  if(!row.known||row.owner!=p)return fail("Yohane shared tag has no uninterrupted owner source witness");
  const auto actual=r.getTag(key);
  if(card){if(actual.metaType().id()!=QMetaType::Int||actual.toInt()!=row.card){row.known=false;return fail("Yohane actual ID tag changed");}answer=actual.toInt();}
  else{auto*t=actual.value<ServerPlayer*>();if(!row.target||t!=row.target||!r.getAllPlayers(true).contains(t)){row.known=false;return fail("Yohane actual Player tag changed");}answer=t->objectName();}
 }else if(card){
  const auto v=in.value("value");double d=v.toDouble();if(!v.isDouble()||!std::isfinite(d)||d<0||d>2147483647.0||d!=std::floor(d))return fail("Yohane source ID malformed");
  const int id=int(d);if(r.getCardOwner(id)!=p||r.getCardPlace(id)!=Player::PlaceHand)return fail("Yohane source ID is not current own hand");
  bool disclosed=false;for(const auto&c:q.worldView.handCards)if(c.effectiveId==id)disclosed=true;
  if(!disclosed)return fail("Yohane source ID lacks owner snapshot disclosure");value=id;
 }else{
  if(!in.value("value").isString())return fail("Yohane source player name malformed");
  const auto name=in.value("value").toString();auto*t=r.findPlayerByObjectName(name);
  if(!t||!q.choiceOptions.playerNames.contains(name)||!q.worldView.playerOrder.contains(name))return fail("Yohane target outside actual offered public players");
  value=QVariant::fromValue(t);
 }
 if(!current(r,q,p,"yohane")||s->yohaneWriting||s->goodEpoch==std::numeric_limits<quint64>::max()||row.generation==std::numeric_limits<quint64>::max())return fail("Yohane source witness stale or exhausted");
 const auto epoch=s->goodEpoch,generation=row.generation;row.known=false;s->yohaneWriting=true;
 const AIRound53YohanePermit permit(r,q,p,key);QString error;bool ok=false;
 try{ok=writeAIRound53YohaneTag(r,q,p,key,value,take,permit,&error);}catch(...){if(s){s->yohaneWriting=false;s->yohaneCard.known=false;s->yohanePlayer.known=false;}throw;}
 if(!s)return fail("Yohane Room or VM ended during prefix");s->yohaneWriting=false;
 auto&after=card?s->yohaneCard:s->yohanePlayer;
 if(!ok||s->goodEpoch!=epoch||after.generation!=generation+1)return fail("Yohane actual source prefix retained but continuation changed");
 if(take)return{{"known",true},{"value",answer}}; // removal stays committed before caller's next face/relation operation
 after.owner=p;after.card=card?value.toInt():-1;after.target=card?nullptr:value.value<ServerPlayer*>();after.known=true;return{{"known",true}};
}

void clearAIRound45InovationState(Room&r){delete lookup(r);}
void observeAIRound45XinyangTagWrite(Room&r,const QString&name){
 if(name=="ai_yohane_card_id"||name=="ai_yohane_player"){if(auto*s=lookup(r)){auto&row=name=="ai_yohane_card_id"?s->yohaneCard:s->yohanePlayer;row.known=false;if(row.generation!=std::numeric_limits<quint64>::max())++row.generation;}return;}
 if(name=="goodatwep"){if(auto*s=lookup(r)){s->goodKnown=false;if(s->goodTagGeneration!=std::numeric_limits<quint64>::max())++s->goodTagGeneration;}return;}
 if(name!="xinyang_judge_card")return;
 if(auto*s=lookup(r)){
  if(s->tagGeneration!=std::numeric_limits<quint64>::max())++s->tagGeneration;
  s->xinyangKnown=false;
  if(s->writing&&!s->entered)s->entered=true;
 }
}
void observeAIRound45XinyangTagRestore(Room&r){
 // Whole-map restore is a genuine writer even if the restored scalar compares
 // equal. Never resurrect prior owner provenance from that equality.
 observeAIRound45XinyangTagWrite(r,QStringLiteral("xinyang_judge_card"));
 observeAIRound45XinyangTagWrite(r,QStringLiteral("goodatwep"));
 observeAIRound45XinyangTagWrite(r,QStringLiteral("ai_yohane_card_id"));
 observeAIRound45XinyangTagWrite(r,QStringLiteral("ai_yohane_player"));
}
void projectAIRound45ShoushiInput(AIRequest&q,Room&r,ServerPlayer*p,const QVariant&data){
 if(!p||q.kind!=AIRequest::Choice||q.choiceOptions.reason!="shoushi")return;
 auto*s=state(r);s->inputs.remove(p->objectName());
 if(!authorized(r,q,p,"shoushi")||!data.canConvert<CardUseStruct>())return;
 const auto use=data.value<CardUseStruct>();if(!use.card)return;
 // This finite native Choice follows the public PreCardUsed Shoushi branch. It clones
 // only the three printed scalars, never the live card's flags/tags/materials.
 if(!use.card->isKindOf("Slash")&&!use.card->isKindOf("Snatch")&&!use.card->isKindOf("Dismantlement"))return;
 Input i;i.owner=p;i.decision=q.decisionId;i.revision=q.stateRevision;
 i.card=AIOwnerReadonlyCardStore::observe(use.card);if(!i.card||!i.card())return;
 i.name=use.card->objectName();i.suit=int(use.card->getSuit());i.number=use.card->getNumber();
 s->inputs.insert(p->objectName(),i);
}
bool aiRound45ShoushiConstruction(const AIRequest&q,const ServerPlayer*owner,const QJsonObject&spec){
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
QJsonObject queryAIRound45Shoushi(Room&r,const AIRequest&q,ServerPlayer*p,const QString&op,const QString&name){
 if(!authorized(r,q,p,"shoushi")||q.choiceOptions.reason!="shoushi")return fail("Shoushi current owner question unavailable");
 const bool read=op=="read";
 if(read?(q.kind!=AIRequest::PlayerChosen||!name.isEmpty()):(q.kind!=AIRequest::Choice||(op!="extra"&&op!="remove")))
  return fail("Shoushi operation is not the current source question");
 auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();
 if(!L||!lua_checkstack(L,2048))return fail("Shoushi actual game VM unavailable");
 Stack stack(L);if(!pushAIRound45ActualAISelf(r,p,L))return fail("Shoushi actual own AI binding unavailable");
 const int self=lua_absindex(L,-1);const void*identity=lua_topointer(L,self);
 if(read){
  const Slot extra=readSlot(r,L,self,"shoushi_extra_target");
  if(!extra.known)return fail("Shoushi actual extra slot is not a witnessed Player, nil, or false");
  // Preserve the actual source short circuit: do not inspect remove when the
  // first slot is truthy. No shadow/VM-reset/legacy-write continuity inference.
  const Slot value=extra.name.isEmpty()?readSlot(r,L,self,"shoushi_remove_target"):extra;
  if(!value.known||!sameSelf(r,q,p,L,identity))return fail("Shoushi actual slot read lost current binding");
  return{{"known",true},{"value",value.name.isEmpty()?QJsonValue(false):QJsonValue(value.name)},{"source_false",value.sourceFalse}};
 }
 ServerPlayer*target=nullptr;
 if(!name.isEmpty()){
  bool visible=q.worldView.self.objectName==name;for(const auto&v:q.worldView.players)if(v.objectName==name)visible=true;
  target=r.findPlayerByObjectName(name,true);
  if(!visible||!target||target->getRoom()!=&r)return fail("Shoushi source target is not a current public player");
 }
 const char*key=op=="extra"?"shoushi_extra_target":"shoushi_remove_target";
 SlotWrite write{&r,&q,p,target,key,identity};
 lua_pushcfunction(L,assignSlot);lua_pushlightuserdata(L,&write);
 if(LuaRuntime::protectedCall(L,1,0,0)!=LUA_OK)return fail("Shoushi native source assignment did not complete; reached prefix retained");
 if(!sameSelf(r,q,p,L,identity))return fail("Shoushi native source assignment lost current binding");
 return{{"known",true}};
}
QJsonObject queryAIRound45Xinyang(Room&r,const AIRequest&q,ServerPlayer*p,const QString&op,int id){
 if(!authorized(r,q,p,"xinyang"))return fail("Xinyang owner/current question unavailable");
 QPointer<State>s=state(r);int answer=-1,write=-1;
 if(op=="write"){
  if(q.kind!=AIRequest::SkillInvoke||q.choiceOptions.reason!="xinyang_judge"||id<0
   ||!p->getPile("xinyang").contains(id)||!disclosed(q,id)||!q.choiceOptions.context.value("judge").isObject())
   return fail("Xinyang selected material is not current disclosed owner judge input");
  write=id;
 }else if(op=="take"){
  if(q.kind!=AIRequest::AmazingGrace||q.choiceOptions.reason!="xinyang"||id!=-1)
   return fail("Xinyang reader requires current AG question");
  if(!s->xinyangKnown||s->xinyangOwner!=p)
   return fail("Xinyang actual tag lacks this owner's witnessed source write");
  // Every actual named writer/whole-map restore revokes provenance. Equality
  // is only an integrity check, NEVER a way to resurrect a cleared record.
  const auto actual=r.getTag("xinyang_judge_card");
  if(actual.metaType().id()!=QMetaType::Int||actual.toInt()!=s->xinyangValue){s->xinyangKnown=false;return fail("Xinyang actual tag no longer matches witnessed write");}
  answer=s->xinyangValue;
 }else return fail("unknown Xinyang source operation");
 if(s->writing||s->tagGeneration==std::numeric_limits<quint64>::max())return fail("Xinyang setter is already active or generation exhausted");
 const auto generation=s->tagGeneration;s->xinyangKnown=false;s->writing=true;s->entered=false;
 const AIRound45XinyangWritePermit permit(r,q,p);
 QString error;bool accepted=false;
 try{accepted=writeAIRound45XinyangTag(r,q,p,write,permit,&error);}catch(...){if(s){s->writing=false;s->xinyangKnown=false;}throw;}
 if(!s)return fail("Xinyang source setter outlived its Room/VM state");
 s->writing=false;
 if(!accepted||generation+1!=s->tagGeneration||!s->entered)return fail("Xinyang setter was replaced or invalidated during its actual prefix");
 s->xinyangKnown=true;s->xinyangOwner=p;s->xinyangValue=write;
 return op=="take"?QJsonObject{{"known",true},{"value",answer}}:QJsonObject{{"known",true}};
}
