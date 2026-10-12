#ifndef AI_STRATEGY_INOVATION_AI_H
#define AI_STRATEGY_INOVATION_AI_H

#include <QString>
#include <QJsonObject>
#include <QVariant>

class AI;
struct AIRequest;
struct AIWorldView;
class Card;
struct CardsMoveOneTimeStruct;
struct lua_State;
class LuaRuntime;
class Player;
class QJsonObject;
class Room;
class ServerPlayer;

int aiJizhanshizPrototype(const AIRequest &, const ServerPlayer *, const QString &, const QString &);
bool emitAIJizhanshizWarning(Room &, const AIRequest &, const ServerPlayer *, const QString &, const QString &);

void projectAIGoutong(AIRequest &,Room &,ServerPlayer *,const QVariant &);
QJsonObject writeAIGoutongMove(Room &,const AIRequest &,ServerPlayer *);
QJsonObject readAIGoutongMove(Room &,const AIRequest &,ServerPlayer *);
void clearAIGoutong(Room &);
// Game-VM-only exact native copy; never registered as a Lua API.
bool pushAIGoutongMove(lua_State *,const CardsMoveOneTimeStruct &);
bool readAIGoutongEndpoints(lua_State *,int,Player *&,Player *&);
void clearAIGoutongBindings(LuaRuntime &);

QJsonObject queryAIInovationKaiqi(Room &,const AIRequest &,ServerPlayer *,const QString &,const QString &);
// Capability constructed only after exact current Kaiqi question/sequence validation.
class AIKaiqiFlagPermit final {
public:
 bool matches(const Room&r,const AIRequest&q,const ServerPlayer*p,const ServerPlayer*t)const{return room==&r&&request==&q&&owner==p&&target==t;}
 AIKaiqiFlagPermit(const AIKaiqiFlagPermit&)=delete;
 AIKaiqiFlagPermit&operator=(const AIKaiqiFlagPermit&)=delete;
private:
 friend QJsonObject queryAIInovationKaiqi(Room &,const AIRequest &,ServerPlayer *,const QString &,const QString &);
 AIKaiqiFlagPermit(const Room&r,const AIRequest&q,const ServerPlayer*p,const ServerPlayer*t):room(&r),request(&q),owner(p),target(t){}
 const Room*room;const AIRequest*request;const ServerPlayer*owner;const ServerPlayer*target;
};
bool writeAIKaiqiFlag(Room &,const AIRequest &,ServerPlayer *,ServerPlayer *,const QString &,const AIKaiqiFlagPermit &,QString *error=nullptr);

// Installed/captured only at native bootstrap and successful real CloneAI.
void initializeAIInovationProviders(Room&);
void captureAIInovationProviders(Room&,ServerPlayer*,AI*);
void projectAIInovationProviders(AIWorldView&,Room&,const ServerPlayer*);
// Generated SWIG implementation: native pointer conversion only; no invocation.
bool aiInovationPlayerMatches(lua_State*,int,const ServerPlayer*);
bool aiInovationRoomMatches(lua_State*,int,const Room*);
bool aiInovationProviderKnown(Room&,const ServerPlayer*,const QString&);
bool aiInovationPhaseConstruction(const AIRequest&,const ServerPlayer*,const QJsonObject&);
// Success pushes one exact native-owner self table, no effects or invocation.
bool pushAIActualAISelf(Room&,const ServerPlayer*,lua_State*);

// R53 finite Yuri Room tag source prefix/guarded current read.
void invalidateAIGoodatwep(Room&) noexcept;
void enterAIGoodatwep(Room&,const QString&viewer);
QJsonObject queryAIGoodatwep(Room&,const AIRequest&,ServerPlayer*,const QJsonObject&);
class AIGoodatwepPermit final {
public:
 bool matches(const Room&r,const AIRequest&q,const ServerPlayer*p)const{return room==&r&&request==&q&&owner==p;}
 AIGoodatwepPermit(const AIGoodatwepPermit&)=delete;
private:
 friend QJsonObject queryAIGoodatwep(Room&,const AIRequest&,ServerPlayer*,const QJsonObject&);
 AIGoodatwepPermit(const Room&r,const AIRequest&q,const ServerPlayer*p):room(&r),request(&q),owner(p){}
 const Room*room;const AIRequest*request;const ServerPlayer*owner;
};
bool writeAIGoodatwepTag(Room&,const AIRequest&,ServerPlayer*,const QString&,const AIGoodatwepPermit&,QString*error=nullptr);

QJsonObject queryAIYohane(Room&,const AIRequest&,ServerPlayer*,const QJsonObject&);
class AIYohanePermit final {
public:
 bool matches(const Room&r,const AIRequest&q,const ServerPlayer*p,const QString&k)const{return room==&r&&request==&q&&owner==p&&key==k;}
 AIYohanePermit(const AIYohanePermit&)=delete;
private:
 friend QJsonObject queryAIYohane(Room&,const AIRequest&,ServerPlayer*,const QJsonObject&);
 AIYohanePermit(const Room&r,const AIRequest&q,const ServerPlayer*p,const QString&k):room(&r),request(&q),owner(p),key(k){}
 const Room*room;const AIRequest*request;const ServerPlayer*owner;QString key;
};
bool writeAIYohaneTag(Room&,const AIRequest&,ServerPlayer*,const QString&,const QVariant&,bool,const AIYohanePermit&,QString*error=nullptr);

// Normal game-VM reset/shutdown only: release native leases before teardown.
void clearAIInovationState(Room &);
// Actual Room tag writes, equal-value writes, removals and whole-map restore.
// Unrelated queries/callbacks do not erase the true tag's owner provenance.
void observeAIXinyangTagWrite(Room &,const QString &);
void observeAIXinyangTagRestore(Room &);
void projectAIShoushiInput(AIRequest &,Room &,ServerPlayer *,const QVariant &);
bool aiShoushiConstruction(const AIRequest &,const ServerPlayer *,const QJsonObject &);
QJsonObject queryAIShoushi(Room &,const AIRequest &,ServerPlayer *,const QString &,const QString &);
QJsonObject queryAIXinyang(Room &,const AIRequest &,ServerPlayer *,const QString &,int);
// Stack-only admission, minted solely by the already-authorized query. It is
// never serialized, copyable, or creatable from the certificate setter itself.
class AIXinyangWritePermit final {
public:
 bool matches(const Room &r,const AIRequest &q,const ServerPlayer *p)const{return room==&r&&request==&q&&owner==p;}
 AIXinyangWritePermit(const AIXinyangWritePermit&)=delete;
 AIXinyangWritePermit&operator=(const AIXinyangWritePermit&)=delete;
private:
 friend QJsonObject queryAIXinyang(Room &,const AIRequest &,ServerPlayer *,const QString &,int);
 AIXinyangWritePermit(const Room&r,const AIRequest&q,const ServerPlayer*p):room(&r),request(&q),owner(p){}
 const Room*room;const AIRequest*request;const ServerPlayer*owner;
};
// Implemented inside the finite request certificate; only the authorized native
// query can construct its permit. No independently callable Lua mutation API.
bool writeAIXinyangTag(Room &,const AIRequest &,ServerPlayer *,int,const AIXinyangWritePermit &,QString *error=nullptr);

// Exact viewer's Akarin visibility only; not other observers' relationship rows.
void projectAIInovationWorld(AIWorldView &,Room &,const ServerPlayer *);
// Finite actual Kuisi Death question; exposes only the reached damage.from.
void projectAIInovationContext(AIRequest &,Room &,ServerPlayer *,const QVariant &);
int aiInovationPlayPrototype(const AIRequest &,const ServerPlayer *,const QString &,const QString &);
bool aiInovationMaterialConstruction(const AIRequest &,const ServerPlayer *,const QJsonObject &);

bool aiXiedouConstruction(const AIRequest &, const ServerPlayer *, const QJsonObject &);
// Sole production issuer: the real fresh Duel constructor in Xiedou.effectOnTarget.
// No isolated binding, face lookup, mutation authority or gameplay-state change.
void recordAIFreshXiedouDuel(Room &, const Card *);
// Sole issuer: current Xufa valuePlan's fresh Card::Parse product, before native
// admission callbacks. Shares the existing literal-flag mutation epoch only.
void recordAIFreshXufaValueDuel(Room &, const Card *);
void invalidateAIXiedouFlags(const Card *) noexcept;
// Only merge into an already lawfully disclosed actual-card descriptor.
QJsonObject readAIXiedouFlags(Room &, ServerPlayer *, const Card *);

#endif
