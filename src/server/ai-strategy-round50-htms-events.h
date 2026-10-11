#ifndef AI_STRATEGY_ROUND50_HTMS_EVENTS_H
#define AI_STRATEGY_ROUND50_HTMS_EVENTS_H
#include <QJsonObject>
#include <functional>
#include <memory>
class Room; class ServerPlayer; class Card; class QVariant; class QString; class LuaTriggerSkillV2;
struct lua_State; struct SkillContext; struct AIRequest; struct AIWorldView; struct AIEventView;
// Every V2 cost/effect, including unrecognised callbacks, shadows the outer scope.
class AIRound50HTMSFrame final {
public:
 AIRound50HTMSFrame(Room&,const LuaTriggerSkillV2*,ServerPlayer*,int,const SkillContext&,bool);
 ~AIRound50HTMSFrame();
 AIRound50HTMSFrame(const AIRound50HTMSFrame&)=delete;
private:struct Data;std::unique_ptr<Data>d;
};
bool registerAIRound50HTMSSource(Room&,lua_State*);
void clearAIRound50HTMSWitnesses(Room&);
void projectAIRound50HTMSEvents(AIRequest&,Room&,ServerPlayer*,const std::function<QJsonObject(const Card*)>&);
void projectAIRound50HTMSIntention(AIWorldView&,Room&,ServerPlayer*,const AIEventView&,const std::function<QJsonObject(const Card*)>&);
// Exact native resolveDamage writes, with all other tag writes/removals revoking
// the witness. This is a synchronous current-damage stack, not event history.
class AIRound50DamageScope final {
public:explicit AIRound50DamageScope(Room&);~AIRound50DamageScope();
private:struct Data;std::unique_ptr<Data>d;
};
class AIRound50DamageTagWrite final {
public:AIRound50DamageTagWrite(Room&,const QVariant&,int writeKind);~AIRound50DamageTagWrite();
private:struct Data;std::unique_ptr<Data>d;
};
void observeAIRound50DamageTag(Room&,const QString&,const QVariant*value);
#endif
