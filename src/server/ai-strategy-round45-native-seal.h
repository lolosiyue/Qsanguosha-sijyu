#ifndef AI_STRATEGY_ROUND45_NATIVE_SEAL_H
#define AI_STRATEGY_ROUND45_NATIVE_SEAL_H
#include <QString>
#include <QStringList>
class Room;
struct lua_State;
// Capture immediately after game LuaRuntime::initialize, before config.lua or
// any authored package. Native-only and one-shot per generation.
void captureAIRound45NativeBindings(Room &,lua_State *);
void clearAIRound45NativeBindings(Room &);
// Code/binding identity only. Callers supply independently audited finite leaf
// names/effects; these APIs do not classify arbitrary native methods as safe.
bool matchAIRound45NativeBinding(Room &,lua_State *,int valueIndex,const QString &path);
bool matchAIRound45NativeConstant(Room &,lua_State *,int valueIndex,const QString &path);
bool matchAIRound45NativeClassMethod(Room &,lua_State *,const QString &className,const QString &method);
bool matchAIRound45NativeDerivedMethods(Room &,lua_State *,const QString &baseClass,const QStringList &methods);
bool matchAIRound45NativeNamespaceProperty(Room &,lua_State *,const QString &space,const QString &property);
bool matchAIRound45NativeClassProperty(Room &,lua_State *,const QString &className,const QString &property);
bool matchAIRound45NativeObjectMethods(Room &,lua_State *,int objectIndex,const QString &baseClass,const QStringList &methods);
bool matchAIRound45NativeNamespaceWrite(Room &,lua_State *,const QString &space,const QString &key);
bool matchAIRound45NativeFileMethods(Room &,lua_State *,const QStringList &methods);
// Current exact SWIG namespace __newindex frame and nonsetter raw-write route.
// No stack growth/push, userdata conversion, Lua calls, or authored values.
bool matchAIRound56NativeNamespaceWriteFrame(Room &,lua_State *,const void *tableIdentity,
                                           const QString &space,const char *key);
// Exact nil lookup, including absence in BOTH SWIG .get and .fn tables.
bool matchAIRound45NativeNamespaceRead(Room &,lua_State *,const QString &space,const QString &key);
// R47 fixed scratch factories only; identity captured before configured scripts.
bool matchAIRound47NativeScratchConstructor(Room &,lua_State *,int,const QString &);
bool matchAIRound47NativeValueMetamethods(Room &,lua_State *,QString *reason=nullptr);
// Trusted bootstrap string metatable identity and every exact entry. This is
// binding evidence only: callers must also prove operand metamethod effects.
bool matchAIRound47NativeStringMetatable(Room &,lua_State *);
// R56 native property setter identity, without invocation or value inspection.
bool matchAIRound56NativeClassSetter(Room &,lua_State *,const QString &className,const QString &property);
// Exact current native frame against pre-script captured no-upvalue leaf.
// No Lua stack changes or allocations.
bool matchAIRound56NativeLeafFrame(Room &,lua_State *,const QString &path);
// Exact current zero-upvalue native class-method frame, captured at birth.
// Binding identity only; caller proves the finite native effect/return typemap.
bool matchAIRound56NativeClassLeafFrame(Room&,lua_State*,const QString&className,const QString&method);
// One deterministic original-bootstrap native writer. Begin watches the actual
// native-birth CardUseStruct metatable; complete needs its observed __gc write.
bool beginAIRound56LianzhanFinalizerTransition(Room&,lua_State*);
bool completeAIRound56LianzhanFinalizerTransition(Room&,lua_State*);
#endif
