#pragma once
#include <QJsonObject>
class Room;class ServerPlayer;struct AIRequest;
// Native-only actual gameplay Self bridge for two fixed target slots. No ledger.
QJsonObject queryAIRound53TargetSlot(Room&,const AIRequest&,ServerPlayer*,const QString&,const QString&,const QJsonObject&);

struct lua_State;
bool acceptAIRound56ChoulveTargetWriteEntry(Room&,lua_State*,int) noexcept;
