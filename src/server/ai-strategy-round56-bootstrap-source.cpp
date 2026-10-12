#include "ai-strategy-round56-playloop-filter.h"
#include "ai-strategy-round56-patterns.h"
#include "ai-strategy-round56-bootstrap-source.h"
#include "ai-strategy-round53-lianzhan.h"
#include "ai-strategy-round56-async.h"
#include "ai-strategy-round56-bootstrap-capture-metadata.h"
#include "ai-strategy-round56-bootstrap-declared-lists.h"
#include "ai-strategy-round56-bootstrap-horse-factories.h"
#include "ai-strategy-round56-require-bindings.h"
#include "ai-strategy-round56-host.h"
#include "ai-strategy-round45-native-seal.h"
#include "settings.h"
#include "engine.h"
#include "ai-runtime.h"
#include <QHash>
#include <QSet>
#include <QJsonObject>
#include "ai-strategy-round45-code-seal.h"
#include "ai-strategy-round45-lua-inspect.h"
#include "ai-strategy-round56-priority-bootstrap.h"
#include "ai-strategy-round56-clone-lineage.h"
#include "ai-strategy-round56-mode-evaluation.h"
#include "ai-strategy-round56-projection-lineage.h"
#include "player-self-source-origin.h"
#include "room.h"
#include "room-runtime.h"
#include "room-definition-registry.h"
#include "lua-runtime.h"
#include <QObject>
#include <QPointer>
#include <cstring>
#include <cstdio>
#include <QElapsedTimer>

namespace {
// Complete source/effect premises are recorded in issuer-activation-v3/AUDIT.md.
// Runtime checks only preserve/refuse that original lexical proof. They never
// infer authority from a pending finalizer, account result, mind or cache value.
struct State final : QObject {
 QElapsedTimer diagnosticTimer;
 explicit State(Room &r,LuaRuntime &rt,LuaRuntime &a):QObject(&r),room(r),runtime(rt),auxiliary(a),lifetime(&r){diagnosticTimer.start();}
 ~State() override {
  clearAIRound56PlayLoopSource(room);
  clearAIRound56PatternsLineage(room);
  runtime.removeLoadedChunkObserver(this);
  runtime.removeBootstrapAsyncObserver(this);
  auxiliary.removeLoadedChunkObserver(this);
 }
 Room &room;LuaRuntime &runtime;LuaRuntime &auxiliary;QPointer<QObject> lifetime;
 lua_State *L=nullptr;quint64 generation=0,serial=0;
 const void *globals=nullptr,*space=nullptr,*auxGlobals=nullptr;
 lua_State *auxL=nullptr;quint64 auxGeneration=0,hostEpoch=0;
 const CClosure *originalRequire=nullptr;
 bool preludeProved=false,auxBorn=false;
 const char *failureTag="none",*lastSource="none";
 bool pinning=false,sourcesReady=false;
 enum Phase { Installed, Born, Prepared, Completing, Completed, Retired } phase=Installed;
 bool tokenIssued=false;
};
void traceProgress(const State &s,const char *phase,const char *source="none") noexcept {
 // Opt-in fixed descriptor diagnostics only; never an authority condition.
 if(qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")) {
  std::fprintf(stderr,"R56_BOOTSTRAP_PROGRESS elapsed_ms=%lld phase=%s source=%s\n",
    static_cast<long long>(s.diagnosticTimer.elapsed()),phase,source);
  std::fflush(stderr);
 }
}
State *find(const Room &r) {
 for(auto *c:r.children())if(auto *s=dynamic_cast<State *>(c))return s;
 return nullptr;
}
void retire(State &s,const char *tag="unclassified-entry") noexcept {
 if(s.phase!=State::Retired)s.failureTag=tag;
 s.phase=State::Retired;
 retireAIRound56PriorityBootstrap(s.room);
 retireAIRound56CloneInputLineage(s.room,s.L);
 retireAIRound56ModeEvaluationLineage(s.room,s.L);
 retireAIRound56ProjectionLineage(s.room,s.L);
 retireAIRound56PatternsLineage(s.room,s.L);
 retireAIRound56PlayLoopSource(s.room,s.L);
 if(s.L)retireAIRound56AsyncUniverse(s.L);
}
bool current(Room &r,lua_State *L,quint64 serial) noexcept {
 auto *s=find(r);
 return s&&s->lifetime.data()==&r&&s->L==L&&serial&&s->serial==serial
  &&(s->phase==State::Completing||s->phase==State::Completed)
  &&s->runtime.rawState()==L&&s->runtime.generation()==s->generation
  &&s->runtime.isCurrentThreadOwner()&&s->hostEpoch
  &&s->hostEpoch==aiRound56ServerMessageSinkEpoch()&&proveAIRound56ServerLogEffects(r);
}
void birth(Room &r,const LuaRuntime::NativeNamespaceBirth &b) noexcept {
 auto *s=find(r);if(!s)return;
 try {
  if(s->phase!=State::Installed||!b.isCurrent()
    ||!hasAIRound56EngineSelfNullSourceOrigin()) {retire(*s);return;}
  s->L=b.state();s->generation=b.generation();s->serial=1;s->globals=b.globalsIdentity();s->space=b.namespaceIdentity();s->phase=State::Born;
  // Birth observers are metadata-only. Source compilation comes later, after
  // Binding and the complete predeclared allocation/finalizer union proof.
  if(!beginAIRound56PriorityBootstrap(r,b)
    ||!beginAIRound56CloneInputLineage(r,b)
    ||!beginAIRound56ModeEvaluationLineage(r,b)
    ||!beginAIRound56ProjectionLineage(r,b)
    ||!beginAIRound56PatternsLineage(r,b)
    ||!beginAIRound56PlayLoopSource(r,b))retire(*s);
 }catch(...){retire(*s);}
}
struct Source {const char *path,*sha;};
constexpr Source sourceRows[]={
#include "ai-strategy-round56-bootstrap-source-rows.inc"
};
constexpr Source auxiliaryRows[]={
#include "ai-strategy-round56-bootstrap-aux-rows.inc"
};
template<size_t N> const Source *sourceFor(const Proto *p,const Source (&rows)[N]) {
 if(!p||!p->source||getstr(p->source)[0]!='@')return nullptr;
 QByteArray hint(getstr(p->source)+1,qsizetype(tsslen(p->source)-1));
 while(hint.startsWith("./"))hint.remove(0,2);
 // sanguosha.lua's fixed dir="" translation arm appends a second slash.
 // This only selects the independent descriptor; executable Proto and _ENV
 // checks below remain mandatory. No file/current-value trust is added.
 if(hint.startsWith("lang/zh_CN//"))hint.remove(11,1);
 // The source string routes a lookup only; the independent full executable
 // Proto and original upvalue binding are both checked below.
 for(const auto &row:rows)if(hint==row.path)return &row;
 return nullptr;
}
bool rootEnvironment(const void *globals,const LClosure *cl) noexcept {
 if(!cl||cl->nupvalues!=1||cl->p->sizeupvalues!=1||!cl->upvals[0])return false;
 const auto *name=cl->p->upvalues[0].name;
 const auto *v=cl->upvals[0]->v.p;
 return name&&tsslen(name)==4&&!std::memcmp(getstr(name),"_ENV",4)
  &&ttistable(v)&&hvalue(v)==globals;
}
bool root(State &s,lua_State *L,int index) {
 const auto *v=AIRound45Inspect::value(L,index);
 if(!ttisLclosure(v))return false;
 const auto *cl=clLvalue(v);const auto *row=sourceFor(cl->p,sourceRows);
 return row&&cl->p->linedefined==0&&cl->p->lastlinedefined==0
  &&rootEnvironment(s.globals,cl)
  &&matchAIRound45PinnedFunction(s.room,L,index,QString::fromLatin1(row->path),0,0);
}
bool pinSources(State &s) {
 // Caller must have proved the WHOLE native allocation/destructor universe
 // before entering this scope. Compilation does not execute authored chunks.
 if(s.sourcesReady||s.pinning)return false;
 s.pinning=true;
 try {
  int sourceCount=0;
  for(const auto &row:sourceRows) {
   if(sourceCount++%25==0)traceProgress(s,"pin-game",row.path);
   if(!registerAIRound45PinnedSource(s.room,s.L,QString::fromLatin1(row.path),QByteArray(row.sha))) {
    s.lastSource=row.path;s.pinning=false;return false;
   }
  }
  sourceCount=0;
  for(const auto &row:auxiliaryRows) {
   if(sourceCount++%25==0)traceProgress(s,"pin-aux",row.path);
   if(!registerAIRound45PinnedSource(s.room,s.L,QString::fromLatin1(row.path),QByteArray(row.sha))) {
    s.lastSource=row.path;s.pinning=false;return false;
   }
  }
  traceProgress(s,"pins-complete");
  s.pinning=false;s.sourcesReady=true;return true;
 }catch(...){s.pinning=false;throw;}
}
void loaded(Room &r,lua_State *L,int kind) noexcept {
 auto *s=find(r);if(!s||s->phase==State::Retired)return;
 try {
  if(s->L!=L||kind!=5) {retire(*s);return;}
  if(s->pinning) {
   // Only the lexical native pin compiler can enter this state; no authored
   // entry is admitted while pinning. Its newly parsed chunks are discarded.
   if(s->phase!=State::Born||!s->preludeProved)retire(*s,"compiler-outside-proved-prelude");
   return;
  }
  if(!s->sourcesReady||s->phase!=State::Prepared||!root(*s,L,-1)) {
   retire(*s,"unsealed-gameplay-root");return;
  }
  s->lastSource=sourceFor(clLvalue(AIRound45Inspect::value(L,-1))->p,sourceRows)->path;
  traceProgress(*s,"game-root-ready",s->lastSource);
 }catch(...){retire(*s);}
}
// All inspections below borrow live TValue/closure metadata. They do not push
// onto the Lua stack, run a metamethod, convert userdata or traverse mind data.
const TValue *rawField(const Table *t,const char *key) noexcept {
 if(!t)return nullptr;const size_t len=std::strlen(key);
 for(int i=0;i<allocsizenode(t);++i){const Node *n=gnode(t,i);
  if(isempty(gval(n))||!keyiscollectable(n)
    ||(keytt(n)!=ctb(LUA_VSHRSTR)&&keytt(n)!=ctb(LUA_VLNGSTR)))continue;
  const TString *k=gco2ts(keyval(n).gc);
  if(tsslen(k)==len&&!std::memcmp(getstr(k),key,len))return gval(n);
 }
 return nullptr;
}
bool stringEquals(const TValue *v,const QByteArray &s) noexcept {
 return v&&ttisstring(v)&&tsslen(tsvalue(v))==size_t(s.size())
  &&!std::memcmp(getstr(tsvalue(v)),s.constData(),size_t(s.size()));
}
struct Capture {const char *name;int sourceBinding;
 enum Kind {Function,ModeTable,SourceScalar,NativeRequire} kind;int first,last;};
struct Closure {const char *path;int first,last;const Capture *captures;int count;};
#include "ai-strategy-round56-bootstrap-captures.inc"
const Closure *closureRow(const char *path,int first,int last) noexcept {
 for(const auto &row:closures)if(row.first==first&&row.last==last&&!std::strcmp(row.path,path))return &row;
 return nullptr;
}
struct CaptureGraph {
 State &s;QSet<const LClosure *> visited;
 QHash<QString,const UpVal *> bindings;
 bool no(const Closure &row,const char *stage,const char *capture="none") const noexcept {
  if(qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")) {
   // Only independent descriptor labels; no capture contents or runtime names.
   std::fprintf(stderr,"R56_BOOTSTRAP_CAPTURE_REFUSAL source=%s first=%d last=%d stage=%s capture=%s\n",row.path,row.first,row.last,stage,capture);
   std::fflush(stderr);
  }
  return false;
 }
 bool check(const LClosure *cl,const Closure &row) {
  if(!cl||visited.size()>64||!matchAIRound56PinnedClosureMetadata(s.room,s.L,cl,
       QString::fromLatin1(row.path),row.first,row.last))return no(row,"proto-or-budget");
  if(visited.contains(cl))return true;
  visited.insert(cl);int matched=0;bool environment=false;
  for(int i=0;i<cl->nupvalues;++i){
   if(i>=cl->p->sizeupvalues||!cl->upvals[i]||!cl->p->upvalues[i].name)return no(row,"missing-upvalue-metadata");
   const TString *name=cl->p->upvalues[i].name;const UpVal *up=cl->upvals[i];const TValue *v=up->v.p;
   if(tsslen(name)==4&&!std::memcmp(getstr(name),"_ENV",4)){
    if(environment||!ttistable(v)||hvalue(v)!=s.globals)return no(row,"original-environment");environment=true;continue;
   }
   const Capture *expected=nullptr;
   for(int j=0;j<row.count;++j)if(tsslen(name)==std::strlen(row.captures[j].name)
      &&!std::memcmp(getstr(name),row.captures[j].name,tsslen(name))){expected=&row.captures[j];break;}
   if(!expected)return no(row,"unexpected-capture");++matched;
   const QString binding=QString::fromLatin1(row.path)+':'+QString::number(expected->sourceBinding);
   auto previous=bindings.constFind(binding);
   if(previous!=bindings.cend()&&*previous!=up)return no(row,"source-binding-alias",expected->name);
   bindings.insert(binding,up);
   switch(expected->kind){
   case Capture::Function:{
    const auto *child=closureRow(row.path,expected->first,expected->last);
    if(!child||!ttisLclosure(v)||!check(clLvalue(v),*child))return no(row,"function-capture",expected->name);break;
   }
   case Capture::ModeTable:
    if(!ttistable(v)||!hasAIRound56BootstrapModeTableOrigin(s.room,s.L,hvalue(v)))return no(row,"mode-table-origin",expected->name);break;
   case Capture::NativeRequire:
    if(!ttisCclosure(v)||clCvalue(v)!=s.originalRequire)return no(row,"native-require",expected->name);break;
   case Capture::SourceScalar:
    // The shared generation UpVal is authenticated by its lexical producer
    // and complete source write history. Do not inspect its current value.
    break;
   }
  }
  return matched==row.count||no(row,"capture-count");
 }
};
bool synchronous(State &s,lua_State *L,int argc) {
 if(L!=s.L||s.phase!=State::Prepared||!s.sourcesReady||argc<0||argc>=lua_gettop(L))return false;
 const int index=lua_gettop(L)-argc;const TValue *v=AIRound45Inspect::value(L,index);
 // Original post-rules lexical registration, exact zero-upvalue native thunk.
 // Its one write replaces only the source-born CardUseStruct meta __gc with
 // the already-predeclared canonical native destructor. No authored code runs.
 if(argc==0&&matchAIRound56LianzhanBootstrapEntry(s.room,L,argc))return true;
 if(!ttisLclosure(v))return false;const LClosure *cl=clLvalue(v);
 if(argc==0)return root(s,L,index);
 CaptureGraph graph{s,{},{}};
 if(argc==1){
  const auto *row=closureRow("lua/sanguosha.lua",47,49);
  if(!row||!graph.check(cl,*row))return false;
  // This native transport iterates the already-proved public fixed list. Both
  // actual path and full captured package_chunk/original_require are checked.
  const auto *path=AIRound45Inspect::value(L,index+1);
  for(const QString &expected:Sanguosha->rulesDeclaredList(QStringLiteral("package_ai")))
   if(stringEquals(path,expected.toUtf8()))return true;
  return false;
 }
 if(argc==4){
  const auto *row=closureRow("lua/ai/mode-ai.lua",1217,1272);
  const TValue *registration=rawField(static_cast<const Table *>(s.space),"registerModeAI");
  const auto *registerRow=closureRow("lua/ai/mode-ai.lua",175,204);
  const bool captures=row&&registerRow&&graph.check(cl,*row)&&registration&&ttisLclosure(registration)
   &&graph.check(clLvalue(registration),*registerRow);
  const bool transport=stringEquals(AIRound45Inspect::value(L,index+1),s.room.getMode().toUtf8())
   &&ttistrue(AIRound45Inspect::value(L,index+2))
   &&ttisfalse(AIRound45Inspect::value(L,index+3))
   &&ttisfalse(AIRound45Inspect::value(L,index+4));
  if((!captures||!transport)&&qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")) {
   std::fprintf(stderr,"R56_BOOTSTRAP_MODE_REFUSAL captures=%d public_transport=%d\n",int(captures),int(transport));std::fflush(stderr);
  }
  return captures&&transport;
 }
 return false;
}
bool asynchronous(State &s,lua_State *L,int kind) noexcept {
 // Entire allocation/destructor union is proved before the first compiler
 // allocation. Looking at this pending callback can only reject that proof.
 if(L!=s.L||!s.preludeProved||s.phase==State::Retired||s.phase==State::Completed
   ||(s.phase!=State::Born&&s.phase!=State::Prepared&&s.phase!=State::Completing)
   ||kind!=3||lua_gettop(L)<2||!luaQsgsStandardWarningSink(L))return false;
 return aiRound56NativeFinalizerNonwriter(L,-2,-1,false,true)
   ||luaQsgsStandardIoFinalizer(L,-2,-1)||luaQsgsStandardLoadlibFinalizer(L,-2,-1);
}
void entry(Room &r,lua_State *L,int kind) noexcept {
 auto *s=find(r);if(!s||s->phase==State::Retired)return;
 try {
  if(!s->preludeProved||s->hostEpoch!=aiRound56ServerMessageSinkEpoch()){
   retire(*s,"bootstrap-source-host-changed");return;
  }
  const bool okay=kind<0?synchronous(*s,L,-(kind+1)):asynchronous(*s,L,kind);
  if(!okay) {
   if(kind<0&&qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")) {
    const int argc=-(kind+1);const char *descriptor="unknown-descriptor";
    int first=-1,last=-1;bool exactBody=false;const char *type="non-lua";
    if(argc>=0&&argc<lua_gettop(L)) {
     const TValue *v=AIRound45Inspect::value(L,lua_gettop(L)-argc);
     if(ttisLclosure(v)) {
      type="lua";const LClosure *cl=clLvalue(v);
      if(const Source *row=sourceFor(cl->p,sourceRows)) {
       descriptor=row->path;
       exactBody=matchAIRound56PinnedClosureMetadata(r,L,cl,
        QString::fromLatin1(row->path),cl->p->linedefined,cl->p->lastlinedefined);
       // Only independently pinned public anchors appear in diagnostics.
       if(exactBody){first=cl->p->linedefined;last=cl->p->lastlinedefined;}
      }
     }
    }
    std::fprintf(stderr,"R56_BOOTSTRAP_REJECTED_ENTRY argc=%d type=%s source=%s exact_body=%d first=%d last=%d\n",
      argc,type,descriptor,int(exactBody),first,last);std::fflush(stderr);
   }
   retire(*s,kind<0?"protected-capture-or-transport":"unproved-bootstrap-async");
  }
 }catch(...){retire(*s,"entry-metadata-exception");}
}
void auxiliaryBirth(Room &r,const LuaRuntime::NativeNamespaceBirth &b) noexcept {
 auto *s=find(r);if(!s||s->phase==State::Retired)return;
 if(s->phase!=State::Prepared||!s->preludeProved||s->auxBorn||!b.isCurrent()
   ||b.state()!=s->auxiliary.rawState()) {retire(*s,"auxiliary-not-original-birth");return;}
 s->auxBorn=true;s->auxL=b.state();s->auxGeneration=b.generation();s->auxGlobals=b.globalsIdentity();
}
void auxiliaryLoaded(Room &r,lua_State *L,int kind) noexcept {
 auto *s=find(r);if(!s||s->phase==State::Retired)return;
 try {
  if(s->phase!=State::Prepared||!s->preludeProved||!s->sourcesReady||!s->auxBorn
    ||L!=s->auxL||L!=s->auxiliary.rawState()||s->auxGeneration!=s->auxiliary.generation()||kind!=5){
   retire(*s,"auxiliary-load-outside-original-prefix");return;
  }
  const TValue *v=AIRound45Inspect::value(L,-1);
  if(!ttisLclosure(v)){retire(*s,"auxiliary-root-not-lua");return;}
  const auto *cl=clLvalue(v);const auto *row=sourceFor(cl->p,auxiliaryRows);
  if(!row||!rootEnvironment(s->auxGlobals,cl)
    ||!matchAIRound56PinnedAuxiliaryRoot(r,L,QString::fromLatin1(row->path))){
   retire(*s,"unsealed-auxiliary-root");return;
  }
  s->lastSource=row->path;
 }catch(...){retire(*s,"auxiliary-metadata-exception");}
}
bool nativePrelude(State &s) {
#if !defined(QSAN_ENGINE_BUILD) || defined(QSAN_XP_LEGACY)
 return false;
#else
 // Fixed SERVER headless native implementation excludes popen/encoding arms.
 // This is the original lexical birth, not a fresh scan of an already used VM.
 if(!Sanguosha||Sanguosha->currentRoomContext()!=s.room.roomRuntime()
   ||!hasAIRound56EngineSelfNullSourceOrigin()||!Config.EnableAI||Config.EnableHegemony
   ||(s.room.getMode()!=QStringLiteral("02p")&&s.room.getMode()!=QStringLiteral("04p"))
   ||s.room.getScenario()||Sanguosha->isCustomGameMode(s.room.getMode())
   ||!matchAIRound56BootstrapDeclaredLists(*Sanguosha)
   ||!matchAIRound56BootstrapHorseFactories(*Sanguosha)
   ||!proveAIRound56ServerLogEffects(s.room)
   ||!aiRound56RegisteredFinalizerUniverse(s.L)||!luaQsgsStandardWarningSink(s.L))return false;
 // Fresh global/native require graph comes from fixed openlibs+compatibility.
 // No cache contents are inspected here. Every subsequent module/cache write
 // belongs to the complete sealed source induction in the loader audit.
 if(!lua_checkstack(s.L,32))return false;
 const int top=lua_gettop(s.L);AIRound45Inspect::globals(s.L);
 AIRound45Inspect::field(s.L,-1,"require");
 const TValue *require=AIRound45Inspect::value(s.L,-1);
 const bool okay=ttisCclosure(require)&&luaQsgsStandardRequireBindings(s.L,-1);
 if(okay)s.originalRequire=clCvalue(require);
 lua_settop(s.L,top);s.hostEpoch=aiRound56ServerMessageSinkEpoch();
 return okay&&s.hostEpoch;
#endif
}

}

bool installAIRound56BootstrapSourceBoundary(Room &r) noexcept {
 try {
  if(find(r))return false;auto *rt=r.roomRuntime();if(!rt)return false;
  auto *s=new State(r,rt->lua(),rt->ai().lua());
  if(!s->runtime.installNativeNamespaceBirthObserver(s,[&r](const LuaRuntime::NativeNamespaceBirth &b){birth(r,b);})
    ||!s->runtime.installLoadedChunkObserver(s,[&r](lua_State *L,int k){loaded(r,L,k);})
    ||!s->runtime.installBootstrapAsyncObserver(s,[&r](lua_State *L,int k){entry(r,L,k);})
    ||!s->auxiliary.installNativeNamespaceBirthObserver(s,[&r](const LuaRuntime::NativeNamespaceBirth &b){auxiliaryBirth(r,b);})
    ||!s->auxiliary.installLoadedChunkObserver(s,[&r](lua_State *L,int k){auxiliaryLoaded(r,L,k);})) {
   retire(*s);return false;
  }
  return true;
 }catch(...){if(auto *s=find(r))retire(*s);return false;}
}
bool prepareAIRound56BootstrapSourceBoundary(Room &r,lua_State *L) noexcept {
 auto *s=find(r);if(!s)return false;
 if(s->phase!=State::Born||s->L!=L||s->runtime.rawState()!=L
   ||s->runtime.generation()!=s->generation||!s->runtime.isCurrentThreadOwner()
   ) {retire(*s,"prepare-not-original-birth");return false;}
 // Source pins are independent compilation, but can run GC. The complete
 // union above is therefore required before pinSources is even entered.
 try {
  traceProgress(*s,"native-prelude-begin");
  if(!nativePrelude(*s)){retire(*s,"native-prelude-unproved");return false;}
  traceProgress(*s,"native-prelude-complete");
  s->preludeProved=true;
  if(!pinSources(*s)){retire(*s,"source-pin-failed");return false;}
  if(!prepareAIRound56ModeEvaluationSources(r,L)){retire(*s,"mode-source-prepare");return false;}
  if(!prepareAIRound56ProjectionSources(r,L)){retire(*s,"projection-source-prepare");return false;}
  if(!prepareAIRound56PatternsSources(r,L)){retire(*s,"patterns-source-prepare");return false;}
  if(!prepareAIRound56PlayLoopSource(r,L)){retire(*s,"playloop-source-prepare");return false;}
  if(s->phase!=State::Born){retire(*s,"retired-during-source-prepare");return false;}
  s->phase=State::Prepared;traceProgress(*s,"prepared");return true;
 }catch(...){retire(*s);return false;}
}
std::unique_ptr<AIRound56BootstrapSourceProof>
proveAIRound56BootstrapSourceUnion(Room &r,lua_State *L) noexcept {
 auto *s=find(r);
 if(!s||!s->preludeProved||s->phase!=State::Completing||s->tokenIssued
   ||!current(r,L,s->serial))return {};
 try {
  s->tokenIssued=true;
  return std::unique_ptr<AIRound56BootstrapSourceProof>(new AIRound56BootstrapSourceProof(
   r,L,s->generation,s->serial,current,true));
 }catch(...){retire(*s);return {};}
}
bool completeAIRound56BootstrapSourceBoundary(Room &r,lua_State *L) noexcept {
 auto *s=find(r);if(!s)return false;
 try {
  if(s->phase!=State::Prepared||s->L!=L||!s->preludeProved) {retire(*s);return false;}
  s->phase=State::Completing;
  auto proof=proveAIRound56BootstrapSourceUnion(r,L);
  if(!proof){retire(*s,"completion-source-token");return false;}
  if(!completeAIRound56FactoryMapOrigin(r,L,*proof)){retire(*s,"completion-factory-maps");return false;}
  if(!completeAIRound56PriorityBootstrap(r,L,*proof)){retire(*s,"completion-priority");return false;}
  if(!completeAIRound56CloneInputLineage(r,L,*proof)){retire(*s,"completion-clone");return false;}
  if(!completeAIRound56ProjectionLineage(r,L,*proof)){retire(*s,"completion-projection");return false;}
  if(!completeAIRound56ModeEvaluationLineage(r,L,*proof)){retire(*s,"completion-mode");return false;}
  if(!completeAIRound56PatternsLineage(r,L,*proof)){retire(*s,"completion-patterns");return false;}
  if(!completeAIRound56PlayLoopSource(r,L,*proof)){retire(*s,"completion-playloop");return false;}
  if(!initializeAIRound56AsyncUniverse(r,L,*proof)){retire(*s,"completion-async");return false;}
  s->phase=State::Completed;
  s->runtime.removeBootstrapAsyncObserver(s);
  // Retain the parsed-code invalidator; ordinary request continuity is owned
  // by the shared exact graph/entry observers after this lexical boundary.
  return true;
 }catch(...){retire(*s);return false;}
}
void retireAIRound56BootstrapSourceBoundary(Room &r) noexcept {if(auto *s=find(r))retire(*s);}
void clearAIRound56BootstrapSourceBoundary(Room &r) noexcept {delete find(r);}

bool acceptAIRound56BootstrapAsyncEntry(lua_State *L,int kind) noexcept {
 // Room's independent observer has already seen this exact entry and may have
 // retired. Resolve only the current public native Room binding; no new seed.
 if(!Sanguosha)return false;auto *r=Sanguosha->currentRoom();if(!r)return false;
 auto *s=find(*r);if(!s)return false;
 if(asynchronous(*s,L,kind))return true;
 return false;
}
bool acceptAIRound56BootstrapProtectedEntry(Room &r,lua_State *L,int arguments) noexcept {
 auto *s=find(r);if(!s||s->phase!=State::Prepared||!s->preludeProved)return false;
 try{return synchronous(*s,L,arguments);}catch(...){retire(*s,"ordinary-entry-metadata-exception");return false;}
}
QJsonObject inspectAIRound56BootstrapSourceBoundary(const Room &r) {
 const auto *s=find(r);if(!s)return {{"phase","absent"},{"failureTag","not-installed"}};
 static const char *phases[]={"installed","born","prepared","completing","completed","retired"};
 return {{"phase",QString::fromLatin1(phases[int(s->phase)])},
         {"failureTag",QString::fromLatin1(s->failureTag)},
         {"source",QString::fromLatin1(s->lastSource)},
         {"priority",inspectAIRound56PriorityBootstrap(const_cast<Room&>(r))}};
}

bool hasAIRound56BootstrapMetadataContinuity(Room&r,lua_State*L)noexcept{
 auto*s=find(r);if(!s||!s->preludeProved||!s->tokenIssued||!current(r,L,s->serial)
  ||!hasAIRound56CloneInputLineage(r,L))return false;
 return s->phase==State::Completing||(s->phase==State::Completed&&hasAIRound56AsyncUniverse(r,L));
}

bool isAIRound56OriginalDefinitionProducer(Room &r,const RoomDefinitionRegistry *definitions) noexcept {
 auto *s=find(r);auto *rt=r.roomRuntime();
 return s&&rt&&definitions==&rt->definitions()&&s->phase==State::Prepared
  &&s->preludeProved&&rt->isLoadingDefinitions()&&!rt->isClosing()
  &&s->L&&s->L==rt->lua().rawState()&&s->generation==rt->lua().generation()
  &&rt->lua().isCurrentThreadOwner()&&Sanguosha->currentRoomContext()
  &&Sanguosha->currentRoomContext()->roomRuntime()==rt;
}
bool completeAIRound56FactoryMapOrigin(Room &r,lua_State *L,const AIRound56BootstrapSourceProof &proof) noexcept {
 auto *rt=r.roomRuntime();if(!rt)return false;auto &defs=rt->definitions();
 const bool ok=defs.m_factorySourcePhase==RoomDefinitionRegistry::FactorySourcePhase::Collecting
  &&defs.m_factorySourceRoom==&r&&L&&rt->lua().rawState()==L
  &&!proof.m_consumed&&proof.m_roomLifetime.data()==&r&&proof.m_room==&r
  &&proof.m_lua==L&&proof.m_generation==rt->lua().generation()
  &&proof.m_serial&&proof.m_current&&proof.m_current(r,L,proof.m_serial);
 defs.m_factorySourcePhase=ok?RoomDefinitionRegistry::FactorySourcePhase::Sealed
                            :RoomDefinitionRegistry::FactorySourcePhase::Retired;
 if(ok)defs.m_factorySourceGeneration=rt->lua().generation();
 return ok;
}
