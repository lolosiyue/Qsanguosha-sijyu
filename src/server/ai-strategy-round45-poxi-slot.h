#pragma once
#include <QJsonObject>
#include <QString>
class Room; class ServerPlayer; struct AIRequest;
// Finite own game-VM LuaAI slot only. No observation ledger, gameplay property,
// card authority, private hand lookup, or arbitrary slot name.
QJsonObject queryAIRound45PoxiSlot(Room &,const AIRequest &,ServerPlayer *,
                                 const QString &operation,const QString &publicTarget);
