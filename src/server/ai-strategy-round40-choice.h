#ifndef AI_STRATEGY_ROUND40_CHOICE_H
#define AI_STRATEGY_ROUND40_CHOICE_H
#include <QJsonObject>
#include <QStringList>
class Room; class ServerPlayer; struct AIRequest; struct AIEventView;
void projectAIRound40OLGeneralContext(Room &,ServerPlayer *,const QStringList &,QJsonObject &);
void captureAIRound40OLJijiang(Room &);
void beginAIRound40OLJijiang(Room &,const QString &viewer);
void invalidateAIRound40OLJijiang(Room &);
QJsonObject aiRound40OLJijiangEvent(const AIEventView &);
QJsonObject queryAIRound40OLJijiang(Room &,const AIRequest &,ServerPlayer *,const QString &op,const QString &value=QString());
#endif
