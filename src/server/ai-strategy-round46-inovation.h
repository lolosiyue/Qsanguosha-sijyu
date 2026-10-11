#pragma once
#include <QJsonObject>
#include <QString>
class Room; class ServerPlayer; struct AIRequest;
QJsonObject queryAIRound46Inovation(Room &,const AIRequest &,ServerPlayer *,const QString &,const QString &);
// Capability constructed only after exact current Kaiqi question/sequence validation.
class AIRound46KaiqiFlagPermit final {
public:
 bool matches(const Room&r,const AIRequest&q,const ServerPlayer*p,const ServerPlayer*t)const{return room==&r&&request==&q&&owner==p&&target==t;}
 AIRound46KaiqiFlagPermit(const AIRound46KaiqiFlagPermit&)=delete;
 AIRound46KaiqiFlagPermit&operator=(const AIRound46KaiqiFlagPermit&)=delete;
private:
 friend QJsonObject queryAIRound46Inovation(Room &,const AIRequest &,ServerPlayer *,const QString &,const QString &);
 AIRound46KaiqiFlagPermit(const Room&r,const AIRequest&q,const ServerPlayer*p,const ServerPlayer*t):room(&r),request(&q),owner(p),target(t){}
 const Room*room;const AIRequest*request;const ServerPlayer*owner;const ServerPlayer*target;
};
bool writeAIRound46KaiqiFlag(Room &,const AIRequest &,ServerPlayer *,ServerPlayer *,const QString &,const AIRound46KaiqiFlagPermit &,QString *error=nullptr);
