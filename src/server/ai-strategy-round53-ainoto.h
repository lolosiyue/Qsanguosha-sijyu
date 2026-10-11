#ifndef AI_STRATEGY_ROUND53_AINOTO_H
#define AI_STRATEGY_ROUND53_AINOTO_H
#include <QJsonObject>
class Room;class ServerPlayer;struct AIRequest;
// Constructor-only baseline, before any policy or restored property writer.
void createdAIRound53AiNoTo(Room&,ServerPlayer*);
void initializeAIRound53AiNoTo(Room&);
void beginAIRound53AiNoTo(Room&,const QString&);
void retireAIRound53AiNoTo(Room&) noexcept;
QJsonObject queryAIRound53AiNoTo(Room&,const AIRequest&,const ServerPlayer*,const QJsonObject&);
class AIRound53AiNoToPermit final {
 friend QJsonObject queryAIRound53AiNoTo(Room&,const AIRequest&,const ServerPlayer*,const QJsonObject&);
 const Room*room;const AIRequest*request;const ServerPlayer*owner;const ServerPlayer*target;bool value;
 AIRound53AiNoToPermit(const Room*r,const AIRequest*q,const ServerPlayer*o,const ServerPlayer*t,bool v):room(r),request(q),owner(o),target(t),value(v){}
public:
 bool matches(const Room&r,const AIRequest&q,const ServerPlayer*o,const ServerPlayer*t,bool v)const{return room==&r&&request==&q&&owner==o&&target==t&&value==v;}
};
// R44 existing certified finite mutation machinery; never restamps request/tickets.
bool writeAIRound53AiNoToProperty(Room&,const AIRequest&,ServerPlayer*,ServerPlayer*,bool,const AIRound53AiNoToPermit&,QString*);
// Native diagnostic only: consumes existing constructor provenance; no property read,
// state creation, witness issuance, or Lua/world-view export.
bool hasAIRound56InitialAiNoToWitness(const Room&,const ServerPlayer*) noexcept;
quint64 observedAIRound56InitialAiNoToReads(const Room&) noexcept;
void recordAIRound56ChoulveTargetWrite(Room&) noexcept;
void recordAIRound56ChoulveNativeAcceptance(Room&) noexcept;
quint64 observedAIRound56ChoulveTargetWrites(const Room&) noexcept;
quint64 observedAIRound56ChoulveNativeAcceptances(const Room&) noexcept;
#endif
