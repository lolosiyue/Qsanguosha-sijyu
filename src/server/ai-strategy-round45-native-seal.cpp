#include "ai-strategy-round53-lianzhan.h"
#include "ai-strategy-round56-bootstrap-source.h"
#include "ai-strategy-round45-native-seal.h"
#include "room.h"
#include "room-runtime.h"
#include "lua-runtime.h"
#include <QObject>
#include <QHash>
#include <QList>
#include <QSet>
#include <QByteArray>
#include <cstring>
#include "ai-strategy-round45-lua-inspect.h"
// Implemented inside the generated SWIG compilation unit. It checks native
// userdata size/type identity, never invokes conversion or Lua callbacks.
extern const char *aiRound45NativeUserdataClass(lua_State *,int);
extern bool aiRound47NativeMetamethodProxy(lua_State *,int,const QString &,const QString &,QStringList &);
namespace {
using namespace AIRound45Inspect;
constexpr const char *ledgerName="AI.R45.NativeBindingSeal";
struct Stack {lua_State *L;int top;explicit Stack(lua_State *s):L(s),top(lua_gettop(s)){}~Stack(){lua_settop(L,top);}};
bool plain(lua_State *L,int i){auto *t=table(L,i);return t&&!t->metatable;}
lua_CFunction function(lua_State *L,int i){
 const TValue *v=value(L,i);if(ttislcf(v))return fvalue(v);
 if(ttisCclosure(v)&&clCvalue(v)->nupvalues==0)return clCvalue(v)->f;
 return nullptr;
}
struct Method {lua_CFunction fn=nullptr;bool nonempty=false;};
QHash<QString,Method> methods(lua_State *L,int i){
 QHash<QString,Method> result;if(!plain(L,i))return result;i=lua_absindex(L,i);
 lua_pushnil(L);while(next(L,i)){
  if(lua_type(L,-2)==LUA_TSTRING)result.insert(QString::fromUtf8(lua_tostring(L,-2)),{function(L,-1),!lua_isnil(L,-1)});
  lua_pop(L,1);
 }return result;
}
struct ValueMeta {Method direct;const void*closure=nullptr;QStringList search;bool proxy=false;};
struct Class {const void *identity=nullptr;lua_CFunction index=nullptr,newindex=nullptr;QHash<QString,Method> getters,setters,functions;QHash<QString,ValueMeta> valueMetas;QList<QString>bases;bool valid=false;};
struct Space {const void *identity=nullptr,*meta=nullptr;lua_CFunction index=nullptr,newindex=nullptr;QHash<QString,Method> getters,setters,functions;bool valid=false;};
struct Constructor {const void*table=nullptr;const void*meta=nullptr;lua_CFunction call=nullptr;};
struct Scalar {int type=LUA_TNONE;bool integer=false;lua_Integer i=0;lua_Number n=0;bool b=false;QByteArray s;};
class Seal;
void finalizerTransitionWrite(lua_State*,const lua_TableObservation*,void*)noexcept;
class Seal final:public QObject {
public:
 explicit Seal(Room &r):QObject(&r),room(r){setObjectName(ledgerName);}
 ~Seal()override{if(finalizerMeta&&L)lua_unobservetableidentity(L,finalizerMeta,finalizerTransitionWrite,this);}
 Room&room;
 enum FinalizerPhase{Unprepared,Prepared,Observed,Finished,Retired};
 FinalizerPhase finalizerPhase=Unprepared;const void*finalizerMeta=nullptr;
 CallInfo*finalizerFrame=nullptr;lua_CFunction finalizerOld=nullptr,finalizerWritten=nullptr;
 lua_State *L=nullptr;quint64 generation=0;
 QHash<QString,lua_CFunction> functions;
 QHash<QString,Constructor> constructors;
 QHash<QString,Scalar> constants;
 QHash<QString,Class> classes;QHash<QString,Space> spaces;
 const void *fileMeta=nullptr,*fileMethods=nullptr;QHash<QString,Method> fileLeaves;
 const void *stringMeta=nullptr,*stringLibrary=nullptr;
 QHash<QString,Method> stringMetas;bool stringMetaValid=false;
};
void finalizerTransitionWrite(lua_State*L,const lua_TableObservation*e,void*u)noexcept{
 auto&s=*static_cast<Seal*>(u);if(!e||!L||!s.L||G(L)!=G(s.L)||e->table_identity!=s.finalizerMeta)return;
 if(e->event==LUA_TABLE_OBSERVER_FREE){s.finalizerMeta=nullptr;s.finalizerPhase=Seal::Retired;retireAIRound56BootstrapSourceBoundary(s.room);return;}
 if(e->event==LUA_TABLE_OBSERVER_READ||e->event==LUA_TABLE_OBSERVER_ENUMERATE)return;
 const bool originalWrite=e->event==LUA_TABLE_OBSERVER_WRITE&&e->flags==0
  &&e->value_type==LUA_TFUNCTION&&e->string_key&&e->string_key_length==4&&!std::memcmp(e->string_key,"__gc",4)
  &&s.finalizerPhase==Seal::Prepared&&L==s.L&&L->ci==s.finalizerFrame
  &&inAIRound56LianzhanBootstrapInstaller(s.room,L);
 s.finalizerPhase=originalWrite?Seal::Observed:Seal::Retired;
 if(!originalWrite)retireAIRound56BootstrapSourceBoundary(s.room);
}
Seal *get(Room &r,bool create=false){for(QObject *o:r.children())if(o->objectName()==ledgerName)return static_cast<Seal *>(o);return create?new Seal(r):nullptr;}
Seal *current(Room &r,lua_State *L){auto *s=get(r);auto *runtime=r.roomRuntime();
 return s&&runtime&&L&&s->L==L&&runtime->lua().state()==L&&runtime->lua().generation()==s->generation?s:nullptr;}
void saveScalar(Seal &s,lua_State *L,int i,const QString &path){
 Scalar v;v.type=lua_type(L,i);
 if(v.type==LUA_TNUMBER){v.integer=lua_isinteger(L,i);if(v.integer)v.i=lua_tointeger(L,i);else v.n=lua_tonumber(L,i);}
 else if(v.type==LUA_TBOOLEAN)v.b=lua_toboolean(L,i);
 else if(v.type==LUA_TSTRING){size_t n=0;const char *p=lua_tolstring(L,i,&n);v.s=QByteArray(p,qsizetype(n));}
 else if(v.type!=LUA_TNIL)return;
 s.constants.insert(path,v);
}
void captureTable(Seal &s,lua_State *L,int t,const QString &prefix){
 if(!table(L,t))return;t=lua_absindex(L,t);lua_pushnil(L);
 while(next(L,t)){if(lua_type(L,-2)==LUA_TSTRING){
  const QString path=prefix+QString::fromUtf8(lua_tostring(L,-2));
  if(auto f=function(L,-1))s.functions.insert(path,f);else saveScalar(s,L,-1,path);
 }lua_pop(L,1);}
}
Class captureClass(lua_State *L,int t,const QString &name){
 Class c;Stack stack(L);t=lua_absindex(L,t);if(!plain(L,t))return c;
 c.identity=lua_topointer(L,t);
 for(const char*k:{"__eq","__lt","__le","__len","__tostring","__add","__sub","__mul","__div","__idiv","__mod","__pow","__unm","__concat","__band","__bor","__bxor","__bnot","__shl","__shr","__gc","__close"}){
  field(L,t,k);ValueMeta v;v.direct={function(L,-1),!lua_isnil(L,-1)};
  if(v.direct.nonempty&&!v.direct.fn){
   v.proxy=aiRound47NativeMetamethodProxy(L,-1,name,QString::fromLatin1(k),v.search);
   if(v.proxy)v.closure=lua_topointer(L,-1);
  }c.valueMetas.insert(QString::fromLatin1(k),v);lua_pop(L,1);
 }
 field(L,t,"__index");c.index=function(L,-1);lua_pop(L,1);
 field(L,t,"__newindex");c.newindex=function(L,-1);lua_pop(L,1);
 field(L,t,".set");if(plain(L,-1))c.setters=methods(L,-1);lua_pop(L,1);
 field(L,t,".get");if(!plain(L,-1))return c;c.getters=methods(L,-1);lua_pop(L,1);
 field(L,t,".fn");if(!plain(L,-1))return c;c.functions=methods(L,-1);lua_pop(L,1);
 field(L,t,".bases");if(!plain(L,-1))return c;const int bases=lua_absindex(L,-1);
 const auto n=lua_rawlen(L,bases);if(n>64)return c;
 for(lua_Unsigned j=1;j<=n;++j){rawGeti(L,bases,lua_Integer(j));if(!plain(L,-1))return c;field(L,-1,".type");
  if(lua_type(L,-1)!=LUA_TSTRING)return c;c.bases.append(QString::fromUtf8(lua_tostring(L,-1)));lua_pop(L,2);}
 c.valid=c.index!=nullptr;return c;
}
bool sameMethod(lua_State *L,int methodsIndex,const QHash<QString,Method> &old,const QString &key){
 if(!plain(L,methodsIndex))return false;Stack stack(L);field(L,methodsIndex,key.toUtf8().constData());
 const Method expected=old.value(key);
 return expected.nonempty?(expected.fn&&function(L,-1)==expected.fn):lua_isnil(L,-1);
}
// -1 invalid, 0 absent, 1 exact native function. Preserve SWIG first-base order
// and reject getter shadows even when an unrelated later base also has a leaf.
int resolve(const Seal &seal,lua_State *L,const QString &name,const QString &key,QSet<QString> &seen,bool property=false){
 if(seen.contains(name)||seen.size()>64)return -1;seen.insert(name);
 auto it=seal.classes.constFind(name);if(it==seal.classes.cend()||!it->valid)return -1;
 Stack stack(L);field(L,LUA_REGISTRYINDEX,"SWIG");if(!plain(L,-1))return -1;field(L,-1,name.toUtf8().constData());
 const int meta=lua_absindex(L,-1);if(!plain(L,meta)||lua_topointer(L,meta)!=it->identity)return -1;
 field(L,meta,"__index");if(function(L,-1)!=it->index)return -1;lua_pop(L,1);
 field(L,meta,".get");if(!sameMethod(L,-1,it->getters,key))return -1;lua_pop(L,1);
 if(it->getters.value(key).nonempty)return property?1:-1;
 field(L,meta,".fn");if(!sameMethod(L,-1,it->functions,key))return -1;lua_pop(L,1);
 if(it->functions.value(key).nonempty)return property?-1:1;
 field(L,meta,".bases");if(!plain(L,-1)||lua_rawlen(L,-1)!=lua_Unsigned(it->bases.size()))return -1;
 const int bases=lua_absindex(L,-1);for(int i=0;i<it->bases.size();++i){rawGeti(L,bases,i+1);
  if(!plain(L,-1))return -1;const auto b=seal.classes.constFind(it->bases[i]);
  if(b==seal.classes.cend()||lua_topointer(L,-1)!=b->identity)return -1;lua_pop(L,1);
 }
 for(const auto &base:it->bases){QSet<QString> branch=seen;const int result=resolve(seal,L,base,key,branch,property);if(result!=0)return result;}
 return 0;
}
}
void captureAIRound45NativeBindings(Room &r,lua_State *L){
 if(get(r)||!L||!lua_checkstack(L,512))return;auto *runtime=r.roomRuntime();
 if(!runtime||runtime->lua().state()!=L)return;
 auto *s=get(r,true);s->L=L;s->generation=runtime->lua().generation();Stack stack(L);
 globals(L);const int g=lua_absindex(L,-1);captureTable(*s,L,g,QString());
 {
  Stack strings(L);field(L,g,"string");s->stringLibrary=lua_topointer(L,-1);
  if(plain(L,-1)&&primitiveMetatable(L,LUA_TSTRING)&&plain(L,-1)){
   s->stringMeta=lua_topointer(L,-1);const int meta=lua_absindex(L,-1);bool valid=true;int indexCount=0;
   lua_pushnil(L);while(next(L,meta)){
    if(lua_type(L,-2)!=LUA_TSTRING)valid=false;
    else {const QString key=QString::fromUtf8(lua_tostring(L,-2));
     if(key=="__index"){++indexCount;if(lua_topointer(L,-1)!=s->stringLibrary)valid=false;}
     else {const auto fn=function(L,-1);if(!fn)valid=false;s->stringMetas.insert(key,{fn,true});}
    }lua_pop(L,1);
   }s->stringMetaValid=valid&&indexCount==1;
  }
 }
 for(const char *lib:{"math","string","table","io","bit32","debug","os"}){field(L,g,lib);captureTable(*s,L,-1,QString::fromLatin1(lib)+'.');lua_pop(L,1);}
 field(L,g,"sgs");const int ns=lua_absindex(L,-1);
 if(table(L,ns)){
  captureTable(*s,L,ns,QStringLiteral("sgs."));
  // R47 finite scratch-only factories. Capture before any configured Lua;
  // source callers separately prove effects and arguments of each use.
  for(const char*name:{"QVariant","IntList","PlayerList","SPlayerList","LuaAI"}){
   field(L,ns,name);Constructor c;c.table=lua_topointer(L,-1);
   if(table(L,-1)&&lua_getmetatable(L,-1)){
    c.meta=lua_topointer(L,-1);field(L,-1,"__call");c.call=function(L,-1);lua_pop(L,2);
   }if(c.call)s->constructors.insert(QString::fromLatin1(name),c);lua_pop(L,1);
  }
  Space space;space.identity=lua_topointer(L,ns);
  if(lua_getmetatable(L,ns)){const int meta=lua_absindex(L,-1);space.meta=lua_topointer(L,meta);
   field(L,meta,"__index");space.index=function(L,-1);lua_pop(L,1);field(L,meta,".get");
   if(plain(L,-1)){space.getters=methods(L,-1);space.valid=space.index!=nullptr;}lua_pop(L,1);
   field(L,meta,"__newindex");space.newindex=function(L,-1);lua_pop(L,1);
   field(L,meta,".set");if(plain(L,-1))space.setters=methods(L,-1);else space.valid=false;lua_pop(L,1);
   field(L,meta,".fn");if(plain(L,-1))space.functions=methods(L,-1);else space.valid=false;lua_pop(L,2);
  }s->spaces.insert(QStringLiteral("sgs"),space);
 }
 field(L,LUA_REGISTRYINDEX,LUA_FILEHANDLE);
 if(plain(L,-1)){s->fileMeta=lua_topointer(L,-1);field(L,-1,"__index");
  if(plain(L,-1)){s->fileMethods=lua_topointer(L,-1);s->fileLeaves=methods(L,-1);}lua_pop(L,1);
 }lua_pop(L,1);
 field(L,LUA_REGISTRYINDEX,"SWIG");if(!plain(L,-1))return;const int registry=lua_absindex(L,-1);
 lua_pushnil(L);while(next(L,registry)){
  if(lua_type(L,-2)==LUA_TSTRING&&plain(L,-1))s->classes.insert(QString::fromUtf8(lua_tostring(L,-2)),captureClass(L,-1,QString::fromUtf8(lua_tostring(L,-2))));
  lua_pop(L,1);
 }
}
void clearAIRound45NativeBindings(Room &r){delete get(r);}
bool beginAIRound56LianzhanFinalizerTransition(Room&r,lua_State*L){
 auto*s=current(r,L);if(!s||s->finalizerPhase!=Seal::Unprepared||!inAIRound56LianzhanBootstrapInstaller(r,L))return false;
 s->finalizerPhase=Seal::Retired; // even a refused first attempt cannot retry
 auto cls=s->classes.constFind("CardUseStruct");if(cls==s->classes.cend()||!cls->valid)return false;
 auto member=cls->valueMetas.constFind("__gc");
 if(member==cls->valueMetas.cend()||member->proxy||!member->direct.nonempty||!member->direct.fn)return false;
 Stack stack(L);if(!capacity(L,32))return false;field(L,LUA_REGISTRYINDEX,"SWIG");if(!plain(L,-1))return false;
 field(L,-1,"CardUseStruct");if(!plain(L,-1)||lua_topointer(L,-1)!=cls->identity)return false;
 const int meta=lua_absindex(L,-1);field(L,meta,"__gc");if(function(L,-1)!=member->direct.fn)return false;
 if(!lua_observetableidentity(L,cls->identity,finalizerTransitionWrite,s))return false;
 s->finalizerMeta=cls->identity;s->finalizerOld=member->direct.fn;s->finalizerFrame=L->ci;s->finalizerPhase=Seal::Prepared;return true;
}
bool completeAIRound56LianzhanFinalizerTransition(Room&r,lua_State*L){
 auto*s=current(r,L);if(!s||s->finalizerPhase!=Seal::Observed||s->finalizerFrame!=L->ci||!inAIRound56LianzhanBootstrapInstaller(r,L))return false;
 s->finalizerPhase=Seal::Retired;
 Stack stack(L);if(!capacity(L,32))return false;field(L,LUA_REGISTRYINDEX,"SWIG");if(!plain(L,-1))return false;
 field(L,-1,"CardUseStruct");if(!plain(L,-1)||lua_topointer(L,-1)!=s->finalizerMeta)return false;
 field(L,-1,"__gc");const auto written=function(L,-1);if(!written||written==s->finalizerOld)return false;
 // This is the result of the just-observed deterministic native assignment,
 // not a late sample: original old identity forced the installer write arm.
 s->finalizerWritten=written;s->finalizerFrame=nullptr;s->finalizerPhase=Seal::Finished;return true;
}

bool matchAIRound45NativeBinding(Room &r,lua_State *L,int index,const QString &path){
 auto *s=current(r,L);return s&&s->functions.contains(path)&&function(L,index)==s->functions.value(path);
}
bool matchAIRound45NativeConstant(Room &r,lua_State *L,int index,const QString &path){
 auto *s=current(r,L);if(!s||!s->constants.contains(path))return false;
 const Scalar &v=s->constants[path];if(lua_type(L,index)!=v.type)return false;
 if(v.type==LUA_TNIL)return true;
 if(v.type==LUA_TBOOLEAN)return bool(lua_toboolean(L,index))==v.b;
 if(v.type==LUA_TNUMBER){if(bool(lua_isinteger(L,index))!=v.integer)return false;
  if(v.integer)return lua_tointeger(L,index)==v.i;const lua_Number n=lua_tonumber(L,index);return !std::memcmp(&n,&v.n,sizeof n);}
 if(v.type==LUA_TSTRING){size_t n=0;const char *p=lua_tolstring(L,index,&n);return n==size_t(v.s.size())&&!std::memcmp(p,v.s.constData(),n);}
 return false;
}
bool matchAIRound45NativeClassMethod(Room &r,lua_State *L,const QString &name,const QString &key){
 auto *s=current(r,L);if(!s||!AIRound45Inspect::capacity(L,512))return false;QSet<QString> seen;return resolve(*s,L,name,key,seen)==1;
}
bool matchAIRound45NativeDerivedMethods(Room &r,lua_State *L,const QString &base,const QStringList &keys){
 auto *s=current(r,L);if(!s||!AIRound45Inspect::capacity(L,1024)||!s->classes.contains(base)||keys.isEmpty())return false;
 const auto derives=[&](auto&&walk,const QString&name,QSet<QString>seen)->bool{
  if(name==base)return true;if(seen.contains(name)||seen.size()>64)return false;seen.insert(name);
  auto it=s->classes.constFind(name);if(it==s->classes.cend())return false;
  for(const auto&parent:it->bases)if(walk(walk,parent,seen))return true;return false;
 };
 bool any=false;
 for(auto it=s->classes.cbegin();it!=s->classes.cend();++it)if(derives(derives,it.key(),{})){
  any=true;for(const auto&key:keys){QSet<QString>seen;if(resolve(*s,L,it.key(),key,seen)!=1)return false;}
 }
 return any;
}
bool matchAIRound45NativeNamespaceProperty(Room &r,lua_State *L,const QString &name,const QString &key){
 auto *s=current(r,L);if(!s||!AIRound45Inspect::capacity(L,32))return false;auto it=s->spaces.constFind(name);
 if(it==s->spaces.cend()||!it->valid||!it->getters.value(key).fn)return false;
 Stack stack(L);globals(L);field(L,-1,name.toUtf8().constData());const int ns=lua_absindex(L,-1);
 if(lua_topointer(L,ns)!=it->identity)return false;field(L,ns,key.toUtf8().constData());if(!lua_isnil(L,-1))return false;
 if(!lua_getmetatable(L,ns)||lua_topointer(L,-1)!=it->meta)return false;const int meta=lua_absindex(L,-1);
 field(L,meta,"__index");if(function(L,-1)!=it->index)return false;field(L,meta,".get");
 return sameMethod(L,-1,it->getters,key);
}

bool matchAIRound45NativeClassProperty(Room &r,lua_State *L,const QString &name,const QString &key){
 auto *s=current(r,L);if(!s||!AIRound45Inspect::capacity(L,512))return false;QSet<QString>seen;
 return resolve(*s,L,name,key,seen,true)==1;
}
bool matchAIRound45NativeObjectMethods(Room &r,lua_State *L,int index,const QString &base,const QStringList &keys){
 auto *s=current(r,L);if(!s||!AIRound45Inspect::capacity(L,1024)||keys.isEmpty())return false;
 const char *nativeName=aiRound45NativeUserdataClass(L,index);if(!nativeName)return false;
 const QString name=QString::fromUtf8(nativeName);auto it=s->classes.constFind(name);
 if(it==s->classes.cend()||!it->valid)return false;Stack stack(L);
 if(!lua_getmetatable(L,index)||lua_topointer(L,-1)!=it->identity)return false;
 const auto derives=[&](auto&&walk,const QString&n,QSet<QString>seen)->bool{
  if(n==base)return true;if(seen.contains(n)||seen.size()>64)return false;seen.insert(n);
  auto c=s->classes.constFind(n);if(c==s->classes.cend())return false;
  for(const auto&parent:c->bases)if(walk(walk,parent,seen))return true;return false;
 };
 if(!derives(derives,name,{}))return false;
 for(const auto&key:keys){QSet<QString>seen;if(resolve(*s,L,name,key,seen)!=1)return false;}
 return true;
}
bool matchAIRound45NativeNamespaceWrite(Room &r,lua_State *L,const QString &name,const QString &key){
 auto *s=current(r,L);if(!s||!AIRound45Inspect::capacity(L,32))return false;auto it=s->spaces.constFind(name);
 if(it==s->spaces.cend()||!it->valid||!it->newindex||it->setters.value(key).nonempty)return false;
 Stack stack(L);globals(L);field(L,-1,name.toUtf8().constData());const int ns=lua_absindex(L,-1);
 if(lua_topointer(L,ns)!=it->identity||!lua_getmetatable(L,ns)||lua_topointer(L,-1)!=it->meta)return false;
 const int meta=lua_absindex(L,-1);field(L,meta,"__newindex");if(function(L,-1)!=it->newindex)return false;
 field(L,meta,".set");return sameMethod(L,-1,it->setters,key);
}
bool matchAIRound56NativeNamespaceWriteFrame(Room&r,lua_State*L,const void*identity,const QString&space,const char*key){
 auto*s=current(r,L);if(!s||!L||isLua(L->ci)||!key)return false;
 const auto it=s->spaces.constFind(space);
 if(it==s->spaces.cend()||!it->valid||it->identity!=identity||!it->newindex||it->setters.value(QString::fromLatin1(key)).nonempty)return false;
 auto fn=[](const TValue*v)->lua_CFunction{
  if(ttislcf(v))return fvalue(v);
  return ttisCclosure(v)&&clCvalue(v)->nupvalues==0?clCvalue(v)->f:nullptr;
 };
 if(fn(s2v(L->ci->func.p))!=it->newindex)return false;
 auto lookup=[&](Table*t,const char*k)->const TValue*{
  if(t)for(int n=0;n<allocsizenode(t);++n){const Node*v=gnode(t,n);
   if(!isempty(gval(v))&&keyiscollectable(v)&&(keytt(v)==ctb(LUA_VSHRSTR)||keytt(v)==ctb(LUA_VLNGSTR))){
    const auto*str=gco2ts(keyval(v).gc);if(tsslen(str)==std::strlen(k)&&!std::memcmp(getstr(str),k,tsslen(str)))return gval(v);
   }
  }return &G(L)->nilvalue;
 };
 if(!ttistable(&G(L)->l_registry))return false;
 const auto*g=luaH_getint(hvalue(&G(L)->l_registry),LUA_RIDX_GLOBALS);
 if(!ttistable(g))return false;
 const auto*v=lookup(hvalue(g),space.toUtf8().constData());
 if(!ttistable(v)||hvalue(v)!=identity)return false;
 Table*t=hvalue(v);if(!t->metatable||t->metatable!=it->meta)return false;
 if(fn(lookup(t->metatable,"__newindex"))!=it->newindex)return false;
 const auto*set=lookup(t->metatable,".set");
 return ttistable(set)&&!hvalue(set)->metatable&&ttisnil(lookup(hvalue(set),key));
}
bool matchAIRound45NativeFileMethods(Room &r,lua_State *L,const QStringList &keys){
 auto *s=current(r,L);if(!s||!AIRound45Inspect::capacity(L,32)||!s->fileMeta||!s->fileMethods||keys.isEmpty())return false;
 Stack stack(L);field(L,LUA_REGISTRYINDEX,LUA_FILEHANDLE);
 if(!plain(L,-1)||lua_topointer(L,-1)!=s->fileMeta)return false;field(L,-1,"__index");
 if(!plain(L,-1)||lua_topointer(L,-1)!=s->fileMethods)return false;
 for(const auto&key:keys)if(!s->fileLeaves.value(key).fn||!sameMethod(L,-1,s->fileLeaves,key))return false;
 return true;
}

bool matchAIRound45NativeNamespaceRead(Room &r,lua_State *L,const QString &name,const QString &key){
 auto *s=current(r,L);if(!s||!AIRound45Inspect::capacity(L,32))return false;auto it=s->spaces.constFind(name);
 if(it==s->spaces.cend()||!it->valid||it->getters.value(key).nonempty||it->functions.value(key).nonempty)return false;
 Stack stack(L);globals(L);field(L,-1,name.toUtf8().constData());const int ns=lua_absindex(L,-1);
 if(lua_topointer(L,ns)!=it->identity)return false;field(L,ns,key.toUtf8().constData());if(!lua_isnil(L,-1))return false;
 if(!lua_getmetatable(L,ns)||lua_topointer(L,-1)!=it->meta)return false;const int meta=lua_absindex(L,-1);
 field(L,meta,"__index");if(function(L,-1)!=it->index)return false;
 field(L,meta,".get");if(!sameMethod(L,-1,it->getters,key))return false;
 field(L,meta,".fn");return sameMethod(L,-1,it->functions,key);
}

// Does not invoke the constructor or grant arbitrary native object authority.
bool matchAIRound47NativeScratchConstructor(Room&r,lua_State*L,int i,const QString&name){
 auto*s=current(r,L);if(!s||!s->constructors.contains(name)||!capacity(L,16))return false;
 Stack stack(L);const auto&c=s->constructors[name];
 if(!table(L,i)||lua_topointer(L,i)!=c.table||!lua_getmetatable(L,i)||lua_topointer(L,-1)!=c.meta)return false;
 field(L,-1,"__call");return c.call&&function(L,-1)==c.call;
}

bool matchAIRound47NativeValueMetamethods(Room&r,lua_State*L,QString*why){
 auto fail=[&](const QString&m){if(why)*why=m;return false;};
 auto*s=current(r,L);if(!s||!capacity(L,64))return fail("native value seal state/generation/stack unavailable");Stack stack(L);
 field(L,LUA_REGISTRYINDEX,"SWIG");if(!plain(L,-1))return fail("native value SWIG registry is not plain");int reg=lua_absindex(L,-1);
 // Check ALL retained base entries first; a proxy's runtime DFS cannot hide a
 // replaced earlier base behind an unchanged terminal in another branch.
 for(auto it=s->classes.cbegin();it!=s->classes.cend();++it){
  field(L,reg,it.key().toUtf8().constData());if(!plain(L,-1)||lua_topointer(L,-1)!=it->identity)return fail("native value class identity changed: "+it.key());
  int mt=lua_absindex(L,-1);for(auto m=it->valueMetas.cbegin();m!=it->valueMetas.cend();++m){
   field(L,mt,m.key().toUtf8().constData());bool ok=false;
   if(m->proxy){QStringList search;ok=lua_topointer(L,-1)==m->closure
      &&aiRound47NativeMetamethodProxy(L,-1,it.key(),m.key(),search)&&search==m->search;}
   else ok=m->direct.nonempty?m->direct.fn&&function(L,-1)==m->direct.fn:lua_isnil(L,-1);
   if(!ok&&it.key()=="CardUseStruct"&&m.key()=="__gc"&&s->finalizerPhase==Seal::Finished
     &&s->finalizerMeta==it->identity&&function(L,-1)==s->finalizerWritten
     &&hasAIRound56BootstrapMetadataContinuity(r,L))ok=true;
   if(!ok){const QString expected=m->proxy?"sealed generated inherited CClosure":m->direct.nonempty?(m->direct.fn?"native C function":"unproved bootstrap value"):"nil";
    const TValue*v=value(L,-1);const int uv=ttisCclosure(v)?clCvalue(v)->nupvalues:-1;
    return fail(QStringLiteral("native value %1.%2: expected %3; actual type=%4 C=%5 upvalues=%6 identity_match=%7")
      .arg(it.key(),m.key(),expected,QString::fromLatin1(lua_typename(L,lua_type(L,-1))))
      .arg(lua_iscfunction(L,-1)).arg(uv).arg(lua_topointer(L,-1)==m->closure));}
   lua_pop(L,1);
  }lua_pop(L,1);
 }
 // SWIG skips the proxy root and walks COMPILED bases depth-first. Captured
 // Lua .bases is not the dispatch authority. All intermediate lookups above
 // are sealed; require the first nonnil nonproxy terminal to be native too.
 for(auto it=s->classes.cbegin();it!=s->classes.cend();++it)
  for(auto m=it->valueMetas.cbegin();m!=it->valueMetas.cend();++m)if(m->proxy){
   bool terminal=false;for(const QString&base:m->search){
    auto b=s->classes.constFind(base);if(b==s->classes.cend())return fail("native proxy unsealed compiled base: "+base);
    auto v=b->valueMetas.constFind(m.key());if(v==b->valueMetas.cend())return fail("native proxy unsealed member: "+base+'.'+m.key());
    if(v->proxy||!v->direct.nonempty)continue;
    if(!v->direct.fn)return fail("native proxy nonnative terminal: "+base+'.'+m.key());
    terminal=true;break;
   }if(!terminal)return fail("native proxy terminal absent: "+it.key()+'.'+m.key());
  }
 return true;
}

// Lua 5.4 default (without LUA_NOCVTS2N) string arithmetic delegates to the second
// operand's metamethod on failed conversion. Matching these C functions is
// therefore NOT an effect grant; finite callers separately seal all operand
// primitive/plain/native-userdata domains and prohibit authored capabilities.
bool matchAIRound47NativeStringMetatable(Room&r,lua_State*L){
 auto*s=current(r,L);if(!s||!s->stringMetaValid||!capacity(L,32))return false;Stack stack(L);
 if(!primitiveMetatable(L,LUA_TSTRING)||!plain(L,-1)||lua_topointer(L,-1)!=s->stringMeta)return false;
 const int meta=lua_absindex(L,-1);int count=0;lua_pushnil(L);
 while(next(L,meta)){
  if(lua_type(L,-2)!=LUA_TSTRING)return false;const QString key=QString::fromUtf8(lua_tostring(L,-2));++count;
  if(key=="__index"){
   if(!plain(L,-1)||lua_topointer(L,-1)!=s->stringLibrary)return false;
   globals(L);field(L,-1,"string");bool same=lua_rawequal(L,-1,-3);lua_pop(L,2);if(!same)return false;
  }else {auto it=s->stringMetas.constFind(key);if(it==s->stringMetas.cend()||!it->fn||function(L,-1)!=it->fn)return false;}
  lua_pop(L,1);
 }
 return count==s->stringMetas.size()+1;
}

// R56 exact setter identity only; caller proves the written value and effects.
bool matchAIRound56NativeClassSetter(Room&r,lua_State*L,const QString&name,const QString&key){
 auto*s=current(r,L);if(!s||!AIRound45Inspect::capacity(L,64))return false;
 auto it=s->classes.constFind(name);if(it==s->classes.cend()||!it->valid||!it->newindex||!it->setters.value(key).fn)return false;
 Stack z(L);field(L,LUA_REGISTRYINDEX,"SWIG");if(!plain(L,-1))return false;field(L,-1,name.toUtf8().constData());
 const int meta=lua_absindex(L,-1);if(!plain(L,meta)||lua_topointer(L,meta)!=it->identity)return false;
 field(L,meta,"__newindex");if(function(L,-1)!=it->newindex)return false;lua_pop(L,1);
 field(L,meta,".set");return sameMethod(L,-1,it->setters,key);
}

bool matchAIRound56NativeLeafFrame(Room&r,lua_State*L,const QString&path){
 auto*s=current(r,L);if(!s||isLua(L->ci))return false;
 const auto expected=s->functions.value(path,nullptr);if(!expected)return false;
 const auto*v=s2v(L->ci->func.p);
 const auto actual=ttislcf(v)?fvalue(v):(ttisCclosure(v)&&clCvalue(v)->nupvalues==0?clCvalue(v)->f:nullptr);
 return actual==expected;
}

bool matchAIRound56NativeClassLeafFrame(Room&r,lua_State*L,const QString&name,const QString&method){
 auto*s=current(r,L);if(!s||isLua(L->ci))return false;
 const auto cls=s->classes.constFind(name);if(cls==s->classes.cend()||!cls->valid)return false;
 const auto expected=cls->functions.value(method).fn;if(!expected)return false;
 const auto*v=s2v(L->ci->func.p);
 const auto actual=ttislcf(v)?fvalue(v):(ttisCclosure(v)&&clCvalue(v)->nupvalues==0?clCvalue(v)->f:nullptr);
 return actual==expected;
}
