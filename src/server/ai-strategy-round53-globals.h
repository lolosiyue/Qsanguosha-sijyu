#pragma once
#include <QJsonObject>
class Room;class ServerPlayer;struct AIRequest;
// Exact three source sgs fields; write-only, no saved value or reader grant.
QJsonObject writeAIRound53HCGlobal(Room&,ServerPlayer*,const AIRequest&,const QJsonObject&);
