#include "ai-strategy/runtime.h"

// ---- bootstrap-source-bootstrap-write ----
#include "ai-strategy/lua-seal.h"
extern "C" {
#include "lstate.h"
#include "lopcodes.h"
}
#include <cstring>
#include <new>

bool matchAIBootstrapWrite(Room&r,lua_State*L,const lua_TableObservation&e,
 const QString&path,int line,const char*key,bool genesis,int first,int last)noexcept{
 try{
  if(!L||!key||e.event!=LUA_TABLE_OBSERVER_WRITE||!e.string_key
   ||e.string_key_length!=std::strlen(key)||std::memcmp(e.string_key,key,e.string_key_length))return false;
  CallInfo*ci=L->ci;
  if(genesis&&(e.value_type!=LUA_TTABLE||!e.value_table_identity))return false;
  const bool bridge=genesis&&!isLua(ci);
  if(bridge){
   if(!matchAINativeNamespaceWriteFrame(r,L,e.table_identity,QStringLiteral("sgs"),key))return false;
   ci=ci->previous;
  }else if(!(e.flags&(LUA_TABLE_OBSERVER_VM_SLOW|LUA_TABLE_OBSERVER_VM_FAST))||!e.lua_function_identity)return false;
  if(!ci||!isLua(ci)||!ttisLclosure(s2v(ci->func.p)))return false;
  const auto*cl=clLvalue(s2v(ci->func.p));const auto*p=cl->p;
  if(!bridge&&cl!=e.lua_function_identity)return false;
  int pc=e.lua_instruction_index;
  if(bridge){
   if(ci->u.l.savedpc<=p->code||ci->u.l.savedpc>p->code+p->sizecode)return false;
   pc=int(ci->u.l.savedpc-p->code)-1;
  }
  if(!matchAIPinnedInstruction(r,L,cl,pc,path,first,last,line))return false;
  const Instruction ins=p->code[pc];
  const auto*base=ci->func.p+1;
  if(GETARG_A(ins)>=p->maxstacksize)return false;
  const TValue*operand=nullptr;
  if(GET_OPCODE(ins)==OP_SETFIELD){
   if(GETARG_B(ins)>=p->sizek)return false;
   operand=&p->k[GETARG_B(ins)];
  }else if(GET_OPCODE(ins)==OP_SETTABLE){
   // luaK_indexed lowers a literal key outside MAXARG_B to a register.
   // The full pinned Proto and independently trusted source PC were already
   // authenticated above; inspect the actual operand, never a current cell.
   if(GETARG_B(ins)>=p->maxstacksize)return false;
   operand=s2v(base+GETARG_B(ins));
  }else return false;
  if(!ttisstring(operand)||tsslen(tsvalue(operand))!=std::strlen(key)
    ||std::memcmp(getstr(tsvalue(operand)),key,std::strlen(key)))return false;
  const auto*target=s2v(base+GETARG_A(ins));
  if(!ttistable(target)||hvalue(target)!=e.table_identity)return false;
  if(genesis){
   if(GETARG_k(ins)||GETARG_C(ins)>=p->maxstacksize)return false;
   const auto*value=s2v(base+GETARG_C(ins));
   if(!ttistable(value)||hvalue(value)!=e.value_table_identity||hvalue(value)->metatable)return false;
  }
  return true;
 }catch(const std::bad_alloc&){return false;}
}

// ---- bootstrap-source ----
#include "ai-strategy/tenyear-ai.h"
#include "settings.h"
#include "engine.h"
#include "ai-runtime.h"
#include <QHash>
#include <QSet>
#include <QJsonObject>
#include "ai-strategy/lua-raw-inspect.h"
#include "ai-strategy/source-slots.h"
#include "player-self-source-origin.h"
#include "room.h"
#include "room-runtime.h"
#include "room-definition-registry.h"
#include "lua-runtime.h"
#include <QObject>
#include <QPointer>
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
  clearAIPlayLoopSource(room);
  clearAICardPatternsLineage(room);
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
 retireAIPriorityBootstrap(s.room);
 retireAICloneInputLineage(s.room,s.L);
 retireAIModeEvaluationLineage(s.room,s.L);
 retireAIProjectionLineage(s.room,s.L);
 retireAICardPatternsLineage(s.room,s.L);
 retireAIPlayLoopSource(s.room,s.L);
 if(s.L)retireAIAsyncUniverse(s.L);
}
bool current(Room &r,lua_State *L,quint64 serial) noexcept {
 auto *s=find(r);
 return s&&s->lifetime.data()==&r&&s->L==L&&serial&&s->serial==serial
  &&(s->phase==State::Completing||s->phase==State::Completed)
  &&s->runtime.rawState()==L&&s->runtime.generation()==s->generation
  &&s->runtime.isCurrentThreadOwner()&&s->hostEpoch
  &&s->hostEpoch==aiServerMessageSinkEpoch()&&proveAIServerLogEffects(r);
}
void birth(Room &r,const LuaRuntime::NativeNamespaceBirth &b) noexcept {
 auto *s=find(r);if(!s)return;
 try {
  if(s->phase!=State::Installed||!b.isCurrent()
    ||!hasAIEngineSelfNullSourceOrigin()) {retire(*s);return;}
  s->L=b.state();s->generation=b.generation();s->serial=1;s->globals=b.globalsIdentity();s->space=b.namespaceIdentity();s->phase=State::Born;
  // Birth observers are metadata-only. Source compilation comes later, after
  // Binding and the complete predeclared allocation/finalizer union proof.
  if(!beginAIPriorityBootstrap(r,b)
    ||!beginAICloneInputLineage(r,b)
    ||!beginAIModeEvaluationLineage(r,b)
    ||!beginAIProjectionLineage(r,b)
    ||!beginAICardPatternsLineage(r,b)
    ||!beginAIPlayLoopSource(r,b))retire(*s);
 }catch(...){retire(*s);}
}
struct BootstrapSourceSource {const char *path,*sha;};
constexpr BootstrapSourceSource sourceRows[]={
#include "ai-strategy/bootstrap-source-rows.inc"
};
constexpr BootstrapSourceSource auxiliaryRows[]={
#include "ai-strategy/bootstrap-source-aux-rows.inc"
};
template<size_t N> const BootstrapSourceSource *sourceFor(const Proto *p,const BootstrapSourceSource (&rows)[N]) {
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
 const auto *v=AILuaInspect::value(L,index);
 if(!ttisLclosure(v))return false;
 const auto *cl=clLvalue(v);const auto *row=sourceFor(cl->p,sourceRows);
 return row&&cl->p->linedefined==0&&cl->p->lastlinedefined==0
  &&rootEnvironment(s.globals,cl)
  &&matchAIPinnedFunction(s.room,L,index,QString::fromLatin1(row->path),0,0);
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
   if(!registerAIPinnedSource(s.room,s.L,QString::fromLatin1(row.path),QByteArray(row.sha))) {
    s.lastSource=row.path;s.pinning=false;return false;
   }
  }
  sourceCount=0;
  for(const auto &row:auxiliaryRows) {
   if(sourceCount++%25==0)traceProgress(s,"pin-aux",row.path);
   if(!registerAIPinnedSource(s.room,s.L,QString::fromLatin1(row.path),QByteArray(row.sha))) {
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
  s->lastSource=sourceFor(clLvalue(AILuaInspect::value(L,-1))->p,sourceRows)->path;
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
#include "ai-strategy/bootstrap-source-captures.inc"
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
  if(!cl||visited.size()>64||!matchAIPinnedClosureMetadata(s.room,s.L,cl,
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
    if(!ttistable(v)||!hasAIBootstrapModeTableOrigin(s.room,s.L,hvalue(v)))return no(row,"mode-table-origin",expected->name);break;
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
 const int index=lua_gettop(L)-argc;const TValue *v=AILuaInspect::value(L,index);
 // Original post-rules lexical registration, exact zero-upvalue native thunk.
 // Its one write replaces only the source-born CardUseStruct meta __gc with
 // the already-predeclared canonical native destructor. No authored code runs.
 if(argc==0&&matchAILianzhanBootstrapEntry(s.room,L,argc))return true;
 if(!ttisLclosure(v))return false;const LClosure *cl=clLvalue(v);
 if(argc==0)return root(s,L,index);
 CaptureGraph graph{s,{},{}};
 if(argc==1){
  const auto *row=closureRow("lua/sanguosha.lua",47,49);
  if(!row||!graph.check(cl,*row))return false;
  // This native transport iterates the already-proved public fixed list. Both
  // actual path and full captured package_chunk/original_require are checked.
  const auto *path=AILuaInspect::value(L,index+1);
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
  const bool transport=stringEquals(AILuaInspect::value(L,index+1),s.room.getMode().toUtf8())
   &&ttistrue(AILuaInspect::value(L,index+2))
   &&ttisfalse(AILuaInspect::value(L,index+3))
   &&ttisfalse(AILuaInspect::value(L,index+4));
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
 return aiNativeFinalizerNonwriter(L,-2,-1,false,true)
   ||luaQsgsStandardIoFinalizer(L,-2,-1)||luaQsgsStandardLoadlibFinalizer(L,-2,-1);
}
void entry(Room &r,lua_State *L,int kind) noexcept {
 auto *s=find(r);if(!s||s->phase==State::Retired)return;
 try {
  if(!s->preludeProved||s->hostEpoch!=aiServerMessageSinkEpoch()){
   retire(*s,"bootstrap-source-host-changed");return;
  }
  const bool okay=kind<0?synchronous(*s,L,-(kind+1)):asynchronous(*s,L,kind);
  if(!okay) {
   if(kind<0&&qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")) {
    const int argc=-(kind+1);const char *descriptor="unknown-descriptor";
    int first=-1,last=-1;bool exactBody=false;const char *type="non-lua";
    if(argc>=0&&argc<lua_gettop(L)) {
     const TValue *v=AILuaInspect::value(L,lua_gettop(L)-argc);
     if(ttisLclosure(v)) {
      type="lua";const LClosure *cl=clLvalue(v);
      if(const BootstrapSourceSource *row=sourceFor(cl->p,sourceRows)) {
       descriptor=row->path;
       exactBody=matchAIPinnedClosureMetadata(r,L,cl,
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
  const TValue *v=AILuaInspect::value(L,-1);
  if(!ttisLclosure(v)){retire(*s,"auxiliary-root-not-lua");return;}
  const auto *cl=clLvalue(v);const auto *row=sourceFor(cl->p,auxiliaryRows);
  if(!row||!rootEnvironment(s->auxGlobals,cl)
    ||!matchAIPinnedAuxiliaryRoot(r,L,QString::fromLatin1(row->path))){
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
   ||!hasAIEngineSelfNullSourceOrigin()||!Config.EnableAI||Config.EnableHegemony
   ||(s.room.getMode()!=QStringLiteral("02p")&&s.room.getMode()!=QStringLiteral("04p"))
   ||s.room.getScenario()||Sanguosha->isCustomGameMode(s.room.getMode())
   ||!matchAIBootstrapDeclaredLists(*Sanguosha)
   ||!matchAIBootstrapHorseFactories(*Sanguosha)
   ||!proveAIServerLogEffects(s.room)
   ||!aiRegisteredFinalizerUniverse(s.L)||!luaQsgsStandardWarningSink(s.L))return false;
 // Fresh global/native require graph comes from fixed openlibs+compatibility.
 // No cache contents are inspected here. Every subsequent module/cache write
 // belongs to the complete sealed source induction in the loader audit.
 if(!lua_checkstack(s.L,32))return false;
 const int top=lua_gettop(s.L);AILuaInspect::globals(s.L);
 AILuaInspect::field(s.L,-1,"require");
 const TValue *require=AILuaInspect::value(s.L,-1);
 const bool okay=ttisCclosure(require)&&luaQsgsStandardRequireBindings(s.L,-1);
 if(okay)s.originalRequire=clCvalue(require);
 lua_settop(s.L,top);s.hostEpoch=aiServerMessageSinkEpoch();
 return okay&&s.hostEpoch;
#endif
}

}

bool installAIBootstrapSourceBoundary(Room &r) noexcept {
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
bool prepareAIBootstrapSourceBoundary(Room &r,lua_State *L) noexcept {
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
  if(!prepareAIModeEvaluationSources(r,L)){retire(*s,"mode-source-prepare");return false;}
  if(!prepareAIProjectionSources(r,L)){retire(*s,"projection-source-prepare");return false;}
  if(!prepareAICardPatternsSources(r,L)){retire(*s,"patterns-source-prepare");return false;}
  if(!prepareAIPlayLoopSource(r,L)){retire(*s,"playloop-source-prepare");return false;}
  if(s->phase!=State::Born){retire(*s,"retired-during-source-prepare");return false;}
  s->phase=State::Prepared;traceProgress(*s,"prepared");return true;
 }catch(...){retire(*s);return false;}
}
std::unique_ptr<AIBootstrapSourceProof>
proveAIBootstrapSourceUnion(Room &r,lua_State *L) noexcept {
 auto *s=find(r);
 if(!s||!s->preludeProved||s->phase!=State::Completing||s->tokenIssued
   ||!current(r,L,s->serial))return {};
 try {
  s->tokenIssued=true;
  return std::unique_ptr<AIBootstrapSourceProof>(new AIBootstrapSourceProof(
   r,L,s->generation,s->serial,current,true));
 }catch(...){retire(*s);return {};}
}
bool completeAIBootstrapSourceBoundary(Room &r,lua_State *L) noexcept {
 auto *s=find(r);if(!s)return false;
 try {
  if(s->phase!=State::Prepared||s->L!=L||!s->preludeProved) {retire(*s);return false;}
  s->phase=State::Completing;
  auto proof=proveAIBootstrapSourceUnion(r,L);
  if(!proof){retire(*s,"completion-source-token");return false;}
  if(!completeAIFactoryMapOrigin(r,L,*proof)){retire(*s,"completion-factory-maps");return false;}
  if(!completeAIPriorityBootstrap(r,L,*proof)){retire(*s,"completion-priority");return false;}
  if(!completeAICloneInputLineage(r,L,*proof)){retire(*s,"completion-clone");return false;}
  if(!completeAIProjectionLineage(r,L,*proof)){retire(*s,"completion-projection");return false;}
  if(!completeAIModeEvaluationLineage(r,L,*proof)){retire(*s,"completion-mode");return false;}
  if(!completeAICardPatternsLineage(r,L,*proof)){retire(*s,"completion-patterns");return false;}
  if(!completeAIPlayLoopSource(r,L,*proof)){retire(*s,"completion-playloop");return false;}
  if(!initializeAIAsyncUniverse(r,L,*proof)){retire(*s,"completion-async");return false;}
  s->phase=State::Completed;
  s->runtime.removeBootstrapAsyncObserver(s);
  // Retain the parsed-code invalidator; ordinary request continuity is owned
  // by the shared exact graph/entry observers after this lexical boundary.
  return true;
 }catch(...){retire(*s);return false;}
}
void retireAIBootstrapSourceBoundary(Room &r) noexcept {if(auto *s=find(r))retire(*s);}
void clearAIBootstrapSourceBoundary(Room &r) noexcept {delete find(r);}

bool acceptAIBootstrapAsyncEntry(lua_State *L,int kind) noexcept {
 // Room's independent observer has already seen this exact entry and may have
 // retired. Resolve only the current public native Room binding; no new seed.
 if(!Sanguosha)return false;auto *r=Sanguosha->currentRoom();if(!r)return false;
 auto *s=find(*r);if(!s)return false;
 if(asynchronous(*s,L,kind))return true;
 return false;
}
bool acceptAIBootstrapProtectedEntry(Room &r,lua_State *L,int arguments) noexcept {
 auto *s=find(r);if(!s||s->phase!=State::Prepared||!s->preludeProved)return false;
 try{return synchronous(*s,L,arguments);}catch(...){retire(*s,"ordinary-entry-metadata-exception");return false;}
}
QJsonObject inspectAIBootstrapSourceBoundary(const Room &r) {
 const auto *s=find(r);if(!s)return {{"phase","absent"},{"failureTag","not-installed"}};
 static const char *phases[]={"installed","born","prepared","completing","completed","retired"};
 return {{"phase",QString::fromLatin1(phases[int(s->phase)])},
         {"failureTag",QString::fromLatin1(s->failureTag)},
         {"source",QString::fromLatin1(s->lastSource)},
         {"priority",inspectAIPriorityBootstrap(const_cast<Room&>(r))}};
}

bool hasAIBootstrapMetadataContinuity(Room&r,lua_State*L)noexcept{
 auto*s=find(r);if(!s||!s->preludeProved||!s->tokenIssued||!current(r,L,s->serial)
  ||!hasAICloneInputLineage(r,L))return false;
 return s->phase==State::Completing||(s->phase==State::Completed&&hasAIAsyncUniverse(r,L));
}

bool isAIOriginalDefinitionProducer(Room &r,const RoomDefinitionRegistry *definitions) noexcept {
 auto *s=find(r);auto *rt=r.roomRuntime();
 return s&&rt&&definitions==&rt->definitions()&&s->phase==State::Prepared
  &&s->preludeProved&&rt->isLoadingDefinitions()&&!rt->isClosing()
  &&s->L&&s->L==rt->lua().rawState()&&s->generation==rt->lua().generation()
  &&rt->lua().isCurrentThreadOwner()&&Sanguosha->currentRoomContext()
  &&Sanguosha->currentRoomContext()->roomRuntime()==rt;
}
bool completeAIFactoryMapOrigin(Room &r,lua_State *L,const AIBootstrapSourceProof &proof) noexcept {
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

// ---- continuity-certificate ----
#include "ai-strategy/xufa.h"
#include "ai-strategy/ol-ai.h"
#include "ai-strategy/newgenerals.h"
#include "ai-strategy/context.h"
#include "ai-strategy/official.h"
#include "ai-strategy/native-registry.h"
#include "runtime-paths.h"
#include "lua.hpp"
#include "lobject.h"
#include <QCryptographicHash>
#include <QFile>
#include <QVector>
#include <QScopeGuard>

namespace {
constexpr const char *ledgerName = "AI.R45.ContinuityCertificate";
struct Stack {
 lua_State *L; int top;
 explicit Stack(lua_State *s):L(s),top(lua_gettop(s)){}
 ~Stack(){lua_settop(L,top);}
};
void raw(lua_State *L,int i,const char *key){AILuaInspect::field(L,i,key);}
bool plain(lua_State *L,int i){
 if(!lua_istable(L,i))return false;
 if(lua_getmetatable(L,i)){lua_pop(L,1);return false;}return true;
}
const Proto *proto(lua_State *L,int i){
 if(!lua_isfunction(L,i)||lua_iscfunction(L,i))return nullptr;
 return static_cast<const LClosure *>(lua_topointer(L,i))->p;
}

struct ContinuityCertificateSource {
 const char *path; const char *sha;
};
const ContinuityCertificateSource modeSource{"lua/ai/mode-ai.lua","f1f4b9ef3a5e9878bf0d66b521447e62c30f9e8a2a1cec94b155e9af65e7c8e5"};
const ContinuityCertificateSource smartSource{"lua/ai/smart-ai.lua","f92033319db3c6aac3504b4b6ddccf5edb4403fb120679b303dedfe673a3efe5"};
const ContinuityCertificateSource utilitiesSource{"lua/utilities.lua","b76cdc44c0fec46f1e10d918c6e0676c5e301ed92cd5b0e35da882606a7d4d3a"};
const ContinuityCertificateSource sanguoshaSource{"lua/sanguosha.lua","fb7ac93090859f607f17158ea9a9ad92c86479457f5193de8c3486d606c5e601"};
const ContinuityCertificateSource sgsExSource{"lua/sgs_ex.lua","786dfccf96d33c351f096f273af89cfd08d787affacb7a82bf305d87479cd315"};
class Certificate final:public QObject {
public:
 explicit Certificate(Room &r):QObject(&r),room(r){setObjectName(ledgerName);}
 Room &room; lua_State *L=nullptr; quint64 generation=0;
 bool initialized=false, modeReady=false, patternsReady=false, bossReady=false, naturalReady=false, correctionReady=false;
 void release(){L=nullptr;modeReady=false;patternsReady=false;bossReady=false;naturalReady=false;correctionReady=false;}

};
Certificate *ledger(Room &r,bool create=false){
 for(QObject *child:r.children())if(child->objectName()==ledgerName)
  return static_cast<Certificate *>(child);
 return create?new Certificate(r):nullptr;
}
// Audit domain: these bodies operate solely on native primitive mode snapshots
// and private primitive mode minds. The registration routines, real SmartAI
// wrappers and intention/event entrypoints are deliberately not roots. Nested
// closures created inside these exact bodies are included by complete Proto
// identity, not admitted by a path/line range.
const QList<int> modeAnchors={10,24,40,49,59,74,209,221,240,250,310,317,321,327,333,
 360,621,627,636,846,860,942,1017,1027,1039,1054,1158,1184,1193,1234,1240,1252,1262,1286,1307,1346,1364};

struct Check {
 Certificate &c; lua_State *L; QString failure; int budget=100000;
 QSet<const void *> functions,tables;
 explicit Check(Certificate &v):c(v),L(v.L){}
 bool no(const QString &where,const QString &why){if(failure.isEmpty())failure=where+": "+why;return false;}
 bool nativeBinding(int actual,const char *path,const QString &where){
  return matchAINativeBinding(c.room,L,actual,QString::fromLatin1(path))
    ?true:no(where,"native binding replaced or unaudited");
 }
 bool global(const QString &key,const QString &where,const Proto *caller){
  static const QSet<QString> values={"assert","error","getmetatable","ipairs","next","pairs","pcall",
   "rawequal","rawget","select","setmetatable","tonumber","tostring","type","unpack"};
  Stack stack(L);AILuaInspect::globals(L);raw(L,-1,key.toUtf8().constData());
  if(values.contains(key))return nativeBinding(-1,key.toUtf8().constData(),where);
  if(key=="sgs"&&caller->linedefined==1054){
   // Audited standard_intention reads only the native immutable enum.
   if(!lua_istable(L,-1))return no(where,"sgs namespace absent");
   raw(L,-1,"Player_Play");return matchAINativeConstant(c.room,L,-1,QStringLiteral("sgs.Player_Play"))
    ?true:no(where,"Player_Play enum replaced");
  }
  static const QHash<QString,QStringList> fields={
   {"math",{"abs","floor","ceil","max","min","huge","random"}},
   {"string",{"sub","find","match","gsub","format","len"}},
   {"table",{"insert","remove","sort","concat","unpack"}}};
  if(!fields.contains(key)||!plain(L,-1))return no(where,"unapproved global capability "+key);
  const int table=lua_absindex(L,-1);
  for(const auto &field:fields.value(key)){
   raw(L,table,field.toUtf8().constData());
   const QByteArray path=(key+"."+field).toUtf8();
   bool ok;
   if(key=="math"&&field=="huge")ok=matchAINativeConstant(c.room,L,-1,path);
   else ok=nativeBinding(-1,path.constData(),where+"."+field);
   lua_pop(L,1);if(!ok)return false;
  }
  return true;
 }
 bool environment(const Proto *p,int up,const QString &where){
  // Lua's emitted _ENV accesses must be literal GETTABUP. Passing _ENV as a
  // value, dynamic indexing or writing globals is outside this pure-mode proof.
  for(int n=0;n<p->sizecode;++n){
   const Instruction instruction=p->code[n];const OpCode op=GET_OPCODE(instruction);
   if(op==OP_GETUPVAL&&GETARG_B(instruction)==up)return no(where,"environment escapes as a value");
   if(op==OP_SETTABUP&&GETARG_A(instruction)==up)return no(where,"global write outside mode proof");
   if(op==OP_SETUPVAL&&GETARG_B(instruction)==up)return no(where,"environment replacement");
   if(op==OP_GETTABUP&&GETARG_B(instruction)==up){
    const int constant=GETARG_C(instruction);
    if(constant<0||constant>=p->sizek||!ttisstring(&p->k[constant]))return no(where,"nonliteral environment lookup");
    const auto *s=tsvalue(&p->k[constant]);
    if(!global(QString::fromUtf8(getstr(s),qsizetype(tsslen(s))),where,p))return false;
   }
  }
  return true;
 }
 bool value(int index,const QString &where,int depth=0,bool callbacks=true){
  if(--budget<0||depth>64)return no(where,"finite graph budget exceeded");
  index=lua_absindex(L,index);const int type=lua_type(L,index);
  if(type==LUA_TNIL||type==LUA_TBOOLEAN||type==LUA_TNUMBER||type==LUA_TSTRING)return true;
  if(type==LUA_TFUNCTION)return callbacks?function(index,where,depth+1):no(where,"function in primitive native argument");
  if(type!=LUA_TTABLE)return no(where,"userdata/thread/native capability in primitive graph");
  const void *identity=lua_topointer(L,index);if(tables.contains(identity))return true;tables.insert(identity);
  if(lua_getmetatable(L,index)){
   // Only weak-index bookkeeping created by the fixed module is permitted at
   // entry. Readonly proxies are local to an invocation and cannot be inputs.
   const int meta=lua_absindex(L,-1);bool weak=plain(L,meta);int fields=0;
   lua_pushnil(L);while(AILuaInspect::next(L,meta)){
    ++fields;weak=weak&&lua_type(L,-2)==LUA_TSTRING&&QString::fromUtf8(lua_tostring(L,-2))=="__mode"
      &&lua_type(L,-1)==LUA_TSTRING&&QString::fromUtf8(lua_tostring(L,-1))=="k";lua_pop(L,1);
   }lua_pop(L,1);
   if(!callbacks||!weak||fields!=1)return no(where,"unapproved metatable");
  }
  lua_pushnil(L);while(AILuaInspect::next(L,index)){
   if(!value(-2,where+".<key>",depth+1,callbacks)||!value(-1,where+".<value>",depth+1,callbacks)){
    lua_pop(L,2);return false;
   }lua_pop(L,1);
  }return true;
 }
 bool childEnvironments(const Proto *p,const QString &where){
  for(int i=0;i<p->sizep;++i){const Proto *child=p->p[i];
   for(int u=0;u<child->sizeupvalues;++u)
    if(child->upvalues[u].name&&!std::strcmp(getstr(child->upvalues[u].name),"_ENV")
       &&!environment(child,u,where+".<child>"))return false;
   if(!childEnvironments(child,where+".<child>"))return false;
  }return true;
 }
 bool primitiveMetatables(){
  Stack stack(L);
  for(int kind:{LUA_TNIL,LUA_TBOOLEAN,LUA_TNUMBER,LUA_TFUNCTION}){
   if(kind==LUA_TNIL)lua_pushnil(L);
   else if(kind==LUA_TBOOLEAN)lua_pushboolean(L,0);
   else if(kind==LUA_TNUMBER)lua_pushnumber(L,0);
   else {AILuaInspect::globals(L);raw(L,-1,"type");lua_remove(L,-2);}
   if(lua_getmetatable(L,-1))return no("primitive.metatable","unexpected primitive metamethod capability");
   lua_pop(L,1);
  }
  if(!AILuaInspect::primitiveMetatable(L,LUA_TSTRING)||!plain(L,-1))return no("string.metatable","missing native string metatable");
  const int meta=lua_absindex(L,-1);int count=0;
  lua_pushnil(L);while(AILuaInspect::next(L,meta)){
   ++count;if(lua_type(L,-2)!=LUA_TSTRING||std::strcmp(lua_tostring(L,-2),"__index"))return no("string.metatable","additional string metamethod");
   AILuaInspect::globals(L);raw(L,-1,"string");bool same=lua_rawequal(L,-1,-3);lua_pop(L,2);
   if(!same)return no("string.metatable","string lookup redirected");lua_pop(L,1);
  }
  // Pinned bodies also use primitive method syntax (name:sub/find/match),
  // which performs no GETTABUP string lookup. Seal those leaves regardless
  // of whether another source expression references the string global.
  if(count!=1)return false;
  AILuaInspect::globals(L);raw(L,-1,"string");const int strings=lua_absindex(L,-1);
  if(!plain(L,strings))return no("string.methods","method library redirected");
  for(const char *key:{"sub","find","match"}){raw(L,strings,key);
   bool ok=nativeBinding(-1,(QByteArray("string.")+key).constData(),"string.methods");lua_pop(L,1);if(!ok)return false;}
  return true;
 }
 bool function(int index,const QString &where,int depth){
  index=lua_absindex(L,index);const Proto *p=proto(L,index);
  if(!p||!matchAIPinnedFunctionFamily(c.room,L,index,QString::fromLatin1(modeSource.path),modeAnchors))return no(where,"Lua callback is outside pinned mode closure union");
  const void *identity=lua_topointer(L,index);if(functions.contains(identity))return true;functions.insert(identity);
  if(!childEnvironments(p,where))return false;
  for(int n=1;;++n){
   const char *name=lua_getupvalue(L,index,n);if(!name)break;
   bool ok;
   if(!std::strcmp(name,"_ENV")){
    AILuaInspect::globals(L);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);
    ok=ok&&environment(p,n-1,where+"._ENV");
   }else ok=value(-1,where+"."+QString::fromUtf8(name),depth+1);
   lua_pop(L,1);if(!ok)return false;
  }return true;
 }
};
bool exactEnvironment(lua_State *L,int fn){
 fn=lua_absindex(L,fn);Stack stack(L);
 for(int u=1;;++u){const char *name=lua_getupvalue(L,fn,u);if(!name)break;
  if(std::strcmp(name,"_ENV"))return false;
  AILuaInspect::globals(L);const bool same=lua_rawequal(L,-1,-2);lua_pop(L,2);if(!same)return false;
 }return true;
}
bool certifyPatterns(Certificate &c,int fn,int count,QString *reason){
 auto fail=[&](const char *why){if(reason)*reason=QString::fromLatin1(why);return false;};
 lua_State *L=c.L;Stack stack(L);Check check(c);
 if(count!=1||!check.value(fn+1,"patterns.argument",0,false))return fail("patterns argument is not a primitive native value");
 // The fixed native callers use true/string/nil. A table argument would be
 // truthy but cannot introduce an arbitrary aliased object into the proof.
 const int kind=lua_type(L,fn+1);
 if(kind!=LUA_TNIL&&kind!=LUA_TSTRING&&kind!=LUA_TBOOLEAN)return fail("patterns argument ABI changed");
 AILuaInspect::globals(L);const int globals=lua_absindex(L,-1);
 if(!plain(L,globals))return fail("patterns global assignment could invoke a metamethod");
 for(const char *name:{"type","ipairs"}){raw(L,globals,name);
  bool ok=matchAINativeBinding(c.room,L,-1,QString::fromLatin1(name));lua_pop(L,1);if(!ok)return fail("patterns native primitive binding changed");}
 raw(L,globals,"string");const int strings=lua_absindex(L,-1);if(!plain(L,strings))return fail("patterns string library redirected");
 raw(L,strings,"sub");if(!matchAINativeBinding(c.room,L,-1,"string.sub"))return fail("patterns string.sub changed");lua_pop(L,2);
 raw(L,globals,"table");const int tables=lua_absindex(L,-1);if(!plain(L,tables))return fail("patterns table library redirected");
 raw(L,tables,"insert");if(!matchAINativeBinding(c.room,L,-1,"table.insert"))return fail("patterns table.insert changed");lua_pop(L,1);
 raw(L,tables,"contains");
 if(!matchAIPinnedFunction(c.room,L,-1,"lua/utilities.lua",95,108)||!exactEnvironment(L,-1))
  return fail("patterns table.contains fixed code/environment changed");
 // The complete pinned patterns body always supplies exactly two arguments to
 // contains. The compare_objectName userdata branch is unreachable by syntax,
 // not by sampling a private value. Both table arguments below contain only
 // immutable primitive values; ipairs and equality cannot dispatch user code.
 lua_pop(L,2);
 if(!matchAINativeNamespaceProperty(c.room,L,"sgs","Sanguosha"))return fail("patterns engine namespace property changed");
 for(const char *method:{"getBanPackages","getCardCount","getEngineCard"})
  if(!matchAINativeClassMethod(c.room,L,"Engine",method))return fail("patterns Engine method changed");
 if(!matchAINativeDerivedMethods(c.room,L,"Card",{"objectName","getClassName","getPackage","getTypeId"}))
  return fail("patterns engine-card metadata method union changed");
 check.tables.clear();
 for(int u=1;;++u){const char *name=lua_getupvalue(L,fn,u);if(!name)break;bool ok=false;
  if(!std::strcmp(name,"_ENV"))ok=lua_rawequal(L,-1,globals);
  else if(!std::strcmp(name,"cached_ban_packages")||!std::strcmp(name,"card_names")||!std::strcmp(name,"class_to_name"))
   ok=plain(L,-1)&&check.value(-1,QString::fromLatin1(name),0,false);
  lua_pop(L,1);if(!ok)return fail("patterns private cache is not its finite primitive graph");
 }
 if(reason)reason->clear();return true;
 }
bool certifyBoss(Certificate &c,int fn,int count,QString *reason){
 auto fail=[&](const char *why){if(reason)*reason=QString::fromLatin1(why);return false;};
 lua_State *L=c.L;Stack stack(L);Check check(c);
 if(count!=2||!matchAINativeObjectMethods(c.room,L,fn+1,"LuaMaxCardsSkill",{"objectName"})
    ||!matchAINativeObjectMethods(c.room,L,fn+2,"Player",{"isLord","getMark","getGeneralName"}))
  return fail("BossMaxCards arguments/native getter bindings are not authentic");
 // The exact wrapper only reads spec.fixed_func and forwards its two arguments.
 // No other spec key, private role or mark outcome selects a different proof.
 bool found=false;
 for(int u=1;;++u){const char *name=lua_getupvalue(L,fn,u);if(!name)break;
  if(std::strcmp(name,"spec")||found||!plain(L,-1))return fail("BossMaxCards wrapper upvalues changed");
  found=true;raw(L,-1,"fixed_func");
  if(!matchAIPinnedFunction(c.room,L,-1,"lua/sanguosha.lua",171,176)||!exactEnvironment(L,-1))
   return fail("BossMaxCards fixed_func provider/environment changed");lua_pop(L,2);
 }
 if(!found||!check.primitiveMetatables())return fail("BossMaxCards primitive metamethod graph changed");
 AILuaInspect::globals(L);raw(L,-1,"string");const int strings=lua_absindex(L,-1);
 if(!plain(L,strings))return fail("BossMaxCards string library redirected");
 raw(L,strings,"startsWith");
 if(!matchAIPinnedFunction(c.room,L,-1,"lua/utilities.lua",164,168)||!exactEnvironment(L,-1))
  return fail("BossMaxCards startsWith source/environment changed");lua_pop(L,1);
 raw(L,strings,"find");if(!matchAINativeBinding(c.room,L,-1,"string.find"))return fail("BossMaxCards string.find changed");
 // Player::isLord's BOTH identity/hegemony branches read only native role,
 // shown bits and General name/lord metadata; getMark's AIMarkQueryScope is
 // a native thread-local scalar overlay. None enters Lua or touches an AI.
 if(reason)reason->clear();return true;
}
}

void initializeAIContinuityCertificate(Room &r){
 auto *c=ledger(r,true);if(c->initialized)return;c->initialized=true;
 auto *runtime=r.roomRuntime();if(!runtime)return;
 LuaRuntime::Binding binding(runtime->lua(),false);c->L=runtime->lua().state();c->generation=runtime->lua().generation();
 if(!c->L||!lua_checkstack(c->L,512))return;Stack stack(c->L);
 if(!registerAIPinnedSource(r,c->L,QString::fromLatin1(modeSource.path),QByteArray(modeSource.sha)))return;
 AILuaInspect::globals(c->L);raw(c->L,-1,"sgs");if(!lua_istable(c->L,-1))return;raw(c->L,-1,"evaluateModeAI");
 c->modeReady=matchAIPinnedFunction(r,c->L,-1,QString::fromLatin1(modeSource.path),1364,1530);
 if(!registerAIPinnedSource(r,c->L,smartSource.path,QByteArray(smartSource.sha))
    ||!registerAIPinnedSource(r,c->L,utilitiesSource.path,QByteArray(utilitiesSource.sha)))return;
 AILuaInspect::globals(c->L);raw(c->L,-1,"patterns");
 c->patternsReady=matchAIPinnedFunction(r,c->L,-1,smartSource.path,33,61);
 c->bossReady=registerAIPinnedSource(r,c->L,sanguoshaSource.path,QByteArray(sanguoshaSource.sha))
  &&registerAIPinnedSource(r,c->L,sgsExSource.path,QByteArray(sgsExSource.sha));
 c->naturalReady=registerAINaturalFilterSources(r,c->L);
 c->correctionReady=registerAIGetterCorrectionSources(r,c->L);
 registerAIXufaBodySources(r,c->L);
 registerAIXufaGetterSources(r,c->L);
 registerAIHeqiaSources(r,c->L);
 registerAILeiluanSources(r,c->L);
 registerAIJianmanSources(r,c->L);
 registerAIXufaSlotSources(r,c->L);
 registerAINewgeneralsPlanningSlotSources(r,c->L);
 registerAIOlClanPlanSlotSources(r,c->L);
}
void clearAIContinuityCertificate(Room &r){
 // This ledger owns only native bytes/identity metadata, never Lua references.
 // Teardown must not bind/inspect a closing gameplay VM.
 delete ledger(r);
 clearAIPinnedSources(r);
 clearAINativeBindings(r);
}
bool certifyAIContinuityEntry(Room &r,lua_State *L,int count,QString *reason){
 auto refuse=[&](const QString &why){if(reason)*reason=why;return false;};
 auto *c=ledger(r);auto *runtime=r.roomRuntime();
 if(!Sanguosha || Sanguosha->currentRoom()!=&r)
  return refuse("certificate Engine runtime Room is absent or foreign");
 if(!c||!runtime||!L||c->L!=L||runtime->lua().state()!=L||runtime->lua().generation()!=c->generation)
  return refuse("certificate runtime/generation is absent or foreign");
 if(count<0||lua_gettop(L)<count+1)return refuse("protected call frame is malformed");
 if(!runtime->lua().hasProtectedEntryObserver())return refuse("asynchronous pre-entry observer absent");
 if(!AILuaInspect::capacity(L,4096))return refuse("pre-reserved inspection stack unavailable");
 Stack stack(L);const int fn=lua_gettop(L)-count;
 if(c->naturalReady&&matchAIPinnedFunction(r,L,fn,smartSource.path,344,375))
  return certifyAINaturalFilterGraph(r,L,count,reason);
 if(c->bossReady&&matchAIPinnedFunction(r,L,fn,sgsExSource.path,404,406))
  return certifyBoss(*c,fn,count,reason);
 if(c->patternsReady&&matchAIPinnedFunction(r,L,fn,smartSource.path,33,61))
  return certifyPatterns(*c,fn,count,reason);
 if(!c->modeReady)return refuse("pinned mode root is unavailable");
 if(!matchAIPinnedFunction(r,L,fn,QString::fromLatin1(modeSource.path),1364,1530))
  return refuse("protected entry is outside the implemented finite root set");
 if(count<1||count>3)return refuse("mode argument count is outside native query ABI");
 Check check(*c);if(!check.primitiveMetatables())return refuse(check.failure);
 // Separate traversals prevent a callback-table visit from accidentally
// authorizing the same table when supplied as a primitive root argument.
 for(int i=1;i<=count;++i){check.tables.clear();if(!check.value(fn+i,"argument"+QString::number(i),0,false))return refuse(check.failure);}
 check.tables.clear();
 if(!check.function(fn,"evaluateModeAI",0))return refuse(check.failure);
 if(reason)reason->clear();return true;
}

void observeAIUnprovedNativeQuery(Room *room,bool preserveAiNoTo) noexcept {
 if(!room)return;
 // Only the separate complete R54 native public-definition proof can exempt aiNoTo.
 if(!preserveAiNoTo)retireAIAiNoTo(*room);
 // A privately skipped callback is not a proof of noninterference. These
 // existing-ledger epoch sinks also cover same-value and cached query paths.
 // A future complete-union verifier may replace this unconditional refusal;
 // an entered-callback subtype alone must never do so.
 interruptAIChoiceContinuityAsync(*room);
 interruptAIFaqiPlansAsync(*room);
}

// ---- async-universe-async ----
#include <QMutex>
namespace {
struct AsyncUniverseAsyncState final : QObject {
 explicit AsyncUniverseAsyncState(Room &r) : QObject(&r), room(&r) {}
 QPointer<Room> room;
 lua_State *lua = nullptr;
 quint64 generation = 0;
 quint64 hostEpoch = 0;
 QSet<const Table*> protectedTables;
 bool attempted = false, valid = false, freshLuaAIOrigin = false, gameplayVariantOrigin = false;
};
// Different Room initialization/gameplay VMs can insert/read concurrently.
// Protect the index only; never hold this lock over Lua, callbacks or QObject
// construction/destruction. State access still requires its owning VM/lifetime.
QMutex statesMutex;
QHash<lua_State *,QPointer<AsyncUniverseAsyncState>> states;
thread_local AIAsyncUniverseScope *active = nullptr;
AsyncUniverseAsyncState *asyncUniverseAsyncFind(lua_State *L) noexcept {
 if(!L)return nullptr;
 QMutexLocker lock(&statesMutex);
 return states.value(G(L)->mainthread,nullptr);
}
bool hook(lua_State *L) noexcept {
 return !lua_gethook(L) || LuaRuntime::InstructionBudgetScope::matches(L)
     || matchesAIOriginalConstructorHook(L);
}
bool finalizer(lua_State *L,int f,int o,bool fresh,bool variants) noexcept {
 return aiNativeFinalizerNonwriter(L,f,o,fresh,variants)
     || luaQsgsStandardIoFinalizer(L,f,o)
     || luaQsgsStandardLoadlibFinalizer(L,f,o);
}
bool seed(AsyncUniverseAsyncState&s,lua_State *L,QString *why) {
 const int top=lua_gettop(L);
 if(!AILuaInspect::capacity(L,8)){if(why)*why="async metadata stack capacity";return false;}
 const auto no=[&](const char*s){lua_settop(L,top);if(why)*why=QString::fromLatin1(s);return false;};
 if(!luaQsgsStandardWarningSink(L))return no("unknown Lua warning callback");
 if(!aiRegisteredFinalizerUniverse(L))return no("unproved registered allocation finalizer");
 AILuaInspect::field(L,LUA_REGISTRYINDEX,"SWIG");
 if(const auto*t=AILuaInspect::table(L,-1))s.protectedTables.insert(t);
 lua_settop(L,top);
 auto*g=G(L);
 if(ttistable(&g->l_registry))s.protectedTables.insert(hvalue(&g->l_registry));
 if(!hook(g->mainthread))return no("unproved main thread hook");
 for(auto*first:{g->allgc,g->finobj,g->tobefnz,g->fixedgc})for(auto*o=first;o;o=o->next){
  if(o->tt==LUA_VTHREAD&&!hook(gco2th(o)))return no("unproved thread hook");
  Table*mt=o->tt==LUA_VUSERDATA?gco2u(o)->metatable:o->tt==LUA_VTABLE?gco2t(o)->metatable:nullptr;
  if(mt)s.protectedTables.insert(mt);
  if(o->tt==LUA_VTABLE){
   // Also protect registered allocation metatables not yet used by an object.
   sethvalue2s(L,L->top.p,gco2t(o));++L->top.p;
   AILuaInspect::field(L,-1,"__gc");
   if(!lua_isnil(L,-1))s.protectedTables.insert(gco2t(o));
   lua_settop(L,top);
  }
 }
 for(auto*first:{g->finobj,g->tobefnz})for(auto*o=first;o;o=o->next){
  Table*mt=o->tt==LUA_VUSERDATA?gco2u(o)->metatable:o->tt==LUA_VTABLE?gco2t(o)->metatable:nullptr;
  if(!mt)continue;
  if(o->tt==LUA_VTABLE)s.protectedTables.insert(gco2t(o));
  sethvalue2s(L,L->top.p,mt);++L->top.p;
  AILuaInspect::field(L,-1,"__gc");
  if(!lua_isnil(L,-1)){
   setgcovalue(L,s2v(L->top.p),o);++L->top.p;
   // Fresh owned LuaAI is deliberately not admitted at bootstrap. Its later
   // allocation has a separate exact constructor/capture-origin obligation.
   if(!finalizer(L,-2,-1,false,s.gameplayVariantOrigin))return no("unproved bootstrap finalizer effect");
  }
  lua_settop(L,top);
 }
 return true;
}
}
AIBootstrapSourceProof::AIBootstrapSourceProof(
 Room&r,lua_State*L,quint64 generation,quint64 serial,Current current,bool gameplayVariants)
 :m_roomLifetime(&r),m_room(&r),m_lua(L),m_generation(generation),m_serial(serial),
  m_current(current),m_gameplayVariants(gameplayVariants){}
bool initializeAIAsyncUniverse(Room&,lua_State*,QString*why){
 if(why)*why="complete bootstrap source-union token required";
 return false;
}
bool initializeAIAsyncUniverse(Room&r,lua_State*L,
                                     AIBootstrapSourceProof&proof,QString*why){
 if(why)why->clear();
 auto*rt=r.roomRuntime();
 if(!L||!rt||rt->lua().state()!=L||proof.m_consumed||proof.m_room!=&r
    ||proof.m_roomLifetime.data()!=static_cast<QObject*>(&r)
    ||proof.m_lua!=L||proof.m_generation!=rt->lua().generation()
    ||!proof.m_serial||!proof.m_current||!proof.m_current(r,L,proof.m_serial)){
  if(why)*why="complete bootstrap source-union proof absent or retired";
  return false;
 }
 proof.m_consumed=true;
 AsyncUniverseAsyncState*s=asyncUniverseAsyncFind(L);if(s&&s->attempted){if(why)*why="async bootstrap already attempted";return false;}
 if(!s){s=new AsyncUniverseAsyncState(r);s->lua=L;QMutexLocker lock(&statesMutex);states.insert(L,s);}
 s->attempted=true;s->generation=r.roomRuntime()->lua().generation();
 s->hostEpoch=aiServerMessageSinkEpoch();
 s->gameplayVariantOrigin=proof.m_gameplayVariants;
 s->valid=seed(*s,L,why);return s->valid;
}
bool hasAIAsyncUniverse(const Room&r,lua_State*L) noexcept {
 // Room exposes its read-only runtime lookup through a nonconst override.
 auto*rt=const_cast<Room&>(r).roomRuntime();
 auto*s=asyncUniverseAsyncFind(L);return s&&s->valid&&s->room==&r&&s->lua==L
  &&rt&&rt->lua().generation()==s->generation
  &&s->hostEpoch==aiServerMessageSinkEpoch()
  &&proveAIServerLogEffects(r);
}
bool isAIAsyncProtectedTable(const Room&r,lua_State*L,int index) noexcept {
 auto*s=asyncUniverseAsyncFind(L);
 if(!hasAIAsyncUniverse(r,L)||!s)return true;
 const Table*t=AILuaInspect::table(L,index);
 return t&&s->protectedTables.contains(t);
}
void retireAIAsyncUniverse(lua_State*L) noexcept {if(auto*s=asyncUniverseAsyncFind(L))s->valid=false;}
AIAsyncUniverseScope::AIAsyncUniverseScope(Room&r,lua_State*L,Products products)
 :m_previous(active),m_lua(L){
 if(!hasAIAsyncUniverse(r,L))return;
 m_state=asyncUniverseAsyncFind(L);auto*s=static_cast<AsyncUniverseAsyncState*>(m_state);
 // This extends the SOURCE-PROVED allocation universe before allocation;
 // it does not inspect, adopt or sample a privately selected existing object.
 if(int(products)&int(FreshLuaAI))s->freshLuaAIOrigin=true;
 if(int(products)&int(GameplayVariants))s->gameplayVariantOrigin=true;
 m_active=true;active=this;
}
AIAsyncUniverseScope::~AIAsyncUniverseScope(){if(m_active)active=m_previous;}
void observeAIAsyncProtectedEntry(lua_State*L) noexcept {
 auto*s=asyncUniverseAsyncFind(L);
 if(!s)return;
 if(!active||active->m_state!=s||active->m_lua!=L)s->valid=false;
}
bool acceptAIAsyncEntry(lua_State*L,int kind) noexcept {
 auto*s=asyncUniverseAsyncFind(L);
 if(!s||!s->valid)return false;
 if(!active||active->m_state!=s||active->m_lua!=L){s->valid=false;return false;}
 if(!s->room||!hasAIAsyncUniverse(*s->room,L)){s->valid=false;return false;}
 if(kind==4){if(hook(L))return true;s->valid=false;return false;}
 if(kind!=3||lua_gettop(L)<2||!luaQsgsStandardWarningSink(L)){
  s->valid=false;return false;
 }
 // Defensive verification only: authorization came from the entire seeded
 // universe + allocation graph before selection, never this observed branch.
 if(finalizer(L,-2,-1,s->freshLuaAIOrigin,s->gameplayVariantOrigin))return true;
 s->valid=false;return false;
}

// ---- async-universe-host ----
#include <atomic>
#include <typeinfo>
namespace {
std::atomic<quint64> sinkEpoch{0};
std::atomic<bool> sinkKnown{false},defaultAttempted{false};
thread_local AIReviewedLogConnect *connection=nullptr;
}
void initializeAIDefaultMessageSink() noexcept {
 bool expected=false;
 if(defaultAttempted.compare_exchange_strong(expected,true)&&sinkEpoch.load()==0){
  sinkEpoch.store(1);sinkKnown.store(true);
 }
}
void recordAIServerMessageSink(bool installed) noexcept {
 sinkKnown.store(false);sinkEpoch.fetch_add(1);sinkKnown.store(installed);
}
quint64 aiServerMessageSinkEpoch() noexcept {
 return sinkKnown.load()?sinkEpoch.load():0;
}
bool proveAIServerLogEffects(const Room&r) noexcept {
 return typeid(r)==typeid(Room)
     &&aiServerMessageSinkEpoch()!=0&&r.logConnectionsReviewed();
}
AIReviewedLogConnect::AIReviewedLogConnect(const QObject*s) noexcept
 :m_sender(s),m_previous(connection){connection=this;}
AIReviewedLogConnect::~AIReviewedLogConnect(){connection=m_previous;}
bool AIReviewedLogConnect::matches(const QObject*s) noexcept {
 return connection&&connection->m_sender==s;
}

// ---- connection-cache ----
#include "ai-strategy/inovation-ai.h"
#include "player-initial-source-origin.h"
#include "ai.h"
#include "serverplayer.h"
#include "skill.h"
#include <QJsonArray>
extern bool matchAINativeRoom(lua_State*,int,const Room*);
namespace {
constexpr const char*smart="lua/ai/smart-ai.lua";
constexpr const char*utility="lua/utilities.lua";
struct ConnectionCacheState;struct Call;thread_local Call*connectionCacheActive=nullptr;
void observe(lua_State*,const lua_TableObservation*,void*)noexcept;
struct ConnectionCacheStack{lua_State*L;int top;explicit ConnectionCacheStack(lua_State*s):L(s),top(lua_gettop(s)){}~ConnectionCacheStack(){lua_settop(L,top);}};
enum Kind{Namespace,Qlist,Connect,Array};
struct ConnectionCacheState final:QObject{
 Room&r;lua_State*L;quint64 generation;bool live=true,resetPending=false;
 const void*space=nullptr,*qlist=nullptr,*connect=nullptr;
 QHash<const void*,Kind>watches;QHash<QString,const void*>lists,connections;QSet<QString>skillKeys;
 ConnectionCacheState(Room&room,lua_State*l):QObject(&room),r(room),L(l),generation(room.roomRuntime()->lua().generation()){}
 ~ConnectionCacheState()override{for(auto i=watches.cbegin();i!=watches.cend();++i)lua_unobservetableidentity(L,i.key(),observe,this);}
 void retire()noexcept{live=false;retireAIProjectionSource(r,L);}
 bool watch(const void*p,Kind kind){
  if(!p||watches.contains(p))return false;
  if(!lua_observetableidentity(L,p,observe,this))return false;
  try{watches.insert(p,kind);return true;}catch(...){lua_unobservetableidentity(L,p,observe,this);throw;}
 }
};
ConnectionCacheState*connectionCacheFind(const Room&r){for(auto*c:r.children())if(auto*s=dynamic_cast<ConnectionCacheState*>(c))return s;return nullptr;}
bool live(const ConnectionCacheState&s){auto*rt=s.r.roomRuntime();return s.live&&!s.resetPending&&rt&&!rt->isClosing()&&rt->lua().rawState()==s.L&&rt->lua().generation()==s.generation&&(inAIProjectionSourceScope(s.r,s.L)||inAIConnectionQueryScope(s.r,s.L))&&hasAIAsyncUniverse(s.r,s.L);}
bool connectionCacheKey(const lua_TableObservation&e,const QByteArray&k){return e.string_key&&e.string_key_length==size_t(k.size())&&!std::memcmp(e.string_key,k.constData(),size_t(k.size()));}
bool at(ConnectionCacheState&s,lua_State*L,const lua_TableObservation&e,int first,int last,int line){return e.lua_function_identity&&(e.flags&(LUA_TABLE_OBSERVER_VM_FAST|LUA_TABLE_OBSERVER_VM_SLOW))&&matchAIPinnedInstruction(s.r,L,e.lua_function_identity,e.lua_instruction_index,smart,first,last,line);}
struct Call{
 ConnectionCacheState*s;const AIRequest*q;ServerPlayer*viewer,*player;QString op,key;
 int depth;bool entered=false;QJsonObject result;const char*error=nullptr;
};
bool permitted(ConnectionCacheState&s,lua_State*L,const lua_TableObservation&e){
 auto*w=connectionCacheActive;if(!w||w->s!=&s||!w->entered||!live(s)||L!=s.L||e.event!=LUA_TABLE_OBSERVER_WRITE||e.value_type!=LUA_TTABLE||!e.value_table_identity)return false;
 if(e.table_identity==s.space)return w->op=="source_connections"&&connectionCacheKey(e,"ai_connect_cache")
  &&(!s.connect||s.connect==e.value_table_identity)
  &&matchAIBootstrapWrite(s.r,L,e,smart,3316,"ai_connect_cache",true,3209,3319);
 if(e.table_identity==s.qlist){
  if(!e.string_key||e.string_key_length>4096)return false;
  const QString written=QString::fromUtf8(e.string_key,int(e.string_key_length));
  // An admitted correction can nest an original helper. Authenticate that
  // actual producer and its whole-roster key, not the outer bridge operation.
  return (written=="global_alive_players"&&at(s,L,e,443,454,451))
   ||(s.skillKeys.contains(written)&&at(s,L,e,470,534,531));
 }
 return s.connect&&e.table_identity==s.connect&&w->op=="source_connections"
  &&connectionCacheKey(e,w->key.toUtf8())&&at(s,L,e,3209,3319,3317);
}
void observe(lua_State*L,const lua_TableObservation*ep,void*u)noexcept{
 auto&s=*static_cast<ConnectionCacheState*>(u);if(!ep||!L||!s.L||G(L)!=G(s.L))return;const auto&e=*ep;auto i=s.watches.constFind(e.table_identity);if(i==s.watches.cend())return;
 if(e.event==LUA_TABLE_OBSERVER_FREE){s.watches.remove(e.table_identity);if(e.table_identity==s.space)s.space=nullptr;if(e.table_identity==s.qlist)s.qlist=nullptr;if(e.table_identity==s.connect)s.connect=nullptr;s.retire();return;}
 if(!s.live||e.event==LUA_TABLE_OBSERVER_READ||e.event==LUA_TABLE_OBSERVER_ENUMERATE)return;
 if(e.event==LUA_TABLE_OBSERVER_METATABLE){s.retire();return;}if(e.event!=LUA_TABLE_OBSERVER_WRITE)return;
 try{
  const Kind kind=*i;
  if(kind==Namespace&&(connectionCacheKey(e,"qlist_cache")||connectionCacheKey(e,"ai_connect_cache"))
    &&allowAIPlayLoopWrite(s.r,L,e)){
   // Actual original reset publication. Drop every old array subscription and
   // receipt immediately; a later FREE/address reuse cannot touch the new map.
   // No stale epoch/source is repaired, and no current table is sampled.
   if(connectionCacheKey(e,"qlist_cache")){
    for(auto w=s.watches.begin();w!=s.watches.end();){
     if(w.key()==s.space){++w;continue;}
     lua_unobservetableidentity(L,w.key(),observe,&s);w=s.watches.erase(w);
    }
    s.lists.clear();s.connections.clear();s.skillKeys.clear();
    s.qlist=nullptr;s.connect=nullptr;s.resetPending=true;
    if(!s.watch(e.value_table_identity,Qlist)){s.retire();return;}
    s.qlist=e.value_table_identity;
   }else{
    if(!s.resetPending||!s.qlist||s.connect||!s.watch(e.value_table_identity,Connect)){s.retire();return;}
    s.connect=e.value_table_identity;s.resetPending=false;
   }
   return;
  }
  if(kind==Namespace&&!connectionCacheKey(e,"ai_connect_cache"))return; // Clone namespace watcher owns every other key.
  if(kind==Qlist&&inAICloneScope(s.r,L)){
   // CloneAI may now create the shared alive list in ordinary identity mode.
   // Accept its exact first publication here, never a later heap snapshot.
   if(connectionCacheKey(e,"global_alive_players")){
    if(e.value_type!=LUA_TTABLE||!at(s,L,e,443,454,451)||s.lists.contains("global_alive_players")
     ||!s.watch(e.value_table_identity,Array)){s.retire();return;}
    s.lists.insert("global_alive_players",e.value_table_identity);
   }
   return;
  }
  if(!permitted(s,L,e)){s.retire();return;}
  if(kind==Namespace){if(!s.connect){if(!s.watch(e.value_table_identity,Connect)){s.retire();return;}s.connect=e.value_table_identity;}return;}
  // A fresh original producer publishes a plain array exactly once per key.
  // No current occupancy/value is used to establish that source origin.
  auto&rows=kind==Qlist?s.lists:s.connections;
  const QString written=QString::fromUtf8(e.string_key,int(e.string_key_length));
  if(rows.contains(written)||!s.watch(e.value_table_identity,Array)){s.retire();return;}
  rows.insert(written,e.value_table_identity);
 }catch(...){s.retire();}
}
bool connectionCacheCurrent(Room&r,const AIRequest&q,ServerPlayer*p){return p&&p->getRoom()==&r&&q.decisionId&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&Sanguosha->currentRoom()==&r&&aiRangeRequestCurrent(r,q,p);}
bool connectionCacheClosure(Room&r,lua_State*L,int i,int env,const char*path,int first,int last){
 i=lua_absindex(L,i);if(!matchAIPinnedFunction(r,L,i,path,first,last))return false;
 for(int n=1;;++n){const char*name=lua_getupvalue(L,i,n);if(!name)break;const bool ok=!std::strcmp(name,"_ENV")&&lua_rawequal(L,-1,env);lua_pop(L,1);if(!ok)return false;}return true;
}
bool sources(Room&r,lua_State*L,ServerPlayer*viewer){
 using namespace AILuaInspect;ConnectionCacheStack stack(L);globals(L);const int env=lua_absindex(L,-1);auto*g=table(L,env);if(!g||g->metatable)return false;
 field(L,env,"sgs");const int ns=lua_absindex(L,-1);if(!lua_istable(L,ns))return false;
 struct F{const char*name;int first,last;bool global;};
 for(const F&f: {F{"aiConnect",3209,3319,true},F{"getCachedAlivePlayers",457,460,false},F{"getPlayerSkillList",470,534,false},F{"qlist_cached",443,454,false}}){ConnectionCacheStack z(L);field(L,f.global?env:ns,f.name);if(!connectionCacheClosure(r,L,-1,env,smart,f.first,f.last))return false;}
 {ConnectionCacheStack z(L);field(L,ns,"QList2Table");if(!connectionCacheClosure(r,L,-1,env,utility,44,51))return false;}
 {ConnectionCacheStack z(L);field(L,ns,"qlist");int f=lua_absindex(L,-1);if(!matchAIPinnedFunction(r,L,f,utility,19,35))return false;
  int seen=0;for(int n=1;;++n){const char*k=lua_getupvalue(L,f,n);if(!k)break;bool ok=false;
   if(!std::strcmp(k,"_ENV"))ok=lua_rawequal(L,-1,env);
   else if(!std::strcmp(k,"qlist_userdata_iterator")){ok=connectionCacheClosure(r,L,-1,env,utility,7,11);seen|=1;}
   else if(!std::strcmp(k,"qlist_table_iterator")){ok=connectionCacheClosure(r,L,-1,env,utility,13,17);seen|=2;}
   lua_pop(L,1);if(!ok)return false;}if(seen!=3)return false;
 }
 // Public original getenv source at smart468; every later namespace write is
 // observed. This finite domain does not execute the optional diagnostic arm.
 {ConnectionCacheStack z(L);field(L,ns,"ai_cache_verify");if(lua_type(L,-1)!=LUA_TBOOLEAN||lua_toboolean(L,-1))return false;}
 {ConnectionCacheStack z(L);field(L,env,"global_room");if(!matchAINativeRoom(L,-1,&r))return false;}
 {ConnectionCacheStack z(L);field(L,env,"current_self");if(!lua_istable(L,-1))return false;
  // Original aiConnect needs the source-created non-nil current_self only.
  // The last ordinary Clone may belong to a different roster player.
  bool found=false;for(auto*p:r.getAllPlayers(true)){ConnectionCacheStack zz(L);if(pushAIActualAISelf(r,p,L)&&lua_rawequal(L,-1,-2)){found=true;break;}}if(!found)return false;
 }
 for(const char*k:{"type","ipairs","error","tostring"}){ConnectionCacheStack z(L);field(L,env,k);if(!matchAINativeBinding(r,L,-1,k))return false;}
 const QHash<QString,QStringList> libraries={{"string",{"find","sub","gmatch"}},{"table",{"insert"}},{"debug",{"traceback"}}};
 for(auto i=libraries.cbegin();i!=libraries.cend();++i){ConnectionCacheStack z(L);field(L,env,i.key().toUtf8().constData());auto*t=table(L,-1);if(!t||t->metatable)return false;int lib=lua_absindex(L,-1);for(const auto&k:i.value()){ConnectionCacheStack zz(L);field(L,lib,k.toUtf8().constData());if(!matchAINativeBinding(r,L,-1,i.key()+'.'+k))return false;}}
 const QHash<QString,QStringList> methods={
  {"ServerPlayer",{"objectName","getSkillList","getVisibleSkillList","hasSkill","hasLordSkill","getMarkNames","getMark","getPileNames","getPile","getEquips"}},
  {"Room",{"getAlivePlayers","getLord"}},{"Engine",{"getViewAsEquipSkill"}},
  {"Skill",{"objectName","isLordSkill","inherits"}},{"ViewAsEquipSkill",{"viewAsEquip"}},
  {"Card",{"objectName"}},{"IntList",{"isEmpty"}},{"SPlayerList",{"length","at"}},
  {"SkillList",{"length","at"}},{"CardList",{"length","at"}}};
 for(auto i=methods.cbegin();i!=methods.cend();++i)for(const auto&k:i.value()){
  const bool derived=i.key()=="Skill"||i.key()=="Card"||i.key()=="ViewAsEquipSkill";
  if(!(derived?matchAINativeDerivedMethods(r,L,i.key(),{k}):matchAINativeClassMethod(r,L,i.key(),k)))return false;
 }
 return matchAINativeNamespaceProperty(r,L,"sgs","Sanguosha")&&matchAINativeNamespaceWrite(r,L,"sgs","ai_connect_cache")&&matchAINativeStringMetatable(r,L)&&matchAINativeValueMetamethods(r,L);
}
QJsonObject unknown(const char*reason){return{{"applicable",true},{"known",false},{"reason",reason}};}
int connectionCacheFail(lua_State*L,Call*w,const char*message){w->error=message;w->s->retire();return luaL_error(L,"%s",message);}
int run(lua_State*L){
 auto*w=static_cast<Call*>(lua_touserdata(L,1));auto&s=*w->s;
 // lua_pcall creates a new C frame with LUA_MINSTACK, independently of the
 // caller reservation. Reserve this actual frame before raw metadata inspection.
 if(!lua_checkstack(L,8192))return connectionCacheFail(L,w,"source cache bridge stack unavailable");
 if(!live(s)||!connectionCacheCurrent(s.r,*w->q,w->viewer))return connectionCacheFail(L,w,"source cache scope changed before execution");
 AILuaInspect::globals(L);const int env=lua_absindex(L,-1);
 if(w->op=="source_connections")AILuaInspect::field(L,env,"aiConnect");
 else{AILuaInspect::field(L,env,"sgs");AILuaInspect::field(L,-1,w->op=="source_alive"?"getCachedAlivePlayers":"getPlayerSkillList");lua_remove(L,-2);}
 if(w->op!="source_alive"&&!pushAISlotPlayer(L,w->player))return connectionCacheFail(L,w,"source cache argument wrapper unavailable");
 lua_call(L,w->op=="source_alive"?0:1,1);
 const int result=lua_absindex(L,-1);const void*expected=(w->op=="source_connections"?s.connections:s.lists).value(w->key,nullptr);
 auto*t=AILuaInspect::table(L,result);
 if(!live(s)||!connectionCacheCurrent(s.r,*w->q,w->viewer)||!expected||t!=expected||t->metatable)return connectionCacheFail(L,w,"actual cache result lacks its original write receipt");
 const size_t count=lua_rawlen(L,result);if(count>4096)return connectionCacheFail(L,w,"source cache exceeds finite roster bound");QJsonArray names,definitions;
 for(size_t n=1;n<=count;++n){
  lua_rawgeti(L,result,lua_Integer(n));
  if(w->op=="source_connections"){
   if(lua_type(L,-1)!=LUA_TSTRING)return connectionCacheFail(L,w,"source connection is not a string");
  }else if(w->op=="source_alive"){
   ServerPlayer*p=nullptr;for(auto*c:s.r.getAllPlayers(true))if(matchAISlotPlayer(L,-1,c)){p=c;break;}
   if(!p)return connectionCacheFail(L,w,"source alive cache contains a foreign player");
   lua_pop(L,1);lua_pushstring(L,p->objectName().toUtf8().constData());
  }else{
   if(!matchAINativeObjectMethods(s.r,L,-1,"Skill",{"objectName"}))return connectionCacheFail(L,w,"source skill metadata binding changed");
   lua_getfield(L,-1,"objectName");lua_pushvalue(L,-2);lua_call(L,1,1);lua_remove(L,-2);
   if(lua_type(L,-1)!=LUA_TSTRING)return connectionCacheFail(L,w,"source skill name is not a string");
  }
  size_t length=0;const char*name=lua_tolstring(L,-1,&length);if(length>4096)return connectionCacheFail(L,w,"source name exceeds finite bound");const QString text=QString::fromUtf8(name,int(length));names.append(text);lua_pop(L,1);
  if(w->op=="source_skills"){
   // The actual cached Skill came from getSkillList's exact Engine lookup.
   // Definition metadata is public; no SkillInstance state/ID is fabricated.
   const Skill*skill=Sanguosha->getSkill(text);if(!skill)return connectionCacheFail(L,w,"public cached skill definition disappeared");
   QJsonArray classes;for(const QMetaObject*m=skill->metaObject();m;m=m->superClass())classes.append(QString::fromLatin1(m->className()));
   QJsonArray events;if(const auto*trigger=dynamic_cast<const TriggerSkill*>(skill))for(const auto event:trigger->getTriggerEvents())events.append(int(event));
   definitions.append(QJsonObject{{"name",text},{"skill_classes",classes},{"lord_skill",skill->isLordSkill()},{"attached_lord_skill",skill->isAttachedLordSkill()},{"frequency",int(skill->getFrequency())},
    {"description",skill->getDescription()},{"limit_mark",skill->getLimitMark()},{"trigger_events",events},{"has_view_as_skill",ViewAsSkill::parseViewAsSkill(skill)!=nullptr}});
  }
 }
 if(!live(s)||!connectionCacheCurrent(s.r,*w->q,w->viewer))return connectionCacheFail(L,w,"source cache continuity changed during result conversion");
 QJsonArray foreign;
 for(auto*p:s.r.getAllPlayers(true))if(p!=w->viewer)foreign.append(p->objectName());
 w->result={{"applicable",true},{"known",true},{"names",names},{"definitions",definitions},{"foreign_original_empty_maps",foreign}};return 0;
}
}
bool allowAIConnectionCacheWrite(Room&r,lua_State*L,const lua_TableObservation&e)noexcept{
 try{auto*s=connectionCacheFind(r);return s&&permitted(*s,L,e);}catch(...){if(auto*s=connectionCacheFind(r))s->retire();return false;}
}
bool acceptAIConnectionEntry(Room&r,lua_State*L,int arguments)noexcept{
 auto*w=connectionCacheActive;if(!w||&w->s->r!=&r||w->s->L!=L||!live(*w->s)||arguments!=1||lua_gettop(L)<2
  ||r.roomRuntime()->lua().protectedCallDepth()!=w->depth)return false;
 const auto*f=AILuaInspect::value(L,-2);const auto*a=AILuaInspect::value(L,-1);
 if(!f||!ttislcf(f)||fvalue(f)!=run||!a||!ttislightuserdata(a)||pvalue(a)!=w)return false;
 w->entered=true;return true;
}
void clearAIConnectionCache(Room&r)noexcept{delete connectionCacheFind(r);}
QJsonObject queryAIConnectionCache(Room&r,const AIRequest&q,ServerPlayer*viewer,const QJsonObject&proposal){
 // Applicability depends solely on the explicit public question, never on an
 // initial-origin bit or current private shape. Other routes retain behavior.
 const bool choulve=q.kind==AIRequest::PlayerChosen&&q.choiceOptions.reason=="choulve"&&q.choiceOptions.question=="askForPlayerChosen";
 const bool activate=q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY;
 if(!choulve&&!activate)return{{"applicable",false}};
 // Public applicability supplies no source/allocation authority. The complete
 // independent native/helper/receiver proof below precedes all private choices.
 const QString op=proposal.value("op").toString();const bool alive=op=="source_alive";
 if((!alive&&op!="source_connections"&&op!="source_skills")||proposal.size()!=(alive?1:2)
  ||(!alive&&!proposal.value("player").isString()))return unknown("source cache proposal malformed");
 auto*rt=r.roomRuntime();if(!rt||!connectionCacheCurrent(r,q,viewer))return unknown("source cache request stale");
 LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();
 if(!L)return unknown("source cache Game VM unavailable");
 try{
  std::unique_ptr<AIConnectionQueryScope> queryScope;
  if(!activate&&!inAIProjectionSourceScope(r,L))
   return unknown("complete source request and allocation union unavailable");
  if(!inAIProjectionSourceScope(r,L))
   queryScope.reset(new AIConnectionQueryScope(r,viewer,L));
  if((queryScope&&!queryScope->accepted())||!hasAIAsyncUniverse(r,L))
   return unknown("complete source query and allocation union unavailable");
  // This whole public roster is proved BEFORE choosing the requested player,
  // reading a cache, or allowing an original helper's private branch.
  const auto roster=r.getAllPlayers(true);ServerPlayer*player=nullptr;
  for(auto*p:roster){
   if(!p||typeid(*p)!=typeid(ServerPlayer)||p->getRoom()!=&r||!p->hasShownGeneral()||(p->getGeneral2()&&!p->hasShownGeneral2()))return unknown("public shown roster source domain unavailable");
   if(p!=viewer&&(!hasAIPlayerEmptyMarksOrigin(*p)||(!hasAIPlayerEmptySkillsOrigin(*p)&&!hasAIPlayerPublicGeneralSkillsOrigin(*p))))return unknown("foreign original empty maps source unavailable");
   if(p->objectName()==proposal.value("player").toString())player=p;
  }
  if(!alive&&!player)return unknown("source cache player not in public roster");
  if(!lua_checkstack(L,8192)||!sources(r,L,viewer)){retireAIProjectionSource(r,L);return unknown("source cache full helper bindings changed");}
  auto*s=connectionCacheFind(r);
  if(!s){
   // Two real producers: initial namespace absence, or actual observed fresh
   // filterEvent cache births. An empty/current-equal cache never supplies one.
   const void*space=nullptr,*qlist=nullptr,*connect=nullptr;quint64 resetSerial=0;
   const bool eventOrigin=captureAIPlayLoopCacheOrigin(r,L,space,qlist,connect,resetSerial);
   const void*cloneAlive=captureAICloneAliveCache(r,L);
   if((!eventOrigin&&!hasAIInitialConnectAbsence(r,L))
      ||(!cloneAlive&&!hasAIInitialQlistKeyAbsence(r,L,"global_alive_players")))return unknown("original source cache absence unavailable");
   for(auto*p:roster)if(!hasAIInitialQlistKeyAbsence(r,L,"skills_"+p->objectName()))return unknown("original skill-list cache absence unavailable");
   if(!eventOrigin&&!captureAIInitialConnectionRoots(r,L,space,qlist))return unknown("original cache root custody unavailable");
   s=new ConnectionCacheState(r,L);s->space=space;s->qlist=qlist;s->connect=connect;
   if(!s->watch(s->space,Namespace)||!s->watch(s->qlist,Qlist)
      ||(eventOrigin&&(!resetSerial||!s->watch(s->connect,Connect)))){s->retire();return unknown("original cache root custody unavailable");}
   if(cloneAlive){
    if(!s->watch(cloneAlive,Array)){s->retire();return unknown("original Clone alive-cache custody unavailable");}
    s->lists.insert("global_alive_players",cloneAlive);
   }
  }
  if(!live(*s))return unknown("source cache lineage retired");
  // Source-proved full public roster, before requested-player execution. After
  // a genuine reset these keys are fresh; old cached arrays were detached.
  for(auto*p:roster)s->skillKeys.insert("skills_"+p->objectName());
  const QString connectionCacheKey=alive?QStringLiteral("global_alive_players"):(op=="source_skills"?"skills_"+player->objectName():player->objectName());
  Call call{s,&q,viewer,player,op,connectionCacheKey,rt->lua().protectedCallDepth()};
  struct Frame{Call*previous;explicit Frame(Call*c):previous(connectionCacheActive){connectionCacheActive=c;}~Frame(){connectionCacheActive=previous;}}frame(&call);
  ConnectionCacheStack restore(L);lua_pushcfunction(L,run);lua_pushlightuserdata(L,&call);
  if(LuaRuntime::protectedCall(L,1,0,0)!=LUA_OK){s->retire();return unknown(call.error?call.error:"original cache producer failed");}
  return call.result;
 }catch(...){retireAIProjectionSource(r,L);return unknown("source cache native allocation failure");}
}

// ---- clone-ai-clone-lineage ----
extern "C" {
}
namespace {
constexpr const char*cloneAiCloneLineageSmart="lua/ai/smart-ai.lua";
enum CloneAiCloneLineageKind {Globals,CloneAiCloneLineageNamespace,Ais,AiSelf,ScalarMap,RoleMap,ScalarChild,PlayerCache,PlayerArray};
struct CloneAiCloneLineageRoot {const char*key;int line;CloneAiCloneLineageKind kind;const char*source;bool required;};
const CloneAiCloneLineageRoot cloneAiCloneLineageRoots[]={
 {"ais",64,Ais,cloneAiCloneLineageSmart,true},{"ai_role",65,ScalarMap,cloneAiCloneLineageSmart,true},
 {"roleValue",66,RoleMap,cloneAiCloneLineageSmart,true},{"ai_NeedPeach",118,ScalarMap,cloneAiCloneLineageSmart,true},
 {"playerRoles",239,ScalarMap,cloneAiCloneLineageSmart,true},{"qlist_cache",243,PlayerCache,cloneAiCloneLineageSmart,true},
 {"defense",378,ScalarMap,cloneAiCloneLineageSmart,false}
};
constexpr int rootCount=sizeof cloneAiCloneLineageRoots/sizeof*cloneAiCloneLineageRoots;
struct CloneAiCloneLineageState;void cloneAiCloneLineageObserve(lua_State*,const lua_TableObservation*,void*)noexcept;
struct CloneAiCloneLineageState final:QObject {
 Room&r;lua_State*L;quint64 generation;bool alive=true,completed=false;
 const void*globals;const void*space;const void*root[rootCount]={};
 QHash<const void*,CloneAiCloneLineageKind>watches;QSet<QString>connectionKeys;
 QHash<QString,const void*>constructorLists;
 CloneAiCloneLineageState(Room&room,const LuaRuntime::NativeNamespaceBirth&birth):QObject(&room),r(room),L(birth.state()),generation(birth.generation()),globals(birth.globalsIdentity()),space(birth.namespaceIdentity()){}
 ~CloneAiCloneLineageState()override{if(L)for(auto i=watches.cbegin();i!=watches.cend();++i)lua_unobservetableidentity(L,i.key(),cloneAiCloneLineageObserve,this);}
 void retire()noexcept{alive=false;retireAIAsyncUniverse(L);retireAIAiNoTo(r);if(auto*rt=r.roomRuntime())rt->revokePrioritySource();}
 bool watch(const void*p,CloneAiCloneLineageKind k){
  if(!p)return false;
  const auto old=watches.constFind(p);
  if(old!=watches.cend())return *old==k;
  if(!lua_observetableidentity(L,p,cloneAiCloneLineageObserve,this))return false;
  try{watches.insert(p,k);return true;}catch(...){lua_unobservetableidentity(L,p,cloneAiCloneLineageObserve,this);throw;}
 }
};
CloneAiCloneLineageState*cloneAiCloneLineageFind(const Room&r){for(auto*c:r.children())if(auto*s=dynamic_cast<CloneAiCloneLineageState*>(c))return s;return nullptr;}
bool cloneAiCloneLineageKey(const lua_TableObservation&e,const char*k){return e.string_key&&e.string_key_length==std::strlen(k)&&!std::memcmp(e.string_key,k,e.string_key_length);}
bool source(CloneAiCloneLineageState&s,lua_State*L,const lua_TableObservation&e,const char*path,int first,int last,int line){
 return e.lua_function_identity&&(e.flags&(LUA_TABLE_OBSERVER_VM_SLOW|LUA_TABLE_OBSERVER_VM_FAST))
  &&matchAIPinnedInstruction(s.r,L,e.lua_function_identity,e.lua_instruction_index,path,first,last,line);
}
bool namespaceSource(CloneAiCloneLineageState&s,lua_State*L,const lua_TableObservation&e,const char*k,int line,int first=339,int last=429){
 if(source(s,L,e,cloneAiCloneLineageSmart,first,last,line))return true;
 // The SWIG namespace __newindex is the sole native bridge. No stack push;
 // validate its exact actual frame and the parent Lua instruction provenance.
 if(!matchAINativeNamespaceWriteFrame(s.r,L,e.table_identity,QStringLiteral("sgs"),k))return false;
 auto*ci=L->ci->previous;if(!ci||!isLua(ci)||!ttisLclosure(s2v(ci->func.p)))return false;
 auto*cl=clLvalue(s2v(ci->func.p));auto*p=cl->p;
 if(ci->u.l.savedpc<=p->code||ci->u.l.savedpc>p->code+p->sizecode)return false;
 return matchAIPinnedInstruction(s.r,L,cl,int(ci->u.l.savedpc-p->code)-1,cloneAiCloneLineageSmart,first,last,line);
}
bool numberOrString(const lua_TableObservation&e){return e.value_type==LUA_TNUMBER||e.value_type==LUA_TSTRING;}
bool sourceScalar(CloneAiCloneLineageState&s,lua_State*L,const lua_TableObservation&e){
 if(!numberOrString(e))return false;
 // Full constructor graph proves every such operand scalar before this event.
 for(int line:{399,401,406,407,408,411,412,414,415})
  if(source(s,L,e,cloneAiCloneLineageSmart,339,429,line))return true;
 if(source(s,L,e,cloneAiCloneLineageSmart,2105,2116,2111)||source(s,L,e,cloneAiCloneLineageSmart,2105,2116,2114))return true;
 if(s.r.getMode()!="04p")return false;
 for(int line:{2121,2122,2123})if(source(s,L,e,cloneAiCloneLineageSmart,2118,2180,line))return true;
 for(int line:{7816,7818,7819,7820})if(source(s,L,e,cloneAiCloneLineageSmart,7812,7823,line))return true;
 return source(s,L,e,cloneAiCloneLineageSmart,1181,1249,1186);
}
bool rootWrite(CloneAiCloneLineageState&s,lua_State*L,const lua_TableObservation&e,int n){
 const CloneAiCloneLineageRoot&r=cloneAiCloneLineageRoots[n];
 if(!s.completed){
  // Native namespace birth established absence. Each root can be produced only
  // once here, from the actual fresh-allocation source branch.
  if(s.root[n]||!r.required||!matchAIBootstrapWrite(s.r,L,e,r.source,r.line,r.key,true))return false;
 }else{
  // The only root replacement in the complete ordinary identity constructor graph.
  if(std::strcmp(r.key,"defense")||!inAICloneScope(s.r,L)
   ||!matchAIBootstrapWrite(s.r,L,e,cloneAiCloneLineageSmart,378,"defense",true,339,429))return false;
 }
 if(!s.watch(e.value_table_identity,r.kind))return false;
 s.root[n]=e.value_table_identity;return true;
}
void cloneAiCloneLineageObserve(lua_State*L,const lua_TableObservation*ep,void*u)noexcept{
 auto&s=*static_cast<CloneAiCloneLineageState*>(u);if(!L||!s.L||G(L)!=G(s.L))return;const auto&e=*ep;
 auto i=s.watches.constFind(e.table_identity);if(i==s.watches.cend())return;const CloneAiCloneLineageKind kind=*i;
 if(e.event==LUA_TABLE_OBSERVER_FREE){
  // Identities are never retained past FREE or reused as lineage for a new table.
  bool rooted=e.table_identity==s.globals||e.table_identity==s.space;
  for(auto&p:s.root)if(p==e.table_identity){p=nullptr;rooted=true;}
  s.watches.remove(e.table_identity);if(rooted)s.retire();return;
 }
 if(!s.alive||e.event==LUA_TABLE_OBSERVER_READ||e.event==LUA_TABLE_OBSERVER_ENUMERATE)return;
 if(e.event==LUA_TABLE_OBSERVER_METATABLE){s.retire();return;}
 if(e.event!=LUA_TABLE_OBSERVER_WRITE)return;
 try{
  // Exact compiled event source writes, independently checked before either
  // observer updates its ledger. No epoch/root value is adopted or restored.
  if(s.completed&&(kind==Globals||kind==CloneAiCloneLineageNamespace)&&allowAIPlayLoopWrite(s.r,L,e)){
   if(kind==CloneAiCloneLineageNamespace&&cloneAiCloneLineageKey(e,"qlist_cache")){
    int n=0;for(;n<rootCount;++n)if(!std::strcmp(cloneAiCloneLineageRoots[n].key,"qlist_cache"))break;
    if(n==rootCount||!s.watch(e.value_table_identity,PlayerCache)){s.retire();return;}
    s.root[n]=e.value_table_identity;s.connectionKeys.clear();s.constructorLists.clear();
   }
   return;
  }
  if(kind==Globals){
   if(s.completed&&allowAICardPatternsGlobalWrite(s.r,L,e))return;
   if(cloneAiCloneLineageKey(e,"sgs")){s.retire();return;}
   if(cloneAiCloneLineageKey(e,"global_room")||cloneAiCloneLineageKey(e,"current_self")){
    if(!s.completed||!inAICloneScope(s.r,L)){s.retire();return;}
    const int line=cloneAiCloneLineageKey(e,"global_room")?385:421;
    if(!source(s,L,e,cloneAiCloneLineageSmart,339,429,line)){s.retire();return;}
    if(line==421&&(e.value_type!=LUA_TTABLE||!s.watch(e.value_table_identity,AiSelf)))s.retire();
   }
   if(s.completed&&!cloneAiCloneLineageKey(e,"global_room")&&!cloneAiCloneLineageKey(e,"current_self")
    &&!(cloneAiCloneLineageKey(e,"global_delay")&&inAICloneScope(s.r,L)&&source(s,L,e,cloneAiCloneLineageSmart,339,429,389)))s.retire();
   return;
  }
  if(s.completed&&(kind==CloneAiCloneLineageNamespace||kind==PlayerCache)&&allowAIConnectionCacheWrite(s.r,L,e)){
   if(kind==PlayerCache)s.connectionKeys.insert(QString::fromUtf8(e.string_key,int(e.string_key_length)));
   return;
  }
  if(kind==CloneAiCloneLineageNamespace){
   if(s.completed&&allowAIJijiangWrite(s.r,L,e))return;
   for(int n=0;n<rootCount;++n)if(cloneAiCloneLineageKey(e,cloneAiCloneLineageRoots[n].key)){if(!rootWrite(s,L,e,n))s.retire();return;}
   if(s.completed){
    struct Write{const char*k;int line;};const Write writes[]={
     {"drawData",379},{"turncount",380},{"throwData",381},{"damageData",382},
     {"convertData",383},{"recoverData",384},{"sort_time",386},{"getMode",387},
     {"AIChat",388},{"ai_humanized",391},{"explicit_renegade",410},{"debugmode",425}};
    bool ok=false;for(const auto&w:writes)if(cloneAiCloneLineageKey(e,w.k)){
     ok=inAICloneScope(s.r,L)&&namespaceSource(s,L,e,w.k,w.line);break;
    }
    // Any other namespace write could introduce a protected-root alias. This
    // conservative retirement is source-time, not a scan of its private value.
    if(!ok&&s.r.getMode()=="04p"&&inAICloneScope(s.r,L)){
     // Every admitted identity write is a scalar produced by the exact pinned
     // source instruction; equal/alias/foreign writes still revoke immediately.
     struct IdentityWrite{const char*k;int first,last,line;int type;};
     const IdentityWrite identityWrites[]={
      {"ai_gameProcess_arg",1642,1686,1649,LUA_TNUMBER},{"ai_gameProcess_arg",1642,1686,1652,LUA_TNUMBER},
      {"ai_gameProcess_arg",1642,1686,1670,LUA_TNUMBER},{"ai_gameProcess",1642,1686,1650,LUA_TSTRING},
      {"ai_gameProcess",1642,1686,1653,LUA_TSTRING},{"ai_gameProcess",1642,1686,1684,LUA_TSTRING},
      {"UnknownRebel",1688,1946,1896,LUA_TBOOLEAN},{"sort_time",1181,1249,1188,LUA_TNUMBER},
      {"explicit_renegade",7812,7823,7813,LUA_TBOOLEAN},{"explicit_renegade",7812,7823,7817,LUA_TBOOLEAN}};
     for(const auto&w:identityWrites)if(cloneAiCloneLineageKey(e,w.k)&&e.value_type==w.type
      &&namespaceSource(s,L,e,w.k,w.line,w.first,w.last)){ok=true;break;}
    }
    if(!ok)s.retire();
   }
   return;
  }
  if(kind==AiSelf){
   // Only raw Room/player fields are used by current_ai. Other self fields are
   // independent foreign-cache receipts and cannot alter this capability pair.
   if(cloneAiCloneLineageKey(e,"room")||cloneAiCloneLineageKey(e,"player"))s.retire();return;
  }
  if(!s.completed||!inAICloneScope(s.r,L)){s.retire();return;}
  bool ok=false;
  if(kind==ScalarMap||kind==ScalarChild)ok=sourceScalar(s,L,e);
  else if(kind==RoleMap){
   if(e.value_type==LUA_TTABLE){
    ok=source(s,L,e,cloneAiCloneLineageSmart,339,429,400);
    // Each permitted RHS is a fresh scalar-literal table or this same already
    // watched child (`or {}`). No heap walk or value-dependent admission.
    if(ok)ok=s.watch(e.value_table_identity,ScalarChild);
   }
  }else if(kind==PlayerCache){
   ok=e.value_type==LUA_TTABLE&&source(s,L,e,cloneAiCloneLineageSmart,443,454,451);
   // Complete QList2Table copies borrowed native ServerPlayer handles into a
   // fresh plain array. All native methods on this cache were proved upfront.
   if(ok){
    ok=s.watch(e.value_table_identity,PlayerArray)&&e.string_key&&e.string_key_length<=4096;
    if(ok){
     const QString name=QString::fromUtf8(e.string_key,int(e.string_key_length));
     if(s.constructorLists.contains(name))ok=false;
     else s.constructorLists.insert(name,e.value_table_identity);
    }
   }
  }else if(kind==Ais){
   ok=e.value_type==LUA_TTABLE&&source(s,L,e,cloneAiCloneLineageSmart,339,429,427);
   if(ok)ok=s.watch(e.value_table_identity,AiSelf);
  }
  // PlayerArray has no writer in CloneAI; any raw/native/Lua write retires.
  if(!ok)s.retire();
 }catch(...){s.retire();}
}
}
bool beginAICloneInputLineage(Room&r,const LuaRuntime::NativeNamespaceBirth&birth)noexcept{
 try{
  auto*rt=r.roomRuntime();if(!birth.isCurrent()||!rt||rt->lua().rawState()!=birth.state()||cloneAiCloneLineageFind(r))return false;
  auto*s=new CloneAiCloneLineageState(r,birth);
  if(!s->watch(s->globals,Globals)||!s->watch(s->space,CloneAiCloneLineageNamespace)){s->retire();return false;}
  return true;
 }catch(...){if(auto*s=cloneAiCloneLineageFind(r))s->retire();return false;}
}
bool completeAICloneInputLineage(Room&r,lua_State*L,const AIBootstrapSourceProof&proof)noexcept{
 auto*s=cloneAiCloneLineageFind(r);auto*rt=r.roomRuntime();
 if(!s||!s->alive||s->completed||!rt||s->L!=L||s->generation!=rt->lua().generation()
  ||proof.m_consumed||proof.m_roomLifetime.data()!=static_cast<QObject*>(&r)||proof.m_room!=&r||proof.m_lua!=L||proof.m_generation!=s->generation
  ||!proof.m_serial||!proof.m_current||!proof.m_current(r,L,proof.m_serial))return false;
 for(int n=0;n<rootCount;++n)if(cloneAiCloneLineageRoots[n].required&&!s->root[n])return false;
 s->completed=true;return true;
}
bool hasAICloneInputLineage(const Room&r,lua_State*L)noexcept{
 auto*s=cloneAiCloneLineageFind(r);auto*rt=const_cast<Room&>(r).roomRuntime();
 return s&&s->alive&&s->completed&&rt&&!rt->isClosing()&&s->L==L
  &&rt->lua().rawState()==L&&rt->lua().generation()==s->generation;
}
void retireAICloneInputLineage(Room&r,lua_State*L)noexcept{auto*s=cloneAiCloneLineageFind(r);if(s&&s->L==L)s->alive=false;}

void clearAICloneInputLineage(Room&r)noexcept{delete cloneAiCloneLineageFind(r);}

bool hasAIInitialQlistKeyAbsence(const Room&r,lua_State*L,const QString&cloneAiCloneLineageKey)noexcept{
 return (cloneAiCloneLineageKey==QStringLiteral("global_alive_players")||cloneAiCloneLineageKey.startsWith(QStringLiteral("skills_")))
  &&hasAICloneInputLineage(r,L)&&!cloneAiCloneLineageFind(r)->connectionKeys.contains(cloneAiCloneLineageKey)
  &&!cloneAiCloneLineageFind(r)->constructorLists.contains(cloneAiCloneLineageKey);
}

bool captureAIInitialConnectionRoots(const Room&r,lua_State*L,const void*&space,const void*&qlist)noexcept{
 if(!hasAICloneInputLineage(r,L))return false;auto*s=cloneAiCloneLineageFind(r);
 for(int n=0;n<rootCount;++n)if(!std::strcmp(cloneAiCloneLineageRoots[n].key,"qlist_cache")){
  if(!s->space||!s->root[n])return false;space=s->space;qlist=s->root[n];return true;
 }return false;
}

const void* captureAICloneAliveCache(const Room&r,lua_State*L)noexcept{
 if(!hasAICloneInputLineage(r,L))return nullptr;
 auto*s=cloneAiCloneLineageFind(r);const void*p=s->constructorLists.value(QStringLiteral("global_alive_players"),nullptr);
 return p&&s->watches.value(p,Globals)==PlayerArray?p:nullptr;
}

// ---- clone-ai-clone-scope ----
extern bool matchAINativePlayer(lua_State*,int,const ServerPlayer*);
namespace {thread_local AICloneScope*cloneAiCloneScopeActive=nullptr;}
struct AICloneScope::Data {
 Room&r;ServerPlayer*p;lua_State*L;AICloneScope*previous=nullptr;
 std::unique_ptr<AIOwnerCardRulesScope>identityRules;
 std::unique_ptr<AIAsyncUniverseScope>async;
 const void*function=nullptr;quint64 generation=0,transport=0;int depth=0;
 bool ready=false,entered=false,invalid=false;
 Data(Room&room,ServerPlayer*player,lua_State*state):r(room),p(player),L(state){}
};
AICloneScope::AICloneScope(Room&r,ServerPlayer*p,lua_State*L):d(new Data(r,p,L)){
 d->previous=cloneAiCloneScopeActive;cloneAiCloneScopeActive=this;
 // A failed preflight retires the shared source prerequisites BEFORE the
 // caller allocates its first SWIG argument. Original CloneAI still runs.
 const auto failedPreallocation=qScopeGuard([&]{
  if(!accepted())retireAIProjectionSource(r,L);
 });
 auto*rt=r.roomRuntime();
 if(!p||p->getRoom()!=&r||!rt||!L||!lua_checkstack(L,8192))return;
 try{
  QString why;
  if(!proveAICloneSource(r,L,&why)){
   if(qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")){
    std::fprintf(stderr,"R56_CLONE_SOURCE_REFUSAL reason=%s\n",qPrintable(why));std::fflush(stderr);
   }
   return;
  }
  if(r.getMode()=="04p"){
   d->identityRules.reset(new AIOwnerCardRulesScope(r,p,L));
   if(!d->identityRules->accepted())return;
  }
  d->generation=rt->lua().generation();d->transport=rt->xufaTransportEpoch();
  d->depth=rt->lua().protectedCallDepth();
  const int top=lua_gettop(L);AILuaInspect::globals(L);AILuaInspect::field(L,-1,"CloneAI");
  d->function=lua_topointer(L,-1);lua_settop(L,top);
  d->async.reset(new AIAsyncUniverseScope(r,L,AIAsyncUniverseScope::FreshLuaAI));
  d->ready=d->function&&d->transport&&d->async->accepted();
 }catch(...){d->invalid=true;}
}
AICloneScope::~AICloneScope(){cloneAiCloneScopeActive=d->previous;}
bool AICloneScope::accepted()const noexcept{
 auto*rt=d->r.roomRuntime();return d->ready&&!d->invalid&&(!d->identityRules||d->identityRules->accepted())&&rt&&!rt->isClosing()
  &&rt->lua().rawState()==d->L&&rt->lua().generation()==d->generation
  &&d->transport&&rt->xufaTransportEpoch()==d->transport
  &&hasAICloneInputLineage(d->r,d->L)&&hasAIAsyncUniverse(d->r,d->L);
}
bool inAICloneScope(const Room&r,lua_State*L)noexcept{
 return cloneAiCloneScopeActive&&&cloneAiCloneScopeActive->d->r==&r&&cloneAiCloneScopeActive->d->L==L&&cloneAiCloneScopeActive->accepted();
}
bool acceptAICloneEntry(Room&r,lua_State*L,int n)noexcept{
 if(!cloneAiCloneScopeActive||&cloneAiCloneScopeActive->d->r!=&r||cloneAiCloneScopeActive->d->L!=L)return false;
 auto&x=*cloneAiCloneScopeActive->d;auto*rt=r.roomRuntime();
 if(cloneAiCloneScopeActive->accepted()&&!x.entered&&n==1&&lua_gettop(L)>=2
  &&rt->lua().protectedCallDepth()==x.depth&&lua_topointer(L,-2)==x.function
  &&matchAINativePlayer(L,-1,x.p)){x.entered=true;return true;}
 x.invalid=true;retireAIProjectionSource(r,L);return false;
}

// ---- mode-evaluation ----
#ifdef Q_OS_LINUX
#include <execinfo.h>
#endif
namespace {
struct ModeEvaluationStack{lua_State*L;int top;ModeEvaluationStack(lua_State*s):L(s),top(lua_gettop(s)){}~ModeEvaluationStack(){lua_settop(L,top);}};
void modeEvaluationRaw(lua_State*L,int i,const char*k){AILuaInspect::field(L,i,k);}
bool modeEvaluationPlain(lua_State*L,int i){if(!lua_istable(L,i))return false;if(lua_getmetatable(L,i)){lua_pop(L,1);return false;}return true;}
const Proto*modeEvaluationProto(lua_State*L,int i){return lua_isfunction(L,i)&&!lua_iscfunction(L,i)?static_cast<const LClosure*>(lua_topointer(L,i))->p:nullptr;}
struct ModeEvaluationSource{const char*path;const char*sha;};
constexpr ModeEvaluationSource modeEvaluationModeSource{"lua/ai/mode-ai.lua","cd0a6fa3b6de7e90db276846f04fe54b01199c28a1d68df3c452f6b14b8ac3e5"};
bool inModeScope(const Room&,lua_State*)noexcept;
struct ModeEvaluationState;void observeRoot(lua_State*,const lua_TableObservation*,void*)noexcept;
void observeBirth(lua_State*,const lua_TableBirthObservation*,void*)noexcept;
void barrier(Room&r,lua_State*L)noexcept{
 retireAIAsyncUniverse(L);retireAICloneInputLineage(r,L);retireAIAiNoTo(r);
 if(auto*rt=r.roomRuntime()){rt->revokePrioritySource();rt->revokeXufaTransportContinuity();}
 observeAIUnprovedNativeQuery(&r);
}
struct ModeEvaluationState final:QObject {
 Room&room;lua_State*L;quint64 generation;
 bool alive=true,completed=false,seeding=true,sourceReady=false,prepareAttempted=false;
 QHash<QString,const void*>privateRoots;QSet<const void*>tables;
 ModeEvaluationState(Room&r,lua_State*l,quint64 g):QObject(&r),room(r),L(l),generation(g){}
 ~ModeEvaluationState()override{if(L){lua_unobservetablebirth(L,observeBirth,this);for(const void*p:tables)lua_unobservetableidentity(L,p,observeRoot,this);}}
 void retire()noexcept{
  if(alive&&qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")) {std::fprintf(stderr,"R56_MODE_SOURCE_RETIRE seeding=%d ready=%d tracked=%lld\n",int(seeding),int(sourceReady),static_cast<long long>(tables.size()));std::fflush(stderr);}
  alive=false;barrier(room,L);
 }
 bool watch(const void*p){if(tables.contains(p))return true;
  if(!lua_observetableidentity(L,p,observeRoot,this))return false;
  try{tables.insert(p);}catch(...){lua_unobservetableidentity(L,p,observeRoot,this);throw;}return true;
 }
};
ModeEvaluationState*modeEvaluationFind(const Room&r){for(auto*c:r.children())if(auto*s=dynamic_cast<ModeEvaluationState*>(c))return s;return nullptr;}
const QList<int>modeEvaluationModeAnchors={10,24,40,49,59,209,221,240,250,310,317,321,327,338,395,656,662,671,889,903,1067,1198,1274,1280,1292,1302,1326,1347,1386,1404,1586,1597,1624,1642,1650};
thread_local AIModeEvaluationScope*modeEvaluationActive=nullptr;
thread_local Room*activeRoom=nullptr;thread_local lua_State*activeLua=nullptr;
struct NativeWorld{Room*r;lua_State*L;CallInfo*frame;int depth;NativeWorld*previous;const void*root=nullptr;};
// Metadata from actual ordinary prepare NEWTABLE instructions; never token data.
struct ObjectiveReceipt{Room*r;lua_State*L;const void*result=nullptr;const void*token=nullptr;bool returned=false,committed=false;};
thread_local ObjectiveReceipt*objectiveReceipt=nullptr;
thread_local NativeWorld*nativeWorld=nullptr;
bool nativeProducer(ModeEvaluationState&s,lua_State*L)noexcept{
 auto*rt=s.room.roomRuntime();return nativeWorld&&nativeWorld->r==&s.room&&nativeWorld->L==L
  &&L->ci==nativeWorld->frame&&rt&&rt->lua().protectedCallDepth()==nativeWorld->depth&&inModeScope(s.room,L);
}
bool sourceInstruction(ModeEvaluationState&s,lua_State*L,const void*fn,int pc,int opcode=-1){
 if(!s.sourceReady||(!s.seeding&&!inModeScope(s.room,L)))return false;
 return matchAIPinnedLiveInstruction(s.room,L,fn,pc,modeEvaluationModeSource.path,
   s.seeding?QList<int>{}:modeEvaluationModeAnchors,opcode);
}
bool nativeSourceWrite(ModeEvaluationState&s,lua_State*L){
 if(nativeProducer(s,L))return true;
 // Every native mutator is both the exact pre-script binding and immediately
 // called at an authenticated source PC. No nearest-frame heuristic crosses
 // pcall, an unknown native callback, or a coroutine boundary.
 if(isLua(L->ci)||!L->ci->previous||!isLua(L->ci->previous))return false;
 bool leaf=false;for(const char*k:{"setmetatable","table.insert","table.remove","table.sort"})
  if(matchAINativeLeafFrame(s.room,L,QString::fromLatin1(k))){leaf=true;break;}
 if(!leaf)return false;
 auto*ci=L->ci->previous;const auto*fn=clLvalue(s2v(ci->func.p));const int pc=int(ci->u.l.savedpc-fn->p->code)-1;
 if(pc<0||pc>=fn->p->sizecode)return false;
 const auto op=GET_OPCODE(fn->p->code[pc]);
 return (op==OP_CALL||op==OP_TAILCALL)&&sourceInstruction(s,L,fn,pc);
}
void observeRoot(lua_State*L,const lua_TableObservation*e,void*u)noexcept{
 auto&s=*static_cast<ModeEvaluationState*>(u);
 if(e->event==LUA_TABLE_OBSERVER_FREE){
  s.tables.remove(e->table_identity);bool root=false;
  if(objectiveReceipt&&objectiveReceipt->r==&s.room&&objectiveReceipt->L==L){
   if(objectiveReceipt->result==e->table_identity){objectiveReceipt->result=nullptr;s.retire();}
   if(objectiveReceipt->token==e->table_identity){objectiveReceipt->token=nullptr;s.retire();}
  }
  for(auto i=s.privateRoots.begin();i!=s.privateRoots.end();)if(i.value()==e->table_identity){root=true;i=s.privateRoots.erase(i);}else ++i;
  if(root&&s.alive)s.retire();return;
 }
 if(!s.alive||e->event==LUA_TABLE_OBSERVER_READ||e->event==LUA_TABLE_OBSERVER_ENUMERATE)return;
 try{
  bool ok=false;
  if(e->event==LUA_TABLE_OBSERVER_WRITE&&e->lua_function_identity)
   ok=sourceInstruction(s,L,e->lua_function_identity,e->lua_instruction_index);
  else if(e->event==LUA_TABLE_OBSERVER_WRITE||e->event==LUA_TABLE_OBSERVER_METATABLE)
   ok=nativeSourceWrite(s,L);
  if(!ok){
   if(s.alive&&qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")) {std::fprintf(stderr,"R56_MODE_WRITE_REFUSAL event=%d flags=%u lua_frame=%d pc=%d\n",e->event,e->flags,int(e->lua_function_identity!=nullptr),e->lua_instruction_index);std::fflush(stderr);}
   s.retire();
  }
 }catch(...){s.retire();}
}
void observeBirth(lua_State*L,const lua_TableBirthObservation*e,void*u)noexcept{
 auto&s=*static_cast<ModeEvaluationState*>(u);if(!s.alive||!s.sourceReady)return;
 try{
  const bool source=e->lua_function_identity&&sourceInstruction(s,L,e->lua_function_identity,e->lua_instruction_index,OP_NEWTABLE);
  const bool native=!e->lua_function_identity&&nativeProducer(s,L);
  if(native&&!nativeWorld->root)nativeWorld->root=e->table_identity;
  if(source&&objectiveReceipt&&objectiveReceipt->r==&s.room&&objectiveReceipt->L==L){
   // Original standard_query_result creates these two tables once per call.
   // Full Proto + independent instruction map, never a live source hint.
   const void**role=nullptr;
   if(matchAIPinnedInstruction(s.room,L,e->lua_function_identity,e->lua_instruction_index,modeEvaluationModeSource.path,1597,1620,1614))role=&objectiveReceipt->result;
   else if(matchAIPinnedInstruction(s.room,L,e->lua_function_identity,e->lua_instruction_index,modeEvaluationModeSource.path,1597,1620,1615))role=&objectiveReceipt->token;
   if(role){if(*role){s.retire();return;}*role=e->table_identity;}
  }
  if((source||native)&&!s.watch(e->table_identity))s.retire();
  // Unrelated births confer no authority. During a proved evaluation all
  // new tables must be one of the two source products above. Bootstrap's
  // unrelated roots are covered separately by its complete source receipt.
  else if(!source&&!native&&inModeScope(s.room,L))s.retire();
 }catch(...){s.retire();}
}
// Independent refresh uses the Game VM binding and continuous source histories,
// never a completed projection scope as a transferable capability.
bool refreshContext(const Room&r,lua_State*L){
 auto*rt=const_cast<Room&>(r).roomRuntime();
 return rt&&!rt->isClosing()&&rt->lua().rawState()==L&&rt->lua().isCurrentThreadOwner()
  &&LuaRuntime::current()==&rt->lua()&&Sanguosha&&Sanguosha->currentRoom()==&r
  &&hasAICloneInputLineage(r,L)&&hasAIProjectionLineage(r,L)
  &&hasAIAsyncUniverse(r,L);
}
bool publicMode(const QString&m){return m==QStringLiteral("02p")||m==QStringLiteral("04p");}
struct ModeEvaluationCheck {
 ModeEvaluationState &c; lua_State *L; QString failure; int budget=100000;
 QSet<const void *> functions,tables;
 explicit ModeEvaluationCheck(ModeEvaluationState &v):c(v),L(v.L){}
 bool no(const QString &where,const QString &why){if(failure.isEmpty())failure=where+": "+why;return false;}
 bool nativeBinding(int actual,const char *path,const QString &where){
  return matchAINativeBinding(c.room,L,actual,QString::fromLatin1(path))
    ?true:no(where,"native binding replaced or unaudited");
 }
 bool global(const QString &key,const QString &where,const Proto *caller){
  static const QSet<QString> values={"assert","error","getmetatable","ipairs","next","pairs","pcall",
   "rawequal","rawget","select","setmetatable","tonumber","tostring","type","unpack"};
  ModeEvaluationStack stack(L);AILuaInspect::globals(L);modeEvaluationRaw(L,-1,key.toUtf8().constData());
  if(values.contains(key))return nativeBinding(-1,key.toUtf8().constData(),where);
  static const QHash<QString,QStringList> fields={
   {"math",{"abs","floor","ceil","max","min","huge"}},
   {"string",{"sub","find","match","gsub","format","len"}},
   {"table",{"insert","remove","sort","concat","unpack"}}};
  if(!fields.contains(key)||!modeEvaluationPlain(L,-1))return no(where,"unapproved global capability "+key);
  const int table=lua_absindex(L,-1);
  for(const auto &field:fields.value(key)){
   modeEvaluationRaw(L,table,field.toUtf8().constData());
   const QByteArray path=(key+"."+field).toUtf8();
   bool ok;
   if(key=="math"&&field=="huge")ok=matchAINativeConstant(c.room,L,-1,path);
   else ok=nativeBinding(-1,path.constData(),where+"."+field);
   lua_pop(L,1);if(!ok)return false;
  }
  return true;
 }
 bool environment(const Proto *p,int up,const QString &where){
  // Lua's emitted _ENV accesses must be literal GETTABUP. Passing _ENV as a
  // value, dynamic indexing or writing globals is outside this pure-mode proof.
  for(int n=0;n<p->sizecode;++n){
   const Instruction instruction=p->code[n];const OpCode op=GET_OPCODE(instruction);
   if(op==OP_GETUPVAL&&GETARG_B(instruction)==up)return no(where,"environment escapes as a value");
   if(op==OP_SETTABUP&&GETARG_A(instruction)==up)return no(where,"global write outside mode proof");
   if(op==OP_SETUPVAL&&GETARG_B(instruction)==up)return no(where,"environment replacement");
   if(op==OP_GETTABUP&&GETARG_B(instruction)==up){
    const int constant=GETARG_C(instruction);
    if(constant<0||constant>=p->sizek||!ttisstring(&p->k[constant]))return no(where,"nonliteral environment lookup");
    const auto *s=tsvalue(&p->k[constant]);
    if(!global(QString::fromUtf8(getstr(s),qsizetype(tsslen(s))),where,p))return false;
   }
  }
  return true;
 }
 bool publicValue(int index,const QString &where,int depth=0){
  if(--budget<0||depth>64)return no(where,"public closure graph limit");
  index=lua_absindex(L,index);const int t=lua_type(L,index);
  if(t==LUA_TNIL||t==LUA_TBOOLEAN||t==LUA_TNUMBER||t==LUA_TSTRING)return true;
  if(t==LUA_TFUNCTION)return function(index,where,depth+1);
  if(t!=LUA_TTABLE||!modeEvaluationPlain(L,index))return no(where,"public capture is not a plain value table");
  const void*p=lua_topointer(L,index);if(tables.contains(p))return true;tables.insert(p);
  lua_pushnil(L);while(AILuaInspect::next(L,index)){
   if(!publicValue(-2,where+".key",depth+1)||!publicValue(-1,where+".value",depth+1)){lua_pop(L,2);return false;}lua_pop(L,1);
  }return true;
 }
 bool ownedRoot(int index,const QString&name){
  // The complete bootstrap source receipt proves representation and absence
  // of escaped aliases. Do not enumerate a mind/index or inspect its values.
  if(!lua_istable(L,index))return no(name,"source-owned capture identity missing");
  const void*p=lua_topointer(L,index);auto i=c.privateRoots.constFind(name);
  if(c.seeding){if(!c.tables.contains(p))return no(name,"capture lacks exact source table birth");if(i!=c.privateRoots.cend())return *i==p;c.privateRoots.insert(name,p);return true;}
  return i!=c.privateRoots.cend()&&*i==p||no(name,"source-owned capture rebound");
 }
 bool policy(int index){
  if(!modeEvaluationPlain(L,index))return no("policies","public policy registry metatable");
  ModeEvaluationStack z(L);modeEvaluationRaw(L,index,c.room.getMode().toUtf8().constData());if(!modeEvaluationPlain(L,-1))return no("02p","standard policy absent");
  int p=lua_absindex(L,-1);
  for(const char*k:{"roles","teams"}){modeEvaluationRaw(L,p,k);if(!modeEvaluationPlain(L,-1))return false;lua_pushnil(L);
   if(AILuaInspect::next(L,-2))return no("02p","author role/team override");lua_pop(L,1);}
  struct Hook{const char*name;int first,last;};
  for(const Hook &h: {Hook{"relation",1067,1077},Hook{"objective",889,898},Hook{"gameProcess",338,393},Hook{"rolePredictable",662,667}}){
   modeEvaluationRaw(L,p,h.name);if(!matchAIPinnedFunction(c.room,L,-1,modeEvaluationModeSource.path,h.first,h.last)||!function(-1,QString::fromLatin1(h.name),0))return no(h.name,"public standard hook provider changed");lua_pop(L,1);
  }
  return true;
 }
 bool capture(int index,const char*name,const QString&where,int depth){
  if(!std::strcmp(name,"minds")||!std::strcmp(name,"active_indexes")||!std::strcmp(name,"readonly_worlds")||!std::strcmp(name,"prepared_objectives"))return ownedRoot(index,QString::fromLatin1(name));
  // generation's value is private history, not an admission predicate. Its
  // source-owned upvalue is changed only by the proved writers; never read it.
  if(!std::strcmp(name,"generation"))return true;
  // Source nil at mainchunk1534; only unadmitted event entrypoints1696/1711
  // assign it. Ordinary prepare passes no event_prefix, and its source token
  // consequently has no session. Never inspect a selected event-session value.
  if(!std::strcmp(name,"active_event_session"))return true;
  if(!std::strcmp(name,"policies"))return policy(index);
  if(!std::strcmp(name,"standard_modes")){
   ModeEvaluationStack z(L);if(!modeEvaluationPlain(L,index))return false;modeEvaluationRaw(L,index,c.room.getMode().toUtf8().constData());
   return lua_type(L,-1)==LUA_TSTRING&&!std::strcmp(lua_tostring(L,-1),"identity")||no(where,"public standard mode registration changed");
  }
  if(!std::strcmp(name,"scenario_modes"))return modeEvaluationPlain(L,index); // base_mode first arm always true for public 02p
  return publicValue(index,where,depth);
 }
 bool childEnvironments(const Proto *p,const QString &where){
  for(int i=0;i<p->sizep;++i){const Proto *child=p->p[i];
   for(int u=0;u<child->sizeupvalues;++u)
    if(child->upvalues[u].name&&!std::strcmp(getstr(child->upvalues[u].name),"_ENV")
       &&!environment(child,u,where+".<child>"))return false;
   if(!childEnvironments(child,where+".<child>"))return false;
  }return true;
 }
 bool primitiveMetatables(){
  ModeEvaluationStack stack(L);
  for(int kind:{LUA_TNIL,LUA_TBOOLEAN,LUA_TNUMBER,LUA_TFUNCTION}){
   if(kind==LUA_TNIL)lua_pushnil(L);
   else if(kind==LUA_TBOOLEAN)lua_pushboolean(L,0);
   else if(kind==LUA_TNUMBER)lua_pushnumber(L,0);
   else {AILuaInspect::globals(L);modeEvaluationRaw(L,-1,"type");lua_remove(L,-2);}
   if(lua_getmetatable(L,-1))return no("primitive.metatable","unexpected primitive metamethod capability");
   lua_pop(L,1);
  }
  if(!matchAINativeStringMetatable(c.room,L))return no("string.metatable","native string metatable changed");
  AILuaInspect::globals(L);modeEvaluationRaw(L,-1,"string");const int strings=lua_absindex(L,-1);
  if(!modeEvaluationPlain(L,strings))return no("string.methods","method library redirected");
  for(const char *key:{"sub","find","match"}){modeEvaluationRaw(L,strings,key);
   bool ok=nativeBinding(-1,(QByteArray("string.")+key).constData(),"string.methods");lua_pop(L,1);if(!ok)return false;}
  return true;
 }
 bool function(int index,const QString &where,int depth){
  index=lua_absindex(L,index);const Proto *p=modeEvaluationProto(L,index);
  if(!p||!matchAIPinnedFunctionFamily(c.room,L,index,QString::fromLatin1(modeEvaluationModeSource.path),modeEvaluationModeAnchors))return no(where,"Lua callback is outside pinned mode closure union");
  const void *identity=lua_topointer(L,index);if(functions.contains(identity))return true;functions.insert(identity);
  if(!childEnvironments(p,where))return false;
  for(int n=1;;++n){
   const char *name=lua_getupvalue(L,index,n);if(!name)break;
   bool ok;
   if(!std::strcmp(name,"_ENV")){
    AILuaInspect::globals(L);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);
    ok=ok&&environment(p,n-1,where+"._ENV");
   }else ok=capture(-1,name,where+"."+QString::fromUtf8(name),depth+1);
   lua_pop(L,1);if(!ok)return false;
  }return true;
 }
};

bool graph(ModeEvaluationState&s,QString*why){
 ModeEvaluationStack z(s.L);ModeEvaluationCheck check(s);AILuaInspect::globals(s.L);
 if(!modeEvaluationPlain(s.L,-1))return false;modeEvaluationRaw(s.L,-1,"sgs");if(!lua_istable(s.L,-1))return false;
 const int ns=lua_absindex(s.L,-1);
 if(!check.primitiveMetatables()){if(why)*why=check.failure;return false;}
 struct Root{const char*name;int first,last;};
 for(const auto&root:{Root{"evaluateModeAI",1404,1578},Root{"prepareStandardObjective",1642,1648},Root{"prepareStandardRelations",1650,1655},Root{"commitStandardObjective",1624,1640}}){
  ModeEvaluationStack item(s.L);modeEvaluationRaw(s.L,ns,root.name);
  if(!matchAIPinnedFunction(s.room,s.L,-1,modeEvaluationModeSource.path,root.first,root.last)||!check.function(-1,QString::fromLatin1(root.name),0)){
   if(why)*why=check.failure.isEmpty()?QStringLiteral("mode root source differs: ")+QString::fromLatin1(root.name):check.failure;return false;
  }
 }
 if(why)*why=check.failure;return true;
}
}
bool beginAIModeEvaluationLineage(Room&r,const LuaRuntime::NativeNamespaceBirth&birth)noexcept{
 try{
  auto*rt=r.roomRuntime();if(modeEvaluationFind(r)||!rt||!birth.isCurrent()||rt->lua().rawState()!=birth.state())return false;
  auto*s=new ModeEvaluationState(r,birth.state(),birth.generation());
  if(!lua_observetablebirth(s->L,observeBirth,s)){s->retire();return false;}return true;
 }catch(...){if(auto*s=modeEvaluationFind(r))s->retire();return false;}
}
bool prepareAIModeEvaluationSources(Room&r,lua_State*L)noexcept{
 auto*s=modeEvaluationFind(r);if(!s||!s->alive||s->L!=L||s->prepareAttempted)return false;s->prepareAttempted=true;
 try{
  // Sole lexical caller: after Binding/native seals, BEFORE config/rules.
  // Compilation here has only the audited pre-authored native allocations.
  s->sourceReady=registerAIPinnedSource(r,L,modeEvaluationModeSource.path,modeEvaluationModeSource.sha);
  if(!s->sourceReady)s->retire();return s->sourceReady;
 }catch(...){s->retire();return false;}
}
bool completeAIModeEvaluationLineage(Room&r,lua_State*L,const AIBootstrapSourceProof&p)noexcept{
 auto*s=modeEvaluationFind(r);try{
  auto*rt=r.roomRuntime();if(!s||!s->alive||s->completed||!s->sourceReady||!rt||s->L!=L||!publicMode(r.getMode())
   ||p.m_consumed||p.m_roomLifetime.data()!=static_cast<QObject*>(&r)||p.m_room!=&r||p.m_lua!=L
   ||p.m_generation!=rt->lua().generation()||p.m_generation!=s->generation||!p.m_serial||!p.m_current||!p.m_current(r,L,p.m_serial))return false;
  if(!lua_checkstack(L,4096)||!graph(*s,nullptr)){s->retire();s->seeding=false;return false;}
  s->completed=s->privateRoots.contains("minds")&&s->privateRoots.contains("active_indexes")&&s->privateRoots.contains("readonly_worlds")&&s->privateRoots.contains("prepared_objectives");
  s->seeding=false;s->completed=s->completed&&p.m_current(r,L,p.m_serial);if(!s->completed)s->retire();return s->completed;
 }catch(...){if(s)s->retire();return false;}
}
bool hasAIModeEvaluationLineage(const Room&r,lua_State*L)noexcept{
 auto*s=modeEvaluationFind(r);auto*rt=const_cast<Room&>(r).roomRuntime();return s&&s->alive&&s->completed&&rt&&!rt->isClosing()
  &&s->L==L&&rt->lua().rawState()==L&&s->generation==rt->lua().generation()&&hasAIAsyncUniverse(r,L);
}
bool proveAIModeEvaluationUnion(Room&r,lua_State*L,QString*why)noexcept{
 try{
  if(!hasAIModeEvaluationLineage(r,L)||!publicMode(r.getMode())||!lua_checkstack(L,4096)){
   if(why)*why=QStringLiteral("source-owned standard mode lineage unavailable");return false;
  }
  QString detail;
  if(graph(*modeEvaluationFind(r),why?why:&detail))return true;
  if(qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")){
   std::fprintf(stderr,"R56_QUERY_RETIRE site=mode-graph reason=%s\n",(why?*why:detail).toUtf8().constData());std::fflush(stderr);
  }
 }catch(...){}
 retireAIModeEvaluationLineage(r,L);return false;
}
void retireAIModeEvaluationLineage(Room&r,lua_State*L)noexcept{auto*s=modeEvaluationFind(r);if(s&&s->L==L){
 if(s->alive&&qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")){
  std::fprintf(stderr,"R56_MODE_EXTERNAL_RETIRE seeding=%d tracked=%lld\n",int(s->seeding),static_cast<long long>(s->tables.size()));std::fflush(stderr);
#ifdef Q_OS_LINUX
  // Code addresses only at an already-decided retirement; no gameplay values,
  // source admission, callback dispatch or retirement conditions are changed.
  std::fprintf(stderr,"R56_MODE_RETIRE_NATIVE_STACK_BEGIN\n");std::fflush(stderr);
  void *frames[32];const int count=::backtrace(frames,32);::backtrace_symbols_fd(frames,count,2);
  std::fprintf(stderr,"R56_MODE_RETIRE_NATIVE_STACK_END\n");std::fflush(stderr);
#endif
 }
 s->alive=false;
}}
void clearAIModeEvaluationLineage(Room&r)noexcept{delete modeEvaluationFind(r);}
struct AIModeEvaluationScope::Data{
 Room&r;lua_State*L;AIModeEvaluationScope*previous=nullptr;Room*previousRoom=nullptr;lua_State*previousLua=nullptr;
 std::unique_ptr<AIAsyncUniverseScope>async;const void*function=nullptr,*commit=nullptr,*world=nullptr;
 Entry entry=Evaluation;ObjectiveReceipt objective;ObjectiveReceipt*previousObjective=nullptr;
 int depth=0;bool ready=false,entered=false,invalid=false;
 Data(Room&a,lua_State*b):r(a),L(b),objective{&a,b}{}
};
AIModeEvaluationScope::AIModeEvaluationScope(Room&r,lua_State*L,const QString&publicMode,Entry entry):d(new Data(r,L)){
 d->entry=entry;d->previousObjective=objectiveReceipt;
 auto failure=qScopeGuard([&]{if(!d->ready){
  if(qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")){std::fprintf(stderr,"R56_QUERY_RETIRE site=mode-scope-preflight\n");std::fflush(stderr);}
  retireAIModeEvaluationLineage(r,L);barrier(r,L);
 }});
 d->previous=modeEvaluationActive;d->previousRoom=activeRoom;d->previousLua=activeLua;modeEvaluationActive=this;activeRoom=&r;activeLua=L;auto*rt=r.roomRuntime();
 try{
  if(!rt||publicMode!=r.getMode()||!Sanguosha||Sanguosha->currentRoom()!=&r
   ||(entry==StandardObjective&&!inAIProjectionSourceScope(r,L))
   ||(entry==StandardRelations&&!refreshContext(r,L))
   ||(entry!=Evaluation&&(d->previous||objectiveReceipt))
   ||!proveAIModeEvaluationUnion(r,L))return;
  ModeEvaluationStack z(L);AILuaInspect::globals(L);modeEvaluationRaw(L,-1,"sgs");int ns=lua_absindex(L,-1);
  modeEvaluationRaw(L,ns,entry==Evaluation?"evaluateModeAI":entry==StandardRelations?"prepareStandardRelations":"prepareStandardObjective");d->function=lua_topointer(L,-1);lua_pop(L,1);
  if(entry!=Evaluation){modeEvaluationRaw(L,ns,"commitStandardObjective");d->commit=lua_topointer(L,-1);objectiveReceipt=&d->objective;}
  d->depth=rt->lua().protectedCallDepth();d->async.reset(new AIAsyncUniverseScope(r,L));d->ready=d->async->accepted();
 }catch(...){d->invalid=true;}
}
AIModeEvaluationScope::~AIModeEvaluationScope(){objectiveReceipt=d->previousObjective;modeEvaluationActive=d->previous;activeRoom=d->previousRoom;activeLua=d->previousLua;}
bool AIModeEvaluationScope::accepted()const noexcept{return d->ready&&!d->invalid&&hasAIModeEvaluationLineage(d->r,d->L)
 &&(d->entry==Evaluation||(d->entry==StandardRelations?refreshContext(d->r,d->L):inAIProjectionSourceScope(d->r,d->L)));}
void AIModeEvaluationScope::preparedObjective(int resultIndex,int tokenIndex)noexcept{
 if(d->entry==Evaluation)return;
 auto*s=modeEvaluationFind(d->r);
 if(!accepted()||!d->entered||d->objective.returned||!s||!d->objective.result||!d->objective.token
  ||lua_topointer(d->L,resultIndex)!=d->objective.result||lua_topointer(d->L,tokenIndex)!=d->objective.token
  ||!s->tables.contains(d->objective.result)||!s->tables.contains(d->objective.token)){
  d->invalid=true;retireAIModeEvaluationLineage(d->r,d->L);barrier(d->r,d->L);return;
 }
 d->objective.returned=true;
}
namespace {bool inModeScope(const Room&r,lua_State*L)noexcept{return modeEvaluationActive&&activeRoom==&r&&activeLua==L&&modeEvaluationActive->accepted();}}
bool acceptAIModeEvaluationEntry(Room&r,lua_State*L,int n)noexcept{
 if(!modeEvaluationActive||&modeEvaluationActive->d->r!=&r||modeEvaluationActive->d->L!=L)return false;auto&d=*modeEvaluationActive->d;auto*rt=r.roomRuntime();
 if(!modeEvaluationActive->accepted()||!rt||n<0||lua_gettop(L)<n+1)return false;
 const int f=lua_gettop(L)-n;
 if(!d.entered&&n==2&&rt->lua().protectedCallDepth()==d.depth&&lua_topointer(L,f)==d.function){
  if(d.entry!=AIModeEvaluationScope::Evaluation&&(!d.world||lua_topointer(L,f+1)!=d.world))return false;
  if(d.entry==AIModeEvaluationScope::StandardObjective&&lua_type(L,f+2)!=LUA_TSTRING)return false;
  if(d.entry==AIModeEvaluationScope::StandardRelations&&!lua_isnil(L,f+2)
    &&(!lua_isinteger(L,f+2)||lua_tointeger(L,f+2)<0))return false;
  d.entered=true;return true;
 }
 if(d.entry!=AIModeEvaluationScope::Evaluation&&d.entered&&d.objective.returned&&!d.objective.committed&&n==1
  &&rt->lua().protectedCallDepth()==d.depth&&lua_topointer(L,f)==d.commit&&d.objective.token
  &&lua_topointer(L,f+1)==d.objective.token){d.objective.committed=true;return true;}
 // Reentrant pcall targets can only be the proved fixed query hooks/helpers or
 // fresh lexical descendants constructed by this same complete source graph.
 if(d.entered&&rt->lua().protectedCallDepth()>d.depth
  &&matchAIPinnedFunctionFamily(r,L,f,modeEvaluationModeSource.path,modeEvaluationModeAnchors))return true;
 if(qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")){
  std::fprintf(stderr,"R56_QUERY_RETIRE site=mode-entry argc=%d entered=%d relative_depth=%d\n",n,int(d.entered),rt->lua().protectedCallDepth()-d.depth);std::fflush(stderr);
 }
 d.invalid=true;retireAIModeEvaluationLineage(r,L);retireAIAsyncUniverse(L);return false;
}

struct AIModeWorldSource::Data{NativeWorld source;bool installed=false;};
AIModeWorldSource::AIModeWorldSource(Room&r,lua_State*L):d(new Data){
 if(!inModeScope(r,L))return;auto*rt=r.roomRuntime();if(!rt)return;
 d->source={&r,L,L->ci,rt->lua().protectedCallDepth(),nativeWorld};nativeWorld=&d->source;d->installed=true;
}
AIModeWorldSource::~AIModeWorldSource(){if(d->installed){
 if(modeEvaluationActive&&activeRoom==d->source.r&&activeLua==d->source.L&&modeEvaluationActive->d->entry!=AIModeEvaluationScope::Evaluation){
  // Actual first native table born inside this exact pushAIWorldView span.
  if(modeEvaluationActive->d->world||!d->source.root){modeEvaluationActive->d->invalid=true;barrier(*d->source.r,d->source.L);}
  else modeEvaluationActive->d->world=d->source.root;
 }
 nativeWorld=d->source.previous;
}}

bool hasAIBootstrapModeTableOrigin(const Room &r,lua_State *L,const void *identity) noexcept {
 auto *s=modeEvaluationFind(r);auto *rt=const_cast<Room &>(r).roomRuntime();
 const bool okay=s&&rt&&identity&&s->alive&&s->seeding&&!s->completed&&s->sourceReady
  &&s->L==L&&rt->lua().rawState()==L&&rt->lua().generation()==s->generation
  &&rt->lua().isCurrentThreadOwner()&&s->tables.contains(identity);
 if(!okay&&qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")) {
  std::fprintf(stderr,"R56_MODE_ORIGIN_REFUSAL state=%d alive=%d seeding=%d completed=%d ready=%d member=%d tracked=%lld\n",int(s!=nullptr),int(s&&s->alive),int(s&&s->seeding),int(s&&s->completed),int(s&&s->sourceReady),int(s&&s->tables.contains(identity)),s?static_cast<long long>(s->tables.size()):0LL);std::fflush(stderr);
 }
 return okay;
}

// ---- projection-source-projection-lineage ----
namespace {
struct ProjectionSourceProjectionLineageState;void changed(lua_State*,const lua_TableObservation*,void*) noexcept;
void born(lua_State*,const lua_TableBirthObservation*,void*) noexcept;
struct ProjectionSourceProjectionLineageState final:QObject {
 Room&r;lua_State*L;quint64 generation;bool alive=true,completed=false,sourceReady=false,prepareAttempted=false;QSet<const void*>tables;
 ProjectionSourceProjectionLineageState(Room&room,lua_State*l):QObject(&room),r(room),L(l),generation(room.roomRuntime()->lua().generation()){}
 ~ProjectionSourceProjectionLineageState()override{lua_unobservetablebirth(L,born,this);for(const void*p:tables)lua_unobservetableidentity(L,p,changed,this);}
 bool watch(const void*p){if(tables.contains(p))return true;if(!lua_observetableidentity(L,p,changed,this))return false;try{tables.insert(p);}catch(...){lua_unobservetableidentity(L,p,changed,this);throw;}return true;}
 void retire()noexcept{alive=false;retireAIAsyncUniverse(L);retireAIAiNoTo(r);r.roomRuntime()->revokePrioritySource();}
};
ProjectionSourceProjectionLineageState*projectionSourceProjectionLineageFind(const Room&r){for(auto*c:r.children())if(auto*s=dynamic_cast<ProjectionSourceProjectionLineageState*>(c))return s;return nullptr;}
void changed(lua_State*,const lua_TableObservation*event,void*opaque)noexcept{
 auto&s=*static_cast<ProjectionSourceProjectionLineageState*>(opaque);const auto&e=*event;
 if(e.event==LUA_TABLE_OBSERVER_FREE){s.tables.remove(e.table_identity);if(s.completed)s.retire();return;}
 if(s.completed&&(e.event==LUA_TABLE_OBSERVER_WRITE||e.event==LUA_TABLE_OBSERVER_METATABLE))s.retire();
}
// These are the complete captured-table allocation producers in all 1005
// correction/helper roots. The remaining captures are fixed Lua closures.
// Hashes are compiled constants; no account/player/mind value supplies a root.
struct CaptureSource {const char*path;const char*sha;};
constexpr CaptureSource captureSources[]={
 {"lua/sanguosha.lua","fb7ac93090859f607f17158ea9a9ad92c86479457f5193de8c3486d606c5e601"},
 {"extensions/hunliesp.lua","8fbe83b8631f273093f7c8dd25fee3b8acdbb154a4ce2df5e35b16e2203a736c"}
};
bool captureProducer(ProjectionSourceProjectionLineageState&s,lua_State*L,const lua_TableBirthObservation&e){
 if(!s.sourceReady||!e.lua_function_identity)return false;
 const auto*f=static_cast<const LClosure*>(e.lua_function_identity);
 const int pc=e.lua_instruction_index;
 if(!f->p||f->p->linedefined!=0||f->p->lastlinedefined!=0
   ||pc<0||pc>=f->p->sizecode||GET_OPCODE(f->p->code[pc])!=OP_NEWTABLE)return false;
 // Trusted line maps come from independently compiled pinned bytes, never
 // mutable debug metadata. The borrowed closure is used only in this event.
 if(matchAIPinnedInstruction(s.r,L,f,pc,captureSources[0].path,0,0,169))return true;
 for(int line:{5203,5204,5205,5206,5207})
  if(matchAIPinnedInstruction(s.r,L,f,pc,captureSources[1].path,0,0,line))return true;
 return false;
}
void born(lua_State*L,const lua_TableBirthObservation*e,void*opaque) noexcept {
 auto&s=*static_cast<ProjectionSourceProjectionLineageState*>(opaque);if(!s.alive||s.completed)return;
 try{if(captureProducer(s,L,*e)&&!s.watch(e->table_identity))s.retire();}
 catch(...){s.retire();}
}
}
bool beginAIProjectionLineage(Room&r,const LuaRuntime::NativeNamespaceBirth&birth) noexcept {
 try{auto*rt=r.roomRuntime();if(!birth.isCurrent()||!rt||rt->lua().rawState()!=birth.state()||projectionSourceProjectionLineageFind(r))return false;
  auto*s=new ProjectionSourceProjectionLineageState(r,birth.state());if(!lua_observetablebirth(s->L,born,s)){s->retire();return false;}return true;
 }catch(...){if(auto*s=projectionSourceProjectionLineageFind(r))s->retire();return false;}
}
bool prepareAIProjectionSources(Room&r,lua_State*L)noexcept{
 auto*s=projectionSourceProjectionLineageFind(r);if(!s||!s->alive||s->L!=L||s->prepareAttempted)return false;
 s->prepareAttempted=true;
 try{
  // Sole caller is the original post-Binding, pre-config/rules boundary.
  for(const auto&source:captureSources)
   if(!registerAIPinnedSource(r,L,source.path,source.sha)){s->retire();return false;}
  s->sourceReady=true;return true;
 }catch(...){s->retire();return false;}
}
bool isAIProjectionCaptureOrigin(const Room&r,lua_State*L,const void*p)noexcept{
 auto*s=projectionSourceProjectionLineageFind(r);auto*rt=const_cast<Room&>(r).roomRuntime();
 return p&&s&&s->alive&&s->sourceReady&&s->L==L&&rt&&!rt->isClosing()
  &&rt->lua().rawState()==L&&rt->lua().generation()==s->generation&&s->tables.contains(p);
}
bool completeAIProjectionLineage(Room&r,lua_State*L,const AIBootstrapSourceProof&proof)noexcept{
 try{
  auto*rt=r.roomRuntime();auto*s=projectionSourceProjectionLineageFind(r);if(!rt||!L||!s||!s->alive||!s->sourceReady||s->completed||s->L!=L||proof.m_consumed||proof.m_roomLifetime.data()!=&r
   ||proof.m_room!=&r||proof.m_lua!=L||proof.m_generation!=rt->lua().generation()
   ||!proof.m_serial||!proof.m_current||!proof.m_current(r,L,proof.m_serial)) {
   if(qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")){std::fprintf(stderr,"R56_PROJECTION_COMPLETION_REFUSAL stage=source-token-or-lifecycle\n");std::fflush(stderr);}
   return false;
  }
  QSet<const void*>captures;
  // The full bootstrap token closes execution/effects and non-aliasing. The
  // capture producer proof is independent: sanguosha169 is a fresh literal
  // {name, fixed_func}; hunliesp5203-5207 are fresh tables filled solely by
  // the fixed 10 x 12 literal-name loops. All other lexical captures are
  // source-pinned helper closures. No private/account tables are admitted.
  // collect checks each table's actual producer BEFORE traversing contents.
  QString captureReason;
  if(!collectAICorrectionCaptureTables(r,L,captures,&captureReason)) {
   // Existing checker reports only fixed source/binding descriptors and shape,
   // never table values. This diagnostic confers no source authority.
   if(qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")){const QByteArray why=captureReason.toUtf8();std::fprintf(stderr,"R56_PROJECTION_COMPLETION_REFUSAL stage=capture-graph reason=%s\n",why.constData());std::fflush(stderr);}
   return false;
  }
  for(const void*p:captures)if(!s->tables.contains(p)){s->retire();return false;}
  for(const void*p:s->tables)if(!captures.contains(p))lua_unobservetableidentity(L,p,changed,s);
  s->tables=std::move(captures);lua_unobservetablebirth(L,born,s);
  s->completed=true;s->alive=proof.m_current(r,L,proof.m_serial);return s->alive;
 }catch(...){return false;}
}
bool hasAIProjectionLineage(const Room&r,lua_State*L)noexcept{
 auto*s=projectionSourceProjectionLineageFind(r);auto*rt=const_cast<Room&>(r).roomRuntime();return s&&s->alive&&s->completed&&s->L==L&&rt
  &&!rt->isClosing()&&rt->lua().rawState()==L&&rt->lua().generation()==s->generation;
}
void retireAIProjectionLineage(Room&r,lua_State*L)noexcept{if(auto*s=projectionSourceProjectionLineageFind(r))if(s->L==L)s->alive=false;}
void clearAIProjectionLineage(Room&r)noexcept{if(auto*s=projectionSourceProjectionLineageFind(r))delete s;}

// ---- projection-source-projection ----
#include "lua-wrapper.h"
#include "wrapped-card.h"
#include "standard.h"
#include "standard-cards.h"
#include "maneuvering.h"
#include "ol.h"
#include "server-info.h"
#ifdef Q_OS_LINUX
#endif
namespace {
thread_local AIProjectionSourceScope*projectionSourceProjectionActive=nullptr;
struct ProjectionSourceProjectionStack{lua_State*L;int n;explicit ProjectionSourceProjectionStack(lua_State*s):L(s),n(lua_gettop(s)){}~ProjectionSourceProjectionStack(){lua_settop(L,n);}};
bool ordinaryPolicy(const Card*c){
 if(!c)return false;
 // No virtual call on an unknown outer type. All permitted wrappers and
 // concrete policies have independently audited full virtual descendants.
 if(typeid(*c)==typeid(WrappedCard))c=static_cast<const WrappedCard*>(c)->getRealCard();
 if(!c)return false;const auto&t=typeid(*c);
 return t==typeid(XufaCard)||t==typeid(Slash)||t==typeid(FireSlash)||t==typeid(ThunderSlash)
  ||t==typeid(Jink)||t==typeid(Peach)||t==typeid(Analeptic)||t==typeid(Duel)
  ||t==typeid(Snatch)||t==typeid(Dismantlement)||t==typeid(Nullification)||t==typeid(ExNihilo)
  ||t==typeid(Collateral)||t==typeid(AmazingGrace)||t==typeid(GodSalvation)
  ||t==typeid(SavageAssault)||t==typeid(ArcheryAttack)||t==typeid(FireAttack)
  ||t==typeid(IronChain)||t==typeid(Indulgence)||t==typeid(SupplyShortage)||t==typeid(Lightning);
}
bool ownerPolicy(const Card*c){
 if(ordinaryPolicy(c))return true;
 // Owner-only extension: Crossbow has no policy override. Its complete
 // Weapon/EquipCard/Card readonly dispatch, metadata, correction and
 // allocation union is audited in equipment-source-v1. Exact native dispatch
 // below restricts that preproved family; no card value supplies the proof.
 if(!c)return false;
 if(typeid(*c)==typeid(WrappedCard))c=static_cast<const WrappedCard*>(c)->getRealCard();
 return c&&typeid(*c)==typeid(Crossbow);
}
bool literalPolicy(const Card*c,const QString&name){
 if(!ordinaryPolicy(c)||typeid(*c)==typeid(WrappedCard))return false;
 // These methods now dispatch only inside the exact admitted source family.
 return c->objectName()==name&&c->getId()<0;
}
}
struct AIProjectionSourceScope::Data{
 Room&r;QPointer<ServerPlayer>viewer;lua_State*L;AIProjectionSourceScope*previous;
 bool ready=false,invalid=false;quint64 generation=0,transport=0;
 quint64 weaponRangeRevision=Weapon::rangeRevision();
 QSet<const void*>getters;QHash<const Card*,QPointer<const Card>>policyCards;
 std::unique_ptr<AIAsyncUniverseScope>async;
 Data(Room&room,ServerPlayer*p,lua_State*l):r(room),viewer(p),L(l),previous(projectionSourceProjectionActive){}
};
AIProjectionSourceScope::AIProjectionSourceScope(Room&r,ServerPlayer*p,lua_State*L,QString*why):d(new Data(r,p,L)){
 projectionSourceProjectionActive=this;auto fail=[&](const QString&s){
  if(qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")){
   // Fixed proof guard/source-binding labels only; no holder, card, mark or
   // callback result is printed and this diagnostic grants no authority.
   std::fprintf(stderr,"R56_PROJECTION_ENTRY_REFUSAL reason=%s\n",s.toUtf8().constData());std::fflush(stderr);
  }
  if(why)*why=s;d->invalid=true;retireAIProjectionSource(r,L);
 };
 try{
  auto*rt=r.roomRuntime();
  if(!p||p->getRoom()!=&r||!rt||!L||rt->lua().rawState()!=L||rt->isClosing()
   ||(r.getMode()!="02p"&&r.getMode()!="04p")||Config.EnableHegemony||ServerInfo.EnableHegemony||r.getScenario()
   ||!lua_checkstack(L,8192)){fail("projection public runtime domain");return;}
  if(!hasAICloneInputLineage(r,L)||!hasAIProjectionLineage(r,L)||!hasAICardPatternsLineage(r,L)||!hasAIAsyncUniverse(r,L)
   ||!hasAIModeEvaluationLineage(r,L)||!proveAIServerLogEffects(r)){
   fail("projection complete bootstrap/constructor/mode/host source lineage unavailable");return;}
  // All public definitions, before private skill-instance applicability. The
  // compiled per-TU type map proves the exact native source implementation;
  // this does not call a virtual getter or examine a holder/card/mark result.
  for(const QString&name:Sanguosha->getSkillNames()){
   const Skill*s=Sanguosha->getSkill(name);if(!s||(aiNativeEffectClass(s)&2u)==0){fail("unreviewed native skill metadata implementation");return;}
   if(auto*x=dynamic_cast<const LuaTriggerSkillV2*>(s))if(x->get_usage_ref!=0){fail("authored usage-reference provider outside projection source union");return;}
   if(auto*x=dynamic_cast<const LuaViewAsSkillV2*>(s))if(x->get_usage_ref!=0){fail("authored view-as usage-reference provider outside projection source union");return;}
  }
  const auto nativeFamily=[&](const auto&definitions){for(const auto*s:definitions)if(!s||(aiNativeEffectClass(s)&1u)==0)return false;return true;};
  if(!nativeFamily(Sanguosha->getDistanceSkills())||!nativeFamily(Sanguosha->getMaxCardsSkills())
   ||!nativeFamily(Sanguosha->getTargetModSkills())||!nativeFamily(Sanguosha->getAttackRangeSkills())
   ||!nativeFamily(Sanguosha->getInvaliditySkills())||!nativeFamily(Sanguosha->getViewAsEquipSkills())
   ||!nativeFamily(Sanguosha->getProhibitSkills())||!nativeFamily(Sanguosha->getCardLimitSkills())){
   fail("unreviewed correction implementation");return;}
  QString error;if(!inspectAIXufaCorrectionBindings(r,L,&error)
   ||!proveAIModeEvaluationUnion(r,L,&error)){fail(error);return;}
  // A detached QPointer records actual native source custody, not a face/value
  // cache. Only this viewer's original hand/equipment enters full policy calls.
  for(const Card*c:p->getCards("he")){
   if(!ownerPolicy(c)){fail("own full-policy card outside finite standard source family");return;}
   d->policyCards.insert(c,QPointer<const Card>(c));
  }
  for(const QString&name:{"jink","slash","peach","fire_slash","thunder_slash","analeptic","dismantlement"})
   if(!Sanguosha->aiStandardCloneFactory(name)){fail("public named projection factory differs");return;}
  const auto capture=[&](const LuaFunction&f){if(f==0)return;ProjectionSourceProjectionStack z(L);f.push(L);d->getters.insert(lua_topointer(L,-1));};
  for(const auto*s:Sanguosha->getDistanceSkills()){
   if(auto*x=dynamic_cast<const LuaDistanceSkillV2*>(s)){capture(x->correct_func);capture(x->fixed_func);}
   else if(auto*x=dynamic_cast<const LuaDistanceSkill*>(s)){capture(x->correct_func);capture(x->fixed_func);}}
  for(const auto*s:Sanguosha->getMaxCardsSkills()){
   if(auto*x=dynamic_cast<const LuaMaxCardsSkillV2*>(s)){capture(x->correct_func);capture(x->fixed_func);}
   else if(auto*x=dynamic_cast<const LuaMaxCardsSkill*>(s)){capture(x->extra_func);capture(x->fixed_func);}}
  for(const auto*s:Sanguosha->getTargetModSkills()){
   if(auto*x=dynamic_cast<const LuaTargetModSkillV2*>(s))capture(x->correct_func);
   else if(auto*x=dynamic_cast<const LuaTargetModSkill*>(s)){capture(x->residue_func);capture(x->distance_limit_func);capture(x->extra_target_func);}}
  for(const auto*s:Sanguosha->getAttackRangeSkills()){
   if(auto*x=dynamic_cast<const LuaAttackRangeSkillV2*>(s)){capture(x->correct_func);capture(x->fixed_func);}
   else if(auto*x=dynamic_cast<const LuaAttackRangeSkill*>(s)){capture(x->extra_func);capture(x->fixed_func);}}
  for(const auto*s:Sanguosha->getInvaliditySkills())if(auto*x=dynamic_cast<const LuaInvaliditySkill*>(s))capture(x->skill_valid);
  for(const auto*s:Sanguosha->getViewAsEquipSkills())if(auto*x=dynamic_cast<const LuaViewAsEquipSkill*>(s))capture(x->view_as_equip);
  for(const auto*s:Sanguosha->getProhibitSkills())if(auto*x=dynamic_cast<const LuaProhibitSkill*>(s))capture(x->is_prohibited);
  for(const auto*s:Sanguosha->getCardLimitSkills())if(auto*x=dynamic_cast<const LuaCardLimitSkill*>(s)){capture(x->limit_list);capture(x->limit_pattern);capture(x->limit_reason);}
  // Both physical catalogue and virtual-horse prototype lookups are public
  // registered domains. They are checked without selecting equipped players.
  QSet<const Card*>horses;for(int id=0;id<Sanguosha->getCardCount();++id)horses.insert(Sanguosha->getEngineCard(id));
  for(const auto*c:Sanguosha->findChildren<OffensiveHorse*>())horses.insert(c);
  for(const auto*c:Sanguosha->findChildren<DefensiveHorse*>())horses.insert(c);
  for(const Card*c:horses){
   const LuaFunction*f=nullptr;
   if(auto*x=dynamic_cast<const LuaHorse*>(c))f=&x->correct_func;
   else if(auto*x=dynamic_cast<const LuaOffensiveHorse*>(c))f=&x->correct_func;
   else if(auto*x=dynamic_cast<const LuaDefensiveHorse*>(c))f=&x->correct_func;
   if(f&&*f!=0){ProjectionSourceProjectionStack z(L);f->push(L);if(!proveAIXufaGetterFunction(r,L,-1,&error)){fail(error);return;}d->getters.insert(lua_topointer(L,-1));}
  }
  d->generation=rt->lua().generation();d->transport=rt->xufaTransportEpoch();
  d->async.reset(new AIAsyncUniverseScope(r,L,AIAsyncUniverseScope::GameplayVariants));
  d->ready=d->transport&&d->async->accepted();if(!d->ready)fail("projection source allocation union unavailable");
 }catch(...){fail("projection native proof allocation failure");}
}
AIProjectionSourceScope::~AIProjectionSourceScope(){
 if(d->ready&&!Weapon::rangeRevisionCurrent(d->weaponRangeRevision))
  retireAIProjectionSource(d->r,d->L);
 projectionSourceProjectionActive=d->previous;
}
bool AIProjectionSourceScope::accepted()const noexcept{
 auto*rt=d->r.roomRuntime();return d->ready&&!d->invalid&&d->viewer&&rt&&!rt->isClosing()
  &&Weapon::rangeRevisionCurrent(d->weaponRangeRevision)
  &&rt->lua().rawState()==d->L&&rt->lua().generation()==d->generation
  &&d->transport&&rt->xufaTransportEpoch()==d->transport
  &&hasAICloneInputLineage(d->r,d->L)&&hasAIAsyncUniverse(d->r,d->L)&&hasAIProjectionLineage(d->r,d->L)&&hasAIModeEvaluationLineage(d->r,d->L)&&hasAICardPatternsLineage(d->r,d->L);
}
bool inAIProjectionSourceScope(const Room&r,lua_State*L)noexcept{
 return projectionSourceProjectionActive&&&projectionSourceProjectionActive->d->r==&r&&projectionSourceProjectionActive->d->L==L&&projectionSourceProjectionActive->accepted();
}
void retireAIProjectionSource(Room&r,lua_State*L)noexcept{
 for(auto*x=projectionSourceProjectionActive;x;x=x->d->previous)if(&x->d->r==&r&&x->d->L==L)x->d->invalid=true;
 retireAIProjectionLineage(r,L);retireAICloneInputLineage(r,L);
 retireAIModeEvaluationLineage(r,L);
 retireAICardPatternsLineage(r,L);
 retireAIAsyncUniverse(L);retireAIAiNoTo(r);
 if(auto*rt=r.roomRuntime()){rt->revokePrioritySource();rt->revokeXufaTransportContinuity();}
 observeAIUnprovedNativeQuery(&r);
}
bool acceptAIProjectionGetterEntry(Room&r,lua_State*L,int n)noexcept{
 if(!inAIProjectionSourceScope(r,L)||n<1||n>5||lua_gettop(L)<n+1)return false;
 // Permission was established for the entire callback union before any private
 // eligibility. This is only exact dispatch membership, not a new proof/grant.
 return projectionSourceProjectionActive->d->getters.contains(lua_topointer(L,-n-1));
}
bool preserveAIProjectionNativeQuery(Room&r,const Card*c,bool hasCard,bool scalarOnly)noexcept{
 auto*rt=r.roomRuntime();if(!rt||!inAIProjectionSourceScope(r,rt->lua().rawState()))return false;
 if(!hasCard||scalarOnly)return true;
 const auto i=projectionSourceProjectionActive->d->policyCards.constFind(c);
 const bool ok=i!=projectionSourceProjectionActive->d->policyCards.cend()&&i.value().data()==c;
 if(!ok){
  if(qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")){
   std::fprintf(stderr,"R56_QUERY_RETIRE site=native-card-policy-custody\n");std::fflush(stderr);
#ifdef Q_OS_LINUX
   // Code locations only, after refusal; no card data or source authority.
   std::fprintf(stderr,"R56_CUSTODY_NATIVE_STACK_BEGIN\n");std::fflush(stderr);
   void *frames[32];const int count=::backtrace(frames,32);::backtrace_symbols_fd(frames,count,2);
   std::fprintf(stderr,"R56_CUSTODY_NATIVE_STACK_END\n");std::fflush(stderr);
#endif
  }
  retireAIProjectionSource(r,rt->lua().rawState());
 }
 return ok;
}
void recordAIProjectionSourceCard(Room&r,const Card*c,const QString&name)noexcept{
 auto*rt=r.roomRuntime();if(!rt||!inAIProjectionSourceScope(r,rt->lua().rawState()))return;
 try{
  if(!literalPolicy(c,name)){retireAIProjectionSource(r,rt->lua().rawState());return;}
  projectionSourceProjectionActive->d->policyCards.insert(c,QPointer<const Card>(c));
 }catch(...){retireAIProjectionSource(r,rt->lua().rawState());}
}

void recordAIProjectionOwnerSourceCard(Room&r,const Player*owner,const Card*c,const QString&name)noexcept{
 auto*rt=r.roomRuntime();if(!rt||!inAIProjectionSourceScope(r,rt->lua().rawState())
  ||projectionSourceProjectionActive->d->viewer.data()!=owner)return;
 recordAIProjectionSourceCard(r,c,name);
}

bool aiOrdinaryCardPolicyType(const Card *card) noexcept { return ordinaryPolicy(card); }

// ---- range-certificate ----
#include <QWeakPointer>
#include <limits>

thread_local AIRangeRequestAlias *AIRangeRequestAlias::s_current=nullptr;
AIRangeRequestAlias::AIRangeRequestAlias(const AIRequest &original,const AIRequest &copy)
    :m_original(&original),m_copy(&copy),m_previous(s_current) {
    // Identity fields are immutable even in an audited native readonly copy.
    if(original.decisionId!=copy.decisionId||original.stateRevision!=copy.stateRevision
        ||original.viewerObjectName!=copy.viewerObjectName||original.conversionLedger!=copy.conversionLedger
        ||original.worldView.weaponRangeTracked!=copy.worldView.weaponRangeTracked
        ||original.worldView.weaponRangeRevision!=copy.worldView.weaponRangeRevision)
        m_copy=nullptr;
    s_current=this;
}
AIRangeRequestAlias::~AIRangeRequestAlias(){s_current=m_previous;}
const AIRequest *AIRangeRequestAlias::canonical(const AIRequest &r) {
    const AIRequest *value=&r;
    for(auto *scope=s_current;scope;scope=scope->m_previous)
        if(scope->m_copy==value)value=scope->m_original;
    return value;
}
const AIWorldView *AIRangeRequestAlias::canonical(const AIWorldView &w) {
    const AIWorldView *value=&w;
    for(auto *scope=s_current;scope;scope=scope->m_previous)
        if(scope->m_copy && &scope->m_copy->worldView==value)value=&scope->m_original->worldView;
    return value;
}

namespace {
struct RetiredRangeDecision {
    quint64 decision=0;
    QWeakPointer<AIConversionLedger> ledger;
    const AIRequest *request=nullptr;
    const AIWorldView *world=nullptr;
};
class RangeCertificate final : public QObject {
public:
    explicit RangeCertificate(Room &r) : QObject(&r) {}
    QPointer<ServerPlayer> owner;
    const AIRequest *request=nullptr;
    const AIWorldView *world=nullptr;
    const void *luaRequest=nullptr;
    QSharedPointer<AIConversionLedger> ledger;
    const void *ledgerIdentity=nullptr;
    quint64 generation=0;
    quint64 decision=0, original=0, expected=0, transitions=0, writes=0;
    QString viewer;
    bool active=false, valid=false, writing=false;
    QSet<QString> expectedFlags;
    QList<RetiredRangeDecision> retired;
};
RangeCertificate *rangeCertificateFind(Room &r, bool create=false) {
    for(QObject *child:r.children()) if(auto *p=dynamic_cast<RangeCertificate *>(child))return p;
    return create?new RangeCertificate(r):nullptr;
}
void rangeCertificateRetire(RangeCertificate *s) {
    for(auto it=s->retired.begin();it!=s->retired.end();)
        if(it->ledger.isNull())it=s->retired.erase(it);else ++it;
    if(s->request && s->ledger && s->decision) {
        for(const auto &old:s->retired)if(old.decision==s->decision)return;
        s->retired.append({s->decision,s->ledger.toWeakRef(),s->request,s->world});
    }
}
bool retired(const RangeCertificate *s,const AIRequest &r) {
    if(!s)return false;
    for(const auto &old:s->retired)if(old.decision==r.decisionId) {
        const auto ledger=old.ledger.toStrongRef();
        if(ledger && (ledger==r.conversionLedger||old.request==AIRangeRequestAlias::canonical(r)))return true;
    }
    return false;
}
QSet<QString> flags(const ServerPlayer *p) {
    const auto values=p->getFlagList();return QSet<QString>(values.begin(),values.end());
}
bool weaponRangeCurrent(const AIWorldView &world) {
    return !world.weaponRangeTracked || Weapon::rangeRevisionCurrent(world.weaponRangeRevision);
}
bool matches(Room &room,const AIRequest &r,const RangeCertificate *s,const ServerPlayer *p) {
    return weaponRangeCurrent(r.worldView)&&s&&s->request==AIRangeRequestAlias::canonical(r)&&s->valid&&!s->writing&&s->owner&&s->owner->getRoom()==&room
        &&(!p||p==s->owner)&&r.decisionId==s->decision&&r.stateRevision==s->original
        &&r.viewerObjectName==s->viewer&&r.worldView.self.objectName==s->viewer
        &&r.conversionLedger&&r.conversionLedger==s->ledger
        &&room.roomRuntime()->stateRevision()==s->expected
        &&flags(s->owner)==s->expectedFlags;
}
}
bool aiRangeSource(const QString &flag,const QString &source) {
    static const QSet<QString> infinity{"GodShijun","kejieshengzhuihun","mouwusheng","@mouxianchou-slash",
        "@@f_yonghunSlash","BearingDownBorder","@sfofl_bearing_down_border","sfofl_jubing","@@sfofl_qifeng"};
    static const QSet<QString> noDistance{"shenghua","shijun","@@luazhuixi","@@luayonglue","@PlusZhongyong",
        "SixQiaoxie","#sfofl_shuangren","sfofl_sitong","sfofl_jiaozheng","sfofl_qingjin",
        "#sfofl_zainei","#sfofl_sixi","sfofl_juedou"};
    return (flag=="InfinityAttackRange"&&infinity.contains(source))
        ||(flag=="slashNoDistanceLimit"&&noDistance.contains(source));
}
void beginAIRangeDecision(Room &room,const AIRequest &r,const void *luaRequest) {
    auto *s=rangeCertificateFind(room,true);rangeCertificateRetire(s);++s->generation;s->valid=false;s->active=false;s->writing=false;s->ledger.clear();
    auto *p=room.findPlayerByObjectName(r.viewerObjectName,true);
    if(!room.roomRuntime()||room.roomRuntime()->isClosing()||!p||!p->isAlive()||r.decisionId==0
        ||r.worldView.self.objectName!=r.viewerObjectName||!r.conversionLedger
        ||r.stateRevision!=room.roomRuntime()->stateRevision()||!weaponRangeCurrent(r.worldView))return;
    s->request=AIRangeRequestAlias::canonical(r);s->world=&s->request->worldView;s->owner=p;s->viewer=r.viewerObjectName;s->ledger=r.conversionLedger;s->ledgerIdentity=r.conversionLedger.get();
    s->decision=r.decisionId;s->original=r.stateRevision;s->expected=r.stateRevision;
    s->luaRequest=luaRequest;s->transitions=0;s->writes=0;s->expectedFlags=flags(p);s->valid=true;s->active=true;
}
void finishAIRangeDecision(Room &room,const AIRequest &r) {
    auto *s=rangeCertificateFind(room);if(!s||s->request!=AIRangeRequestAlias::canonical(r)||s->decision!=r.decisionId)return;
    if(!matches(room,r,s,nullptr))s->valid=false;
    s->active=false;
}
void invalidateAIRangeDecision(Room &room) {
    if(auto *s=rangeCertificateFind(room)){rangeCertificateRetire(s);++s->generation;s->valid=false;s->active=false;s->ledger.clear();}
}
bool aiRangeRequestCurrent(Room &room,const AIRequest &r,const ServerPlayer *p) {
    if(!weaponRangeCurrent(r.worldView))return false;
    if(!room.roomRuntime()||room.roomRuntime()->isClosing())return false;
    // Existing strict requests remain strict. A matching poisoned certificate
    // cannot be revived by a no-op or a reused original stamp.
    const auto *s=rangeCertificateFind(room);
    if(retired(s,r))return false;
    if(s&&s->decision==r.decisionId
        &&(s->ledgerIdentity==r.conversionLedger.get()||s->request==AIRangeRequestAlias::canonical(r)))
        return matches(room,r,s,p);
    return (!p||(p->getRoom()==&room&&p->objectName()==r.viewerObjectName))
        &&r.stateRevision==room.roomRuntime()->stateRevision();
}
bool aiRangeDecisionActive(Room &room,const AIRequest &r,const ServerPlayer *p) {
    if(!room.roomRuntime()||room.roomRuntime()->isClosing())return false;
    const auto *s=rangeCertificateFind(room);
    return s&&s->active&&!retired(s,r)&&matches(room,r,s,p);
}
bool writeAIRangeFlag(Room &room,const AIRequest &r,ServerPlayer *p,
    const QString &flag,const QString &source,bool value,QString *error) {
    auto fail=[&](const char *message){if(error)*error=QString::fromLatin1(message);return false;};
    auto *s=rangeCertificateFind(room);
    if(!aiRangeSource(flag,source)||!s||!s->active||!matches(room,r,s,p))
        return fail("range flag source/request certificate is invalid");
    const bool before=p->getFlagList().contains(flag); // Never an RAII override.
    const quint64 revision=room.roomRuntime()->stateRevision();
    if(before!=value&&revision==std::numeric_limits<quint64>::max())return fail("range revision overflow");
    const auto generation=s->generation;
    s->writing=true;
    // Exact donor operation: ordinary public gameplay_property_changed signal
    // order is retained. No Room broadcast, synthetic effect or rollback.
    try { p->setFlags(value?flag:QStringLiteral("-")+flag); }
    catch(...) {if(s->generation==generation){s->writing=false;s->valid=false;}throw;}
    // A synchronous property observer may enter/finish a different decision or
    // invalidate this one. Never advance or overwrite that replacement session.
    if(s->generation!=generation)return fail("range write reentered a different native session");
    s->writing=false;++s->writes;
    QSet<QString> expected=s->expectedFlags;
    if(value&&!before)expected.insert(flag);
    else if(!value&&before)expected.remove(flag);
    const quint64 next=revision+(before!=value?1:0);
    if(room.roomRuntime()->stateRevision()!=next||flags(p)!=expected) {
        s->valid=false;return fail("range write caused an uncertified mutation");
    }
    s->expected=next;s->expectedFlags=expected;if(before!=value)++s->transitions;
    return true;
}

bool aiRangeContinuation(Room &room,const AIRequest &r) {
    const auto *s=rangeCertificateFind(room);return s&&s->active&&s->writes>0&&matches(room,r,s,nullptr);
}

bool aiRangeWorldCurrent(Room &room,const AIWorldView &world) {
    if(!weaponRangeCurrent(world))return false;
    if(!room.roomRuntime()||room.roomRuntime()->isClosing())return false;
    const auto *s=rangeCertificateFind(room);
    if(s&&s->world==AIRangeRequestAlias::canonical(world))
        return s->valid&&!s->writing&&s->owner&&s->owner->getRoom()==&room
            &&world.self.objectName==s->viewer&&world.revision==s->original
            &&room.roomRuntime()->stateRevision()==s->expected&&flags(s->owner)==s->expectedFlags;
    if(s)for(const auto &old:s->retired)
        if(old.world==AIRangeRequestAlias::canonical(world)&&!old.ledger.isNull())return false;
    return world.revision==room.roomRuntime()->stateRevision();
}

bool aiRangeLuaRequestCurrent(Room &room,const AIRequest &r,const void *luaRequest) {
    const auto *s=rangeCertificateFind(room);return s&&s->active&&luaRequest&&luaRequest==s->luaRequest&&matches(room,r,s,nullptr);
}

// Exact source Room:setTag primitive; never a generic tag mutation endpoint.
bool writeAIXinyangTag(Room &room,const AIRequest &r,ServerPlayer *p,int value,const AIXinyangWritePermit &permit,QString *error) {
    auto fail=[&](const char *message){if(error)*error=QString::fromLatin1(message);return false;};
    auto*s=rangeCertificateFind(room);
    const bool setting=r.kind==AIRequest::SkillInvoke&&r.choiceOptions.reason=="xinyang_judge"&&value>=0;
    const bool resetting=r.kind==AIRequest::AmazingGrace&&r.choiceOptions.reason=="xinyang"&&value==-1;
    if((!setting&&!resetting)||!p||!permit.matches(room,r,p)||!s||!s->active||!matches(room,r,s,p))
        return fail("Xinyang source/request certificate invalid");
    if(setting){
        bool known=false;for(const auto&pile:r.worldView.self.piles)if(pile.name=="xinyang")for(const auto&c:pile.cards)if(c.effectiveId==value)known=true;
        if(!known||!p->getPile("xinyang").contains(value))return fail("Xinyang material is not the disclosed owner pile");
    }
    // No deferred effect: execute the genuine reached source setter now.
    // A Scenario override is a separate native callback graph; until that
    // graph is attested, deny BEFORE the write rather than drop onTagSet.
    if(room.getScenario()!=nullptr)return fail("Xinyang Scenario onTagSet continuation is not certified");
    const QString tag=QStringLiteral("xinyang_judge_card");const QVariant before=room.getTag(tag),after(value);
    const bool changed=!before.isValid()||before!=after;const quint64 revision=room.roomRuntime()->stateRevision();
    if(changed&&revision==std::numeric_limits<quint64>::max())return fail("Xinyang revision overflow");
    const auto generation=s->generation;s->writing=true;
    try {room.setTag(tag,after);}catch(...){if(s->generation==generation){s->writing=false;s->valid=false;}throw;}
    if(s->generation!=generation)return fail("Xinyang setter reentered another native session");
    s->writing=false;++s->writes;
    const auto next=revision+(changed?1:0);
    if(room.roomRuntime()->stateRevision()!=next||room.getTag(tag)!=after||flags(p)!=s->expectedFlags){s->valid=false;return fail("Xinyang setter caused an uncertified mutation");}
    s->expected=next;if(changed)++s->transitions;return true;
}

// The exact reached source Room:setTag operation shares the existing native
// request certificate. Its revision transition grants no payment authority.
bool writeAIGoodatwepTag(Room&room,const AIRequest&r,ServerPlayer*p,const QString&value,
 const AIGoodatwepPermit&permit,QString*error){
 auto fail=[&](const char*m){if(error)*error=QString::fromLatin1(m);return false;};auto*s=rangeCertificateFind(room);
 if(!p||!permit.matches(room,r,p)||r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY
  ||!s||!s->active||!matches(room,r,s,p))return fail("goodatwep source certificate unavailable");
 if(room.getScenario())return fail("goodatwep Scenario onTagSet not certified");
 const QString key=QStringLiteral("goodatwep");const QVariant before=room.getTag(key),after(value);
 const bool changed=!before.isValid()||before!=after;const auto revision=room.roomRuntime()->stateRevision();
 if(changed&&revision==std::numeric_limits<quint64>::max())return fail("goodatwep revision exhausted");
 const auto generation=s->generation;s->writing=true;
 try{room.setTag(key,after);}catch(...){if(s->generation==generation){s->writing=false;s->valid=false;}throw;}
 if(s->generation!=generation)return fail("goodatwep setter reentered session");s->writing=false;++s->writes;
 const auto next=revision+(changed?1:0);
 if(room.roomRuntime()->stateRevision()!=next||room.getTag(key)!=after||flags(p)!=s->expectedFlags){s->valid=false;return fail("goodatwep prefix committed but current state changed");}
 s->expected=next;if(changed)++s->transitions;return true;
}

// Exact finite source setTag/removeTag. No external tag key or QVariant arrives
// from policy; the native helper constructs only own-hand Int / offered Player.
bool writeAIYohaneTag(Room&room,const AIRequest&r,ServerPlayer*p,const QString&key,const QVariant&value,bool remove,
 const AIYohanePermit&permit,QString*error){
 auto fail=[&](const char*m){if(error)*error=QString::fromLatin1(m);return false;};auto*s=rangeCertificateFind(room);
 if(!p||!permit.matches(room,r,p,key)||(key!="ai_yohane_card_id"&&key!="ai_yohane_player")
  ||!s||!s->active||!matches(room,r,s,p))return fail("Yohane source certificate unavailable");
 if(!remove&&room.getScenario())return fail("Yohane Scenario onTagSet not certified");
 const QVariant before=room.getTag(key);const bool changed=remove?before.isValid():(!before.isValid()||before!=value);
 const auto revision=room.roomRuntime()->stateRevision();if(changed&&revision==std::numeric_limits<quint64>::max())return fail("Yohane revision exhausted");
 const auto generation=s->generation;s->writing=true;
 try{if(remove)room.removeTag(key);else room.setTag(key,value);}catch(...){if(s->generation==generation){s->writing=false;s->valid=false;}throw;}
 if(s->generation!=generation)return fail("Yohane setter reentered session");s->writing=false;++s->writes;
 const auto next=revision+(changed?1:0);const QVariant after=room.getTag(key);
 if(room.roomRuntime()->stateRevision()!=next||(remove?after.isValid():after!=value)||flags(p)!=s->expectedFlags){s->valid=false;return fail("Yohane prefix committed but current state changed");}
 s->expected=next;if(changed)++s->transitions;return true;
}

// Finite Kaiqi reached Player:setFlags, including a selected public other player.
// No Room::setPlayerFlag broadcast and no synthetic strategy-only substitute.
bool writeAIKaiqiFlag(Room &room,const AIRequest &r,ServerPlayer *owner,ServerPlayer *target,
 const QString &flag,const AIKaiqiFlagPermit &permit,QString *error) {
 auto fail=[&](const char*m){if(error)*error=QString::fromLatin1(m);return false;};
 auto*s=rangeCertificateFind(room);
 const bool self=target==owner&&flag=="kaiqi_self_used";
 const bool other=target&&flag=="kaiqi_used"&&r.choiceOptions.playerNames.contains(target->objectName());
 if(r.kind!=AIRequest::PlayerChosen||r.choiceOptions.reason!="kaiqi"||(!self&&!other)
  ||!owner||!target||target->getRoom()!=&room||!permit.matches(room,r,owner,target)
  ||!s||!s->active||!matches(room,r,s,owner))return fail("Kaiqi source flag certificate unavailable");
 const auto beforeFlags=flags(target);auto expectedTarget=beforeFlags;expectedTarget.insert(flag);
 const bool changed=!beforeFlags.contains(flag);const auto revision=room.roomRuntime()->stateRevision();
 if(changed&&revision==std::numeric_limits<quint64>::max())return fail("Kaiqi revision overflow");
 const auto generation=s->generation;s->writing=true;
 try{target->setFlags(flag);}catch(...){if(s->generation==generation){s->writing=false;s->valid=false;}throw;}
 if(s->generation!=generation)return fail("Kaiqi source setter reentered another native session");
 s->writing=false;++s->writes;
 auto expectedOwner=s->expectedFlags;if(target==owner)expectedOwner.insert(flag);
 const auto next=revision+(changed?1:0);
 if(room.roomRuntime()->stateRevision()!=next||flags(target)!=expectedTarget||flags(owner)!=expectedOwner){
  s->valid=false;return fail("Kaiqi source setter caused an uncertified mutation; prefix retained");
 }
 s->expected=next;s->expectedFlags=expectedOwner;if(changed)++s->transitions;return true;
}

// R50 fixed source Player::addMark/Player::setFlags. This is not Room::setPlayerMark
// and must not manufacture broadcast or MarkChange/MarkChanged events.
bool writeAISourceEffect(Room&room,const AIRequest&r,ServerPlayer*owner,ServerPlayer*target,
 const AISourceEffectPermit&permit,QString*error){
 auto fail=[&](const char*m){if(error)*error=QString::fromLatin1(m);return false;};
 auto*s=rangeCertificateFind(room);const auto&key=permit.key;
 const bool markKey=key=="AI_has_clubs"||key=="AI_has_spades"||key=="AI_has_diamonds"||key=="AI_has_hearts"||key=="ai_anzhi-PlayClear";
 const bool flagKey=key=="needCrossbow"||(key=="turned_target"&&permit.value)||(key=="majiBad"&&!permit.value)||(key=="majiGood"&&!permit.value)||(key=="majiHeal"&&!permit.value);
 if(permit.room!=&room||permit.request!=&r||permit.owner!=owner||permit.target!=target||!owner||!target
  ||target->getRoom()!=&room||!s||!s->active||!matches(room,r,s,owner)
  ||(permit.mark?(!markKey||target!=owner):!flagKey))return fail("source effect permit/current certificate invalid");
 const int beforeMark=permit.mark?target->getMark(key):0;
 if(permit.mark&&beforeMark==std::numeric_limits<int>::max())return fail("source signed mark addition overflow unsupported");
 const auto beforeFlags=flags(target);auto expectedTarget=beforeFlags;
 if(!permit.mark){if(permit.value)expectedTarget.insert(key);else expectedTarget.remove(key);}
 const bool changed=permit.mark||beforeFlags.contains(key)!=permit.value;
 const auto revision=room.roomRuntime()->stateRevision();
 if(changed&&revision==std::numeric_limits<quint64>::max())return fail("source effect revision overflow unsupported");
 const auto generation=s->generation;s->writing=true;
 try{if(permit.mark)target->addMark(key);else target->setFlags(permit.value?key:QStringLiteral("-")+key);}
 catch(...){if(s->generation==generation){s->writing=false;s->valid=false;}throw;}
 if(s->generation!=generation)return fail("source effect reentered replacement session; prefix retained");
 s->writing=false;++s->writes;
 auto expectedOwner=s->expectedFlags;if(target==owner&&!permit.mark)expectedOwner=expectedTarget;
 const auto next=revision+(changed?1:0);
 if(room.roomRuntime()->stateRevision()!=next||flags(target)!=expectedTarget||flags(owner)!=expectedOwner
  ||(permit.mark&&target->getMark(key)!=beforeMark+1)){
  s->valid=false;return fail("source effect caused uncertified synchronous mutation; prefix retained");
 }
 s->expected=next;s->expectedFlags=expectedOwner;if(changed)++s->transitions;return true;
}

// Finite source targetRevises Player property, not a broadcast or shadow flag.
bool writeAIAiNoToProperty(Room&room,const AIRequest&r,ServerPlayer*owner,ServerPlayer*target,
 bool value,const AIAiNoToPermit&permit,QString*error){
 auto fail=[&](const char*m){if(error)*error=QString::fromLatin1(m);return false;};auto*s=rangeCertificateFind(room);
 if(!owner||!target||target->getRoom()!=&room||!permit.matches(room,r,owner,target,value)
  ||!s||!s->active||!matches(room,r,s,owner))return fail("aiNoTo source certificate invalid");
 const QVariant before=target->property("aiNoTo"),after(value);const bool changed=before!=after;
 const auto revision=room.roomRuntime()->stateRevision();if(changed&&revision==std::numeric_limits<quint64>::max())return fail("aiNoTo revision overflow");
 const auto generation=s->generation;s->writing=true;
 try{target->setProperty("aiNoTo",after);}catch(...){if(s->generation==generation){s->writing=false;s->valid=false;}throw;}
 if(s->generation!=generation)return fail("aiNoTo setter reentered a replacement decision");s->writing=false;++s->writes;
 const auto next=revision+(changed?1:0);
 if(room.roomRuntime()->stateRevision()!=next||target->property("aiNoTo")!=after||flags(owner)!=s->expectedFlags){s->valid=false;return fail("aiNoTo source prefix caused uncertified synchronous mutation");}
 s->expected=next;if(changed)++s->transitions;return true;
}

// ---- card-patterns ----
#include "card-lifetime-manager.h"
namespace {
constexpr const char*cardPatternsSource="extensions/addFunction.lua";
constexpr const char*sha="32938f895048d1abc62a119f179ed356fda155d1ccc8ba40664ff81c2ecba046";
constexpr const char*cardPatternsUtility="lua/utilities.lua";
struct CardPatternsStack{lua_State*L;int top;explicit CardPatternsStack(lua_State*s):L(s),top(lua_gettop(s)){}~CardPatternsStack(){lua_settop(L,top);}};
struct CardPatternsState;struct CardPatternsCall;
thread_local CardPatternsCall*cardPatternsActive=nullptr;
void cardPatternsChanged(lua_State*,const lua_TableObservation*,void*) noexcept;
void cardPatternsBorn(lua_State*,const lua_TableBirthObservation*,void*) noexcept;
enum CardPatternsKind{CardPatternsGlobals,Ban,Names,Classes,Equips};
struct CardPatternsState final:QObject{
 Room&r;lua_State*L;quint64 generation;const void*globals;const void*space;
 bool alive=true,ready=false,prepared=false,completed=false;unsigned genesis=0;
 const void*latest[5]={};QHash<const void*,CardPatternsKind>tables;
 CardPatternsState(Room&room,const LuaRuntime::NativeNamespaceBirth&b):QObject(&room),r(room),L(b.state()),generation(b.generation()),globals(b.globalsIdentity()),space(b.namespaceIdentity()){}
 ~CardPatternsState()override{lua_unobservetablebirth(L,cardPatternsBorn,this);for(auto i=tables.cbegin();i!=tables.cend();++i)lua_unobservetableidentity(L,i.key(),cardPatternsChanged,this);}
 void retire(const char*why)noexcept{
  if(alive&&qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")){std::fprintf(stderr,"R56_PATTERNS_REFUSAL stage=%s\n",why);std::fflush(stderr);}
  alive=false;retireAIProjectionSource(r,L);
 }
 bool watch(const void*p,CardPatternsKind k){
  if(!p||tables.contains(p)||!lua_observetableidentity(L,p,cardPatternsChanged,this))return false;
  try{tables.insert(p,k);return true;}catch(...){lua_unobservetableidentity(L,p,cardPatternsChanged,this);throw;}
 }
};
struct CardPatternsCall{CardPatternsState*s;const void*function;int depth;bool entered=false;CardPatternsCall*previous;bool independent=false;};
CardPatternsState*cardPatternsFind(const Room&r){for(auto*c:r.children())if(auto*s=dynamic_cast<CardPatternsState*>(c))return s;return nullptr;}
bool cardPatternsCurrent(const CardPatternsState&s){auto*rt=s.r.roomRuntime();return s.alive&&rt&&!rt->isClosing()&&rt->lua().rawState()==s.L&&rt->lua().generation()==s.generation&&rt->lua().isCurrentThreadOwner();}
bool nativeContext(const CardPatternsCall &c){
 const auto&s=*c.s;
 if(inAIProjectionSourceScope(s.r,s.L))return hasAIAsyncUniverse(s.r,s.L);
 auto*rt=s.r.roomRuntime();
 return c.independent&&cardPatternsCurrent(s)&&rt&&LuaRuntime::current()==&rt->lua()
  &&Sanguosha->currentRoom()==&s.r&&hasAICloneInputLineage(s.r,s.L)
  &&hasAIProjectionLineage(s.r,s.L)&&hasAIAsyncUniverse(s.r,s.L)
  &&hasAIGlobalFactoryMapOrigin(Sanguosha)
  &&rt->definitions().hasFactorySourceOrigin(s.r,s.L);
}
bool executing(CardPatternsState&s,lua_State*L){return cardPatternsCurrent(s)&&s.ready&&L==s.L&&(!s.completed||(cardPatternsActive&&cardPatternsActive->s==&s&&cardPatternsActive->entered&&nativeContext(*cardPatternsActive)));}
bool cardPatternsKey(const lua_TableObservation&e,const char*k){return e.string_key&&e.string_key_length==std::strlen(k)&&!std::memcmp(e.string_key,k,e.string_key_length);}
bool instruction(CardPatternsState&s,lua_State*L,const void*f,int pc,int first,int last,int line){return executing(s,L)&&f&&matchAIPinnedInstruction(s.r,L,f,pc,cardPatternsSource,first,last,line);}
bool cardPatternsAt(CardPatternsState&s,lua_State*L,const lua_TableObservation&e,int first,int last,int line){return (e.flags&(LUA_TABLE_OBSERVER_VM_FAST|LUA_TABLE_OBSERVER_VM_SLOW))&&instruction(s,L,e.lua_function_identity,e.lua_instruction_index,first,last,line);}
bool caller(CardPatternsState&s,lua_State*L,int line){
 // Immediate actual caller, never a nearest-Lua-frame search across callbacks.
 if(!executing(s,L)||isLua(L->ci)||!L->ci->previous||!isLua(L->ci->previous))return false;
 auto*ci=L->ci->previous;auto*f=clLvalue(s2v(ci->func.p));int pc=int(ci->u.l.savedpc-f->p->code)-1;
 // Header mismatch is negative-only; acceptance still checks the whole Proto.
 if(f->p->linedefined!=1809||f->p->lastlinedefined!=1838)return false;
 if(pc<0||pc>=f->p->sizecode)return false;auto op=GET_OPCODE(f->p->code[pc]);
 return (op==OP_CALL||op==OP_TAILCALL)&&instruction(s,L,f,pc,1809,1838,line);
}
bool banProducer(CardPatternsState&s,lua_State*L){return caller(s,L,1813)&&matchAINativeClassLeafFrame(s.r,L,QStringLiteral("Engine"),QStringLiteral("getBanPackages"));}
bool equipWrite(CardPatternsState&s,lua_State*L,const lua_TableObservation&e){
 if(e.event!=LUA_TABLE_OBSERVER_WRITE||e.table_identity!=s.globals||!cardPatternsKey(e,"equip_patterns")||e.value_type!=LUA_TTABLE
   ||!e.value_table_identity||e.value_table_identity!=s.latest[Equips]||s.tables.value(e.value_table_identity,CardPatternsGlobals)!=Equips)return false;
 return (!s.completed&&cardPatternsAt(s,L,e,0,0,1806))||cardPatternsAt(s,L,e,1809,1838,1817);
}
void cardPatternsChanged(lua_State*L,const lua_TableObservation*ep,void*u)noexcept{
 auto&s=*static_cast<CardPatternsState*>(u);if(!ep||!L||!s.L||G(L)!=G(s.L))return;const auto&e=*ep;
 auto it=s.tables.constFind(e.table_identity);if(it==s.tables.cend())return;CardPatternsKind k=*it;
 if(e.event==LUA_TABLE_OBSERVER_FREE){
  s.tables.remove(e.table_identity);
  if(k==CardPatternsGlobals){s.globals=nullptr;s.retire("global-free");}
  else if(s.latest[k]==e.table_identity){s.latest[k]=nullptr;s.retire("current-cache-free");}
  return;
 }
 if(!s.alive||e.event==LUA_TABLE_OBSERVER_READ||e.event==LUA_TABLE_OBSERVER_ENUMERATE)return;
 try{
  if(e.event==LUA_TABLE_OBSERVER_METATABLE){s.retire("cache-metatable-write");return;}
  if(e.event!=LUA_TABLE_OBSERVER_WRITE)return;
  if(k==CardPatternsGlobals){
   if(cardPatternsKey(e,"equip_patterns")){if(!equipWrite(s,L,e))s.retire("equip-global-writer");return;}
   if(cardPatternsKey(e,"patterns")){
    if(s.completed||e.value_type!=LUA_TFUNCTION||!(cardPatternsAt(s,L,e,0,0,1809)||cardPatternsAt(s,L,e,0,0,1838)))s.retire("patterns-global-writer");
   }
   return;
  }
  if(e.value_type!=LUA_TSTRING){s.retire("cache-nonstring-write");return;}
  bool ok=false;
  if(k==Ban)ok=!e.flags&&banProducer(s,L);
  else if(k==Classes)ok=e.string_key&&cardPatternsAt(s,L,e,1809,1838,1823);
  else if(k==Names||k==Equips)ok=!e.lua_function_identity&&e.lua_instruction_index==-1
   &&(e.flags==0||e.flags==LUA_TABLE_OBSERVER_VM_SLOW)&&caller(s,L,k==Names?1828:1830)&&matchAINativeLeafFrame(s.r,L,QStringLiteral("table.insert"));
  if(!ok||e.table_identity!=s.latest[k])s.retire("cache-writer-source");
 }catch(...){s.retire("write-observer-exception");}
}
void cardPatternsBorn(lua_State*L,const lua_TableBirthObservation*e,void*u)noexcept{
 auto&s=*static_cast<CardPatternsState*>(u);if(!e||!s.alive||!s.ready||L!=s.L)return;
 try{
  CardPatternsKind kind=CardPatternsGlobals;bool initial=false;
  if(e->lua_function_identity){
   const auto*f=static_cast<const LClosure*>(e->lua_function_identity);const int pc=e->lua_instruction_index;
   if(!f->p||pc<0||pc>=f->p->sizecode||GET_OPCODE(f->p->code[pc])!=OP_NEWTABLE)return;
   if(!s.completed&&f->p->linedefined==0){
    const struct{int line;CardPatternsKind kind;}rows[]={{1805,Ban},{1806,Equips},{1807,Names},{1808,Classes}};
    for(const auto&row:rows)if(instruction(s,L,f,pc,0,0,row.line)){kind=row.kind;initial=true;break;}
   }else if(f->p->linedefined==1809){
    if(instruction(s,L,f,pc,1809,1838,1816))kind=Names;
    else if(instruction(s,L,f,pc,1809,1838,1817))kind=Equips;
   }
  }else if(banProducer(s,L))kind=Ban;
  if(kind==CardPatternsGlobals)return;
  if((initial&&(s.genesis&(1u<<kind)))||!s.watch(e->table_identity,kind)){s.retire("cache-birth-registration");return;}
  if(initial)s.genesis|=1u<<kind;
  s.latest[kind]=e->table_identity;
 }catch(...){s.retire("birth-observer-exception");}
}
bool closureEnv(CardPatternsState&s,lua_State*L,int index,int env,const char*path,int first,int last){
 index=lua_absindex(L,index);if(!matchAIPinnedFunction(s.r,L,index,path,first,last))return false;
 bool seen=false;for(int n=1;;++n){const char*k=lua_getupvalue(L,index,n);if(!k)break;bool ok=!seen&&!std::strcmp(k,"_ENV")&&lua_rawequal(L,-1,env);lua_pop(L,1);if(!ok)return false;seen=true;}return seen;
}
bool cardPatternsGraph(CardPatternsState&s,lua_State*L,int fn){
 using namespace AILuaInspect;CardPatternsStack stack(L);fn=lua_absindex(L,fn);
 if(!cardPatternsCurrent(s)||s.genesis!=30||!capacity(L,2048)||!matchAIPinnedFunction(s.r,L,fn,cardPatternsSource,1809,1838))return false;
 globals(L);int env=lua_absindex(L,-1);auto*g=table(L,env);if(g!=s.globals||!g||g->metatable)return false;
 field(L,env,"patterns");bool same=lua_rawequal(L,fn,-1);lua_pop(L,1);if(!same)return false;
 field(L,env,"sgs");if(table(L,-1)!=s.space)return false;lua_pop(L,1);
 unsigned seen=0;
 for(int n=1;;++n){const char*k=lua_getupvalue(L,fn,n);if(!k)break;
  if(!std::strcmp(k,"_ENV")){if((seen&1)||!lua_rawequal(L,-1,env))return false;seen|=1;}
  else{
   CardPatternsKind role=!std::strcmp(k,"banPs")?Ban:!std::strcmp(k,"cardNames")?Names:!std::strcmp(k,"class2Names")?Classes:CardPatternsGlobals;
   auto*t=table(L,-1);
   // Original observed origin is checked BEFORE any current-shape inspection.
   if(role==CardPatternsGlobals||(seen&(1u<<role))||!t||s.latest[role]!=t||s.tables.value(t,CardPatternsGlobals)!=role||t->metatable)return false;
   seen|=1u<<role;
  }lua_pop(L,1);
 }
 if(seen!=15)return false;
 field(L,env,"equip_patterns");auto*eq=table(L,-1);if(!eq||eq!=s.latest[Equips]||s.tables.value(eq,CardPatternsGlobals)!=Equips||eq->metatable)return false;lua_pop(L,1);
 for(int type:{LUA_TNIL,LUA_TBOOLEAN,LUA_TNUMBER,LUA_TFUNCTION})if(G(L)->mt[type])return false;
 if(!matchAINativeStringMetatable(s.r,L)||!matchAINativeValueMetamethods(s.r,L))return false;
 for(const char*k:{"type","ipairs"}){CardPatternsStack z(L);field(L,env,k);if(!matchAINativeBinding(s.r,L,-1,QString::fromLatin1(k)))return false;}
 {CardPatternsStack z(L);field(L,env,"table");int lib=lua_absindex(L,-1);auto*t=table(L,lib);if(!t||t->metatable)return false;
  field(L,lib,"insert");if(!matchAINativeBinding(s.r,L,-1,QStringLiteral("table.insert")))return false;lua_pop(L,1);
  field(L,lib,"contains");if(!closureEnv(s,L,-1,env,cardPatternsUtility,95,108))return false;
 }
 {CardPatternsStack z(L);field(L,env,"string");auto*t=table(L,-1);if(!t||t->metatable)return false;field(L,-1,"sub");if(!matchAINativeBinding(s.r,L,-1,QStringLiteral("string.sub")))return false;}
 if(!matchAINativeNamespaceProperty(s.r,L,"sgs","Sanguosha"))return false;
 for(const char*k:{"getBanPackages","getCardCount","getEngineCard"})if(!matchAINativeClassMethod(s.r,L,"Engine",k))return false;
 return matchAINativeDerivedMethods(s.r,L,"Card",{"objectName","getClassName","getPackage","getTypeId"});
}
}
struct AICardPatternsScope::Data{
 CardPatternsCall call;
 std::unique_ptr<CardLifetimeScope> lifetime;
 std::unique_ptr<AIAsyncUniverseScope> async;
 explicit Data(CardPatternsState*s):call{s,nullptr,0,false,cardPatternsActive}{}
};
bool beginAICardPatternsLineage(Room&r,const LuaRuntime::NativeNamespaceBirth&b)noexcept{
 try{auto*rt=r.roomRuntime();if(!b.isCurrent()||!rt||rt->lua().rawState()!=b.state()||cardPatternsFind(r))return false;
  auto*s=new CardPatternsState(r,b);if(!s->watch(s->globals,CardPatternsGlobals)||!lua_observetablebirth(s->L,cardPatternsBorn,s)){s->retire("native-birth-observer");return false;}return true;
 }catch(...){if(auto*s=cardPatternsFind(r))s->retire("begin-exception");return false;}
}
bool prepareAICardPatternsSources(Room&r,lua_State*L)noexcept{
 auto*s=cardPatternsFind(r);if(!s||s->prepared||s->L!=L||!cardPatternsCurrent(*s))return false;s->prepared=true;
 try{if(!registerAIPinnedSource(r,L,cardPatternsSource,sha)){s->retire("source-pin");return false;}s->ready=true;return true;}
 catch(...){s->retire("prepare-exception");return false;}
}
bool completeAICardPatternsLineage(Room&r,lua_State*L,const AIBootstrapSourceProof&p)noexcept{
 auto*s=cardPatternsFind(r);if(!s)return false;
 try{
  if(!cardPatternsCurrent(*s)||!s->ready||s->completed||s->L!=L||p.m_consumed||p.m_roomLifetime.data()!=&r||p.m_room!=&r||p.m_lua!=L||p.m_generation!=s->generation||!p.m_serial||!p.m_current||!p.m_current(r,L,p.m_serial)){s->retire("completion-token");return false;}
  if(!lua_checkstack(L,4096)){s->retire("completion-stack");return false;}
  CardPatternsStack z(L);AILuaInspect::globals(L);AILuaInspect::field(L,-1,"patterns");
  if(!cardPatternsGraph(*s,L,-1)){s->retire("completion-source-graph");return false;}
  s->completed=true;return cardPatternsCurrent(*s)&&p.m_current(r,L,p.m_serial);
 }catch(...){s->retire("completion-exception");return false;}
}
bool hasAICardPatternsLineage(const Room&r,lua_State*L)noexcept{auto*s=cardPatternsFind(r);return s&&s->L==L&&s->completed&&cardPatternsCurrent(*s);}
void retireAICardPatternsLineage(Room&r,lua_State*L)noexcept{if(auto*s=cardPatternsFind(r))if(s->L==L)s->alive=false;}
void clearAICardPatternsLineage(Room&r)noexcept{delete cardPatternsFind(r);}
AICardPatternsScope::AICardPatternsScope(Room&r,lua_State*L)noexcept{
 // The exact public catalogue call can prove its own complete source graph.
 // It does not grant full Projection, native Card policy or any factory input.
 auto*s=cardPatternsFind(r);
 try{
  if(!s||!hasAICardPatternsLineage(r,L)||cardPatternsActive||!hasAIAsyncUniverse(r,L)||Sanguosha->currentRoom()!=&r
   ||lua_gettop(L)<2||lua_type(L,-1)!=LUA_TBOOLEAN||!lua_toboolean(L,-1)||!lua_checkstack(L,4096)||!cardPatternsGraph(*s,L,-2)){
   if(s)s->retire("query-source-graph");else retireAIProjectionSource(r,L);return;
  }
  d.reset(new Data(s));
  if(!inAIProjectionSourceScope(r,L)) {
   auto*rt=r.roomRuntime();QString why;
   if(!rt||LuaRuntime::current()!=&rt->lua()||!rt->lua().isCurrentThreadOwner()
     ||!hasAICloneInputLineage(r,L)||!hasAIProjectionLineage(r,L)
     ||!proveAIServerLogEffects(r)||!Sanguosha->aiPublicPatternMetadataDefinitions(r,&why)) {
    if(qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP"))
     std::fprintf(stderr,"R56_PATTERNS_NATIVE_REFUSAL reason=%s\n",qPrintable(why));
    s->retire("independent-native-source");d.reset();return;
   }
   d->lifetime.reset(new CardLifetimeScope(globalCardLifetimeManager(),"R56PatternsCatalogue"));
   d->async.reset(new AIAsyncUniverseScope(r,L,AIAsyncUniverseScope::GameplayVariants));
   if(!d->async->accepted()){s->retire("independent-allocation-source");d.reset();return;}
   d->call.independent=true;
  }
  d->call.function=lua_topointer(L,-2);d->call.depth=r.roomRuntime()->lua().protectedCallDepth();cardPatternsActive=&d->call;
 }catch(...){if(s)s->retire("query-preflight-exception");else retireAIProjectionSource(r,L);}
}
AICardPatternsScope::~AICardPatternsScope(){
 if(!d)return;auto&c=d->call;auto&s=*c.s;
 try{
  if(!c.entered||!cardPatternsCurrent(s)||!nativeContext(c))s.retire("query-exit-continuity");
  else{CardPatternsStack z(s.L);AILuaInspect::globals(s.L);AILuaInspect::field(s.L,-1,"patterns");if(!cardPatternsGraph(s,s.L,-1))s.retire("query-exit-source-graph");}
 }catch(...){s.retire("query-exit-exception");}
 cardPatternsActive=c.previous;
}
bool acceptAICardPatternsEntry(Room&r,lua_State*L,int n)noexcept{
 auto*c=cardPatternsActive;if(!c||&c->s->r!=&r||c->s->L!=L||c->entered||n!=1||lua_gettop(L)<2||!cardPatternsCurrent(*c->s)
  ||!nativeContext(*c)||!hasAIAsyncUniverse(r,L)||r.roomRuntime()->lua().protectedCallDepth()!=c->depth
  ||lua_topointer(L,-2)!=c->function||lua_type(L,-1)!=LUA_TBOOLEAN||!lua_toboolean(L,-1))return false;
 c->entered=true;return true;
}
bool allowAICardPatternsGlobalWrite(Room&r,lua_State*L,const lua_TableObservation&e)noexcept{
 try{auto*s=cardPatternsFind(r);return s&&s->completed&&cardPatternsCurrent(*s)&&equipWrite(*s,L,e);}catch(...){if(auto*s=cardPatternsFind(r))s->retire("shared-write-exception");return false;}
}

// ---- clone-ai-clone-proof ----
extern bool matchAINativeRoom(lua_State*,int,const Room*);
extern const char *aiNativeUserdataClass(lua_State*,int);
namespace {
using namespace AILuaInspect;
struct CloneAiCloneProofStack {lua_State*L;int n;explicit CloneAiCloneProofStack(lua_State*s):L(s),n(lua_gettop(s)){}~CloneAiCloneProofStack(){lua_settop(L,n);}};
constexpr const char*cloneAiCloneProofSmart="lua/ai/smart-ai.lua";
constexpr const char*mode="lua/ai/mode-ai.lua";
constexpr const char*middle="lua/lib/middleclass.lua";
constexpr const char*cloneAiCloneProofUtility="lua/utilities.lua";
constexpr const char*heg="lua/ai/original-hegemony/heg-common-ai.lua";
bool cloneAiCloneProofPlain(lua_State*L,int i){auto*t=table(L,i);return t&&!t->metatable;}
bool scalar(lua_State*L,int i){const int t=lua_type(L,i);return t==LUA_TNIL||t==LUA_TNUMBER||t==LUA_TBOOLEAN||t==LUA_TSTRING;}
struct CloneAiCloneProofCheck {
 Room&r;lua_State*L;QString why;int env=0,ns=0,cls=0,dict=0;const void*standardModes=nullptr;QSet<const void*> protectedRoots;
 bool no(const QString&s){if(why.isEmpty())why=s;return false;}
 bool code(int i,const char*p,int first,int last){
  if(matchAIPinnedFunction(r,L,i,p,first,last))return true;
  QString actual=QStringLiteral("nonfunction");
  if(lua_isfunction(L,i)){CloneAiCloneProofStack z(L);lua_Debug ar{};lua_pushvalue(L,i);
   if(lua_getinfo(L,">S",&ar))actual=QString("%1:%2..%3").arg(ar.source?ar.source:"?").arg(ar.linedefined).arg(ar.lastlinedefined);}
  return no(QString("code %1:%2..%3 actual=%4").arg(p).arg(first).arg(last).arg(actual));
 }
 bool capture(int i,const char*k){i=lua_absindex(L,i);for(int n=1;;++n){const char*name=lua_getupvalue(L,i,n);if(!name)return no(QString("missing capture ")+k);if(!std::strcmp(name,k))return true;lua_pop(L,1);}}
 bool closure(int i,const char*p,int first,int last,const QSet<QByteArray>&captures={}){
  i=lua_absindex(L,i);if(!code(i,p,first,last))return false;QSet<QByteArray>seen;
  for(int u=1;;++u){const char*k=lua_getupvalue(L,i,u);if(!k)break;QByteArray key(k);bool ok=false;
   if(key=="_ENV")ok=lua_rawequal(L,-1,env);else ok=captures.contains(key);
   lua_pop(L,1);if(!ok||seen.contains(key))return no(QString("unexpected closure capture ")+key);seen.insert(key);
  }
  for(const auto&k:captures)if(!seen.contains(k))return no(QString("missing audited capture ")+k);return true;
 }
 bool fieldClosure(int t,const char*k,const char*p,int first,int last,const QSet<QByteArray>&captures={}){CloneAiCloneProofStack z(L);field(L,t,k);return closure(-1,p,first,last,captures);}
 bool native(int t,const char*k,const QString&name){CloneAiCloneProofStack z(L);field(L,t,k);return matchAINativeBinding(r,L,-1,name)||no("native binding "+name);}
 bool capturedClosure(int f,const char*k,const char*p,int first,int last,const QSet<QByteArray>&caps={}){CloneAiCloneProofStack z(L);return capture(f,k)&&closure(-1,p,first,last,caps);}
 bool nil(int t,const char*k){CloneAiCloneProofStack z(L);field(L,t,k);return lua_isnil(L,-1)||no(QString("unexpected metamethod ")+k);}
 // Simulate only the pinned middleclass table lookup. No authored __index runs.
 bool lookup(int t,const char*k,int depth=0){
  if(depth>20||!lua_istable(L,t))return no("class lookup chain");t=lua_absindex(L,t);protectedRoots.insert(lua_topointer(L,t));const int top=lua_gettop(L);
  field(L,t,k);if(!lua_isnil(L,-1))return true;lua_pop(L,1);
  if(!lua_getmetatable(L,t)){push(L,nullptr);return true;}const int mt=lua_absindex(L,-1);protectedRoots.insert(lua_topointer(L,mt));
  if(!nil(mt,"__gc")||!nil(mt,"__close"))return false;field(L,mt,"__index");
  if(lua_isnil(L,-1))return true;
  bool ok=false;
  if(lua_istable(L,-1))ok=lookup(lua_absindex(L,-1),k,depth+1);
  else {
   const int f=lua_absindex(L,-1);const bool inherited=matchAIPinnedFunction(r,L,f,middle,33,33);
   if(!closure(f,middle,inherited?33:35,inherited?33:35,inherited?QSet<QByteArray>{"dict","superStatic"}:QSet<QByteArray>{"dict"}))return false;
   if(!capture(f,"dict")||!lua_istable(L,-1)||!lookup(-1,k,depth+1))return false;
   if(lua_toboolean(L,-1)||!inherited)ok=true;
   else if(capture(f,"superStatic"))ok=lookup(-1,k,depth+1);
  }
  if(!ok)return false;lua_replace(L,top+1);lua_settop(L,top+1);return true;
 }
 bool modeEnabled(int i){
  i=lua_absindex(L,i);if(!closure(i,mode,2336,2353,{"standard_modes","policies","managed_rooms"}))return false;
  // Only standard_modes[public Room mode] is reached. Neither policies nor
  // managed_rooms is read on this public branch, regardless of hidden roles.
  CloneAiCloneProofStack z(L);if(!capture(i,"standard_modes")||!cloneAiCloneProofPlain(L,-1))return no("standard mode public table");standardModes=lua_topointer(L,-1);protectedRoots.insert(standardModes);field(L,-1,r.getMode().toUtf8().constData());return lua_type(L,-1)==LUA_TSTRING&&!std::strcmp(lua_tostring(L,-1),"identity")||no("ordinary identity public mode registration");
 }
 bool policy(int i){return closure(i,mode,2362,2366);}
 bool currentAI(int i){return closure(i,mode,2328,2333);}
 bool sourceGraph(){
  if(!fieldClosure(env,"CloneAI",cloneAiCloneProofSmart,23,25))return false;
  if(!lua_getmetatable(L,cls))return no("class metatable");field(L,-1,"__call");if(!closure(-1,middle,44,44))return false;lua_pop(L,2);
  for(auto row:{qMakePair("new",103),qMakePair("allocate",98)}){CloneAiCloneProofStack z(L);if(!lookup(cls,row.first)||!closure(-1,middle,row.second,row.second==103?107:101))return false;}
  field(L,cls,"__instanceDict");dict=lua_absindex(L,-1);protectedRoots.insert(lua_topointer(L,dict));if(!lua_istable(L,dict))return no("instance dict");field(L,dict,"__index");if(!lua_rawequal(L,-1,dict))return no("instance __index");lua_pop(L,1);
  for(const char*k:{"__newindex","__eq","__gc","__close"}){CloneAiCloneProofStack z(L);if(!lookup(dict,k)||!lua_isnil(L,-1))return no(QString("instance metamethod ")+k);}
  // Public no-hegemony bootstrap leaves the ordinary base initializer.
  // basara58 is gated by GetConfig EnableHegemony; original-hegemony common
  // returns unless its publicly gated loader sets original_hegemony_ai_loading.
  {CloneAiCloneProofStack z(L);if(!lookup(dict,"initialize")||!closure(-1,cloneAiCloneProofSmart,339,429,{"version"}))return false;
   if(!capture(-1,"version")||lua_type(L,-1)!=LUA_TSTRING)return no("version scalar");}
  {CloneAiCloneProofStack z(L);if(!lookup(dict,"updatePlayers")||!closure(-1,mode,2390,2403,{"policy","update"}))return false;const int f=lua_absindex(L,-1);
   if(!capturedClosure(f,"update",cloneAiCloneProofSmart,2118,2180)||!capture(f,"policy")||!policy(-1))return false;}
  {CloneAiCloneProofStack z(L);field(L,ns,"modeAIEnabled");if(!modeEnabled(-1))return false;}
  {CloneAiCloneProofStack z(L);field(L,env,"isRolePredictable");const int f=lua_absindex(L,-1);
   if(!closure(f,mode,2433,2442,{"current_ai","policy","policies","standard_modes","predictable"})
    ||!capturedClosure(f,"predictable",cloneAiCloneProofSmart,1425,1437))return false;
   {CloneAiCloneProofStack a(L);if(!capture(f,"policy")||!policy(-1))return false;}
   {CloneAiCloneProofStack a(L);if(!capture(f,"current_ai")||!currentAI(-1))return false;}
   // current_ai sees either the just-created raw room/player fields or an
   // authenticated existing self below. policy always returns nil in the proved standard identity modes.
   for(const char*k:{"policies","standard_modes"}){CloneAiCloneProofStack a(L);if(!capture(f,k)||!cloneAiCloneProofPlain(L,-1))return no("predictable mode table");protectedRoots.insert(lua_topointer(L,-1));if(!std::strcmp(k,"standard_modes")&&lua_topointer(L,-1)!=standardModes)return no("mode registration capture alias changed");}
  }
  {CloneAiCloneProofStack z(L);field(L,env,"updateAlivePlayerRoles");const int f=lua_absindex(L,-1);
   if(!closure(f,mode,2458,2466,{"current_ai","policy","counts"})||!capturedClosure(f,"counts",cloneAiCloneProofSmart,2105,2116))return false;
   {CloneAiCloneProofStack a(L);if(!capture(f,"policy")||!policy(-1))return false;}{CloneAiCloneProofStack a(L);if(!capture(f,"current_ai")||!currentAI(-1))return false;}}
  if(!fieldClosure(ns,"qlist_cached",cloneAiCloneProofSmart,443,454)||!fieldClosure(ns,"QList2Table",cloneAiCloneProofUtility,44,51)
   ||!fieldClosure(ns,"getValue",cloneAiCloneProofSmart,560,563)||!fieldClosure(ns,"getDefense",cloneAiCloneProofSmart,574,709))return false;
  {CloneAiCloneProofStack z(L);field(L,ns,"ai_compare_funcs");protectedRoots.insert(lua_topointer(L,-1));if(!cloneAiCloneProofPlain(L,-1)||!fieldClosure(lua_absindex(L,-1),"chaofeng",cloneAiCloneProofSmart,1165,1167))return false;}
  {CloneAiCloneProofStack z(L);field(L,ns,"qlist");const int f=lua_absindex(L,-1);
   if(!closure(f,cloneAiCloneProofUtility,19,35,{"qlist_userdata_iterator","qlist_table_iterator"})
    ||!capturedClosure(f,"qlist_userdata_iterator",cloneAiCloneProofUtility,7,11)||!capturedClosure(f,"qlist_table_iterator",cloneAiCloneProofUtility,13,17))return false;}
  return true;
 }

 bool ordinaryIdentityGraph(){
  // The four-player identity constructor reaches both role outcomes. Prove
  // the complete union before any role/mark/hand lookup. This finite domain
  // uses the public non-humanized option, and the monotonic native no-controller
  // history; neither a current private scalar nor an empty heap seeds authority.
  if(r.getMode()!="04p")return true;
  if(Config.value("AIHumanized",true).toBool()||Player::controllerTagEverSet())
   return no("04p constructor public non-humanized/controller source domain");
  if(r.getAllPlayers(true).size()!=4)return no("04p complete four-player roster source domain");
  {CloneAiCloneProofStack z(L);field(L,ns,"ai_humanized");if(!lua_isnil(L,-1)&&(lua_type(L,-1)!=LUA_TBOOLEAN||lua_toboolean(L,-1)))return no("public constructor cached humanized option");}
  for(auto*p:r.getAllPlayers(true))
   if(!p||typeid(*p)!=typeid(ServerPlayer)||!hasAIPlayerEmptyMarksOrigin(*p))
    return no("04p constructor complete original mark-writer domain");
  struct F{const char*name;int first,last;bool method;};
  for(const F&f:{F{"getActualController",1994,2018,true},F{"getOverflow",5209,5241,true},
   F{"sort",1181,1249,true},F{"getKnownCards",5821,5847,false}}){
   CloneAiCloneProofStack z(L);if(f.method){if(!lookup(dict,f.name))return false;}else field(L,env,f.name);
   if(!closure(-1,cloneAiCloneProofSmart,f.first,f.last))return false;
  }
  {CloneAiCloneProofStack z(L);if(!lookup(dict,"objectiveLevel")||!closure(-1,mode,2374,2381,{"policy","objective"}))return false;
   const int f=lua_absindex(L,-1);
   {CloneAiCloneProofStack a(L);if(!capture(f,"policy")||!policy(-1))return false;}
   if(!capture(f,"objective")||!closure(-1,cloneAiCloneProofSmart,2036,2045,{"_smartai_objectiveLevel"})
    ||!capturedClosure(-1,"_smartai_objectiveLevel",cloneAiCloneProofSmart,1688,1946))return false;
  }
  {CloneAiCloneProofStack z(L);if(!lookup(dict,"adjustAIRole")||!closure(-1,mode,2429,2432,{"policy","adjust"}))return false;
   const int f=lua_absindex(L,-1);
   {CloneAiCloneProofStack a(L);if(!capture(f,"policy")||!policy(-1))return false;}
   if(!capturedClosure(f,"adjust",cloneAiCloneProofSmart,7812,7823))return false;
  }
  {CloneAiCloneProofStack z(L);field(L,ns,"gameProcess");const int f=lua_absindex(L,-1);
   if(!closure(f,mode,2443,2449,{"current_ai","policy","process"})
    ||!capturedClosure(f,"process",cloneAiCloneProofSmart,1642,1686))return false;
   {CloneAiCloneProofStack a(L);if(!capture(f,"policy")||!policy(-1))return false;}
   {CloneAiCloneProofStack a(L);if(!capture(f,"current_ai")||!currentAI(-1))return false;}
  }
  for(const F&f:{F{"getCachedAlivePlayers",457,460,false},F{"isLordHealthy",1600,1611,false},F{"isLordInDanger",1613,1625,false}})
   if(!fieldClosure(ns,f.name,cloneAiCloneProofSmart,f.first,f.last))return false;
  if(!fieldClosure(env,"aiRejectValueView","lua/ai/value-boundary.lua",12,16)
   ||!fieldClosure(env,"aiValueKind","lua/ai/value-boundary.lua",5,8))return false;
  // objective only passes literal hp/chaofeng to sort. Its start=true defense
  // call takes getValue's scalar arm under the public AIHumanized=false domain.
  // getKnownCards receives the exact owner userdata and default literal &h;
  // original-empty public-card ancestry excludes hand-pile object recursion.
  for(auto lib:{qMakePair("math",QStringList{"min","max"}),qMakePair("string",QStringList{"find","match"})}){
   CloneAiCloneProofStack z(L);field(L,env,lib.first);if(!cloneAiCloneProofPlain(L,-1))return no("identity library metatable");
   for(const auto&k:lib.second)if(!native(lua_absindex(L,-1),k.toUtf8().constData(),QString(lib.first)+'.'+k))return false;
  }
  {CloneAiCloneProofStack z(L);field(L,env,"string");field(L,-1,"contains");
   // Optional native-loaded addFunction replaces the core helper with the
   // pattern-search variant. Both complete source bodies call only the same
   // authenticated string.find; no selected runtime outcome is used here.
   const bool extension=matchAIPinnedFunction(r,L,-1,"extensions/addFunction.lua",1557,1559);
   if(!closure(-1,extension?"extensions/addFunction.lua":cloneAiCloneProofUtility,extension?1557:91,extension?1559:93))return false;
  }
  {CloneAiCloneProofStack z(L);field(L,env,"table");if(!fieldClosure(lua_absindex(L,-1),"contains",cloneAiCloneProofUtility,95,108))return false;}
  if(!native(ns,"ControllerTagInUse","sgs.ControllerTagInUse"))return false;
  for(const char*k:{"ai_gameProcess","ai_gameProcess_arg","UnknownRebel","target_role"}){
   CloneAiCloneProofStack z(L);field(L,!std::strcmp(k,"target_role")?env:ns,k);if(!scalar(L,-1))return no("identity scalar source operand");
   if(std::strcmp(k,"target_role")&&lua_isnil(L,-1)&&!matchAINativeNamespaceRead(r,L,"sgs",k))return no("identity namespace scalar read");
   if(std::strcmp(k,"target_role")&&!matchAINativeNamespaceWrite(r,L,"sgs",k))return no("identity namespace writer");
  }
  // All readonly native descendants, including complete validity/correction
  // families, are admitted by the independent OwnerCardRules lexical scope.
  const QHash<QString,QStringList> methods={
   {"ServerPlayer",{"getMark","getMaxHp","hasFlag","hasSkill","getAliveSiblings","getHujia","getPhase","getKingdom","getCardCount","getMaxCards","getHandcards","canSeeHandcard","isCardLimited","inMyAttackRange"}},
   {"Room",{"getLord","getCurrent"}}, {"SPlayerList",{"removeOne"}},
   {"CardList",{"length","at"}}, {"PlayerList",{"length","at"}}, {"IntList",{"at"}}};
  for(auto i=methods.cbegin();i!=methods.cend();++i)for(const auto&k:i.value())
   if(!matchAINativeClassMethod(r,L,i.key(),k))return no("identity native method "+i.key()+'.'+k);
  if(!matchAINativeDerivedMethods(r,L,"Card",{"hasFlag"}))return no("identity physical card flag method");
  for(const char*k:{"Card_MethodIgnore","Player_Finish"}){CloneAiCloneProofStack z(L);field(L,ns,k);if(!matchAINativeConstant(r,L,-1,QString("sgs.")+k))return no("identity constant");}
  return true;
 }
 bool rootsAndData(){
  globals(L);env=lua_absindex(L,-1);if(!cloneAiCloneProofPlain(L,env))return no("global environment metatable");protectedRoots.insert(lua_topointer(L,env));
  field(L,env,"sgs");ns=lua_absindex(L,-1);if(!lua_istable(L,ns))return no("sgs namespace");protectedRoots.insert(lua_topointer(L,ns));
  {CloneAiCloneProofStack z(L);field(L,ns,"ai_use_priority");if(!cloneAiCloneProofPlain(L,-1))return no("actual priority root");protectedRoots.insert(lua_topointer(L,-1));}
  field(L,env,"SmartAI");cls=lua_absindex(L,-1);if(!lua_istable(L,cls))return no("actual class");
  for(int type:{LUA_TNIL,LUA_TNUMBER,LUA_TBOOLEAN,LUA_TFUNCTION}){CloneAiCloneProofStack z(L);if(primitiveMetatable(L,type))return no("primitive metamethod");}
  if(!matchAINativeStringMetatable(r,L)||!matchAINativeValueMetamethods(r,L,&why))return false;
  // The mutable caches, cached public mode and current_ai capability pair
  // come only from native namespace birth plus actual bootstrap/constructor
  // writes. Do not traverse current private values, sizes or cache occupancy.
  if(!hasAICloneInputLineage(r,L))return no("constructor input source lineage unavailable");
  {CloneAiCloneProofStack z(L);field(L,env,"_VERSION");if(lua_type(L,-1)!=LUA_TSTRING)return no("version concatenation operand");}
  // Every source namespace write must use the original raw/native setter path.
  for(const char*k:{"defense","drawData","turncount","throwData","damageData","convertData","recoverData","sort_time","getMode","AIChat","ai_humanized","debugmode","explicit_renegade"})
   if(!matchAINativeNamespaceWrite(r,L,"sgs",k))return no(QString("namespace writer ")+k);
  return true;
 }
 bool leaves(){
  for(const char*k:{"type","ipairs","pairs","assert","setmetatable","error","tostring"})if(!native(env,k,k))return false;
  for(auto lib:{qMakePair("table",QStringList{"insert","sort"}),qMakePair("string",QStringList{"sub"}),qMakePair("os",QStringList{"time"}),qMakePair("debug",QStringList{"traceback"})}){
   CloneAiCloneProofStack z(L);field(L,env,lib.first);if(!cloneAiCloneProofPlain(L,-1))return no("library metatable");protectedRoots.insert(lua_topointer(L,-1));for(const auto&k:lib.second)if(!native(lua_absindex(L,-1),k.toUtf8().constData(),QString(lib.first)+'.'+k))return false;}
  if(!native(ns,"GetConfig","sgs.GetConfig"))return false;
  for(const char*k:{"LuaAI","QVariant","IntList"}){CloneAiCloneProofStack z(L);field(L,ns,k);if(!matchAINativeScratchConstructor(r,L,-1,k))return no(QString("native factory ")+k);}
  if(!matchAINativeClassSetter(r,L,"LuaAI","callback"))return no("LuaAI callback native setter");
  const QHash<QString,QStringList> methods={
   {"ServerPlayer",{"getRoom","getRole","objectName","getHp","getHandcardNum","getHandPile"}},
   {"Room",{"getTag","setTag","getMode","getPlayers","getAllPlayers","getAlivePlayers","writeToConsole"}},
   {"LuaAI",{"isFriend","isEnemy","relationTo"}},
   {"QVariant",{"toBool"}}, {"IntList",{"length"}}, {"SPlayerList",{"length","at"}}};
  for(auto i=methods.cbegin();i!=methods.cend();++i)for(const auto&k:i.value())if(!matchAINativeClassMethod(r,L,i.key(),k))return no("native method "+i.key()+'.'+k);
  {CloneAiCloneProofStack z(L);field(L,ns,"AI_Neutrality");if(!matchAINativeConstant(r,L,-1,"sgs.AI_Neutrality"))return no("relation constant");}
  return true;
 }
};
}
bool registerAICloneSources(Room&r,lua_State*L,QString*why){
 struct Source{const char*path;const char*hash;};const Source sources[]={
  {cloneAiCloneProofSmart,"f92033319db3c6aac3504b4b6ddccf5edb4403fb120679b303dedfe673a3efe5"},
  {mode,"cd0a6fa3b6de7e90db276846f04fe54b01199c28a1d68df3c452f6b14b8ac3e5"},
  {middle,"32938b2068cb9b8a5b6a8d734bce0a6c73020f6a128e9cf9bfca200490cedf6e"},
  {cloneAiCloneProofUtility,"b76cdc44c0fec46f1e10d918c6e0676c5e301ed92cd5b0e35da882606a7d4d3a"},
  {heg,"9ad128880b7876a95d022551533a8168ef924173a29d704af837cd5edb98ff7f"},
  {"lua/ai/value-boundary.lua","6a4635d93a996a65e93f3981a726e1dcc0b4b25467ceae67d41fac403631a897"},
  {"extensions/addFunction.lua","32938f895048d1abc62a119f179ed356fda155d1ccc8ba40664ff81c2ecba046"}};
 for(const auto&s:sources)if(!registerAIPinnedSource(r,L,s.path,s.hash,why))return false;return true;
}
bool proveAICloneSource(Room&r,lua_State*L,QString*why){
 auto fail=[&](const char*s){if(why)*why=QString::fromLatin1(s);return false;};
 if((r.getMode()!="02p"&&r.getMode()!="04p")||Config.EnableHegemony||r.getScenario())return fail("public constructor domain");
 auto*rt=r.roomRuntime();if(!rt||rt->isClosing()||rt->lua().rawState()!=L||!rt->lua().isCurrentThreadOwner()||!capacity(L,8192))return fail("constructor native runtime/inspection stack");
 if(!proveAIServerLogEffects(r))return fail("constructor logging host graph");
 CloneAiCloneProofStack z(L);CloneAiCloneProofCheck c{r,L};const bool ok=c.rootsAndData()&&c.sourceGraph()&&c.ordinaryIdentityGraph()&&c.leaves()&&c.rootsAndData();if(why)*why=c.why;return ok;
}

// ---- playloop-filter ----
#include "ai-strategy/anime.h"
#include "structs.h"
extern "C" {
}
extern bool matchAINativeRoom(lua_State*,int,const Room*);
extern bool matchAINativePlayer(lua_State*,int,const ServerPlayer*);
namespace {
using namespace AILuaInspect;
constexpr const char*playloopFilterSmart="lua/ai/smart-ai.lua";
constexpr const char*playloopFilterMiddle="lua/lib/middleclass.lua";
constexpr const char*middleHash="32938b2068cb9b8a5b6a8d734bce0a6c73020f6a128e9cf9bfca200490cedf6e";
constexpr const char*smartHash="f92033319db3c6aac3504b4b6ddccf5edb4403fb120679b303dedfe673a3efe5";
struct PlayloopFilterStack{lua_State*L;int top;explicit PlayloopFilterStack(lua_State*s):L(s),top(lua_gettop(s)){}~PlayloopFilterStack(){lua_settop(L,top);}};
enum PlayloopFilterKind{PlayloopFilterGlobals,PlayloopFilterNamespace,Events,Row,Filter,Queue,PlayloopFilterQlist,PlayloopFilterConnect,Crossbow,Slash,ClassSource,SelfSource,Count};
void trace(const char*tag,int a=0,int b=0)noexcept{
 if(qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")){std::fprintf(stderr,"R56_PLAYLOOP_TRACE tag=%s a=%d b=%d\n",tag,a,b);std::fflush(stderr);}
}
struct PlayloopFilterState;
void playloopFilterChanged(lua_State*,const lua_TableObservation*,void*) noexcept;
void playloopFilterBorn(lua_State*,const lua_TableBirthObservation*,void*) noexcept;
thread_local AIPlayLoopFilterScope*playloopFilterActive=nullptr;
struct PlayloopFilterState final:QObject{
 Room&r;lua_State*L;quint64 generation,epoch;bool alive=true,ready=false,completed=false,prepareAttempted=false;
 bool generationScalar=false,turnScalar=false,pristine=false,resetComplete=false;
 quint64 resetSerial=0;
 const void*root[Count]={};const void*latest[Count]={};
 QHash<const void*,PlayloopFilterKind>watches;
 // These identities exist only after actual middleclass NEWTABLE receipts.
 // No preflight scan populates or repairs these source ledgers.
 QHash<const void*,const void*>selfClasses,selfDictionaries;
 PlayloopFilterState(Room&room,const LuaRuntime::NativeNamespaceBirth&b):QObject(&room),r(room),L(b.state()),generation(b.generation()),epoch(room.roomRuntime()->prioritySourceEpoch()){
  root[PlayloopFilterGlobals]=b.globalsIdentity();root[PlayloopFilterNamespace]=b.namespaceIdentity();
 }
 ~PlayloopFilterState()override{lua_unobservetablebirth(L,playloopFilterBorn,this);for(auto i=watches.cbegin();i!=watches.cend();++i)lua_unobservetableidentity(L,i.key(),playloopFilterChanged,this);}
 void retire(int site=0)noexcept{if(!alive)return;trace("source-retire",site,int(completed));alive=false;retireAIProjectionSource(r,L);}
 bool watch(const void*p,PlayloopFilterKind k){
  if(!p||watches.contains(p)||!lua_observetableidentity(L,p,playloopFilterChanged,this))return false;
  try{watches.insert(p,k);}catch(...){lua_unobservetableidentity(L,p,playloopFilterChanged,this);throw;}return true;
 }
 void forget(const void*p){if(!p)return;lua_unobservetableidentity(L,p,playloopFilterChanged,this);watches.remove(p);}
};
PlayloopFilterState*playloopFilterFind(const Room&r){for(auto*c:r.children())if(auto*s=dynamic_cast<PlayloopFilterState*>(c))return s;return nullptr;}
bool playloopFilterCurrent(const PlayloopFilterState&s){auto*rt=s.r.roomRuntime();return s.alive&&rt&&!rt->isClosing()&&s.L&&rt->lua().rawState()==s.L
 &&rt->lua().generation()==s.generation&&rt->lua().isCurrentThreadOwner()&&s.epoch&&rt->prioritySourceEpoch()==s.epoch;}
bool key(const lua_TableObservation&e,const char*k){return e.string_key&&e.string_key_length==std::strlen(k)&&!std::memcmp(e.string_key,k,e.string_key_length);}
bool playloopFilterAt(PlayloopFilterState&s,lua_State*L,const lua_TableObservation&e,int first,int last,int line,const char*namespaceKey=nullptr){
 if(e.event!=LUA_TABLE_OBSERVER_WRITE)return false;
 if(e.lua_function_identity&&(e.flags&(LUA_TABLE_OBSERVER_VM_FAST|LUA_TABLE_OBSERVER_VM_SLOW)))
  return matchAIPinnedInstruction(s.r,L,e.lua_function_identity,e.lua_instruction_index,playloopFilterSmart,first,last,line);
 if(!namespaceKey||e.table_identity!=s.root[PlayloopFilterNamespace]
  ||!matchAINativeNamespaceWriteFrame(s.r,L,e.table_identity,QStringLiteral("sgs"),namespaceKey))return false;
 auto*ci=L->ci->previous;
 if(!ci||!isLua(ci)||!ttisLclosure(s2v(ci->func.p)))return false;
 auto*f=clLvalue(s2v(ci->func.p));auto*p=f->p;
 if(ci->u.l.savedpc<=p->code||ci->u.l.savedpc>p->code+p->sizecode)return false;
 return matchAIPinnedInstruction(s.r,L,f,int(ci->u.l.savedpc-p->code)-1,playloopFilterSmart,first,last,line);
}
bool integerKey(lua_State*L,const lua_TableObservation&e,lua_Integer&out){
 if(!e.lua_function_identity||!isLua(L->ci)||!ttisLclosure(s2v(L->ci->func.p)))return false;
 auto*f=clLvalue(s2v(L->ci->func.p));auto*p=f->p;const int pc=e.lua_instruction_index;
 if(f!=e.lua_function_identity||pc<0||pc>=p->sizecode)return false;const Instruction i=p->code[pc];
 if(GET_OPCODE(i)==OP_SETI){out=GETARG_B(i);return true;}
 if(GET_OPCODE(i)!=OP_SETTABLE||GETARG_B(i)>=p->maxstacksize)return false;
 const TValue*v=s2v(L->ci->func.p+1+GETARG_B(i));if(!ttisinteger(v))return false;out=ivalue(v);return true;
}
bool fresh(const PlayloopFilterState&s,const lua_TableObservation&e,PlayloopFilterKind k){return e.value_type==LUA_TTABLE&&e.value_table_identity
 &&e.value_table_identity==s.latest[k]&&s.watches.value(e.value_table_identity,PlayloopFilterGlobals)==k;}
bool sourceKind(const PlayloopFilterState&s,const void*p,PlayloopFilterKind k){
 auto i=s.watches.constFind(p);return p&&i!=s.watches.cend()&&*i==k;
}
bool selfClassWrite(PlayloopFilterState&s,lua_State*L,const lua_TableObservation&e){
 return inAICloneScope(s.r,L)&&!s.selfClasses.contains(e.table_identity)
  &&e.lua_function_identity&&(e.flags&(LUA_TABLE_OBSERVER_VM_FAST|LUA_TABLE_OBSERVER_VM_SLOW))
  &&e.value_type==LUA_TTABLE&&sourceKind(s,e.value_table_identity,ClassSource)
  &&matchAIPinnedInstruction(s.r,L,e.lua_function_identity,e.lua_instruction_index,playloopFilterMiddle,98,101,100);
}
bool selfMetatableWrite(PlayloopFilterState&s,lua_State*L,const lua_TableObservation&e){
 // Exact first native setter immediately called by allocate's pinned source.
 // Inspect its actual arguments, not a later current metatable snapshot.
 if(!inAICloneScope(s.r,L)||!s.selfClasses.contains(e.table_identity)
  ||s.selfDictionaries.contains(e.table_identity)
  ||!matchAINativeLeafFrame(s.r,L,QStringLiteral("setmetatable"))
  ||table(L,1)!=e.table_identity||!sourceKind(s,table(L,2),ClassSource))return false;
 auto*ci=L->ci->previous;if(!ci||!isLua(ci)||!ttisLclosure(s2v(ci->func.p)))return false;
 const auto*f=clLvalue(s2v(ci->func.p));const auto*p=f->p;
 if(ci->u.l.savedpc<=p->code||ci->u.l.savedpc>p->code+p->sizecode)return false;
 const int pc=int(ci->u.l.savedpc-p->code)-1;const auto op=GET_OPCODE(p->code[pc]);
 return (op==OP_CALL||op==OP_TAILCALL)&&matchAIPinnedInstruction(s.r,L,f,pc,playloopFilterMiddle,98,101,100);
}
struct Reset{const char*key;PlayloopFilterKind kind;int line;unsigned bit;};
constexpr Reset resets[]={{"qlist_cache",PlayloopFilterQlist,434,2},{"ai_connect_cache",PlayloopFilterConnect,436,4},
 {"ai_crossbow_cache",Crossbow,438,16},{"ai_slash_query_cache",Slash,439,32},{"_deferred_delete_cards",Queue,5485,128}};
constexpr unsigned allWrites=511; // current_self, six invalidation writes, queue, filterData
bool nestedCloneWrite(PlayloopFilterState&s,lua_State*L,const lua_TableObservation&e){
 return inAICloneScope(s.r,L)&&e.value_type==LUA_TTABLE&&e.string_key
  &&playloopFilterAt(s,L,e,443,454,451); // complete Clone helper graph + native source producer
}
}
struct AIPlayLoopFilterScope::Data{
 Room&r;LuaAI*ai;ServerPlayer*p;lua_State*L;const QVariant*original;AIPlayLoopFilterScope*previous=nullptr;
 PlayloopFilterState*s=nullptr;std::unique_ptr<AIAsyncUniverseScope>async;
 const void*callback=nullptr,*self=nullptr,*frame=nullptr;quint64 generation=0,epoch=0,transport=0;
 int depth=0;unsigned writes=0;bool ready=false,entered=false,invalid=false;
 Data(Room&room,LuaAI*a,ServerPlayer*player,lua_State*l,const QVariant&v):r(room),ai(a),p(player),L(l),original(&v){}
};
struct AIPlayLoopAccess{
 static AIPlayLoopFilterScope::Data*call(Room&r,lua_State*L,bool entered=true)noexcept{
  if(!playloopFilterActive||!playloopFilterActive->d||&playloopFilterActive->d->r!=&r||playloopFilterActive->d->L!=L||!playloopFilterActive->accepted())return nullptr;
  return !entered||playloopFilterActive->d->entered?playloopFilterActive->d.get():nullptr;
 }
};
namespace {
bool playloopFilterPermitted(PlayloopFilterState&s,lua_State*L,const lua_TableObservation&e){
 auto*c=AIPlayLoopAccess::call(s.r,L);if(!c||c->s!=&s||e.event!=LUA_TABLE_OBSERVER_WRITE)return false;
 if(e.table_identity==s.root[PlayloopFilterGlobals])return key(e,"current_self")&&e.value_type==LUA_TTABLE
  &&e.value_table_identity==c->self&&playloopFilterAt(s,L,e,344,375,360);
 if(e.table_identity==s.root[Filter]){lua_Integer event=0;return e.value_type==LUA_TUSERDATA
  &&playloopFilterAt(s,L,e,2491,2866,2493)&&integerKey(L,e,event)&&event==int(EventPlayPhaseLoop);}
 if(e.table_identity!=s.root[PlayloopFilterNamespace])return false;
 for(const auto&w:resets)if(key(e,w.key))return fresh(s,e,w.kind)
  &&playloopFilterAt(s,L,e,w.kind==Queue?5481:433,w.kind==Queue?5486:441,w.line,w.key);
 if(key(e,"ai_active_recommends_cache"))return e.value_type==LUA_TNIL&&playloopFilterAt(s,L,e,433,441,437,"ai_active_recommends_cache");
 if(key(e,"qlist_cache_gen"))return e.value_type==LUA_TNUMBER&&s.generationScalar&&playloopFilterAt(s,L,e,433,441,440,"qlist_cache_gen");
 return false;
}
void noteWrite(PlayloopFilterState&s,lua_State*L,const lua_TableObservation&e){
 auto*c=AIPlayLoopAccess::call(s.r,L);if(!c){s.retire(__LINE__);return;}unsigned bit=0;
 if(e.table_identity==s.root[PlayloopFilterGlobals])bit=1;
 else if(e.table_identity==s.root[Filter])bit=256;
 else if(key(e,"ai_active_recommends_cache"))bit=8;
 else if(key(e,"qlist_cache_gen"))bit=64;
 else for(const auto&w:resets)if(key(e,w.key)){
  bit=w.bit;s.root[w.kind]=e.value_table_identity;
  if(w.kind==PlayloopFilterQlist){
   if(s.resetSerial==std::numeric_limits<quint64>::max()){s.retire(__LINE__);return;}
   ++s.resetSerial;s.resetComplete=false;s.pristine=true;
  }else if(w.kind==PlayloopFilterConnect)s.resetComplete=(c->writes&2)!=0;
  break;
 }
 if(!bit||(c->writes&bit)){s.retire(__LINE__);return;}c->writes|=bit;trace("writer-receipt",int(bit),int(c->writes));
}
void playloopFilterChanged(lua_State*L,const lua_TableObservation*ep,void*u)noexcept{
 auto&s=*static_cast<PlayloopFilterState*>(u);if(!ep||!L||!s.L||G(L)!=G(s.L))return;const auto&e=*ep;
 auto i=s.watches.constFind(e.table_identity);if(i==s.watches.cend())return;const PlayloopFilterKind k=*i;
 if(e.event==LUA_TABLE_OBSERVER_FREE){
  s.watches.remove(e.table_identity);s.selfClasses.remove(e.table_identity);s.selfDictionaries.remove(e.table_identity);
  bool rooted=s.completed&&(k==ClassSource||k==SelfSource);
  for(int n=0;n<Count;++n){if(s.root[n]==e.table_identity){s.root[n]=nullptr;rooted=true;}if(s.latest[n]==e.table_identity)s.latest[n]=nullptr;}
  if(rooted)s.retire(__LINE__);return;
 }
 if(!s.alive||e.event==LUA_TABLE_OBSERVER_READ||e.event==LUA_TABLE_OBSERVER_ENUMERATE)return;
 try{
  if(k==ClassSource){
   // Complete bootstrap source token closes all class construction/registration
   // writes. Every later alias write or metatable replacement retires forever.
   if(s.completed){trace("class-source-mutation",e.event);s.retire(__LINE__);}return;
  }
  if(k==SelfSource){
   if(e.event==LUA_TABLE_OBSERVER_METATABLE){
    if(!selfMetatableWrite(s,L,e)){trace("self-source-metatable");s.retire(__LINE__);return;}
    s.selfDictionaries.insert(e.table_identity,table(L,2));trace("self-source-metatable-receipt");return;
   }
   if(e.event!=LUA_TABLE_OBSERVER_WRITE)return;
   if(key(e,"filterEvent")){trace("self-source-filter-write");s.retire(__LINE__);return;}
   if(key(e,"class")){
    if(!selfClassWrite(s,L,e)){trace("self-source-class-write");s.retire(__LINE__);return;}
    s.selfClasses.insert(e.table_identity,e.value_table_identity);
   }
   return; // Existing CloneInputLineage owns room/player and other self effects.
  }
  if(e.event==LUA_TABLE_OBSERVER_METATABLE){s.retire(__LINE__);return;}
  if(e.event!=LUA_TABLE_OBSERVER_WRITE)return;
  if(k==PlayloopFilterGlobals){if(key(e,"sgs")){s.retire(__LINE__);return;}if(s.completed&&playloopFilterPermitted(s,L,e))noteWrite(s,L,e);return;} // clone lineage owns all postbootstrap global writers
  if(k==PlayloopFilterNamespace){
   const struct {const char*key;PlayloopFilterKind kind;int line;}initial[]={{"ai_event_callback",Events,117},{"filterData",Filter,2361},{"_deferred_delete_cards",Queue,5479}};
   for(const auto&w:initial)if(key(e,w.key)){
    if(!s.completed){
     if(s.root[w.kind]||!s.ready||!fresh(s,e,w.kind)||!playloopFilterAt(s,L,e,0,0,w.line,w.key)){s.retire(__LINE__);return;}
     s.root[w.kind]=e.value_table_identity;return;
    }
    if(w.kind==Queue&&playloopFilterPermitted(s,L,e)){noteWrite(s,L,e);return;}
    s.retire(__LINE__);return;
   }
   if(key(e,"qlist_cache_gen")){
    if(!s.completed){if(!s.ready||s.generationScalar||e.value_type!=LUA_TNUMBER||!playloopFilterAt(s,L,e,0,0,244,"qlist_cache_gen")){s.retire(__LINE__);return;}s.generationScalar=true;return;}
    if(playloopFilterPermitted(s,L,e)){noteWrite(s,L,e);return;}s.retire(__LINE__);return;
   }
   if(key(e,"turncount")){
    if(s.completed&&inAICloneScope(s.r,L)&&e.value_type==LUA_TNUMBER&&playloopFilterAt(s,L,e,339,429,380,"turncount")){s.turnScalar=true;return;}
    s.retire(__LINE__);return;
   }
   for(const auto&w:resets)if(key(e,w.key)){
    if(!s.completed)return; // complete bootstrap graph owns unrelated initial cache roots
    if(playloopFilterPermitted(s,L,e)){noteWrite(s,L,e);return;}
    // The pinned aiConnect producer assigns its root even after the event
    // reset. Preserve that actual source write only when it carries the exact
    // previously issued reset identity. Never adopt a replacement or restore
    // a retired lineage; equal contents alone grant nothing.
    if(w.kind==PlayloopFilterConnect&&allowAIConnectionCacheWrite(s.r,L,e)
      &&(!s.root[PlayloopFilterConnect]||s.root[PlayloopFilterConnect]==e.value_table_identity))return;
    s.retire(__LINE__);return;
   }
   if(key(e,"ai_active_recommends_cache")&&s.completed){if(playloopFilterPermitted(s,L,e))noteWrite(s,L,e);else s.retire(__LINE__);}
   return;
  }
  if(k==Events){
   lua_Integer event=0;
   if(s.completed||!s.ready||!fresh(s,e,Row)||!playloopFilterAt(s,L,e,0,0,235)||!integerKey(L,e,event)
    ||event<int(NonTrigger)||event>int(EventForDiy)){s.retire(__LINE__);return;}
   if(event==int(EventPlayPhaseLoop)){if(s.root[Row]){s.retire(__LINE__);return;}s.root[Row]=e.value_table_identity;}
   else s.forget(e.value_table_identity);
   return;
  }
  if(k==Filter){if(s.completed&&playloopFilterPermitted(s,L,e)){noteWrite(s,L,e);return;}s.retire(__LINE__);return;}
  if(k==PlayloopFilterQlist||k==PlayloopFilterConnect){
   // Preserve valid source evolution, but never late-capture a populated root.
   if(e.table_identity!=s.root[k]){s.retire(__LINE__);return;}
   if(allowAIConnectionCacheWrite(s.r,L,e)||(k==PlayloopFilterQlist&&nestedCloneWrite(s,L,e))){s.pristine=false;return;}
  }
  // Selected event row, deferred queue and other caches have no nested writer
  // in this bounded graph. Same-value nil writes are still real writes.
  s.retire(__LINE__);
 }catch(...){s.retire(__LINE__);}
}
void playloopFilterBorn(lua_State*L,const lua_TableBirthObservation*e,void*u)noexcept{
 auto&s=*static_cast<PlayloopFilterState*>(u);if(!e||!s.alive||!s.ready||!L||G(L)!=G(s.L)||!e->lua_function_identity)return;
 try{
  PlayloopFilterKind kind=Count;
  // All four tables in _createClass's literal are fresh public class metadata.
  // They are watched before initialization writes, then frozen at completion.
  if(!s.completed&&matchAIPinnedInstruction(s.r,L,e->lua_function_identity,e->lua_instruction_index,playloopFilterMiddle,48,54,49))kind=ClassSource;
  else if(s.completed&&inAICloneScope(s.r,L)
   &&matchAIPinnedInstruction(s.r,L,e->lua_function_identity,e->lua_instruction_index,playloopFilterMiddle,98,101,100))kind=SelfSource;
  else if(!s.completed){
   for(const auto&w:{qMakePair(117,Events),qMakePair(235,Row),qMakePair(2361,Filter),qMakePair(5479,Queue)})
    if(matchAIPinnedInstruction(s.r,L,e->lua_function_identity,e->lua_instruction_index,playloopFilterSmart,0,0,w.first)){kind=w.second;break;}
  }else if(AIPlayLoopAccess::call(s.r,L)){
   for(const auto&w:resets)if(matchAIPinnedInstruction(s.r,L,e->lua_function_identity,e->lua_instruction_index,
     playloopFilterSmart,w.kind==Queue?5481:433,w.kind==Queue?5486:441,w.line)){kind=w.kind;break;}
  }
  if(kind==Count)return;
  const auto*f=static_cast<const LClosure*>(e->lua_function_identity);const int pc=e->lua_instruction_index;
  if(!f->p||pc<0||pc>=f->p->sizecode||GET_OPCODE(f->p->code[pc])!=OP_NEWTABLE||!s.watch(e->table_identity,kind)){s.retire(__LINE__);return;}
  if(kind!=ClassSource&&kind!=SelfSource)s.latest[kind]=e->table_identity;
  trace("source-birth",int(kind),s.completed?1:0);
 }catch(...){s.retire(__LINE__);}
}
bool playloopFilterPlain(lua_State*L,int i){auto*t=table(L,i);return t&&!t->metatable;}
struct Graph{
 PlayloopFilterState&s;Room&r;lua_State*L;LuaAI*ai;ServerPlayer*actor;int env=0,ns=0,self=0;const void*callback=nullptr,*selfIdentity=nullptr;
 Graph(PlayloopFilterState&state,LuaAI*a,ServerPlayer*p):s(state),r(state.r),L(state.L),ai(a),actor(p){}
 bool closure(int i,int first,int last){
  i=lua_absindex(L,i);if(!matchAIPinnedFunction(r,L,i,playloopFilterSmart,first,last)){trace("closure-source",first,last);return false;}
  for(int u=1;;++u){const char*k=lua_getupvalue(L,i,u);if(!k)break;const bool ok=!std::strcmp(k,"_ENV")&&lua_rawequal(L,-1,env);lua_pop(L,1);if(!ok){trace("closure-environment",first,last);return false;}}return true;
 }
 bool filterClosure(int i){
  i=lua_absindex(L,i);
  // Full recursive Proto seal includes the inner pcall closure, its upvalue
  // routing, every event comparison and every helper read/call instruction.
  if(!matchAIPinnedFunction(r,L,i,playloopFilterSmart,2477,2867)){trace("filter-source");return false;}
  static_assert(EventPlayPhaseLoop!=TargetSpecified&&EventPlayPhaseLoop!=DamageInflicted&&EventPlayPhaseLoop!=HpRecover,
    "PlayLoop cannot execute the captured intention helper");
  bool sawEnv=false,sawIntention=false;
  for(int u=1;;++u){const char*k=lua_getupvalue(L,i,u);if(!k)break;bool ok=false;
   if(!std::strcmp(k,"_ENV")){
    ok=!sawEnv&&lua_rawequal(L,-1,env);sawEnv=true;
    if(!ok)trace("filter-capture-environment",u);
   }else if(!std::strcmp(k,"getCardIntention")){
    // Only captured into the child for three other event arms (2537/2628/2657).
    // Their native enum bindings are sealed in leaves(), and exact PlayLoop
    // arguments are required at entry. No helper call/effect is granted here.
    ok=!sawIntention&&closure(-1,1948,1956);sawIntention=true;
    if(!ok)trace("filter-capture-getCardIntention",1948,1956);
   }else trace("filter-capture-unexpected",u);
   lua_pop(L,1);if(!ok)return false;
  }
  if(!sawEnv||!sawIntention){trace("filter-capture-missing",int(sawEnv),int(sawIntention));return false;}
  return true;
 }
 bool lookup(int object,const char*k){
  object=lua_absindex(L,object);QSet<const void*>seen;
  for(int n=0;n<16;++n){if(!table(L,object)||seen.contains(lua_topointer(L,object)))return false;seen.insert(lua_topointer(L,object));
   field(L,object,k);if(!lua_isnil(L,-1))return true;lua_pop(L,1);
   if(!lua_getmetatable(L,object)){trace("lookup-no-metatable",n);return false;}
   // Lua reads the metatable's __index slot raw; that table may itself inherit.
   // Permit only source-born, continuously frozen class dictionaries. This is
   // source membership, never adoption of an arbitrary current shape/value.
   if(!sourceKind(s,table(L,-1),ClassSource)){trace("lookup-metatable-source",n);return false;}
   field(L,-1,"__index");if(!sourceKind(s,table(L,-1),ClassSource)){trace("lookup-index-source",n);return false;}object=lua_absindex(L,-1);
  }return false;
 }
 bool native(int t,const char*k,const QString&path){PlayloopFilterStack z(L);field(L,t,k);const bool ok=matchAINativeBinding(r,L,-1,path);if(!ok)trace(k);return ok;}
 bool spaceClosure(const char*k,int first,int last){PlayloopFilterStack z(L);field(L,ns,k);return closure(-1,first,last);}
 bool roots(){
  globals(L);env=lua_absindex(L,-1);if(!playloopFilterPlain(L,env)||lua_topointer(L,env)!=s.root[PlayloopFilterGlobals])return false;
  field(L,env,"sgs");ns=lua_absindex(L,-1);if(!table(L,ns)||lua_topointer(L,ns)!=s.root[PlayloopFilterNamespace])return false;
  for(const auto&x:{qMakePair("ai_event_callback",Events),qMakePair("filterData",Filter),qMakePair("_deferred_delete_cards",Queue)}){
   PlayloopFilterStack z(L);field(L,ns,x.first);if(!playloopFilterPlain(L,-1)||lua_topointer(L,-1)!=s.root[x.second])return false;
   if(x.second==Events){rawGeti(L,-1,int(EventPlayPhaseLoop));if(!playloopFilterPlain(L,-1)||lua_topointer(L,-1)!=s.root[Row])return false;}
  }
  // Empty row/queue authority is their actual NEWTABLE plus every subsequent
  // mutation observer, not size/value inspection at this entry.
  return s.generationScalar&&s.turnScalar;
 }
 bool callbackGraph(){
  // Diagnostic tags expose only the failed structural gate, never self fields,
  // userdata payloads, callback names, addresses or private game values.
  if(!ai){trace("callback-ai-null");return false;}
  if(typeid(*ai)!=typeid(LuaAI)){trace("callback-ai-type");return false;}
  if(!ai->callback.isValid()){trace("callback-invalid");return false;}
  if(ai->callback.state()!=L){trace("callback-state");return false;}
  rawGeti(L,LUA_REGISTRYINDEX,ai->callback.registryRef());const int fn=lua_absindex(L,-1);
  if(!matchAIPinnedFunction(r,L,fn,playloopFilterSmart,344,375)){trace("callback-source",344,375);return false;}callback=lua_topointer(L,fn);
  bool sawSelf=false,sawEnv=false;
  for(int u=1;;++u){const char*k=lua_getupvalue(L,fn,u);if(!k)break;
   if(!std::strcmp(k,"self")){
    if(sawSelf||!table(L,-1)){trace("callback-self-upvalue",u,int(sawSelf));return false;}sawSelf=true;self=lua_absindex(L,-1);
   }else if(!std::strcmp(k,"_ENV")){
    const bool ok=!sawEnv&&lua_rawequal(L,-1,env);lua_pop(L,1);
    if(!ok){trace("callback-env-upvalue",u,int(sawEnv));return false;}sawEnv=true;
   }else{trace("callback-unexpected-upvalue",u);return false;}
  }
  if(!sawSelf||!sawEnv){trace("callback-missing-upvalue",int(sawSelf),int(sawEnv));return false;}selfIdentity=lua_topointer(L,self);
  // Source-created self comes from the existing real CloneAI receipt; locating
  // the native observer owner uses only AI pointer identity, not skill state.
  const ServerPlayer*owner=nullptr;for(auto*p:r.getPlayers())if(p&&p->getSmartAI()==ai){if(owner){trace("callback-owner-duplicate");return false;}owner=p;}
  if(!owner){trace("callback-owner-missing");return false;}
  {PlayloopFilterStack z(L);
   if(!pushAIActualAISelf(r,owner,L)){trace("callback-self-receipt");return false;}
   if(!lua_rawequal(L,-1,self)){trace("callback-self-binding");return false;}
  }
  // Actual allocation and one-shot class/metatable writes precede all queries.
  // Self's filterEvent slot has remained absent since that literal's birth.
  if(!sourceKind(s,selfIdentity,SelfSource)||!s.selfClasses.contains(selfIdentity)
   ||!s.selfDictionaries.contains(selfIdentity)){trace("callback-self-source");return false;}
  {PlayloopFilterStack z(L);
   if(!lookup(self,"filterEvent")){trace("callback-filter-lookup");return false;}
   if(!filterClosure(-1)){trace("callback-filter-closure");return false;}
  }
  {PlayloopFilterStack z(L);
   if(!lookup(self,"room")){trace("callback-room-lookup");return false;}
   if(!matchAINativeRoom(L,-1,&r)){trace("callback-room-binding");return false;}
   if(!matchAINativeObjectMethods(r,L,-1,"Room",{"writeToConsole","outputEventStack"})){trace("callback-room-methods");return false;}
  }
  if(!spaceClosure("invalidate_qlist_cache",433,441)){trace("callback-invalidate-closure");return false;}
  if(!spaceClosure("flushDeferredDeleteCards",5481,5486)){trace("callback-flush-closure");return false;}
  return true;
 }
 bool leaves(){
  for(const char*k:{"type","pcall","pairs","ipairs","tostring"})if(!native(env,k,QString::fromLatin1(k)))return false;
  for(const auto&lib:{qMakePair("string",QStringList{"find","sub"}),qMakePair("io",QStringList{"open"})}){
   PlayloopFilterStack z(L);field(L,env,lib.first);if(!playloopFilterPlain(L,-1))return false;const int t=lua_absindex(L,-1);
   for(const auto&k:lib.second)if(!native(t,k.toUtf8().constData(),QString::fromLatin1(lib.first)+'.'+k))return false;
  }
  if(!matchAINativeFileMethods(r,L,{"write","close"})){trace("leaf-file");return false;}
  if(!matchAINativeStringMetatable(r,L)){trace("leaf-string-meta");return false;}
  if(!matchAINativeValueMetamethods(r,L)){trace("leaf-value-meta");return false;}
  if(!aiFocusBindings(r,L)){trace("leaf-focus");return false;}
  for(int type:{LUA_TNIL,LUA_TBOOLEAN,LUA_TNUMBER,LUA_TFUNCTION}){PlayloopFilterStack z(L);if(primitiveMetatable(L,type))return false;}
  // Both diagnostic flag outcomes are proved. A nil namespace read needs the
  // exact SWIG missing-field path; no branch is granted because the flag is off.
  {PlayloopFilterStack z(L);field(L,ns,"aiHandCardVisible");if(lua_isnil(L,-1)&&!matchAINativeNamespaceRead(r,L,"sgs","aiHandCardVisible"))return false;}
  for(const char*k:{"qlist_cache","ai_connect_cache","ai_active_recommends_cache","ai_crossbow_cache","ai_slash_query_cache","qlist_cache_gen","_deferred_delete_cards"})
   if(!matchAINativeNamespaceWrite(r,L,"sgs",QString::fromLatin1(k)))return false;
  for(const char*k:{"Death","BeforeGameOverJudge","Revived","TargetSpecified","ChoiceMade","CardEffect","DamageInflicted","PreCardUsed","HpRecover","ShowCards","CardsMoveOneTime","StartJudge","AskForRetrial","RoundStart","GameReady"}){
   PlayloopFilterStack z(L);field(L,ns,k);if(!matchAINativeConstant(r,L,-1,QStringLiteral("sgs.")+k))return false;
  }
  return matchAINativeClassMethod(r,L,"ServerPlayer","getLogName")
   &&matchAINativeClassMethod(r,L,"QVariant","toString")
   &&proveAIServerLogEffects(r);
 }
 bool prove(){if(!roots()){trace("preflight-source-roots");return false;}if(!callbackGraph()){trace("preflight-callback-graph");return false;}if(!leaves()){trace("preflight-native-leaves");return false;}return true;}
};
}
bool beginAIPlayLoopSource(Room&r,const LuaRuntime::NativeNamespaceBirth&b)noexcept{
 try{if(!b.isCurrent()||playloopFilterFind(r))return false;auto*s=new PlayloopFilterState(r,b);
  if(!s->epoch||!s->watch(s->root[PlayloopFilterGlobals],PlayloopFilterGlobals)||!s->watch(s->root[PlayloopFilterNamespace],PlayloopFilterNamespace)
   ||!lua_observetablebirth(s->L,playloopFilterBorn,s)){s->retire(__LINE__);return false;}return true;
 }catch(...){if(auto*s=playloopFilterFind(r))s->retire(__LINE__);return false;}
}
bool prepareAIPlayLoopSource(Room&r,lua_State*L)noexcept{
 auto*s=playloopFilterFind(r);if(!s||!playloopFilterCurrent(*s)||s->L!=L||s->prepareAttempted)return false;s->prepareAttempted=true;
 try{if(!registerAIPinnedSource(r,L,playloopFilterSmart,smartHash)||!registerAIPinnedSource(r,L,playloopFilterMiddle,middleHash)){s->retire(__LINE__);return false;}s->ready=true;return true;}
 catch(...){s->retire(__LINE__);return false;}
}
bool completeAIPlayLoopSource(Room&r,lua_State*L,const AIBootstrapSourceProof&p)noexcept{
 auto*s=playloopFilterFind(r);if(!s||!playloopFilterCurrent(*s)||s->L!=L||!s->ready||s->completed||!s->generationScalar
  ||p.m_consumed||p.m_room!=&r||p.m_roomLifetime.data()!=&r||p.m_lua!=L||p.m_generation!=s->generation
  ||!p.m_serial||!p.m_current||!p.m_current(r,L,p.m_serial))return false;
 for(PlayloopFilterKind k:{Events,Row,Filter,Queue})if(!s->root[k]||s->watches.value(s->root[k],PlayloopFilterGlobals)!=k)return false;
 s->completed=true;trace("bootstrap-complete");return true;
}
bool hasAIPlayLoopSource(const Room&r,lua_State*L)noexcept{
 auto*s=playloopFilterFind(r);return s&&s->L==L&&s->completed&&playloopFilterCurrent(*s)
  &&hasAICloneInputLineage(r,L)&&hasAIProjectionLineage(r,L)
  &&hasAIModeEvaluationLineage(r,L)&&hasAICardPatternsLineage(r,L)&&hasAIAsyncUniverse(r,L);
}
void retireAIPlayLoopSource(Room&r,lua_State*L)noexcept{if(auto*s=playloopFilterFind(r))if(s->L==L)s->alive=false;}
void clearAIPlayLoopSource(Room&r)noexcept{delete playloopFilterFind(r);}
bool allowAIPlayLoopWrite(Room&r,lua_State*L,const lua_TableObservation&e)noexcept{
 try{auto*s=playloopFilterFind(r);return s&&s->L==L&&playloopFilterPermitted(*s,L,e);}catch(...){if(auto*s=playloopFilterFind(r))s->retire(__LINE__);return false;}
}
bool captureAIPlayLoopCacheOrigin(const Room&r,lua_State*L,const void*&space,const void*&qlist,const void*&connect,quint64&serial)noexcept{
 space=qlist=connect=nullptr;serial=0;auto*s=playloopFilterFind(r);
 if(!hasAIPlayLoopSource(r,L)||!s->pristine||!s->resetComplete||!s->resetSerial||!s->root[PlayloopFilterQlist]||!s->root[PlayloopFilterConnect]){
  if(qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")){
   auto*rt=const_cast<Room&>(r).roomRuntime();
   std::fprintf(stderr,"R56_CACHE_RESET_ORIGIN_REFUSAL state=%d alive=%d completed=%d current=%d pristine=%d reset_complete=%d serial=%llu qlist=%d connect=%d saved_epoch=%llu actual_epoch=%llu clone=%d projection=%d mode=%d patterns=%d async=%d\n",
    int(s!=nullptr),int(s&&s->alive),int(s&&s->completed),int(s&&playloopFilterCurrent(*s)),int(s&&s->pristine),int(s&&s->resetComplete),s?static_cast<unsigned long long>(s->resetSerial):0ULL,int(s&&s->root[PlayloopFilterQlist]),int(s&&s->root[PlayloopFilterConnect]),s?static_cast<unsigned long long>(s->epoch):0ULL,rt?static_cast<unsigned long long>(rt->prioritySourceEpoch()):0ULL,
    int(hasAICloneInputLineage(r,L)),int(hasAIProjectionLineage(r,L)),int(hasAIModeEvaluationLineage(r,L)),int(hasAICardPatternsLineage(r,L)),int(hasAIAsyncUniverse(r,L)));std::fflush(stderr);
  }
  return false;
 }
 space=s->root[PlayloopFilterNamespace];qlist=s->root[PlayloopFilterQlist];connect=s->root[PlayloopFilterConnect];serial=s->resetSerial;return true;
}
AIPlayLoopFilterScope::AIPlayLoopFilterScope(Room&r,LuaAI*ai,ServerPlayer*p,int event,const QVariant&data,lua_State*L)noexcept{
 // Other genuine events retain all original behavior and default retirement.
 if(event!=int(EventPlayPhaseLoop))return;
 try{
  d.reset(new Data(r,ai,p,L,data));d->previous=playloopFilterActive;playloopFilterActive=this;
  const auto fail=qScopeGuard([&]{if(!accepted())retireAIProjectionSource(r,L);});
  auto*rt=r.roomRuntime();
  if(!rt||!L||!p||p->getRoom()!=&r||typeid(*p)!=typeid(ServerPlayer)){trace("preflight-native-domain");return;}
  if(!matchesAINaturalFilterPreEntry(r,ai,p,event,&data,L)){trace("preflight-dispatch-token");return;}
  if(!hasAIPlayLoopSource(r,L)){trace("preflight-source-lineage");return;}
  // Graph retains at most 40 slots; the largest nested native seal needs
  // 1024 additional slots (64-level native resolution is below that bound).
  // Use existing capacity only; no stack allocation before source proof.
  if(!capacity(L,2048)){trace("preflight-stack");return;}
  d->s=playloopFilterFind(r);PlayloopFilterStack z(L);Graph g(*d->s,ai,p);if(!g.prove())return;
  d->callback=g.callback;d->self=g.selfIdentity;d->frame=L->ci;d->generation=rt->lua().generation();
  d->epoch=rt->prioritySourceEpoch();d->transport=rt->xufaTransportEpoch();d->depth=rt->lua().protectedCallDepth();
  // Complete finite source/effect/allocation union is proved before the first
  // callback/player/owned-QVariant wrapper can allocate or run finalizers.
  d->async.reset(new AIAsyncUniverseScope(r,L,AIAsyncUniverseScope::GameplayVariants));
  d->ready=d->callback&&d->self&&d->epoch&&d->transport&&d->async->accepted();trace("preflight-complete",int(d->ready));
 }catch(...){if(d)d->invalid=true;retireAIProjectionSource(r,L);}
}
AIPlayLoopFilterScope::~AIPlayLoopFilterScope(){
 if(!d)return;
 trace("scope-exit",int(accepted()),int(d->writes));
 if(!d->entered||d->writes!=allWrites||!accepted())retireAIProjectionSource(d->r,d->L);
 playloopFilterActive=d->previous;
}
bool AIPlayLoopFilterScope::accepted()const noexcept{
 if(!d||!d->ready||d->invalid)return false;auto*rt=d->r.roomRuntime();
 return rt&&!rt->isClosing()&&rt->lua().rawState()==d->L&&rt->lua().generation()==d->generation
  &&rt->prioritySourceEpoch()==d->epoch&&rt->xufaTransportEpoch()==d->transport
  &&hasAIPlayLoopSource(d->r,d->L);
}
bool acceptAIPlayLoopFilterEntry(Room&r,lua_State*L,int n)noexcept{
 auto*c=AIPlayLoopAccess::call(r,L,false);if(!c)return false;
 try{
  AINaturalFilterProof proof;auto*rt=r.roomRuntime();const int f=lua_gettop(L)-n;
  if(!c->entered&&n==4&&f>=1&&L->ci==c->frame&&rt->lua().protectedCallDepth()==c->depth
   &&lua_isfunction(L,f)&&lua_topointer(L,f)==c->callback
   &&readAINaturalFilterEntry(r,L,n,&proof)&&proof.event==int(EventPlayPhaseLoop)
   &&matchesAINativeFilterDispatch(r,L,n,int(EventPlayPhaseLoop),c->p,c->original)
   &&matchAINativePlayer(L,f+3,c->p)
   &&matchAINativeObjectMethods(r,L,f+4,"QVariant",{"toString"})
   &&aiFocusContinuityNoop(r,L,n)){
   c->entered=true;trace("entry-accepted",int(EventPlayPhaseLoop));return true;
  }
 }catch(...){}
 trace("entry-refused");c->invalid=true;retireAIProjectionSource(r,L);return false;
}
