#pragma once
#include <QJsonObject>
#include <QString>
#include <QVariant>
class Room; class ServerPlayer; struct AIRequest;

// R53 finite Yuri Room tag source prefix/guarded current read.
void invalidateAIRound53Goodatwep(Room&) noexcept;
void enterAIRound53Goodatwep(Room&,const QString&viewer);
QJsonObject queryAIRound53Goodatwep(Room&,const AIRequest&,ServerPlayer*,const QJsonObject&);
class AIRound53GoodatwepPermit final {
public:
 bool matches(const Room&r,const AIRequest&q,const ServerPlayer*p)const{return room==&r&&request==&q&&owner==p;}
 AIRound53GoodatwepPermit(const AIRound53GoodatwepPermit&)=delete;
private:
 friend QJsonObject queryAIRound53Goodatwep(Room&,const AIRequest&,ServerPlayer*,const QJsonObject&);
 AIRound53GoodatwepPermit(const Room&r,const AIRequest&q,const ServerPlayer*p):room(&r),request(&q),owner(p){}
 const Room*room;const AIRequest*request;const ServerPlayer*owner;
};
bool writeAIRound53GoodatwepTag(Room&,const AIRequest&,ServerPlayer*,const QString&,const AIRound53GoodatwepPermit&,QString*error=nullptr);


QJsonObject queryAIRound53Yohane(Room&,const AIRequest&,ServerPlayer*,const QJsonObject&);
class AIRound53YohanePermit final {
public:
 bool matches(const Room&r,const AIRequest&q,const ServerPlayer*p,const QString&k)const{return room==&r&&request==&q&&owner==p&&key==k;}
 AIRound53YohanePermit(const AIRound53YohanePermit&)=delete;
private:
 friend QJsonObject queryAIRound53Yohane(Room&,const AIRequest&,ServerPlayer*,const QJsonObject&);
 AIRound53YohanePermit(const Room&r,const AIRequest&q,const ServerPlayer*p,const QString&k):room(&r),request(&q),owner(p),key(k){}
 const Room*room;const AIRequest*request;const ServerPlayer*owner;QString key;
};
bool writeAIRound53YohaneTag(Room&,const AIRequest&,ServerPlayer*,const QString&,const QVariant&,bool,const AIRound53YohanePermit&,QString*error=nullptr);

// Normal game-VM reset/shutdown only: release native leases before teardown.
void clearAIRound45InovationState(Room &);
// Actual Room tag writes, equal-value writes, removals and whole-map restore.
// Unrelated queries/callbacks do not erase the true tag's owner provenance.
void observeAIRound45XinyangTagWrite(Room &,const QString &);
void observeAIRound45XinyangTagRestore(Room &);
void projectAIRound45ShoushiInput(AIRequest &,Room &,ServerPlayer *,const QVariant &);
bool aiRound45ShoushiConstruction(const AIRequest &,const ServerPlayer *,const QJsonObject &);
QJsonObject queryAIRound45Shoushi(Room &,const AIRequest &,ServerPlayer *,const QString &,const QString &);
QJsonObject queryAIRound45Xinyang(Room &,const AIRequest &,ServerPlayer *,const QString &,int);
// Stack-only admission, minted solely by the already-authorized query. It is
// never serialized, copyable, or creatable from the certificate setter itself.
class AIRound45XinyangWritePermit final {
public:
 bool matches(const Room &r,const AIRequest &q,const ServerPlayer *p)const{return room==&r&&request==&q&&owner==p;}
 AIRound45XinyangWritePermit(const AIRound45XinyangWritePermit&)=delete;
 AIRound45XinyangWritePermit&operator=(const AIRound45XinyangWritePermit&)=delete;
private:
 friend QJsonObject queryAIRound45Xinyang(Room &,const AIRequest &,ServerPlayer *,const QString &,int);
 AIRound45XinyangWritePermit(const Room&r,const AIRequest&q,const ServerPlayer*p):room(&r),request(&q),owner(p){}
 const Room*room;const AIRequest*request;const ServerPlayer*owner;
};
// Implemented inside the finite request certificate; only the authorized native
// query can construct its permit. No independently callable Lua mutation API.
bool writeAIRound45XinyangTag(Room &,const AIRequest &,ServerPlayer *,int,const AIRound45XinyangWritePermit &,QString *error=nullptr);
