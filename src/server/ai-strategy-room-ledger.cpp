#include "ai-strategy-room-ledger.h"
#include "room.h"
#include <QMutex>
#include <QThread>

AIRoomLedger::AIRoomLedger(Room &room)
    : QObject(nullptr)
{
    QObject &owner = room;
    // A new parentless object belongs to this thread, so it may push itself over.
    if (thread() != owner.thread())
        moveToThread(owner.thread());
    // Room::run may still be returning while RoomThread starts; serialize the
    // writes to the Room's child list.
    static QMutex adoption;
    QMutexLocker locker(&adoption);
    setParent(&owner);
}
