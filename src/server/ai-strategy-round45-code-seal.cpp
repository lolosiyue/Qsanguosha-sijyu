#include "ai-strategy-round56-bootstrap-capture-metadata.h"
#include "ai-strategy-room-ledger.h"
#include "ai-strategy-round45-code-seal.h"
#include "room.h"
#include "room-runtime.h"
#include "lua-runtime.h"
#include "ai-runtime.h"
#include "ai-strategy-round45-lua-inspect.h"
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
class Seals final:public AIRoomLedger {
public:
 explicit Seals(Room &room):AIRoomLedger(room){setObjectName(name);}
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
bool registerAIRound45PinnedSource(Room &room,lua_State *L,const QString &path,
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
bool matchAIRound45PinnedFunction(Room &room,lua_State *L,int index,const QString &path,
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
bool matchAIRound45PinnedFunctionFamily(Room &room,lua_State *L,int index,const QString &path,
                                       const QList<int> &outers,QString *reason){
 // match checks the runtime before any reference is used.
 const Proto *p=L?getProto(L,index):nullptr;
 if(!p||!matchAIRound45PinnedFunction(room,L,index,path,p->linedefined,p->lastlinedefined,reason))return false;
 const auto *seals=get(room);const auto it=seals->sources.constFind(path);
 const auto ancestors=it->ancestors.value(key(p->linedefined,p->lastlinedefined));
 for(int first:outers)if(ancestors.contains(first)){if(reason)reason->clear();return true;}
 return refuse(reason,"function is outside the independently audited source family");
}
bool matchAIRound56PinnedInstruction(Room &room,lua_State *L,const void *borrowedClosure,
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
bool matchAIRound56PinnedLiveInstruction(Room&r,lua_State*L,const void*borrowed,int pc,
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
void clearAIRound45PinnedSources(Room &room){delete get(room);}

// A live caller-owned capture graph keeps this closure reachable throughout
// metadata inspection. signature allocates native bytes only, never Lua/GC.
bool matchAIRound56PinnedClosureMetadata(Room &room,lua_State *L,const void *live,
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

bool matchAIRound56PinnedAuxiliaryRoot(Room &room,lua_State *auxiliary,const QString &path) {
 auto *runtime=room.roomRuntime();auto *seals=get(room);
 if(!runtime||!seals||!auxiliary||runtime->ai().lua().rawState()!=auxiliary
   ||!runtime->ai().lua().isCurrentThreadOwner()||!runtime->lua().isCurrentThreadOwner()
   ||seals->state!=runtime->lua().rawState()||seals->generation!=runtime->lua().generation())return false;
 const auto it=seals->sources.constFind(path);if(it==seals->sources.cend())return false;
 const auto *value=AIRound45Inspect::value(auxiliary,-1);
 if(!ttisLclosure(value))return false;const Proto *actual=clLvalue(value)->p;
 const auto anchor=key(0,0);
 if(!actual||actual->linedefined||actual->lastlinedefined||it->ambiguous.contains(anchor)
   ||!it->bodies.contains(anchor))return false;
 QByteArray signatureBytes;
 try{return signature(actual,signatureBytes)&&signatureBytes==it->bodies.value(anchor);}
 catch(const std::bad_alloc &){return false;}
}

bool matchAIRound56PinnedAuxiliaryFunction(Room &room,lua_State *L,int index,
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
