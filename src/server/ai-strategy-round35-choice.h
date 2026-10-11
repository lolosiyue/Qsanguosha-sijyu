#ifndef AI_STRATEGY_ROUND35_CHOICE_H
#define AI_STRATEGY_ROUND35_CHOICE_H
#include <QJsonObject>
#include <QString>
class Room;
class ServerPlayer;
struct AIRequest;
bool aiRound35OriginalQuestion(const AIRequest &request);
bool aiRound35GuanxingConstruction(const AIRequest &request, const ServerPlayer *viewer,
                                   const QJsonObject &spec);
bool emitAIRound35SourceWarning(Room &room, const AIRequest &request,
                                const ServerPlayer *viewer, const QString &source,
                                const QString &message);
#endif
