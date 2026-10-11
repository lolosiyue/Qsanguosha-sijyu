#include "ai-strategy-round56-bootstrap-write.h"
#include "ai-strategy-round45-code-seal.h"
#include "ai-strategy-round45-native-seal.h"
extern "C" {
#include "lstate.h"
#include "lopcodes.h"
}
#include <cstring>
#include <new>

bool matchAIRound56BootstrapWrite(Room&r,lua_State*L,const lua_TableObservation&e,
 const QString&path,int line,const char*key,bool genesis,int first,int last)noexcept{
 try{
  if(!L||!key||e.event!=LUA_TABLE_OBSERVER_WRITE||!e.string_key
   ||e.string_key_length!=std::strlen(key)||std::memcmp(e.string_key,key,e.string_key_length))return false;
  CallInfo*ci=L->ci;
  if(genesis&&(e.value_type!=LUA_TTABLE||!e.value_table_identity))return false;
  const bool bridge=genesis&&!isLua(ci);
  if(bridge){
   if(!matchAIRound56NativeNamespaceWriteFrame(r,L,e.table_identity,QStringLiteral("sgs"),key))return false;
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
  if(!matchAIRound56PinnedInstruction(r,L,cl,pc,path,first,last,line))return false;
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
