#ifndef AI_STRATEGY_ROUND40_CHOICE_CONTINUITY_H
#define AI_STRATEGY_ROUND40_CHOICE_CONTINUITY_H
#include <QJsonObject>
#include <QString>
class Room;class ServerPlayer;struct AIRequest;struct lua_TableObservation;
class AIChoiceContinuityInquiry {
public:
 AIChoiceContinuityInquiry(Room &,const QString &kind=QString(),ServerPlayer *viewer=nullptr,const QString &reason=QString());
 ~AIChoiceContinuityInquiry();
 AIChoiceContinuityInquiry(const AIChoiceContinuityInquiry&)=delete;
 AIChoiceContinuityInquiry&operator=(const AIChoiceContinuityInquiry&)=delete;
 Room *room;QString kind,reason;ServerPlayer *viewer;quint64 serial;
 QJsonObject inherited;
 AIChoiceContinuityInquiry *previous;
};
void invalidateAIChoiceContinuity(Room &);
void interruptAIChoiceContinuity(Room &);
void interruptAIChoiceContinuityAsync(Room &) noexcept;
void beginAIChoiceContinuityPolicy(Room &,const QString &viewer);
void observeAIChoiceContinuityEvent(Room &,const QString &viewer,const QString &actor);
void initializeAIChoiceContinuityRoots(Room &,bool isolated,bool valid,const QJsonObject &initialNil,quint64 xufaTransportStamp=0,quint64 xufaGenerationStamp=0);
void observeAIChoiceContinuityTable(Room &,int domain,bool isolated,const lua_TableObservation &) noexcept;
void observeAIChoiceContinuityDecision(Room &,const AIRequest &);
QJsonObject queryAIChoiceContinuity(Room &,ServerPlayer *,const AIRequest &,const QJsonObject &);
ServerPlayer*currentAIRound47XufaContinuityWriter(Room&) noexcept;
// Native-only monotonic reached-write identity. Zero means unknown; no values
// or epoch exposed to policy. A later same-owner write is a different source.
quint64 currentAIRound47XufaWriteSerial(Room&) noexcept;
void interruptAIChoiceContinuityExceptXufa(Room&);
void interruptAIRound47XufaContinuity(Room&);
#endif
