#ifndef QSAN_SERVER_CORE_H
#define QSAN_SERVER_CORE_H

#include <QtCore>
#include <QtNetwork>

#include "game-session-config.h"
#include "protocol/protocol-runtime.h"
#include "server-status.h"

class Room;
class ServerSocket;
class ClientSocket;
class QtUpnpPortMapping;
class ServerPlayer;
class BanIpDialog;
class ServerConnectionContext;
namespace QSanProtocol { struct SignupRequestPayload; }

class Server : public QObject
{
    Q_OBJECT

public:
    explicit Server(QObject *parent);

    friend class BanIpDialog;

    static void writeHeadlessLog(const QString &msg);
    static void setHeadlessLogFile(const QString &path);
    static bool isHeadlessMode;
    // 自動化測試: headless 壓力測試總局數 (--games N 覆寫, 預設 10000)
    static int headlessGameLimit;
    // 自動化測試: headless 指定主公武將 (--test-general/--test-general2, 空 = 隨機)
    static QString forcedHeadlessGeneral;
    static QString forcedHeadlessGeneral2;
    static bool configureGameSeed(const QString &seedText, QString *error = nullptr);

    void broadcast(const QString &msg);
    ServerStatusSnapshot statusSnapshot() const;
    QList<RoomStatusSnapshot> roomSnapshots() const;
    QList<PlayerStatusSnapshot> playerSnapshots() const;
    bool kickPlayer(const QString &id);
    void broadcastAdminMessage(const QString &message);
    bool listen();
    QStringList startupMessages() const;
    void daemonize();
    Room *createNewRoom();
    void signupPlayer(ServerPlayer *player);
    void checkUpnpAndListServer();
    void startHeadlessGame();
    void startTestGame(const QString &scenarioFile, bool headless);

private:
    GameSessionConfig gameSessionConfig(quint64 sessionIndex) const;
    void scheduleDisposeRoom(Room *room);
    void waitForDisposingRooms();
    bool disposingRoomStillRunning() const;
    void finalizeSignup(ServerConnectionContext *context,
                        const QSanProtocol::SignupRequestPayload &signup,
                        quint64 requestId);
    void rejectConnection(ServerConnectionContext *context,
                          const QString &code, const QString &detail);

    ServerSocket *server;
    Room *current;
    QSet<Room *> rooms;
    QList<QPointer<Room> > m_disposingRooms;
    QHash<QString, ServerPlayer *> players;
    QSet<QString> addresses;
    QMultiHash<QString, QString> name2objname;
    bool created_successfully;
    int playerCount;
    quint64 m_nextGameSeedIndex;
    QElapsedTimer m_uptimeTimer;
    QHash<Room *, qint64> m_roomCreatedAtMs;
    QHash<ClientSocket *, ServerConnectionContext *> m_connectionContexts;
    quint64 m_nextConnectionGeneration = 1;

    static bool s_hasGameSeed;
    static quint64 s_gameSeedBase;

    QtUpnpPortMapping *upnpPortMapping;
    QNetworkAccessManager networkAccessManager;
    QNetworkReply *networkReply;
    bool serverListFirstReg;
    int tryTimes;

private slots:
    void processNewConnection(ClientSocket *socket);
    void processRequest(const QByteArray &request);
    void cleanup();
    void gameOver();

    void upnpFinished();
    void upnpTimeout();
    void listServerReply();
    void addToListServer();
    void sendListServerRequest();

signals:
    void logMessage(const QString &message);
    void roomLogMessage(int roomId, const QString &message);
    void server_message(const QString &);
    void newPlayer(ServerPlayer *player);
    void playerJoined(const QString &playerId, const QString &playerName, int roomId);
    // 自動化測試: 房間對局開始/結束標記
    void roomGameStarted(int roomId, const QString &mode);
    void roomGameOver(int roomId, const QString &mode, const QString &winner);
};

#endif
