#ifndef _SERVER_INFO_H
#define _SERVER_INFO_H

#include "protocol.h"

struct ServerInfoStruct
{
    bool parse(const QString &str);
    time_t getCommandTimeout(QSanProtocol::CommandType command, QSanProtocol::ProcessInstanceType instance);

    QString Name;
    QString GameMode;
    QString GameRuleMode;
    int OperationTimeout = 0;
    int NullificationCountDown = 0;
    int ServerTimeoutGraciousPeriod = 1000;
    QStringList BanPackages;
    bool RandomSeat = false;
    bool EnableCheat = false;
    bool FreeChoose = false;
    bool Enable2ndGeneral = false;
    bool EnableSame = false;
    bool EnableBasara = false;
    bool EnableHegemony = false;
    bool EnableMeleeMode = false;
    bool EnableAI = false;
    bool DisableChat = false;
    int MaxHpScheme = 0;
    int Scheme0Subtraction = 0;
    bool DuringGame = false;
};

extern ServerInfoStruct ServerInfo;

#endif
