#ifndef AI_STRATEGY_ROUND50_EFFECTS_H
#define AI_STRATEGY_ROUND50_EFFECTS_H
#include <QJsonObject>
#include <QString>
class Room;class ServerPlayer;struct AIRequest;
QJsonObject queryAIRound50Effects(Room&,const AIRequest&,ServerPlayer*,const QJsonObject&);
class AIRound50EffectPermit final {
public:
 AIRound50EffectPermit(const AIRound50EffectPermit&)=delete;
 AIRound50EffectPermit&operator=(const AIRound50EffectPermit&)=delete;
private:
 friend QJsonObject queryAIRound50Effects(Room&,const AIRequest&,ServerPlayer*,const QJsonObject&);
 friend QJsonObject queryAIRound53Collateral(Room&,const AIRequest&,ServerPlayer*,const QJsonObject&);
 friend bool writeAIRound50Effect(Room&,const AIRequest&,ServerPlayer*,ServerPlayer*,const AIRound50EffectPermit&,QString*);
 AIRound50EffectPermit(const Room&r,const AIRequest&q,const ServerPlayer*p,const ServerPlayer*t,const QString&k,bool m,bool v):room(&r),request(&q),owner(p),target(t),key(k),mark(m),value(v){}
 const Room*room;const AIRequest*request;const ServerPlayer*owner;const ServerPlayer*target;
 QString key;bool mark,value;
};
bool writeAIRound50Effect(Room&,const AIRequest&,ServerPlayer*,ServerPlayer*,const AIRound50EffectPermit&,QString*error=nullptr);
#endif
