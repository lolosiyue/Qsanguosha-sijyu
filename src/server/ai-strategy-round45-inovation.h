#pragma once
#include <QString>
#include <QVariant>
class Room;
class ServerPlayer;
struct AIRequest;
struct AIWorldView;
// Exact viewer's Akarin visibility only; not other observers' relationship rows.
void projectAIRound45InovationWorld(AIWorldView &,Room &,const ServerPlayer *);
// Finite actual Kuisi Death question; exposes only the reached damage.from.
void projectAIRound45InovationContext(AIRequest &,Room &,ServerPlayer *,const QVariant &);
class QJsonObject;
int aiRound45InovationPrototype(const AIRequest &,const ServerPlayer *,const QString &,const QString &);
bool aiRound45InovationConstruction(const AIRequest &,const ServerPlayer *,const QJsonObject &);
