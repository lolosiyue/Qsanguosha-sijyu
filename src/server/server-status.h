#ifndef QSAN_SERVER_STATUS_H
#define QSAN_SERVER_STATUS_H

#include <QtCore>

struct ServerStatusSnapshot
{
    qint64 uptimeMs = 0;
    QString bindAddress;
    quint16 port = 0;
    QString gameMode;
    int roomCount = 0;
    int gamesRunning = 0;
    int playerCount = 0;
    int onlineCount = 0;
    int robotCount = 0;
    bool aiEnabled = false;
    bool luaEnabled = false;
};

struct RoomStatusSnapshot
{
    int id = -1;
    QString state;
    QString gameMode;
    int playerCount = 0;
    int playerCapacity = 0;
    qint64 uptimeMs = -1;
};

struct PlayerStatusSnapshot
{
    QString id;
    QString name;
    int roomId = -1;
    QString state;
};

#endif
