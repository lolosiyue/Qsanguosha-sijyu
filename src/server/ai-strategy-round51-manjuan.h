#pragma once
#include <QJsonObject>
class Room;class ServerPlayer;struct AIRequest;
// Finite current-source operations. Reference tokens are equality evidence only,
// never cards, selected receipts, or permission to inspect hidden live faces.
QJsonObject queryAIRound51Manjuan(Room&,const AIRequest&,ServerPlayer*,const QString&,const QJsonObject&);
