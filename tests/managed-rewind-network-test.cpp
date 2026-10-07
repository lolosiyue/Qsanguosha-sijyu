#include "engine-bootstrap.h"
#include "engine.h"
#include "runtime-paths.h"
#include "settings.h"
#include "server-core.h"
#include "room.h"
#include "serverplayer.h"
#include "nativesocket.h"
#include "websocket-gateway.h"
#include "protocol.h"
#include "protocol/protocol-v2-codec.h"
#include "protocol/session/client-session-controller.h"
#include "protocol/session/managed-rewind-payloads.h"
#include "client/core/client-game-state-reducer.h"
#include "client/core/client-game-state.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>
#include <functional>
#include <iostream>
using namespace QSanProtocol;
static int checks = 0;
static QString error;
#define CHECK(x) do { ++checks; if (!(x)) qFatal("CHECK %s:%d: %s (%s)", __FILE__, __LINE__, #x, qPrintable(error)); } while(false)
static void until(const std::function<bool()> &predicate) {
    QElapsedTimer timer; timer.start();
    while (!predicate() && timer.elapsed() < 10000) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(1); }
    CHECK(predicate());
}
struct Peer {
    NativeClientSocket socket;
    ClientSessionController controller;
    ProtocolCodecRouter codec;
    ClientGameState state;
    RewindStatusPayload status;
    quint64 sequence = 0;
    int syncEnds = 0, statuses = 0;
    QString self, name;
    QList<ProtocolMessage> messages;
    bool syncing = false;
    Peer(const QString &name, quint16 port, bool reconnect = false) : name(name) {
        QObject::connect(&socket, &ClientSocket::message_got, &socket, [&, reconnect](const QByteArray &bytes) {
            ProtocolMessage message;
            const auto decoded = codec.decode(bytes, &message);
            error = decoded.detail;
            CHECK(decoded.success);
            CHECK(controller.acceptIncoming(message, &error));
            messages.append(message);
            if (message.command == S_COMMAND_CHECK_VERSION) {
                ServerHelloPayload hello; CHECK(ServerHelloPayload::parse(message.payload,&hello,&error));
                state.setCardIdSpace(hello.cardCount);
                SignupRequestPayload signup; signup.screenName=name; signup.avatar="sujiang"; signup.reconnectRequested=reconnect;
                ProtocolMessage request; CHECK(controller.makeSignupRequest(signup,&request,&error)); send(request);
            } else if (message.command == S_COMMAND_SIGNUP) {
                SignupReplyPayload signup; CHECK(SignupReplyPayload::parse(message.payload,&signup,&error));
                error=signup.message; CHECK(signup.accepted); self=signup.playerId; state.setSelfName(self);
            } else if (message.command == S_COMMAND_SETUP) {
                ProtocolMessage ready; CHECK(controller.makeReadyNotification(&ready,&error));
                ReadyPayload payload; payload.ready=false; ready.payload=payload.toVariant(); send(ready);
                RewindControlPayload query; query.operation="status"; query.sequence=QString::number(++sequence);
                application(S_COMMAND_MANAGED_REWIND,query.toVariant());
            } else if (message.command == S_COMMAND_MANAGED_REWIND_STATE) {
                CHECK(RewindStatusPayload::parse(message.payload,&status,&error)); ++statuses;
                if (!status.message.isEmpty()) qInfo() << "rewind" << this->name << status.message;
                if (status.supported && !status.busy) CHECK(!syncing);
            } else {
                if (message.command == S_COMMAND_STATE_SYNC) {
                    StateSyncPayload sync; CHECK(StateSyncPayload::parse(message.payload,&sync,&error));
                    if (sync.phase=="begin") { CHECK(!syncing); syncing=true; state.resetGameplayState(); }
                    else { CHECK(syncing); syncing=false; ++syncEnds; CHECK(!sync.rootGameId.isEmpty()); }
                }
                ClientGameStateReducer::applyNotification(&state,message.command,message.payload);
            }
        });
        qInfo() << "connecting" << name;
        socket.connectToHost("127.0.0.1",port);
        until([&]{return controller.phase()==ClientSessionPhase::Active && statuses>0;});
    }
    void send(ProtocolMessage message, bool raw=false) {
        const auto bytes=raw ? ProtocolV2Codec().encode(message,&error) : codec.encode(message,&error);
        CHECK(!bytes.isEmpty()); socket.send(bytes);
    }
    void application(int command, const QVariant &payload, bool raw=false) {
        ProtocolMessage message; message.type=ProtocolMessageType::Notification;
        message.source=ProtocolEndpoint::Client; message.destination=ProtocolEndpoint::Room;
        message.command=command; message.hasPayload=true; message.payload=payload;
        CHECK(controller.prepareApplicationMessage(&message,&error)); send(message,raw);
    }
    RewindControlPayload controlPayload(const QString &operation) {
        RewindControlPayload p; p.operation=operation; p.rootGameId=status.rootGameId;
        p.worldId=status.worldId; p.generation=status.generation; p.token=status.token;
        p.sequence=QString::number(++sequence);
        if (operation=="status" && status.token.isEmpty()) { p.rootGameId.clear(); p.worldId.clear(); p.generation.clear(); }
        return p;
    }
    void operation(const QString &operation) {
        const auto p=controlPayload(operation); application(S_COMMAND_MANAGED_REWIND,p.toVariant());
        until([&]{return status.ackSequence==p.sequence && !status.busy;});
    }
    QVariantMap visible() const {
        QVariantMap players;
        for (const auto &id:state.playerNames()) {
            QVariantMap p;
            for (const auto &key:{"hp","max_hp","phase","hand_count","marks"}) p.insert(key,state.playerValue(id,key));
            p.insert("hand",QVariant::fromValue(state.cardsForPlayer(id,Player::PlaceHand)));
            p.insert("equipment",QVariant::fromValue(state.cardsForPlayer(id,Player::PlaceEquip)));
            players.insert(id,p);
        }
        return {{"players",players},{"current_player",state.gameValue("current_player")},{"round",state.gameValue("round")},{"discard",state.gameValue("discard_pile")},
                {"draw_count",state.gameValue("draw_pile_count")}};
    }
};
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv); QTemporaryDir data; CHECK(data.isValid());
    qputenv("XDG_DATA_HOME",data.path().toUtf8()); qputenv("XDG_CONFIG_HOME",data.path().toUtf8());
    CHECK(QSanRuntimePaths::resolve(app.arguments(),&error));
    Config.setValueOverrides({{"EnableAI",false},{"Enable2ndGeneral",false},{"EnableHegemony",false},
        {"EnableMeleeMode",false},{"EnableLuckCard",false},{"EnableCheat",true},{"RestrictedRewindLab",true},
        {"GameMode","02p"},{"ForbidSIMC",false}});
    CHECK(EngineBootstrap::initialize(false,&error));
    QObject::disconnect(&app,SIGNAL(aboutToQuit()),Sanguosha,SLOT(deleteLater())); Config.init();
    Config.EnableAI=false; Config.EnableCheat=true; Config.Enable2ndGeneral=false; Config.EnableHegemony=false;
    Config.GameMode=GameModeStruct("02p",QString(),2); Config.BindAddress="127.0.0.1"; Config.ServerPort=0; Config.WebSocketPort=0;
    Config.ForbidSIMC=false; Config.AIDelay=Config.OriginAIDelay=0;
    {
#if QSAN_ENABLE_WEBSOCKETS
        qsanLinkWebSocketGateway();
#endif
        CHECK(Server::configureGameSeed("17",&error));
        qApp->setProperty("restrictedRewindLabRequested",true);
        Server server(nullptr,GameSessionConfig(17),Server::InitialRoomPolicy::Immediate);
        CHECK(server.listen()); const auto port=server.statusSnapshot().port; CHECK(port>0);
        Peer owner("rewind-owner",port), guest("rewind-guest",port);
        CHECK(owner.status.authorized); CHECK(!guest.status.authorized);
        owner.application(S_COMMAND_READY,ReadyPayload().toVariant());
        until([&]{return owner.status.supported && !owner.status.busy && owner.syncEnds>0 && guest.syncEnds>0;});
        error=owner.status.message; CHECK(error.isEmpty());
        CHECK(owner.status.rootGameId==guest.status.rootGameId);
        CHECK(owner.state.playerValue(owner.self,"hp").toInt() >= 3);
        CHECK(owner.state.playerValue(owner.self,"max_hp").toInt() == 4);
        CHECK(!owner.state.cardsForPlayer(owner.self,Player::PlaceHand).isEmpty());
        CHECK(owner.state.gameValue("current_player").toString() == guest.self);
        CHECK(owner.state.playerValue(owner.self,"phase").toString() == "not_active");
        CHECK(owner.status.token!=guest.status.token);
        const auto root=owner.status.rootGameId;
        const auto originalOwner=owner.visible(), originalGuest=guest.visible();
        const int before=owner.syncEnds;
        guest.operation("turn"); CHECK(guest.status.message.contains("owner")); CHECK(guest.status.generation=="0");
        CHECK(owner.syncEnds==before);
        owner.operation("step"); CHECK(owner.status.message.isEmpty());
        const auto continuedOwner=owner.visible(), continuedGuest=guest.visible();
        owner.operation("turn"); CHECK(owner.status.generation=="1"); CHECK(owner.status.message.isEmpty());
        owner.operation("step"); CHECK(owner.status.message.isEmpty());
        CHECK(owner.visible()==continuedOwner); CHECK(guest.visible()==continuedGuest);
        owner.operation("step"); owner.operation("step");
        owner.operation("round"); CHECK(owner.status.generation=="2"); CHECK(owner.status.message.isEmpty());
        owner.operation("step"); owner.operation("step");
        CHECK(owner.status.rootGameId==root); CHECK(owner.socket.isConnected() && guest.socket.isConnected());
        CHECK(owner.state.gameValue("draw_pile").toList().isEmpty());
        CHECK(guest.state.gameValue("draw_pile").toList().isEmpty());
        // Opponents' card identities must never be introduced by the snapshot.
        CHECK(owner.state.cardsForPlayer(guest.self,Player::PlaceHand).isEmpty());
        CHECK(guest.state.cardsForPlayer(owner.self,Player::PlaceHand).isEmpty());
        auto stale=owner.controlPayload("turn"); stale.generation="0";
        owner.application(S_COMMAND_MANAGED_REWIND,stale.toVariant());
        until([&]{return owner.status.ackSequence==stale.sequence;});
        CHECK(owner.status.message.contains("Stale")); CHECK(owner.status.generation=="2");
        auto foreign=owner.controlPayload("turn"); foreign.rootGameId="another-room";
        owner.application(S_COMMAND_MANAGED_REWIND,foreign.toVariant());
        until([&]{return owner.status.ackSequence==foreign.sequence;}); CHECK(owner.status.message.contains("foreign"));
        auto repeated=owner.controlPayload("status"); owner.application(S_COMMAND_MANAGED_REWIND,repeated.toVariant());
        until([&]{return owner.status.ackSequence==repeated.sequence;}); const int statusCount=owner.statuses;
        owner.application(S_COMMAND_MANAGED_REWIND,repeated.toVariant()); until([&]{return owner.statuses>statusCount;});
        CHECK(owner.status.message.contains("Repeated"));
        const auto ownerSyncCount = owner.syncEnds;
        guest.operation("resync"); CHECK(guest.status.message.isEmpty());
        CHECK(owner.syncEnds == ownerSyncCount);
        // Ordinary state-mutating controls cannot bypass the audited profile.
        const int denied=owner.statuses;
        owner.application(S_COMMAND_TRUST,QVariantMap{{"schema_version",1},{"trusted",false}});
        until([&]{return owner.statuses>denied;}); CHECK(owner.status.message.contains("unsupported"));
        const auto oldToken=owner.status.token;
        owner.socket.disconnectFromHost(); until([&]{return !owner.socket.isConnected() && guest.status.authorized;});
        guest.operation("step"); CHECK(guest.status.message.isEmpty());
        // Actual same-seat reconnect preserves room identity and rotates token.
        Peer reconnected("rewind-owner",port,true);
        until([&]{return reconnected.syncEnds>0 && !reconnected.status.busy;});
        CHECK(reconnected.status.rootGameId==root); CHECK(reconnected.status.token!=oldToken);
        CHECK(!reconnected.status.authorized); CHECK(reconnected.self==owner.self);
        reconnected.operation("turn"); CHECK(reconnected.status.message.contains("owner"));
        guest.operation("turn"); CHECK(guest.status.generation=="3");
        // Cancellation races the boundary claim. Either cancellation wins or
        // exactly one whole restore commits; neither outcome leaves a busy UI.
        const quint64 beforeCancel = guest.status.generation.toULongLong();
        auto mutation = guest.controlPayload("turn");
        auto cancel = guest.controlPayload("cancel");
        guest.application(S_COMMAND_MANAGED_REWIND,mutation.toVariant());
        guest.application(S_COMMAND_MANAGED_REWIND,cancel.toVariant());
        until([&]{return !guest.status.busy && (guest.status.ackSequence==mutation.sequence || guest.status.ackSequence==cancel.sequence);});
        guest.operation("resync");
        CHECK(guest.status.generation.toULongLong() >= beforeCancel);
        CHECK(guest.status.generation.toULongLong() <= beforeCancel+1);
        CHECK(!guest.syncing && !reconnected.syncing);
        // Schema-invalid packets are rejected by the real server decoder.
        auto malformed=guest.controlPayload("step").toVariant(); malformed.insert("unexpected",true);
        guest.application(S_COMMAND_MANAGED_REWIND,malformed,true);
        until([&]{return !guest.socket.isConnected();});
        CHECK(reconnected.socket.isConnected());
        until([&]{return reconnected.status.authorized;});
        reconnected.operation("step"); CHECK(reconnected.status.message.isEmpty());
        server.beginShutdown(); until([&]{return server.shutdownComplete();});
    }
    {
        // A fresh ordinary server must not inherit the one-shot debug opt-in.
        CHECK(!qApp->property("restrictedRewindLabRequested").toBool());
        Server ordinary(nullptr);
        CHECK(ordinary.listen());
        Peer unsupported("ordinary-peer",ordinary.statusSnapshot().port);
        unsupported.operation("status");
        CHECK(!unsupported.status.supported && !unsupported.status.authorized);
        CHECK(unsupported.status.message.contains("explicitly enabled"));
        ordinary.beginShutdown(); until([&]{return ordinary.shutdownComplete();});
    }
    BattleStatistics::waitForPendingWrites(); EngineBootstrap::shutdown();
    std::cout << "managed-rewind-network: " << checks << " checks passed\n";
}
