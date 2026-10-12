#include "ai-strategy/shadows.h"

// ---- guanxing-shadow ----
#include "ai-strategy/original-hegemony.h"
#include "ai-strategy/owner.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QHash>
#include <QObject>
namespace {
struct Row{qint64 epoch=0;bool value=false;bool known=true;};
class Shadow final:public QObject{public:explicit Shadow(Room&r):QObject(&r){}QHash<const ServerPlayer*,Row> owners;bool baselineKnown=true;};
Shadow* ledger(Room&r,bool create=true){for(auto*c:r.children())if(auto*l=dynamic_cast<Shadow*>(c))return l;return create?new Shadow(r):nullptr;}
QJsonObject guanxingShadowFail(const char*why){return{{"kind","original_guanxing_shadow"},{"known",false},{"reason",QString::fromLatin1(why)}};}
bool valid(Room&r,const AIRequest&q,const ServerPlayer*p){return p&&p->getRoom()==&r&&r.getAllPlayers(true).contains(const_cast<ServerPlayer*>(p))
 &&r.roomRuntime()&&q.viewerObjectName==p->objectName()&&q.stateRevision==r.roomRuntime()->stateRevision()&&aiOriginalHegemonyQuestion(q);}
qint64 epoch(Room&r,const ServerPlayer*p){AIWorldView fresh;fresh.self.objectName=p->objectName();projectAIStrategyFlagLifecycle(fresh,r);
 return qint64(fresh.strategyFacts.value("strategy_flag_clear_epochs").toObject().value(p->objectName()).toDouble());}
Row& row(Room&r,const AIRequest&q,const ServerPlayer*p){auto*l=ledger(r);auto it=l->owners.find(p);const qint64 e=epoch(r,p);
 if(it==l->owners.end()){
  QObject::connect(p,&QObject::destroyed,l,[l,p]{l->owners.remove(p);});
  // Initial migration is lawful only while no legacy/external policy entry
  // has interrupted this ledger's baseline, including before the first read.
  if(!l->baselineKnown)return l->owners.insert(p,Row{e,false,false}).value();
  // Preserve an uninterrupted R35 owner shadow; otherwise use lawful own flag.
  auto prior=q.worldView.modePolicy.value("strategy_flags").toObject().value(p->objectName()).toObject().value("willSkipPlayPhase");
  const bool value=prior.isBool()?prior.toBool():p->hasFlag("willSkipPlayPhase");
  return l->owners.insert(p,Row{e,value,true}).value();
 }
 if(it->epoch!=e)*it=Row{e,false,true};
 return it.value();
}
QJsonObject answer(const Row&r){return r.known?QJsonObject{{"kind","original_guanxing_shadow"},{"known",true},{"value",r.value}}
 :guanxingShadowFail("legacy policy interrupted viewer shadow continuity");}
}
QJsonObject readAIGuanxingShadow(Room&r,const AIRequest&q,const ServerPlayer*p){if(!valid(r,q,p))return guanxingShadowFail("Original owner/request revision mismatch");return answer(row(r,q,p));}
QJsonObject writeAIGuanxingShadow(Room&r,const AIRequest&q,const ServerPlayer*p,bool value){
 if(!valid(r,q,p)||q.kind!=AIRequest::Guanxing||q.choiceOptions.question!="askForGuanxing"
  ||q.worldView.strategyFacts.value("original_guanxing_flag_write")!=QJsonValue(true))return guanxingShadowFail("Original Guanxing writer scope mismatch");
 auto&v=row(r,q,p);v.value=value;v.known=true;
 // Pure Room-owned strategy state: no Player write/revision/payment/event
 // transaction or Lua VM state. Reached writes survive errors and VM reset.
 return answer(v);
}
void invalidateAIGuanxingShadow(Room&r){
 auto*l=ledger(r);l->baselineKnown=false;
 for(auto it=l->owners.begin();it!=l->owners.end();++it)it->known=false;
 // Seed public clear epochs, not flag values. A later first read must not
 // resurrect a stale R35 mode-policy value. A real clear after this entry
 // reestablishes false, even if no read/write existed before the entry.
 for(const auto*p:r.getAllPlayers(true)){
  if(!l->owners.contains(p))QObject::connect(p,&QObject::destroyed,l,[l,p]{l->owners.remove(p);});
  l->owners.insert(p,Row{epoch(r,p),false,false});
 }
}

// ---- kurou-shadow ----
#include "ai-strategy/ol-ai.h"
#include "ai-strategy/overseas.h"
namespace {
struct KurouShadowRow{qint64 epoch=0;bool value=false;bool known=false;};
class KurouShadowShadow final:public QObject{public:explicit KurouShadowShadow(Room&r):QObject(&r){}bool initialized=false;bool baselineKnown=true;QHash<const ServerPlayer*,KurouShadowRow> owners;};
KurouShadowShadow*kurouShadowLedger(Room&r){for(auto*c:r.children())if(auto*l=dynamic_cast<KurouShadowShadow*>(c))return l;return new KurouShadowShadow(r);}
QJsonObject kurouShadowFail(const char*why){return{{"kind","original_kurou_shadow"},{"known",false},{"reason",QString::fromLatin1(why)}};}
bool kurouShadowValid(Room&r,const AIRequest&q,const ServerPlayer*p){return p&&p->getRoom()==&r&&r.getAllPlayers(true).contains(const_cast<ServerPlayer*>(p))&&r.roomRuntime()&&q.viewerObjectName==p->objectName()&&q.stateRevision==r.roomRuntime()->stateRevision()&&aiOriginalHegemonyQuestion(q);}
qint64 kurouShadowEpoch(Room&r,const ServerPlayer*p){AIWorldView w;w.self.objectName=p->objectName();projectAIStrategyFlagLifecycle(w,r);return qint64(w.strategyFacts.value("strategy_flag_clear_epochs").toObject().value(p->objectName()).toDouble());}
KurouShadowRow&kurouShadowRow(Room&r,const ServerPlayer*p){auto*l=kurouShadowLedger(r);const auto e=kurouShadowEpoch(r,p);auto it=l->owners.find(p);if(it==l->owners.end()){
 QObject::connect(p,&QObject::destroyed,l,[l,p]{l->owners.remove(p);});
 // The initialization hook executes before policies. Never seed a late unknown
 // row from mutable native state after an uninstrumented legacy policy.
 const bool baseline=l->initialized&&l->baselineKnown;
 return l->owners.insert(p,KurouShadowRow{e,baseline?p->hasFlag("Kurou_toDie"):false,baseline}).value();
 }if(it->epoch!=e)*it=KurouShadowRow{e,false,true};return it.value();}
QJsonObject kurouShadowAnswer(const KurouShadowRow&r){return r.known?QJsonObject{{"kind","original_kurou_shadow"},{"known",true},{"value",r.value}}:kurouShadowFail("untracked legacy policy interrupted owner Kurou flag continuity");}
bool planning(Room&r,const AIRequest&q,const ServerPlayer*p){if(q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY)return true;
 if(!q.worldView.strategyFacts.contains("readonly_play_context"))return false;
 return aiOlClanPlanningQuestion(q,r,p)||aiXiafengChoicePlanningQuestion(q,r,p)||aiNiwoShelieChoicePlanningQuestion(q,r,p);
}
}
void initializeAIKurouShadow(Room&r){auto*l=kurouShadowLedger(r);if(l->initialized)return;l->initialized=true;for(auto*p:r.getAllPlayers(true))kurouShadowRow(r,p);}
void invalidateAIKurouShadow(Room&r){auto*l=kurouShadowLedger(r);l->baselineKnown=false;for(auto it=l->owners.begin();it!=l->owners.end();++it)it->known=false;}
QJsonObject readAIKurouShadow(Room&r,const AIRequest&q,const ServerPlayer*p){if(!kurouShadowValid(r,q,p))return kurouShadowFail("Original owner/request revision mismatch");return kurouShadowAnswer(kurouShadowRow(r,p));}
QJsonObject writeAIKurouShadow(Room&r,const AIRequest&q,const ServerPlayer*p,bool v){if(!kurouShadowValid(r,q,p)||!planning(r,q,p)||!p->hasSkill("heg_kurou"))return kurouShadowFail("Original Kurou fill writer scope mismatch");auto&x=kurouShadowRow(r,p);x.value=v;x.known=true;return kurouShadowAnswer(x);}
int aiHegKurouPrototype(const AIRequest&q,const ServerPlayer*p,const QString&skill,const QString&prototype){
 if(!p||!p->getRoom()||!kurouShadowValid(*p->getRoom(),q,p)||q.kind!=AIRequest::Activate||q.reason!=CardUseStruct::CARD_USE_REASON_PLAY||skill!="heg_kurou"||prototype!="ActiveSkillCard"||!p->hasSkill(skill))return -1;
 // Fixed effective heg-common38-54, not raw Card::Parse. bit1 permits the
 // source's material-bearing hypothesis. Current native skill refs, ordered
 // canSelectCard/cardSelectionFeasible/createCard and final payment validation
 // remain downstream. No legacy HKurouCard prototype is invented.
 return 1;
}

// ---- crossbow-shadow ----
#include "ai-runtime.h"
#include "card.h"
#include "engine.h"
#include <cmath>
namespace {
struct CrossbowShadowRow{qint64 epoch=0;bool value=false;bool known=false;};
class CrossbowShadowShadow final:public QObject{public:explicit CrossbowShadowShadow(Room&r):QObject(&r){}bool initialized=false,baselineKnown=true;QHash<const ServerPlayer*,CrossbowShadowRow>owners;};
CrossbowShadowShadow*crossbowShadowLedger(Room&r){for(auto*c:r.children())if(auto*l=dynamic_cast<CrossbowShadowShadow*>(c))return l;return new CrossbowShadowShadow(r);}
QJsonObject crossbowShadowFail(const char*why){return{{"kind","original_crossbow_shadow"},{"known",false},{"reason",QString::fromLatin1(why)}};}
bool crossbowShadowValid(Room&r,const AIRequest&q,const ServerPlayer*p){return p&&p->getRoom()==&r&&r.getAllPlayers(true).contains(const_cast<ServerPlayer*>(p))&&r.roomRuntime()&&q.viewerObjectName==p->objectName()&&q.stateRevision==r.roomRuntime()->stateRevision()&&aiOriginalHegemonyQuestion(q);}
qint64 crossbowShadowEpoch(Room&r,const ServerPlayer*p){AIWorldView w;w.self.objectName=p->objectName();projectAIStrategyFlagLifecycle(w,r);return qint64(w.strategyFacts.value("strategy_flag_clear_epochs").toObject().value(p->objectName()).toDouble());}
CrossbowShadowRow&crossbowShadowRow(Room&r,const ServerPlayer*p){auto*l=crossbowShadowLedger(r);const auto e=crossbowShadowEpoch(r,p);auto it=l->owners.find(p);if(it==l->owners.end()){
 QObject::connect(p,&QObject::destroyed,l,[l,p]{l->owners.remove(p);});const bool baseline=l->initialized&&l->baselineKnown;
 return l->owners.insert(p,CrossbowShadowRow{e,baseline?p->hasFlag("AI_needCrossbow"):false,baseline}).value();
 }if(it->epoch!=e)*it=CrossbowShadowRow{e,false,true};return it.value();}
QJsonObject crossbowShadowAnswer(const CrossbowShadowRow&r){return r.known?QJsonObject{{"kind","original_crossbow_shadow"},{"known",true},{"value",r.value}}:crossbowShadowFail("untracked legacy policy interrupted owner Crossbow observation");}
const Card*sourceCard(Room&r,const AIRequest&q,ServerPlayer*p,const QJsonObject&s){
 if(s.size()!=1)return nullptr;
 if(s.value("recipe_key").isString())return r.roomRuntime()->ai().readonlyCard(q,p,s.value("recipe_key").toString());
 if(!s.value("id").isDouble())return nullptr;const double raw=s.value("id").toDouble();
 if(!std::isfinite(raw)||std::floor(raw)!=raw||raw<0||raw>=Sanguosha->getCardCount())return nullptr;const int id=int(raw);
 bool disclosed=false;for(const auto&c:q.worldView.handCards)if(c.effectiveId==id)disclosed=true;
 for(const auto&c:q.worldView.self.equips)if(c.effectiveId==id)disclosed=true;
 if(!disclosed)return nullptr;for(const Card*c:p->getCards("he"))if(c&&c->getEffectiveId()==id)return c;return nullptr;
}
}
void initializeAICrossbowShadow(Room&r){auto*l=crossbowShadowLedger(r);if(l->initialized)return;l->initialized=true;for(auto*p:r.getAllPlayers(true))crossbowShadowRow(r,p);}
void invalidateAICrossbowShadow(Room&r){auto*l=crossbowShadowLedger(r);l->baselineKnown=false;for(auto i=l->owners.begin();i!=l->owners.end();++i)i->known=false;}
QJsonObject readAICrossbowShadow(Room&r,const AIRequest&q,const ServerPlayer*p){if(!crossbowShadowValid(r,q,p))return crossbowShadowFail("Original Crossbow owner/request mismatch");return crossbowShadowAnswer(crossbowShadowRow(r,p));}
QJsonObject writeAICrossbowShadow(Room&r,const AIRequest&q,ServerPlayer*p,const QJsonObject&s){
 if(!crossbowShadowValid(r,q,p)||(q.kind!=AIRequest::Activate&&q.kind!=AIRequest::UseCard))return crossbowShadowFail("Original Crossbow writer question mismatch");
 const Card*c=sourceCard(r,q,p,s);if(!c||!c->isKindOf("Collateral"))return crossbowShadowFail("Original Crossbow source is not current owned/retained Collateral");
 auto&x=crossbowShadowRow(r,p);x.known=true;x.value=true;return crossbowShadowAnswer(x);
}

// ---- manbing ----
namespace {
struct Owner { qint64 epoch=0; bool diverged=false; bool baselineKnown=true; QHash<QString,bool> values; };
class Ledger final:public QObject {
public:explicit Ledger(Room&r):QObject(&r){}
 bool initialized=false;
 QHash<const ServerPlayer*,Owner> owners;
};
Ledger*manbingLedger(Room&r){for(QObject*c:r.children())if(auto*l=dynamic_cast<Ledger*>(c))return l;return new Ledger(r);}
bool belongs(Room&r,const ServerPlayer*p){return p&&p->getRoom()==&r&&r.getAllPlayers(true).contains(const_cast<ServerPlayer*>(p));}
qint64 manbingEpoch(Room&r,const ServerPlayer*p){AIWorldView w;w.self.objectName=p->objectName();projectAIStrategyFlagLifecycle(w,r);return qint64(w.strategyFacts.value("strategy_flag_clear_epochs").toObject().value(p->objectName()).toDouble());}
QJsonObject manbingFail(const char*reason){return{{"kind","manbing_flag"},{"known",false},{"reason",QString::fromLatin1(reason)}};}
Owner&owner(Room&r,const ServerPlayer*p){
 auto*l=manbingLedger(r);const auto e=manbingEpoch(r,p);auto it=l->owners.find(p);
 if(it==l->owners.end()){
  QObject::connect(p,&QObject::destroyed,l,[l,p]{l->owners.remove(p);});
  Owner row;row.epoch=e;
  it=l->owners.insert(p,row);
 }
 if(it->epoch!=e){it->epoch=e;it->diverged=false;it->baselineKnown=true;it->values.clear();}
 return it.value();
}
}
void initializeAIManbing(Room&r){auto*l=manbingLedger(r);if(l->initialized)return;l->initialized=true;for(const auto*p:r.getAllPlayers(true))owner(r,p);}
void invalidateAIManbing(Room&r){
 // Before any shadow write, current native owner flags are the exact source
 // state, including all startup CloneAI/legacy writes. Only divergence makes
 // subsequent untracked legacy execution ambiguous.
 for(const auto*p:r.getAllPlayers(true)){
  auto&row=owner(r,p);
  if(row.diverged){row.baselineKnown=false;row.values.clear();}
 }
}
void observeAIManbingFlag(Room&r,const ServerPlayer*p,const QString&flag){
 if(!belongs(r,p))return;
 bool remove=flag.startsWith('-');const QString key=remove?flag.mid(1):flag;
 const QString prefix=QStringLiteral("manbingTo_");if(!key.startsWith(prefix))return;
 const QString target=key.mid(prefix.size());if(!r.findPlayerByObjectName(target,true))return;
 // A real external named operation may supersede speculative writes even
 // when native state already lacks that flag. Fail closed until a new reached
 // write or a true all-flags clear establishes the next source value.
 auto&row=owner(r,p);if(row.diverged){row.baselineKnown=false;row.values.clear();}
}
QJsonObject queryAIManbing(Room&r,const AIRequest&q,const ServerPlayer*p,const QString&targetName,bool write){
 if(!belongs(r,p)||!r.roomRuntime()||q.viewerObjectName!=p->objectName()
  ||q.worldView.self.objectName!=p->objectName()||q.stateRevision!=r.roomRuntime()->stateRevision())return manbingFail("Manbing active owner/request revision mismatch");
 const auto*t=r.findPlayerByObjectName(targetName,true);
 if(!belongs(r,t)||t->objectName()!=targetName)return manbingFail("Manbing target is outside current roster");
 auto*l=manbingLedger(r);if(!l->initialized)return manbingFail("Manbing baseline was not initialized before policies");
 auto&row=owner(r,p);
 if(!row.diverged){
  if(!write)return{{"kind","manbing_flag"},{"known",true},{"value",p->hasFlag("manbingTo_"+targetName)}};
  row.values.clear();row.baselineKnown=true;
  for(const auto*other:r.getAllPlayers(true))if(p->hasFlag("manbingTo_"+other->objectName()))row.values.insert(other->objectName(),true);
  row.diverged=true;
 }
 if(write)row.values.insert(targetName,true);
 if(!row.values.contains(targetName)&&!row.baselineKnown)return manbingFail("legacy policy interrupted Manbing owner flag continuity");
 return{{"kind","manbing_flag"},{"known",true},{"value",row.values.value(targetName,false)}};
}

// ---- use-value-flag ----
#include "ai-strategy/runtime.h"

namespace {
struct Observation { qint64 epoch = 0; bool value = false; };
class ValuationFlags final : public QObject {
public:
    explicit ValuationFlags(Room &room) : QObject(&room) {}
    QHash<const ServerPlayer *, Observation> owners;
};
ValuationFlags *useValueFlagLedger(Room &room)
{
    for (QObject *child : room.children())
        if (auto *found = dynamic_cast<ValuationFlags *>(child)) return found;
    return new ValuationFlags(room);
}
bool member(Room &room, const ServerPlayer *owner)
{
    return owner && owner->getRoom() == &room
        && room.getAllPlayers(true).contains(const_cast<ServerPlayer *>(owner));
}
qint64 currentEpoch(Room &room, const ServerPlayer *owner)
{
    AIWorldView fresh;
    fresh.self.objectName = owner->objectName();
    projectAIStrategyFlagLifecycle(fresh, room);
    return qint64(fresh.strategyFacts.value("strategy_flag_clear_epochs").toObject()
        .value(owner->objectName()).toDouble());
}
bool validate(Room &room, const ServerPlayer *owner, const AIRequest &request, QString *error)
{
    if (!member(room, owner) || request.viewerObjectName != owner->objectName()
        || !room.roomRuntime() || !aiRangeRequestCurrent(room,request,owner)) {
        if (error) *error = QStringLiteral("valuation flag owner/request revision mismatch");
        return false;
    }
    return true;
}
Observation &observation(Room &room, const ServerPlayer *owner)
{
    auto *state = useValueFlagLedger(room);
    const qint64 epoch = currentEpoch(room, owner);
    auto found = state->owners.find(owner);
    if (found == state->owners.end()) {
        // Native-only pointer key, removed on player destruction, never exposed.
        QObject::connect(owner, &QObject::destroyed, state, [state, owner]() {
            state->owners.remove(owner);
        });
        return state->owners.insert(owner, Observation{epoch,
            owner->hasFlag(QStringLiteral("stack_overflow_UseValue"))}).value();
    }
    if (found->epoch != epoch) {
        found->epoch = epoch;
        found->value = false;
    }
    return found.value();
}
}

bool readAIUseValueFlag(Room &room, const ServerPlayer *owner,
    const AIRequest &request, bool &value, QString *error)
{
    if (!validate(room, owner, request, error)) return false;
    value = observation(room, owner).value;
    return true;
}
bool writeAIUseValueFlag(Room &room, const ServerPlayer *owner,
    const AIRequest &request, bool value, QString *error)
{
    if (!validate(room, owner, request, error)) return false;
    observation(room, owner).value = value;
    // Deliberately no live Player flags, revision, effect queue, Lua state or
    // event transaction. Source errors and VM reset preserve the last write.
    return true;
}
void synchronizeAIUseValueFlag(Room &room, const ServerPlayer *owner,
    const QString &command)
{
    if (!member(room, owner)) return;
    if (command == QLatin1String("stack_overflow_UseValue"))
        observation(room, owner).value = true;
    else if (command == QLatin1String("-stack_overflow_UseValue"))
        observation(room, owner).value = false;
}

// ---- overseas-priority ----
#include "ai-strategy/source-slots.h"
#include "ai-strategy/context.h"
#include "ai-strategy/lua-seal.h"
#include "lua-runtime.h"
#include "lua.hpp"
#include <QSet>
#include <QScopeGuard>
namespace {
class Priorities final:public QObject {
public:explicit Priorities(Room &r):QObject(&r){}
 bool captured=false,baselineLive=true,entered=false;
 QString viewer;
 QSet<QString> keys;
 QHash<QString,QJsonObject> baseline;
 QHash<QString,QHash<QString,QJsonObject>> values;
};
Priorities *state(Room &room) {
 for(auto *o:room.children())if(auto *s=dynamic_cast<Priorities *>(o))return s;
 return new Priorities(room);
}
bool current(Room &room,const AIRequest &r,const ServerPlayer *p) {
 return p&&p->getRoom()==&room&&r.viewerObjectName==p->objectName()
  &&r.worldView.self.objectName==p->objectName()
  &&aiRangeRequestCurrent(room,r,p);
}
QJsonObject unknown() {return{{"known",false},{"reason","priority continuity has no lawful last-writer witness"}};}
QJsonObject rejected(const QString &reason) {return{{"error",reason},{"key","OverseasVersion.priority"}};}
bool plain(lua_State *L,int i) {
 if(!lua_istable(L,i))return false;
 if(lua_getmetatable(L,i)){lua_pop(L,1);return false;}return true;
}
}
void captureAIOverseasPriorities(Room &room) {
 captureAIOLJijiang(room);
 captureAIOriginalSD(room);
 auto *s=state(room);if(s->captured)return;
 s->captured=true;
 for(const auto &k:QStringList{"ov_lingbaoCard","ov_jichouCard","ov_bomingCard","Tiaojiyanmei","ov_muyueCard","ov_yanshiYLCard"})s->keys.insert(k);
 // Public registered card definitions, never draw order or private hand faces.
 for(int i=0;i<Sanguosha->getCardCount();++i)if(const Card *c=Sanguosha->getEngineCard(i))s->keys.insert(c->getClassName());
 // Capturing after any policy entry cannot establish an initial default.
 if(s->entered){s->baselineLive=false;return;}
 LuaRuntime &runtime=room.roomRuntime()->lua();LuaRuntime::Binding binding(runtime,false);
 lua_State *L=runtime.state();if(!L){s->baselineLive=false;return;}
 int top=lua_gettop(L);const auto restore=qScopeGuard([&]{lua_settop(L,top);});
 lua_pushglobaltable(L);lua_pushliteral(L,"sgs");lua_rawget(L,-2);
 // SWIG namespaces have a metatable; rawget below never invokes it.
 if(!lua_istable(L,-1)){s->baselineLive=false;return;}
 lua_pushliteral(L,"ai_use_priority");lua_rawget(L,-2);
 if(!plain(L,-1)){s->baselineLive=false;return;}
 const int table=lua_absindex(L,-1);
 for(const auto &key:s->keys) {
  const auto bytes=key.toUtf8();lua_pushlstring(L,bytes.constData(),bytes.size());lua_rawget(L,table);
  QJsonObject row=unknown();
  if(lua_isnil(L,-1))row={{"known",true},{"removed",true}};
  else if(lua_type(L,-1)==LUA_TNUMBER&&std::isfinite(lua_tonumber(L,-1)))
   row={{"known",true},{"removed",false},{"value",lua_tonumber(L,-1)}};
  row.insert("basis","configured_registry_initial_value");s->baseline.insert(key,row);lua_pop(L,1);
 }
}
void beginAIOverseasPriorityPolicy(Room &room,const QString &viewer) {
 beginAIChoiceContinuityPolicy(room,viewer);
 beginAIOLJijiang(room,viewer);
 beginAIOriginalSD(room,viewer);
 auto *s=state(room);s->entered=true;
 if(viewer.isEmpty()||(!s->viewer.isEmpty()&&s->viewer!=viewer)) {
  s->baselineLive=false;s->values.clear();
 }
 s->viewer=viewer;
}
void invalidateAIOverseasPriorities(Room &room,bool invalidateChoices) {
 if(invalidateChoices)invalidateAIChoiceContinuity(room);
 invalidateAIOLJijiang(room);
 invalidateAIOriginalSD(room);
 invalidateAIKurouShadow(room);
 invalidateAIManbing(room);
 invalidateAIOriginalTurnUse(room);
 invalidateAICrossbowShadow(room);
 invalidateAIGuanxingShadow(room);
 auto *s=state(room);s->entered=true;s->baselineLive=false;s->values.clear();
}
QJsonObject queryAIOverseasPriority(Room &room,const AIRequest &r,ServerPlayer *p,const QJsonObject &spec) {
 if(!current(room,r,p))return rejected("stale priority source request");
 auto *s=state(room);const QString key=spec.value("key").toString();
 if(!s->captured)return rejected("configured priority registry was not captured before policy execution");
 if(!s->keys.contains(key))return{{"kind","overseas_priority"},{"tracked",false}};
 if(s->viewer!=p->objectName())return rejected("priority policy-entry witness missing");
 const QString op=spec.value("op").toString();QJsonObject row;
 if(op=="read"&&spec.size()==3) {
  const auto own=s->values.value(p->objectName());
  row=own.contains(key)?own.value(key):(s->baselineLive?s->baseline.value(key,unknown()):unknown());
  // Preserve already-issued R29 writes/baselines. The original mixed-sort
  // ExNihilo read can instead consume its independent source-born cell when
  // the generic baseline was conservatively retired by the Play event.
  // This performs a fresh actual read behind monotonic writer history; it
  // never repairs R29 state or trusts an equal current registry value.
  if(!row.value("known").toBool()&&key==QStringLiteral("ExNihilo")
   &&r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY) {
   const auto actual=readAIUsePriority(room,p,r,{{"op","source_priority_read"},{"key",key}});
   if(actual.value("known")==true&&actual.value("present").isBool()) {
    const bool present=actual.value("present").toBool();const auto value=actual.value("value");
    if(!present||(value.isDouble()&&std::isfinite(value.toDouble()))) {
     row={{"known",true},{"removed",!present},{"basis","original_source_cell_read"}};
     if(present)row.insert("value",value);
    }
   }
  }
 }else if(op=="write"&&spec.size()==5&&spec.value("removed").isBool()) {
  const bool removed=spec.value("removed").toBool();const auto v=spec.value("value");
  if((removed&&!v.isNull())||(!removed&&(!v.isDouble()||!std::isfinite(v.toDouble()))))return rejected("invalid raw priority assignment");
  row={{"known",true},{"removed",removed},{"basis","reached_viewer_source_write"}};
  if(!removed)row.insert("value",v);
  s->values[p->objectName()].insert(key,row);
 }else return rejected("invalid priority operation shape");
 row.insert("kind","overseas_priority");row.insert("tracked",true);row.insert("key",key);
 row.insert("viewer",p->objectName());row.insert("decision_id",r.getDecisionId());row.insert("state_revision",r.getStateRevision());
 return row;
}
QJsonObject writeAIXiafengFlag(Room &room,const AIRequest &r,ServerPlayer *p,int id,bool enabled) {
 auto fail=[](const QString &s){return QJsonObject{{"error",s},{"key","ov_xiafeng.card_flag"}};};
 if(!current(room,r,p)||!aiXiafengChoicePlanningQuestion(r,room,p)
  ||r.choiceOptions.question!="askForSkillInvoke"||room.getCurrent()!=p)
  return fail("Xiafeng source flag write is outside the actual owner inquiry");
 const Card *c=room.getCard(id);
 if(id<0||!c||c->isVirtualCard()||c->getEffectiveId()!=id||!p->handCards().contains(id)
  ||room.getCardOwner(id)!=p||room.getCardPlace(id)!=Player::PlaceHand)
  return fail("Xiafeng source flag requires an actual owner hand card");
 bool disclosed=false;
 for(const auto &v:r.worldView.handCards)if(v.effectiveId==id&&v.objectName==c->objectName()
  &&v.className==c->getClassName()&&v.suit==int(c->getSuit())&&v.number==c->getNumber())disclosed=true;
 if(!disclosed)return fail("Xiafeng source hand identity changed");
 if(enabled&&!(c->isDamageCard()&&(c->getTypeId()==Card::TypeBasic||c->isKindOf("AOE")||c->isKindOf("SingleTargetTrick"))))
  return fail("Xiafeng positive source flag predicate rejected");
 // Fixed donor AI-only side effect. Card::setFlags mutates only its flag list;
 // no room revision, card lifetime, payment, gameplay rule, or target changes.
 c->setFlags(enabled?"AIGlobal_KillOff":"-AIGlobal_KillOff");
 return{{"kind","overseas_xiafeng_flag"},{"card_id",id},{"enabled",enabled},
  {"viewer",p->objectName()},{"decision_id",r.getDecisionId()},{"state_revision",r.getStateRevision()}};
}

// ---- original-sd-choice ----
#include "game-rng.h"
#include "standard.h"
#include "player-armor-effect-domain.h"
#include <QUuid>
namespace {
struct Capture {QString viewer,target,flags,scope,identity;QJsonObject source;quint64 decision=0,revision=0;QList<int> ids;int selected=-1;};
struct Mark {QString identity,target;bool value=false;};
struct OriginalSdChoiceOwner {bool baseline=false;QHash<QString,QHash<int,Mark>> marks;};
class OriginalSdChoiceLedger final:public QObject {public:explicit OriginalSdChoiceLedger(Room&r):QObject(&r){}bool captured=false,entered=false,initial=false;QString active;
 QHash<QString,OriginalSdChoiceOwner> owners;QHash<QString,Capture> scopes,samples,receipts;};
OriginalSdChoiceLedger &originalSdChoiceLedger(Room&r){for(auto*c:r.children())if(auto*l=dynamic_cast<OriginalSdChoiceLedger*>(c))return*l;return *new OriginalSdChoiceLedger(r);}
QString nonce(const char*prefix){return QString::fromLatin1(prefix)+QUuid::createUuid().toString(QUuid::WithoutBraces);}
QJsonObject fail(const char*why){return{{"kind","original_sd_choice"},{"known",false},{"reason",QString::fromLatin1(why)}};}
QJsonObject ok(){return{{"kind","original_sd_choice"},{"known",true}};}
bool originalSdChoiceCurrent(Room&r,const AIRequest&q,ServerPlayer*p){return p&&p->getRoom()==&r&&q.viewerObjectName==p->objectName()
 &&q.stateRevision==r.roomRuntime()->stateRevision()&&aiOriginalHegemonyQuestion(q);}
bool reason(const QString&s){return s=="snatch"||s=="dismantlement";}
bool zones(const QString&s){if(s.isEmpty())return false;for(auto c:s)if(c!='h'&&c!='e'&&c!='j')return false;return true;}
QList<int> full(ServerPlayer*p,const QString&flags){QList<int> ids;for(auto*c:p->getCards(flags)){if(!c||c->getEffectiveId()<0)return{};ids<<c->getEffectiveId();}return ids;}
bool stamp(const Capture&c,const AIRequest&q){return c.viewer==q.viewerObjectName&&c.decision==q.decisionId&&c.revision==q.stateRevision;}
QString identity(Room&r,ServerPlayer*p,int id){return aiLegacyCardIdentity(r,p,Sanguosha->getCard(id));}
bool disclosed(const AIRequest&q,ServerPlayer*p,ServerPlayer*t,int id){
 if(p==t){for(const auto&c:q.worldView.handCards)if(c.effectiveId==id)return true;}
 const AIPlayerView*v=p==t?&q.worldView.self:nullptr;for(const auto&x:q.worldView.players)if(x.objectName==t->objectName())v=&x;
 if(!v)return false;for(const auto*cards:{&v->knownCards,&v->equips,&v->judgingArea})for(const auto&c:*cards)if(c.effectiveId==id)return true;return false;
}
bool originalSdChoiceSourceCard(Room&r,const AIRequest&q,ServerPlayer*p,const QJsonObject&s,const QString&name){
 // Actual owner physical or retained readonly object only. No arbitrary clone,
 // hidden material lookup, declaration, payment or native flag modification.
 const Card*c=nullptr;
 if(s.size()==1&&s.value("recipe_key").isString())c=r.roomRuntime()->ai().readonlyCard(q,p,s.value("recipe_key").toString());
 else if(s.size()==1&&s.value("id").isDouble()){
  const double n=s.value("id").toDouble();if(!std::isfinite(n)||std::floor(n)!=n||n<0||n>Sanguosha->getCardCount())return false;
  int id=int(n);for(auto*x:p->getCards("he"))if(x&&x->getEffectiveId()==id)c=x;
 }
 return c&&c->objectName()==name;
}
OriginalSdChoiceOwner &originalSdChoiceOwner(OriginalSdChoiceLedger&l,const QString&name){if(!l.owners.contains(name)){OriginalSdChoiceOwner o;o.baseline=l.captured&&l.initial;l.owners.insert(name,o);}return l.owners[name];}
Capture capture(const AIRequest&q,ServerPlayer*t,const QString&flags,const QString&scope){Capture c;c.viewer=q.viewerObjectName;c.target=t->objectName();c.flags=flags;c.scope=scope;c.decision=q.decisionId;c.revision=q.stateRevision;c.ids=full(t,flags);return c;}
}
void captureAIOriginalSD(Room&r){auto&l=originalSdChoiceLedger(r);if(l.captured)return;l.captured=true;l.initial=!l.entered;
 // Bootstrap is public configured initial state, captured before any policy.
 // Never expose or reconstruct a preexisting private flag map.
 if(l.initial)for(int id=0;id<Sanguosha->getCardCount();++id){const Card*c=Sanguosha->getCard(id);if(c&&(c->hasFlag("AIGlobal_SDCardChosen_snatch")||c->hasFlag("AIGlobal_SDCardChosen_dismantlement"))){l.initial=false;break;}}
}
void beginAIOriginalSD(Room&r,const QString&viewer){auto&l=originalSdChoiceLedger(r);l.entered=true;
 if(viewer.isEmpty()||(!l.active.isEmpty()&&l.active!=viewer)){l.initial=false;for(auto i=l.owners.begin();i!=l.owners.end();++i){i->baseline=false;i->marks.clear();}}
 l.active=viewer;l.scopes.clear();l.samples.clear();l.receipts.clear();
}
void invalidateAIOriginalSD(Room&r){auto&l=originalSdChoiceLedger(r);l.entered=true;l.initial=false;l.scopes.clear();l.samples.clear();l.receipts.clear();for(auto i=l.owners.begin();i!=l.owners.end();++i){i->baseline=false;i->marks.clear();}}
void invalidateAIOriginalSDCard(Room&r,int id){auto&l=originalSdChoiceLedger(r);for(auto i=l.owners.begin();i!=l.owners.end();++i)for(auto j=i->marks.begin();j!=i->marks.end();++j){
 // Movement does not clear source flags. Lose knowledge, do not invent false.
 if(j->contains(id)){Mark unknown;j->insert(id,unknown);}}
 for(auto i=l.samples.begin();i!=l.samples.end();)if(i->ids.contains(id))i=l.samples.erase(i);else++i;
 for(auto i=l.receipts.begin();i!=l.receipts.end();)if(i->ids.contains(id))i=l.receipts.erase(i);else++i;
}
QJsonObject queryAIOriginalSD(Room&r,const AIRequest&q,ServerPlayer*p,const QJsonObject&s){
 if(!originalSdChoiceCurrent(r,q,p))return fail("Original SD request/viewer/revision mismatch");auto&l=originalSdChoiceLedger(r);if(l.active!=p->objectName())return fail("Original SD policy-entry witness missing");
 const QString op=s.value("op").toString();
 if(op=="enter"&&s.size()==3&&s.value("source").isObject()){
  const QString name=s.value("reason").toString();if(!reason(name)
   ||!originalSdChoiceSourceCard(r,q,p,s.value("source").toObject(),name))return fail("Original SD planning source is not an exact owner/retained object");
  const QString key=nonce("original-sd-scope:");Capture c;c.viewer=p->objectName();c.decision=q.decisionId;c.revision=q.stateRevision;c.flags=name;c.source=s.value("source").toObject();l.scopes.insert(key,c);auto out=ok();out.insert("scope",key);return out;
 }
 if(op=="leave"&&s.size()==2){l.scopes.remove(s.value("scope").toString());return ok();}
 if(op=="public_armor_absence"&&s.size()==3){
  // Exact literal names reached by fixed Original common/standard-card families.
  // bazhen stays a literal armor-name query, never a private skill query.
  const auto name=s.value("name").toString();
  if(name!="SilverLion"&&name!="EightDiagram"&&name!="bazhen"&&name!="Vine"&&name!="IronArmor"&&name!="RenwangShield"&&name!="PeaceSpell"&&name!="Breastplate")return fail("Original public armor name outside finite source domain");
  auto*t=r.findPlayerByObjectName(s.value("target").toString(),true);
  if(!t)return fail("Original public armor target missing");
  const AIPlayerView*v=t==p?&q.worldView.self:nullptr;
  for(const auto&row:q.worldView.players)if(row.objectName==t->objectName())v=&row;
  if(!v||!v->equips.isEmpty()||!t->getEquips().isEmpty())return fail("Original public armor equipment domain is not empty");
  if(isPlayerArmorEffectNamePossible(name))return fail("Original public armor virtual effect is not proved");
  auto out=ok();out.insert("value",false);return out;
 }
 if(op=="silver_lion"){
  auto*t=r.findPlayerByObjectName(s.value("target").toString(),true);
  if(!t)return fail("Original SilverLion target missing");
  if(s.contains("scope")){
   const auto sc=l.scopes.constFind(s.value("scope").toString());
   if(s.size()!=3||sc==l.scopes.cend()||!stamp(*sc,q)||!originalSdChoiceSourceCard(r,q,p,sc->source,sc->flags))return fail("Original SilverLion call-tree scope missing");
  }else{
   if(s.size()!=2||q.kind!=AIRequest::CardChosen||q.choiceOptions.question!="askForCardChosen"
    ||q.choiceOptions.context.value("who")!=QJsonValue(t->objectName()))return fail("Original SilverLion inquiry mismatch");
  }
  // Empty public equipment makes the entire realEquip/matching loop empty.
  // No foreign skill, property, IgnoreArea mark or nullification rule is read.
  const AIPlayerView*v=t==p?&q.worldView.self:nullptr;
  for(const auto&row:q.worldView.players)if(row.objectName==t->objectName())v=&row;
  if(!v||!v->equips.isEmpty()||!t->getEquips().isEmpty())return fail("Original SilverLion equipped effect is not proved");
  if(isPlayerArmorEffectNamePossible(QStringLiteral("SilverLion")))return fail("Original SilverLion virtual effect is not proved");
  auto out=ok();out.insert("value",false);return out;
 }
 if(op=="read"&&s.size()==4){
  const QString name=s.value("reason").toString(),target=s.value("target").toString(),flags=s.value("flags").toString();
  auto*t=r.findPlayerByObjectName(target,true);const auto&ctx=q.choiceOptions.context;
  if(!reason(name)||!t||!zones(flags)||q.kind!=AIRequest::CardChosen||q.choiceOptions.question!="askForCardChosen"
   ||q.choiceOptions.reason!=name||ctx.value("who")!=QJsonValue(target)||ctx.value("flags")!=QJsonValue(flags))return fail("Original SD reader question mismatch");
  auto&o=originalSdChoiceOwner(l,p->objectName());auto&marks=o.marks[name];const auto ids=full(t,flags);
  for(int id:ids){auto i=marks.find(id);if(i==marks.end()){if(!o.baseline)return fail("Original SD earlier flag is not observed");continue;}
   if(i->identity!=identity(r,p,id)||i->target!=target)return fail("Original SD card lifetime/location continuity unknown");
   if(!i->value)continue;
   // Clear at the reached source read, not after accepted decision. Prefix
   // survives later errors/native rejection just as source Card::setFlags.
   i->value=false;Capture c=capture(q,t,flags,{});c.selected=id;c.identity=i->identity;
   const QString token=nonce("isolated-original-sd-selected:");l.receipts.insert(token,c);
   auto out=ok();out.insert("selected",true);out.insert("receipt",QJsonObject{{"token",token},{"index",1},{"avoid_wounded_enemy_silver_lion",false}});return out;
  }
  auto out=ok();out.insert("selected",false);return out;
 }
 const QString scope=s.value("scope").toString();auto sc=l.scopes.constFind(scope);if(sc==l.scopes.cend()||!stamp(*sc,q)||!originalSdChoiceSourceCard(r,q,p,sc->source,sc->flags))return fail("Original SD active call-tree scope missing");
 // Insert after the existing sc/stamp check. This is an observation-only
 // query under the same sealed Snatch/Dismantlement source call tree.
 if(op=="can_get"&&s.size()==4){
  auto*t=r.findPlayerByObjectName(s.value("target").toString(),true);
  if(!t)return fail("Original canGet target missing");
  const auto key=s.value("key");
  if(key.isString()){
   if(!p->isAlive()||!t->isAlive()){auto out=ok();out.insert("value",false);return out;}
   const QString flags=key.toString();if(!zones(flags))return fail("Original canGet zone string invalid");
   // Player::canGet(target,flags) visits h,e,j in that order. A foreign
   // hand predicate can reveal hidden class/suit existence: authenticate ALL
   // selected hand faces before evaluating its native existential predicate.
   for(const auto zone:{QStringLiteral("h"),QStringLiteral("e"),QStringLiteral("j")}){
    if(!flags.contains(zone))continue;
    const auto ids=full(t,zone);
    for(int id:ids)if(!disclosed(q,p,t,id))return fail("Original canGet selected zone is not fully disclosed");
    for(int id:ids)if(p->canGet(t,id)){auto out=ok();out.insert("value",true);return out;}
   }
   auto out=ok();out.insert("value",false);return out;
  }
  if(key.isDouble()){
   const double raw=key.toDouble();if(!std::isfinite(raw)||std::floor(raw)!=raw||raw<0||raw>=Sanguosha->getCardCount())return fail("Original canGet material malformed");
   const int id=int(raw);if(!full(t,"hej").contains(id)||!disclosed(q,p,t,id))return fail("Original canGet material not a current disclosed target card");
   auto out=ok();out.insert("value",p->canGet(t,id));return out;
  }
  return fail("Original canGet key is neither exact ID nor zones");
 }
 if(op=="domain"&&s.size()==4){auto*t=r.findPlayerByObjectName(s.value("target").toString(),true);const auto flags=s.value("flags").toString();if(!t||!zones(flags))return fail("Original SD target/zones invalid");
  Capture c=capture(q,t,flags,scope);const QString token=nonce("original-sd-domain:");l.samples.insert(token,c);auto out=ok();out.insert("token",token);out.insert("count",c.ids.size());return out;}
 if(op=="select"&&s.size()==5){auto it=l.samples.find(s.value("token").toString());const double ordinal=s.value("ordinal").toDouble(-1);if(it==l.samples.end()||!stamp(*it,q)||it->scope!=scope||!s.value("avoid").isBool()
  ||!std::isfinite(ordinal)||std::floor(ordinal)!=ordinal||ordinal<0||ordinal>=it->ids.size())return fail("Original SD source sample ordinal invalid");auto*t=r.findPlayerByObjectName(it->target,true);if(!t||full(t,it->flags)!=it->ids)return fail("Original SD sampled domain changed");int index=int(ordinal);
  if(s.value("avoid").toBool()){if(!t->isWounded()||!t->hasArmorEffect("SilverLion"))return fail("Original SD SilverLion prerequisites changed");if(Sanguosha->getCard(it->ids[index])==t->getArmor()){if(index+1<it->ids.size())++index;else if(index>0)--index;}}
  it->selected=it->ids[index];it->identity=identity(r,p,it->selected);auto out=ok();out.insert("handle",s.value("token"));return out;}
 if(op=="native_hand"&&s.size()==3){auto*t=r.findPlayerByObjectName(s.value("target").toString(),true);if(!t)return fail("Original SD hand target missing");Capture c=capture(q,t,"h",scope);
  // Player::getRandomHandCardId preserves its own gameplay RNG stream/order.
  {GameRng::Binding gameplay(r.roomRuntime()->rng());c.selected=t->getRandomHandCardId();}auto out=ok();if(c.selected<0){out.insert("empty",true);return out;}c.identity=identity(r,p,c.selected);const auto key=nonce("original-sd-native-hand:");l.samples.insert(key,c);out.insert("handle",key);return out;}
 if(op=="write"&&s.size()==5){const auto name=s.value("reason").toString();auto*t=r.findPlayerByObjectName(s.value("target").toString(),true);if(!t||name!=sc->flags)return fail("Original SD writer source/target mismatch");int id=-1;QString ident;
  const auto value=s.value("selection");if(value.isString()){auto i=l.samples.constFind(value.toString());if(i==l.samples.cend()||!stamp(*i,q)||i->scope!=scope||i->target!=t->objectName()||i->selected<0||full(t,i->flags)!=i->ids)return fail("Original SD selected handle expired");id=i->selected;ident=i->identity;}
  else if(value.isDouble()){double n=value.toDouble();if(!std::isfinite(n)||std::floor(n)!=n||n<0||n>=Sanguosha->getCardCount())return fail("Original SD disclosed selection invalid");id=int(n);if(!disclosed(q,p,t,id)||!full(t,"hej").contains(id))return fail("Original SD selected ID was not disclosed");ident=identity(r,p,id);}
  else return fail("Original SD selection shape invalid");if(ident!=identity(r,p,id))return fail("Original SD selected identity changed");
  originalSdChoiceOwner(l,p->objectName()).marks[name].insert(id,Mark{ident,t->objectName(),true});return ok();}
 return fail("Original SD operation shape invalid");
}
bool resolveAIOriginalSD(Room&r,ServerPlayer*p,const AIRequest&q,AIResult&result){
 const auto payload=result.action.randomCardChoice;const QString token=payload.value("token").toString();if(!token.startsWith("isolated-original-sd-selected:"))return false;
 if(!originalSdChoiceCurrent(r,q,p)||q.kind!=AIRequest::CardChosen||q.choiceOptions.question!="askForCardChosen"||payload.size()!=3||payload.value("index")!=QJsonValue(1)||payload.value("avoid_wounded_enemy_silver_lion")!=QJsonValue(false))return false;
 auto&l=originalSdChoiceLedger(r);auto i=l.receipts.find(token);if(i==l.receipts.end()||!stamp(*i,q)||i->selected<0)return false;auto*t=r.findPlayerByObjectName(i->target,true);
 if(!t||q.choiceOptions.context.value("who")!=QJsonValue(i->target)||q.choiceOptions.context.value("flags")!=QJsonValue(i->flags)||full(t,i->flags)!=i->ids||identity(r,p,i->selected)!=i->identity)return false;
 result.action.selectedCardIds<<i->selected;result.action.randomCardChoiceResolved=true;l.receipts.erase(i);return true;
}

// ---- target-cache ----
#include "general.h"
#include "server-info.h"
#include <atomic>

namespace {
QJsonObject scarletMetadata(Room &);
QJsonObject description(const ServerPlayer *player, const ServerPlayer *viewer,
                        const QString &disclosedName, bool second)
{
    const General *general = second ? player->getGeneral2() : player->getGeneral();
    const QString actualName = general ? general->objectName() : QString();
    // An absent foreign snapshot name may conceal a general: it never proves
    // native null. A nonempty disclosed name must still match the live slot.
    const bool slotPublic = !ServerInfo.EnableHegemony || player->isDead()
        || (second ? player->hasShownGeneral2() : player->hasShownGeneral());
    if (player != viewer && (!slotPublic || disclosedName != actualName))
        return {{QStringLiteral("known"), false}};
    if (!general)
        return {{QStringLiteral("known"), true}, {QStringLiteral("present"), false}};
    return {{QStringLiteral("known"), true}, {QStringLiteral("present"), true},
            {QStringLiteral("text"), general->getSkillDescription(true)}};
}
}
void projectAITargetRecommend(AIWorldView &world, Room &room, const ServerPlayer *viewer)
{
    if (!viewer || viewer->getRoom() != &room || world.self.objectName != viewer->objectName()) return;
    QJsonObject rows;
    auto append = [&](const AIPlayerView &view) {
        const ServerPlayer *player = room.findPlayerByObjectName(view.objectName, true);
        if (!player || player->getRoom() != &room) return;
        rows.insert(view.objectName, QJsonObject{
            {QStringLiteral("first"), description(player, viewer, view.generalName, false)},
            {QStringLiteral("second"), description(player, viewer, view.general2Name, true)}});
    };
    append(world.self);
    for (const AIPlayerView &view : world.players) append(view);
    QJsonObject facts = world.strategyFacts.value(QStringLiteral("target_recommend")).toObject();
    facts.insert(QStringLiteral("general_descriptions"), rows);
    facts.insert(QStringLiteral("scarlet"), scarletMetadata(room));
    world.strategyFacts.insert(QStringLiteral("target_recommend"), facts);
}

namespace {
class TargetCache final : public QObject {
public:
    explicit TargetCache(Room &room) : QObject(&room) {
        static std::atomic<quint64> next{0};
        scope = QString::number(++next);
    }
    QString scope;
    quint64 epoch = 0;
    bool initialized = false;
    bool entered = false;
    bool known = false;
    bool populated = false;
    QString viewer;
    QJsonObject scarlet{{QStringLiteral("captured"), false}};
};
TargetCache *cache(Room &room) {
    for (QObject *child : room.children())
        if (auto *value = dynamic_cast<TargetCache *>(child)) return value;
    return new TargetCache(room);
}
QJsonObject scarletMetadata(Room &room) { return cache(room)->scarlet; }
bool origin(lua_State *L, int index, const char *file, int line) {
    if (!lua_isfunction(L, index)) return false;
    lua_pushvalue(L, index); lua_Debug info{};
    return lua_getinfo(L, ">S", &info) && info.what && QByteArray(info.what) == "Lua"
        && info.source && QString::fromUtf8(info.source) == QStringLiteral("@lua/ai/") + QString::fromLatin1(file)
        && info.linedefined == line;
}
bool upvalue(lua_State *L, int index, const char *wanted) {
    index = lua_absindex(L, index);
    for (int i=1;;++i) {
        const char *name = lua_getupvalue(L, index, i);
        if (!name) return false;
        if (QByteArray(name) == wanted) return true;
        lua_pop(L, 1);
    }
}
bool targetCachePlain(lua_State *L, int index) {
    if (!lua_istable(L,index)) return false;
    if (lua_getmetatable(L,index)) {lua_pop(L,1); return false;}
    return true;
}
QJsonObject captureScarlet(lua_State *L, int sgsIndex) {
    const int base=lua_gettop(L); sgsIndex=lua_absindex(L,sgsIndex);
    const auto restore=qScopeGuard([&]{lua_settop(L,base);});
    lua_pushliteral(L,"ai_target_recommend");lua_rawget(L,sgsIndex);
    const int registry=lua_absindex(L,-1);
    const bool registryKnown=targetCachePlain(L,registry);
    QJsonObject entries;
    const QList<QPair<QString,int>> keys{{"s4_banjiang",1686},{"s4_ganglie",1828},{"s4_hunzi",2035},{"s4_qiaobian",2285}};
    for (const auto &key:keys) {
        const int top=lua_gettop(L);
        QJsonObject row{{"known",registryKnown}};
        if (registryKnown) {
            const QByteArray name=key.first.toUtf8();lua_pushlstring(L,name.constData(),size_t(name.size()));lua_rawget(L,registry);
            const int wrapper=lua_absindex(L,-1);
            const bool present=lua_toboolean(L,wrapper);
            row.insert("present",present);
            bool matched=origin(L,wrapper,"smart-ai.lua",11203);
            if (matched) {
                matched=upvalue(L,wrapper,"skill_name");
                if (matched) {matched=lua_type(L,-1)==LUA_TSTRING&&QString::fromUtf8(lua_tostring(L,-1))==key.first;lua_pop(L,1);}
            }
            if (matched) matched=upvalue(L,wrapper,"fn");
            if (matched && key.first=="s4_ganglie") {
                matched=origin(L,-1,"smart-ai.lua",11229)&&upvalue(L,-1,"spec")&&targetCachePlain(L,-1);
                if (matched) {
                    const int spec=lua_absindex(L,-1);lua_pushliteral(L,"invalid");lua_rawget(L,spec);
                    matched=lua_type(L,-1)==LUA_TNUMBER&&lua_tonumber(L,-1)==3;lua_pop(L,1);
                    lua_pushliteral(L,"rules");lua_rawget(L,spec);matched=matched&&targetCachePlain(L,-1)&&lua_rawlen(L,-1)==1;
                    if(matched) {lua_rawgeti(L,-1,1);matched=targetCachePlain(L,-1);}
                    if(matched) {
                        const int rule=lua_absindex(L,-1);lua_pushliteral(L,"score");lua_rawget(L,rule);
                        matched=lua_type(L,-1)==LUA_TNUMBER&&lua_tonumber(L,-1)==-3;lua_pop(L,1);
                        lua_pushliteral(L,"when");lua_rawget(L,rule);matched=matched&&origin(L,-1,"scarlet-ai.lua",key.second);
                    }
                }
            } else if (matched) matched=origin(L,-1,"scarlet-ai.lua",key.second);
            row.insert("source_matches",matched);row.insert("enabled",present&&matched);
        }
        entries.insert(key.first,row);lua_settop(L,top);
    }
    QJsonObject types;
    for (const auto &pair:QList<QPair<QString,QString>>{{"draw","s4_ganglie"},{"decrease","s4_qiaobian"}}) {
        const QByteArray list=(pair.first+"SkillsList").toUtf8();lua_pushlstring(L,list.constData(),size_t(list.size()));lua_rawget(L,sgsIndex);
        bool known=targetCachePlain(L,-1),present=false;
        if(known) {
            const size_t size=lua_rawlen(L,-1);
            if(size>4096) known=false;
            else for(size_t i=1;i<=size;++i) {lua_rawgeti(L,-1,lua_Integer(i));if(lua_type(L,-1)==LUA_TSTRING&&QString::fromUtf8(lua_tostring(L,-1))==pair.second)present=true;lua_pop(L,1);}
        }
        types.insert(pair.second,QJsonObject{{"known",known},{"type",pair.first},{"present",present}});lua_pop(L,1);
    }
    return {{"captured",true},{"entries",entries},{"types",types}};
}
QJsonObject unknownCache(const QString &reason) {
    return {{QStringLiteral("known"), false}, {QStringLiteral("reason"), reason}};
}
QJsonObject cacheRow(const TargetCache &state) {
    return {{QStringLiteral("known"), true}, {QStringLiteral("scope"), state.scope},
            {QStringLiteral("epoch"), QString::number(state.epoch)},
            {QStringLiteral("state"), state.populated ? QStringLiteral("captured") : QStringLiteral("empty")}};
}
}
void captureAITargetCache(Room &room) {
    TargetCache *state = cache(room);
    if (state->initialized) return;
    state->initialized = true;
    LuaRuntime &runtime = room.roomRuntime()->lua();
    LuaRuntime::Binding binding(runtime, false);
    lua_State *L = runtime.state();
    if (!L) return;
    const int top = lua_gettop(L);
    const auto restore = qScopeGuard([&] { lua_settop(L, top); });
    lua_pushglobaltable(L);
    lua_pushliteral(L, "sgs"); lua_rawget(L, -2);
    // SWIG's namespace has a metatable; these exact raw reads invoke no policy.
    if (!lua_istable(L, -1)) return;
    state->scarlet=captureScarlet(L,-1);
    if (state->entered) return;
    lua_pushliteral(L, "ai_active_recommends_cache"); lua_rawget(L, -2);
    // Do not export or reconstruct an already-populated donor table.
    state->known = !lua_toboolean(L, -1);
}
void resetAITargetCache(Room &room) {
    TargetCache *state = cache(room);
    ++state->epoch;
    state->known = true;
    state->populated = false;
    state->viewer.clear();
}
void invalidateAITargetCache(Room &room) {
    TargetCache *state = cache(room);
    state->entered = true;
    state->known = false;
}
QJsonObject queryAITargetCache(Room &room, const AIRequest &request,
                                    ServerPlayer *viewer, const QJsonObject &proposal) {
    // Pure observer caches need only their actual snapshot/lifecycle stamp.
    // Unlike legacy target-cache capture this does not read a GameVM table,
    // attest an old absence, or depend on card/range payment certificates.
    if (proposal.value(QStringLiteral("op")) == QStringLiteral("observer_epoch")) {
        if (proposal.size() != 1 || !room.roomRuntime() || room.roomRuntime()->isClosing()
            || !viewer || viewer->getRoom() != &room
            || request.viewerObjectName != viewer->objectName()
            || request.worldView.self.objectName != request.viewerObjectName
            || request.worldView.revision != request.stateRevision
            || request.stateRevision != room.roomRuntime()->stateRevision())
            return unknownCache(QStringLiteral("observer cache snapshot is stale or foreign"));
        const auto *state = cache(room);
        return {{"known",true},{"scope",state->scope},{"epoch",QString::number(state->epoch)},
                {"viewer",request.viewerObjectName},{"revision",QString::number(request.stateRevision)}};
    }
    if (!viewer || viewer->getRoom() != &room || request.viewerObjectName != viewer->objectName()
        || request.worldView.self.objectName != viewer->objectName()
        || !aiRangeRequestCurrent(room,request,viewer))
        return unknownCache(QStringLiteral("target recommendation cache request is stale or foreign"));
    TargetCache *state = cache(room);
    state->entered = true;
    const QString op = proposal.value(QStringLiteral("op")).toString();
    if ((op != QStringLiteral("read") && op != QStringLiteral("capture") && op != QStringLiteral("epoch"))
        || proposal.size() != (op == QStringLiteral("capture") ? 3 : 1))
        return unknownCache(QStringLiteral("target recommendation cache proposal is malformed"));
    if (op == QStringLiteral("epoch"))
        return {{"known",true},{"scope",state->scope},{"epoch",QString::number(state->epoch)}};
    if (!state->known)
        return unknownCache(QStringLiteral("target recommendation cache history is unknown"));
    if (state->populated && state->viewer != viewer->objectName())
        return unknownCache(QStringLiteral("target recommendation cache was captured by another viewer"));
    if (op == QStringLiteral("read")) return cacheRow(*state);
    if (!proposal.value(QStringLiteral("scope")).isString()
        || proposal.value(QStringLiteral("scope")).toString() != state->scope
        || !proposal.value(QStringLiteral("epoch")).isString()
        || proposal.value(QStringLiteral("epoch")).toString() != QString::number(state->epoch)
        || state->populated)
        return unknownCache(QStringLiteral("target recommendation cache capture stamp is stale"));
    state->populated = true;
    state->viewer = viewer->objectName();
    return cacheRow(*state);
}

// ---- fixed-distance ----
#include <QJsonObject>
namespace {
class FixedDistanceLedger final : public QObject {
public:
 explicit FixedDistanceLedger(Room &room) : QObject(&room) { setObjectName("isolated-round34-fixed-distance"); }
 // Viewer -> source -> targets. These are notification witnesses, not native maps.
 QHash<const ServerPlayer *, QHash<const Player *, QSet<const Player *>>> rows;
 QSet<const Player *> connected;
 void invalidate(const Player *p) {
  for (auto &sources : rows) {
   sources.remove(p);
   for (auto &targets : sources) targets.remove(p);
  }
 }
};
FixedDistanceLedger *fixedDistanceLedger(Room &room, bool create) {
 auto *p=room.findChild<QObject *>("isolated-round34-fixed-distance",Qt::FindDirectChildrenOnly);
 if (!p && create) return new FixedDistanceLedger(room);
 return static_cast<FixedDistanceLedger *>(p);
}
bool roster(Room &room,const Player *p) {
 return p && room.findPlayerByObjectName(p->objectName(),true)==p;
}
void connectPlayer(FixedDistanceLedger *state, const Player *p) {
 if (state->connected.contains(p)) return;
 state->connected.insert(p);
 QObject::connect(p,&Player::gameplay_property_changed,state,[state,p]{state->invalidate(p);});
 QObject::connect(p,&QObject::destroyed,state,[state,p]{
  state->invalidate(p);state->connected.remove(p);
  // ServerPlayer is the only admitted viewer. Avoid casts during destruction.
  for(auto it=state->rows.begin();it!=state->rows.end();) {
   if(static_cast<const Player *>(it.key())==p) it=state->rows.erase(it);else ++it;
  }
 });
}
}
void observeAIFixedDistance(Room &room,Player *from,const Player *to,int distance,
                                  bool set,const QList<ServerPlayer *> &notifiedPlayers) {
 if(!roster(room,from)||!roster(room,to))return;
 auto *state=fixedDistanceLedger(room,true);
 connectPlayer(state,from);connectPlayer(state,to);
 // A removal gives no proof of a remaining constant. Never inspect the whole map.
 for(auto &sources:state->rows) sources[from].remove(to);
 if(!set || from->fixedDistanceCount(to,distance)<1)return;
 for(auto *viewer:notifiedPlayers) {
  if(!roster(room,viewer))continue;
  connectPlayer(state,viewer);
  state->rows[viewer][from].insert(to);
 }
}
void projectAIFixedDistanceWitness(AIWorldView &world,Room &room,const ServerPlayer *viewer) {
 QJsonObject result;
 const auto *state=fixedDistanceLedger(room,false);
 if(state&&roster(room,viewer)&&world.self.objectName==viewer->objectName()) {
  const auto sources=state->rows.value(viewer);
  for(auto it=sources.cbegin();it!=sources.cend();++it) {
   if(!roster(room,it.key()))continue;
   QJsonObject row;
   for(const auto *target:it.value())if(roster(room,target))row.insert(target->objectName(),true);
   if(!row.isEmpty())result.insert(it.key()->objectName(),row);
  }
 }
 world.strategyFacts.insert("distance_fixed_witness",result);
}

// ---- visible-hand-flags ----
#include <QJsonArray>

void projectAIVisibleHandFlags(AIWorldView &world, Room &room, const ServerPlayer *viewer)
{
    if (!viewer || viewer->getRoom() != &room || world.self.objectName != viewer->objectName()) return;
    QJsonObject rows;
    auto append = [&](const AIPlayerView &view) {
        const ServerPlayer *owner = room.findPlayerByObjectName(view.objectName, true);
        if (!owner || owner->getRoom() != &room) return;
        QSet<int> disclosed;
        const auto &known = owner == viewer ? world.handCards : view.knownCards;
        for (const AICardView &card : known) disclosed.insert(card.effectiveId);
        const QString flag = QStringLiteral("visible_%1_%2").arg(viewer->objectName(), owner->objectName());
        QJsonArray ids;
        bool complete = true;
        for (const Card *card : owner->getHandcards()) {
            if (!card || !(card->hasFlag(QStringLiteral("visible")) || card->hasFlag(flag))) continue;
            const int id = card->getEffectiveId();
            // Never enlarge the lawful face domain, including after an event
            // ledger revoked a stale view. Missing evidence is not an empty hand.
            if (!disclosed.contains(id)) { complete = false; break; }
            ids.append(id);
        }
        QJsonObject row{{QStringLiteral("known"), complete}};
        if (complete) row.insert(QStringLiteral("ids"), ids);
        rows.insert(view.objectName, row);
    };
    append(world.self);
    for (const AIPlayerView &view : world.players) append(view);
    QJsonObject facts = world.strategyFacts.value(QStringLiteral("round31_choice")).toObject();
    facts.insert(QStringLiteral("visible_hand_ids"), rows);
    world.strategyFacts.insert(QStringLiteral("round31_choice"), facts);
}

// ---- public-skill-flags ----
namespace {
const QStringList names={"FiveFanjianPindianTarget","FourTianxiang",
    "feijiangtsTarget","luajueshengTarget"};
class Observations final:public QObject {
public:
 explicit Observations(Room &room):QObject(&room){}
 // Pointer identity, not player name: deletion never transfers observations to a
 // replacement player. Room owns this across isolated VM recreation.
 QHash<ServerPlayer *,QJsonObject> flags;
};
Observations *observations(Room &room,bool create=false){
 for(QObject *c:room.children())if(auto *o=dynamic_cast<Observations *>(c))return o;
 return create?new Observations(room):nullptr;
}
void watch(Observations *o,ServerPlayer *p){
 if(o->flags.contains(p))return;
 o->flags.insert(p,{});
 QObject::connect(p,&QObject::destroyed,o,[o,p]{o->flags.remove(p);});
}
QJsonObject values(Room &room,ServerPlayer *viewer,const QString &key){
 QJsonObject rows;const auto *o=observations(room);
 for(ServerPlayer *p:room.getPlayers()){
  if(p==viewer){rows.insert(p->objectName(),QJsonObject{{"status","exact"},
   {"value",p->hasFlag(key)},{"basis","current_owner_flag"}});continue;}
  if(!o)continue;
  const auto observation=o->flags.value(p).value(key);
  if(!observation.isBool())continue;
  // Deliberately no current native hasFlag test, revision stamp, private
  // mutation listener, or conditional invalidation here. Those would reveal
  // unseen writes through a changed value OR a changed known/unknown status.
  rows.insert(p->objectName(),QJsonObject{{"status","estimate"},
   {"value",observation},{"basis","skill_flag_last_public_observation"}});
 }
 return {{"viewer",viewer->objectName()},{"flags",QJsonObject{{key,rows}}}};
}
void publish(AIWorldView &w,Room &room,ServerPlayer *viewer,const QString &key){
 w.strategyFacts.remove("round41_skill_flags");
 if(key.isEmpty()||!viewer||viewer->getRoom()!=&room
  ||w.self.objectName!=viewer->objectName()||room.roomRuntime()->isClosing())return;
 w.strategyFacts.insert("round41_skill_flags",values(room,viewer,key));
}
}
void initializeAIPublicSkillFlags(Room &room,ServerPlayer *p){
 if(!p||p->getRoom()!=&room)return;
 auto *o=observations(room,true);if(o->flags.contains(p))return;watch(o,p);
 // The call site is ServerPlayer's constructor after the Player base has
 // initialized its empty flag set. A late/nonempty call grants no baseline.
 if(!p->getFlagList().isEmpty())return;
 QJsonObject initial;for(const auto &name:names)initial.insert(name,false);
 o->flags.insert(p,initial);
}
void observeAIPublicSkillFlag(Room &room,ServerPlayer *p,const QString &operation){
 if(!p||p->getRoom()!=&room||!room.getPlayers().contains(p))return;
 const QString key=operation.startsWith('-')?operation.mid(1):operation;
 if(operation!="."&&!names.contains(key))return;
 auto *o=observations(room,true);watch(o,p);auto row=o->flags.value(p);
 // The notification itself is the observation. Never sample a foreign native
 // flag when reading the ledger. An unchanged repeated broadcast is still valid.
 if(operation==".")for(const auto &name:names)row.insert(name,false);
 else row.insert(key,!operation.startsWith('-'));
 o->flags.insert(p,row);
}
void projectAIPublicSkillFlagQuestion(AIRequest &r,Room &room,ServerPlayer *viewer){
 r.worldView.strategyFacts.remove("round41_skill_flags");
 if(!viewer||r.viewerObjectName!=viewer->objectName()
  ||r.stateRevision!=room.roomRuntime()->stateRevision())return;
 if(r.kind==AIRequest::RespondCard&&r.choiceOptions.question=="askForPindian"
  &&r.choiceOptions.reason=="FiveFanjian")
  publish(r.worldView,room,viewer,"FiveFanjianPindianTarget");
}
void projectAIPublicSkillFlagEvent(AIWorldView &w,Room &room,ServerPlayer *viewer,
 const AIEventView &event){
 w.strategyFacts.remove("round41_skill_flags");
 // Keep existing actor-private ChoiceMade admission. Do not turn internal
 // skill invoke/choice notifications into a broadcast to additional viewers.
 if(!viewer||event.triggerEvent!=ChoiceMade||event.kind!="choice"
  ||event.from!=viewer->objectName()||!event.privateEvent
  ||event.privateViewer!=viewer->objectName())return;
 const QString kind=event.details.value("choice_kind").toString();QString key;
 if(kind=="skillChoice"&&event.reason=="FourTianxiang")key="FourTianxiang";
 if(kind=="skillInvoke"&&event.reason=="feijiangts")key="feijiangtsTarget";
 if(kind=="skillInvoke"&&event.reason=="luajuesheng")key="luajueshengTarget";
 publish(w,room,viewer,key);
}

// ---- public-general-slots ----
#include <QPointer>

namespace {
struct SlotWitness {
    QPointer<ServerPlayer> player;
    QString publicName;
};
// Room-child lifetime; no process-global map, cross-room thread data, native
// card/userdata retention, or gameplay revision mutation.
class GeneralSlotLedger final : public QObject {
public:
    explicit GeneralSlotLedger(Room &room) : QObject(&room) {
        setObjectName(QStringLiteral("isolated-ai-round28-public-general-slots"));
    }
    QHash<QString, SlotWitness> deputy;
};
GeneralSlotLedger *publicGeneralSlotsLedger(Room &room, bool create) {
    auto *object=room.findChild<QObject *>(QStringLiteral("isolated-ai-round28-public-general-slots"),Qt::FindDirectChildrenOnly);
    auto *result=dynamic_cast<GeneralSlotLedger *>(object);
    if (!result && create && !object) result=new GeneralSlotLedger(room);
    return result;
}
bool rosterMember(Room &room, ServerPlayer *player) {
    return player && player->getRoom()==&room && room.getAllPlayers(true).contains(player);
}
}

void recordAIPublicGeneralSlot(Room &room, ServerPlayer *player,
                                     const char *property, const QString &value) {
    if (!property || QString::fromLatin1(property)!=QStringLiteral("general2")
        || !rosterMember(room,player)) return;
    auto *state=publicGeneralSlotsLedger(room,true);
    if (!state) return;
    const QString sent=value.isEmpty() ? player->property(property).toString() : value;
    state->deputy.insert(player->objectName(), SlotWitness{player,sent});
}

QJsonValue aiShownAllGenerals(Room &room, ServerPlayer *viewer,
                                   ServerPlayer *player) {
    if (!rosterMember(room,viewer) || !rosterMember(room,player)) return QJsonValue(QJsonValue::Undefined);
    if (viewer==player) return player->hasShownAllGenerals();
    if (!player->hasShownGeneral()) return false;
    if (player->hasShownGeneral2()) return true;
    const auto *state=publicGeneralSlotsLedger(room,false);
    if (!state) return QJsonValue(QJsonValue::Undefined);
    const auto it=state->deputy.constFind(player->objectName());
    if (it==state->deputy.cend() || it->player!=player || it->publicName.isEmpty())
        return QJsonValue(QJsonValue::Undefined);
    // Equality validates already-public existence; no new foreign face is
    // returned. Nonempty general2 makes (actual_general2 ? actual : general2)
    // nonnull regardless of the private actual slot. Public empty alone cannot
    // prove the private slot absent and is deliberately insufficient for true.
    if (!player->getGeneral2() || player->getGeneral2Name()!=it->publicName)
        return QJsonValue(QJsonValue::Undefined);
    return false;
}

// ---- intention-events ----
void projectAIIntentionWorld(AIWorldView &world, Room &room, ServerPlayer *viewer)
{
    if (!viewer || viewer->getRoom()!=&room || world.self.objectName!=viewer->objectName()) return;
    QJsonObject roots, cycles;
    for (ServerPlayer *start : room.getAllPlayers(true)) {
        QSet<QString> seen;
        ServerPlayer *current=start;
        bool cycle=false;
        while (current) {
            const QString name=current->objectName();
            if (seen.contains(name)) { cycle=true; break; }
            seen.insert(name);
            const QString next=current->getTag("Controller_Name").toString();
            if (next.isEmpty()) break;
            // Fixed SmartAI uses the default alive-only lookup, unlike the
            // native Room controller getter's includeDead=true lookup.
            ServerPlayer *other=room.findPlayerByObjectName(next);
            if (!other) break;
            current=other;
        }
        roots.insert(start->objectName(),current ? current->objectName() : start->objectName());
        cycles.insert(start->objectName(),cycle);
    }
    world.strategyFacts.insert("intention_controller",QJsonObject{{"roots",roots},{"cycles",cycles}});
}

void projectAIIntentionEvent(AIEventView &event, const AIWorldView &world, Room &room,
    ServerPlayer *observer, ServerPlayer *eventPlayer, const QVariant &data,
    const std::function<QJsonObject(const Card *)> &publicCard)
{
    if (!observer || observer->getRoom()!=&room || world.self.objectName!=observer->objectName()) return;
    if (data.canConvert<CardUseStruct>()) {
        const auto use=data.value<CardUseStruct>();
        // Remove the old shared scalar even when the actual struct is absent.
        event.details.remove("zenhui_user");
        if (use.from && use.card && use.from==observer) {
            event.details.insert("zenhui_user",use.from->hasFlag("ZenhuiUser_"+use.card->toString()));
            event.details.remove("zenhui_user_private");
        } else event.details.insert("zenhui_user_private",true);
    }
    if (event.triggerEvent!=ChoiceMade || data.userType()!=QMetaType::QString || !eventPlayer) return;
    const QStringList prompt=data.toString().split(':');
    const QString kind=prompt.value(0);
    if (kind=="playerChosen" || kind=="Yiji") {
        // Only the public kind/reason/recipient text. Yiji's fourth field is
        // deliberately excluded: its physical IDs need individual knowledge.
        event.details.insert("prompt_list",QJsonArray::fromStringList(prompt.mid(0,3)));
        if (kind=="playerChosen")
            event.details.insert("prompt_string_complete",prompt.size()==3);
    }
    if (kind=="viewCards" && observer==eventPlayer) {
        event.details.insert("view_cards_observed",true);
        QSet<int> ids;
        ServerPlayer *owner=room.findPlayerByObjectName(prompt.value(1));
        if (owner) for (const QString &part:prompt.value(2).split('+',Qt::SkipEmptyParts)) {
            bool ok=false;const int id=part.toInt(&ok);
            if (ok && owner->handCards().contains(id)) ids.insert(id);
        }
        event.details.insert("view_cards_observed_count",ids.size());
    }
    if (kind!="Yiji") return;
    QList<AICardView> known=world.handCards;
    known.append(world.self.equips);
    for (const auto &pile:world.self.piles) known.append(pile.cards);
    for (const auto &player:world.players) {
        known.append(player.knownCards);known.append(player.equips);known.append(player.judgingArea);
        for (const auto &pile:player.piles) known.append(pile.cards);
    }
    QJsonArray cards;bool complete=true;
    for (const QString &part:prompt.value(3).split('+',Qt::SkipEmptyParts)) {
        bool ok=false;const int id=part.toInt(&ok);
        const AICardView *snapshot=nullptr;
        if (ok && id>=0) for (const auto &card:known)
            if (!card.virtualCard && card.effectiveId==id) { snapshot=&card;break; }
        if (!snapshot) { complete=false;break; }
        const Card *card=room.getCard(id);
        if (!card || card->isVirtualCard() || card->getEffectiveId()!=id
            || card->objectName()!=snapshot->objectName || card->getClassName()!=snapshot->className
            || int(card->getSuit())!=snapshot->suit || card->getNumber()!=snapshot->number) { complete=false;break; }
        cards.append(publicCard(card));
    }
    event.details.insert("yiji_cards_complete",complete);
    if (complete) event.details.insert("yiji_cards",cards);
    else event.details.remove("yiji_cards");
}

// ---- midao-na ----
namespace {
class MidaoGlobalSnapshot final : public QObject {
public:
    MidaoGlobalSnapshot(Room &room, const QJsonObject &row) : QObject(&room), value(row) {}
    const QJsonObject value;
};
const MidaoGlobalSnapshot *snapshot(Room &room) {
    for (auto *child : room.children())
        if (auto *row = dynamic_cast<MidaoGlobalSnapshot *>(child)) return row;
    return nullptr;
}
}
void captureAIMidaoNaMetadata(Room &room) {
    if (snapshot(room)) return;
    auto &vm = room.roomRuntime()->lua();
    LuaRuntime::Binding binding(vm, false);
    auto *L = vm.state();
    QJsonObject row{{"known", false}, {"known_nil", false}};
    if (!L) { new MidaoGlobalSnapshot(room, row); return; }
    const int top = lua_gettop(L);
    lua_pushglobaltable(L);
    const int globals = lua_absindex(L, -1);
    lua_pushliteral(L, "na"); lua_rawget(L, globals);
    const bool absent = lua_isnil(L, -1); lua_pop(L, 1);
    bool plainLookup = true;
    if (absent && lua_getmetatable(L, globals)) {
        lua_pushliteral(L, "__index"); lua_rawget(L, -2);
        plainLookup = lua_isnil(L, -1); lua_pop(L, 2);
    }
    bool source = false;
    lua_pushliteral(L, "SmartAI"); lua_rawget(L, globals);
    if (lua_istable(L, -1)) {
        const int klass = lua_absindex(L, -1);
        // middleclass.lua27-43 installs instance methods in __instanceDict,
        // not the raw class table. Real AI instances use this exact dictionary
        // (middleclass.lua100). Authenticate its finite layout without calling
        // the class/static __index closures or any strategy callback.
        lua_pushliteral(L, "__instanceDict"); lua_rawget(L, klass);
        if (lua_istable(L, -1)) {
            const int dictionary = lua_absindex(L, -1);
            lua_pushliteral(L, "__index"); lua_rawget(L, dictionary);
            const bool instanceLookup = lua_rawequal(L, -1, dictionary);
            lua_pop(L, 1);
            bool assignmentLookup = false;
            if (lua_getmetatable(L, klass)) {
                lua_pushliteral(L, "__newindex"); lua_rawget(L, -2);
                assignmentLookup = lua_rawequal(L, -1, dictionary);
                lua_pop(L, 2);
            }
            row.insert("instance_dictionary", instanceLookup && assignmentLookup);
            lua_pushliteral(L, "ajustDamage"); lua_rawget(L, dictionary);
            if (instanceLookup && assignmentLookup && lua_isfunction(L, -1)) {
                lua_Debug info{}; lua_pushvalue(L, -1);
                if (lua_getinfo(L, ">S", &info)) {
                    const QString file = QString::fromUtf8(info.source ? info.source : "");
                    source = file.endsWith("/smart-ai.lua") && info.linedefined == 8180
                        && QString::fromLatin1(info.what ? info.what : "") == "Lua";
                    row.insert("source", file); row.insert("line", info.linedefined);
                }
            }
        }
    }
    // Fixed-load writer audit: both smart-ai na tables and mobile-ai's na are
    // lexical locals; no global na writer/restore exists in the audited AI load.
    // Never sample a mutable gameplay global at decision time.
    row.insert("known", plainLookup && source);
    row.insert("known_nil", plainLookup && source && absent);
    lua_settop(L, top);
    new MidaoGlobalSnapshot(room, row);
}
void projectAIMidaoNaMetadata(AIWorldView &world, Room &room) {
    const auto *row = snapshot(room);
    world.strategyFacts.insert("legacy_midao_na", row ? row->value
        : QJsonObject{{"known", false}, {"known_nil", false}});
}
