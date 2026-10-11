/* Native inspection only; never part of the Lua API. */
#ifndef LQSGSASYNC_H
#define LQSGSASYNC_H
#include "lstate.h"
#include "ltable.h"
static const TValue *qsgs_async_value(lua_State *L,int index) {
 if(index>0)return s2v(L->ci->func.p+index);
 return s2v(L->top.p+index);
}
static const TValue *qsgs_async_field(Table*t,const char*key) {
 size_t len=strlen(key);int i;
 for(i=0;i<allocsizenode(t);++i){const Node*n=gnode(t,i);
  if(!isempty(gval(n))&&keyiscollectable(n)
     &&(keytt(n)==ctb(LUA_VSHRSTR)||keytt(n)==ctb(LUA_VLNGSTR))){
   const TString*s=gco2ts(keyval(n).gc);
   if(tsslen(s)==len&&!memcmp(getstr(s),key,len))return gval(n);
  }
 }return NULL;
}
static int qsgs_async_cfunction(const TValue*v,lua_CFunction f) {
 return ttislcf(v)&&fvalue(v)==f; /* zero captures; no arbitrary C closure */
}
#endif
