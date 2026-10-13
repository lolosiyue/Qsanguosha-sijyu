#include "ai-strategy-round56-projection-lineage.h"
#include "ai-strategy-room-ledger.h"
#include <cstdio>
#include "ai-strategy-round56-async.h"
#include "ai-strategy-round47-xufa-proof.h"
#include "ai-strategy-round45-code-seal.h"
#include "ai-strategy-round53-ainoto.h"
#include "room.h"
#include "room-runtime.h"
#include <QSet>
#include "lua.hpp"
#include "ai-strategy-round45-lua-inspect.h"
#include "lopcodes.h"
namespace {
struct State;void changed(lua_State*,const lua_TableObservation*,void*) noexcept;
void born(lua_State*,const lua_TableBirthObservation*,void*) noexcept;
struct State final:AIRoomLedger {
 Room&r;lua_State*L;quint64 generation;bool alive=true,completed=false,sourceReady=false,prepareAttempted=false;QSet<const void*>tables;
 State(Room&room,lua_State*l):AIRoomLedger(room),r(room),L(l),generation(room.roomRuntime()->lua().generation()){}
 ~State()override{lua_unobservetablebirth(L,born,this);for(const void*p:tables)lua_unobservetableidentity(L,p,changed,this);}
 bool watch(const void*p){if(tables.contains(p))return true;if(!lua_observetableidentity(L,p,changed,this))return false;try{tables.insert(p);}catch(...){lua_unobservetableidentity(L,p,changed,this);throw;}return true;}
 void retire()noexcept{alive=false;retireAIRound56AsyncUniverse(L);retireAIRound53AiNoTo(r);r.roomRuntime()->revokePrioritySource();}
};
State*find(const Room&r){for(auto*c:r.children())if(auto*s=dynamic_cast<State*>(c))return s;return nullptr;}
void changed(lua_State*,const lua_TableObservation*event,void*opaque)noexcept{
 auto&s=*static_cast<State*>(opaque);const auto&e=*event;
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
bool captureProducer(State&s,lua_State*L,const lua_TableBirthObservation&e){
 if(!s.sourceReady||!e.lua_function_identity)return false;
 const auto*f=static_cast<const LClosure*>(e.lua_function_identity);
 const int pc=e.lua_instruction_index;
 if(!f->p||f->p->linedefined!=0||f->p->lastlinedefined!=0
   ||pc<0||pc>=f->p->sizecode||GET_OPCODE(f->p->code[pc])!=OP_NEWTABLE)return false;
 // Trusted line maps come from independently compiled pinned bytes, never
 // mutable debug metadata. The borrowed closure is used only in this event.
 if(matchAIRound56PinnedInstruction(s.r,L,f,pc,captureSources[0].path,0,0,169))return true;
 for(int line:{5203,5204,5205,5206,5207})
  if(matchAIRound56PinnedInstruction(s.r,L,f,pc,captureSources[1].path,0,0,line))return true;
 return false;
}
void born(lua_State*L,const lua_TableBirthObservation*e,void*opaque) noexcept {
 auto&s=*static_cast<State*>(opaque);if(!s.alive||s.completed)return;
 try{if(captureProducer(s,L,*e)&&!s.watch(e->table_identity))s.retire();}
 catch(...){s.retire();}
}
}
bool beginAIRound56ProjectionLineage(Room&r,const LuaRuntime::NativeNamespaceBirth&birth) noexcept {
 try{auto*rt=r.roomRuntime();if(!birth.isCurrent()||!rt||rt->lua().rawState()!=birth.state()||find(r))return false;
  auto*s=new State(r,birth.state());if(!lua_observetablebirth(s->L,born,s)){s->retire();return false;}return true;
 }catch(...){if(auto*s=find(r))s->retire();return false;}
}
bool prepareAIRound56ProjectionSources(Room&r,lua_State*L)noexcept{
 auto*s=find(r);if(!s||!s->alive||s->L!=L||s->prepareAttempted)return false;
 s->prepareAttempted=true;
 try{
  // Sole caller is the original post-Binding, pre-config/rules boundary.
  for(const auto&source:captureSources)
   if(!registerAIRound45PinnedSource(r,L,source.path,source.sha)){s->retire();return false;}
  s->sourceReady=true;return true;
 }catch(...){s->retire();return false;}
}
bool isAIRound56ProjectionCaptureOrigin(const Room&r,lua_State*L,const void*p)noexcept{
 auto*s=find(r);auto*rt=const_cast<Room&>(r).roomRuntime();
 return p&&s&&s->alive&&s->sourceReady&&s->L==L&&rt&&!rt->isClosing()
  &&rt->lua().rawState()==L&&rt->lua().generation()==s->generation&&s->tables.contains(p);
}
bool completeAIRound56ProjectionLineage(Room&r,lua_State*L,const AIRound56BootstrapSourceProof&proof)noexcept{
 try{
  auto*rt=r.roomRuntime();auto*s=find(r);if(!rt||!L||!s||!s->alive||!s->sourceReady||s->completed||s->L!=L||proof.m_consumed||proof.m_roomLifetime.data()!=&r
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
  if(!collectAIRound56CorrectionCaptureTables(r,L,captures,&captureReason)) {
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
bool hasAIRound56ProjectionLineage(const Room&r,lua_State*L)noexcept{
 auto*s=find(r);auto*rt=const_cast<Room&>(r).roomRuntime();return s&&s->alive&&s->completed&&s->L==L&&rt
  &&!rt->isClosing()&&rt->lua().rawState()==L&&rt->lua().generation()==s->generation;
}
void retireAIRound56ProjectionLineage(Room&r,lua_State*L)noexcept{if(auto*s=find(r))if(s->L==L)s->alive=false;}
void clearAIRound56ProjectionLineage(Room&r)noexcept{if(auto*s=find(r))delete s;}
