#pragma once
#include <QJsonObject>
class Room;class ServerPlayer;struct AIRequest;struct AIEventView;
QJsonObject aiRound53ChuliEvent(const AIEventView&);
QJsonObject queryAIRound53ChuliMap(Room&,const AIRequest&,ServerPlayer*,const QString&,const QJsonObject&);
