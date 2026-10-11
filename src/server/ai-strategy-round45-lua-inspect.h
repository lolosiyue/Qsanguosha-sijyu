#ifndef AI_STRATEGY_ROUND45_LUA_INSPECT_H
#define AI_STRATEGY_ROUND45_LUA_INSPECT_H
#include "lua.hpp"
extern "C" {
#include "lstate.h"
#include "ltable.h"
#include "lgc.h"
}
#include <cstring>
// Inspection only. No Lua allocation, metamethod/function invocation or
// semantic-read notification: this is native certificate metadata inspection,
// not an execution of a gameplay/strategy read. The caller reserves bounded
// stack space with lua_checkstack before using these helpers. That API grows
// only the stack; it does not run GC or Lua code.
namespace AIRound45Inspect {
inline bool capacity(lua_State *L,int extra){
 return L && extra>=0 && L->stack_last.p-L->top.p>=extra && L->ci->top.p-L->top.p>=extra;
}

inline const TValue *value(lua_State *L,int index){
 if(index==LUA_REGISTRYINDEX)return &G(L)->l_registry;
 if(index>0){auto *v=L->ci->func.p+index;return v<L->top.p?s2v(v):&G(L)->nilvalue;}
 if(index>LUA_REGISTRYINDEX&&index<0){auto *v=L->top.p+index;return v>L->ci->func.p?s2v(v):&G(L)->nilvalue;}
 return &G(L)->nilvalue; // no inspection of arbitrary C-frame pseudo-upvalues
}
inline Table *table(lua_State *L,int index){const auto *v=value(L,index);return ttistable(v)?hvalue(v):nullptr;}
inline void push(lua_State *L,const TValue *v){
 if(!v||isempty(v))setnilvalue(s2v(L->top.p));else setobj2s(L,L->top.p,v);
 ++L->top.p;
}
inline void field(lua_State *L,int index,const char *key){
 Table *t=table(L,index);const size_t len=std::strlen(key);
 if(t)for(int i=0;i<allocsizenode(t);++i){const Node *n=gnode(t,i);
  if(!isempty(gval(n))&&keyiscollectable(n)&&keytt(n)==ctb(LUA_VSHRSTR)){
   const TString *s=gco2ts(keyval(n).gc);
   if(tsslen(s)==len&&!std::memcmp(getstr(s),key,len)){push(L,gval(n));return;}
  }
  else if(!isempty(gval(n))&&keyiscollectable(n)&&keytt(n)==ctb(LUA_VLNGSTR)){
   const TString *s=gco2ts(keyval(n).gc);
   if(tsslen(s)==len&&!std::memcmp(getstr(s),key,len)){push(L,gval(n));return;}
  }
 }
 push(L,nullptr);
}
inline int rawGet(lua_State *L,int index){
 Table *t=table(L,index);const auto *v=t?luaH_get(t,s2v(L->top.p-1)):nullptr;
 --L->top.p;push(L,v);return lua_type(L,-1);
}
inline int rawGeti(lua_State *L,int index,lua_Integer key){
 Table *t=table(L,index);push(L,t?luaH_getint(t,key):nullptr);return lua_type(L,-1);
}
inline int rawGetp(lua_State *L,int index,const void *key){
 Table *t=table(L,index);TValue k;setpvalue(&k,const_cast<void *>(key));
 push(L,t?luaH_get(t,&k):nullptr);return lua_type(L,-1);
}
inline int next(lua_State *L,int index){
 Table *t=table(L,index);if(!t){--L->top.p;return 0;}
 // Only keys supplied by this same traversal are passed here; no authored
// arbitrary key can trigger luaH_next's invalid-key ordinary error.
 const int more=luaH_next(L,t,L->top.p-1);
 if(more)++L->top.p;else --L->top.p;return more;
}
inline void globals(lua_State *L){rawGeti(L,LUA_REGISTRYINDEX,LUA_RIDX_GLOBALS);}
inline bool primitiveMetatable(lua_State *L,int type){
 if(!G(L)->mt[type])return false;
 sethvalue2s(L,L->top.p,G(L)->mt[type]);++L->top.p;return true;
}
}
#endif
