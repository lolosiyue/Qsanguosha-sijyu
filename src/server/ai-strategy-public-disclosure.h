#ifndef AI_STRATEGY_PUBLIC_DISCLOSURE_H
#define AI_STRATEGY_PUBLIC_DISCLOSURE_H

#include <QJsonObject>
#include <QList>

class Room;
class ServerPlayer;
struct CardsMoveStruct;
struct LogMessage;

// Native movement hooks only. A notification-only pile does not call either
// function and cannot establish a witness for an actual table/hidden card.
void invalidateAIStrategyPublicDisclosure(Room &room, int cardId);
void recordAIStrategyPublicMoves(Room &room, const QList<CardsMoveStruct> &moves,
                                bool publiclyVisible);

// Frozen public face. Unknown has no card/location payload. Only a witnessed
// unchanged public mapping permits validation of already disclosed primitives;
// a different live face is never returned. Revision is disclosure provenance.
QJsonObject readAIStrategyPublicDisclosure(Room &room, ServerPlayer *viewer, int cardId);

// Only Room's all-recipient log path may record these public marriage events.
void recordAIStrategyPublicCoupleLog(Room &room, const LogMessage &log);
// A witnessed partner (or initial no-partner) must still match native state.
// Transitions before/after the public log remain unknown until they agree.
QJsonObject readAIStrategyPublicSpouse(Room &room, ServerPlayer *viewer);
void recordAIStrategyPublicKaiyuanFlag(Room &room, ServerPlayer *player, const QString &operation);
QJsonValue readAIStrategyPublicKaiyuanFlag(Room &room, ServerPlayer *player);

#endif
