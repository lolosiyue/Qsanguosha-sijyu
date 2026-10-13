#include "ai-strategy-round56-bootstrap-source.h"
#include "ai-strategy-room-ledger.h"
#include "ai-strategy-round53-lianzhan.h"
#include "ai-strategy-round45-inovation-providers.h"
#include "ai-strategy-round45-slot-bindings.h"
#include "ai-strategy-round45-code-seal.h"
#include "ai-strategy-round45-native-seal.h"
#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-card-identity.h"
#include "ai.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "lua-runtime.h"
#include "card.h"
#include "card-lifetime-manager.h"
#include <QWeakPointer>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QJsonArray>
#include <cstring>
#include "ai-strategy-round45-lua-inspect.h"
namespace {
// Exact native lexical producer; never exported to Lua and no generic C-entry
// privilege. The source issuer also requires its original Prepared bootstrap.
struct BootstrapInstall {
 Room*room;lua_State*L;quint64 generation;int depth;BootstrapInstall*previous;bool accepted=false;
};
thread_local BootstrapInstall*bootstrapInstall=nullptr;
int installCanonicalLianzhanBinding(lua_State*state){
 // Provenance is optional instrumentation. Its refusal must neither skip the
 // original installer nor add an authored bootstrap failure.
 if(!lua_checkstack(state,4096))
  return luaL_error(state,"Lianzhan canonical CardUseStruct binding changed");
 auto*f=bootstrapInstall;const bool sourceReceipt=f&&f->accepted;bool began=false;
 if(sourceReceipt){
  try{began=beginAIRound56LianzhanFinalizerTransition(*f->room,state);}catch(...){}
  if(!began)retireAIRound56BootstrapSourceBoundary(*f->room);
 }
 const bool installed=installAIRound53LianzhanBinding(state);
 if(!installed){
  if(sourceReceipt)retireAIRound56BootstrapSourceBoundary(*f->room);
  return luaL_error(state,"Lianzhan canonical CardUseStruct binding changed");
 }
 if(sourceReceipt&&began){
  bool completed=false;
  try{completed=completeAIRound56LianzhanFinalizerTransition(*f->room,state);}catch(...){}
  if(!completed)retireAIRound56BootstrapSourceBoundary(*f->room);
 }
 return 0;
}
struct BootstrapInstallScope {
 BootstrapInstall frame;
 BootstrapInstallScope(Room&r,lua_State*L):frame{&r,L,r.roomRuntime()->lua().generation(),r.roomRuntime()->lua().protectedCallDepth(),bootstrapInstall}{bootstrapInstall=&frame;}
 ~BootstrapInstallScope(){bootstrapInstall=frame.previous;}
};
bool current(const AIRequest&q,ServerPlayer*p){return p&&p->getRoom()&&p->getRoom()->roomRuntime()&&!p->getRoom()->roomRuntime()->isClosing()&&q.decisionId&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&Sanguosha->currentRoom()==p->getRoom()&&aiRound44RangeRequestCurrent(*p->getRoom(),q,p);}
bool scope(const AIRequest&q,bool write){return q.choiceOptions.reason=="lianzhan"&&(write?(q.kind==AIRequest::SkillInvoke&&q.choiceOptions.question=="askForSkillInvoke"):(q.kind==AIRequest::PlayerChosen&&q.choiceOptions.question=="askForPlayerChosen"));}
QString name(Room&r,const ServerPlayer*p){for(auto*x:r.getAllPlayers(true))if(x==p)return x->objectName();return{};}
QJsonArray names(Room&r,const QList<ServerPlayer*>&ps,bool&ok){QJsonArray out;ok=true;for(auto*p:ps){auto n=name(r,p);if(n.isEmpty()){ok=false;return{};}out.append(n);}return out;}
QJsonObject face(const Card*c){return{{"name",c->objectName()},{"class",c->getClassName()},{"skill",c->getSkillName(false)},{"suit",int(c->getSuit())},{"number",c->getNumber()},{"virtual",c->isVirtualCard()},{"id",c->getEffectiveId()}};}
// Borrow exactly the actual native source, including virtual damage cards.
// This lease never owns/deletes the Card and never mints a readonly recipe.
struct BorrowedCard {
 QPointer<const Card> object;std::shared_ptr<const CardLifetimeToken> token;CardLifetimeLease lease;
 explicit BorrowedCard(const Card*c):object(c),token(globalCardLifetimeManager().liveToken(c)),lease(globalCardLifetimeManager(),token){}
 const Card*live()const{return !object.isNull()&&token&&token->live&&lease.isValid()&&globalCardLifetimeManager().liveToken(object.data())==token?object.data():nullptr;}
};
using Borrow=std::shared_ptr<BorrowedCard>;
struct Row{QPointer<ServerPlayer>owner;Borrow card;QJsonObject observedFace;};
struct Capture {QPointer<ServerPlayer>owner;QWeakPointer<AIConversionLedger>ledger;quint64 decision=0,revision=0;std::shared_ptr<const CardUseStruct>use;Borrow card;};
class Ledger final:public AIRoomLedger{public:explicit Ledger(Room&r):AIRoomLedger(r){}quint64 generation=0;QHash<const ServerPlayer*,Row>rows;QHash<quint64,Capture>captures;};
Ledger*ledger(Room&r,bool create=false){for(auto*x:r.children())if(auto*l=dynamic_cast<Ledger*>(x))return l;return create?new Ledger(r):nullptr;}
bool field(lua_State*L,int index,const char*key){QSet<const void*>seen;lua_pushvalue(L,index);for(int depth=0;depth<16;++depth){if(!lua_istable(L,-1)||seen.contains(lua_topointer(L,-1)))return false;seen.insert(lua_topointer(L,-1));const int row=lua_absindex(L,-1);AIRound45Inspect::field(L,row,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);if(!lua_getmetatable(L,row)){lua_pushnil(L);return true;}AIRound45Inspect::field(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;}return false;}
bool assignment(lua_State*L,int self){AIRound45Inspect::field(L,self,"lianzhanUse");const bool exists=!lua_isnil(L,-1);lua_pop(L,1);if(exists||!lua_getmetatable(L,self))return true;AIRound45Inspect::field(L,-1,"__newindex");const bool absent=lua_isnil(L,-1);lua_pop(L,2);return absent;}
bool source(Room&r,lua_State*L,bool write){const int top=lua_gettop(L);struct Restore{lua_State*L;int n;~Restore(){lua_settop(L,n);}}restore{L,top};AIRound45Inspect::globals(L);const int env=lua_absindex(L,-1);AIRound45Inspect::field(L,env,"sgs");if(!lua_istable(L,-1))return false;AIRound45Inspect::field(L,-1,write?"ai_skill_invoke":"ai_skill_playerchosen");if(!lua_istable(L,-1))return false;AIRound45Inspect::field(L,-1,"lianzhan");const int f=lua_absindex(L,-1);if(!matchAIRound45PinnedFunction(r,L,f,"lua/ai/tenyear_hc-ai.lua",write?8972:8978,write?8976:8985))return false;for(int i=1;;++i){const char*k=lua_getupvalue(L,f,i);if(!k)break;const bool ok=!std::strcmp(k,"_ENV")&&lua_rawequal(L,env,-1);lua_pop(L,1);if(!ok)return false;}return true;}
struct Call{const AIRequest*q;ServerPlayer*p;bool write;Borrow lease;std::shared_ptr<const CardUseStruct>sourceUse;QJsonObject observedFace,result;QString error;std::function<QJsonObject(const Card*)>profile;};
int run(lua_State*L){auto*w=static_cast<Call*>(lua_touserdata(L,1));auto fail=[&](const char*s){w->error=QString::fromLatin1(s);return luaL_error(L,"%s",s);};if(!lua_checkstack(L,4096))return fail("Lianzhan source slot stack unavailable");Room&r=*w->p->getRoom();int value=0;
 if(w->write){if(!pushAIRound53LianzhanUse(L,*w->sourceUse))return fail("Lianzhan source CardUse binding unavailable");value=lua_absindex(L,-1);}
 if(!current(*w->q,w->p)||!pushAIRound45ActualAISelf(r,w->p,L))return fail("Lianzhan actual Self unavailable");const int self=lua_absindex(L,-1);
 if(!source(r,L,w->write)||(!w->write&&(!matchAIRound45NativeClassMethod(r,L,"SPlayerList","length")||!matchAIRound45NativeClassMethod(r,L,"SPlayerList","at"))))return fail("Lianzhan actual source provider changed");
 if(w->write){
  if(!assignment(L,self)||!current(*w->q,w->p)||w->lease->live()!=w->sourceUse->card)return fail("Lianzhan assignment dispatch or source custody changed");
  lua_pushvalue(L,value);lua_setfield(L,self,"lianzhanUse");w->result={{"written",true}};
 }else{
  if(!field(L,self,"lianzhanUse"))return fail("Lianzhan saved field requires unproved getter");value=lua_absindex(L,-1);
  if(lua_isnil(L,value)||lua_isboolean(L,value)){if(AIRound45Inspect::primitiveMetatable(L,lua_type(L,value)))return fail("Lianzhan primitive metatable changed");w->result={{"source_error","source lianzhanUse is not a CardUse value"}};return 0;}
  const Card*savedCard=nullptr;ServerPlayer*savedFrom=nullptr;QList<ServerPlayer*>savedTo;if(!readAIRound53LianzhanUse(L,value,savedCard,savedFrom,savedTo))return fail("Lianzhan current slot is not its genuine source CardUse value");
  // Native copy retains exactly the original Card. Whole from/card/to is read
  // from the current actual userdata; no stale isolated plan is rehydrated.
  auto*l=ledger(r);if(!l||l->generation!=r.roomRuntime()->lua().generation())return fail("Lianzhan source Card custody unavailable");const auto row=l->rows.value(w->p);w->lease=row.card;
  const Card*c=w->lease?w->lease->live():nullptr;if(row.owner!=w->p||!c||(savedCard&&savedCard!=c)||(savedCard&&face(c)!=row.observedFace))return fail("Lianzhan source Card identity or disclosed face changed");
  const QString from=savedFrom?name(r,savedFrom):QString();if(savedFrom&&from.isEmpty())return fail("Lianzhan source actor left current roster");bool ok=false;const auto to=names(r,savedTo,ok);if(!ok)return fail("Lianzhan source target left current roster");
  // Only the already observed public face is exported. No private card flags,
  // material IDs, target legality, receipt, or evaluation clone is manufactured.
  QJsonObject card;if(savedCard){card=w->profile(c);if(card.isEmpty())return fail("Lianzhan public source face unavailable");card.remove("known_flags");card.remove("flags_complete");card.remove("flags_complete_scope");card.remove("subcards");card.insert("virtual_card",c->isVirtualCard());card.insert("legacy_identity_token",aiLegacyCardIdentity(r,w->p,c));card.insert("evaluation_only",true);}
  w->result={{"status","use"},{"from",savedFrom?QJsonValue(from):QJsonValue(false)},{"card",savedCard?QJsonValue(card):QJsonValue(false)},{"to",to}};
 }
 if(!current(*w->q,w->p)||!w->lease||!w->lease->live()||!pushAIRound45ActualAISelf(r,w->p,L)||!lua_rawequal(L,self,-1))return fail("Lianzhan operation changed source request or Self");return 0;
}
}
void projectAIRound53Lianzhan(AIRequest&q,Room&r,ServerPlayer*p,const QVariant&data){
 // Projection precedes lexical request aliases and canonical execution. Bind
 // the complete native value to the immutable decision + weak ledger, not a
 // temporary stack address; query still requires R44's exact current request.
 auto*l=ledger(r,true);for(auto i=l->captures.begin();i!=l->captures.end();)if(i->ledger.isNull())i=l->captures.erase(i);else ++i;
 l->captures.remove(q.decisionId);
 if(!p||p->getRoom()!=&r||!q.decisionId||q.conversionLedger.isNull()||q.viewerObjectName!=p->objectName()||q.worldView.self.objectName!=p->objectName()||!scope(q,true)||data.metaType().id()!=qMetaTypeId<CardUseStruct>())return;
 const auto use=data.value<CardUseStruct>();bool ok=false;names(r,use.to,ok);
 if(use.from!=p||!use.card||!use.card->isDamageCard()||use.to.size()!=1||!ok)return;
 auto copy=std::make_shared<CardUseStruct>(use);auto borrowed=std::make_shared<BorrowedCard>(use.card);if(borrowed->live()!=use.card)return;
 Capture cap;cap.owner=p;cap.ledger=q.conversionLedger;cap.decision=q.decisionId;cap.revision=q.stateRevision;cap.use=std::move(copy);cap.card=std::move(borrowed);l->captures.insert(q.decisionId,std::move(cap));
}
bool registerAIRound53LianzhanSources(Room&r,lua_State*L,QString*why){
 if(!registerAIRound45PinnedSource(r,L,"lua/ai/tenyear_hc-ai.lua",QByteArray("94a38f49a220eeb0f0816dc9737c868027cc32ebba6001edfab1c304364799f5"),why))return false;
 BootstrapInstallScope lexicalInstaller(r,L);
 const int top=lua_gettop(L);lua_pushcfunction(L,installCanonicalLianzhanBinding);
 const bool ok=LuaRuntime::protectedCall(L,0,0,0)==LUA_OK;if(!ok&&why)*why=QStringLiteral("Lianzhan canonical native binding bootstrap failed");lua_settop(L,top);return ok;
}
bool matchAIRound56LianzhanBootstrapEntry(Room&r,lua_State*L,int arguments)noexcept{
 auto*f=bootstrapInstall;auto*rt=r.roomRuntime();
 if(!f||f->room!=&r||f->L!=L||!rt||rt->isClosing()||rt->lua().rawState()!=L
  ||rt->lua().generation()!=f->generation||rt->lua().protectedCallDepth()!=f->depth
  ||arguments!=0||lua_gettop(L)<1||LuaRuntime::current()!=&rt->lua())return false;
 // Borrow exact pending value; no stack growth, upvalue push or Lua call.
 const auto*v=AIRound45Inspect::value(L,-1);
 const bool ok=v&&ttislcf(v)&&fvalue(v)==installCanonicalLianzhanBinding;
 if(ok)f->accepted=true;return ok;
}
void clearAIRound53LianzhanSlots(Room&r){if(auto*l=ledger(r))delete l;}
QJsonObject queryAIRound53LianzhanSlot(const AIRequest&q,ServerPlayer*p,const QString&op,const QJsonObject&payload,const std::function<QJsonObject(const Card*)>&profile){auto fail=[](const QString&s){return QJsonObject{{"error",s},{"key","lianzhan.saved_use"}};};if((op!="write"&&op!="read")||!current(q,p)||!scope(q,op=="write"))return fail("Lianzhan exact current source question unavailable");Call w;w.q=&q;w.p=p;w.write=op=="write";w.profile=profile;Room&r=*p->getRoom();
 if(w.write){auto*l=ledger(r);if(!l)return fail("Lianzhan current native source capture unavailable");const auto capture=l->captures.value(q.decisionId);if(capture.owner!=p||capture.decision!=q.decisionId||capture.revision!=q.stateRevision||capture.ledger.toStrongRef()!=q.conversionLedger||!capture.use||!capture.card||capture.card->live()!=capture.use->card)return fail("Lianzhan current native source capture changed");w.sourceUse=capture.use;w.lease=capture.card;if(payload.size()!=3||!payload.value("from").isString()||!payload.value("card_identity").isString()||!payload.value("to").isArray())return fail("Lianzhan source use envelope malformed");const auto&use=*w.sourceUse;bool ok=false;const auto to=names(r,use.to,ok);if(!ok||use.from!=p||!w.lease||w.lease->live()!=use.card||payload.value("from")!=p->objectName()||payload.value("to").toArray()!=to||payload.value("card_identity").toString()!=aiLegacyCardIdentity(r,p,use.card))return fail("Lianzhan DTO differs from exact native question source");w.observedFace=face(use.card);}
 else if(!payload.isEmpty()||!profile)return fail("Lianzhan read envelope malformed");
 auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();if(!L||!lua_checkstack(L,4096))return fail("Lianzhan game VM unavailable");const int top=lua_gettop(L);struct Restore{lua_State*L;int n;~Restore(){lua_settop(L,n);}}restore{L,top};lua_pushcfunction(L,run);lua_pushlightuserdata(L,&w);if(LuaRuntime::protectedCall(L,1,0,0)!=LUA_OK)return fail(w.error.isEmpty()?QStringLiteral("Lianzhan protected operation failed"):w.error);
 if(w.write){auto*l=ledger(r,true);if(l->generation&&l->generation!=vm.generation())return fail("Lianzhan game generation changed");l->generation=vm.generation();l->rows[p]={p,std::move(w.lease),w.observedFace};}return w.result;
}

bool inAIRound56LianzhanBootstrapInstaller(Room&r,lua_State*L)noexcept{
 auto*f=bootstrapInstall;auto*rt=r.roomRuntime();
 if(!f||!f->accepted||f->room!=&r||f->L!=L||!rt||rt->isClosing()||rt->lua().rawState()!=L
  ||rt->lua().generation()!=f->generation||rt->lua().protectedCallDepth()!=f->depth+1
  ||LuaRuntime::current()!=&rt->lua()||!L->ci||isLua(L->ci))return false;
 const auto*v=s2v(L->ci->func.p);
 return ttislcf(v)&&fvalue(v)==installCanonicalLianzhanBinding;
}
