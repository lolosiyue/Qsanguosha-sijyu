#include "ai-strategy-round51-manjuan.h"
#include "ai-strategy-round45-inovation-providers.h"
#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-card-identity.h"
#include "card-lifetime-manager.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "lua-runtime.h"
#include <QSet>
#include <cmath>
#include "ai-strategy-round45-lua-inspect.h"
namespace {
constexpr const char*slot="kezhuanmanjuan_id";
QJsonObject unknown(const char*s){return{{"known",false},{"reason",QString::fromLatin1(s)}};}
bool current(Room&r,const AIRequest&q,ServerPlayer*p){return p&&r.getAllPlayers(true).contains(p)&&p->getRoom()==&r&&r.roomRuntime()&&!r.roomRuntime()->isClosing()&&q.decisionId&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&aiRound44RangeRequestCurrent(r,q,p)&&Sanguosha&&Sanguosha->currentRoom()==&r;}
bool producer(const AIRequest&q){return q.kind==AIRequest::Activate||q.kind==AIRequest::RespondCard||q.kind==AIRequest::UseCard;}
bool reader(const AIRequest&q){return q.kind==AIRequest::AmazingGrace&&q.choiceOptions.reason=="kezhuanmanjuan"&&q.choiceOptions.question=="askForAG";}
bool integer(const QJsonValue&v,int&out){double d=v.toDouble();if(!v.isDouble()||!std::isfinite(d)||d<0||d>2147483647.0||d!=std::floor(d))return false;out=int(d);return true;}
bool field(lua_State*L,int self){QSet<const void*>seen;lua_pushvalue(L,self);for(int n=0;n<16;++n){if(!lua_istable(L,-1)||seen.contains(lua_topointer(L,-1)))return false;seen.insert(lua_topointer(L,-1));int row=lua_absindex(L,-1);AIRound45Inspect::field(L,row,slot);if(!lua_isnil(L,-1))return true;lua_pop(L,1);if(!lua_getmetatable(L,row)){lua_pushnil(L);return true;}AIRound45Inspect::field(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;}return false;}
bool assignment(lua_State*L,int self){AIRound45Inspect::field(L,self,slot);bool present=!lua_isnil(L,-1);lua_pop(L,1);if(present||!lua_getmetatable(L,self))return true;AIRound45Inspect::field(L,-1,"__newindex");bool absent=lua_isnil(L,-1);lua_pop(L,2);return absent;}
struct Write{Room*r;const AIRequest*q;ServerPlayer*p;int id;const char*error=nullptr;};
int fail(lua_State*L,Write*w,const char*s){w->error=s;return luaL_error(L,"%s",s);}
int assign(lua_State*L){
 auto*w=static_cast<Write*>(lua_touserdata(L,1));if(!lua_checkstack(L,4096))return fail(L,w,"Manjuan assignment frame capacity unavailable");
 lua_pushstring(L,slot);int key=lua_absindex(L,-1);lua_pushinteger(L,w->id);int value=lua_absindex(L,-1);
 if(!current(*w->r,*w->q,w->p)||!pushAIRound45ActualAISelf(*w->r,w->p,L))return fail(L,w,"Manjuan current actual Self unavailable before write");int self=lua_absindex(L,-1);
 if(!assignment(L,self)||!current(*w->r,*w->q,w->p))return fail(L,w,"Manjuan source assignment handler changed");
 lua_pushvalue(L,key);lua_pushvalue(L,value);lua_settable(L,self);
 if(!current(*w->r,*w->q,w->p)||!pushAIRound45ActualAISelf(*w->r,w->p,L)||!lua_rawequal(L,self,-1))return fail(L,w,"Manjuan current Self changed after source prefix");
 AIRound45Inspect::field(L,self,slot);if(!lua_rawequal(L,value,-1))return fail(L,w,"Manjuan assigned source slot changed");return 0;
}
bool same(Room&r,ServerPlayer*p,const AICardView&v,const Card*c,int id){
 if(!c||v.effectiveId!=id||c->getEffectiveId()!=id||v.legacyIdentityToken.isEmpty()||v.legacyIdentityToken!=aiLegacyCardIdentity(r,p,c))return false;
 if(v.nativeIdentity&&(!globalCardLifetimeManager().isLive(v.nativeIdentity)||v.nativeIdentity->address!=c))return false;
 return v.objectName==c->objectName()&&v.className==c->getClassName()&&v.suit==int(c->getSuit())&&v.number==c->getNumber()&&v.skillName==c->getSkillName(false)&&v.subcardIds==c->getSubcards()&&v.virtualCard==c->isVirtualCard();
}
const Card*disclosed(Room&r,const AIRequest&q,ServerPlayer*p,int id){
 // Classify lawful location BEFORE reading/comparing any current face. A hidden
 // foreign card is always unknown, even if its old printed/known face matches.
 const auto place=r.getCardPlace(id);const bool own=r.getCardOwner(id)==p;
 if(place!=Player::DiscardPile&&!own)return nullptr;
 const Card*c=Sanguosha->getCard(id);if(!c)return nullptr;
 if(place==Player::DiscardPile){for(const auto&v:q.worldView.discardPile)if(same(r,p,v,c,id))return c;return nullptr;}
 if(place==Player::PlaceHand){for(const auto&v:q.worldView.handCards)if(same(r,p,v,c,id))return c;}
 else if(place==Player::PlaceEquip){for(const auto&v:q.worldView.self.equips)if(same(r,p,v,c,id))return c;}
 else if(place==Player::PlaceJudge){for(const auto&v:q.worldView.self.judgingArea)if(same(r,p,v,c,id))return c;}
 else if(place==Player::PlaceSpecial){for(const auto&pile:q.worldView.self.piles)if(p->getPile(pile.name).contains(id))for(const auto&v:pile.cards)if(same(r,p,v,c,id))return c;}
 return nullptr;
}
}
QJsonObject queryAIRound51Manjuan(Room&r,const AIRequest&q,ServerPlayer*p,const QString&op,const QJsonObject&input){
 if(!current(r,q,p))return unknown("Manjuan requires current native owner request");
 if(op=="read"){
  if(!reader(q)||!input.isEmpty())return unknown("Manjuan reader requires exact current AG question");
  auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();if(!L||!lua_checkstack(L,4096))return unknown("Manjuan gameplay VM unavailable");int top=lua_gettop(L);struct Restore{lua_State*L;int n;~Restore(){lua_settop(L,n);}}restore{L,top};
  if(!current(r,q,p)||!pushAIRound45ActualAISelf(r,p,L)||!field(L,lua_absindex(L,-1)))return unknown("Manjuan actual source field lookup unavailable");
  QJsonObject out{{"known",true}};switch(lua_type(L,-1)){case LUA_TNIL:out.insert("type","nil");break;case LUA_TBOOLEAN:out.insert("type","boolean");out.insert("value",bool(lua_toboolean(L,-1)));break;case LUA_TNUMBER:{double n=lua_tonumber(L,-1);if(!std::isfinite(n))return unknown("Manjuan nonfinite actual scalar unsupported");out.insert("type","number");out.insert("value",n);break;}case LUA_TSTRING:{size_t n;const char*s=lua_tolstring(L,-1,&n);if(n>256)return unknown("Manjuan non-source string exceeds scalar domain");out.insert("type","string");out.insert("value",QString::fromUtf8(s,int(n)));break;}default:return unknown("Manjuan actual slot is not a scalar");}
  if(!current(r,q,p))return unknown("Manjuan request changed during read");return out;
 }
 if(!producer(q))return unknown("Manjuan cardsview source requires an active use/response request");
 // Fixed collector cardsView enumerates owned skill instances. include_lose=true
 // checks ownership only, not unrelated native activation/invalidity rules.
 // The body enters its catalogue loop only while its actual owner has no hand.
 if(!p->hasSkill("kezhuanmanjuan",true)||!p->isKongcheng()||!current(r,q,p))return unknown("Manjuan source requires its actual skill owner and empty hand");
 if(op=="count"){if(!input.isEmpty())return unknown("Manjuan count takes no arguments");return{{"known",true},{"value",Sanguosha->getCardCount()}};}
 int id;if(!integer(input.value("id"),id)||id>=Sanguosha->getCardCount())return unknown("Manjuan index outside actual source catalogue");
 if(op=="step"){
  if(input.size()!=1)return unknown("Manjuan step argument shape changed");
  // Exact source order: getCard is an opaque reference acquisition; no getter
  // or public-face demand precedes the unconditional actual Self assignment.
  const Card*c=Sanguosha->getCard(id);const auto identity=c?aiLegacyCardIdentity(r,p,c):QString();
  auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();if(!L||!lua_checkstack(L,4096))return unknown("Manjuan gameplay VM unavailable");int top=lua_gettop(L);struct Restore{lua_State*L;int n;~Restore(){lua_settop(L,n);}}restore{L,top};
  Write w{&r,&q,p,id};lua_pushcfunction(L,assign);lua_pushlightuserdata(L,&w);if(LuaRuntime::protectedCall(L,1,0,0)!=LUA_OK)return unknown(w.error?w.error:"Manjuan protected source assignment failed");
  if(!current(r,q,p))return unknown("Manjuan source prefix changed current request");
  return{{"known",true},{"exists",c!=nullptr},{"identity",identity}};
 }
 const bool kind=op=="kind",locked=op=="locked";
 if((op!="number"&&op!="name"&&!kind&&!locked)||input.size()!=(kind||locked?3:2)||!input.value("identity").isString())return unknown("Manjuan finite card query shape unsupported");
 if(kind&&(!input.value("class").isString()||input.value("class").toString().isEmpty()))return unknown("Manjuan source class missing");
 if(locked&&input.value("player")!=p->objectName())return unknown("Manjuan foreign private locked rule is unavailable");
 const Card*c=disclosed(r,q,p,id);if(!c||input.value("identity")!=aiLegacyCardIdentity(r,p,c))return unknown("Manjuan current live face is not lawfully disclosed with same identity");
 QJsonValue value;
 if(op=="number")value=c->getNumber();else if(op=="name")value=c->objectName();else if(kind)value=c->isKindOf(input.value("class").toString().toUtf8().constData());
 else{auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);if(!vm.state())return unknown("Manjuan owner rule gameplay binding unavailable");value=p->isLocked(c);}
 if(!current(r,q,p)||disclosed(r,q,p,id)!=c)return unknown("Manjuan card/current request changed during source query");
 return{{"known",true},{"value",value}};
}
