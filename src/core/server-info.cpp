#include "server-info.h"

ServerInfoStruct ServerInfo;

time_t ServerInfoStruct::getCommandTimeout(QSanProtocol::CommandType command,
                                           QSanProtocol::ProcessInstanceType instance)
{
    if (OperationTimeout < 1)
        return 0;
    time_t timeout = OperationTimeout * 1000;
    if (command == QSanProtocol::S_COMMAND_CHOOSE_GENERAL
        || command == QSanProtocol::S_COMMAND_ASK_GENERAL)
        timeout = OperationTimeout * 1500;
    else if (command == QSanProtocol::S_COMMAND_SKILL_GUANXING
        || command == QSanProtocol::S_COMMAND_ARRANGE_GENERAL)
        timeout = OperationTimeout * 2000;
    else if (command == QSanProtocol::S_COMMAND_NULLIFICATION)
        timeout = NullificationCountDown * 1000;

    if (instance == QSanProtocol::S_SERVER_INSTANCE)
        timeout += ServerTimeoutGraciousPeriod;
    return timeout;
}

bool ServerInfoStruct::parse(const QString &str)
{
    if (str.isEmpty()) {
        DuringGame = false;
        return true;
    }

    DuringGame = true;
    const QStringList fields = str.split(":");
    if (fields.size() < 6)
        return false;

    Name = QString::fromUtf8(QByteArray::fromBase64(fields.at(0).toLatin1()));
    GameMode = fields.at(1);
    if (GameMode.startsWith("02_1v1") || GameMode.startsWith("06_3v3")) {
        GameRuleMode = GameMode.mid(6);
        GameMode = GameMode.mid(0, 6);
    }
    OperationTimeout = fields.at(2).toInt();
    NullificationCountDown = fields.at(3).toInt();
    BanPackages = fields.at(4).split("+");

    const QString flags = fields.at(5);
    RandomSeat = flags.contains("R");
    EnableCheat = flags.contains("C");
    FreeChoose = EnableCheat && flags.contains("F");
    Enable2ndGeneral = flags.contains("S");
    EnableSame = flags.contains("T");
    EnableBasara = flags.contains("B");
    EnableHegemony = flags.contains("H");
    EnableMeleeMode = flags.contains("E");
    EnableAI = flags.contains("A");
    DisableChat = flags.contains("M");

    if (flags.contains("1"))
        MaxHpScheme = 1;
    else if (flags.contains("2"))
        MaxHpScheme = 2;
    else if (flags.contains("3"))
        MaxHpScheme = 3;
    else {
        MaxHpScheme = 0;
        Scheme0Subtraction = 0;
        for (char c = 'a'; c <= 'r'; ++c) {
            if (flags.contains(c)) {
                Scheme0Subtraction = int(c) - int('a') - 5;
                break;
            }
        }
    }
    return true;
}
