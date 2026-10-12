#include "ai-strategy/anime.h"

// ---- imagine-focus ----
#include "ai-strategy/lua-seal.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "structs.h"
#include "lua-runtime.h"
#include "lua.hpp"
#include "lobject.h" // native-only immutable child Proto identity; no Lua-facing capability
#include <QHash>
#include <QPointer>
#include <QScopeGuard>
#include <limits>
#include <atomic>
#include "ai-strategy/lua-raw-inspect.h"

namespace {
char capturedKey;
std::atomic<quint64> sequence{0};
constexpr int filterRef=-1, pcallRef=-2, executionRef=-3;
int depth(lua_State*L){lua_Debug a{};int n=0;while(lua_getstack(L,n,&a))++n;return n;}
bool caller(lua_State*L,int level=1,int reference=filterRef){
 lua_Debug a{};if(!lua_getstack(L,level,&a)||!lua_getinfo(L,"f",&a))return false;
 lua_rawgetp(L,LUA_REGISTRYINDEX,&capturedKey);
 if(!lua_istable(L,-1)){lua_pop(L,2);return false;}
 lua_rawgeti(L,-1,reference);bool ok=lua_rawequal(L,-1,-3);lua_pop(L,3);return ok;
}
// lua_topointer returns the actual LClosure GC object for a verified Lua
// function (lapi.c). A new invocation creates a new closure, but its child
// Proto must be the immutable child of the exact captured filter closure.
// Source/line metadata alone would permit a same-name loaded impostor.
bool authenticBody(lua_State*L,int level){
 const int top=lua_gettop(L);const auto restore=qScopeGuard([&]{lua_settop(L,top);});
 lua_Debug a{};if(!lua_getstack(L,level,&a)||!lua_getinfo(L,"f",&a)
    ||!lua_isfunction(L,-1)||lua_iscfunction(L,-1))return false;
 const auto *body=static_cast<const LClosure *>(lua_topointer(L,-1));
 lua_rawgetp(L,LUA_REGISTRYINDEX,&capturedKey);if(!lua_istable(L,-1))return false;
 lua_rawgeti(L,-1,filterRef);if(!lua_isfunction(L,-1)||lua_iscfunction(L,-1))return false;
 const auto *filter=static_cast<const LClosure *>(lua_topointer(L,-1));
 for(int i=0;i<filter->p->sizep;++i)
  if(filter->p->p[i]==body->p&&body->p->linedefined==2491)return true;
 return false;
}
thread_local QHash<Room *,QList<const AIFocusFrame *>> frames;
bool plain(lua_State *L,int i){if(!lua_istable(L,i))return false;if(lua_getmetatable(L,i)){lua_pop(L,1);return false;}return true;}
void rawfield(lua_State *L,int i,const char*k){i=lua_absindex(L,i);lua_pushstring(L,k);lua_rawget(L,i);}
bool imagineFocusSource(lua_State *L,int i,int first,int last){
 if(!lua_isfunction(L,i)||lua_iscfunction(L,i))return false;
 lua_pushvalue(L,i);lua_Debug info{};
 return lua_getinfo(L,">S",&info)&&QString::fromUtf8(info.source?info.source:"").endsWith("/ai/imagine-ai.lua")
  &&info.linedefined==first&&info.lastlinedefined==last;
}
bool filterSource(lua_State *L,int i){
 if(!lua_isfunction(L,i)||lua_iscfunction(L,i))return false;
 lua_pushvalue(L,i);lua_Debug info{};
 return lua_getinfo(L,">S",&info)&&QString::fromUtf8(info.source?info.source:"").endsWith("/ai/smart-ai.lua")
  &&info.linedefined==2477&&info.lastlinedefined==2867;
}
bool currentCallback(lua_State *L,int event,int reference){
 const int top=lua_gettop(L);const auto restore=qScopeGuard([&]{lua_settop(L,top);});
 // The SWIG sgs namespace has a native metatable. Only raw fields are read;
 // the callback registry and event rows must still be plain tables.
 lua_pushglobaltable(L);rawfield(L,-1,"sgs");if(!lua_istable(L,-1))return false;
 rawfield(L,-1,"ai_event_callback");if(!plain(L,-1))return false;
 lua_rawgeti(L,-1,event);if(!plain(L,-1))return false;rawfield(L,-1,"general");
 lua_rawgetp(L,LUA_REGISTRYINDEX,&capturedKey);if(!plain(L,-1))return false;
 lua_rawgeti(L,-1,reference);return lua_rawequal(L,-1,-3);
}
}
struct AIFocusFrame::Data {
 Room *room=nullptr;QPointer<ServerPlayer> actor;QString mark;const QVariant *data=nullptr;int event=0,filterDepth=0;quint64 serial=0;const void *filterBody=nullptr;bool valid=false,attempted=false;
};
struct AIFocusAccess {
 static AIFocusFrame::Data *current(Room *room){
  auto it=frames.constFind(room);return it==frames.cend()||it->isEmpty()?nullptr:it->last()->d.get();
 }
 static int enter(lua_State*L){
  auto *room=static_cast<Room *>(lua_touserdata(L,lua_upvalueindex(1)));auto*f=current(room);
  if(f&&f->valid&&!f->filterDepth&&caller(L,3)&&caller(L,2,pcallRef)&&authenticBody(L,1)){
   lua_Debug a{};if(lua_getstack(L,1,&a)&&lua_getinfo(L,"f",&a)){
    f->filterBody=lua_topointer(L,-1);lua_pop(L,1);f->filterDepth=depth(L);
   }
  }
  return 0;
 }
 static bool bodyCaller(lua_State*L,const AIFocusFrame::Data*f,int level=1){
  lua_Debug a{};if(!lua_getstack(L,level,&a)||!lua_getinfo(L,"f",&a))return false;
  const bool same=lua_topointer(L,-1)==f->filterBody;lua_pop(L,1);return same;
 }
 static int select(lua_State*L){
  auto *room=static_cast<Room *>(lua_touserdata(L,lua_upvalueindex(1)));auto*f=current(room);
  if(lua_gettop(L)!=1||!lua_isfunction(L,1)){lua_pushnil(L);return 1;}
  if(!f||!f->valid||f->attempted||!f->filterDepth||f->filterDepth!=depth(L)||!bodyCaller(L,f)||!caller(L,2,pcallRef)||!caller(L,3)){
   lua_pushvalue(L,1);return 1;
  }
  lua_pushglobaltable(L);rawfield(L,-1,"pcall");lua_rawgetp(L,LUA_REGISTRYINDEX,&capturedKey);
  if(!plain(L,-1)){lua_settop(L,1);lua_pushvalue(L,1);return 1;}
  lua_rawgeti(L,-1,pcallRef);const bool protectedCall=lua_rawequal(L,-1,-3);lua_settop(L,1);
  lua_rawgetp(L,LUA_REGISTRYINDEX,&capturedKey);lua_rawgeti(L,-1,f->event);
  const bool same=lua_rawequal(L,1,-1);lua_settop(L,1);
  if(!protectedCall||!same||!currentCallback(L,f->event,f->event)){lua_pushvalue(L,1);return 1;}
  lua_pushlightuserdata(L,room);lua_pushinteger(L,lua_Integer(f->serial));lua_pushvalue(L,1);
  lua_pushcclosure(L,&invoke,3);return 1;
 }
 static int invoke(lua_State *L){
  auto *room=static_cast<Room *>(lua_touserdata(L,lua_upvalueindex(1)));
  auto *f=current(room);
  if(lua_gettop(L)!=3||!f||!f->valid||f->attempted||!f->actor
     ||f->serial!=quint64(lua_tointeger(L,lua_upvalueindex(2)))
     ||f->actor->getRoom()!=room||depth(L)!=f->filterDepth+1
     ||!caller(L,1,pcallRef)||!bodyCaller(L,f,2)||!caller(L,3,pcallRef)||!caller(L,4)){return luaL_error(L,"Imagine native callback scope expired");}
  lua_pushvalue(L,lua_upvalueindex(3));lua_rawgetp(L,LUA_REGISTRYINDEX,&capturedKey);
  lua_rawgeti(L,-1,f->event);const bool same=lua_rawequal(L,-1,-3);lua_pop(L,3);
  if(!same||!currentCallback(L,f->event,f->event)){return luaL_error(L,"Imagine callback identity changed");}
  // No value supplied by Lua controls target, mark, count or current baseline.
  // Native frame is entered only for this genuine target filterEvent.
  // QVariant is the exact Lua-owned argument copied by LuaAI::filterEvent.
  // Earlier sibling mutations are observed here, not at frame creation.
  int amount;
  if(f->event==int(Damaged))amount=f->data->value<DamageStruct>().damage;
  else amount=f->data->value<RecoverStruct>().recover; // exact QVariant::toRecover value conversion
  const int before=f->actor->getMark(f->mark);
  const qint64 next=qint64(before)+qint64(amount);
  if(next<std::numeric_limits<int>::min()||next>std::numeric_limits<int>::max())
   return luaL_error(L,"Imagine Focus sum is outside native signed integer domain");
  f->attempted=true; // before native reentry: veto/throw is still a reached call.
  lua_rawgetp(L,LUA_REGISTRYINDEX,&capturedKey);lua_rawgeti(L,-1,executionRef);
  const lua_Integer executions=lua_tointeger(L,-1);lua_pop(L,1);
  lua_pushinteger(L,executions<std::numeric_limits<lua_Integer>::max()?executions+1:executions);lua_rawseti(L,-2,executionRef);lua_pop(L,1);
  room->setPlayerMark(f->actor,f->mark,int(next));
  return 0;
 }
};
AIFocusFrame::AIFocusFrame(Room&r,ServerPlayer*p,int event,const QVariant&v):d(new Data){
 d->room=&r;d->actor=p;d->event=event;d->data=&v;d->serial=++sequence;frames[&r].append(this);
 if(!p||p->getRoom()!=&r||!r.getAllPlayers(true).contains(p))return;
 if(event==int(Damaged)&&v.canConvert<DamageStruct>()){
  d->mark="FocusDamageCount";d->valid=true;
 }else if(event==int(HpRecover)&&v.canConvert<RecoverStruct>()){
  d->mark="FocusRecoverCount";d->valid=true;
 }
}
AIFocusFrame::~AIFocusFrame(){auto it=frames.find(d->room);if(it==frames.end())return;it->removeAll(this);if(it->isEmpty())frames.erase(it);}
void installAIFocusCallbacks(Room&r){
 auto *runtime=r.roomRuntime();if(!runtime||runtime->isClosing())return;
 LuaRuntime::Binding binding(runtime->lua(),false);lua_State*L=runtime->lua().state();
 if(!L)return;
 const int top=lua_gettop(L);const auto restore=qScopeGuard([&]{lua_settop(L,top);});
 // The SWIG sgs namespace has a native metatable. Only raw fields are read;
 // the callback registry and event rows must still be plain tables.
 lua_pushglobaltable(L);rawfield(L,-1,"sgs");if(!lua_istable(L,-1))return;
 rawfield(L,-1,"ai_event_callback");if(!plain(L,-1))return;
 const int callbacks=lua_gettop(L);lua_newtable(L);const int refs=lua_gettop(L);
 for(int event:{int(Damaged),int(HpRecover)}){
  lua_rawgeti(L,callbacks,event);if(!plain(L,-1)){lua_pop(L,1);continue;}
  rawfield(L,-1,"general");const int first=event==int(Damaged)?756:762;
  if(imagineFocusSource(L,-1,first,first+5)){lua_pushvalue(L,-1);lua_rawseti(L,refs,event);}
  lua_pop(L,2);
 }
 lua_pushglobaltable(L);rawfield(L,-1,"SmartAI");
 if(lua_istable(L,-1)){
  rawfield(L,-1,"filterEvent");
  if(!lua_isfunction(L,-1)){lua_pop(L,1);rawfield(L,-1,"__instanceDict");if(lua_istable(L,-1))rawfield(L,-1,"filterEvent");}
  if(filterSource(L,-1)){lua_pushvalue(L,-1);lua_rawseti(L,refs,filterRef);}
 }
 lua_settop(L,refs);lua_pushglobaltable(L);rawfield(L,-1,"pcall");
 if(lua_iscfunction(L,-1)){lua_pushvalue(L,-1);lua_rawseti(L,refs,pcallRef);}
 lua_settop(L,refs);lua_pushvalue(L,refs);lua_rawsetp(L,LUA_REGISTRYINDEX,&capturedKey);
 lua_pushlightuserdata(L,&r);lua_pushcclosure(L,&AIFocusAccess::select,1);
 lua_setglobal(L,"ai_imagine_focus_callback");
 lua_pushlightuserdata(L,&r);lua_pushcclosure(L,&AIFocusAccess::enter,1);lua_setglobal(L,"ai_imagine_focus_enter");
}
bool hasAIFocusCallbacks(Room&r){
 auto *runtime=r.roomRuntime();if(!runtime||runtime->isClosing())return false;
 LuaRuntime::Binding binding(runtime->lua(),false);lua_State*L=runtime->lua().state();
 if(!L)return false;
 const int top=lua_gettop(L);const auto restore=qScopeGuard([&]{lua_settop(L,top);});
 lua_pushglobaltable(L);rawfield(L,-1,"ai_imagine_focus_callback");
 if(lua_tocfunction(L,-1)!=&AIFocusAccess::select)return false;
 if(!lua_getupvalue(L,-1,1)||lua_touserdata(L,-1)!=&r)return false;
 lua_settop(L,top);lua_pushglobaltable(L);rawfield(L,-1,"ai_imagine_focus_enter");
 if(lua_tocfunction(L,-1)!=&AIFocusAccess::enter)return false;
 if(!lua_getupvalue(L,-1,1)||lua_touserdata(L,-1)!=&r)return false;
 // Capability must attest the current dispatcher route, not merely a
 // retained old function that select/invoke would refuse to execute.
 lua_settop(L,top);lua_pushglobaltable(L);rawfield(L,-1,"pcall");
 if(!lua_iscfunction(L,-1))return false;
 lua_rawgetp(L,LUA_REGISTRYINDEX,&capturedKey);if(!plain(L,-1))return false;
 lua_rawgeti(L,-1,pcallRef);if(!lua_rawequal(L,-1,-3))return false;
 lua_settop(L,top);lua_pushglobaltable(L);rawfield(L,-1,"SmartAI");
 if(!lua_istable(L,-1))return false;
 rawfield(L,-1,"filterEvent");
 if(!lua_isfunction(L,-1)){
  lua_pop(L,1);rawfield(L,-1,"__instanceDict");
  if(!lua_istable(L,-1))return false;
  rawfield(L,-1,"filterEvent");
 }
 if(!lua_isfunction(L,-1))return false;
 lua_rawgetp(L,LUA_REGISTRYINDEX,&capturedKey);if(!plain(L,-1))return false;
 lua_rawgeti(L,-1,filterRef);if(!lua_rawequal(L,-1,-3))return false;
 return currentCallback(L,int(Damaged),int(Damaged))&&currentCallback(L,int(HpRecover),int(HpRecover));
}

quint64 aiFocusExecutionSerial(Room&r){
 auto *runtime=r.roomRuntime();if(!runtime||runtime->isClosing())return 0;
 LuaRuntime::Binding binding(runtime->lua(),false);lua_State*L=runtime->lua().state();
 if(!L)return 0;
 const int top=lua_gettop(L);const auto restore=qScopeGuard([&]{lua_settop(L,top);});
 lua_rawgetp(L,LUA_REGISTRYINDEX,&capturedKey);if(!plain(L,-1))return 0;
 lua_rawgeti(L,-1,executionRef);return quint64(lua_tointeger(L,-1));
}

// No authored function, semantic read hook, metamethod or Lua GC is called.
// The central observer provides preallocated Lua stack space before entry.
// A pre-entry checker must separately seal the full filterEvent graph.
bool aiFocusContinuityNoop(Room&r,lua_State*L,int argumentCount) {
 AINaturalFilterProof proof;
 if(!readAINaturalFilterEntry(r,L,argumentCount,&proof)
    ||proof.event==int(Damaged)||proof.event==int(HpRecover))return false;
 auto *f=AIFocusAccess::current(&r);
 if(!f||f->valid||f->event!=proof.event||!f->actor
    ||!matchesAINaturalFilterPayload(r,L,argumentCount,f->data,f->actor))return false;
 return aiFocusBindings(r,L);
}
bool aiFocusBindings(Room&r,lua_State*L) noexcept {
 if(!AILuaInspect::capacity(L,4))return false;
 const int top=lua_gettop(L);
 const auto restore=qScopeGuard([&]{lua_settop(L,top);});
 AILuaInspect::globals(L);
 if(!AILuaInspect::table(L,-1))return false;
 auto exact=[&](const char *name,lua_CFunction function){
  AILuaInspect::field(L,-1,name);
  const TValue *v=AILuaInspect::value(L,-1);
  bool ok=false;
  if(ttisCclosure(v)){
   const CClosure *c=clCvalue(v);
   ok=c->f==function&&c->nupvalues==1&&ttislightuserdata(&c->upvalue[0])
      &&pvalue(&c->upvalue[0])==&r;
  }
  lua_pop(L,1);return ok;
 };
 return exact("ai_imagine_focus_enter",&AIFocusAccess::enter)
     &&exact("ai_imagine_focus_callback",&AIFocusAccess::select);
}

// ---- imagine-helpers ----
#include "ai.h"
#include <QJsonObject>

namespace {
const QString damageMark = QStringLiteral("FocusDamageCount");
const QString recoverMark = QStringLiteral("FocusRecoverCount");
bool currentViewer(const AIWorldView &world, Room &room, ServerPlayer *viewer)
{
    return viewer && viewer->getRoom() == &room
        && world.self.objectName == viewer->objectName()
        && room.getAllPlayers(true).contains(viewer);
}
QJsonObject counters(const AIPlayerView &view)
{
    // Only existing observer-visible mark evidence, including owner-complete
    // absence. A known source writer does not authorize private restored values.
    QJsonObject row;
    for (const QString &name : {damageMark, recoverMark}) {
        if (view.publicMarks.contains(name)) row.insert(name, view.publicMarks.value(name));
        else if (view.marksComplete) row.insert(name, 0);
    }
    return row;
}
}
void projectAIImagineWorld(AIWorldView &world, Room &room, ServerPlayer *viewer)
{
    world.strategyFacts.remove(QStringLiteral("imagine_focus_event"));
    if (!currentViewer(world, room, viewer)) return;
    QJsonObject all;
    const auto add = [&](const AIPlayerView &view) {
        const auto row = counters(view);
        if (!row.isEmpty()) all.insert(view.objectName, row);
    };
    add(world.self);
    for (const AIPlayerView &view : world.players) add(view);
    world.strategyFacts.insert(QStringLiteral("imagine"),
                               QJsonObject{{QStringLiteral("focus_marks"), all},
        {"native_callback_bridge", hasAIFocusCallbacks(room)}});
}
void projectAIImagineEvent(AIWorldView &world, Room &room, ServerPlayer *viewer,
                                 const AIEventView &event)
{
    world.strategyFacts.remove(QStringLiteral("imagine_focus_event"));
    if (!currentViewer(world, room, viewer)
        || event.revision != room.roomRuntime()->stateRevision() || event.amount < 0)
        return;
    QString name;
    if (event.triggerEvent == Damaged && event.kind == QStringLiteral("damage")) name = damageMark;
    else if (event.triggerEvent == HpRecover && event.kind == QStringLiteral("recover")) name = recoverMark;
    else return;
    const QString actor = event.details.value(QStringLiteral("player")).toString();
    ServerPlayer *player = room.findPlayerByObjectName(actor, true);
    if (!player || (event.privateEvent && event.privateViewer != viewer->objectName())) return;
    const auto baseline = world.strategyFacts.value(QStringLiteral("imagine")).toObject()
        .value(QStringLiteral("focus_marks")).toObject().value(actor).toObject().value(name);
    if (!baseline.isDouble()) return;
    world.strategyFacts.insert(QStringLiteral("imagine_focus_event"), QJsonObject{
        {QStringLiteral("player"), actor}, {QStringLiteral("name"), name},
        {QStringLiteral("amount"), event.amount},
        {QStringLiteral("baseline"), baseline}});
}

// ---- imagine ----
#include "ai-strategy/runtime.h"
#include <QRegularExpression>
namespace {
bool imagineMatches(const QString &site,const QString &text)
{
    // Match the complete fixed-source format at each reached source location.
    // The Lua producer performs string.format first, preserving its errors.
    static const QHash<QString,QString> literals{
        {"56","vine+1"},{"60","gale+1"},{"69","guding+1"},
        {"76","luoyi+1"},{"82","drank+1"},{"87","jie+1"},
        {"624","in chained."},{"670","--------------------"},
        {"671","*******START********"},{"674","target:"},
        {"678","another:"},{"682","********END*********"},
        {"683","--------------------"}};
    const auto literal=literals.constFind(site);
    if(literal!=literals.cend())return text==literal.value();
    const QString integer=QStringLiteral("-?[0-9]+");
    QString pattern;
    if(site=="582")pattern="Now:immediateDamage="+integer;
    else if(site=="592")pattern="Now:damageValue="+integer+",fd="+integer+",fc="+integer+",ft="+integer;
    else if(site=="607")pattern="Now:damageValue="+integer+",ed="+integer+",ec="+integer+",et="+integer;
    else if(site=="672")pattern="damage from [\\s\\S]* to [\\s\\S]*:";
    else if(site=="673")pattern=" damageValue: "+integer;
    else if(site=="675"||site=="679")pattern=" damageCount: "+integer;
    else if(site=="676"||site=="680")pattern=" drawCards: "+integer;
    else if(site=="677"||site=="681")pattern=" turnDelt: "+integer;
    else return false;
    return QRegularExpression("\\A(?:"+pattern+")\\z").match(text).hasMatch();
}
}
bool emitAIImagineDiagnostic(Room &room,const AIRequest &request,ServerPlayer *viewer,
                                    const QString &site,const QString &message)
{
    if(!viewer||viewer->getRoom()!=&room||request.viewerObjectName!=viewer->objectName()
        ||!aiRangeRequestCurrent(room,request,viewer)
        ||!room.getAllPlayers(true).contains(viewer)||message.size()>65536
        ||message.contains(QChar(0))||!imagineMatches(site,message))return false;
    // Identical SWIG Room.writeToConsole sink and source order. No pcall here:
    // a sink failure must not permit a later source diagnostic or return.
    room.output(message);
    qWarning("%s",qPrintable(message));
    return true;
}

// ---- goddess-response-2 ----
#include "ai-strategy/owner.h"
#include "card.h"
#include "skill.h"
#include "engine.h"
#include "lua-wrapper.h"
#include <QObject>
#include <QVector>
#include <QJsonArray>
namespace {
struct Frame {QString skill;QPointer<ServerPlayer> actor;int event=NonTrigger;bool cost=false;QVariant data;int depth=-1;const void*callback=nullptr;};
class Ledger final:public QObject {public:explicit Ledger(Room&r):QObject(&r){}QVector<Frame>frames;};
Ledger*ledger(Room&r){for(auto*c:r.children())if(auto*l=dynamic_cast<Ledger*>(c))return l;return new Ledger(r);}
bool sameData(const QVariant&a,const QVariant&b){
 if(a.userType()!=b.userType())return false;
 if(a.userType()==qMetaTypeId<QStringList>())return a.toStringList()==b.toStringList();
 if(a.userType()==qMetaTypeId<CardUseStruct>()){const auto x=a.value<CardUseStruct>(),y=b.value<CardUseStruct>();return x.card==y.card&&x.from==y.from&&x.to==y.to;}
 if(a.userType()==qMetaTypeId<CardResponseStruct>()){const auto x=a.value<CardResponseStruct>(),y=b.value<CardResponseStruct>();return x.m_card==y.m_card&&x.m_who==y.m_who&&x.m_isUse==y.m_isUse;}
 return false;
}
bool knownId(const AIRequest&r,Room&room,ServerPlayer*p,int id){
 const auto scan=[id](const QList<AICardView>&cs){for(const auto&c:cs)if(c.effectiveId==id)return true;return false;};
 if(scan(r.worldView.handCards)||scan(r.worldView.discardPile)||scan(r.choiceOptions.cards))return true;
 QList<AIPlayerView>ps=r.worldView.players;ps.prepend(r.worldView.self);
 for(const auto&v:ps){if(scan(v.knownCards)||scan(v.equips)||scan(v.judgingArea))return true;for(const auto&pile:v.piles)if(pile.open&&scan(pile.cards))return true;}
 return readAIStrategyPublicDisclosure(room,p,id).value("known")==QJsonValue(true);
}
void tagRow(AIRequest&r,const QString&key,const QJsonObject&row){auto g=r.worldView.strategyFacts.value("goddess").toObject();auto tags=g.value("tag_cards").toObject();tags.insert(key,row);g.insert("tag_cards",tags);r.worldView.strategyFacts.insert("goddess",g);}
bool roster(Room&r,const ServerPlayer*p){return !p||r.findPlayerByObjectName(p->objectName(),true)==p;}
}
struct AIGoddessScope::Data {Ledger*l;int depth;};
AIGoddessScope::AIGoddessScope(Room&r,const LuaTriggerSkillV2*skill,ServerPlayer*p,int e,const SkillContext&c,bool cost):d(new Data{ledger(r),0}){
 d->depth=d->l->frames.size();Frame f;f.actor=p;f.event=e;f.cost=cost;if(c.original_data)f.data=*c.original_data;
 // Only actual registered fixed Lua callback identities; no general name/tag
 // permission. All other Lua V2 callbacks install a denying shadow frame.
 if(skill&&Sanguosha->getSkill(skill->objectName())==skill){
  const auto name=skill->objectName();const auto&callback=cost?skill->on_cost:skill->on_effect;
  int first=0,last=0;
  if(cost&&name=="GodManwu"){first=608;last=616;}
  if(!cost&&name=="#Kai_Skill"){first=1088;last=1201;}
  if(!cost&&name=="#Weikai_Skill"){first=1249;last=1425;}
  auto&runtime=r.roomRuntime()->lua();lua_State*L=runtime.state();const int top=L?lua_gettop(L):0;
  if(L&&first&&callback.state()==L&&callback.push(L)){
   lua_Debug info{};const void*identity=lua_topointer(L,-1);
   if(lua_getinfo(L,">S",&info)&&info.source&&QString::fromUtf8(info.source)=="@extensions/goddess.lua"
    &&info.linedefined==first&&info.lastlinedefined==last){f.skill=name;f.callback=identity;f.depth=runtime.protectedCallDepth()+1;}
  }
  if(L)lua_settop(L,top);
 }
 d->l->frames.append(f);
}
AIGoddessScope::~AIGoddessScope(){if(d->l->frames.size()==d->depth+1)d->l->frames.removeLast();else d->l->frames.clear();}
void projectAIGoddessContext(AIRequest&r,Room&room,ServerPlayer*p,const QVariant&data,QJsonObject&context,const std::function<QJsonObject(const Card*)>&face){
 // This derived default-value proof belongs to the current producer only.
 {auto g=context.value("goddess").toObject();g.remove("po_ask_empty_use");
  if(g.isEmpty())context.remove("goddess");else context.insert("goddess",g);}
 if(!p||p->getRoom()!=&room||!r.decisionId||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.stateRevision!=room.roomRuntime()->stateRevision()||!face)return;
 const auto*l=ledger(room);if(l->frames.isEmpty())return;const Frame f=l->frames.last();
 if(!f.actor||!roster(room,f.actor)||!f.callback||room.roomRuntime()->lua().protectedCallDepth()!=f.depth)return;
 // The innermost Lua frame must be the source writer's actual ask call site.
 // Nested protected callbacks cannot borrow an outer producer, including direct
 // execution helpers that do not go through RoomThread's dispatcher.
 lua_State*L=room.roomRuntime()->lua().state();if(!L)return;int line=0;const int top=lua_gettop(L);
 for(int level=0;;++level){lua_Debug ar{};if(!lua_getstack(L,level,&ar))break;
  if(!lua_getinfo(L,"Slf",&ar))break;const void*id=lua_topointer(L,-1);lua_pop(L,1);
  if(ar.what&&QString::fromLatin1(ar.what)=="C")continue;
  if(id==f.callback)line=ar.currentline;break;
 }
 lua_settop(L,top);if(!line)return;
 // Fixed goddess.lua609-614: own cost, exact public table->discard move and
 // already disclosed physical face. The tag itself grants no face authority.
 if(r.kind==AIRequest::SkillInvoke&&r.choiceOptions.reason=="GodManwu"&&f.skill=="GodManwu"&&f.cost
  &&!data.isValid()&&line==613&&f.actor==p&&f.event==BeforeCardsMove&&p->hasSkill("GodManwu")&&f.data.canConvert<CardsMoveOneTimeStruct>()){
  const auto m=f.data.value<CardsMoveOneTimeStruct>();
  if(m.card_ids.size()!=1||!m.from_places.contains(Player::PlaceTable)||m.to_place!=Player::DiscardPile
   ||m.reason.m_reason!=CardMoveReason::S_REASON_USE||!m.from||m.from==p)return;
  const int id=m.card_ids.first();if(room.getTag("GodManwu").toString()!=QString::number(id)||!knownId(r,room,p,id))return;
  const Card*c=room.getCard(id);if(!c||c->getEffectiveId()!=id)return;
  tagRow(r,"GodManwu",{{"known",true},{"present",true},{"card",face(c)}});return;
 }
 if(r.kind!=AIRequest::RespondCard||f.cost)return;
 // Kai's source string list is exactly the native CardAsked input, not a
 // free-form history index. Non-use/no-fourth-field keeps donor nil errors.
 if(r.prompt=="Kai-Jink"&&r.pattern=="Kai"&&r.handlingMethod==Card::MethodResponse
  &&line==1102&&f.skill=="#Kai_Skill"&&f.event==CardAsked&&f.actor==p&&sameData(data,f.data)){
  const auto fields=data.toStringList();if(fields.isEmpty()||fields.first()!="jink")return;
  // The actual CardAsked strings are source input, including short/non-use
  // lists whose later source nil-use access is an ordinary error.
  QJsonArray asked;for(const auto&field:fields)asked.append(field);
  auto askedContext=context.value("goddess").toObject();
  askedContext.insert("kai_asked",QJsonObject{{"known",true},{"fields",asked}});
  context.insert("goddess",askedContext);
  if(fields.size()<4||fields.at(2)!="use")return;
  const QString index=fields.at(3);const QVariant history=room.getTag("UseHistory"+index);
  QJsonObject row{{"known",true},{"index",index},{"present",false}};
  const auto use=history.value<CardUseStruct>();
  if(history.isValid()&&!history.canConvert<CardUseStruct>())return;
  if(use.card){
   if(use.card->toString()!=index||!use.to.contains(p)||!roster(room,use.from))return;
   auto card=face(use.card);if(card.isEmpty())return;
   // Native NosJiefan publicly announces this used rescue Slash (yjcm2012).
   auto flags=card.value("known_flags").toObject();flags.insert("NosJiefanUsed",use.card->hasFlag("NosJiefanUsed"));card.insert("known_flags",flags);
   row.insert("present",true);row.insert("use",QJsonObject{{"from",use.from?QJsonValue(use.from->objectName()):QJsonValue(QJsonValue::Null)},{"card",card}});
  }
  auto g=context.value("goddess").toObject();g.insert("use_history",row);context.insert("goddess",g);return;
 }
 // Fixed PoAskPo first performs QVariant::value<CardUseStruct>(). The Kai
 // QStringList and Weikai CardResponseStruct questions have no such converter:
 // native CardUseStruct() has null from/card. Its entire policy returns dot
 // independently of the ambient Weikai tag. Prove that default conversion,
 // not the private tag's value/presence, at the actual question call site.
 if(r.prompt=="PoAskPo"&&r.pattern=="Weikai"&&r.handlingMethod==Card::MethodUse&&sameData(data,f.data)){
  const bool kai=f.skill=="#Kai_Skill"&&f.event==CardAsked&&(line==1129||line==1146)
   &&data.userType()==qMetaTypeId<QStringList>();
  const bool response=f.skill=="#Weikai_Skill"&&f.event==CardResponded&&(line==1355||line==1377)
   &&data.userType()==qMetaTypeId<CardResponseStruct>();
  if((kai||response)&&!data.canConvert<CardUseStruct>()){
   const auto empty=data.value<CardUseStruct>();
   if(!empty.from&&!empty.card){auto g=context.value("goddess").toObject();
    g.insert("po_ask_empty_use",QJsonObject{{"known",true},{"source",kai?"Kai.CardAsked":"Weikai.CardResponded"}});
    context.insert("goddess",g);return;
   }
  }
 }
 // PoAskPo only tests pointer truthiness: never publish unused card face/IDs.
 // Only the two adjacent source-local setTag(temp)->ask calls attest this
 // value, including known nil temp. Ambient first-loop/Kai reads do not acquire
 // authority from a skill name or absence of a differently scoped writer.
 if(r.prompt=="PoAskPo"&&r.pattern=="Weikai"&&r.handlingMethod==Card::MethodUse&&sameData(data,f.data)
  &&f.skill=="#Weikai_Skill"&&((f.event==CardUsed&&line==1298)||(f.event==CardResponded&&line==1377))){
  if((f.event==CardUsed&&!f.data.canConvert<CardUseStruct>())
    ||(f.event==CardResponded&&!f.data.canConvert<CardResponseStruct>()))return;
  const Card*c=room.getTag("Weikai").value<const Card*>();
  tagRow(r,"Weikai",{{"known",true},{"present",c!=nullptr}});
 }
}

// ---- goddess-response ----
#include <cmath>
#include <memory>
namespace {
QJsonObject failure(const char*why){return{{"kind","god_youhua_source"},{"known",false},{"error",why}};}
bool integer(const QJsonValue&v,int&n){double d=v.toDouble(-1);if(!v.isDouble()||!std::isfinite(d)||d<0||d>std::numeric_limits<int>::max()||std::floor(d)!=d)return false;n=int(d);return true;}
QJsonObject goddessResponseFace(const AICardView&c){return{{"name",c.objectName},{"class_name",c.className},{"suit",c.suit},{"number",c.number},{"effective_id",c.effectiveId}};}
QJsonObject disclosed(const AIRequest&r,Room&room,ServerPlayer*p,int id){
 const auto scan=[id](const QList<AICardView>&cards){for(const auto&c:cards)if(c.effectiveId==id)return goddessResponseFace(c);return QJsonObject();};
 for(const auto*cards:{&r.worldView.handCards,&r.worldView.discardPile,&r.choiceOptions.cards}){auto f=scan(*cards);if(!f.isEmpty())return f;}
 const auto player=[&](const AIPlayerView&v){for(const auto*cards:{&v.knownCards,&v.equips,&v.judgingArea}){auto f=scan(*cards);if(!f.isEmpty())return f;}for(const auto&pile:v.piles)if(pile.open){auto f=scan(pile.cards);if(!f.isEmpty())return f;}return QJsonObject();};
 auto f=player(r.worldView.self);if(!f.isEmpty())return f;
 for(const auto&v:r.worldView.players){f=player(v);if(!f.isEmpty())return f;}
 const auto witness=readAIStrategyPublicDisclosure(room,p,id);
 return witness.value("known")==QJsonValue(true)?witness.value("card").toObject():QJsonObject();
}
bool scope(const AIRequest&r,Room&room,ServerPlayer*p){
 return p&&p->getRoom()==&room&&room.findPlayerByObjectName(p->objectName(),true)==p
  &&r.viewerObjectName==p->objectName()&&r.worldView.self.objectName==p->objectName()
  &&r.decisionId&&r.stateRevision==room.roomRuntime()->stateRevision()
  &&r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY
  &&p->hasSkill("GodYouhua")&&!p->hasFlag("YouhuaUsed")&&p->getMark("LastId")>0;
}
QJsonObject printed(const Card*c){return{{"name",c->objectName()},{"class_name",c->getClassName()},
 {"kind_of",QJsonArray::fromStringList(c->getKindOfNames())},{"suit",int(c->getSuit())},{"number",c->getNumber()},
 {"skill_name",c->getSkillName(false)},{"virtual_card",c->isVirtualCard()},
 {"legacy_string_key",c->toString()}};}
}
QJsonObject queryAIGodYouhua(const AIRequest&r,Room&room,ServerPlayer*p,const QJsonObject&proposal){
 if(!scope(r,room,p)||proposal.size()!=4||proposal.value("kind")!="evaluate_skill_card"
  ||proposal.value("skill")!="GodYouhua"||!proposal.value("subcards").isArray())return failure("GodYouhua actual owner Play source scope mismatch");
 const auto op=proposal.value("name").toString();const auto costs=proposal.value("subcards").toArray();
 if((op!="source_reference"&&op!="source_parse")||(op=="source_reference"&&!costs.isEmpty())||(op=="source_parse"&&costs.size()!=1))return failure("GodYouhua source query shape mismatch");
 const int reference=p->getMark("LastId");
 // A mark is not disclosure. Prove the exact current face before native getCard.
 const auto f=disclosed(r,room,p,reference);
 if(f.isEmpty())return failure("GodYouhua LastId reference is not currently disclosed");
 const Card*ref=Sanguosha->getCard(reference);
 if(!ref||ref->getEffectiveId()!=reference||f.value("name")!=ref->objectName()
  ||f.value("class_name")!=ref->getClassName()||f.value("suit")!=int(ref->getSuit())||f.value("number")!=ref->getNumber())return failure("GodYouhua disclosed reference face changed");
 EngineRuntimeContextScope engine(*Sanguosha,&room);LuaRuntime::Binding game(room.roomRuntime()->lua(),false);
 std::unique_ptr<Card> clone(Sanguosha->cloneCard(ref->objectName(),Card::SuitToBeDecided,-1));
 QJsonObject out{{"kind","god_youhua_source"},{"known",true},{"reference_id",reference},
  {"viewer",r.viewerObjectName},{"decision_id",r.getDecisionId()},{"state_revision",r.getStateRevision()},
  {"reference",f},{"clone_exists",bool(clone)}};
 if(clone)out.insert("clone",printed(clone.get()));
 if(op=="source_reference"){
  if(!scope(r,room,p)||p->getMark("LastId")!=reference)return failure("GodYouhua source observation changed its scope");
  return out;
 }
 if(!clone)return failure("GodYouhua parse follows absent source clone");
 if(clone->isKindOf("IronChain"))return failure("GodYouhua source declines IronChain before parsing");
 int id=-1;if(!integer(costs.first(),id))return failure("GodYouhua source material must be one physical ID");
 bool ownerKnown=false;for(const auto&c:r.worldView.handCards)if(c.effectiveId==id)ownerKnown=true;
 for(const auto&c:r.worldView.self.equips)if(c.effectiveId==id)ownerKnown=true;
 if(!ownerKnown||room.getCardOwner(id)!=p||(room.getCardPlace(id)!=Player::PlaceHand&&room.getCardPlace(id)!=Player::PlaceEquip))return failure("GodYouhua material is outside current disclosed owner HE");
 const Card*material=Sanguosha->getCard(id);if(!material||material->getEffectiveId()!=id)return failure("GodYouhua material is unavailable");
 const auto mf=disclosed(r,room,p,id);
 if(mf.value("name")!=material->objectName()||mf.value("class_name")!=material->getClassName()
  ||mf.value("suit")!=int(material->getSuit())||mf.value("number")!=material->getNumber())return failure("GodYouhua material face changed");
 // Exact fixed donor concatenation, including its nested first =. segment.
 clone->setSkillName("GodYouhua");
 const QString wire=clone->toString()+QString(":GodYouhua[%1:%2]=%3").arg(material->getSuitString()).arg(material->getNumberString()).arg(id);
 const Card*rawParsed=Card::Parse(wire);
 // Card::Parse can generally borrow physical cards. Do not own/delete one,
 // even on an unexpected parser branch; only the proven virtual result is ours.
 if(rawParsed&&(rawParsed->getId()>=0||!rawParsed->isVirtualCard()))return failure("GodYouhua parser unexpectedly returned a borrowed physical card");
 std::unique_ptr<const Card> parsed(rawParsed);
 out.insert("source_wire",wire);out.insert("exists",bool(parsed));out.insert("material_id",id);
 if(parsed){
  if(!parsed->isVirtualCard()||parsed->getSubcards()!=QList<int>{id}||parsed->getSkillName(false)!="GodYouhua")return failure("GodYouhua fixed parser returned an unexpected source product");
  auto card=printed(parsed.get());card.insert("subcards",QJsonArray{id});out.insert("card",card);
 }
 if(!scope(r,room,p)||p->getMark("LastId")!=reference)return failure("GodYouhua parsing changed source scope");
 return out;
}
bool aiGodYouhuaConstruction(const AIRequest&r,Room&room,ServerPlayer*p,const QJsonObject&spec){
 if(spec.size()!=6||spec.value("source_key")!="GodYouhua"||spec.value("skill")!="GodYouhua"||!spec.value("subcards").isArray())return false;
 const auto row=queryAIGodYouhua(r,room,p,{{"kind","evaluate_skill_card"},{"skill","GodYouhua"},{"name","source_parse"},{"subcards",spec.value("subcards")}});
 const auto card=row.value("card").toObject();
 return row.value("known")==QJsonValue(true)&&row.value("exists")==QJsonValue(true)
  &&card.value("name")==spec.value("name")&&card.value("skill_name")==spec.value("skill")
  &&card.value("suit")==spec.value("suit")&&card.value("number")==spec.value("number")
  &&card.value("subcards")==spec.value("subcards");
}

// ---- yuri ----
#include <QSet>
namespace {
thread_local AIYuriScope *yuriCurrent=nullptr;
thread_local quint64 nextSerial=0; // Never reset/reused when a Room/runtime is recreated.
const QSet<QString> keys{"SlashMissed","owner_flexible","flexible","skeleton","bougairensha","kirisou","tomowodaji","japenknifejile","loyalm","bffeedingpoisoning","yuriganglie","suckblood"};
bool yuriRoster(Room&r,const ServerPlayer*p){return p&&r.getAllPlayers(true).contains(const_cast<ServerPlayer*>(p));}
QJsonValue name(Room&r,const ServerPlayer*p){return yuriRoster(r,p)?QJsonValue(p->objectName()):QJsonValue(false);}
void clear(QJsonObject&facts){auto out=facts.value("yuri").toObject();for(const auto&k:keys)out.remove(k);if(out.isEmpty())facts.remove("yuri");else facts.insert("yuri",out);}
void add(QJsonObject&facts,const QString&key,const QJsonObject&row){if(row.isEmpty())return;auto out=facts.value("yuri").toObject();out.insert(key,row);facts.insert("yuri",out);}
QJsonObject emptyUse(){return{{"r50_type","CardUse"},{"from",false},{"to",QJsonArray()},{"card",false}};}
QJsonObject emptyDamage(){DamageStruct d;return{{"r50_type","Damage"},{"from",false},{"to",false},{"card",false},{"amount",d.damage},{"nature",int(d.nature)},{"chain",d.chain},{"transfer",d.transfer},{"by_user",d.by_user}};}
QJsonObject emptySlash(){SlashEffectStruct e;return{{"r50_type","SlashEffect"},{"from",false},{"to",false},{"slash",false},{"jink_num",e.jink_num},{"drank",e.drank},{"nature",int(e.nature)}};}
}
struct AIYuriScope::State {
 Room*room=nullptr;const TriggerSkillV2*definition=nullptr;SkillContext*ctx=nullptr;
 QVariant*data=nullptr;QPointer<ServerPlayer>owner,invoker;QPointer<Card>card;
 QString skill;int event=-1;quint64 serial=0;qint64 history=0;
 const void *callback=nullptr;int depth=-1;lua_State*luaState=nullptr;quint64 generation=0;
};
bool registerAIYuriSource(Room&r,lua_State*L){return registerAIPinnedSource(r,L,QStringLiteral("extensions/yuri.lua"),QByteArrayLiteral("406788870d21618cba643e2994bdb08e72329df80196e0fc3773d383d9756fd6"));}
AIYuriScope::AIYuriScope(Room&r,const TriggerSkillV2*d,SkillContext*c,int event):previous_(yuriCurrent){
 yuriCurrent=this;state_=new State;state_->serial=++nextSerial;
 static const QSet<QString>skills{"flexible","skeleton","bougairensha","kirisou","#kirisou_umaretuski","tomowodaji","japenknifejile","loyalm","bffeedingpoisoning","yuriganglie","suckblood"};
 if(!d||!c||!skills.contains(d->objectName())||Sanguosha->getSkill(d->objectName())!=d
  ||!yuriRoster(r,c->owner)||!yuriRoster(r,c->invoker)||!c->original_data
  ||c->activationRef.key.skillName!=d->objectName()
  ||c->activationRef.ownerObjectName!=c->owner->objectName())return;
 const auto*luaDefinition=dynamic_cast<const LuaTriggerSkillV2*>(d);if(!luaDefinition||!r.roomRuntime())return;
 int first=0,last=0;const auto n=d->objectName();
 if(n=="flexible"){first=6363;last=6397;}
 else if(n=="skeleton"){first=8049;last=8092;}
 else if(n=="bougairensha"){first=13830;last=13875;}
 else if(n=="kirisou"){first=15536;last=15576;}
 else if(n=="#kirisou_umaretuski"){first=15591;last=15612;}
 else if(n=="tomowodaji"){first=7691;last=7732;}
 else if(n=="japenknifejile"){first=7796;last=7928;}
 else if(n=="loyalm"){first=8723;last=8747;}
 else if(n=="bffeedingpoisoning"){first=5699;last=5744;}
 else if(n=="yuriganglie"){first=10582;last=10610;}
 else if(n=="suckblood"){first=12427;last=12465;}
 auto&vm=r.roomRuntime()->lua();auto*L=vm.state();if(!L)return;const int top=lua_gettop(L);
 const bool pinned=first&&luaDefinition->on_effect.state()==L&&luaDefinition->on_effect.push(L)
  &&matchAIPinnedFunction(r,L,-1,QStringLiteral("extensions/yuri.lua"),first,last);
 const void*callback=pinned?lua_topointer(L,-1):nullptr;lua_settop(L,top);if(!callback)return;
 state_->callback=callback;state_->depth=vm.protectedCallDepth()+1;state_->luaState=L;state_->generation=vm.generation();
 auto&s=*state_;s.room=&r;s.definition=d;s.ctx=c;s.owner=c->owner;s.invoker=c->invoker;
 s.data=c->original_data;s.skill=d->objectName();s.event=event;
 if(s.data->canConvert<CardUseStruct>()){const auto u=s.data->value<CardUseStruct>();s.card=const_cast<Card*>(u.card);s.history=u.targetModReveal.useHistoryEventId;}
}
AIYuriScope::~AIYuriScope(){yuriCurrent=previous_;delete state_;}
namespace {
const AIYuriScope::State* live(Room&r){
 const auto*s=yuriCurrent?yuriCurrent->state():nullptr;
 if(!s||s->room!=&r||!r.roomRuntime()||r.roomRuntime()->isClosing()||!s->ctx||!s->owner||!s->invoker||!yuriRoster(r,s->owner)||!yuriRoster(r,s->invoker)||s->ctx->original_data!=s->data
  ||s->ctx->owner!=s->owner||s->ctx->invoker!=s->invoker
  ||s->ctx->activationRef.key.skillName!=s->skill
  ||s->ctx->activationRef.ownerObjectName!=s->owner->objectName()
  ||Sanguosha->getSkill(s->skill)!=s->definition)return nullptr;
 auto&vm=r.roomRuntime()->lua();auto*L=vm.state();if(!L||L!=s->luaState||vm.generation()!=s->generation||!s->callback||vm.protectedCallDepth()!=s->depth)return nullptr;
 // First non-C frame must be the pinned actual source body, not a nested sibling helper.
 const int top=lua_gettop(L);int line=0;
 for(int level=0;;++level){lua_Debug ar{};if(!lua_getstack(L,level,&ar)||!lua_getinfo(L,"Slf",&ar))break;const void*fn=lua_topointer(L,-1);lua_pop(L,1);if(ar.what&&QString::fromLatin1(ar.what)=="C")continue;if(fn==s->callback)line=ar.currentline;break;}
 lua_settop(L,top);
 bool sourceAsk=false;
 if(s->skill=="flexible")sourceAsk=(line==6372||line==6374);
 else if(s->skill=="skeleton")sourceAsk=(line==8057);
 else if(s->skill=="bougairensha")sourceAsk=(line==13843);
 else if(s->skill=="kirisou")sourceAsk=(line==15547);
 else if(s->skill=="#kirisou_umaretuski")sourceAsk=(line==15603);
 else if(s->skill=="tomowodaji")sourceAsk=(line==7705);
 else if(s->skill=="japenknifejile")sourceAsk=(line==7887);
 else if(s->skill=="loyalm")sourceAsk=(line==8730);
 else if(s->skill=="bffeedingpoisoning")sourceAsk=(line==5705);
 else if(s->skill=="yuriganglie")sourceAsk=(line==10591);
 else if(s->skill=="suckblood")sourceAsk=(line==12444);
 return sourceAsk?s:nullptr;
}
QJsonObject useSlot(Room&r,const AIYuriScope::State&s,ServerPlayer*viewer,const QString&key,bool owner,
 const std::function<QJsonObject(const Card*)>&serialize){
 const QVariant tag=owner?viewer->getTag(key):r.getTag(key);
 // Exact missing/wrong QVariant conversion remains the source default value.
 // Nonempty convertible payload needs current typed frame proof; never infer it from a tag name.
 if(!tag.canConvert<CardUseStruct>())return emptyUse();
 if(!s.data->canConvert<CardUseStruct>())return{};
 const auto t=tag.value<CardUseStruct>(),u=s.data->value<CardUseStruct>();
 if(!u.card||u.card!=s.card||u.targetModReveal.useHistoryEventId!=s.history||!yuriRoster(r,u.from)
  ||t.card!=u.card||t.from!=u.from||t.to!=u.to||t.targetModReveal.useHistoryEventId!=u.targetModReveal.useHistoryEventId)return{};
 bool exposed=(s.event==TargetConfirmed||s.event==TargetConfirming||s.event==TargetSpecified);
 if(s.event==PreCardUsed)exposed=u.from==viewer&&s.owner==viewer;
 if(!exposed)return{};
 QJsonArray tos;for(auto*p:u.to){if(!yuriRoster(r,p))return{};tos.append(p->objectName());}
 auto face=serialize(u.card);if(face.isEmpty())return{};
 return{{"r50_type","CardUse"},{"from",u.from->objectName()},{"to",tos},{"card",face}};
}
QJsonObject damageSlot(Room&r,const AIYuriScope::State&s,ServerPlayer*viewer,const QString&key){
 const QVariant tag=r.getTag(key);if(!tag.canConvert<DamageStruct>())return emptyDamage();
 // Only this fixed named writer installs a Room DamageStruct. Other names
 // must not substitute their invoke argument for an unwritten Room slot.
 if(key!="bffeedingpoisoning"||s.event!=DamageCaused||s.owner!=viewer||!s.data->canConvert<DamageStruct>())return{};
 const auto d=s.data->value<DamageStruct>(),t=tag.value<DamageStruct>();
 if(d.from!=viewer||!yuriRoster(r,d.to)||t.from!=d.from||t.to!=d.to||t.card!=d.card||t.damage!=d.damage||t.nature!=d.nature||t.chain!=d.chain||t.transfer!=d.transfer)return{};
 // The actual intention body reads only damage.to; no pre-damage hidden card,
 // chain/filter metadata or amount is exported merely because this actor owns the skill.
 return{{"r50_type","Damage"},{"to",d.to->objectName()}};
}
bool eventDomain(const AIYuriScope::State&s,const QString&k){
 if(s.skill!=k)return false;
 if(k=="tomowodaji")return s.event==DamageInflicted;
 if(k=="japenknifejile"||k=="bffeedingpoisoning")return s.event==DamageCaused;
 if(k=="loyalm"||k=="yuriganglie")return s.event==Damaged;
 if(k=="suckblood")return s.event==Damage;
 if(k=="skeleton")return s.event==TargetConfirmed;
 return k=="bougairensha"&&s.event==TargetConfirming;
}
}
void projectAIYuriQuestion(AIRequest&q,Room&r,ServerPlayer*p,const std::function<QJsonObject(const Card*)>&serialize){
 clear(q.worldView.strategyFacts);
 // Bind the native-only tombstone before checking scope, including an absent frame.
 const auto*s=live(r);const quint64 serial=s?s->serial:0;
 if(!q.yuriFrameBound){q.yuriFrameBound=true;q.yuriFrameSerial=serial;}
 if(!s||!q.decisionId||q.yuriFrameSerial!=serial||!yuriRoster(r,p)||q.viewerObjectName!=p->objectName()
  ||q.worldView.self.objectName!=p->objectName()||q.stateRevision!=r.roomRuntime()->stateRevision())return;
 const auto k=q.choiceOptions.reason;
 if(q.kind==AIRequest::UseCard&&q.pattern=="@@flexible"&&s->skill=="flexible"&&s->event==PreCardUsed&&s->owner==p){
  add(q.worldView.strategyFacts,"flexible",useSlot(r,*s,p,"flexible",false,serialize));
  add(q.worldView.strategyFacts,"owner_flexible",useSlot(r,*s,p,"flexible",true,serialize));
 }else if(q.kind==AIRequest::Choice&&q.choiceOptions.question=="askForChoice"&&k=="kirisou"&&s->skill==k&&s->event==TargetSpecified&&s->owner==p){
  add(q.worldView.strategyFacts,k,useSlot(r,*s,p,k,false,serialize));
 }else if(q.kind==AIRequest::Discard&&q.choiceOptions.question=="askForDiscard"&&k=="kirisou_umaretuski"&&s->skill=="#kirisou_umaretuski"&&s->event==TargetSpecifying){
  // This current native question has no SlashMissed tag writer. Preserve the
  // default source SlashEffect; a retained nonempty tag has no lawful frame.
  if(!p->getTag("SlashMissed").canConvert<SlashEffectStruct>())add(q.worldView.strategyFacts,"SlashMissed",emptySlash());
 }
}
void projectAIYuriEvent(AIWorldView&w,Room&r,ServerPlayer*viewer,ServerPlayer*actor,const AIEventView&e,const QVariant&data,const std::function<QJsonObject(const Card*)>&serialize){
 clear(w.strategyFacts);
 if(e.triggerEvent!=ChoiceMade||viewer!=actor||!yuriRoster(r,viewer)||w.self.objectName!=viewer->objectName()||!r.roomRuntime()||e.revision!=r.roomRuntime()->stateRevision()||data.userType()!=QMetaType::QString)return;
 const auto*s=live(r);if(!s)return;const auto parts=data.toString().split(':');
 if(parts.size()<3)return;const auto kind=parts[0],key=parts[1];
 if(kind=="skillInvoke"&&parts.size()==3&&(parts[2]=="yes"||parts[2]=="no")&&eventDomain(*s,key)){
  // Tomowodaji may ask each actual holder; the other source actors are ctx.owner.
  if(key=="tomowodaji"){if(!viewer->hasSkill(key))return;}else if(s->owner!=viewer)return;
  add(w.strategyFacts,key,key=="skeleton"?useSlot(r,*s,viewer,key,false,serialize):damageSlot(r,*s,viewer,key));
 }else if(kind=="cardResponded"&&parts.size()>=4&&parts[2]=="@bougairensha"&&eventDomain(*s,"bougairensha")&&viewer->hasSkill("bougairensha")){
  add(w.strategyFacts,"bougairensha",useSlot(r,*s,viewer,"bougairensha",false,serialize));
 }
}

// ---- htms-events ----
namespace {
const char*path="extensions/htms.lua";
struct HtmsEventsFrame {QString name;QPointer<ServerPlayer>actor;int event=NonTrigger,depth=-1;const QVariant*data=nullptr;const void*callback=nullptr;};
struct DamageFrame {QVector<QVariant>publicValues;QVariant previous;};
struct Pending {const QVariant*value=nullptr;bool allowed=false;};
class HtmsEventsLedger final:public QObject {public:explicit HtmsEventsLedger(Room&r):QObject(&r){}QVector<HtmsEventsFrame>frames;QVector<DamageFrame>damages;QVector<Pending>pending;QVariant currentDamage;};
HtmsEventsLedger*htmsEventsFind(Room&r){for(auto*c:r.children())if(auto*l=dynamic_cast<HtmsEventsLedger*>(c))return l;return nullptr;}
HtmsEventsLedger*htmsEventsLedger(Room&r){auto*l=htmsEventsFind(r);return l?l:new HtmsEventsLedger(r);}
bool htmsEventsRoster(Room&r,const ServerPlayer*p){return !p||r.getAllPlayers(true).contains(const_cast<ServerPlayer*>(p));}
bool sameDamage(const QVariant&a,const QVariant&b){
 if(a.userType()!=qMetaTypeId<DamageStruct>()||b.userType()!=qMetaTypeId<DamageStruct>())return false;
 const auto x=a.value<DamageStruct>(),y=b.value<DamageStruct>();
 return x.from==y.from&&x.to==y.to&&x.card==y.card&&x.damage==y.damage&&x.nature==y.nature&&x.chain==y.chain&&x.transfer==y.transfer&&x.by_user==y.by_user&&x.reason==y.reason&&x.transfer_reason==y.transfer_reason&&x.prevented==y.prevented&&x.tips==y.tips&&x.ignore_hujia==y.ignore_hujia;
}
bool sameUse(const QVariant&a,const QVariant&b){
 if(a.userType()!=qMetaTypeId<CardUseStruct>()||b.userType()!=qMetaTypeId<CardUseStruct>())return false;
 const auto x=a.value<CardUseStruct>(),y=b.value<CardUseStruct>();return x.from==y.from&&x.to==y.to&&x.card==y.card;
}
void htmsEventsClear(QJsonObject&facts){auto h=facts.value("htms").toObject();for(const char*k:{"CurrentDamageStruct","CurrentUseStruct","huanggui","tcmfs","htms_lunbian"})h.remove(k);if(h.isEmpty())facts.remove("htms");else facts.insert("htms",h);}
void htmsEventsPublish(QJsonObject&facts,const QString&key,const QJsonObject&row){auto h=facts.value("htms").toObject();h.insert(key,row);facts.insert("htms",h);}
bool frame(Room&r,HtmsEventsFrame&out,int&line){
 auto*l=htmsEventsFind(r);if(!l||l->frames.isEmpty()||!r.roomRuntime()||r.roomRuntime()->isClosing())return false;
 out=l->frames.last();if(!out.actor||!htmsEventsRoster(r,out.actor)||!out.callback||!out.data||r.roomRuntime()->lua().protectedCallDepth()!=out.depth)return false;
 lua_State*L=r.roomRuntime()->lua().state();if(!L)return false;const int top=lua_gettop(L);line=0;
 for(int level=0;;++level){lua_Debug ar{};if(!lua_getstack(L,level,&ar)||!lua_getinfo(L,"Slf",&ar))break;const void*id=lua_topointer(L,-1);lua_pop(L,1);if(ar.what&&QString::fromLatin1(ar.what)=="C")continue;if(id==out.callback)line=ar.currentline;break;}
 lua_settop(L,top);return line!=0;
}
QJsonValue player(const ServerPlayer*p){return p?QJsonValue(p->objectName()):QJsonValue(false);}
QJsonObject cardFace(Room&r,ServerPlayer*p,const Card*c,const std::function<QJsonObject(const Card*)>&face){
 auto out=face(c);if(!out.isEmpty())out.insert("legacy_identity_token",aiLegacyCardIdentity(r,p,c));return out;
}
QJsonObject useRow(Room&r,ServerPlayer*p,const QVariant&v,const std::function<QJsonObject(const Card*)>&face){
 const auto u=v.value<CardUseStruct>();if(!htmsEventsRoster(r,u.from))return{};QJsonArray to;for(auto*t:u.to){if(!t||!htmsEventsRoster(r,t))return{};to.append(t->objectName());}
 QJsonValue c(false);if(u.card){const auto f=cardFace(r,p,u.card,face);if(f.isEmpty())return{};c=f;}
 return{{"current_event",true},{"from",player(u.from)},{"to",to},{"card",c}};
}
QJsonObject damageRow(Room&r,ServerPlayer*p,const QVariant&v,const std::function<QJsonObject(const Card*)>&face){
 const auto x=v.value<DamageStruct>();if(!htmsEventsRoster(r,x.from)||!htmsEventsRoster(r,x.to))return{};
 QJsonValue c(false);if(x.card){const auto f=cardFace(r,p,x.card,face);if(f.isEmpty())return{};c=f;}
 return{{"current_event",true},{"from",player(x.from)},{"to",player(x.to)},{"card",c},{"amount",x.damage},{"nature",int(x.nature)},{"chain",x.chain},{"transfer",x.transfer}};
}
}
void clearAIHTMSWitnesses(Room&r){if(auto*l=htmsEventsFind(r)){l->frames.clear();l->pending.clear();l->damages.clear();l->currentDamage.clear();}}
bool registerAIHTMSSource(Room&r,lua_State*L){clearAIHTMSWitnesses(r);return registerAIPinnedSource(r,L,QString::fromLatin1(path),QByteArrayLiteral("90fb50d2d63de365ef5af92839558baa8093ec61a24f6ed5a9d505a33c5ac0fb"));}
struct AIHTMSFrame::Data {HtmsEventsLedger*l;int depth;};
AIHTMSFrame::AIHTMSFrame(Room&r,const LuaTriggerSkillV2*s,ServerPlayer*p,int e,const SkillContext&ctx,bool cost):d(new Data{htmsEventsLedger(r),0}){
 d->depth=d->l->frames.size();HtmsEventsFrame f;f.actor=ctx.invoker?ctx.invoker:p;f.event=e;f.data=ctx.original_data;
 if(!cost&&s&&Sanguosha->getSkill(s->objectName())==s){
  int first=0,last=0;const auto n=s->objectName();
  if(n=="shilianEX"){first=2978;last=3023;}else if(n=="chuszy"){first=4215;last=4246;}
  else if(n=="meihuomoyan"){first=6712;last=6756;}else if(n=="anni"){first=14677;last=14746;}
  else if(n=="huanggui"){first=15344;last=15378;}else if(n=="tcmfs"){first=25302;last=25323;}else if(n=="htms_lunbian"){first=31307;last=31346;}
  auto&runtime=r.roomRuntime()->lua();lua_State*L=runtime.state();const int top=L?lua_gettop(L):0;
  if(L&&first&&s->on_effect.state()==L&&s->on_effect.push(L)&&matchAIPinnedFunction(r,L,-1,QString::fromLatin1(path),first,last)){f.name=n;f.callback=lua_topointer(L,-1);f.depth=runtime.protectedCallDepth()+1;}
  if(L)lua_settop(L,top);
 }
 d->l->frames.append(f);
}
AIHTMSFrame::~AIHTMSFrame(){if(d->l->frames.size()==d->depth+1)d->l->frames.removeLast();else d->l->frames.clear();}
void projectAIHTMSEvents(AIRequest&q,Room&r,ServerPlayer*p,const std::function<QJsonObject(const Card*)>&face){
 htmsEventsClear(q.worldView.strategyFacts);
 if(!p||!htmsEventsRoster(r,p)||p->getRoom()!=&r||!r.roomRuntime()||r.roomRuntime()->isClosing()||!q.decisionId||q.viewerObjectName!=p->objectName()||q.worldView.self.objectName!=p->objectName()||q.stateRevision!=r.roomRuntime()->stateRevision()||!face)return;
 HtmsEventsFrame f;int line=0;if(!frame(r,f,line))return;
 const auto reason=q.choiceOptions.reason;QString key;bool damage=false;
 if(f.name=="chuszy"&&f.event==Damaged&&line==4235&&q.kind==AIRequest::Discard&&reason=="chuszy"){
  auto*l=htmsEventsFind(r);if(!l||l->damages.isEmpty()||!l->currentDamage.isValid())return;const auto row=damageRow(r,p,l->currentDamage,face);if(!row.isEmpty())htmsEventsPublish(q.worldView.strategyFacts,"CurrentDamageStruct",row);return;
 }
 if(f.name=="meihuomoyan"&&f.event==CardUsed&&(line==6735||line==6736)&&q.kind==AIRequest::PlayerChosen&&reason=="meihuomoyan")key="CurrentUseStruct";
 else if(f.name=="anni"&&f.event==TargetConfirmed&&reason=="anni"&&((q.kind==AIRequest::PlayerChosen&&(line==14695||line==14696)&&f.actor==p)||(q.kind==AIRequest::Choice&&line==14698)))key="CurrentUseStruct";
 else if(f.name=="huanggui"&&f.event==TargetConfirming&&line==15365&&q.kind==AIRequest::PlayerChosen&&reason=="huanggui"&&f.actor==p)key="huanggui";
 else if(f.name=="tcmfs"&&f.event==DamageCaused&&line==25315&&q.kind==AIRequest::Discard&&reason=="tcmfs"&&f.actor==p){key="tcmfs";damage=true;}
 else if(f.name=="htms_lunbian"&&f.event==TargetConfirmed&&line==31318&&q.kind==AIRequest::Discard&&reason=="htms_lunbian")key="htms_lunbian";
 if(key.isEmpty())return;const auto tag=r.getTag(key);if(damage?!sameDamage(tag,*f.data):!sameUse(tag,*f.data))return;
 const auto row=damage?damageRow(r,p,tag,face):useRow(r,p,tag,face);if(!row.isEmpty())htmsEventsPublish(q.worldView.strategyFacts,key,row);
}
void projectAIHTMSIntention(AIWorldView&w,Room&r,ServerPlayer*p,const AIEventView&e,const std::function<QJsonObject(const Card*)>&face){
 htmsEventsClear(w.strategyFacts);if(!p||!htmsEventsRoster(r,p)||!r.roomRuntime()||r.roomRuntime()->isClosing()||w.self.objectName!=p->objectName()||e.revision!=r.roomRuntime()->stateRevision()||e.triggerEvent!=ChoiceMade||e.from!=p->objectName()||!face)return;
 const auto a=e.details.value("prompt_list").toArray();if(a.size()!=3||a.at(0)!="skillInvoke"||a.at(1)!="shilianEX"||(a.at(2)!="yes"&&a.at(2)!="no"))return;
 HtmsEventsFrame f;int line=0;if(!frame(r,f,line)||f.name!="shilianEX"||f.event!=Damaged||f.actor!=p||line!=2988)return;
 auto*l=htmsEventsFind(r);if(!l||l->damages.isEmpty()||!l->currentDamage.isValid())return;const auto row=damageRow(r,p,l->currentDamage,face);if(!row.isEmpty())htmsEventsPublish(w.strategyFacts,"CurrentDamageStruct",row);
}
struct AIHTMSDamageScope::Data {HtmsEventsLedger*l;int depth;};
AIHTMSDamageScope::AIHTMSDamageScope(Room&r):d(new Data{htmsEventsLedger(r),0}){d->depth=d->l->damages.size();DamageFrame f;f.previous=d->l->currentDamage;if(f.previous.isValid())f.publicValues.append(f.previous);d->l->damages.append(f);}
AIHTMSDamageScope::~AIHTMSDamageScope(){if(d->l->damages.size()==d->depth+1)d->l->damages.removeLast();else d->l->damages.clear();if(d->l->damages.isEmpty())d->l->currentDamage.clear();}
struct AIHTMSDamageTagWrite::Data {HtmsEventsLedger*l;int depth;};
AIHTMSDamageTagWrite::AIHTMSDamageTagWrite(Room&r,const QVariant&v,int kind):d(new Data{htmsEventsLedger(r),0}){
 d->depth=d->l->pending.size();bool allowed=false;
 if(v.userType()==qMetaTypeId<DamageStruct>()&&!d->l->damages.isEmpty()){
  if(kind==0)allowed=true;
  else if(kind==1){for(const auto&f:d->l->damages)for(const auto&old:f.publicValues)if(sameDamage(v,old))allowed=true;}
  else if(kind==2)allowed=sameDamage(v,d->l->damages.last().previous);
 }
 d->l->pending.append({&v,allowed});
}
AIHTMSDamageTagWrite::~AIHTMSDamageTagWrite(){if(d->l->pending.size()==d->depth+1)d->l->pending.removeLast();else{d->l->pending.clear();d->l->currentDamage.clear();}}
void observeAIHTMSDamageTag(Room&r,const QString&key,const QVariant*value){
 if(key!="CurrentDamageStruct")return;auto*l=htmsEventsFind(r);if(!l)return;l->currentDamage.clear();
 if(!value||l->pending.isEmpty()||l->damages.isEmpty())return;
 auto&pending=l->pending.last();const bool allowed=pending.allowed&&pending.value==value;pending.allowed=false;
 if(allowed){l->currentDamage=*value;l->damages.last().publicValues.append(*value);}
}

// ---- htms-extra-response ----
#include "standard.h"
namespace {
bool htmsExtraResponseCurrent(const AIRequest&r,Room&room,const ServerPlayer*p){
 return p&&p->getRoom()==&room&&room.roomRuntime()&&!room.roomRuntime()->isClosing()
  &&r.decisionId&&room.getPlayers().contains(const_cast<ServerPlayer*>(p))
  &&r.viewerObjectName==p->objectName()&&r.worldView.self.objectName==p->objectName()
  &&aiRangeRequestCurrent(room,r,p);
}
bool owns(const ServerPlayer*p,const char*name){for(const auto &i:p->getSkillInstances())if(i.skillName==QLatin1String(name))return true;return false;}
// The issued inquiry is source scope. Do not re-run invalidity or activation
// gates before the fixed callback constructs its readonly policy card.
bool invoke(const AIRequest&r,const char*key){return r.kind==AIRequest::SkillInvoke&&r.choiceOptions.reason==QLatin1String(key);}
bool htmsExtraResponseFace(const AICardView&f,const Card*c,Room&r,const ServerPlayer*p){return c&&f.effectiveId==c->getEffectiveId()&&f.objectName==c->objectName()&&f.className==c->getClassName()&&f.suit==int(c->getSuit())&&f.number==c->getNumber()&&!f.legacyIdentityToken.isEmpty()&&f.legacyIdentityToken==aiLegacyCardIdentity(r,const_cast<ServerPlayer*>(p),c);}
}
void projectAIHTMSProperties(AIRequest&r,Room&room,ServerPlayer*p)
{
 // Reprojection is replacement of this slice, never an additive cache.
 // Clear before current() so stale/closing/foreign requests cannot retain facts.
 auto props=r.worldView.strategyFacts.value("owner_skill_properties").toObject();
 props.remove("maware_use");props.remove("s_newtype");
 auto htms=r.worldView.strategyFacts.value("htms").toObject();htms.remove("nvzidaoTarget");
 auto publicProps=htms.value("public_player_properties").toObject();
 for(const QString &name:publicProps.keys())if(publicProps.value(name).isObject()){
  auto row=publicProps.value(name).toObject();row.remove("s_newtype");
  if(row.isEmpty())publicProps.remove(name);else publicProps.insert(name,row);
 }
 if(publicProps.isEmpty())htms.remove("public_player_properties");else htms.insert("public_player_properties",publicProps);
 const auto publish=[&]{
  if(props.isEmpty())r.worldView.strategyFacts.remove("owner_skill_properties");else r.worldView.strategyFacts.insert("owner_skill_properties",props);
  if(htms.isEmpty())r.worldView.strategyFacts.remove("htms");else r.worldView.strategyFacts.insert("htms",htms);
 };publish();
 if(!htmsExtraResponseCurrent(r,room,p))return;
 if(r.kind==AIRequest::UseCard&&r.pattern=="@@maware")props.insert("maware_use",p->property("maware_use").toString());
 if(r.kind==AIRequest::PlayerChosen&&r.choiceOptions.reason=="nvzidaoCard"){
  const auto *who=room.getTag("nvzidaoTarget").value<ServerPlayer*>();
  // Null is source-known nil. An alien pointer is never dereferenced or guessed.
  if(!who)htms.insert("nvzidaoTarget",false);
  else if(room.getAllPlayers(true).contains(const_cast<ServerPlayer*>(who)))htms.insert("nvzidaoTarget",who->objectName());
 }
 if(r.kind==AIRequest::UseCard&&r.pattern=="@@s_newtypeOther"){
  auto values=htms.value("public_player_properties").toObject();
  for(const auto *other:room.getAllPlayers(true)){
   auto row=values.value(other->objectName()).toObject();
   // Fixed writer is Room::setPlayerProperty, broadcast to every recipient.
   // Preserve the serialized value; do not substitute newtype_pattern/name.
   row.insert("s_newtype",other->property("s_newtype").toString());values.insert(other->objectName(),row);
  }
  htms.insert("public_player_properties",values);
  props.insert("s_newtype",p->property("s_newtype").toString());
 }
 publish();
}
bool aiHTMSConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&q)
{
 if(!htmsExtraResponseCurrent(r,room,p)||q.size()!=6||!q.value("source_key").isString()||!q.value("name").isString()||!q.value("skill").isString()||!q.value("subcards").isArray())return false;
 const QString key=q.value("source_key").toString(),name=q.value("name").toString(),skill=q.value("skill").toString();
 const auto costs=q.value("subcards").toArray();
 const bool play=r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY&&r.pattern.isEmpty();
 int suit=int(Card::NoSuit),number=0,maxCosts=0;QString raw;bool scope=false;
 if(key=="#htms_zangsongCard"){scope=play&&owns(p,"htms_zangsong")&&name=="slash";raw="htms_zangsong";maxCosts=1;}
 else if(key=="#wangzhe"){scope=play&&owns(p,"wangzhe")&&name=="slash";raw="wangzhe";}
 else if(key=="soulfire"){scope=play&&owns(p,"soulfire")&&name=="fire_slash";raw="soulfire";maxCosts=1;}
 else if(key=="jiasuduijue"){scope=invoke(r,"jiasuduijue")&&name=="Duel";raw="jiasuduijue";suit=int(Card::SuitToBeDecided);}
 else if(key=="tprs"){scope=invoke(r,"tprs")&&name=="fire_slash";raw="tprs";}
 else if(key=="slash_defence"){scope=invoke(r,"slash_defence")&&name=="duel";raw="zhanxianfanyu";}
 else if(key=="@@maware"){
  const QStringList fields=r.worldView.strategyFacts.value("owner_skill_properties").toObject().value("maware_use").toString().split('+');
  bool numeric=false;const int count=fields.value(0).toInt(&numeric);
  scope=r.kind==AIRequest::UseCard&&r.pattern==key&&numeric&&count>0&&fields.value(1)==name
      &&p->property("maware_use").toString()==r.worldView.strategyFacts.value("owner_skill_properties").toObject().value("maware_use").toString();
  raw="samsara";number=-1;maxCosts=count;
 }
 else if(key=="@@s_newtypeOther"){
  scope=r.kind==AIRequest::UseCard&&r.pattern==key;raw="s_newtype";number=-1;
  bool declared=false;const auto all=r.worldView.strategyFacts.value("htms").toObject().value("public_player_properties").toObject();
  for(auto i=all.begin();i!=all.end();++i)if(i.value().toObject().value("s_newtype")==QJsonValue(name))declared=true;
  scope=scope&&declared;
 }
 if(!scope||(skill!=""&&skill!=raw)||q.value("suit")!=QJsonValue(suit)||q.value("number")!=QJsonValue(number)||costs.size()>maxCosts)return false;
 for(const auto &v:costs){
  if(!v.isDouble()||!std::isfinite(v.toDouble())||v.toDouble()!=std::floor(v.toDouble())||v.toDouble()<0||v.toDouble()>std::numeric_limits<int>::max())return false;
  const int id=v.toInt();const Card*c=nullptr;bool disclosed=false;
  if(p->handCards().contains(id)){
   c=Sanguosha->getCard(id);for(const auto &f:r.worldView.handCards)if(htmsExtraResponseFace(f,c,room,p))disclosed=true;
  }else if((key=="soulfire"&&p->getHandPile().contains(id))||(key=="@@maware"&&p->getPile("wooden_ox").contains(id))){
   c=Sanguosha->getCard(id);for(const auto &pile:r.worldView.self.piles)if(key!="@@maware"||pile.name=="wooden_ox")for(const auto &f:pile.cards)if(htmsExtraResponseFace(f,c,room,p))disclosed=true;
  }else if(key=="soulfire"){
   for(const auto *eq:p->getEquips())if(eq->getEffectiveId()==id){c=eq;for(const auto &f:r.worldView.self.equips)if(htmsExtraResponseFace(f,c,room,p))disclosed=true;}
  }
  if(!c||!disclosed||room.getCardOwner(id)!=p)return false;
  if(key=="#htms_zangsongCard"&&!c->isDamageCard())return false;
  if(key=="soulfire"&&!c->isKindOf("Slash")&&!c->isKindOf("Jink"))return false;
 }
 return htmsExtraResponseCurrent(r,room,p);
}

// ---- zhongerhx ----
#include "ai-strategy/context.h"
std::unique_ptr<Card> aiZhongerhxEvaluation(const AIRequest &q,Room &room,ServerPlayer *viewer,int id,
    QJsonObject *disclosedFace,QString *error)
{
 auto deny=[&](const char *text)->std::unique_ptr<Card>{if(error)*error=QString::fromLatin1(text);return {};};
 if(error)error->clear();
 if(!viewer||viewer->getRoom()!=&room||q.viewerObjectName!=viewer->objectName()
  ||q.worldView.self.objectName!=viewer->objectName()||!q.decisionId
  ||q.stateRevision!=room.roomRuntime()->stateRevision()||q.kind!=AIRequest::UseCard
  ||q.pattern!="@@zhongerhx"||!viewer->hasSkill("zhongerhx")
  ||(q.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&q.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE))
  return deny("Zhongerhx physical hypothesis is outside its actual owner inquiry");
 if(id<0||id>=Sanguosha->getCardCount()||viewer->getMark("zhongerhuan")!=id+1)return deny("Zhongerhx marked physical identity differs");
 // Existing real fillAG recipient witness survives clearAG, but any move,
 // identity change or changed disclosed face revokes it. No hidden deck read.
 const auto witness=readAIStrategyChoiceAGDisclosure(room,viewer,id);
 if(witness.value("known")!=QJsonValue(true))return deny("Zhongerhx selected face lacks retained AG disclosure");
 const Card *source=room.getCard(id);
 if(!source||source->isVirtualCard()||source->getEffectiveId()!=id)
  return deny("Zhongerhx actual physical source changed");
 std::unique_ptr<Card> card(Card::CloneReadonlyPhysicalPolicy(source));
 if(!card)return deny("Zhongerhx exact physical factory cannot form a readonly hypothesis");
 // This is the only source mutation. Never setSkillName on Room's real card.
 card->setSkillName("zhongerhx");
 if(card->isVirtualCard()||card->getEffectiveId()!=id||card->objectName()!=source->objectName()
  ||card->getClassName()!=source->getClassName()||card->getSuit()!=source->getSuit()
  ||card->getNumber()!=source->getNumber()||card->toString()!=source->toString()
  ||card->getSubcards()!=source->getSubcards())
  return deny("Zhongerhx readonly factory changed physical source metadata");
 if(disclosedFace){*disclosedFace=witness.value("card").toObject();
  disclosedFace->insert("legacy_identity_token",aiLegacyCardIdentity(room,viewer,source));}
 return card;
}

// ---- scarlet-yuri-constructions-extra ----
namespace {
bool scarletYuriConstructionsExtraOwns(const ServerPlayer*p,const QString&skill){
 for(const auto&i:p->getSkillInstances())if(i.skillName==skill)return true;
 return false;
}
bool faceCurrent(Room&room,const ServerPlayer*p,const AICardView&v,const Card*c){
 return c&&v.effectiveId==c->getEffectiveId()&&room.getCardOwner(v.effectiveId)==p
  &&v.objectName==c->objectName()&&v.className==c->getClassName()
  &&v.suit==int(c->getSuit())&&v.number==c->getNumber()
  &&!v.legacyIdentityToken.isEmpty()
  &&v.legacyIdentityToken==aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c);
}
const AICardView* gensouFirst(const AIRequest&r,Room&room,const ServerPlayer*p){
 const auto ids=p->getPile("gensou");if(ids.isEmpty())return nullptr;
 // Do not substitute the current native loop element for the AI's first().
 for(const auto&pile:r.worldView.self.piles)if(pile.name=="gensou")
  for(const auto&v:pile.cards)if(v.effectiveId==ids.first()
   &&faceCurrent(room,p,v,Sanguosha->getCard(ids.first())))return &v;
 return nullptr;
}
bool play(const AIRequest&r){return r.kind==AIRequest::Activate
 &&r.reason==CardUseStruct::CARD_USE_REASON_PLAY&&r.pattern.isEmpty();}
}
bool aiScarletYuriConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||!r.decisionId||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||!aiRangeRequestCurrent(room,r,p))return false;
 const QSet<QString>fields{"source_key","skill","name","suit","number","subcards"};
 if(s.size()!=fields.size())return false;
 for(auto i=s.begin();i!=s.end();++i)if(!fields.contains(i.key()))return false;
 if(!s.value("source_key").isString()||!s.value("skill").isString()||!s.value("name").isString()
  ||!s.value("suit").isDouble()||!s.value("number").isDouble()||!s.value("subcards").isArray())return false;
 const QString key=s.value("source_key").toString(),skill=s.value("skill").toString(),name=s.value("name").toString();
 const auto costs=s.value("subcards").toArray();
 const bool zero=s.value("suit")==QJsonValue(int(Card::NoSuit))&&s.value("number")==QJsonValue(0);
 if(key=="@s2_jizhan-exchange")return scarletYuriConstructionsExtraOwns(p,"s2_jizhan")
  &&r.kind==AIRequest::RespondCard&&r.choiceOptions.question=="askForCard"
  &&r.prompt.startsWith("@s2_jizhan-exchange:")&&r.handlingMethod==Card::MethodDiscard
  &&name=="slash"&&skill=="s2_jizhan"&&zero&&costs.isEmpty();
 if(key=="LuaZhenlie")return scarletYuriConstructionsExtraOwns(p,key)&&r.kind==AIRequest::Choice
  &&r.choiceOptions.question=="askForChoice"&&r.choiceOptions.reason==key
  &&name=="fire_slash"&&skill==key&&zero&&costs.isEmpty();
 if(key=="genjutsu"){
  if(!scarletYuriConstructionsExtraOwns(p,key)||r.kind!=AIRequest::PlayerChosen||r.choiceOptions.question!="askForPlayerChosen"
   ||r.choiceOptions.reason!=key||name!="slash")return false;
  const auto*v=gensouFirst(r,room,p);if(!v||s.value("suit")!=QJsonValue(v->suit)
   ||s.value("number")!=QJsonValue(v->number))return false;
  // clone -> addSubcard -> setSkillName: all actual intermediate products.
  if(costs.isEmpty())return skill.isEmpty();
  return costs.size()==1&&costs.first()==QJsonValue(v->effectiveId)
   &&(skill.isEmpty()||skill=="genjutsuslash");
 }
 if(key=="#s2_longhun"){
  static const QSet<QString>names{"slash","fire_slash","thunder_slash","peach"};
  if(!play(r)||!scarletYuriConstructionsExtraOwns(p,"s2_longhun")||!names.contains(name)||!costs.isEmpty()
   ||(!skill.isEmpty()&&skill!="s2_longhun"))return false;
  if(zero)return true; // empty native SkillCard defaults
  const auto matches=[&](const AICardView&v){const Card*c=Sanguosha->getCard(v.effectiveId);
   return faceCurrent(room,p,v,c)&&c->isKindOf("BasicCard")
    &&s.value("suit")==QJsonValue(v.suit)&&s.value("number")==QJsonValue(v.number);};
  for(const auto&v:r.worldView.handCards)if(p->handCards().contains(v.effectiveId)&&matches(v))return true;
  for(const auto&pile:r.worldView.self.piles)if(pile.name=="wooden_ox")
   for(const auto&v:pile.cards)if(p->getPile("wooden_ox").contains(v.effectiveId)&&matches(v))return true;
  return false;
 }
 if(key=="#genshunoYURICard"){
  if(!play(r)||!scarletYuriConstructionsExtraOwns(p,"genshunoYURI")||skill!="genshunoYURI"||!zero||!costs.isEmpty())return false;
  bool listed=false;for(const auto&v:r.worldView.strategyFacts.value("pattern_names").toArray())
   if(v.isString()&&v.toString()==name){listed=true;break;}
  if(!listed)return false;
  std::unique_ptr<Card>c(Sanguosha->cloneCard(name,Card::NoSuit,0));
  return c&&(c->isKindOf("BasicCard")||c->isNDTrick());
 }
 return false;
}
bool aiGenjutsuPileMaterial(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s,int id){
 return s.value("source_key")==QJsonValue("genjutsu")&&aiScarletYuriConstruction(r,room,p,s)
  &&s.value("subcards").toArray().size()==1&&s.value("subcards").toArray().first()==QJsonValue(id);
}

// ---- scarlet-yuri-constructions-zero ----
bool aiScarletTenyearZeroConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||!r.decisionId||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.stateRevision!=room.roomRuntime()->stateRevision()||!room.getPlayers().contains(const_cast<ServerPlayer*>(p)))return false;
 const QSet<QString>keys{"source_key","skill","name","suit","number","subcards"};
 if(s.size()!=keys.size())return false;for(auto i=s.begin();i!=s.end();++i)if(!keys.contains(i.key()))return false;
 if(s.value("suit")!=QJsonValue(int(Card::NoSuit))||s.value("number")!=QJsonValue(0)
  ||!s.value("subcards").isArray()||!s.value("subcards").toArray().isEmpty())return false;
 const QString key=s.value("source_key").toString();
 // Exact reached source inquiry only. Recipients of Jianji/Suizheng need not own
 // the skill. No phase, use-count, availability or payment grant is added.
 if(key=="@@jianjiyh")return r.kind==AIRequest::UseCard&&r.pattern=="@@jianjiyh"
  &&(r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE||r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE)
  &&s.value("skill")==QJsonValue("_jianjiyh")&&s.value("name")==QJsonValue("slash");
 if(key=="suizheng1")return r.kind==AIRequest::PlayerChosen&&r.choiceOptions.question=="askForPlayerChosen"
  &&r.choiceOptions.reason==key&&s.value("skill")==QJsonValue("suizheng")&&s.value("name")==QJsonValue("slash");
 if(key!="s2_fanji"&&key!="s2_yongbing")return false;
 return r.kind==AIRequest::SkillInvoke&&r.choiceOptions.question=="askForSkillInvoke"
  &&r.choiceOptions.reason==key&&s.value("skill")==QJsonValue(key)
  &&s.value("name")==QJsonValue(key=="s2_fanji"?"slash":"amazing_grace");
}

// ---- sgs10th-nyarz-planning-2 ----
void projectAIZhuangpoPlanning(AIRequest&r,Room&room,const ServerPlayer*p){
 auto facts=r.worldView.strategyFacts.value("sgs10th").toObject();
 facts.remove("ny_10th_zhuangpo");r.worldView.strategyFacts.insert("sgs10th",facts);
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||!r.decisionId||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.stateRevision!=room.roomRuntime()->stateRevision()||r.kind!=AIRequest::SkillInvoke
  ||r.choiceOptions.question!="askForSkillInvoke"||r.choiceOptions.reason!="ny_10th_zhuangpo")return;
 // Fixed TargetConfirmed producer sets this owner's own tag to the actual
 // ordered use.to member immediately before this inquiry. No prompt inference,
 // additional hasSkill test, card-face exposure, or payment authorization.
 auto*target=p->getTag("ny_10th_zhuangpo").value<ServerPlayer*>();
 if(!target||!room.getAllPlayers(true).contains(target))return;
 facts.insert("ny_10th_zhuangpo",target->objectName());
 r.worldView.strategyFacts.insert("sgs10th",facts);
}

// ---- sgs10th-nyarz-planning-3 ----
void projectAIXiaoyanPlanning(AIRequest&r,Room&room,const ServerPlayer*p){
 auto facts=r.worldView.strategyFacts.value("sgs10th").toObject();facts.remove("ny_10th_xiaoyan");
 r.worldView.strategyFacts.insert("sgs10th",facts);
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||!r.decisionId||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.stateRevision!=room.roomRuntime()->stateRevision()||r.kind!=AIRequest::Discard
  ||r.choiceOptions.question!="askForDiscard"||r.choiceOptions.reason!="ny_10th_xiaoyan"
  ||r.choiceOptions.minCount!=1||r.choiceOptions.maxCount!=1||!r.choiceOptions.optional
  ||!r.choiceOptions.context.value("exchange").toBool()
  ||!r.choiceOptions.context.value("include_equip").toBool())return;
 // The fixed GameStart producer publicly logs its actor and deals damage before
 // storing that actor, then asks each recipient this exact optional HE exchange.
 // Observe the actual tag, not a reconstructed prompt target or hidden card.
 auto*target=room.getTag("ny_10th_xiaoyan").value<ServerPlayer*>();
 if(!target||!room.getAllPlayers(true).contains(target))return;
 facts.insert("ny_10th_xiaoyan",target->objectName());r.worldView.strategyFacts.insert("sgs10th",facts);
}

// ---- sgs10th-nyarz-planning-4 ----
namespace {
bool ownerPlay(const AIRequest&r,const ServerPlayer*p){
 if(!p||!p->getRoom()||!p->getRoom()->roomRuntime()||p->getRoom()->roomRuntime()->isClosing()
  ||!r.decisionId||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY||!r.pattern.isEmpty()
  ||!aiRangeRequestCurrent(*p->getRoom(),r,p))return false;
 // Source presence only. Native activation/invalidity/selected targets remain
 // the existing skill-scope/factory/final checks, not this readonly predicate.
 for(const auto&i:p->getSkillInstances())if(i.skillName=="renchou")return true;
 return false;
}
}
bool aiRenchouEvaluation(const AIRequest&r,const ServerPlayer*p,const QJsonObject&s){
 return ownerPlay(r,p)&&s.size()==4&&s.value("kind")=="evaluate_skill_card"
  &&(s.value("skill")=="renchou"||s.value("skill")=="renchou_limit")
  &&s.value("name")=="slash"&&s.value("subcards").isArray()&&s.value("subcards").toArray().isEmpty();
}
int aiRenchouPrototype(const AIRequest&r,const ServerPlayer*p,const QString&skill,const QString&name){
 // Source #renchou:.:: actual LuaSkillCard, empty cost/declaration. This admits
 // only its finite prototype. Existing native receipt construction still owns
 // source/instance refs and independently checks zero cost and target legality.
 return skill=="renchou"&&name=="#renchou"&&ownerPlay(r,p)?0:-1;
}

// ---- sgs10th-nyarz-planning-5 ----
bool aiHfgushiConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||!r.decisionId||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY||!r.pattern.isEmpty()
  ||!aiRangeRequestCurrent(room,r,p))return false;
 const QSet<QString>fields{"source_key","name","skill","suit","number","subcards"};
 if(s.size()!=fields.size())return false;for(auto i=s.begin();i!=s.end();++i)if(!fields.contains(i.key()))return false;
 if(s.value("source_key")!="hfgushi"||s.value("skill")!="hfgushi"
  ||(s.value("name")!="peach"&&s.value("name")!="ex_nihilo"&&s.value("name")!="duel")
  ||!s.value("subcards").isArray()||!s.value("subcards").toArray().isEmpty())return false;
 const bool first=s.value("suit")==QJsonValue(int(Card::SuitToBeDecided))&&s.value("number")==QJsonValue(-1);
 const bool parsed=s.value("suit")==QJsonValue(int(Card::NoSuit))&&s.value("number")==QJsonValue(0);
 if(!first&&!parsed)return false;
 // Exact fixed source's two allocations. Current own public mark chooses the
 // name in Lua; this grants all three readonly hypotheses, not availability,
 // activation, mark change, selected payment or a foreign owner's rule facts.
 for(const auto&i:p->getSkillInstances())if(i.skillName=="hfgushi")return true;
 return false;
}

// ---- sgs10th-nyarz-planning-6 ----
#include "skill-runtime-coordinator.h"
#include "settings.h"
namespace {
bool sgs10thNyarzPlanning6Current(const AIRequest&r,Room&room,const ServerPlayer*p){return p&&p->getRoom()==&room&&room.roomRuntime()&&!room.roomRuntime()->isClosing()&&r.decisionId&&r.viewerObjectName==p->objectName()&&r.worldView.self.objectName==p->objectName()&&aiRangeRequestCurrent(room,r,p);}
bool response(const AIRequest&r,const QString&key){return r.kind==AIRequest::UseCard&&r.pattern=="@@"+key&&(r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE||r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE);}
bool same(const AICardView&v,const Card*c,Room&room,const ServerPlayer*p){return c&&v.effectiveId==c->getEffectiveId()&&v.objectName==c->objectName()&&v.className==c->getClassName()&&v.suit==int(c->getSuit())&&v.number==c->getNumber()&&!v.legacyIdentityToken.isEmpty()&&v.legacyIdentityToken==aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c);}
bool material(const AIRequest&r,Room&room,const ServerPlayer*p,int id){
 // Source getCards("he") yields the wrapper in hand and actual inner Equip.
 // Matching an ID alone does not authenticate either object's current face.
 if(p->handCards().contains(id))for(const auto&v:r.worldView.handCards)if(same(v,Sanguosha->getCard(id),room,p))return true;
 for(const Card*c:p->getEquips())if(c->getEffectiveId()==id)for(const auto&v:r.worldView.self.equips)if(same(v,c,room,p))return true;
 return false;
}
struct Sgs10thNyarzPlanning6Frame{Room*room=nullptr;QPointer<ServerPlayer>target;const void*callback=nullptr;int depth=-1;};
thread_local QVector<Sgs10thNyarzPlanning6Frame> sgs10thNyarzPlanning6Frames;
}
bool aiGfengyingJxyangjieEvaluation(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 if(!sgs10thNyarzPlanning6Current(r,room,p)||s.size()!=4||s.value("kind")!="evaluate_skill_card"||!s.value("subcards").isArray())return false;
 const QString key=s.value("skill").toString();if(!response(r,key))return false;
 const auto ids=s.value("subcards").toArray();
 if(key=="jxyangjie")return s.value("name")=="fire_slash"&&ids.isEmpty();
 if(key!="gfengying"||s.value("name")!="thunder_slash"||ids.size()!=1||!ids.first().isDouble())return false;
 const double id=ids.first().toDouble();return id>=0&&id<Sanguosha->getCardCount()&&id==std::floor(id)&&material(r,room,p,int(id));
}
bool aiGfengyingJxyangjieRecipe(const AIRequest&r,Room&room,const ServerPlayer*p,const QString&key,const Card*c){
 // The caller obtains c exclusively from the existing current-request store.
 // No new clone, ticket, payment or Lua descriptor can enter this predicate.
 if(!sgs10thNyarzPlanning6Current(r,room,p)||!response(r,key)||!c||c->getSkillName(false)!=key)return false;
 if(key=="jxyangjie")return c->objectName()=="fire_slash"&&c->getSubcards().isEmpty()&&c->getSuit()==Card::NoSuit&&c->getNumber()==0;
 return key=="gfengying"&&c->objectName()=="thunder_slash"&&c->getSubcards().size()==1&&material(r,room,p,c->getSubcards().first());
}
int aiGfengyingPrototype(const AIRequest&r,const ServerPlayer*p,const QString&key,const QString&name){
 if(!p||!p->getRoom()||!sgs10thNyarzPlanning6Current(r,*p->getRoom(),p)||!response(r,key))return -1;
 // Actual LuaSkillCard protocols. Native V2 selection/final material/target
 // checks still run; this only admits the exact source-declared prototype.
 return key=="gfengying"&&name=="#"+key?1:-1;
}
bool registerAISgs10thSources(Room&room,lua_State*L,QString*reason){
 return registerAIPinnedSource(room,L,"extensions/sgs10th.lua",QByteArray("356189a1f627c36a77521246ecfa6d583690eb49a511824b8fb90b8b12ceead6"),reason);
}
struct AIYuxinScope::Data{int depth;};
AIYuxinScope::AIYuxinScope(Room&room,const LuaTriggerSkillV2*skill,ServerPlayer*p,int event,const SkillContext&ctx,bool effect):d(new Data{int(sgs10thNyarzPlanning6Frames.size())}){
 Sgs10thNyarzPlanning6Frame f;f.room=&room;
 if(effect&&event==EnterDying&&skill&&skill->objectName()=="ny_10th_yuxin"&&Sanguosha->getSkill(skill->objectName())==skill){
  auto&vm=room.roomRuntime()->lua();lua_State*L=vm.state();const int top=lua_gettop(L);
  if(skill->on_effect.state()==L&&skill->on_effect.push(L)){
   const void*id=lua_topointer(L,-1);
   if(matchAIPinnedFunction(room,L,-1,"extensions/sgs10th.lua",14072,14099)){f.callback=id;f.depth=vm.protectedCallDepth()+1;f.target=ctx.invoker?ctx.invoker:p;}
  }lua_settop(L,top);
 }sgs10thNyarzPlanning6Frames.append(f); // unrelated/nested callbacks shadow the outer producer
}
AIYuxinScope::~AIYuxinScope(){if(sgs10thNyarzPlanning6Frames.size()==d->depth+1)sgs10thNyarzPlanning6Frames.removeLast();else sgs10thNyarzPlanning6Frames.clear();}
void projectAIYuxinChenfusuiPlanning(AIRequest&r,Room&room,const ServerPlayer*p){
 auto ten=r.worldView.strategyFacts.value("sgs10th").toObject();ten.remove("ny_10th_yuxin");r.worldView.strategyFacts.insert("sgs10th",ten);
 r.worldView.strategyFacts.remove("round50_descriptions");
 if(!sgs10thNyarzPlanning6Current(r,room,p))return;
 if(r.kind==AIRequest::SkillInvoke&&r.choiceOptions.question=="askForSkillInvoke"&&r.choiceOptions.reason=="ny_10th_yuxin"&&!sgs10thNyarzPlanning6Frames.isEmpty()){
  const auto f=sgs10thNyarzPlanning6Frames.last();auto&vm=room.roomRuntime()->lua();
  if(f.room==&room&&f.callback&&f.target&&room.findPlayerByObjectName(f.target->objectName(),true)==f.target&&vm.protectedCallDepth()==f.depth&&room.getTag("ny_10th_yuxin").value<ServerPlayer*>()==f.target){
   lua_State*L=vm.state();bool reached=false;
   for(int n=0;;++n){lua_Debug a{};if(!lua_getstack(L,n,&a)||!lua_getinfo(L,"Slf",&a))break;const void*id=lua_topointer(L,-1);lua_pop(L,1);if(a.what&&QString::fromLatin1(a.what)=="C")continue;reached=id==f.callback&&a.currentline==14091;break;}
   if(reached){ten.insert("ny_10th_yuxin",f.target->objectName());r.worldView.strategyFacts.insert("sgs10th",ten);}
  }
 }
 if(r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY)return;
 bool owner=false;for(const auto&i:p->getSkillInstances())if(i.skillName=="chenfusui")owner=true;if(!owner)return;
 QJsonObject out;auto ps=r.worldView.players;ps.prepend(r.worldView.self);
 for(const auto&v:ps){auto*t=room.findPlayerByObjectName(v.objectName,true);if(!t)continue;QJsonObject row;
  for(const auto&sv:v.skills){const Skill*s=Sanguosha->getSkill(sv.skillName);if(!s||!s->isVisible())continue;
   bool visible=!Config.EnableHegemony||t==p;
   for(const auto&i:t->getSkillInstances())if(i.skillName==sv.skillName&&SkillRuntimeCoordinator::canReceiveSkillInstance(room,p,t,i))visible=true;
   // Match ServerPlayer::setSkillDescriptionSwap receiver visibility. The
   // source passes no instanceId: only public legacy translation/swap applies.
   if(visible)row.insert(sv.skillName,s->getDescription(t));
  }out.insert(v.objectName,row);
 }r.worldView.strategyFacts.insert("round50_descriptions",out);
}

// ---- sgs10th-nyarz-planning ----
void projectAIYijiaPlanning(AIRequest&r,Room&room,const ServerPlayer*p){
 auto facts=r.worldView.strategyFacts.value("sgs10th").toObject();facts.remove("ny_10th_yijia");
 r.worldView.strategyFacts.insert("sgs10th",facts);
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.stateRevision!=room.roomRuntime()->stateRevision()||r.choiceOptions.reason!="ny_10th_yijia")return;
 if(!((r.kind==AIRequest::PlayerChosen&&r.choiceOptions.question=="askForPlayerChosen")
   ||(r.kind==AIRequest::CardChosen&&r.choiceOptions.question=="askForCardChosen"
      &&r.choiceOptions.choices==QStringList{QStringLiteral("e")})))return;
 // Native owner writes the damaged recipient's objectName before both questions.
 // Preserve the source string (including known empty / no matching roster name);
 // only the existing lawful roster resolver interprets it as a Player.
 facts.insert("ny_10th_yijia",p->getTag("ny_10th_yijia").toString());
 r.worldView.strategyFacts.insert("sgs10th",facts);
}

// ---- qhstandard-choice-2 ----
void projectAIQhstandardChoiceProperties(AIRequest &request, Room &room, ServerPlayer *viewer)
{
    auto properties = request.worldView.strategyFacts.value("owner_skill_properties").toObject();
    properties.remove("mythluochong_choice");
    properties.remove("qhstandardtuxi_card");
    const auto publish = [&] {
        if (properties.isEmpty()) request.worldView.strategyFacts.remove("owner_skill_properties");
        else request.worldView.strategyFacts.insert("owner_skill_properties", properties);
    };
    publish();
    if (!viewer || viewer->getRoom() != &room || !room.roomRuntime()
        || room.roomRuntime()->isClosing() || !room.getPlayers().contains(viewer)
        || !request.decisionId || request.viewerObjectName != viewer->objectName()
        || request.worldView.self.objectName != viewer->objectName()
        || request.stateRevision != room.roomRuntime()->stateRevision()) return;
    const char *property = nullptr;
    if (request.kind == AIRequest::PlayerChosen
        && request.choiceOptions.reason == "mythluochong"
        && request.choiceOptions.question == "askForPlayerChosen") {
        QSet<QString> names;
        for (const QString &name : request.choiceOptions.playerNames) {
            ServerPlayer *candidate = room.findPlayerByObjectName(name, true);
            if (!candidate || !room.getPlayers().contains(candidate) || names.contains(name)) return;
            names.insert(name);
        }
        property = "mythluochong_choice";
    } else if (request.kind == AIRequest::Discard
        && request.choiceOptions.reason == "qhstandardtuxiCARD"
        && request.choiceOptions.question == "askForDiscard") {
        property = "qhstandardtuxi_card";
    }
    if (!property) return;
    // Fixed qhstandard.lua:8330/8401 and1444 write these owner properties
    // before the exact respective question. Preserve current QVariant::toString
    // including absent/empty/unusual strings; do not infer from prompt/choices.
    // Tuxi IDs do not disclose faces or authorize payment: existing owner/known
    // face access and final native discard admission remain authoritative.
    properties.insert(QString::fromLatin1(property), viewer->property(property).toString());
    publish();
}

// ---- qhstandard-choice ----
void projectAIQhfireQinyin(AIRequest &request, Room &room, ServerPlayer *viewer)
{
    auto properties = request.worldView.strategyFacts.value("owner_skill_properties").toObject();
    properties.remove("qhfireqinyin-AI");
    const auto publish = [&] {
        if (properties.isEmpty()) request.worldView.strategyFacts.remove("owner_skill_properties");
        else request.worldView.strategyFacts.insert("owner_skill_properties", properties);
    };
    publish();
    if (!viewer || viewer->getRoom() != &room || !room.roomRuntime()
        || room.roomRuntime()->isClosing() || !room.getPlayers().contains(viewer)
        || !request.decisionId || request.viewerObjectName != viewer->objectName()
        || request.worldView.self.objectName != viewer->objectName()
        || request.stateRevision != room.roomRuntime()->stateRevision()
        || request.kind != AIRequest::PlayerChosen
        || request.choiceOptions.reason != "qhfireqinyin"
        || request.choiceOptions.question != "askForPlayerChosen") return;
    QSet<QString> names;
    for (const QString &name : request.choiceOptions.playerNames) {
        ServerPlayer *candidate = room.findPlayerByObjectName(name, true);
        if (!candidate || !room.getPlayers().contains(candidate) || names.contains(name)) return;
        names.insert(name);
    }
    // Fixed extensions/qhstandard.lua:5453 publishes exactly this current
    // owner's property immediately before the native PlayerChosen question.
    // The global mark-resolution source can ask a player after skill loss;
    // do not add a hasSkill/phase/current-player gate absent from that source.
    // QVariant::toString preserves absent/empty and non-choice string values.
    // No prompt-derived repair, foreign property, callback or history read.
    properties.insert("qhfireqinyin-AI", viewer->property("qhfireqinyin-AI").toString());
    publish();
}

// ---- qhstandard-guhuo ----
namespace {
bool qhstandardGuhuoCurrent(const AIRequest&r,Room&room,const ServerPlayer*p){
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||!r.decisionId||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||!aiRangeRequestCurrent(room,r,p))return false;
 bool owned=false;for(const auto&i:p->getSkillInstances())if(i.skillName=="qhwindguhuo")owned=true;
 return owned;
}
}
void projectAIQhwindGuhuo(AIRequest&r,Room&room,ServerPlayer*p){
 auto props=r.worldView.strategyFacts.value("owner_skill_properties").toObject();props.remove("qhwindguhuoused");
 const auto publish=[&]{if(props.isEmpty())r.worldView.strategyFacts.remove("owner_skill_properties");else r.worldView.strategyFacts.insert("owner_skill_properties",props);};publish();
 if(!qhstandardGuhuoCurrent(r,room,p))return;
 const bool play=r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY;
 const bool response=(r.kind==AIRequest::RespondCard||r.kind==AIRequest::UseCard)
  &&(r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE||r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE);
 if(!play&&!response)return;
 // Fixed qhstandard-ai.lua2915/2934 reads the actual actor's current public
 // property. Preserve absent/empty/duplicate name text; no last-writer ledger.
 props.insert("qhwindguhuoused",p->property("qhwindguhuoused").toString());publish();
}
bool aiQhwindGuhuoConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 if(!qhstandardGuhuoCurrent(r,room,p)||r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY
  ||!r.pattern.isEmpty()||p->isKongcheng()||p->getMark("qhwindguhuoNum")>3)return false;
 const QSet<QString>fields{"source_key","skill","name","suit","number","subcards"};
 if(s.size()!=fields.size())return false;for(auto i=s.begin();i!=s.end();++i)if(!fields.contains(i.key()))return false;
 static const QStringList names{"ex_nihilo","snatch","peach","slash","archery_attack","savage_assault","dismantlement","fire_attack"};
 const QString name=s.value("name").toString();
 if(s.value("source_key")!=QJsonValue("qhwindguhuo")||s.value("skill")!=QJsonValue(QString())
  ||!s.value("name").isString()||!names.contains(name)||!s.value("suit").isDouble()||!s.value("number").isDouble()
  ||!s.value("subcards").isArray()||!s.value("subcards").toArray().isEmpty()
  ||p->property("qhwindguhuoused").toString().split('+').contains(name))return false;
 // Empty-cost fresh source clone has the face of one actually disclosed hand
 // card. Sorting/first acceptable name remain in the full source policy.
 for(int id:p->handCards())if(room.getCardOwner(id)==p&&room.getCardPlace(id)==Player::PlaceHand){
  const Card*c=Sanguosha->getCard(id);if(!c||s.value("suit")!=QJsonValue(int(c->getSuit()))||s.value("number")!=QJsonValue(c->getNumber()))continue;
  for(const auto&v:r.worldView.handCards)if(v.effectiveId==id&&v.objectName==c->objectName()&&v.className==c->getClassName()
   &&v.suit==int(c->getSuit())&&v.number==c->getNumber()&&!v.legacyIdentityToken.isEmpty()
   &&v.legacyIdentityToken==aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c))return true;
 }
 return false;
}

// ---- qhstandard-lianhuan ----
bool aiQhfireLianhuanConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s)
{
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||!r.decisionId||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY||!r.pattern.isEmpty()
  ||!aiRangeRequestCurrent(room,r,p))return false;
 const QSet<QString>fields{"source_key","skill","name","suit","number","subcards"};
 if(s.size()!=fields.size())return false;for(auto i=s.begin();i!=s.end();++i)if(!fields.contains(i.key()))return false;
 if(s.value("source_key")!=QJsonValue("qhfirelianhuan")||s.value("skill")!=QJsonValue(QString())
  ||s.value("name")!=QJsonValue("thunder_slash")||s.value("suit")!=QJsonValue(int(Card::Spade))
  ||!s.value("number").isDouble()||!s.value("subcards").isArray()||!s.value("subcards").toArray().isEmpty())return false;
 // Fixed qhstandard-ai.lua1207–1237: a fresh unskilled, cost-free
 // thunder Slash carries the printed number of an actual own HE spade.
 // Presence attests this source; selected provider eligibility stays separate.
 bool owned=false;for(const auto&i:p->getSkillInstances())if(i.skillName=="qhfirelianhuan")owned=true;
 if(!owned)return false;
 const auto exact=[&](const AICardView&v,const Card*c){
  if(!c||c->getEffectiveId()<0||c->getSuit()!=Card::Spade||s.value("number")!=QJsonValue(c->getNumber()))return false;
  const int id=c->getEffectiveId();return room.getCardOwner(id)==p&&v.effectiveId==id
   &&v.objectName==c->objectName()&&v.className==c->getClassName()&&v.suit==int(c->getSuit())&&v.number==c->getNumber()
   &&!v.legacyIdentityToken.isEmpty()&&v.legacyIdentityToken==aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c);
 };
 for(int id:p->handCards())if(room.getCardPlace(id)==Player::PlaceHand)
  for(const auto&face:r.worldView.handCards)if(face.effectiveId==id&&exact(face,Sanguosha->getCard(id)))return true;
 // Player::getCards("he") supplies the actual equipment object, not a
 // replacement wrapper. Require that exact already-disclosed source identity.
 for(const Card*c:p->getEquips())if(c&&room.getCardPlace(c->getEffectiveId())==Player::PlaceEquip)
  for(const auto&face:r.worldView.self.equips)if(exact(face,c))return true;
 return false;
}

// ---- qhstandard-qhturn ----
bool aiQhTurnConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()||!r.decisionId
  ||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY||!r.pattern.isEmpty()
  ||!aiRangeRequestCurrent(room,r,p))return false;
 const QSet<QString>fields{"source_key","skill","name","suit","number","subcards"};
 if(s.size()!=fields.size())return false;for(auto i=s.begin();i!=s.end();++i)if(!fields.contains(i.key()))return false;
 const QString key=s.value("source_key").toString(),name=s.value("name").toString();
 if((key!="qhfirelianhuan"&&key!="qhfireshuangxiong"&&key!="qhwindguhuo")||s.value("skill")!=QJsonValue(key)
  ||!s.value("name").isString()||!s.value("suit").isDouble()||!s.value("number").isDouble()
  ||!s.value("subcards").isArray()||s.value("subcards").toArray().size()!=1)return false;
 bool owned=false;for(const auto&i:p->getSkillInstances())if(i.skillName==key)owned=true;if(!owned)return false;
 const auto v=s.value("subcards").toArray().first();const double n=v.toDouble(-1);
 if(!v.isDouble()||!std::isfinite(n)||n<0||n!=std::floor(n)||n>std::numeric_limits<int>::max())return false;
 const int id=int(n);if(room.getCardOwner(id)!=p)return false;
 const Card*c=nullptr;const AICardView*face=nullptr;
 if(room.getCardPlace(id)==Player::PlaceHand&&p->handCards().contains(id)){
  c=Sanguosha->getCard(id);for(const auto&row:r.worldView.handCards)if(row.effectiveId==id){face=&row;break;}
 }else if(key=="qhfirelianhuan"&&room.getCardPlace(id)==Player::PlaceEquip){
  for(const Card*e:p->getEquips())if(e->getEffectiveId()==id){c=e;break;}
  for(const auto&row:r.worldView.self.equips)if(row.effectiveId==id){face=&row;break;}
 }
 if(!c||!face||face->objectName!=c->objectName()||face->className!=c->getClassName()
  ||face->suit!=int(c->getSuit())||face->number!=c->getNumber()||face->legacyIdentityToken.isEmpty()
  ||face->legacyIdentityToken!=aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c)
  ||s.value("suit")!=QJsonValue(int(c->getSuit()))||s.value("number")!=QJsonValue(c->getNumber()))return false;
 if(key=="qhfirelianhuan")return(c->getSuit()==Card::Spade&&name=="thunder_slash")||(c->getSuit()==Card::Club&&name=="iron_chain");
 if(key=="qhfireshuangxiong"){
  if(p->getMark("&qhfireshuangxiong_damage-Clear")==2)return false;
  int color=-1;if(p->getMark("&qhfireshuangxiong_red-Clear")==1)color=int(Card::Red);else if(p->getMark("&qhfireshuangxiong_black-Clear")==1)color=int(Card::Black);
  return name==(int(c->getColor())==color?"slash":"duel");
 }
 // Fixed source2931–2969 returns an ordinary faced parse. This is that
 // readonly policy object only; it does not rename a Guhuo native receipt.
 static const QStringList names{"ex_nihilo","snatch","peach","slash","archery_attack","savage_assault","dismantlement","fire_attack"};
 return p->getMark("qhwindguhuoNum")<=3&&names.contains(name)&&!p->property("qhwindguhuoused").toString().split('+').contains(name);
}

// ---- qhstandard-qixi ----
bool aiQhstandardQixiConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()||!r.decisionId
  ||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY||!r.pattern.isEmpty()
  ||!aiRangeRequestCurrent(room,r,p))return false;
 const QSet<QString>fields{"source_key","skill","name","suit","number","subcards"};
 if(s.size()!=fields.size())return false;for(auto i=s.begin();i!=s.end();++i)if(!fields.contains(i.key()))return false;
 const QString key=s.value("source_key").toString(),name=s.value("name").toString();
 if(key!="qhstandardqixi"||name!="dismantlement"||s.value("skill")!=QJsonValue(key)
  ||!s.value("name").isString()||!s.value("suit").isDouble()||!s.value("number").isDouble()
  ||!s.value("subcards").isArray()||s.value("subcards").toArray().size()!=1)return false;
 bool owned=false;for(const auto&i:p->getSkillInstances())if(i.skillName==key)owned=true;if(!owned)return false;
 const auto v=s.value("subcards").toArray().first();const double n=v.toDouble(-1);
 if(!v.isDouble()||!std::isfinite(n)||n<0||n!=std::floor(n)||n>std::numeric_limits<int>::max())return false;
 const int id=int(n);if(room.getCardOwner(id)!=p)return false;
 const Card*c=nullptr;const AICardView*face=nullptr;
 if(room.getCardPlace(id)==Player::PlaceHand&&p->handCards().contains(id)){
  c=Sanguosha->getCard(id);for(const auto&row:r.worldView.handCards)if(row.effectiveId==id){face=&row;break;}
 }else if(room.getCardPlace(id)==Player::PlaceEquip){
  for(const Card*e:p->getEquips())if(e->getEffectiveId()==id){c=e;break;}
  for(const auto&row:r.worldView.self.equips)if(row.effectiveId==id){face=&row;break;}
 }
 if(!c||!face||face->objectName!=c->objectName()||face->className!=c->getClassName()
  ||face->suit!=int(c->getSuit())||face->number!=c->getNumber()||face->legacyIdentityToken.isEmpty()
  ||face->legacyIdentityToken!=aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c)
  ||s.value("suit")!=QJsonValue(int(Card::NoSuit))||s.value("number")!=QJsonValue(0))return false;
 // The source constructs for EVERY HE card before the black short circuit.
 // Source NoSuit/0 is a constructor tuple, not its inherited material face.
 // This capability evaluates only; selected native skill/cost gates are unchanged.
 return true;
}

// ---- qhstandard-shuangxiong ----
bool aiQhfireShuangxiongConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s)
{
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||!r.decisionId||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY||!r.pattern.isEmpty()
  ||!aiRangeRequestCurrent(room,r,p))return false;
 const QSet<QString>fields{"source_key","skill","name","suit","number","subcards"};
 if(s.size()!=fields.size())return false;for(auto i=s.begin();i!=s.end();++i)if(!fields.contains(i.key()))return false;
 if(s.value("source_key")!=QJsonValue("qhfireshuangxiong")||s.value("skill")!=QJsonValue(QString())
  ||!(s.value("name")==QJsonValue("duel")||s.value("name")==QJsonValue("slash"))||!s.value("suit").isDouble()
  ||!s.value("number").isDouble()||!s.value("subcards").isArray()||!s.value("subcards").toArray().isEmpty())return false;
 // Fixed qhstandard-ai.lua3100–3147: an unskilled, cost-free Duel
 // or Slash carries the exact printed suit/number of an actual own hand card.
 // Presence attests this source; selected provider eligibility stays separate.
 bool owned=false;for(const auto&i:p->getSkillInstances())if(i.skillName=="qhfireshuangxiong")owned=true;
 if(!owned||p->getMark("&qhfireshuangxiong_damage-Clear")==2)return false;
 int sourceColor=-1;
 if(p->getMark("&qhfireshuangxiong_red-Clear")==1)sourceColor=int(Card::Red);
 else if(p->getMark("&qhfireshuangxiong_black-Clear")==1)sourceColor=int(Card::Black);
 const auto exact=[&](const AICardView&v,const Card*c){
  if(!c||c->getEffectiveId()<0||s.value("suit")!=QJsonValue(int(c->getSuit()))||s.value("number")!=QJsonValue(c->getNumber()))return false;
  const QString sourceName=int(c->getColor())==sourceColor?QStringLiteral("slash"):QStringLiteral("duel");
  if(s.value("name")!=QJsonValue(sourceName))return false;
  const int id=c->getEffectiveId();return room.getCardOwner(id)==p&&v.effectiveId==id
   &&v.objectName==c->objectName()&&v.className==c->getClassName()&&v.suit==int(c->getSuit())&&v.number==c->getNumber()
   &&!v.legacyIdentityToken.isEmpty()&&v.legacyIdentityToken==aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c);
 };
 for(int id:p->handCards())if(room.getCardPlace(id)==Player::PlaceHand)
  for(const auto&face:r.worldView.handCards)if(face.effectiveId==id&&exact(face,Sanguosha->getCard(id)))return true;
 return false;
}

// ---- qhstandard-xinhuo ----
void projectAIQhfireXinhuo(AIRequest &request,Room &room,ServerPlayer *viewer)
{
    auto family=request.worldView.strategyFacts.value("qhstandard").toObject();
    family.remove("xinhuo_suits");
    const auto publish=[&]{if(family.isEmpty())request.worldView.strategyFacts.remove("qhstandard");else request.worldView.strategyFacts.insert("qhstandard",family);};
    publish();
    if(!viewer||viewer->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
        ||!room.getPlayers().contains(viewer)||!request.decisionId
        ||request.viewerObjectName!=viewer->objectName()||request.worldView.self.objectName!=viewer->objectName()
        ||request.stateRevision!=room.roomRuntime()->stateRevision()
        ||request.kind!=AIRequest::RespondCard||request.choiceOptions.question!="askForCardShow"
        ||request.choiceOptions.reason!="qhfirexinhuo")return;
    // Exact fixed source ai/qhstandard-ai.lua:2286 and native writer5481–5532.
    // This is the answering owner's current tag, including absent -> empty.
    // Do not inspect a requester/foreign tag or pre-sample the later hand branch.
    family.insert("xinhuo_suits",viewer->getTag("qhfirexinhuo").toString());
    publish();
}

// ---- fatewangzhe ----
#include "ai-runtime.h"
#include "room-state.h"

class AIFateInquiryState final {
public:
    QPointer<Room> room;
    QPointer<ServerPlayer> viewer;
    QString pattern;
    bool active=true;
    quint64 entryEpoch=0;
};
thread_local std::shared_ptr<AIFateInquiryState> AIFateInquiry::s_current;
AIFateInquiry::AIFateInquiry(Room &room,ServerPlayer *viewer,const QString &pattern)
    :m_state(std::make_shared<AIFateInquiryState>()),m_previous(s_current)
{
    // Entering any nested inquiry permanently retires earlier selections from
    // the suspended frame; restoring the frame cannot revive an old receipt.
    if(m_previous) ++m_previous->entryEpoch;
    m_state->room=&room;m_state->viewer=viewer;m_state->pattern=pattern;s_current=m_state;
}
AIFateInquiry::~AIFateInquiry(){m_state->active=false;s_current=m_previous;}
class AIFateUseProof final {
public:
    std::shared_ptr<AIFateInquiryState> inquiry;
    AIRequest request;
    AICardConversionView ticket;
    QPointer<const Card> submitted;
    QString submittedWire,submittedRawSkill;
    quint64 gameGeneration=0,aiGeneration=0,inquiryEpoch=0;
    mutable bool consumed=false;
};
namespace {
bool shape(const Card *card,const AICardConversionView &ticket)
{
    const auto *skillCard=card?dynamic_cast<const SkillCard *>(card):nullptr;
    return skillCard && card->isVirtualCard() && card->getTypeId()==Card::TypeSkill
        && card->objectName()==ticket.name && card->getClassName()==ticket.className
        && int(card->getSuit())==ticket.suit && card->getNumber()==ticket.number
        && card->getSubcards()==ticket.subcardIds && skillCard->getUserString()==ticket.declaredName;
}
bool requestShape(const ActiveSkillRequest &r,const AIFateUseProof &p)
{
    return r.initiator==p.inquiry->viewer && r.reason==p.request.reason && r.pattern==p.request.pattern
        && r.activationRef==p.ticket.activationRef && r.sourceRef==p.ticket.sourceRef
        && r.selectedCardIds==p.ticket.subcardIds && r.selectedTargetNames.isEmpty()
        && r.userString==p.ticket.declaredName;
}
}
bool AIFateUseAccess::current(const Room &room,const std::shared_ptr<const AIFateUseProof> &p)
{
    Room &mutableRoom=const_cast<Room &>(room);
    const auto stable=[&]() {
        return p && !p->consumed && p->inquiry && p->inquiry->active
            && p->inquiry==AIFateInquiry::s_current
            && p->inquiry->entryEpoch==p->inquiryEpoch
            && p->inquiry->room==&room && p->inquiry->viewer && p->inquiry->pattern=="@fatewangzhe"
            && mutableRoom.roomRuntime() && !mutableRoom.roomRuntime()->isClosing()
            && mutableRoom.roomRuntime()->stateRevision()==p->request.stateRevision
            && mutableRoom.roomRuntime()->lua().generation()==p->gameGeneration
            && mutableRoom.roomRuntime()->ai().lua().generation()==p->aiGeneration
            && !p->submitted.isNull() && shape(p->submitted,p->ticket)
            && p->submitted->toString()==p->submittedWire
            && p->submitted->getSkillName(false)==p->submittedRawSkill
            && p->submitted->getActivationSkillName()==p->ticket.activationRef.key.skillName
            && p->submitted->getActivationSkillInstanceId()==p->ticket.activationRef.key.instanceID
            && p->submitted->getSourceSkillName()==p->ticket.sourceRef.key.skillName
            && p->submitted->getSourceSkillInstanceId()==p->ticket.sourceRef.key.instanceID
            && mutableRoom.roomState()->getCurrentCardUseReason()==p->request.reason
            && mutableRoom.roomState()->getCurrentCardUsePattern()==p->request.pattern;
    };
    if(!stable())return false;
    const bool authorized=authorizeAIStrategyPreviewPayment(p->request,mutableRoom,p->inquiry->viewer,
        p->ticket.activationRef,p->ticket.sourceRef,p->ticket.subcardIds);
    // The authority's native invalidity/root queries can enter Lua. Never rely
    // on the pre-call stamp, weak object pointer or enclosing inquiry afterward.
    return authorized && stable();
}
bool AIFateUseAccess::attach(const AIRequest &r,Room &room,ServerPlayer *viewer,
    const AICardConversionView &ticket,CardUseStruct &use)
{
    if(r.kind!=AIRequest::UseCard || r.pattern!="@fatewangzhe"
        || r.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE || r.handlingMethod!=Card::MethodUse
        || ticket.activationRef.key.skillName!="fatewangzhe" || ticket.producerVersion!=2
        || ticket.producerReason!=r.reason || ticket.producerPattern!=r.pattern
        || ticket.subcardIds.size()!=1 || !ticket.declaredName.isEmpty()
        || use.from!=viewer || !use.to.isEmpty() || !use.hasSkillActivationRequest
        || use.activationRef!=ticket.activationRef || use.sourceRef!=ticket.sourceRef
        || use.bypass_cost || use.skipSkillEffect || use.skillExecutionID!=0 || !shape(use.card,ticket)
        || !AIFateInquiry::s_current || AIFateInquiry::s_current->viewer!=viewer)
        return false;
    auto p=std::make_shared<AIFateUseProof>();
    p->inquiry=AIFateInquiry::s_current;p->inquiryEpoch=p->inquiry->entryEpoch;p->request=r;p->ticket=ticket;p->submitted=use.card;
    p->submittedWire=use.card->toString();p->submittedRawSkill=use.card->getSkillName(false);
    p->gameGeneration=room.roomRuntime()->lua().generation();p->aiGeneration=room.roomRuntime()->ai().lua().generation();
    if(!current(room,p))return false;
    use.m_aiFateUse=p;return true;
}
bool AIFateUseAccess::bind(Room &room,const CardUseStruct &use,ActiveSkillRequest &r)
{
    const auto p=use.m_aiFateUse;if(!p)return true;
    if(!current(room,p) || use.card!=p->submitted || use.from!=p->inquiry->viewer
        || !use.to.isEmpty() || !use.hasSkillActivationRequest || use.bypass_cost || use.skipSkillEffect
        || use.skillExecutionID!=0 || use.activationRef!=p->ticket.activationRef || use.sourceRef!=p->ticket.sourceRef
        || !requestShape(r,*p))return false;
    r.m_aiFateUse=p;return true;
}
bool AIFateUseAccess::extend(const Room &room,const ServerPlayer *viewer,const ViewAsSkillV2 *skill,
    const ActiveSkillRequest &r,QSet<int> &selectable)
{
    const auto p=r.m_aiFateUse;if(!p)return true;
    if(!current(room,p) || viewer!=p->inquiry->viewer || !skill
        || skill!=Sanguosha->getViewAsSkill("fatewangzhe") || !requestShape(r,*p))return false;
    selectable.insert(p->ticket.subcardIds.first());return true;
}
bool AIFateUseAccess::finish(const Room &room,const ActiveSkillRequest &r,const Card *card)
{
    const auto p=r.m_aiFateUse;if(!p)return true;
    if(!current(room,p) || !requestShape(r,*p) || !shape(card,p->ticket)
        || card->getActivationSkillName()!=p->ticket.activationRef.key.skillName
        || card->getActivationSkillInstanceId()!=p->ticket.activationRef.key.instanceID
        || card->getSourceSkillName()!=p->ticket.sourceRef.key.skillName
        || card->getSourceSkillInstanceId()!=p->ticket.sourceRef.key.instanceID)return false;
    p->consumed=true;return true;
}

// ---- package-tag-projections-planning-2 ----
void projectAIChendancuiPlanning(AIRequest&r,Room&room,const ServerPlayer*p){
 auto facts=r.worldView.strategyFacts.value("shixinrumo_yi").toObject();facts.remove("chendancuiData");
 r.worldView.strategyFacts.insert("shixinrumo_yi",facts);
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.stateRevision!=room.roomRuntime()->stateRevision()||r.choiceOptions.reason!="chendancui")return;
 if(!((r.kind==AIRequest::SkillInvoke&&r.choiceOptions.question=="askForSkillInvoke")
  ||(r.kind==AIRequest::Discard&&r.choiceOptions.question=="askForDiscard")))return;
 // The fixed compulsory branch intentionally DOES NOT overwrite this owner
 // tag. Preserve its stored target, not the current event/prompt target.
 // QVariant::toDamage is value<DamageStruct>(); absent/wrong type yields a
 // default structure with null to. No card/from/amount/nature is exposed.
 const auto d=p->getTag("chendancuiData").value<DamageStruct>();QJsonObject row;
 if(d.to){if(!room.getAllPlayers(true).contains(d.to))return;row.insert("to",d.to->objectName());}
 facts.insert("chendancuiData",row);r.worldView.strategyFacts.insert("shixinrumo_yi",facts);
}

// ---- package-tag-projections-planning-3 ----
void projectAIKesxchunlaoPlanning(AIRequest&r,Room&room,const ServerPlayer*p){
 auto facts=r.worldView.strategyFacts.value("kearmsxfyli").toObject();
 facts.remove("kesxchunlaoToGetCount");
 r.worldView.strategyFacts.insert("kearmsxfyli",facts);
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.stateRevision!=room.roomRuntime()->stateRevision()
  ||r.kind!=AIRequest::PlayerChosen||r.choiceOptions.reason!="kesxchunlao"
  ||r.choiceOptions.question!="askForPlayerChosen")return;
 // Fixed owner tag records its own discarded IDs, including duplicates. Source
 // only observes IntList.length(); do not expose IDs, moved faces or other tags.
 // QVariant::toIntList uses value<QList<int>>: absent/wrong type means empty.
 facts.insert("kesxchunlaoToGetCount",p->getTag("kesxchunlaoToGet").value<QList<int>>().size());
 r.worldView.strategyFacts.insert("kearmsxfyli",facts);
}

// ---- package-tag-projections-planning-4 ----
void projectAILuncePlanning(AIRequest&r,Room&room,const ServerPlayer*p){
 auto facts=r.worldView.strategyFacts.value("hunlie").toObject();
 facts.remove("st_AI");facts.remove("lunce_condition_AI");
 r.worldView.strategyFacts.insert("hunlie",facts);
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.stateRevision!=room.roomRuntime()->stateRevision()
  ||r.kind!=AIRequest::Choice||r.choiceOptions.question!="askForChoice")return;
 // Fixed sgkgodlunce writes each own selected string immediately before this
 // respective question. Read exactly that current stored string, not a cached
 // previous AI choice, inferred prompt or a foreign recipient's strategy tag.
 const QString key=r.choiceOptions.reason=="lunce_condition"?QStringLiteral("st_AI")
  :r.choiceOptions.reason=="lunce_effect"?QStringLiteral("lunce_condition_AI"):QString();
 if(key.isEmpty())return;
 facts.insert(key,p->getTag(key).toString());
 r.worldView.strategyFacts.insert("hunlie",facts);
}

// ---- package-tag-projections-planning-5 ----
void projectAIBackpackShizongPlanning(AIRequest&r,Room&room,const ServerPlayer*p){
 auto gaoda=r.worldView.strategyFacts.value("gaoda").toObject();gaoda.remove("G_SELF_HT_skill");
 auto tenth=r.worldView.strategyFacts.value("sgs10th").toObject();tenth.remove("ny_tenth_shizong_from");
 r.worldView.strategyFacts.insert("gaoda",gaoda);r.worldView.strategyFacts.insert("sgs10th",tenth);
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.stateRevision!=room.roomRuntime()->stateRevision())return;
 const bool backpack=r.kind==AIRequest::SkillInvoke&&r.choiceOptions.question=="askForSkillInvoke"
  &&r.choiceOptions.reason=="#G_SELF_HT_skill";
 const bool exchange=r.kind==AIRequest::Discard&&r.choiceOptions.question=="askForDiscard"
  &&r.choiceOptions.reason=="ny_tenth_shizong";
 if(!backpack&&!exchange)return;
 const QString key=backpack?QStringLiteral("G_SELF_HT_skill"):QStringLiteral("ny_tenth_shizong_from");
 auto*target=p->getTag(key).value<ServerPlayer*>();
 // Both fixed producers assign a nonnull existing source/target immediately
 // before their question. A null or non-roster pointer is outside that proved
 // writer domain: leave unknown, never fabricate a target or relation false.
 if(!target||!room.getAllPlayers(true).contains(target))return;
 if(backpack){gaoda.insert(key,target->objectName());r.worldView.strategyFacts.insert("gaoda",gaoda);}
 else{tenth.insert(key,target->objectName());r.worldView.strategyFacts.insert("sgs10th",tenth);}
}

// ---- package-tag-projections-planning-6 ----
void projectAIQiangwuPlanning(AIRequest&r,Room&room,const ServerPlayer*p){
 auto package=r.worldView.strategyFacts.value("gaoda").toObject();
 auto props=package.value("owner_properties").toObject();props.remove("luaqiangwucolor");
 package.insert("owner_properties",props);r.worldView.strategyFacts.insert("gaoda",package);
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||!r.decisionId||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.stateRevision!=room.roomRuntime()->stateRevision()||r.kind!=AIRequest::RespondCard
  ||r.choiceOptions.question!="askForCard"||r.pattern!="..!"||r.prompt!="@luaqiangwu"
  ||r.handlingMethod!=Card::MethodDiscard)return;
 // Fixed Gaoda owner property, cleared at TurnStart and written after own Shoot.
 // The callback reads only toString: absent QVariant is the actual empty string.
 // No current skill/mark/color filter is added before this source read.
 props.insert("luaqiangwucolor",p->property("luaqiangwucolor").toString());
 package.insert("owner_properties",props);r.worldView.strategyFacts.insert("gaoda",package);
}

// ---- package-tag-projections-planning ----
namespace {
bool packageTagProjectionsPlanningSame(const AICardView&v,const Card*c,Room&room,const ServerPlayer*p,int id){
 return c&&v.effectiveId==id&&v.objectName==c->objectName()&&v.className==c->getClassName()
  &&v.suit==int(c->getSuit())&&v.number==c->getNumber()&&!v.legacyIdentityToken.isEmpty()
  &&v.legacyIdentityToken==aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c);
}
bool packageTagProjectionsPlanningDisclosed(const AIWorldView&w,Room&room,const ServerPlayer*p,int id){
 const Card*c=Sanguosha->getCard(id);if(!c)return false;
 for(const auto&v:w.handCards)if(packageTagProjectionsPlanningSame(v,c,room,p,id))return true;
 for(const auto&v:w.discardPile)if(packageTagProjectionsPlanningSame(v,c,room,p,id))return true;
 auto players=w.players;players.prepend(w.self);
 for(const auto&v:players){
  for(const auto&k:v.knownCards)if(packageTagProjectionsPlanningSame(k,c,room,p,id))return true;
  for(const auto&k:v.equips)if(packageTagProjectionsPlanningSame(k,c,room,p,id))return true;
  for(const auto&k:v.judgingArea)if(packageTagProjectionsPlanningSame(k,c,room,p,id))return true;
  for(const auto&pile:v.piles)if(v.objectName==p->objectName()||pile.open)
   for(const auto&k:pile.cards)if(packageTagProjectionsPlanningSame(k,c,room,p,id))return true;
 }
 return false;
}
}
void projectAIYikuxinChenlanjiaoPlanning(AIRequest&r,Room&room,const ServerPlayer*p){
 auto cleared=r.worldView.strategyFacts.value("shixinrumo_yi").toObject();
 cleared.remove("yikuxinIds");cleared.remove("chenlanjiaoIds");
 r.worldView.strategyFacts.insert("shixinrumo_yi",cleared);
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.stateRevision!=room.roomRuntime()->stateRevision())return;
 QString key;
 if(r.kind==AIRequest::PlayerChosen&&r.choiceOptions.question=="askForPlayerChosen"&&r.choiceOptions.reason=="yikuxin")key="yikuxinIds";
 else if(r.kind==AIRequest::SkillInvoke&&r.choiceOptions.question=="askForSkillInvoke"&&r.choiceOptions.reason=="chenlanjiao")key="chenlanjiaoIds";
 else return;
 // Fixed QVariant::toIntList absent/wrong-type result is the known empty list.
 // A nonempty list is atomic: no partial foreign-ID projection is permitted.
 const auto ids=p->getTag(key).value<QList<int>>();QJsonArray out;
 for(int id:ids){
  if(id<0)return;
  if(key=="yikuxinIds") {if(!packageTagProjectionsPlanningDisclosed(r.worldView,room,p,id))return;}
  else {
   // Native chenlanjiao asks the recipient, not the skill owner. No hasSkill
   // gate. Its source only tests membership in the recipient's current HE.
   bool owned=false;for(const Card*c:p->getCards("he"))if(c->getEffectiveId()==id)owned=true;
   if(!owned||room.getCardOwner(id)!=p)return;
  }
  out.append(id); // Preserve source order and duplicates, including empty.
 }
 auto facts=r.worldView.strategyFacts.value("shixinrumo_yi").toObject();facts.insert(key,out);
 r.worldView.strategyFacts.insert("shixinrumo_yi",facts);
}

// ---- package-tag-projections-yitian ----
#include "general.h"
void projectAIYitianChoices(AIRequest &q,Room &r,ServerPlayer *p){
 q.choiceOptions.context.remove("guixin_modify_target");q.worldView.decisionContext.remove("guixin_modify_target");
 auto y=q.worldView.strategyFacts.value("yitian").toObject();y.remove("general_is_male");
 const auto publish=[&]{if(y.isEmpty())q.worldView.strategyFacts.remove("yitian");else q.worldView.strategyFacts.insert("yitian",y);};publish();
 if(!p||p->getRoom()!=&r||!r.roomRuntime()||r.roomRuntime()->isClosing()||!r.getPlayers().contains(p)
  ||!q.decisionId||q.viewerObjectName!=p->objectName()||q.worldView.self.objectName!=p->objectName()
  ||q.stateRevision!=r.roomRuntime()->stateRevision()||q.kind!=AIRequest::Choice
  ||q.choiceOptions.question!="askForChoice")return;
 if(q.choiceOptions.reason=="weiwudi_guixin"&&q.choiceOptions.choices.join('+')=="wei+shu+wu+qun"){
  // Native yitian.cpp382–397 retains the publicly selected target only during
  // this kingdom question; null QVariant::toPlayer remains source nil.
  const auto *target=r.getTag("Guixin2Modify").value<ServerPlayer*>();
  QJsonValue value(false);
  if(target){if(!r.getAllPlayers(true).contains(const_cast<ServerPlayer*>(target)))return;value=target->objectName();}
  q.choiceOptions.context.insert("guixin_modify_target",value);q.worldView.decisionContext.insert("guixin_modify_target",value);
 }
 if(q.choiceOptions.reason=="shenjun"&&q.choiceOptions.choices.join('+')=="male+female"){
  QJsonObject genders;
  const auto project=[&](const AIPlayerView&v){
   ServerPlayer *target=nullptr;for(auto *a:r.getAllPlayers(true))if(a->objectName()==v.objectName){target=a;break;}
   if(!target||v.generalName.isEmpty()||(target!=p&&!target->hasShownGeneral()))return;
   const auto *g=target->getGeneral();if(!g||g->objectName()!=v.generalName)return;
   genders.insert(v.objectName,g->isMale());
  };
  project(q.worldView.self);for(const auto&v:q.worldView.players)project(v);
  y.insert("general_is_male",genders);publish();
 }
}

// ---- rushb-danlve ----
#include "ai-strategy/ol-ai.h"
#include "ai-strategy/overseas.h"
namespace {
bool danlveSkill(const QString&s){return s=="mk_danlve_yinyue"||s=="mk_danlve_qinggang";}
QJsonObject physicalLayer(const Card*c){
 QJsonArray costs;for(int id:c->getSubcards())costs.append(id);
 return {{"id",c->getId()},{"effective_id",c->getEffectiveId()},{"virtual",c->isVirtualCard()},
  {"name",c->objectName()},{"class",c->getClassName()},{"suit",int(c->getSuit())},{"number",c->getNumber()},
  {"raw_skill",c->rawSkillName()},{"skill_instance",c->getSkillInstanceId()},
  {"activation",c->getActivationSkillName()},{"activation_id",c->getActivationSkillInstanceId()},
  {"source",c->getSourceSkillName()},{"source_id",c->getSourceSkillInstanceId()},
  {"subcards",costs},{"flags",QJsonArray::fromStringList(c->getFlags())},
  {"skill_names",QJsonArray::fromStringList(c->getSkillNames(false))},
  {"can_recast",c->canRecast()},{"handling",int(c->getHandlingMethod())}};
}
QJsonObject physicalStamp(const AIRequest&r,Room&room,ServerPlayer*p,const Card*c,const QString&skill){
 return {{"decision",QString::number(r.decisionId)},{"revision",QString::number(r.stateRevision)},
  {"viewer",r.viewerObjectName},{"kind",int(r.kind)},{"reason",int(r.reason)},{"pattern",r.pattern},
  {"skill",skill},{"identity",aiLegacyCardIdentity(room,p,c)},
  {"inner_identity",aiLegacyCardIdentity(room,p,c->getRealCard())},
  {"outer_state",physicalLayer(c)},{"inner_state",physicalLayer(c->getRealCard())},
  {"name",c->objectName()},{"class",c->getClassName()},{"suit",int(c->getSuit())},{"number",c->getNumber()},
  {"activation",c->getActivationSkillName()},{"activation_id",c->getActivationSkillInstanceId()},
  {"source",c->getSourceSkillName()},{"source_id",c->getSourceSkillInstanceId()}};
}
const Card* currentDanlveWrite(Room&room,const AIRequest&r,ServerPlayer*p,int id){
 if(!p||p->getRoom()!=&room||!r.conversionLedger||r.viewerObjectName!=p->objectName()
  ||r.worldView.self.objectName!=p->objectName()||!aiRangeRequestCurrent(room,r,p))return nullptr;
 const auto record=r.conversionLedger->danlvePhysicalWrites.value(id);
 const QString skill=record.value("skill").toString();
 if(!danlveSkill(skill)||!p->hasSkill(skill)||room.getCardOwner(id)!=p
  ||room.getCardPlace(id)!=Player::PlaceSpecial||!p->getPile(skill).contains(id))return nullptr;
 const Card*c=Sanguosha->getCard(id);
 if(!c||c->isVirtualCard()||c->getEffectiveId()!=id||c->getSkillName(false)!=skill
  ||c->getSubcards()!=QList<int>{id}||record!=physicalStamp(r,room,p,c,skill))return nullptr;
 return c;
}
}
bool authorizeAIDanlvePhysical(Room&room,const AIRequest&r,ServerPlayer*p,int id){
 if(r.worldView.strategyFacts.contains("readonly_play_context"))return false;
 if(r.kind!=AIRequest::Activate&&r.kind!=AIRequest::UseCard&&r.kind!=AIRequest::RespondCard)return false;
 if(r.kind==AIRequest::RespondCard){
  const auto q=r.choiceOptions.question;
  if(q!="askForCard"&&q!="askForNullification"&&q!="askForSinglePeach")return false;
 }
 return currentDanlveWrite(room,r,p,id)!=nullptr;
}
const Card* attachAIDanlvePhysical(Room&room,const AIRequest&r,ServerPlayer*p,
 const QJsonObject&scope,const QString&registry,const QString&hook,int id,QString*error){
 const auto fail=[&](const char*s)->const Card*{if(error)*error=QString::fromLatin1(s);return nullptr;};
 const Card*physical=currentDanlveWrite(room,r,p,id);
 const QString skill=scope.value("skill_name").toString();
 if(!physical||registry!="ai_cardsview"||!danlveSkill(skill)||hook!=skill
  ||physical->getSkillName(false)!=skill||scope.value("version").toInt()!=2
  ||!scope.value("has_action").toBool()||scope.value("play").toBool()||scope.value("skill_action").toBool())
  return fail("Danlve physical cardsView attachment lacks its reached native write/source");
 bool issued=false;const auto scopes=r.conversionPolicy.value("scopes").toObject();
 for(auto i=scopes.begin();i!=scopes.end();++i)if(i.value().toObject()==scope){issued=true;break;}
 const auto v=scope.value("identity").toObject();
 const SkillInstanceRef activation(v.value("activation_owner").toString(),SkillInstanceKey(v.value("activation_skill").toString(),v.value("activation_instance").toInt(-1)));
 const SkillInstanceRef source(v.value("source_owner").toString(),SkillInstanceKey(v.value("source_skill").toString(),v.value("source_instance").toInt(-1)));
 if(!issued||!activation.isValid()||!source.isValid()||activation.ownerObjectName!=p->objectName()
  ||activation.key.skillName!=skill||!p->hasSkillInstance(skill,activation.key.instanceID)
  ||room.resolveSkillInstanceRootRef(activation)!=source
  ||!dynamic_cast<const ViewAsSkillV2*>(Sanguosha->getViewAsSkill(skill)))return fail("Danlve issued attachment identity changed");
 // Fixed SmartAI::insertCardsViewResult writes all three on the same Card.
 Card*c=const_cast<Card*>(physical);c->setSkillName(activation.key.skillName);
 c->setActivationSkill(activation.key.skillName,activation.key.instanceID);
 c->setSourceSkill(source.key.skillName,source.key.instanceID);
 r.conversionLedger->danlvePhysicalWrites[id]=physicalStamp(r,room,p,c,skill);
 return c;
}

const Card *writeAIRushBPhysicalSkill(Room &room,const AIRequest &r,ServerPlayer *p,const QJsonObject &s,QString *error) {
 const auto fail=[&](const char *why)->const Card*{if(error)*error=QString::fromLatin1(why);return nullptr;};
 static const QSet<QString> keys{"kind","skill","id","identity","name","class_name","suit","number","previous_skill"};
 if(s.size()!=keys.size())return fail("RushB mutation shape differs");
 for(auto i=s.begin();i!=s.end();++i)if(!keys.contains(i.key()))return fail("RushB mutation extra field");
 if((s.value("kind")!="rushb_physical_skill"&&s.value("kind")!="rushb_physical_skill_read")||!p||p->getRoom()!=&room||!r.decisionId||!r.conversionLedger
    ||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
    ||!aiRangeRequestCurrent(room,r,p))return fail("RushB mutation requires current owner request");
 const bool ordinary=r.kind==AIRequest::Activate||r.kind==AIRequest::UseCard
    ||r.kind==AIRequest::RespondCard;
 const bool nested=r.worldView.strategyFacts.contains("readonly_play_context")
    &&(aiOlClanPlanningQuestion(r,room,p)||aiXiafengChoicePlanningQuestion(r,room,p)||aiNiwoShelieChoicePlanningQuestion(r,room,p));
 if(!ordinary&&!nested)return fail("RushB mutation inquiry is outside planning/response family");
 // Guhuo/cardsview may inspect another class while planning this inquiry; its
 // requested class is not a payment pattern and must not be used as one.
 const QString skill=s.value("skill").toString();
 const bool he=skill=="diy_m_jichou"||skill=="diy_m_jilun";
 if(!he&&skill!="mk_danlve_yinyue"&&skill!="mk_danlve_qinggang")return fail("RushB mutation skill is outside fixed family");
 if(!p->hasSkill(skill))return fail("RushB mutation source skill absent");
 const double number=s.value("id").toDouble(-1);
 if(!s.value("id").isDouble()||!std::isfinite(number)||number<0||number!=std::floor(number)||number>std::numeric_limits<int>::max())return fail("RushB physical ID invalid");
 const int id=int(number);Card *card=Sanguosha->getCard(id);
 if(!card||card->isVirtualCard()||card->getEffectiveId()!=id||room.getCardOwner(id)!=p)return fail("RushB physical object no longer owned");
 const auto place=room.getCardPlace(id);
 if(he){if(place!=Player::PlaceHand&&place!=Player::PlaceEquip)return fail("RushB source requires owner HE");if(!card->isNDTrick())return fail("RushB source requires NDTrick");}
 else if(place!=Player::PlaceSpecial||!p->getPile(skill).contains(id))return fail("RushB source requires its actual owner pile");
 const AICardView *face=nullptr;
 const auto find=[&](const QList<AICardView>&cards){for(const auto &v:cards)if(v.effectiveId==id){face=&v;return;}};
 find(r.worldView.handCards);if(!face)find(r.worldView.self.equips);
 if(!face)for(const auto &pile:r.worldView.self.piles)if(pile.name==skill)find(pile.cards);
 const QString token=aiLegacyCardIdentity(room,p,card);
 if(!face||face->legacyIdentityToken.isEmpty()||face->legacyIdentityToken!=token
    ||face->objectName!=card->objectName()||face->className!=card->getClassName()
    ||face->suit!=int(card->getSuit())||face->number!=card->getNumber())return fail("RushB current lawful face/identity differs");
 if(s.value("identity")!=token||s.value("name")!=card->objectName()||s.value("class_name")!=card->getClassName()
    ||s.value("suit")!=int(card->getSuit())||s.value("number")!=card->getNumber()
    ||!s.value("previous_skill").isString()||s.value("previous_skill")!=card->getSkillName(false))return fail("RushB mutation input is stale or forged");
 // Fixed Card::setSkillName is virtual. The generated SWIG wrapper calls
 // (arg1)->setSkillName, so WrappedCard's override updates wrapper AND inner.
 // Preserve that same-object dispatch; do not qualify away the source effect.
 // No extra revision, payment, cleanup or rollback is introduced.
 if(s.value("kind")=="rushb_physical_skill"){
  card->setSkillName(skill);
  if(danlveSkill(skill))r.conversionLedger->danlvePhysicalWrites[id]=physicalStamp(r,room,p,card,skill);
 }
 else if(card->getSkillName(false)!=skill)return fail("RushB physical refresh skill differs");
 return card;
}

// ---- gaoda-skin ----
namespace {
bool current(const AIRequest &r,const ServerPlayer *p) {
    return p && p->isAlive() && p->getRoom() && r.decisionId
        && r.viewerObjectName==p->objectName() && r.worldView.self.objectName==p->objectName()
        && r.stateRevision==p->getRoom()->roomRuntime()->stateRevision()
        && r.kind==AIRequest::Activate && r.reason==CardUseStruct::CARD_USE_REASON_PLAY
        && p->getPhase()==Player::Play && p->hasSkill("skin");
}
bool actualSource(const AIRequest &r,const ServerPlayer *p,const SkillInstanceRef &a,const SkillInstanceRef &s) {
    if(!current(r,p) || !a.isValid() || !s.isValid() || a.ownerObjectName!=p->objectName()
        || a.key.skillName!="skin" || !p->hasSkillInstance(a.key.skillName,a.key.instanceID)
        || p->isSkillInvalid(a.key.skillName,a.key.instanceID))return false;
    QList<AiSkillActionContext> all=r.skillActions;
    if(r.hasSkillActionContext)all.prepend(r.skillActionContext);
    for(const auto &row:all)if(row.isValid()&&row.activationRef==a&&row.sourceRef==s)return true;
    return false;
}
}
// ai/gaoda-ai.lua126-143: source parses #skin:.:, never a declaration.
// Native scope admission is independent of source hasUsed/RNG branch order.
int aiSkinPrototype(const AIRequest &r,const ServerPlayer *p,const QString &skill,const QString &prototype) {
    if(skill!="skin"||prototype!="#skin"||!current(r,p))return -1;
    QList<AiSkillActionContext> all=r.skillActions;
    if(r.hasSkillActionContext)all.prepend(r.skillActionContext);
    for(const auto &a:all)if(actualSource(r,p,a.activationRef,a.sourceRef))return 0;
    return -1;
}
// gaoda.lua1496-1512: only the actual native V2 producer may generate the
// declaration. This function reads no persisted Skin data and exports none.
bool aiSkinProduct(const AIRequest &r,const ServerPlayer *p,const SkillInstanceRef &a,
                         const SkillInstanceRef &s,const Card *produced,const Card *policy) {
    const auto *actual=dynamic_cast<const SkillCard *>(produced);
    const auto *source=dynamic_cast<const SkillCard *>(policy);
    return actualSource(r,p,a,s) && actual && source && source->getUserString().isEmpty()
        && actual->objectName()=="skin" && source->objectName()=="skin"
        && actual->getClassName()==source->getClassName()
        && actual->isVirtualCard() && source->isVirtualCard()
        && actual->getSubcards().isEmpty() && source->getSubcards().isEmpty()
        && actual->getSuit()==source->getSuit() && actual->getNumber()==source->getNumber()
        && actual->getSkillName(false)==source->getSkillName(false)
        && actual->targetFixed()==source->targetFixed() && actual->willThrow()==source->willThrow()
        && actual->getHandlingMethod()==source->getHandlingMethod() && actual->canRecast()==source->canRecast()
        && actual->getFlags()==source->getFlags();
}

// ---- dmpkancolle-hayate ----
namespace {
bool owner(const AIRequest&r,const ServerPlayer*p){return p&&p->getRoom()&&r.viewerObjectName==p->objectName()&&r.stateRevision==p->getRoom()->roomRuntime()->stateRevision();}
}
bool aiDmpkancollePlanningConstruction(const AIRequest&r,const ServerPlayer*p,const QJsonObject&s,QString*error){
 auto no=[&](const char*e){if(error)*error=QString::fromLatin1(e);return false;};
 if(!owner(r,p)||r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY||!r.pattern.isEmpty())return no("Dmpkancolle readonly constructor needs current owner Play");
 const QSet<QString>fields{"source_key","name","skill","suit","number","subcards"};
 if(s.size()!=fields.size())return no("Dmpkancolle constructor fields differ");
 for(auto it=s.begin();it!=s.end();++it)if(!fields.contains(it.key()))return no("unaudited Dmpkancolle constructor field");
 if(!s.value("source_key").isString()||!s.value("name").isString()||!s.value("skill").isString()||!s.value("suit").isDouble()||!s.value("number").isDouble()||!s.value("subcards").isArray()||!s.value("subcards").toArray().isEmpty())return no("Dmpkancolle source preview carries no payment or inferred face");
 const auto key=s.value("source_key").toString();if(!p->hasSkill(key))return no("Dmpkancolle constructor source is not owned");
 if(key=="se_hongzha")return s.value("name")=="slash"&&s.value("skill")=="se_hongzha"&&s.value("suit").toDouble()==int(Card::NoSuit)&&s.value("number").toDouble()==0;
 if(key=="nuequ"&&s.value("name")=="fire_slash"&&s.value("skill").toString().isEmpty()){
  // Fixed AI751-758 reads the first sorted owner hand material's *current*
  // face, but the clone itself has NO subcards. Only actual owner hand faces
  // may witness this readonly constructor; selected payment is a later action.
  for(const Card*c:p->getHandcards())if(c&&s.value("suit").toDouble()==int(c->getSuit())&&s.value("number").toDouble()==c->getNumber())return true;
 }
 return no("Dmpkancolle constructor differs from the two fixed source recipes");
}
void projectAIDmpkancollePlanningContext(AIRequest&r,Room&room,const ServerPlayer*p,const QVariant&data,const std::function<QJsonObject(const Card*)>&face){
 projectAIHayateEnemyMetadata(r.worldView,room);
 if(!owner(r,p)||p->getRoom()!=&room||r.kind!=AIRequest::SkillInvoke||r.choiceOptions.reason!="se_huibi_jink"||data.metaType().id()!=QMetaType::QStringList)return;
 const auto values=data.toStringList();if(values.size()<4||values.at(2)!="use")return;
 // player-decision-service.cpp1213-1222 is the exact CardAsked list writer;
 // GameRule792 stores the publicly used card before asking its recipients.
 // No caller-controlled tag name and no history-wide enumeration is accepted.
 const QString index=values.at(3);const QVariant history=room.getTag("UseHistory"+index);
 QJsonObject row{{"known",true},{"history_index",index},{"present",false}};
 if(history.isValid()&&!history.isNull()){
  if(!history.canConvert<CardUseStruct>())return;
  const auto use=history.value<CardUseStruct>();
  if(use.card){
   if(use.card->toString()!=index||!use.to.contains(const_cast<ServerPlayer*>(p))||!face)return;
   if(use.from&&!room.getAllPlayers(true).contains(use.from))return;
   auto card=face(use.card);if(card.isEmpty())return;
   // This publicly declared rescue Slash flag is set by the native
   // NosJiefan provider (yjcm2012.cpp1899), not an arbitrary private flag.
   auto flags=card.value("known_flags").toObject();flags.insert("NosJiefanUsed",use.card->hasFlag("NosJiefanUsed"));card.insert("known_flags",flags);
   row.insert("present",true);row.insert("use",QJsonObject{{"card",card},{"from",use.from?QJsonValue(use.from->objectName()):QJsonValue(false)}});
  }
 }
 auto context=r.choiceOptions.context.value("dmpkancolle").toObject();context.insert("huibi_use",row);r.choiceOptions.context.insert("dmpkancolle",context);r.worldView.decisionContext.insert("dmpkancolle",context);
}

namespace {
class HayateGlobalSnapshot final:public QObject{
public:HayateGlobalSnapshot(Room&r,const QJsonObject&v):QObject(&r),value(v){}const QJsonObject value;
};
const HayateGlobalSnapshot*hayateCapture(Room&r){for(auto*c:r.children())if(auto*s=dynamic_cast<HayateGlobalSnapshot*>(c))return s;return nullptr;}
}
void captureAIHayateEnemyMetadata(Room&room){
 if(hayateCapture(room))return;
 auto&vm=room.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();QJsonObject row{{"known",false},{"known_nil",false}};
 if(!L){new HayateGlobalSnapshot(room,row);return;}
 const int top=lua_gettop(L);lua_pushglobaltable(L);const int globals=lua_absindex(L,-1);
 lua_pushliteral(L,"enemy");lua_rawget(L,globals);bool known=true;const bool absent=lua_isnil(L,-1);lua_pop(L,1);
 if(absent&&lua_getmetatable(L,globals)){lua_pushliteral(L,"__index");lua_rawget(L,-2);known=lua_isnil(L,-1);lua_pop(L,2);}
 // Source registration is checked without invoking strategy. Writer audit of
 // fixed engine/ext Lua corpus is recorded in planning-round25-global-audit:
 // no _ENV.enemy stores, dynamic loader/restore sites inspected separately.
 lua_pushliteral(L,"sgs");lua_rawget(L,globals);bool source=false;
 if(lua_istable(L,-1)){lua_pushliteral(L,"ai_skill_playerchosen");lua_rawget(L,-2);
  if(lua_istable(L,-1)){lua_pushliteral(L,"vector");lua_rawget(L,-2);
   if(lua_isfunction(L,-1)){lua_Debug info{};lua_pushvalue(L,-1);if(lua_getinfo(L,">S",&info)){
    const QString file=QString::fromUtf8(info.source?info.source:"");source=file.endsWith("/hayate-ai.lua")&&info.linedefined==727&&QString::fromLatin1(info.what?info.what:"")=="Lua";
    row.insert("source",file);row.insert("line",info.linedefined);
   }}
  }
 }
 row.insert("known",known&&source);row.insert("known_nil",known&&source&&absent);lua_settop(L,top);new HayateGlobalSnapshot(room,row);
}
void projectAIHayateEnemyMetadata(AIWorldView&world,Room&room){
 auto hayate=world.strategyFacts.value("hayate").toObject();const auto*c=hayateCapture(room);
 const auto row=c?c->value:QJsonObject{{"known",false},{"known_nil",false}};
 hayate.insert("legacy_enemy_global",row);hayate.insert("legacy_enemy_global_known_nil",row.value("known").toBool()&&row.value("known_nil").toBool());world.strategyFacts.insert("hayate",hayate);
}

// ---- animecard ----
#include "ai-strategy/native-registry.h"

namespace {
class ReijyuuWitness final : public QObject {
public:
    explicit ReijyuuWitness(Room &room) : QObject(&room) {
        setObjectName(QStringLiteral("isolated-reijyuu-public-choice-witness"));
    }
    QHash<QString, QString> targets;
};
ReijyuuWitness *witness(Room &room, bool create) {
    for (QObject *child : room.children())
        if (auto *state = dynamic_cast<ReijyuuWitness *>(child)) return state;
    return create ? new ReijyuuWitness(room) : nullptr;
}
bool inRoom(Room &room, const ServerPlayer *player) {
    return player && room.findPlayerByObjectName(player->objectName(), true) == player;
}
}
void witnessAIStrategyReijyuuChoice(Room &room, const ServerPlayer *viewer,
                                  const ServerPlayer *target) {
    if (!inRoom(room, viewer)) return;
    ReijyuuWitness *state = witness(room, true);
    // A failed/invalid accepted result cannot leave an older target available.
    state->targets.remove(viewer->objectName());
    if (inRoom(room, target) && target->isAlive())
        state->targets.insert(viewer->objectName(), target->objectName());
}
void projectAIStrategyReijyuuChoice(Room &room, const ServerPlayer *viewer,
                                  AIRequest &request) {
    if (request.kind != AIRequest::Choice || request.choiceOptions.reason != QStringLiteral("reijyuu")
        || !inRoom(room, viewer) || request.viewerObjectName != viewer->objectName()) return;
    request.choiceOptions.context.remove(QStringLiteral("reijyuu_flagged_players"));
    const ReijyuuWitness *state = witness(room, false);
    if (!state || !state->targets.contains(viewer->objectName())) return;
    ServerPlayer *target = room.findPlayerByObjectName(state->targets.value(viewer->objectName()), true);
    // This is the public target of this viewer's actual accepted inquiry, not
    // an arbitrary scan of opponents' flags or a new hidden-information oracle.
    if (!target || !target->isAlive() || !target->hasFlag("reijyuuT")) return;
    request.choiceOptions.context.insert(QStringLiteral("reijyuu_flagged_players"),
                                        QJsonArray{target->objectName()});
}

void projectAIStrategyAnimecardCatalog(AIWorldView &world, Room &room,
                                      const ServerPlayer *viewer) {
    if (!inRoom(room, viewer)) return;
    // The attached view-as skill uses an old SkillCard parse spelling even
    // though the fixed package now registers its product as a BasicCard.
    // Report public registry metadata; do not manufacture a replacement card.
    if (([&] { std::unique_ptr<AIWorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIProjectionSourceScope(room,gameLua))
            source.reset(new AIWorldSkillQueriesScope(room,const_cast<ServerPlayer *>(viewer),gameLua));
        return viewer->hasSkill(QStringLiteral("Promote_jiuyuanVS")); }())
        || ([&] { std::unique_ptr<AIWorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIProjectionSourceScope(room,gameLua))
            source.reset(new AIWorldSkillQueriesScope(room,const_cast<ServerPlayer *>(viewer),gameLua));
        return viewer->hasSkill(QStringLiteral("Promote_jiuyuan")); }())) {
        std::unique_ptr<LuaSkillCard> prototype(
            LuaSkillCard::Parse(QStringLiteral("#Promote_jiuyuanCard:.:")));
        QJsonObject dragon = world.strategyFacts.value(QStringLiteral("dragon")).toObject();
        dragon.insert(QStringLiteral("promote_jiuyuan_skill_prototype"), bool(prototype));
        world.strategyFacts.insert(QStringLiteral("dragon"), dragon);
    }
    if (!([&] { std::unique_ptr<AIWorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIProjectionSourceScope(room,gameLua))
            source.reset(new AIWorldSkillQueriesScope(room,const_cast<ServerPlayer *>(viewer),gameLua));
        return viewer->hasSkill(QStringLiteral("ark_guozai")); }())
        && !([&] { std::unique_ptr<AIWorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIProjectionSourceScope(room,gameLua))
            source.reset(new AIWorldSkillQueriesScope(room,const_cast<ServerPlayer *>(viewer),gameLua));
        return viewer->hasSkill(QStringLiteral("ark_zhuilie")); }())) return;
    QJsonObject prototypes;
    for (const QString &name : {QStringLiteral("#ark_guozai"), QStringLiteral("#ark_zhuilie")}) {
        // Public registered prototype metadata only. This never invokes an AI
        // callback or substitutes the modern V2 produced card for an old class.
        std::unique_ptr<LuaSkillCard> prototype(LuaSkillCard::Parse(name + QStringLiteral(":.:")));
        prototypes.insert(name, bool(prototype));
    }
    QJsonObject arknights = world.strategyFacts.value(QStringLiteral("arknights")).toObject();
    arknights.insert(QStringLiteral("legacy_prototypes"), prototypes);
    world.strategyFacts.insert(QStringLiteral("arknights"), arknights);
}
