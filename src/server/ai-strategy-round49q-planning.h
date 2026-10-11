#pragma once
#include <QString>
class QJsonObject;struct AIRequest;class ServerPlayer;
bool aiRound49qRenchouEvaluation(const AIRequest&,const ServerPlayer*,const QJsonObject&);
int aiRound49qRenchouPrototype(const AIRequest&,const ServerPlayer*,const QString&,const QString&);
