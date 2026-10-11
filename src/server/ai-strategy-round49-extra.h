#ifndef AI_STRATEGY_ROUND49_EXTRA_H
#define AI_STRATEGY_ROUND49_EXTRA_H
#include <QJsonObject>
class Room; class ServerPlayer; struct AIRequest;
// Five source hypotheses only: no payment, playable conversion or new disclosure.
bool aiRound49ExtraConstruction(const AIRequest &,Room &,const ServerPlayer *,const QJsonObject &);
bool aiRound49ExtraPileMaterial(const AIRequest &,Room &,const ServerPlayer *,const QJsonObject &,int);
#endif
