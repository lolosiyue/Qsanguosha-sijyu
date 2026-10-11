#pragma once
#include <QJsonObject>
#include <QString>
class Room;class ServerPlayer;struct AIRequest;
// Two fixed source Player-valued scratch fields on the current own LuaAI.
QJsonObject queryAIRound47OlClanPlayerSlot(const AIRequest&,ServerPlayer*,const QString&,const QString&,const QJsonObject&);
