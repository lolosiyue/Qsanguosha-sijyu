#pragma once
#include <QJsonObject>
class Room;class ServerPlayer;struct AIRequest;
// Seven named reached source mutations only. No retained value, read permit,
// observer exemption, copied callback, action receipt or generic registry setter.
QJsonObject writeAIRound53OverseasPrefix(Room&,ServerPlayer*,const AIRequest&,const QJsonObject&);
