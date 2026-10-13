#ifndef AI_STRATEGY_ROOM_LEDGER_H
#define AI_STRATEGY_ROOM_LEDGER_H
#include <QObject>
class Room;
// Base for per-Room AI state that is found again through room.children().
// Room is a QThread object living on the main thread, but most ledgers are first
// built on its game threads (Room::run, RoomThread). QObject(&room) there makes Qt
// refuse the parent ("Cannot create children for a parent that is in a different
// thread"): the ledger leaked, no lookup found it, and its state started over on
// every call. This hands the ledger to the Room's thread before parenting it.
class AIRoomLedger : public QObject
{
public:
    explicit AIRoomLedger(Room &room);
};
#endif
