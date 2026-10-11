#pragma once
#include <QString>
class AIRequest;
class ServerPlayer;
class Room;
int aiRound43InovationPrototype(const AIRequest &, const ServerPlayer *, const QString &, const QString &);
bool emitAIRound43InovationWarning(Room &, const AIRequest &, const ServerPlayer *, const QString &, const QString &);
