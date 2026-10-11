#pragma once
#include <QJsonObject>
#include <QVariant>
class Room;class ServerPlayer;struct AIRequest;
void projectAIRound47OlClanScalar(const AIRequest&,Room&,const ServerPlayer*,const QVariant&,QJsonObject&);
QJsonObject queryAIRound47OlClanScalarSlot(const AIRequest&,ServerPlayer*,const QString&,const QJsonObject&);
