#include "ai-strategy-round53-chuli.h"
#include "ai-strategy-round45-inovation-providers.h"
#include "ai-strategy-round44-certificate.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "lua-runtime.h"
#include <QSet>
#include <QJsonArray>
#include <cmath>
#include "ai-strategy-round45-lua-inspect.h"
namespace {
constexpr const char*key="chuli_id_choice";
QJsonObject no(const char*s){return{{"known",false},{"reason",QString::fromLatin1(s)}};}
bool current(Room&r,const AIRequest&q,ServerPlayer*p){return p&&r.getAllPlayers(true).contains(p)&&p->getRoom()==&r&&r.roomRuntime()&&!r.roomRuntime()->isClosing()&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&aiRound44RangeRequestCurrent(r,q,p)&&Sanguosha&&Sanguosha->currentRoom()==&r;}
bool play(const AIRequest&q){
 if(q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY&&q.pattern.isEmpty())return true;
 const auto c=q.worldView.strategyFacts.value("readonly_play_context").toObject();const auto s=c.value("source").toString();
 if(c.value("known")!=true||c.value("readonly")!=true||c.value("viewer")!=q.viewerObjectName||c.value("decision_id")!=q.getDecisionId()||c.value("state_revision")!=q.getStateRevision()||c.value("kind")!="activate"||c.value("reason")!=int(CardUseStruct::CARD_USE_REASON_PLAY)||c.value("pattern")!=""||c.value("handling_method")!=int(Card::MethodUse))return false;
 if(q.kind==AIRequest::SkillInvoke)return(s=="olClan.zuheta"&&(q.choiceOptions.reason=="zuheta"||q.choiceOptions.reason=="zuheta$-1"))||(s=="OverseasVersion.ov_xiafeng"&&q.choiceOptions.reason=="ov_xiafeng");
 if(q.kind==AIRequest::Choice)return s=="OverseasVersion.ov_shelie"&&q.choiceOptions.reason=="ov_shelie"&&q.choiceOptions.choices==QStringList{"Player_Draw","Player_Play"};
 return s=="OverseasVersion.ov_niwo"&&q.kind==AIRequest::UseCard&&q.pattern=="@@ov_niwo"&&q.prompt=="ov_niwo0"&&q.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&q.handlingMethod==Card::MethodUse;
}
bool writer(const AIRequest&q){return q.decisionId&&!q.choiceOptions.context.contains("r53_chuli_event")&&(play(q)||(q.kind==AIRequest::UseCard&&q.pattern=="@@heg_yanxi"));}
bool integer(const QJsonValue&v,int&out){double d=v.toDouble();if(!v.isDouble()||!std::isfinite(d)||d<0||d>2147483647.0||d!=std::floor(d))return false;out=int(d);return true;}
bool disclosed(Room&r,const AIRequest&q,ServerPlayer*t,int id){
 if(r.getCardOwner(id)!=t)return false;const auto place=r.getCardPlace(id);if(place!=Player::PlaceHand&&place!=Player::PlaceEquip)return false;
 const AIPlayerView*v=nullptr;if(q.worldView.self.objectName==t->objectName())v=&q.worldView.self;else for(const auto&x:q.worldView.players)if(x.objectName==t->objectName()){v=&x;break;}if(!v)return false;
 auto has=[&](const QList<AICardView>&cards){for(const auto&c:cards)if(c.effectiveId==id)return true;return false;};
 if(place==Player::PlaceEquip)return has(v->equips);
 return t->objectName()==q.viewerObjectName?has(q.worldView.handCards):has(v->knownCards);
}
bool field(lua_State*L,int self){QSet<const void*>seen;lua_pushvalue(L,self);for(int n=0;n<16;++n){if(!lua_istable(L,-1)||seen.contains(lua_topointer(L,-1)))return false;seen.insert(lua_topointer(L,-1));int row=lua_absindex(L,-1);AIRound45Inspect::field(L,row,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);if(!lua_getmetatable(L,row)){lua_pushnil(L);return true;}AIRound45Inspect::field(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;}return false;}
bool assignment(lua_State*L,int self){AIRound45Inspect::field(L,self,key);bool present=!lua_isnil(L,-1);lua_pop(L,1);if(present||!lua_getmetatable(L,self))return true;AIRound45Inspect::field(L,-1,"__newindex");bool absent=lua_isnil(L,-1);lua_pop(L,2);return absent;}
struct Op{Room*r;const AIRequest*q;ServerPlayer*p;QString op,name;int id=-1;QJsonObject result;const char*error=nullptr;};
int fail(lua_State*L,Op*w,const char*s){w->error=s;return luaL_error(L,"%s",s);}
int execute(lua_State*L){
 auto*w=static_cast<Op*>(lua_touserdata(L,1));if(!lua_checkstack(L,4096))return fail(L,w,"Chuli map frame capacity unavailable");
 int fresh=0;if(w->op=="reset"){lua_newtable(L);fresh=lua_absindex(L,-1);}
 const auto name=w->name.toUtf8();lua_pushlstring(L,name.constData(),size_t(name.size()));int nameKey=lua_absindex(L,-1);
 if(!current(*w->r,*w->q,w->p)||!pushAIRound45ActualAISelf(*w->r,w->p,L))return fail(L,w,"Chuli actual current Self unavailable");int self=lua_absindex(L,-1);
 if(w->op=="reset"){
  if(!assignment(L,self)||!current(*w->r,*w->q,w->p))return fail(L,w,"Chuli map source assignment handler changed");
  lua_pushstring(L,key);lua_pushvalue(L,fresh);lua_settable(L,self);
 }else{
  if(!field(L,self))return fail(L,w,"Chuli actual field lookup changed");int map=lua_absindex(L,-1);
  if(w->op=="state"){
   if(lua_isnil(L,map)||lua_type(L,map)==LUA_TBOOLEAN){w->result={{"known",true},{"type",lua_isnil(L,map)?"nil":"boolean"},{"value",bool(lua_toboolean(L,map))}};}
   else if(lua_istable(L,map)&&!lua_getmetatable(L,map))w->result={{"known",true},{"type","map"}};
   else return fail(L,w,"Chuli current slot is outside its fixed scalar-map domain");
  }else{
   if(lua_isnil(L,map)){w->result={{"known",true},{"type","nil_map"}};}
   else if(!lua_istable(L,map)||lua_getmetatable(L,map))return fail(L,w,"Chuli source map changed type or handlers");
   else if(w->op=="set"){
    if(!current(*w->r,*w->q,w->p))return fail(L,w,"Chuli request changed before reached entry write");
    lua_pushvalue(L,nameKey);lua_pushinteger(L,w->id);lua_settable(L,map);
   }else{
    lua_pushvalue(L,nameKey);lua_rawget(L,map);
    if(lua_isnil(L,-1))w->result={{"known",true},{"type","nil"}};
    else if(lua_type(L,-1)==LUA_TNUMBER){double n=lua_tonumber(L,-1);if(!std::isfinite(n)||n<0||n>2147483647.0||n!=std::floor(n))return fail(L,w,"Chuli saved source entry is not an ID");// An actual own slot is not disclosure authority for a legacy-picked
     // opponent hand ID. TargetSpecified precedes the native discard/reveal.
     // Admit only a same-snapshot disclosed, still-current target HE identity.
     auto*target=w->r->findPlayerByObjectName(w->name,true);
     if(!target||!disclosed(*w->r,*w->q,target,int(n)))return fail(L,w,"Chuli saved source ID is not currently disclosed target HE");
     w->result={{"known",true},{"type","number"},{"value",n}};}
    else return fail(L,w,"Chuli actual source entry is outside fixed integer shape");
   }
  }
 }
 if(!current(*w->r,*w->q,w->p)||!pushAIRound45ActualAISelf(*w->r,w->p,L)||!lua_rawequal(L,self,-1))return fail(L,w,"Chuli current Self changed after source operation");
 if(w->op=="reset"){AIRound45Inspect::field(L,self,key);if(!lua_rawequal(L,fresh,-1))return fail(L,w,"Chuli reset table replaced during assignment");}
 if(w->result.isEmpty())w->result={{"known",true}};return 0;
}
}
QJsonObject aiRound53ChuliEvent(const AIEventView&e){
 if(e.triggerEvent!=int(TargetSpecified)||(e.cardClass!="ChuliCard"&&e.cardClass!="heg_yanxi"&&e.cardName!="heg_yanxi"))return{};
 return{{"sequence",QString::number(e.sequence)},{"targets",QJsonArray::fromStringList(e.targets)}};
}
QJsonObject queryAIRound53ChuliMap(Room&r,const AIRequest&q,ServerPlayer*p,const QString&op,const QJsonObject&v){
 if(!current(r,q,p))return no("Chuli actual source map requires current owner world");
 const bool write=op=="reset"||op=="set";const auto event=q.choiceOptions.context.value("r53_chuli_event").toObject();
 if(write?!writer(q):(event.isEmpty()||(op!="state"&&op!="entry")))return no("Chuli map operation outside exact source writer/reader");
 QString name;int id=-1;
 if(op=="reset"||op=="state"){if(!v.isEmpty())return no("Chuli map state operation takes no arguments");}
 else{
  if(v.size()!=(op=="set"?2:1)||!v.value("player").isString())return no("Chuli map entry shape changed");name=v.value("player").toString();auto*t=r.findPlayerByObjectName(name,true);if(!t)return no("Chuli map key is outside actual roster");
  if(write){if(!integer(v.value("id"),id)||!disclosed(r,q,t,id)||(q.pattern=="@@heg_yanxi"&&r.getCardPlace(id)!=Player::PlaceHand))return no("Chuli selected source ID lacks current disclosed target HE/hand custody");}
  else if(!event.value("targets").toArray().contains(name))return no("Chuli reader key outside actual public event targets");
 }
 auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();if(!L||!lua_checkstack(L,4096))return no("Chuli gameplay VM unavailable");int top=lua_gettop(L);struct Restore{lua_State*L;int n;~Restore(){lua_settop(L,n);}}restore{L,top};
 Op w{&r,&q,p,op,name,id};lua_pushcfunction(L,execute);lua_pushlightuserdata(L,&w);if(LuaRuntime::protectedCall(L,1,0,0)!=LUA_OK)return no(w.error?w.error:"Chuli protected source map operation failed");return w.result;
}
