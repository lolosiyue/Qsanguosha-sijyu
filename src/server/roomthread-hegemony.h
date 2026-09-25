#ifndef _ROOM_THREAD_HEGEMONY_H
#define _ROOM_THREAD_HEGEMONY_H

#include <QThread>

class Room;

// National-war general draft, same lifetime as RoomThread1v1 / RoomThread3v3:
// the session starts this thread and waits for it before the ordinary RoomThread.
class RoomThreadHegemony : public QThread
{
    Q_OBJECT

public:
    explicit RoomThreadHegemony(Room *room);
    static void chooseGenerals(Room *room);

protected:
    void run() override;

private:
    Room *room;
};

#endif
