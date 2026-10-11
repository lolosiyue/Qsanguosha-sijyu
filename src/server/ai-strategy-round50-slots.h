#pragma once
#include <QJsonObject>
class Room;class ServerPlayer;struct AIRequest;
QJsonObject queryAIRound50SourceSlot(Room&,const AIRequest&,ServerPlayer*,const QString&,const QString&,const QJsonObject&);
