#pragma once
#include <QJsonObject>
class Room;class ServerPlayer;struct AIRequest;
// Two fixed actual-source copies only. No value is returned, no reader permit.
QJsonObject copyAIRound53SourcePriority(Room&,ServerPlayer*,const AIRequest&,const QJsonObject&);

// Fixed qizhan/Jianying suffixes only; no priority value or reader permit.
QJsonObject incrementAIRound53SourcePriority(Room&,ServerPlayer*,const AIRequest&,const QJsonObject&);
