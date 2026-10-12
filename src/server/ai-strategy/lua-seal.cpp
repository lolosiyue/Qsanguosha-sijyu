#include "ai-strategy/lua-seal.h"

// ---- lua-code-seal ----
#include "ai-strategy/runtime.h"
#include "room.h"
#include "room-runtime.h"
#include "lua-runtime.h"
#include "ai-runtime.h"
#include "ai-strategy/lua-raw-inspect.h"
#include "runtime-paths.h"
#include "lua.hpp"
#include "lobject.h"
#include "lstate.h"
#include "lopcodes.h"
extern "C" {
#include "ldebug.h"
}
#include <QVector>
#include <array>
#include <QCryptographicHash>
#include <QFile>
#include <QHash>
#include <QSet>
#include <QObject>
#include <cstring>
#include <limits>
#include <new>

namespace {
constexpr const char *name="AI.R45.PinnedSourceCode";
using ProtoShape=std::array<int,9>;
ProtoShape shape(const Proto *p) noexcept {
 return {p->linedefined,p->lastlinedefined,p->numparams,p->is_vararg,p->maxstacksize,
         p->sizeupvalues,p->sizek,p->sizecode,p->sizep};
}

struct Source {
 QByteArray sha;
 QHash<quint64,QByteArray> bodies;
 QHash<quint64,ProtoShape> shapes;
 QSet<quint64> ambiguous;
 QHash<quint64,QSet<int>> ancestors;
 QHash<quint64,QVector<int>> instructionLines;
};
class Seals final:public QObject {
public:
 explicit Seals(Room &room):QObject(&room){setObjectName(name);}
 lua_State *state=nullptr; quint64 generation=0;
 QHash<QString,Source> sources;
 // C++ owns only bytes, never a Lua pointer/reference. Room destruction and
 // Lua GC/free/reuse cannot leave a dangling Proto or resurrect an old seal.
};
Seals *get(Room &room,bool create=false){
 for(QObject *child:room.children())if(child->objectName()==name)
  return static_cast<Seals *>(child);
 return create?new Seals(room):nullptr;
}
bool bound(Room &room,lua_State *L,quint64 &generation){
 auto *runtime=room.roomRuntime();
 if(!runtime||!L||runtime->lua().state()!=L)return false;
 generation=runtime->lua().generation();return true;
}
bool refuse(QString *reason,const char *message){if(reason)*reason=QString::fromLatin1(message);return false;}
quint64 key(int first,int last){return (quint64(quint32(first))<<32)|quint32(last);}
const Proto *getProto(lua_State *L,int index){
 if(!lua_isfunction(L,index)||lua_iscfunction(L,index))return nullptr;
 return static_cast<const LClosure *>(lua_topointer(L,index))->p;
}
void integer(QByteArray &bytes,quint64 value){
 for(int i=0;i<8;++i)bytes.append(char(value>>(i*8)));
}
void string(QByteArray &bytes,const TString *value){
 bytes.append(value?'S':'N');if(!value)return;
 const auto length=tsslen(value);integer(bytes,quint64(length));
 bytes.append(getstr(value),qsizetype(length));
}
bool encode(const Proto *p,QByteArray &bytes,int depth,int &budget){
 if(!p||depth>128||--budget<0)return false;
 // Debug source strings and local variable names are not executable. Full
 // anchors are included, but cannot substitute for code/constant equality.
 integer(bytes,p->linedefined);integer(bytes,p->lastlinedefined);
 integer(bytes,p->numparams);integer(bytes,p->is_vararg);integer(bytes,p->maxstacksize);
 integer(bytes,p->sizeupvalues);integer(bytes,p->sizek);integer(bytes,p->sizecode);integer(bytes,p->sizep);
 for(int i=0;i<p->sizecode;++i)integer(bytes,quint64(p->code[i]));
 for(int i=0;i<p->sizek;++i){
  const TValue &v=p->k[i];integer(bytes,ttypetag(&v));
  if(ttisnil(&v)||ttisboolean(&v))continue;
  if(ttisinteger(&v)){integer(bytes,quint64(ivalue(&v)));continue;}
  if(ttisfloat(&v)){
   const lua_Number number=fltvalue(&v);bytes.append(reinterpret_cast<const char *>(&number),sizeof(number));continue;
  }
  if(ttisstring(&v)){string(bytes,tsvalue(&v));continue;}
  return false;
 }
 for(int i=0;i<p->sizeupvalues;++i){const auto &u=p->upvalues[i];
  integer(bytes,u.instack);integer(bytes,u.idx);integer(bytes,u.kind);string(bytes,u.name);
 }
 for(int i=0;i<p->sizep;++i)if(!encode(p->p[i],bytes,depth+1,budget))return false;
 return true;
}
bool signature(const Proto *p,QByteArray &out){
 QByteArray bytes;int budget=100000;if(!encode(p,bytes,0,budget))return false;
 out=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);return true;
}
bool collect(const Proto *p,Source &source,int depth=0,QSet<int> ancestors={}){
 if(!p||depth>128)return false;QByteArray body;if(!signature(p,body))return false;
 const quint64 anchor=key(p->linedefined,p->lastlinedefined);
 source.shapes.insert(anchor,shape(p));
 ancestors.insert(p->linedefined);source.ancestors.insert(anchor,ancestors);
 QVector<int> lines;lines.reserve(p->sizecode);
 for(int pc=0;pc<p->sizecode;++pc)lines.append(luaG_getfuncline(p,pc));
 source.instructionLines.insert(anchor,lines);
 if(source.bodies.contains(anchor)){source.ambiguous.insert(anchor);source.bodies.remove(anchor);}
 else if(!source.ambiguous.contains(anchor))source.bodies.insert(anchor,body);
 for(int i=0;i<p->sizep;++i)if(!collect(p->p[i],source,depth+1,ancestors))return false;
 return true;
}
}
bool registerAIPinnedSource(Room &room,lua_State *L,const QString &path,
                                  const QByteArray &sha,QString *reason){
 quint64 generation=0;if(!bound(room,L,generation))return refuse(reason,"source seal requires the bound gameplay runtime");
 if(path.isEmpty()||path.startsWith('/')||path.contains("..")||sha.size()!=64)
  return refuse(reason,"source seal path/hash is not a finite trusted descriptor");
 for(char c:sha)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))
  return refuse(reason,"source seal hash must be a fixed lowercase SHA256");
 Seals *seals=get(room,true);
 if(seals->state&&(seals->state!=L||seals->generation!=generation))
  return refuse(reason,"source seal belongs to another runtime generation; explicit reset required");
 seals->state=L;seals->generation=generation;
 if(seals->sources.contains(path)){
  if(seals->sources.value(path).sha!=sha)return refuse(reason,"source seal cannot be repinned in one runtime generation");
  if(reason)reason->clear();return true;
 }
 QFile file(QSanRuntimePaths::assetPath(path));
 if(!file.open(QIODevice::ReadOnly))return refuse(reason,"pinned source unavailable");
 const QByteArray bytes=file.readAll();
 if(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()!=sha)
  return refuse(reason,"installed source differs from the independently audited SHA256");
 const int top=lua_gettop(L);const QByteArray chunkName='@'+path.toUtf8();
 // luaL_loadfile accepts an initial UTF-8 BOM. Keep the independent hash on
 // the exact installed bytes above, then apply only that lexical prefix rule.
 // No newline is removed, so full Proto/source line anchors remain unchanged.
 const int bom = bytes.startsWith(QByteArray::fromHex("efbbbf")) ? 3 : 0;
 if(luaL_loadbufferx(L,bytes.constData()+bom,size_t(bytes.size()-bom),chunkName.constData(),"t")!=LUA_OK){
  lua_settop(L,top);return refuse(reason,"pinned source did not compile");
 }
 Source source;source.sha=sha;bool valid=false;
 try { valid=collect(getProto(L,-1),source); }
 catch(const std::bad_alloc &){valid=false;}
 lua_settop(L,top);
 if(!valid)return refuse(reason,"pinned source metadata is outside bounded encoding");
 seals->sources.insert(path,source);if(reason)reason->clear();return true;
}
bool matchAIPinnedFunction(Room &room,lua_State *L,int index,const QString &path,
                                 int first,int last,QString *reason){
 quint64 generation=0;auto *seals=get(room);
 if(!bound(room,L,generation)||!seals||seals->state!=L||seals->generation!=generation)
  return refuse(reason,"source seal runtime/generation is absent or foreign");
 const auto it=seals->sources.constFind(path);
 if(it==seals->sources.cend())return refuse(reason,"source has no trusted bootstrap seal");
 const quint64 anchor=key(first,last);
 if(it->ambiguous.contains(anchor)||!it->bodies.contains(anchor))
  return refuse(reason,"source function anchor is missing or ambiguous");
 const Proto *actual=getProto(L,index);
 if(!actual||actual->linedefined!=first||actual->lastlinedefined!=last)
  return refuse(reason,"effective value is not the pinned Lua function");
 QByteArray actualSignature;bool valid=false;
 try { valid=signature(actual,actualSignature); }
 catch(const std::bad_alloc &){valid=false;}
 if(!valid||actualSignature!=it->bodies.value(anchor))return refuse(reason,"effective executable Proto differs from the pinned source body");
 if(reason)reason->clear();return true;
}
bool matchAIPinnedFunctionFamily(Room &room,lua_State *L,int index,const QString &path,
                                       const QList<int> &outers,QString *reason){
 // match checks the runtime before any reference is used.
 const Proto *p=L?getProto(L,index):nullptr;
 if(!p||!matchAIPinnedFunction(room,L,index,path,p->linedefined,p->lastlinedefined,reason))return false;
 const auto *seals=get(room);const auto it=seals->sources.constFind(path);
 const auto ancestors=it->ancestors.value(key(p->linedefined,p->lastlinedefined));
 for(int first:outers)if(ancestors.contains(first)){if(reason)reason->clear();return true;}
 return refuse(reason,"function is outside the independently audited source family");
}
bool matchAIPinnedInstruction(Room &room,lua_State *L,const void *borrowedClosure,
                                    int pc,const QString &path,int first,int last,int line){
 quint64 generation=0;auto *seals=get(room);
 if(!borrowedClosure||!bound(room,L,generation)||!seals||seals->state!=L||seals->generation!=generation)return false;
 const auto it=seals->sources.constFind(path);if(it==seals->sources.cend())return false;
 const quint64 anchor=key(first,last);
 if(it->ambiguous.contains(anchor)||!it->bodies.contains(anchor))return false;
 // The caller supplies only the CURRENT LIVE Lua frame's closure. No Lua
 // handle, saved address, heap search, or sampled registry value is accepted.
 const auto *actual=static_cast<const LClosure *>(borrowedClosure)->p;
 if(!actual||actual->linedefined!=first||actual->lastlinedefined!=last||pc<0||pc>=actual->sizecode)return false;
 // Negative-only filter using exactly the scalar headers already serialized
 // by signature(). Equal shape still requires the complete executable hash.
 const auto expectedShape=it->shapes.constFind(anchor);
 if(expectedShape==it->shapes.cend()||shape(actual)!=expectedShape.value())return false;
 const auto lines=it->instructionLines.constFind(anchor);
 if(lines==it->instructionLines.cend()||pc>=lines->size()||lines->at(pc)!=line)return false;
 // The line map comes from independently compiled hash-pinned bytes. Reading
 // actual->lineinfo would let altered debug metadata relabel another write.
 QByteArray sig;bool valid=false;
 try{valid=signature(actual,sig);}catch(const std::bad_alloc&){valid=false;}
 return valid&&sig==it->bodies.value(anchor);
}
bool matchAIPinnedLiveInstruction(Room&r,lua_State*L,const void*borrowed,int pc,
 const QString&path,const QList<int>&families,int opcode){
 quint64 generation=0;auto*seals=get(r);
 if(!borrowed||!bound(r,L,generation)||!seals||seals->state!=L||seals->generation!=generation)return false;
 const auto it=seals->sources.constFind(path);if(it==seals->sources.cend())return false;
 const auto*p=static_cast<const LClosure*>(borrowed)->p;if(!p||pc<0||pc>=p->sizecode)return false;
 const quint64 anchor=key(p->linedefined,p->lastlinedefined);
 if(it->ambiguous.contains(anchor)||!it->bodies.contains(anchor))return false;
 const auto expectedShape=it->shapes.constFind(anchor);
 if(expectedShape==it->shapes.cend()||shape(p)!=expectedShape.value())return false;
 if(opcode>=0&&GET_OPCODE(p->code[pc])!=opcode)return false;
 if(!families.isEmpty()){
  bool family=false;const auto ancestors=it->ancestors.value(anchor);
  for(int first:families)if(ancestors.contains(first)){family=true;break;}
  if(!family)return false;
 }
 QByteArray actual;bool valid=false;try{valid=signature(p,actual);}catch(const std::bad_alloc&){return false;}
 return valid&&actual==it->bodies.value(anchor);
}
void clearAIPinnedSources(Room &room){delete get(room);}

// A live caller-owned capture graph keeps this closure reachable throughout
// metadata inspection. signature allocates native bytes only, never Lua/GC.
bool matchAIPinnedClosureMetadata(Room &room,lua_State *L,const void *live,
 const QString &path,int first,int last) {
 quint64 generation=0;auto *seals=get(room);
 if(!live||!bound(room,L,generation)||!seals||seals->state!=L||seals->generation!=generation)return false;
 const auto it=seals->sources.constFind(path);if(it==seals->sources.cend())return false;
 const quint64 anchor=key(first,last);
 if(it->ambiguous.contains(anchor)||!it->bodies.contains(anchor))return false;
 const auto *actual=static_cast<const LClosure *>(live)->p;
 if(!actual||actual->linedefined!=first||actual->lastlinedefined!=last)return false;
 QByteArray actualSignature;
 try{return signature(actual,actualSignature)&&actualSignature==it->bodies.value(anchor);}
 catch(const std::bad_alloc &){return false;}
}

bool matchAIPinnedAuxiliaryRoot(Room &room,lua_State *auxiliary,const QString &path) {
 auto *runtime=room.roomRuntime();auto *seals=get(room);
 if(!runtime||!seals||!auxiliary||runtime->ai().lua().rawState()!=auxiliary
   ||!runtime->ai().lua().isCurrentThreadOwner()||!runtime->lua().isCurrentThreadOwner()
   ||seals->state!=runtime->lua().rawState()||seals->generation!=runtime->lua().generation())return false;
 const auto it=seals->sources.constFind(path);if(it==seals->sources.cend())return false;
 const auto *value=AILuaInspect::value(auxiliary,-1);
 if(!ttisLclosure(value))return false;const Proto *actual=clLvalue(value)->p;
 const auto anchor=key(0,0);
 if(!actual||actual->linedefined||actual->lastlinedefined||it->ambiguous.contains(anchor)
   ||!it->bodies.contains(anchor))return false;
 QByteArray signatureBytes;
 try{return signature(actual,signatureBytes)&&signatureBytes==it->bodies.value(anchor);}
 catch(const std::bad_alloc &){return false;}
}

bool matchAIPinnedAuxiliaryFunction(Room &room,lua_State *L,int index,
 const QString &path,int first,int last) {
 auto *runtime=room.roomRuntime();auto *seals=get(room);
 if(!runtime||!seals||!L||runtime->ai().lua().rawState()!=L
   ||!runtime->ai().lua().isCurrentThreadOwner()||!runtime->lua().isCurrentThreadOwner()
   ||seals->state!=runtime->lua().rawState()||seals->generation!=runtime->lua().generation())return false;
 const auto it=seals->sources.constFind(path);if(it==seals->sources.cend())return false;
 const auto anchor=key(first,last);const Proto *actual=getProto(L,index);
 if(!actual||actual->linedefined!=first||actual->lastlinedefined!=last
   ||it->ambiguous.contains(anchor)||!it->bodies.contains(anchor))return false;
 QByteArray body;
 try{return signature(actual,body)&&body==it->bodies.value(anchor);}
 catch(const std::bad_alloc &){return false;}
}

// ---- lua-table-observers ----
#include "ai-strategy/native-registry.h"
#include <cstdio>
#ifdef __linux__
#include <execinfo.h>
#endif
#include "ai-strategy/source-slots.h"
#include "ai-strategy/ol-ai.h"
#include "ai-strategy/xufa.h"
#include "ai-strategy/official.h"
#include "ai-strategy/original-hegemony.h"
#include "ai-strategy/context.h"
#include "ai-strategy/inovation-ai.h"
#include "ai.h"
#include "serverplayer.h"
#include <QPointer>
namespace {
// Raw metadata only: model the audited middleclass lookup, never call __index.
struct StackRestore { lua_State*L;int top;explicit StackRestore(lua_State*s):L(s),top(lua_gettop(s)){}~StackRestore(){lua_settop(L,top);} };
bool origin(lua_State*L,int i,const char*source,int line){
 if(!lua_isfunction(L,i)||lua_iscfunction(L,i))return false;
 lua_pushvalue(L,i);lua_Debug d{};return lua_getinfo(L,">S",&d)&&(QString::fromUtf8(d.source?d.source:"")==QString::fromLatin1(source)||(QString::fromLatin1(source)=="@lua/lib/middleclass.lua"&&QString::fromUtf8(d.source?d.source:"")=="@./lua/lib/middleclass.lua"))&&d.linedefined==line;
}
void raw(lua_State*L,int i,const char*key){i=lua_absindex(L,i);lua_pushstring(L,key);lua_rawget(L,i);}
bool upvalue(lua_State*L,int i,const char*key){
 i=lua_absindex(L,i);for(int n=1;;++n){const char*k=lua_getupvalue(L,i,n);if(!k)return false;if(!strcmp(k,key))return true;lua_pop(L,1);}
}
// Returns exactly one value on success; recursion is finite and fail-closed.
bool luaTableObserversLookup(lua_State*L,int i,const char*key,int depth=0){
 if(depth>12||!lua_istable(L,i))return false;i=lua_absindex(L,i);
 const int top=lua_gettop(L);raw(L,i,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);
 if(!lua_getmetatable(L,i)){lua_pushnil(L);return true;}
 raw(L,-1,"__index");
 if(lua_isnil(L,-1)){lua_settop(L,top);lua_pushnil(L);return true;}
 bool ok=false;
 if(lua_istable(L,-1))ok=luaTableObserversLookup(L,-1,key,depth+1);
 else if(origin(L,-1,"@lua/lib/middleclass.lua",33)||origin(L,-1,"@lua/lib/middleclass.lua",35)){
  const int fn=lua_absindex(L,-1);const bool inherited=origin(L,fn,"@lua/lib/middleclass.lua",33);
  if(upvalue(L,fn,"dict")&&luaTableObserversLookup(L,-1,key,depth+1)){
   if(lua_toboolean(L,-1)||!inherited)ok=true;
   else if(upvalue(L,fn,"superStatic"))ok=luaTableObserversLookup(L,-1,key,depth+1);
  }
 }
 if(!ok){lua_settop(L,top);return false;}lua_replace(L,top+1);lua_settop(L,top+1);return true;
}
bool initializer(lua_State*L,int i,QVector<const void*>&proof,int depth=0){
 if(depth>3)return false;i=lua_absindex(L,i);proof<<lua_topointer(L,i);
 if(origin(L,i,"@lua/ai/smart-ai.lua",339))return true;
 const char*up=nullptr;
 if(origin(L,i,"@lua/ai/original-hegemony/heg-common-ai.lua",192))up="initialize";
 else if(origin(L,i,"@lua/ai/basara-ai.lua",58))up="init";
 if(!up||!upvalue(L,i,up))return false;bool ok=initializer(L,-1,proof,depth+1);lua_pop(L,1);return ok;
}
QVector<const void*> constructorProof(lua_State*L){
 StackRestore stack(L);QVector<const void*> proof;
 lua_pushglobaltable(L);const int globals=lua_absindex(L,-1);
 raw(L,globals,"CloneAI");if(!origin(L,-1,"@lua/ai/smart-ai.lua",23))return{};proof<<lua_topointer(L,-1);
 raw(L,globals,"SmartAI");if(!lua_istable(L,-1))return{};const int cls=lua_absindex(L,-1);proof<<lua_topointer(L,cls);
 if(!lua_getmetatable(L,cls))return{};raw(L,-1,"__call");if(!origin(L,-1,"@lua/lib/middleclass.lua",44))return{};proof<<lua_topointer(L,-1);
 for(const auto&entry:{qMakePair("new",103),qMakePair("allocate",98)}){
  if(!luaTableObserversLookup(L,cls,entry.first)||!origin(L,-1,"@lua/lib/middleclass.lua",entry.second))return{};proof<<lua_topointer(L,-1);
 }
 raw(L,cls,"__instanceDict");if(!lua_istable(L,-1))return{};const int dict=lua_absindex(L,-1);proof<<lua_topointer(L,dict);
 raw(L,dict,"__index");if(!lua_rawequal(L,-1,dict))return{};
 if(!luaTableObserversLookup(L,dict,"initialize")||!origin(L,-1,"@lua/ai/original-hegemony/heg-common-ai.lua",192)||!initializer(L,-1,proof))return{};proof<<lua_topointer(L,-1);
 if(!luaTableObserversLookup(L,dict,"updatePlayers")||!(origin(L,-1,"@lua/ai/mode-ai.lua",2330)||origin(L,-1,"@lua/ai/smart-ai.lua",2118)))return{};proof<<lua_topointer(L,-1);
 // The userdata factory is trusted at startup, and must remain the same C function.
 raw(L,globals,"sgs");if(!lua_istable(L,-1))return{};raw(L,-1,"LuaAI");proof<<lua_topointer(L,-1);
 if(!lua_iscfunction(L,-1)){
  if(!lua_istable(L,-1)||!lua_getmetatable(L,-1))return{};
  raw(L,-1,"__call");if(!lua_iscfunction(L,-1))return{};proof<<lua_topointer(L,-1);
 }
 return proof;
}

struct LuaTableObserversRow {
 QString state; quint64 stamp=0, writes=0;
 const void*selfIdentity=nullptr;const void*listIdentity=nullptr;
 bool constructorEmpty=false;
 // Native table still is the exact untouched constructor-empty object. This
 // is separate from the isolated shadow state, which may have been cleared.
 bool mirrorEmpty=false,shadowDiverged=false;
};
struct Capture;
struct ChoiceProxyProof {
 const void*proxy=nullptr;const void*meta=nullptr;const void*values=nullptr;const void*writer=nullptr;
 bool operator==(const ChoiceProxyProof&o)const{return proxy==o.proxy&&meta==o.meta&&values==o.values&&writer==o.writer;}
};
struct Watched { enum Kind { Self, List, Metadata, Registry, Namespace, Globals, Choice, ChosenPlayer, ChosenCard, IsolatedGlobals, IsolatedNamespace, ChoiceProxy, ChosenPlayerProxy, ChosenCardProxy, ChoiceMeta, ChosenPlayerMeta, ChosenCardMeta, IsolatedCatalogue } kind; const ServerPlayer*owner=nullptr; bool isolated=false; };
class Ledger final:public QObject {
public:
 explicit Ledger(Room&r):QObject(&r),room(r){}
 ~Ledger()override;
 Room&room;bool initialized=false,interrupted=false,watchRoots=false,constructorEntryPending=false;quint64 generation=0,invalidations=0;
 QVector<const void*> proof;const ServerPlayer*pending=nullptr;
 QHash<const ServerPlayer*,LuaTableObserversRow> rows;
 QHash<const void*,Watched> watched;
 lua_State*state=nullptr;Capture*capture=nullptr;
 lua_State*choiceState=nullptr;quint64 choiceGeneration=0;bool choiceCaptured=false,choiceValid=false;
 const void*choiceGlobals=nullptr;const void*choiceCatalogue=nullptr;
 QHash<QString,ChoiceProxyProof>choiceProxies;
 void invalidate(const ServerPlayer*p){auto it=rows.find(p);if(it!=rows.end()){it->state="unknown";it->constructorEmpty=false;it->mirrorEmpty=false;it->shadowDiverged=false;++it->stamp;++it->writes;}}
 void invalidateAll(){for(auto it=rows.begin();it!=rows.end();++it){it->state="unknown";it->constructorEmpty=false;it->mirrorEmpty=false;it->shadowDiverged=false;++it->stamp;++it->writes;}}
};
struct Capture {
 Ledger*l=nullptr;const ServerPlayer*p=nullptr;const void*self=nullptr;const void*list=nullptr;const void*baseFunction=nullptr;
 bool assignment=false,registered=false,failed=false;
};
thread_local QHash<lua_State*,Capture*> captures;
void observeTable(lua_State*,const lua_TableObservation*,void*) noexcept;
bool watch(Ledger*l,const void*id,Watched w,lua_State*vm=nullptr){
 if(!id)return false;
 auto it=l->watched.constFind(id);
 if(it!=l->watched.cend())return it->kind==w.kind&&it->owner==w.owner&&it->isolated==w.isolated;
 if(!lua_observetableidentity(vm?vm:l->state,id,observeTable,l))return false;
 try{l->watched.insert(id,w);}catch(...){lua_unobservetableidentity(l->state,id,observeTable,l);return false;}
 return true;
}
void unwatchOwner(Ledger*l,const ServerPlayer*p){
 for(auto it=l->watched.begin();it!=l->watched.end();){
  if(it->owner==p){lua_unobservetableidentity(l->state,it.key(),observeTable,l);it=l->watched.erase(it);}else ++it;
 }
}
Ledger::~Ledger(){
 // FREE notifications removed dead identities synchronously. Only live tables
 // remain, so pointer reuse or a closed Lua state is never dereferenced here.
 for(auto it=watched.cbegin();it!=watched.cend();++it)lua_unobservetableidentity(state,it.key(),observeTable,this);
 watched.clear();
}
bool sameKey(const lua_TableObservation&e,const char*key){return e.string_key&&e.string_key_length==strlen(key)&&!memcmp(e.string_key,key,e.string_key_length);}
bool sourceAssignment(lua_State*L,const Capture*c,const lua_TableObservation&e){
 // Protect leaves top at ci->top: never push a function from this callback.
 // The VM supplies current Lua closure identity without touching its stack.
 lua_Debug d{};
 return e.lua_function_identity==c->baseFunction&&lua_getstack(L,0,&d)
  &&lua_getinfo(L,"Sl",&d)&&d.currentline==418&&d.linedefined==339
  &&d.source&&!strcmp(d.source,"@lua/ai/smart-ai.lua");
}
// Structural checks complement an identity captured at the trusted native core
// load boundary. Source path/line alone is explicitly not the attestation.
bool choiceProxyShape(lua_State*L,int proxy,ChoiceProxyProof&out){
 StackRestore restore(L);proxy=lua_absindex(L,proxy);if(!lua_istable(L,proxy))return false;
 lua_pushnil(L);if(lua_next(L,proxy)){return false;} // raw proxy must stay empty
 if(!lua_getmetatable(L,proxy))return false;const int mt=lua_absindex(L,-1);
 if(lua_getmetatable(L,mt))return false;
 int count=0;lua_pushnil(L);while(lua_next(L,mt)){
  if(lua_type(L,-2)!=LUA_TSTRING)return false;const char*k=lua_tostring(L,-2);
  if(strcmp(k,"__index")&&strcmp(k,"__newindex"))return false;++count;lua_pop(L,1);
 }if(count!=2)return false;
 raw(L,mt,"__index");if(!lua_istable(L,-1))return false;const int values=lua_absindex(L,-1);
 if(lua_getmetatable(L,values))return false;
 raw(L,mt,"__newindex");const int writer=lua_absindex(L,-1);
 if(!origin(L,writer,"@lua/ai/isolated/ask-for-choice.lua",29))return false;
 lua_Debug info{};lua_pushvalue(L,writer);if(!lua_getinfo(L,">S",&info)||info.lastlinedefined!=36)return false;
 bool sawValues=false,sawEnvironment=false;
 for(int i=1;;++i){const char*k=lua_getupvalue(L,writer,i);if(!k)break;
  if(!strcmp(k,"values")){if(sawValues||!lua_rawequal(L,-1,values))return false;sawValues=true;}
  else if(!strcmp(k,"_ENV")){lua_pushglobaltable(L);const bool same=lua_rawequal(L,-1,-2);lua_pop(L,1);if(sawEnvironment||!same)return false;sawEnvironment=true;}
  else return false;lua_pop(L,1);
 }
 if(!sawValues||!sawEnvironment)return false;
 out={lua_topointer(L,proxy),lua_topointer(L,mt),lua_topointer(L,values),lua_topointer(L,writer)};return true;
}
void observeTable(lua_State*L,const lua_TableObservation*ep,void*ud) noexcept {
 auto*l=static_cast<Ledger*>(ud);const auto&e=*ep;
 try{
  auto it=l->watched.constFind(e.table_identity);if(it==l->watched.cend())return;
  const Watched w=*it;
  const bool choiceProxy=w.kind==Watched::ChoiceProxy||w.kind==Watched::ChosenPlayerProxy||w.kind==Watched::ChosenCardProxy;
  const bool choiceMeta=w.kind==Watched::ChoiceMeta||w.kind==Watched::ChosenPlayerMeta||w.kind==Watched::ChosenCardMeta;
  const bool choiceCatalogue=w.kind==Watched::IsolatedCatalogue;
  if(choiceProxy||choiceMeta||choiceCatalogue){
   const bool changed=e.event==LUA_TABLE_OBSERVER_FREE||e.event==LUA_TABLE_OBSERVER_METATABLE
    ||(e.event==LUA_TABLE_OBSERVER_WRITE&&(!choiceCatalogue||sameKey(e,"ai_skill_choice")||sameKey(e,"ai_skill_playerchosen")||sameKey(e,"ai_skill_cardchosen")));
   if(changed){l->choiceValid=false;initializeAIChoiceContinuityRoots(l->room,true,false,{});interruptAIChoiceContinuity(l->room);}
   if(e.event==LUA_TABLE_OBSERVER_FREE)l->watched.remove(e.table_identity);
   return;
  }
  if(w.kind==Watched::IsolatedGlobals&&e.event==LUA_TABLE_OBSERVER_WRITE&&sameKey(e,"ai_choice_legacy_registries")){
   l->choiceValid=false;initializeAIChoiceContinuityRoots(l->room,true,false,{});interruptAIChoiceContinuity(l->room);
  }
  const int choiceDomain=w.kind==Watched::Globals||w.kind==Watched::IsolatedGlobals?0:
   w.kind==Watched::Namespace||w.kind==Watched::IsolatedNamespace?1:
   w.kind==Watched::Choice?2:w.kind==Watched::ChosenPlayer?3:w.kind==Watched::ChosenCard?4:-1;
  if(w.isolated&&choiceDomain>=0&&(e.event==LUA_TABLE_OBSERVER_FREE||e.event==LUA_TABLE_OBSERVER_METATABLE))l->choiceValid=false;
  if(choiceDomain>=0)observeAIChoiceContinuityTable(l->room,choiceDomain,w.isolated,e);
  if(w.kind==Watched::Choice||w.kind==Watched::ChosenPlayer||w.kind==Watched::ChosenCard
    ||w.kind==Watched::IsolatedGlobals||w.kind==Watched::IsolatedNamespace){
   if(e.event==LUA_TABLE_OBSERVER_FREE)l->watched.remove(e.table_identity);
   return;
  }
  if(e.event==LUA_TABLE_OBSERVER_READ||e.event==LUA_TABLE_OBSERVER_ENUMERATE){
   // Non-observation proof, not a value query. Ignore unrelated keys and
   // registry/class reads; a self enumeration can reveal key presence/order.
   if(w.kind==Watched::Self&&(e.event==LUA_TABLE_OBSERVER_ENUMERATE||sameKey(e,"toUse"))){
    auto row=l->rows.constFind(w.owner);
    if(row!=l->rows.cend()&&row->shadowDiverged)l->invalidate(w.owner);
   }
   return;
  }
  if(e.event==LUA_TABLE_OBSERVER_FREE){
   // Remove before Lua frees/reuses this identity. The VM frees its watcher.
   l->watched.remove(e.table_identity);
   if(w.owner){l->invalidate(w.owner);auto r=l->rows.find(w.owner);if(r!=l->rows.end()){
    if(r->selfIdentity==e.table_identity)r->selfIdentity=nullptr;
    if(r->listIdentity==e.table_identity)r->listIdentity=nullptr;
   }}else l->invalidateAll();
   if(l->capture&&(l->capture->self==e.table_identity||l->capture->list==e.table_identity))l->capture->failed=true;
   return;
  }
  if(w.kind==Watched::Namespace||w.kind==Watched::Globals){
   if(e.event!=LUA_TABLE_OBSERVER_WRITE||sameKey(e,w.kind==Watched::Namespace?"ais":"sgs")){
    l->invalidateAll();if(l->capture)l->capture->failed=true;
   }
   return;
  }
  if(w.kind==Watched::Metadata){l->invalidateAll();if(l->capture)l->capture->failed=true;return;}
  if(w.kind==Watched::Registry){
   if(e.event!=LUA_TABLE_OBSERVER_WRITE||!e.string_key){l->invalidateAll();if(l->capture)l->capture->failed=true;return;}
   const QString name=QString::fromUtf8(e.string_key,int(e.string_key_length));
   if(l->capture&&name==l->capture->p->objectName()&&e.value_table_identity==l->capture->self){l->capture->registered=true;return;}
   if(l->capture&&name==l->capture->p->objectName())l->capture->failed=true;
   for(auto r=l->rows.begin();r!=l->rows.end();++r)if(r.key()->objectName()==name){l->invalidate(r.key());break;}
   return;
  }
  if(w.kind==Watched::Self&&e.event==LUA_TABLE_OBSERVER_WRITE&&!sameKey(e,"toUse"))return;
  auto*c=l->capture;
  if(w.kind==Watched::Self&&e.event==LUA_TABLE_OBSERVER_WRITE&&sameKey(e,"toUse")
   &&c&&w.owner==c->p&&e.table_identity==c->self&&!c->assignment
   &&e.value_type==LUA_TTABLE&&(e.flags&LUA_TABLE_OBSERVER_VM_SLOW)&&sourceAssignment(L,c,e)){
    c->assignment=true;c->list=e.value_table_identity;
    if(!watch(l,c->list,{Watched::List,c->p}))c->failed=true;
    return;
  }
  l->invalidate(w.owner);
  if(c&&w.owner==c->p)c->failed=true;
 }catch(...){
  try{interruptAIChoiceContinuity(l->room);}catch(...){}
  // Never unwind into Lua. Fail closed on native bookkeeping allocation failure.
  for(auto r=l->rows.begin();r!=l->rows.end();++r){r->state=QStringLiteral("unknown");r->constructorEmpty=false;r->mirrorEmpty=false;r->shadowDiverged=false;++r->writes;}
  if(l->capture)l->capture->failed=true;
 }
}
void constructorHook(lua_State*L,lua_Debug*d) noexcept {
 auto*c=captures.value(L,nullptr);if(!c||c->failed||c->self)return;
 if(d->event!=LUA_HOOKCALL&&d->event!=LUA_HOOKTAILCALL)return;
 const int top=lua_gettop(L);
 try{
  if(!lua_getinfo(L,"S",d)||d->linedefined!=339||!d->source||strcmp(d->source,"@lua/ai/smart-ai.lua"))return;
  if(!lua_getinfo(L,"f",d)){c->failed=true;lua_settop(L,top);return;}
  c->baseFunction=lua_topointer(L,-1);lua_pop(L,1);
  if(!c->l->proof.contains(c->baseFunction)){c->failed=true;lua_settop(L,top);return;}
  // Inspect only the exact fixed initializer's table identity, never a field.
  const char*local=lua_getlocal(L,d,1);
  if(!local||strcmp(local,"self")||!lua_istable(L,-1)){c->failed=true;lua_settop(L,top);return;}
  c->self=lua_topointer(L,-1);
  if(!watch(c->l,c->self,{Watched::Self,c->p}))c->failed=true;
  // Every actual metatable in the audited middleclass chain must remain unchanged.
  int depth=0;
  while(lua_getmetatable(L,-1)){
   if(++depth>16||!watch(c->l,lua_topointer(L,-1),{Watched::Metadata,nullptr})){c->failed=true;break;}
  }
 }catch(...){c->failed=true;}
 lua_settop(L,top);
 // The call hook is needed only to capture the new self identity. All later
 // checks use table observers, so large native projection trees pay no hook cost.
 if(c->self)lua_sethook(L,nullptr,0,0);
}
Ledger*ledger(Room&r){for(auto*c:r.children())if(auto*l=dynamic_cast<Ledger*>(c))return l;return new Ledger(r);}
QJsonObject denied(const char*reason){return{{"kind","original_turn_use_presence"},{"known",false},{"state","unknown"},{"reason",QString::fromLatin1(reason)}};}
}
// R56 lexical native hook identity. No Lua code or authored hook is invoked.
bool matchesAIOriginalConstructorHook(Room&r,lua_State*L) noexcept {
 const auto*c=captures.value(L,nullptr);
 return c && c->l && &c->l->room == &r&&c->l->state==L&&c->p&&c->p->getRoom()==&r
  &&c->l->capture==c&&c->l->pending==c->p&&lua_gethook(L)==constructorHook
  &&lua_gethookmask(L)==LUA_MASKCALL;
}
bool matchesAIOriginalConstructorHook(lua_State*L) noexcept {
 const auto*c=captures.value(L,nullptr);
 return c&&c->l&&matchesAIOriginalConstructorHook(c->l->room,L);
}
struct AIOriginalTurnUseConstructor::Data {
 Room&r;ServerPlayer*p;lua_State*L;bool active=false,completed=false;quint64 epoch=0,generation=0;
 Capture capture;QHash<const ServerPlayer*,LuaTableObserversRow> prior;bool hookInstalled=false;
 lua_Hook oldHook=nullptr;int oldMask=0,oldCount=0;
 Data(Room&room,ServerPlayer*player,lua_State*state):r(room),p(player),L(state){}
};
void captureAIChoiceRegistryProxies(Room&r,lua_State*L){
 auto*l=ledger(r);auto*runtime=LuaRuntime::fromState(L);if(!runtime)return;
 if(l->choiceCaptured&&l->choiceState==L&&l->choiceGeneration==runtime->generation())return; // never reseal
 l->choiceCaptured=true;l->choiceValid=false;l->choiceState=L;l->choiceGeneration=runtime->generation();l->choiceProxies.clear();
 StackRestore restore(L);lua_pushglobaltable(L);const int globals=lua_absindex(L,-1);
 raw(L,globals,"ai_choice_legacy_registries");if(!lua_istable(L,-1)||lua_getmetatable(L,-1))return;const int catalogue=lua_absindex(L,-1);
 QHash<QString,ChoiceProxyProof> proofs;
 for(const char*name:{"ai_skill_choice","ai_skill_playerchosen","ai_skill_cardchosen"}){
  raw(L,catalogue,name);ChoiceProxyProof proof;if(!choiceProxyShape(L,-1,proof))return;proofs.insert(QString::fromLatin1(name),proof);lua_pop(L,1);
 }
 bool valid=watch(l,lua_topointer(L,globals),{Watched::IsolatedGlobals,nullptr,true},L)
  &&watch(l,lua_topointer(L,catalogue),{Watched::IsolatedCatalogue,nullptr,true},L);
 for(auto it=proofs.cbegin();it!=proofs.cend();++it){
  const bool choice=it.key()=="ai_skill_choice",player=it.key()=="ai_skill_playerchosen";
  valid=watch(l,it->proxy,{choice?Watched::ChoiceProxy:player?Watched::ChosenPlayerProxy:Watched::ChosenCardProxy,nullptr,true},L)&&valid;
  valid=watch(l,it->meta,{choice?Watched::ChoiceMeta:player?Watched::ChosenPlayerMeta:Watched::ChosenCardMeta,nullptr,true},L)&&valid;
  valid=watch(l,it->values,{choice?Watched::Choice:player?Watched::ChosenPlayer:Watched::ChosenCard,nullptr,true},L)&&valid;
 }
 l->choiceGlobals=lua_topointer(L,globals);l->choiceCatalogue=lua_topointer(L,catalogue);l->choiceProxies=proofs;l->choiceValid=valid;
}
bool validateAIChoiceRegistryProxies(Room&r){
 auto*l=ledger(r);if(!l->choiceValid||!l->choiceState)return false;
 auto*runtime=LuaRuntime::fromState(l->choiceState);
 if(!runtime||runtime->generation()!=l->choiceGeneration){l->choiceValid=false;return false;}
 LuaRuntime::Binding binding(*runtime,false);lua_State*L=l->choiceState;StackRestore restore(L);lua_pushglobaltable(L);const int globals=lua_absindex(L,-1);
 bool valid=lua_topointer(L,globals)==l->choiceGlobals;
 raw(L,globals,"ai_choice_legacy_registries");const int catalogue=lua_absindex(L,-1);
 valid=valid&&lua_istable(L,catalogue)&&lua_topointer(L,catalogue)==l->choiceCatalogue;
 if(valid&&lua_getmetatable(L,catalogue)){lua_pop(L,1);valid=false;}
 raw(L,globals,"sgs");const int ns=lua_absindex(L,-1);valid=valid&&lua_istable(L,ns);
 if(valid)for(auto it=l->choiceProxies.cbegin();it!=l->choiceProxies.cend();++it){
  const auto name=it.key().toUtf8();raw(L,catalogue,name.constData());const int proxy=lua_absindex(L,-1);
  raw(L,ns,name.constData());valid=lua_rawequal(L,proxy,-1);lua_pop(L,1);
  ChoiceProxyProof actual;valid=valid&&choiceProxyShape(L,proxy,actual)&&actual==it.value();lua_pop(L,1);if(!valid)break;
 }
 if(!valid){l->choiceValid=false;initializeAIChoiceContinuityRoots(r,true,false,{});interruptAIChoiceContinuity(r);}
 return valid;
}
void initializeAIChoiceRegistryTables(Room&r,lua_State*L,bool isolated){
 // Capture before observing initial globals. Do not restamp old nil evidence
 // after a transport interruption during watch installation/allocation.
 const quint64 xufaTransportStamp=r.roomRuntime()->xufaTransportEpoch();
 const quint64 xufaGenerationStamp=r.roomRuntime()->lua().generation();
 auto*l=ledger(r);if(!L){initializeAIChoiceContinuityRoots(r,isolated,false,{});return;}
 bool entryReady=true;
 if(!isolated){
  QPointer<Ledger> guard(l);
  entryReady=r.roomRuntime()->lua().installProtectedEntryObserver(l,[guard](lua_State*state,int arguments){
   if(!guard)return;
   // Each R56 certifier is a complete native/source lexical entry proof. Try
   // their union before any legacy partial/current-shape observer can revoke
   // or preserve another source. Unknown entries retire before execution.
   bool sourceEntryCertified=false;
   if(arguments>=0)try{
    sourceEntryCertified=acceptAIJijiangEntry(guard->room,state,arguments)
     ||acceptAIBootstrapProtectedEntry(guard->room,state,arguments)
     ||acceptAICloneEntry(guard->room,state,arguments)
     ||acceptAIModeEvaluationEntry(guard->room,state,arguments)
     ||acceptAIProjectionGetterEntry(guard->room,state,arguments)
     ||acceptAIPlayerCorrectionGetterEntry(guard->room,state,arguments)
     ||acceptAIChoulveTargetWriteEntry(guard->room,state,arguments)
     ||acceptAIConnectionEntry(guard->room,state,arguments)
     ||acceptAICardPatternsEntry(guard->room,state,arguments)
     ||acceptAIPlayLoopFilterEntry(guard->room,state,arguments);
   }catch(...){}
   invalidateAIGoodatwep(guard->room);
   if(sourceEntryCertified)return;
   observeAIJijiangEntry(guard->room,state,arguments);
   if(hasAIAsyncUniverse(guard->room,state)
      &&qEnvironmentVariableIsSet("QSAN_TRACE_R56_BOOTSTRAP")){
    std::fprintf(stderr,"R56_QUERY_RETIRE site=protected-entry argc=%d\n",arguments);std::fflush(stderr);
#ifdef __linux__
    // Failed exact-entry union; code addresses only, no Lua arguments/values.
    std::fprintf(stderr,"R56_PROTECTED_NATIVE_STACK_BEGIN\n");std::fflush(stderr);
    void *frames[32];const int count=::backtrace(frames,32);::backtrace_symbols_fd(frames,count,2);
    std::fprintf(stderr,"R56_PROTECTED_NATIVE_STACK_END\n");std::fflush(stderr);
#endif
   }
   retireAIProjectionSource(guard->room,state);
   observeAIUsePriorityEntry(guard->room,state,arguments);
   if(arguments<0){
    interruptAIChoiceContinuityAsync(guard->room);
    interruptAIFaqiPlansAsync(guard->room);
    return;
   }
   // Only a complete pinned effect/binding graph may retain prior observations.
   // Unknown entries and any inspection failure invalidate before execution.
   try {
    if(certifyAIContinuityEntry(guard->room,state,arguments)){
     completeAIXufaEventProof(guard->room,state,arguments);return;
    }
    if(certifyAIXufaQueryEntry(guard->room,state,arguments)||certifyAIXufaPatternsEntry(guard->room,state,arguments)){
     interruptAIChoiceContinuityExceptXufa(guard->room);clearAIFaqiPlans(guard->room);return;
    }
   }
   catch (...) {} // allocation/proof failure is never a continuity grant
   interruptAIChoiceContinuity(guard->room);
   clearAIFaqiPlans(guard->room);
  });
 }
 StackRestore stack(L);lua_pushglobaltable(L);const int globals=lua_absindex(L,-1);
 bool valid=entryReady&&watch(l,lua_topointer(L,globals),{isolated?Watched::IsolatedGlobals:Watched::Globals,nullptr,isolated},L);
 raw(L,globals,"sgs");if(!lua_istable(L,-1)){initializeAIChoiceContinuityRoots(r,isolated,false,{});return;}
 const int ns=lua_absindex(L,-1);
 valid=watch(l,lua_topointer(L,ns),{isolated?Watched::IsolatedNamespace:Watched::Namespace,nullptr,isolated},L)&&valid;
 QJsonObject initial;
 for(const auto&name:{"ai_skill_choice","ai_skill_playerchosen","ai_skill_cardchosen"}){
  raw(L,ns,name);if(!lua_istable(L,-1)){valid=false;lua_pop(L,1);continue;}
  const int table=lua_absindex(L,-1);
  if(isolated){valid=validateAIChoiceRegistryProxies(r)&&valid;lua_pop(L,1);continue;}
  if(lua_getmetatable(L,table)){valid=false;lua_pop(L,1);}
  const auto kind=!strcmp(name,"ai_skill_choice")?Watched::Choice:!strcmp(name,"ai_skill_playerchosen")?Watched::ChosenPlayer:Watched::ChosenCard;
  // Startup nil capture precedes watching this registry: no self-invalidation.
  if(!isolated)for(const auto&key:QStringList{"xuhe","olxuhe","xufa","olshebian_from","olshebian_to","olshebian"}){
   const bool belongs=kind==Watched::Choice?!key.startsWith("olshebian"):
    kind==Watched::ChosenPlayer?key.startsWith("olshebian_"):key=="olshebian";
   if(belongs){const auto b=key.toUtf8();raw(L,table,b.constData());initial.insert(key,lua_isnil(L,-1));lua_pop(L,1);}
  }
  valid=watch(l,lua_topointer(L,table),{kind,nullptr,isolated},L)&&valid;lua_pop(L,1);
 }
 initializeAIChoiceContinuityRoots(r,isolated,valid,initial,xufaTransportStamp,xufaGenerationStamp);
 // The actual R53 writer now shares the existing pre-entry/query revocation
 // routes. Every native trigger is revoked BEFORE private selection; this
 // enables only uninterrupted proved source intervals, never move events.
 if(!isolated&&valid){
  if(initializeAIXufaChoiceRegistry(r,L))enableAIXufaChoiceRegistryEntryCoverage(r,true);
  initializeAIShebian(r,L);initializeAIJijiang(r,L);
 }
}
void initializeAIOriginalTurnUse(Room&r){
 auto*l=ledger(r);if(l->initialized)return;l->initialized=true;
 l->generation=r.roomRuntime()->lua().generation();
 LuaRuntime::Binding binding(r.roomRuntime()->lua(),false);
 l->state=r.roomRuntime()->lua().state();
 l->proof=constructorProof(l->state);
 // Only registry identities, captured at trusted startup; no instance fields.
 StackRestore stack(l->state);lua_pushglobaltable(l->state);
 bool roots=watch(l,lua_topointer(l->state,-1),{Watched::Globals,nullptr});
 raw(l->state,-1,"sgs");
 if(lua_istable(l->state,-1)){
  roots=watch(l,lua_topointer(l->state,-1),{Watched::Namespace,nullptr})&&roots;
  raw(l->state,-1,"ais");
  roots=lua_istable(l->state,-1)&&watch(l,lua_topointer(l->state,-1),{Watched::Registry,nullptr})&&roots;
  l->watchRoots=roots;
 }
 initializeAIChoiceRegistryTables(r,l->state,false);
}
void invalidateAIOriginalTurnUse(Room&r){
 auto*l=ledger(r);
 // Every existing invalidation remains in force, including constructor entry.
 const bool expectedConstructorEntry=l->constructorEntryPending;
 l->constructorEntryPending=false;
 if(!expectedConstructorEntry)l->invalidateAll();
 l->interrupted=true;++l->invalidations;
 for(auto it=l->rows.begin();it!=l->rows.end();++it){it->state="unknown";++it->stamp;}
}
AIOriginalTurnUseConstructor::AIOriginalTurnUseConstructor(Room&r,ServerPlayer*p,lua_State*L):d(new Data(r,p,L)){
 auto*l=ledger(r);
 if(!p||p->getRoom()!=&r||!r.roomRuntime()||l->pending||!l->initialized||l->proof.isEmpty()
  ||r.roomRuntime()->lua().state()!=L||l->generation!=r.roomRuntime()->lua().generation()||constructorProof(L)!=l->proof)return;
 l->pending=p;l->constructorEntryPending=true;d->active=true;d->epoch=l->invalidations;d->generation=l->generation;
 d->capture={l,p};
 // Preserve either untouched startup-empty, or an exact isolated nil whose
 // watched legacy mirror has not been read/exposed or changed since divergence.
 // Every ordinary invalidation still fires; only this successful constructor
 // may restore its saved known state. Unknown rows are never candidates.
 for(auto it=l->rows.cbegin();it!=l->rows.cend();++it)
  if(it.key()!=p&&((it->state=="empty"&&it->constructorEmpty)||(it->state=="nil"&&it->shadowDiverged&&it->mirrorEmpty))&&it->selfIdentity&&it->listIdentity)
   d->prior.insert(it.key(),*it);
 unwatchOwner(l,p);
 auto it=l->rows.find(p);if(it==l->rows.end()){
  QObject::connect(p,&QObject::destroyed,l,[l,p]{unwatchOwner(l,p);l->rows.remove(p);});it=l->rows.insert(p,LuaTableObserversRow{QStringLiteral("unknown"),0});
 }
 it->state="unknown";it->constructorEmpty=false;it->mirrorEmpty=false;it->shadowDiverged=false;it->selfIdentity=nullptr;it->listIdentity=nullptr;++it->stamp;
 d->oldHook=lua_gethook(L);d->oldMask=lua_gethookmask(L);d->oldCount=lua_gethookcount(L);
 // Never displace another debug/budget hook. Such executions remain conservative.
 if(l->watchRoots&&!d->oldHook&&!captures.contains(L)){
  l->capture=&d->capture;captures.insert(L,&d->capture);
  lua_sethook(L,constructorHook,LUA_MASKCALL,0);d->hookInstalled=true;
 }
}
AIOriginalTurnUseConstructor::~AIOriginalTurnUseConstructor(){
 if(d->hookInstalled){
  if(lua_gethook(d->L)==constructorHook)lua_sethook(d->L,d->oldHook,d->oldMask,d->oldCount);
  captures.remove(d->L);
 }
 if(d->active){auto*l=ledger(d->r);l->constructorEntryPending=false;if(!d->completed)l->invalidateAll();if(l->capture==&d->capture)l->capture=nullptr;if(l->pending==d->p)l->pending=nullptr;}
 delete d;
}
void AIOriginalTurnUseConstructor::complete(AI*created){
 if(!d->active||!dynamic_cast<LuaAI*>(created))return;
 auto*l=ledger(d->r);
 if(l->pending!=d->p||l->invalidations!=d->epoch+1
  ||d->r.roomRuntime()->lua().generation()!=d->generation||constructorProof(d->L)!=l->proof)return;
 auto it=l->rows.find(d->p);if(it==l->rows.end())return;
 const bool guarded=d->hookInstalled&&lua_gethook(d->L)==d->oldHook
  &&!d->capture.failed&&d->capture.assignment&&d->capture.registered;
 if(!guarded)return;
 it->state="empty";++it->stamp;
 if(guarded){
  it->selfIdentity=d->capture.self;it->listIdentity=d->capture.list;it->constructorEmpty=true;it->mirrorEmpty=true;it->shadowDiverged=false;
  for(auto saved=d->prior.cbegin();saved!=d->prior.cend();++saved){
   auto now=l->rows.find(saved.key());
   if(now!=l->rows.end()&&now->writes==saved->writes
    &&now->selfIdentity==saved->selfIdentity&&now->listIdentity==saved->listIdentity
    &&((saved->state=="empty"&&now->constructorEmpty)
       ||(saved->state=="nil"&&now->mirrorEmpty&&now->shadowDiverged))
    &&l->watched.contains(now->selfIdentity)&&l->watched.contains(now->listIdentity))
    now->state=saved->state;
  }
 }
 d->completed=true;
 // Only called after successful protectedCall AND SWIG AI* conversion. Never
 // called on the TrustAI error/default path, and never inferred from an old AI.
}
QJsonObject queryAIOriginalTurnUse(Room&r,const AIRequest&q,const ServerPlayer*p,const QString&write){
 if(!p||p->getRoom()!=&r||!r.roomRuntime()||!r.getAllPlayers(true).contains(const_cast<ServerPlayer*>(p))
  ||q.viewerObjectName!=p->objectName()||q.stateRevision!=r.roomRuntime()->stateRevision()||!aiOriginalHegemonyQuestion(q))return denied("Original owner/request scope mismatch");
 auto*l=ledger(r);
 if(l->generation!=r.roomRuntime()->lua().generation())return denied("Original runtime generation changed");
 if(!write.isEmpty()&&(q.kind!=AIRequest::Activate||q.reason!=CardUseStruct::CARD_USE_REASON_PLAY
  ||(write!="empty"&&write!="nil"&&write!="unknown")))return denied("Original toUse source writer scope mismatch");
 auto it=l->rows.find(p);
 if(it==l->rows.end()){
  QObject::connect(p,&QObject::destroyed,l,[l,p]{unwatchOwner(l,p);l->rows.remove(p);});
  it=l->rows.insert(p,LuaTableObserversRow{l->initialized&&!l->interrupted?QStringLiteral("empty"):QStringLiteral("unknown"),0});
 }
 // A read does not change the native mirror. Every exact isolated assignment
 // starts/continues divergence from the still-watched constructor-empty
 // mirror; only a later exact nil state is eligible for restoration. All intervening semantic reads are observed.
 it->constructorEmpty=false;
 if(!write.isEmpty()){
  // Observe all isolated assignment-to-clear intervals. The actual legacy
  // mirror is unchanged; a nonempty/unknown plan is never itself restored.
  // Only a subsequent exact nil writer can yield a restorable shadow state.
  if(it->mirrorEmpty)it->shadowDiverged=true;
  it->state=write;++it->stamp;
 }
 return{{"kind","original_turn_use_presence"},{"known",it->state!="unknown"},{"state",it->state},
  {"viewer",q.viewerObjectName},{"decision_id",q.getDecisionId()},{"state_revision",q.getStateRevision()},
  {"continuity_stamp",QString::number(it->stamp)}};
}

// ---- natural-filter-natural-event ----
#include "structs.h"
#include <QList>
#include <QMetaType>
#include <QVariant>
namespace {
thread_local QHash<Room *,QList<const AINaturalFilterDispatch *>> dispatches;
thread_local QHash<Room *,QList<const AINaturalFilterEntry *>> entries;
bool method(lua_State *L,int index) {
 if(lua_type(L,index)!=LUA_TSTRING)return false;
 size_t n=0;const char *s=lua_tolstring(L,index,&n);
 return n==11&&std::memcmp(s,"filterEvent",11)==0;
}
bool faqiAG(const QVariant &v) {
 if(v.metaType().id()!=QMetaType::QString)return false;
 const QString s=v.toString(),prefix=QStringLiteral("AGChosen:faqi:");
 if(!s.startsWith(prefix))return false;
 const QString number=s.mid(prefix.size());bool ok=false;int id=number.toInt(&ok);
 return ok&&id>=-1&&number==QString::number(id);
}
template<class T>void removeFrame(QHash<Room *,QList<const T *>> &map,Room *room,const T *frame) {
 auto it=map.find(room);if(it==map.end())return;
 it->removeAll(frame);if(it->isEmpty())map.erase(it);
}
}
struct AINaturalFilterDispatch::Data {
 Room *room=nullptr;AI *ai=nullptr;QPointer<ServerPlayer> actor;
 int event=0;const QVariant *original=nullptr;
};
struct AINaturalFilterEntry::Data {
 Room *room=nullptr;AI *ai=nullptr;QPointer<ServerPlayer> actor;
 int event=0,top=0;lua_State *state=nullptr;const QVariant *copy=nullptr;
 const AINaturalFilterDispatch *dispatch=nullptr;
 const void *function=nullptr,*playerArgument=nullptr,*dataArgument=nullptr,*callFrame=nullptr;
 bool valid=false;
};
struct AINaturalEventAccess {
 static AINaturalFilterDispatch::Data *dispatch(Room *r) {
  auto it=dispatches.constFind(r);
  return it==dispatches.cend()||it->isEmpty()?nullptr:it->last()->d.get();
 }
 static AINaturalFilterEntry::Data *current(Room &r,lua_State *L,int count) {
  auto it=entries.constFind(&r);if(count!=4||!L||it==entries.cend()||it->isEmpty())return nullptr;
  auto *d=it->last()->d.get();auto di=dispatches.constFind(&r);
  if(!d->valid||d->state!=L||!d->actor||d->actor->getRoom()!=&r
     ||di==dispatches.cend()||di->isEmpty()||di->last()!=d->dispatch
     ||L->ci!=d->callFrame||lua_gettop(L)<d->top)return nullptr;
  const int f=d->top-4;
  if(!lua_isfunction(L,f)||lua_topointer(L,f)!=d->function||!method(L,f+1)
     ||!lua_isinteger(L,f+2)||lua_tointeger(L,f+2)!=d->event
     ||lua_type(L,f+3)!=LUA_TUSERDATA||lua_topointer(L,f+3)!=d->playerArgument
     ||lua_type(L,f+4)!=LUA_TUSERDATA||lua_topointer(L,f+4)!=d->dataArgument)return nullptr;
  return d;
 }
};
AINaturalFilterDispatch::AINaturalFilterDispatch(Room&r,AI*ai,ServerPlayer*p,int e,const QVariant&v):d(new Data) {
 d->room=&r;d->ai=ai;d->actor=p;d->event=e;d->original=&v;dispatches[&r].append(this);
}
AINaturalFilterDispatch::~AINaturalFilterDispatch(){removeFrame(dispatches,d->room,this);}
AINaturalFilterEntry::AINaturalFilterEntry(Room&r,AI*ai,ServerPlayer*p,int e,const QVariant&original,const QVariant&copy,lua_State*L):d(new Data) {
 d->room=&r;d->ai=ai;d->actor=p;d->event=e;d->copy=&copy;d->state=L;entries[&r].append(this);
 auto *outer=AINaturalEventAccess::dispatch(&r);
 if(!L||!outer||outer->ai!=ai||!ai||outer->actor!=p||!p||p->getRoom()!=&r
    ||outer->event!=e||outer->original!=&original||r.getLuaState()!=L)return;
 d->dispatch=dispatches.value(&r).last();d->callFrame=L->ci;d->top=lua_gettop(L);const int f=d->top-4;
 if(f<1||!lua_isfunction(L,f)||!method(L,f+1)||!lua_isinteger(L,f+2)
    ||lua_tointeger(L,f+2)!=e||lua_type(L,f+3)!=LUA_TUSERDATA||lua_type(L,f+4)!=LUA_TUSERDATA)return;
 d->function=lua_topointer(L,f);d->playerArgument=lua_topointer(L,f+3);d->dataArgument=lua_topointer(L,f+4);d->valid=true;
}
AINaturalFilterEntry::~AINaturalFilterEntry(){removeFrame(entries,d->room,this);}
bool readAINaturalFilterEntry(Room&r,lua_State*L,int count,AINaturalFilterProof*out) {
 if(out)*out={};auto*d=AINaturalEventAccess::current(r,L,count);if(!d||!out)return false;
 out->event=d->event;out->faqiAGFormat=d->event==int(ChoiceMade)&&faqiAG(*d->copy);return true;
}
bool matchesAINaturalFilterPayload(Room&r,lua_State*L,int count,const QVariant*v,ServerPlayer*p) {
 auto*d=AINaturalEventAccess::current(r,L,count);return d&&d->copy==v&&d->actor==p;
}

bool inspectAINaturalFilterEntry(Room&r,lua_State*L,int count,
 const std::function<bool(int,ServerPlayer *,const QVariant &)> &inspector) {
 auto*d=AINaturalEventAccess::current(r,L,count);
 return d&&inspector&&inspector(d->event,d->actor,*d->copy);
}

bool pushAINaturalFilterSelf(Room&r,lua_State*L,int count) {
 auto*d=AINaturalEventAccess::current(r,L,count);if(!d)return false;
 const ServerPlayer*owner=nullptr;
 for(const auto*p:r.getPlayers())if(p&&p->getSmartAI()==d->ai){if(owner)return false;owner=p;}
 return owner&&pushAIActualAISelf(r,owner,L);
}

bool matchesAINativeFilterDispatch(Room&r,lua_State*L,int count,int e,
 ServerPlayer*p,const QVariant*original){
 auto*d=AINaturalEventAccess::current(r,L,count);auto*dispatch=AINaturalEventAccess::dispatch(&r);
 return d&&dispatch&&d->event==e&&d->actor==p&&dispatch->event==e&&dispatch->actor==p
   &&dispatch->original==original;
}

// The scope caller is compiled LuaAI::filterEvent before pushCallback. The
// outer dispatch records identity, not payload values or private skill state.
bool matchesAINaturalFilterPreEntry(Room&r,AI*ai,ServerPlayer*p,int event,
 const QVariant*original,lua_State*L) noexcept {
 auto*d=AINaturalEventAccess::dispatch(&r);
 return d&&ai&&p&&p->getRoom()==&r&&d->room==&r&&d->ai==ai
  &&d->actor==p&&d->event==event&&d->original==original&&r.getLuaState()==L;
}

// ---- source-compatibility ----
#include "game-rng.h"
#include "util.h"
#include <QJsonArray>
#include <cmath>
#include <ctime>
namespace {
QJsonObject failure(const char *message) { return {{"error", message}, {"key", "source_compatibility"}}; }
}
AISourceCompatibilitySession::AISourceCompatibilitySession(Room &room, const AIRequest &request,
 int trigger, const QString &kind) : m_room(room), m_request(request),
 m_viewer(request.viewerObjectName), m_eventKind(kind), m_decision(request.decisionId),
 m_revision(request.stateRevision), m_trigger(trigger) {}
bool AISourceCompatibilitySession::current() const {
 return m_request.viewerObjectName == m_viewer && m_request.decisionId == m_decision
  && m_request.stateRevision == m_revision && m_room.roomRuntime()->stateRevision() == m_revision
  && m_room.findPlayerByObjectName(m_viewer, true);
}
bool AISourceCompatibilitySession::gaoda() const {
 return m_trigger < 0 && m_request.kind == AIRequest::UseCard
  && m_request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
  && m_request.pattern == "@@gaoda_zhanshi";
}
bool AISourceCompatibilitySession::replayClock(const QVector<ClockRead> &reads) {
 if (!m_reads.isEmpty() || m_replaying || reads.size() > 64) return false;
 for (const auto &read : reads) if (read.seconds < 0 || read.seconds > Q_INT64_C(9007199254740991)) return false;
 m_replay = reads; m_replaying = true; return true;
}
QJsonObject AISourceCompatibilitySession::clock(const QString &key) {
 if (!current()) return failure("source clock invocation expired");
 const bool shuffle = key == "gaoda:shuffle:zhanshi" && gaoda();
 const bool chat = (key == "chat:PreCardUsed:CardUse" && m_trigger == PreCardUsed && m_eventKind == "card_use")
  || (key == "chat:EventPhaseStart:luanwu" && m_trigger == EventPhaseStart && m_eventKind == "other");
 if ((!shuffle && !chat) || m_reads.size() >= 64) return failure("source clock key or bounded domain unavailable");
 if (shuffle && (m_shuffleReady || m_nextUpper != 0)) return failure("source shuffle clock order differs");
 qint64 seconds;
 if (m_replaying) {
  if (m_reads.size() >= m_replay.size() || m_replay.at(m_reads.size()).key != key)
   return failure("source clock replay order differs");
  seconds = m_replay.at(m_reads.size()).seconds;
 } else {
  const std::time_t sampled = std::time(nullptr);
  if (sampled == std::time_t(-1)) return failure("native source clock unavailable");
  seconds = qint64(sampled);
 }
 if (seconds < 0 || seconds > Q_INT64_C(9007199254740991)) return failure("source clock value outside exact Lua integer range");
 m_reads.append({key, seconds});
 if (shuffle) m_shuffleReady = true;
 return {{"kind", "source_clock"}, {"source_key", key}, {"seconds", double(seconds)}};
}
QJsonObject AISourceCompatibilitySession::gaodaRandom(GameRng &aiRng, int upper) {
 if (!current() || !gaoda() || !m_shuffleReady
  || (m_nextUpper == 0 ? (upper < 8 || upper > 11) : upper != m_nextUpper))
  return failure("Gaoda shuffle random order or source unavailable");
 // Fixed engine util.cpp190–219: randomseed only checks an integer; it does
 // not seed. Preserve random(i)'s fraction transform using the AI RNG only.
 const double random = double(aiRng.bounded(0x7fffffff)) / double(0x7fffffff);
 const int value = int(std::floor(random * upper)) + 1;
 m_nextUpper = upper - 1;
 if (upper == 2) { m_shuffleReady = false; m_nextUpper = -1; }
 return {{"kind", "source_gaoda_random"}, {"upper", upper}, {"value", value}};
}
QJsonObject AISourceCompatibilitySession::numericCosts(const QString &package, const QString &text) const {
 if (!current() || (package != "gaoda" && package != "sfoflCard")) return failure("numeric source invocation unavailable");
 if (text.toUtf8().size() > 4096 || text.contains(QChar(0))) return failure("numeric source text outside bounded domain");
 const QStringList pieces = text.split('+');
 if (pieces.size() > 128) return failure("numeric source cost count exceeds bound");
 QJsonArray ids;
 for (int id : ListS2I(pieces)) ids.append(id);
 return {{"kind", "source_numeric_costs"}, {"package", package}, {"ids", ids}};
}

// ---- lua-native-seal ----
#include "ai-strategy/tenyear-ai.h"
#include <QByteArray>
// Implemented inside the generated SWIG compilation unit. It checks native
// userdata size/type identity, never invokes conversion or Lua callbacks.
extern const char *aiNativeUserdataClass(lua_State *,int);
extern bool aiNativeMetamethodProxy(lua_State *,int,const QString &,const QString &,QStringList &);
namespace {
using namespace AILuaInspect;
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
 if(e->event==LUA_TABLE_OBSERVER_FREE){s.finalizerMeta=nullptr;s.finalizerPhase=Seal::Retired;retireAIBootstrapSourceBoundary(s.room);return;}
 if(e->event==LUA_TABLE_OBSERVER_READ||e->event==LUA_TABLE_OBSERVER_ENUMERATE)return;
 const bool originalWrite=e->event==LUA_TABLE_OBSERVER_WRITE&&e->flags==0
  &&e->value_type==LUA_TFUNCTION&&e->string_key&&e->string_key_length==4&&!std::memcmp(e->string_key,"__gc",4)
  &&s.finalizerPhase==Seal::Prepared&&L==s.L&&L->ci==s.finalizerFrame
  &&inAILianzhanBootstrapInstaller(s.room,L);
 s.finalizerPhase=originalWrite?Seal::Observed:Seal::Retired;
 if(!originalWrite)retireAIBootstrapSourceBoundary(s.room);
}
Seal *luaNativeSealGet(Room &r,bool create=false){for(QObject *o:r.children())if(o->objectName()==ledgerName)return static_cast<Seal *>(o);return create?new Seal(r):nullptr;}
Seal *current(Room &r,lua_State *L){auto *s=luaNativeSealGet(r);auto *runtime=r.roomRuntime();
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
   v.proxy=aiNativeMetamethodProxy(L,-1,name,QString::fromLatin1(k),v.search);
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
void captureAINativeBindings(Room &r,lua_State *L){
 if(luaNativeSealGet(r)||!L||!lua_checkstack(L,512))return;auto *runtime=r.roomRuntime();
 if(!runtime||runtime->lua().state()!=L)return;
 auto *s=luaNativeSealGet(r,true);s->L=L;s->generation=runtime->lua().generation();Stack stack(L);
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
void clearAINativeBindings(Room &r){delete luaNativeSealGet(r);}
bool beginAILianzhanFinalizerTransition(Room&r,lua_State*L){
 auto*s=current(r,L);if(!s||s->finalizerPhase!=Seal::Unprepared||!inAILianzhanBootstrapInstaller(r,L))return false;
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
bool completeAILianzhanFinalizerTransition(Room&r,lua_State*L){
 auto*s=current(r,L);if(!s||s->finalizerPhase!=Seal::Observed||s->finalizerFrame!=L->ci||!inAILianzhanBootstrapInstaller(r,L))return false;
 s->finalizerPhase=Seal::Retired;
 Stack stack(L);if(!capacity(L,32))return false;field(L,LUA_REGISTRYINDEX,"SWIG");if(!plain(L,-1))return false;
 field(L,-1,"CardUseStruct");if(!plain(L,-1)||lua_topointer(L,-1)!=s->finalizerMeta)return false;
 field(L,-1,"__gc");const auto written=function(L,-1);if(!written||written==s->finalizerOld)return false;
 // This is the result of the just-observed deterministic native assignment,
 // not a late sample: original old identity forced the installer write arm.
 s->finalizerWritten=written;s->finalizerFrame=nullptr;s->finalizerPhase=Seal::Finished;return true;
}

bool matchAINativeBinding(Room &r,lua_State *L,int index,const QString &path){
 auto *s=current(r,L);return s&&s->functions.contains(path)&&function(L,index)==s->functions.value(path);
}
bool matchAINativeConstant(Room &r,lua_State *L,int index,const QString &path){
 auto *s=current(r,L);if(!s||!s->constants.contains(path))return false;
 const Scalar &v=s->constants[path];if(lua_type(L,index)!=v.type)return false;
 if(v.type==LUA_TNIL)return true;
 if(v.type==LUA_TBOOLEAN)return bool(lua_toboolean(L,index))==v.b;
 if(v.type==LUA_TNUMBER){if(bool(lua_isinteger(L,index))!=v.integer)return false;
  if(v.integer)return lua_tointeger(L,index)==v.i;const lua_Number n=lua_tonumber(L,index);return !std::memcmp(&n,&v.n,sizeof n);}
 if(v.type==LUA_TSTRING){size_t n=0;const char *p=lua_tolstring(L,index,&n);return n==size_t(v.s.size())&&!std::memcmp(p,v.s.constData(),n);}
 return false;
}
bool matchAINativeClassMethod(Room &r,lua_State *L,const QString &name,const QString &key){
 auto *s=current(r,L);if(!s||!AILuaInspect::capacity(L,512))return false;QSet<QString> seen;return resolve(*s,L,name,key,seen)==1;
}
bool matchAINativeDerivedMethods(Room &r,lua_State *L,const QString &base,const QStringList &keys){
 auto *s=current(r,L);if(!s||!AILuaInspect::capacity(L,1024)||!s->classes.contains(base)||keys.isEmpty())return false;
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
bool matchAINativeNamespaceProperty(Room &r,lua_State *L,const QString &name,const QString &key){
 auto *s=current(r,L);if(!s||!AILuaInspect::capacity(L,32))return false;auto it=s->spaces.constFind(name);
 if(it==s->spaces.cend()||!it->valid||!it->getters.value(key).fn)return false;
 Stack stack(L);globals(L);field(L,-1,name.toUtf8().constData());const int ns=lua_absindex(L,-1);
 if(lua_topointer(L,ns)!=it->identity)return false;field(L,ns,key.toUtf8().constData());if(!lua_isnil(L,-1))return false;
 if(!lua_getmetatable(L,ns)||lua_topointer(L,-1)!=it->meta)return false;const int meta=lua_absindex(L,-1);
 field(L,meta,"__index");if(function(L,-1)!=it->index)return false;field(L,meta,".get");
 return sameMethod(L,-1,it->getters,key);
}

bool matchAINativeClassProperty(Room &r,lua_State *L,const QString &name,const QString &key){
 auto *s=current(r,L);if(!s||!AILuaInspect::capacity(L,512))return false;QSet<QString>seen;
 return resolve(*s,L,name,key,seen,true)==1;
}
bool matchAINativeObjectMethods(Room &r,lua_State *L,int index,const QString &base,const QStringList &keys){
 auto *s=current(r,L);if(!s||!AILuaInspect::capacity(L,1024)||keys.isEmpty())return false;
 const char *nativeName=aiNativeUserdataClass(L,index);if(!nativeName)return false;
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
bool matchAINativeNamespaceWrite(Room &r,lua_State *L,const QString &name,const QString &key){
 auto *s=current(r,L);if(!s||!AILuaInspect::capacity(L,32))return false;auto it=s->spaces.constFind(name);
 if(it==s->spaces.cend()||!it->valid||!it->newindex||it->setters.value(key).nonempty)return false;
 Stack stack(L);globals(L);field(L,-1,name.toUtf8().constData());const int ns=lua_absindex(L,-1);
 if(lua_topointer(L,ns)!=it->identity||!lua_getmetatable(L,ns)||lua_topointer(L,-1)!=it->meta)return false;
 const int meta=lua_absindex(L,-1);field(L,meta,"__newindex");if(function(L,-1)!=it->newindex)return false;
 field(L,meta,".set");return sameMethod(L,-1,it->setters,key);
}
bool matchAINativeNamespaceWriteFrame(Room&r,lua_State*L,const void*identity,const QString&space,const char*key){
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
bool matchAINativeFileMethods(Room &r,lua_State *L,const QStringList &keys){
 auto *s=current(r,L);if(!s||!AILuaInspect::capacity(L,32)||!s->fileMeta||!s->fileMethods||keys.isEmpty())return false;
 Stack stack(L);field(L,LUA_REGISTRYINDEX,LUA_FILEHANDLE);
 if(!plain(L,-1)||lua_topointer(L,-1)!=s->fileMeta)return false;field(L,-1,"__index");
 if(!plain(L,-1)||lua_topointer(L,-1)!=s->fileMethods)return false;
 for(const auto&key:keys)if(!s->fileLeaves.value(key).fn||!sameMethod(L,-1,s->fileLeaves,key))return false;
 return true;
}

bool matchAINativeNamespaceRead(Room &r,lua_State *L,const QString &name,const QString &key){
 auto *s=current(r,L);if(!s||!AILuaInspect::capacity(L,32))return false;auto it=s->spaces.constFind(name);
 if(it==s->spaces.cend()||!it->valid||it->getters.value(key).nonempty||it->functions.value(key).nonempty)return false;
 Stack stack(L);globals(L);field(L,-1,name.toUtf8().constData());const int ns=lua_absindex(L,-1);
 if(lua_topointer(L,ns)!=it->identity)return false;field(L,ns,key.toUtf8().constData());if(!lua_isnil(L,-1))return false;
 if(!lua_getmetatable(L,ns)||lua_topointer(L,-1)!=it->meta)return false;const int meta=lua_absindex(L,-1);
 field(L,meta,"__index");if(function(L,-1)!=it->index)return false;
 field(L,meta,".get");if(!sameMethod(L,-1,it->getters,key))return false;
 field(L,meta,".fn");return sameMethod(L,-1,it->functions,key);
}

// Does not invoke the constructor or grant arbitrary native object authority.
bool matchAINativeScratchConstructor(Room&r,lua_State*L,int i,const QString&name){
 auto*s=current(r,L);if(!s||!s->constructors.contains(name)||!capacity(L,16))return false;
 Stack stack(L);const auto&c=s->constructors[name];
 if(!table(L,i)||lua_topointer(L,i)!=c.table||!lua_getmetatable(L,i)||lua_topointer(L,-1)!=c.meta)return false;
 field(L,-1,"__call");return c.call&&function(L,-1)==c.call;
}

bool matchAINativeValueMetamethods(Room&r,lua_State*L,QString*why){
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
      &&aiNativeMetamethodProxy(L,-1,it.key(),m.key(),search)&&search==m->search;}
   else ok=m->direct.nonempty?m->direct.fn&&function(L,-1)==m->direct.fn:lua_isnil(L,-1);
   if(!ok&&it.key()=="CardUseStruct"&&m.key()=="__gc"&&s->finalizerPhase==Seal::Finished
     &&s->finalizerMeta==it->identity&&function(L,-1)==s->finalizerWritten
     &&hasAIBootstrapMetadataContinuity(r,L))ok=true;
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
bool matchAINativeStringMetatable(Room&r,lua_State*L){
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
bool matchAINativeClassSetter(Room&r,lua_State*L,const QString&name,const QString&key){
 auto*s=current(r,L);if(!s||!AILuaInspect::capacity(L,64))return false;
 auto it=s->classes.constFind(name);if(it==s->classes.cend()||!it->valid||!it->newindex||!it->setters.value(key).fn)return false;
 Stack z(L);field(L,LUA_REGISTRYINDEX,"SWIG");if(!plain(L,-1))return false;field(L,-1,name.toUtf8().constData());
 const int meta=lua_absindex(L,-1);if(!plain(L,meta)||lua_topointer(L,meta)!=it->identity)return false;
 field(L,meta,"__newindex");if(function(L,-1)!=it->newindex)return false;lua_pop(L,1);
 field(L,meta,".set");return sameMethod(L,-1,it->setters,key);
}

bool matchAINativeLeafFrame(Room&r,lua_State*L,const QString&path){
 auto*s=current(r,L);if(!s||isLua(L->ci))return false;
 const auto expected=s->functions.value(path,nullptr);if(!expected)return false;
 const auto*v=s2v(L->ci->func.p);
 const auto actual=ttislcf(v)?fvalue(v):(ttisCclosure(v)&&clCvalue(v)->nupvalues==0?clCvalue(v)->f:nullptr);
 return actual==expected;
}

bool matchAINativeClassLeafFrame(Room&r,lua_State*L,const QString&name,const QString&method){
 auto*s=current(r,L);if(!s||isLua(L->ci))return false;
 const auto cls=s->classes.constFind(name);if(cls==s->classes.cend()||!cls->valid)return false;
 const auto expected=cls->functions.value(method).fn;if(!expected)return false;
 const auto*v=s2v(L->ci->func.p);
 const auto actual=ttislcf(v)?fvalue(v):(ttisCclosure(v)&&clCvalue(v)->nupvalues==0?clCvalue(v)->f:nullptr);
 return actual==expected;
}

// ---- natural-filter ----
#include "ai-strategy/anime.h"
// Independent finite family modules. No runtime callback/plugin registration.
bool certifyAIPoxiFilterBody(Room&,lua_State*,int,const AINaturalFilterProof&,QString*);
bool certifyAIXufaFilterBody(Room&,lua_State*,int,const AINaturalFilterProof&,QString*);
#include <QScopeGuard>
namespace {
using namespace AILuaInspect;
struct NaturalFilterStack {lua_State*L;int top;explicit NaturalFilterStack(lua_State*s):L(s),top(lua_gettop(s)){}~NaturalFilterStack(){lua_settop(L,top);}};
bool naturalFilterPlain(lua_State*L,int i){auto*t=table(L,i);return t&&!t->metatable;}
struct NaturalFilterSource {const char*path;const char*sha;};
const NaturalFilterSource sources[]={
 {"lua/ai/doudizhu-ai.lua","ffcf7174fe43e481d48c08d3482580a3a049915078e9f13756ace23e711aea2b"},
 {"lua/ai/smart-ai.lua","f92033319db3c6aac3504b4b6ddccf5edb4403fb120679b303dedfe673a3efe5"},
 {"lua/ai/AIgeneral-ai.lua","c4b0ee3cbbde098ac3a9e8cad6563fe63ace06e7816cb95dcadf0c5ce430e767"},
 {"lua/ai/wisdom-ai.lua","77e7fb67978c5e4f2bc31ca343329fe79866de92ec5acf71fa71f8ce12fba19b"},
 {"lua/ai/du-ai.lua","b0635b44fb918ed716a07f49928d4ec55d773062300a69797c0ced7b3db0c205"},
 {"lua/ai/yitian-ai.lua","88ce495f28c0a4622f99386cfca10063288ff15559c635e04476b384868ec126"},
 {"lua/ai/original-hegemony/heg-common-ai.lua","9ad128880b7876a95d022551533a8168ef924173a29d704af837cd5edb98ff7f"},
 {"lua/ai/chat-ai.lua","29e507189bc261e46ec9ab0fa327781833ccf9c3e7962521eddc471d478ba98e"},
 {"lua/utilities.lua","b76cdc44c0fec46f1e10d918c6e0676c5e301ed92cd5b0e35da882606a7d4d3a"},
};
struct Graph {
 Room&r;lua_State*L;QString failure;int sgs=0,self=0,actor=0,data=0;
 explicit Graph(Room&room,lua_State*state):r(room),L(state){}
 bool no(const QString&text){if(failure.isEmpty())failure=text;return false;}
 bool native(int index,const QString&path){return matchAINativeBinding(r,L,index,path)||no("native binding: "+path);}
 bool globalNative(const char*key){NaturalFilterStack s(L);globals(L);field(L,-1,key);return native(-1,QString::fromLatin1(key));}
 bool memberNative(const char*space,const char*key){NaturalFilterStack s(L);globals(L);field(L,-1,space);if(!naturalFilterPlain(L,-1))return no("library table metamethod");field(L,-1,key);return native(-1,QString::fromLatin1(space)+'.'+key);}
 bool env(int index,const char*excluded=nullptr){
  index=lua_absindex(L,index);if(!lua_isfunction(L,index)||lua_iscfunction(L,index))return no("Lua closure required");
  for(int u=1;;++u){const char*name=lua_getupvalue(L,index,u);if(!name)break;
   bool good=false;
   if(!std::strcmp(name,"_ENV")){globals(L);good=lua_rawequal(L,-1,-2);lua_pop(L,1);}
   else if(excluded&&!std::strcmp(name,excluded))good=true; // proven never read under public guard
   lua_pop(L,1);if(!good)return no("unexpected reached closure upvalue");
  }return true;
 }
 bool code(int index,const char*path,int first,int last,const char*excluded=nullptr){return matchAIPinnedFunction(r,L,index,QString::fromLatin1(path),first,last)&&env(index,excluded)||no(QString("source/binding mismatch: ")+path+':'+QString::number(first));}
 bool sgsFunction(const char*name,const char*path,int first,int last){NaturalFilterStack s(L);field(L,sgs,name);return code(-1,path,first,last);}
 // Resolve only native table __index chains. No Lua lookup or __index function
 // is executed, including middleclass instance dictionary/super tables.
 bool lookup(int object,const char*key){
  object=lua_absindex(L,object);QSet<const void*>seen;
  for(int n=0;n<16;++n){if(!table(L,object))return no("self lookup is not a table");
   if(seen.contains(lua_topointer(L,object)))return no("cyclic __index table");seen.insert(lua_topointer(L,object));
   field(L,object,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);
   if(!lua_getmetatable(L,object))return no(QString("missing member: ")+key);
   if(!naturalFilterPlain(L,-1))return no("metatable has a metatable");field(L,-1,"__index");
   if(!table(L,-1))return no("function/absent __index outside finite instance lookup");object=lua_absindex(L,-1);
  }return no("instance lookup budget");
 }
 bool objectMethods(int index,const char*base,const QStringList&methods){return matchAINativeObjectMethods(r,L,index,QString::fromLatin1(base),methods)||no(QString("native object/method mismatch: ")+base);}
 bool selfRoom(){NaturalFilterStack s(L);if(!lookup(self,"room"))return false;return objectMethods(-1,"Room",{"getAlivePlayers","writeToConsole","outputEventStack"});}
 bool callbackRoot(int index){
  index=lua_absindex(L,index);
  if(!matchAIPinnedFunction(r,L,index,"lua/ai/smart-ai.lua",344,375))return no("actual LuaAI callback code mismatch");
  int captured=0;
  for(int u=1;;++u){const char*name=lua_getupvalue(L,index,u);if(!name)break;
   if(!std::strcmp(name,"self")){if(captured||!table(L,-1))return no("callback self invalid");self=lua_absindex(L,-1);captured=1;}
   else if(!std::strcmp(name,"_ENV")){globals(L);const bool same=lua_rawequal(L,-1,-2);lua_pop(L,2);if(!same)return no("callback environment replaced");}
   else return no("callback unknown capture");
  }
  if(!captured)return no("callback self absent");
  {NaturalFilterStack s(L);if(!pushAINaturalFilterSelf(r,L,4)||!lua_rawequal(L,self,-1))return no("callback self differs from current native observer");}
  {NaturalFilterStack s(L);if(!lookup(self,"filterEvent")||!code(-1,"lua/ai/smart-ai.lua",2477,2867,"getCardIntention"))return false;}
  return selfRoom();
 }
 bool primitiveMetatables(){
  for(int k:{LUA_TNIL,LUA_TBOOLEAN,LUA_TNUMBER,LUA_TFUNCTION})
   if(primitiveMetatable(L,k))return no("primitive metamethod capability");
  return matchAINativeStringMetatable(r,L)
      ||no("string bootstrap metamethod bindings changed");
 }
 bool libraries(){
  for(const char*k:{"type","pcall","pairs","ipairs","tostring"})if(!globalNative(k))return false;
  for(const char*k:{"find","sub"})if(!memberNative("string",k))return false;
  for(const char*k:{"insert","concat"})if(!memberNative("table",k))return false;
  if(!memberNative("math","random"))return false;
  NaturalFilterStack s(L);globals(L);field(L,-1,"string");if(!naturalFilterPlain(L,-1))return no("string library indirect");const int strings=lua_absindex(L,-1);
  for(const auto&row:{QPair<const char*,QPair<int,int>>{"split",{77,88}}, {"contains",{91,93}}, {"startsWith",{164,168}}}){
   field(L,strings,row.first);
   if(!std::strcmp(row.first,"contains")) {
    // Installed addFunction keeps Lua pattern semantics; utilities uses plain
    // search. Both source bodies close the same native string.find effect graph.
    if(!(matchAIPinnedFunction(r,L,-1,"lua/utilities.lua",91,93)
       ||matchAIPinnedFunction(r,L,-1,"extensions/addFunction.lua",1557,1559))
       ||!env(-1))return no("string.contains source differs");
   } else if(!code(-1,"lua/utilities.lua",row.second.first,row.second.second))return false;
   lua_pop(L,1);
  }return primitiveMetatables();
 }
 bool qlist(){
  NaturalFilterStack s(L);field(L,sgs,"qlist");const int f=lua_absindex(L,-1);
  if(!matchAIPinnedFunction(r,L,f,"lua/utilities.lua",19,35))return no("qlist source");
  for(int u=1;;++u){const char*name=lua_getupvalue(L,f,u);if(!name)break;bool good=false;
   if(!std::strcmp(name,"_ENV")){globals(L);good=lua_rawequal(L,-1,-2);lua_pop(L,1);}
   else if(!std::strcmp(name,"qlist_userdata_iterator"))good=code(-1,"lua/utilities.lua",7,11);
   else if(!std::strcmp(name,"qlist_table_iterator"))good=code(-1,"lua/utilities.lua",13,17);
   lua_pop(L,1);if(!good)return no("qlist iterator capture");
  }
  return matchAINativeClassMethod(r,L,"SPlayerList","length")
      &&matchAINativeClassMethod(r,L,"SPlayerList","at")||no("SPlayerList iterator native methods");
 }
 bool namespaceRead(const char*key){
  NaturalFilterStack s(L);field(L,sgs,key);if(!lua_isnil(L,-1))return true;
  return matchAINativeNamespaceRead(r,L,"sgs",QString::fromLatin1(key))||no(QString("unproved namespace nil lookup: ")+key);
 }
 bool prefix(){
  for(const char*k:{"aiHandCardVisible","ai_humanized","qlist_cache_gen"})if(!namespaceRead(k))return false;
  if(!sgsFunction("invalidate_qlist_cache","lua/ai/smart-ai.lua",433,441)
     ||!sgsFunction("flushDeferredDeleteCards","lua/ai/smart-ai.lua",5481,5486))return false;
  for(const char*k:{"qlist_cache","ai_connect_cache","ai_active_recommends_cache","ai_crossbow_cache","ai_slash_query_cache","qlist_cache_gen","_deferred_delete_cards"})
   if(!matchAINativeNamespaceWrite(r,L,"sgs",QString::fromLatin1(k)))return no(QString("namespace setter: ")+k);
  {NaturalFilterStack s(L);field(L,sgs,"qlist_cache_gen");if(!lua_isnil(L,-1)&&(lua_type(L,-1)!=LUA_TNUMBER||!std::isfinite(lua_tonumber(L,-1))))return no("cache generation nonnumeric");}
  {NaturalFilterStack s(L);field(L,sgs,"filterData");if(!naturalFilterPlain(L,-1))return no("filterData write metamethod/absence");}
  {NaturalFilterStack s(L);field(L,sgs,"_deferred_delete_cards");if(!naturalFilterPlain(L,-1))return no("delete queue nonplain");const int list=lua_absindex(L,-1);
   bool end=false;for(int i=1;i<=4096;++i){rawGeti(L,list,i);if(lua_isnil(L,-1)){end=true;break;}
    if(!objectMethods(-1,"Card",{"deleteLater"}))return false;lua_pop(L,1);
   }if(!end)return no("delete queue budget");
  }
  // Both diagnostic outcomes remain in the union, regardless of the current
  // private aiHandCardVisible/turncount branch. No read-outcome-based pruning.
  {NaturalFilterStack s(L);field(L,sgs,"turncount");if(lua_type(L,-1)!=LUA_TNUMBER||!std::isfinite(lua_tonumber(L,-1)))return no("turncount nonnumeric");}
  if(!memberNative("io","open")||!matchAINativeFileMethods(r,L,{"write","close"}))return no("diagnostic file native methods");
  return objectMethods(data,"QVariant",{"toString"})
      &&objectMethods(actor,"ServerPlayer",{"getLogName"})
      &&matchAINativeDerivedMethods(r,L,"ServerPlayer",{"getLogName","getState","speak"})
      ||no("event/player native leaves");
 }
 bool enums(){
  for(const char*k:{"AskForPeaches","AskForRetrial","BeforeGameOverJudge","CardEffect","CardsMoveOneTime","ChoiceMade","DamageInflicted","Death","GameReady","HpRecover","PreCardUsed","Revived","RoundStart","ShowCards","StartJudge","TargetSpecified"}){
   NaturalFilterStack s(L);field(L,sgs,k);if(!matchAINativeConstant(r,L,-1,QStringLiteral("sgs.")+k))return no(QString("event enum replaced: ")+k);
  }return true;
 }
 bool chat(){
  NaturalFilterStack s(L);globals(L);const int g=lua_absindex(L,-1);field(L,g,"AIChat");if(!code(-1,"lua/ai/chat-ai.lua",3,6))return false;lua_pop(L,1);
  field(L,g,"global_delay");if(lua_type(L,-1)!=LUA_TNUMBER||!std::isfinite(lua_tonumber(L,-1)))return no("chat delay nonnumeric");
  return qlist(); // speak exact binding's native QString fails structured parser before reentry
 }
 bool faqiCallback(int f){
  f=lua_absindex(L,f);
  struct Row{const char*path;int first,last;const char*unused;};
  const Row rows[]={{"lua/ai/AIgeneral-ai.lua",26,45,nullptr},{"lua/ai/wisdom-ai.lua",392,401,nullptr},
   {"lua/ai/du-ai.lua",59,64,nullptr},{"lua/ai/du-ai.lua",582,604,nullptr},{"lua/ai/yitian-ai.lua",1037,1042,nullptr},
   {"lua/ai/original-hegemony/heg-common-ai.lua",257,268,"public_card_lack"},{"lua/ai/chat-ai.lua",528,533,nullptr}};
  for(const auto&row:rows)if(matchAIPinnedFunction(r,L,f,row.path,row.first,row.last)){
   if(!env(f,row.unused))return false;return row.first==528?chat():true;
  }return no("unknown ChoiceMade callable sibling");
 }
 bool eventRegistry(int event,bool allowFaqi){
  NaturalFilterStack s(L);field(L,sgs,"ai_event_callback");if(!naturalFilterPlain(L,-1))return no("event callback registry metamethod");
  rawGeti(L,-1,event);
  if(!lua_istable(L,-1))return true; // Source type guard skips every non-table.
  if(!naturalFilterPlain(L,-1))return no("event row metatable");
  const int row=lua_absindex(L,-1);lua_pushnil(L);int size=0;
  while(next(L,row)){if(++size>4096)return no("event row budget");
   if(lua_isfunction(L,-1)&&(!allowFaqi||!faqiCallback(-1)))return no("unproved event-row callable sibling");
   lua_pop(L,1);
  }return true;
 }
 bool faqiRegistry(){
  NaturalFilterStack s(L);if(!eventRegistry(int(ChoiceMade),true))return false;
  field(L,sgs,"ai_choicemade_filter");if(!naturalFilterPlain(L,-1))return no("choice filter registry metamethod");
  field(L,-1,"AGChosen");if(lua_istable(L,-1)){if(!naturalFilterPlain(L,-1))return no("AGChosen table metatable");field(L,-1,"faqi");}
  // Fixed source has no AGChosen.faqi callable. Do not infer absence: inspect
  // the current complete raw table, and refuse any added function.
  if(lua_isfunction(L,-1))return no("uncertified dynamic AGChosen callable");return true;
 }
};
}
bool registerAINaturalFilterSources(Room&r,lua_State*L,QString*reason){
 for(const auto&s:sources)if(!registerAIPinnedSource(r,L,s.path,QByteArray(s.sha),reason))return false;
 return true;
}
bool certifyAINaturalFilterGraph(Room&r,lua_State*L,int nargs,QString*reason){
 if(reason)reason->clear();AINaturalFilterProof proof;
 if(!readAINaturalFilterEntry(r,L,nargs,&proof)){if(reason)*reason="no authenticated native filterEvent frame";return false;}
 // The fixed filter's captured getCardIntention is called only in these three
 // exact switch arms. No family below admits them; make that source premise
 // explicit before ignoring the otherwise-unreached capture in callbackRoot.
 if(proof.event==int(TargetSpecified)||proof.event==int(DamageInflicted)||proof.event==int(HpRecover)){
  if(reason)*reason="natural filter intention-helper arm outside finite event domain";return false;
 }
 // The trusted bootstrap/central observer prepares this capacity. Admission
 // never grows the Lua stack or invokes a Lua allocator/finalizer.
 if(!AILuaInspect::capacity(L,4096)){if(reason)*reason="preallocated native inspection stack unavailable";return false;}
 NaturalFilterStack stack(L);Graph g(r,L);const int callback=lua_gettop(L)-nargs;
 g.actor=callback+3;g.data=callback+4;
 globals(L);if(!naturalFilterPlain(L,-1)){if(reason)*reason="global table metamethod";return false;}
 field(L,-1,"sgs");g.sgs=lua_absindex(L,-1);
 bool accepted=table(L,g.sgs)&&g.callbackRoot(callback)&&g.libraries()&&g.prefix()&&g.enums()
     &&aiFocusContinuityNoop(r,L,nargs);
 if(accepted){
  if(proof.event==int(ChoiceMade)&&proof.faqiAGFormat)accepted=g.faqiRegistry();
  else switch(proof.event){
  // These enum values have no case in the fixed filterEvent switch. All
  // callable event-row siblings must still be proved; none are silently skipped.
  case EventSkillWillInvoke:case EventSkillPay:case EventSkillTargetConfirming:
  case EventSkillInvoking:case EventSkillEffect:case EventSkillEffectTarget:
  case EventSkillEffectFinished:case CardAsked:case MarkChange:case MarkChanged:
   accepted=g.eventRegistry(proof.event,false);break;
  default:{
   // Each delegate is a complete body+registry proof, not a private branch
   // trace or a saved-plan replacement. Missing services remain fail-closed.
   QString poxi,xufa;
   // Poxi's body delegate does not own arbitrary sibling registrations. It
   // receives a row already proved callback-free; a populated row needs a
   // future complete sibling validator, never an observed-branch exemption.
   const QString earlier=g.failure;
   accepted=g.eventRegistry(proof.event,false)
         &&certifyAIPoxiFilterBody(r,L,g.self,proof,&poxi);
   if(!accepted){g.failure=earlier;
    // Xufa's finite body validator explicitly owns its full move/choice row.
    accepted=certifyAIXufaFilterBody(r,L,g.self,proof,&xufa);
   }
   if(!accepted)g.no("family body/registry union unavailable: "+poxi+"; "+xufa);
   break;
  }}
 }
 // Async finalizer/debug-hook protection is a mandatory premise checked by
 // the sole native continuity entry observer before calling this module. This
 // return certifies only the finite graph; it is not independently a lease.
 if(!accepted&&reason)*reason=g.failure.isEmpty()?QStringLiteral("natural filter graph prerequisite unavailable"):g.failure;
 return accepted;
}
