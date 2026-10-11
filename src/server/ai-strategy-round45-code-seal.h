#ifndef AI_STRATEGY_ROUND45_CODE_SEAL_H
#define AI_STRATEGY_ROUND45_CODE_SEAL_H
#include <QString>
#include <QList>
class Room;
struct lua_State;
// Native-only trusted bootstrap API. fixedSha must be a compiled/audited
// constant; callers must never derive it from the currently installed file.
// Compilation is for independent metadata only: the chunk is never executed.
bool registerAIRound45PinnedSource(Room &, lua_State *, const QString &relativePath,
                                  const QByteArray &fixedSha, QString *reason=nullptr);
// No Lua allocation, function execution, _ENV lookup or userdata inspection.
// This proves complete executable Proto identity, NOT upvalue binding/effects.
bool matchAIRound45PinnedFunction(Room &, lua_State *, int functionIndex,
                                 const QString &relativePath, int firstLine,
                                 int lastLine, QString *reason=nullptr);
// Same complete-code check, additionally restricted to descendants of the
// listed independently audited outer function anchors. No line-range admit.
bool matchAIRound45PinnedFunctionFamily(Room &, lua_State *, int functionIndex,
                                       const QString &relativePath, const QList<int> &outerFirstLines,
                                       QString *reason=nullptr);
// Metadata-only source-time witness. borrowedClosure MUST come directly from
// the current live CallInfo; never pass a retained pointer or authored handle.
// The trusted line is looked up in the independently compiled pinned source,
// not current debug metadata. This proves code/PC only, not effects or _ENV.
bool matchAIRound56PinnedInstruction(Room &,lua_State *,const void *borrowedClosure,
                                    int pc,const QString &relativePath,int firstLine,
                                    int lastLine,int trustedSourceLine);
void clearAIRound45PinnedSources(Room &);
// Only the synchronous observer's CURRENT LIVE closure and explicit VM PC,
// or an actual suspended Lua caller PC, may be supplied. No pointer cache.
bool matchAIRound56PinnedLiveInstruction(Room &,lua_State *,const void *borrowedClosure,
 int pc,const QString &path,const QList<int> &families,int requiredOpcode=-1);
#endif
