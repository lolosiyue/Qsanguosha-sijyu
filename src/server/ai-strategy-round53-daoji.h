#pragma once
#include <QJsonObject>
class Room;class ServerPlayer;struct AIRequest;
QJsonObject queryAIRound53DaojiCard(Room&,const AIRequest&,ServerPlayer*,const QString&,const QJsonObject&);
