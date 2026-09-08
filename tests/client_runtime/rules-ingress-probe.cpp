// A native consumer of the production stream ABI. No test reducer/rules engine.
#include "client-rules-host.h"
#include "engine.h"
#include "player.h"
#include "protocol/protocol-runtime.h"
#include "protocol/session/session-payloads.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <limits>
#include <stdexcept>
#include <cstdio>

using namespace QSanProtocol;
namespace {
void require(bool condition, const char *reason)
{
    if (!condition) throw std::runtime_error(reason);
}
QByteArray read(const QString &path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "cannot read probe output");
    return file.readAll();
}
void write(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit(),
            "cannot write probe input");
}
QVariantMap typed(QVariantMap value = {})
{
    value.insert(QStringLiteral("schema_version"), 1);
    return value;
}
struct Probe {
    ClientRulesHost &host;
    QString work;
    QJsonObject registry, status;
    QJsonArray records;
    int generation = 0;
    quint64 incoming = 0;
    ProtocolCodecRouter codec;

    QJsonObject call(const QString &label, QJsonObject operation, bool expected = true,
                     const QString &reason = {})
    {
        operation.insert(QStringLiteral("schema_version"), 1);
        if (!operation.contains(QStringLiteral("generation")))
            operation.insert(QStringLiteral("generation"), generation);
        write(work + "/stream.json", QJsonDocument(operation).toJson(QJsonDocument::Compact));
        require(host.stream() == 0, "stream ABI returned nonzero");
        const QByteArray bytes = read(work + "/stream-result.json");
        const QJsonObject result = QJsonDocument::fromJson(bytes).object();
        require(result.value("success").isBool() && result.value("success").toBool() == expected,
                qPrintable(label + ": " + result.value("reason").toString()));
        if (!reason.isEmpty()) require(result.value("reason").toString().startsWith(reason),
                                       qPrintable(label + ": unexpected error"));
        if (!expected) require(!result.value("can_confirm").toBool() && result.value("wire").isNull(),
                               "failure leaked a successful reply");
        status = result.value("status").toObject();
        records.append(QJsonObject{{"label", label}, {"operation", operation},
                                   {"response_utf8", QString::fromUtf8(bytes)}});
        return result;
    }
    void frame(const QString &label, bool outgoing, ProtocolMessageType type,
               ProtocolEndpoint source, ProtocolEndpoint destination, int command,
               const QVariantMap &payload, quint64 id, quint64 replyTo = 0,
               bool expected = true, const QString &reason = {})
    {
        ProtocolMessage message;
        message.type = type; message.source = source; message.destination = destination;
        message.command = command; message.messageId = id; message.replyTo = replyTo;
        message.hasPayload = true; message.payload = payload;
        QString error;
        const auto bytes = codec.encode(message, &error);
        require(!bytes.isEmpty(), qPrintable(label + ": cannot encode: " + error));
        call(label, {{"action", "frame"}, {"direction", outgoing ? "outgoing" : "incoming"},
                     {"frame", QString::fromUtf8(bytes)}}, expected, reason);
    }
    void notify(const QString &label, int command, const QVariantMap &payload)
    {
        frame(label, false, ProtocolMessageType::Notification, ProtocolEndpoint::Room,
              ProtocolEndpoint::Client, command, payload, ++incoming);
    }
    void connect()
    {
        ++generation; incoming = 0;
        call("reset_" + QString::number(generation), {{"action", "reset"}});
        ServerHelloPayload hello;
        hello.gameVersion = "probe"; hello.modName = "probe";
        hello.cardCount = registry.value("card_count").toInt();
        auto greeting = hello.toVariant();
        greeting.insert("rules_bundle", registry.value("rules_bundle").toObject().toVariantMap());
        frame("hello", false, ProtocolMessageType::Notification, ProtocolEndpoint::Lobby,
              ProtocolEndpoint::Client, S_COMMAND_CHECK_VERSION, greeting, ++incoming);
        SignupRequestPayload signup;
        signup.screenName = "probe"; signup.avatar = "caocao";
        auto registration = signup.toVariant();
        registration.insert("rules_bundle", registry.value("rules_bundle").toObject().toVariantMap());
        frame("signup", true, ProtocolMessageType::Request, ProtocolEndpoint::Client,
              ProtocolEndpoint::Lobby, S_COMMAND_SIGNUP, registration, 1);
        SignupReplyPayload accepted;
        accepted.accepted = true; accepted.playerId = "sgs1"; accepted.roomId = 1;
        frame("accepted", false, ProtocolMessageType::Reply, ProtocolEndpoint::Lobby,
              ProtocolEndpoint::Client, S_COMMAND_SIGNUP, accepted.toVariant(), ++incoming, 1);
        SetupPayload setup;
        setup.serverName = "probe"; setup.gameMode = "02p"; setup.playerCount = 2;
        frame("setup", false, ProtocolMessageType::Notification, ProtocolEndpoint::Lobby,
              ProtocolEndpoint::Client, S_COMMAND_SETUP, setup.toVariant(), ++incoming);
        frame("ready", true, ProtocolMessageType::Notification, ProtocolEndpoint::Client,
              ProtocolEndpoint::Room, S_COMMAND_READY, ReadyPayload().toVariant(), 2);
        require(status.value("active").toBool(), "handshake did not become active");
    }
    void property(const QString &player, const QString &name, const QString &value)
    {
        notify("property_" + player + '_' + name, S_COMMAND_SET_PROPERTY,
            typed({{"action", "property"}, {"player_name", player},
                   {"property_name", name}, {"string_value", value}}));
    }
    void scene(int slash)
    {
        for (const QString &name : {QStringLiteral("sgs1"), QStringLiteral("sgs2")}) {
            notify("add_" + name, S_COMMAND_ADD_PLAYER,
                typed({{"player_name", name}, {"screen_name", name}, {"avatar", "caocao"}}));
            property(name, "hp", "4"); property(name, "maxhp", "4");
            property(name, "alive", "true"); property(name, "removed", "false");
        }
        property("sgs1", "phase", "play");
        notify("seats", S_COMMAND_ARRANGE_SEATS,
               typed({{"player_names", QVariantList{QString("sgs1"), QString("sgs2")}}}));
        notify("game_start", S_COMMAND_GAME_START, typed({{"card_ids", QVariantList{}}}));
        const QVariantMap reason{{"reason", 0}, {"player_id", "sgs1"}, {"skill_name", ""},
                                  {"event_name", ""}, {"target_id", ""}};
        const QVariantMap move{{"card_ids", QVariantList{slash}}, {"from_place", static_cast<int>(Player::DrawPile)},
            {"to_place", static_cast<int>(Player::PlaceHand)}, {"from_player", ""}, {"to_player", "sgs1"},
            {"from_pile", ""}, {"to_pile", ""}, {"reason", reason}, {"open", true}};
        notify("draw_slash", S_COMMAND_GET_CARD,
               typed({{"move_id", 1}, {"moves", QVariantList{move}}}));
    }
    QString request()
    {
        frame("play_request", false, ProtocolMessageType::Request, ProtocolEndpoint::Room,
              ProtocolEndpoint::Client, S_COMMAND_PLAY_CARD, typed({{"player", "sgs1"}}), ++incoming);
        return QString::number(incoming);
    }
    QJsonObject query(const QString &label, const QString &request, int slash,
                      bool success = true, const QString &reason = {}, int revision = -1)
    {
        return call(label, {{"action", "query"}, {"revision", revision < 0 ? status.value("revision").toInt() : revision},
            {"request_id", request}, {"selection", QJsonObject{
                {"card_ids", QJsonArray{slash}}, {"targets", QJsonArray{"sgs2"}},
                {"skill_name", ""}, {"skill_instance_id", 0}, {"user_string", ""}}}}, success, reason);
    }
};
}

int main(int argc, char **argv)
{
    try {
        require(argc == 5 && QByteArray(argv[1]) == "--asset-root" && QByteArray(argv[3]) == "--output",
                "usage: probe --asset-root <isolated builtin assets> --output <report.json>");
        const QString assets = QString::fromLocal8Bit(argv[2]);
        const QString output = QString::fromLocal8Bit(argv[4]);
        const QString work = QDir::current().absoluteFilePath("stream-work");
        ClientRulesHost host(assets, work, QDir::current().absoluteFilePath("userdata"));
        require(host.initialize() == 0, "production host initialization failed");
        const QJsonObject registry = QJsonDocument::fromJson(read(work + "/init.json")).object();
        int slash = -1;
        for (const auto &entry : registry.value("registry").toArray()) {
            const auto card = entry.toObject();
            if (card.value("object_name") == QJsonValue("slash")) { slash = card.value("id").toInt(); break; }
        }
        require(slash >= 0, "native registry does not contain slash");
        Engine *const engine = Sanguosha;
        Probe probe{host, work, registry, {}, {}};
        probe.connect(); probe.scene(slash);
        QString request = probe.request();
        const auto first = probe.query("select_slash", request, slash);
        require(first.value("evaluation").toObject().value("can_confirm").toBool(),
                qPrintable(QString::fromUtf8(QJsonDocument(first).toJson())));
        const int oldRevision = probe.status.value("revision").toInt();
        probe.notify("mark_update", S_COMMAND_SET_MARK,
                     typed({{"player_name", "sgs1"}, {"mark_name", "probe"}, {"value", 7}}));
        probe.query("stale_revision", request, slash, false, "stream_stale_query", oldRevision);
        require(probe.query("query_after_mark", request, slash).value("evaluation").toObject()
                    .value("can_confirm").toBool(), "mark update lost native query");
        const auto committed = probe.call("committed_view", {{"action", "view"}}).value("state");
        StateSyncPayload sync; sync.syncId = "9007199254740993"; sync.phase = "begin";
        probe.notify("sync_begin", S_COMMAND_STATE_SYNC, sync.toVariant());
        probe.notify("uncommitted_mark", S_COMMAND_SET_MARK,
                     typed({{"player_name", "sgs1"}, {"mark_name", "pending"}, {"value", 77}}));
        probe.query("query_during_sync", request, slash, false, "stream_not_queryable");
        require(probe.call("view_during_sync", {{"action", "view"}}).value("state") == committed,
                "uncommitted snapshot became visible");
        probe.scene(slash);
        sync.phase = "end"; probe.notify("sync_commit", S_COMMAND_STATE_SYNC, sync.toVariant());
        probe.query("old_request_after_sync", request, slash, false, "stream_no_matching_request");
        request = probe.request();
        require(probe.query("query_after_sync", request, slash).value("evaluation").toObject()
                    .value("can_confirm").toBool(), "committed snapshot lost native rules");
        const auto committedAfter = probe.call("view_after_sync", {{"action", "view"}}).value("state");
        require(committedAfter != committed, "state sync did not replace committed state");
        // External snapshots/contexts cannot override the native committed state.
        probe.call("reject_snapshot", {{"action", "query"}, {"revision", 0}, {"request_id", request},
                    {"selection", QJsonObject{}}, {"state", QJsonObject{}}}, false, "invalid_stream_operation");
        probe.call("reject_frame", {{"action", "frame"}, {"direction", "incoming"}, {"frame", "{"}},
                   false, "stream_decode:");
        probe.query("failed_stream_blocks_query", request, slash, false, "stream_not_queryable");
        require(probe.call("rollback_after_failure", {{"action", "view"}}).value("state") == committedAfter,
                "invalid frame mutated committed state");
        probe.connect();
        probe.call("old_generation_ignored", {{"action", "frame"}, {"generation", 1},
                    {"direction", "incoming"}, {"frame", "{"}}, false, "stream_stale_generation");
        require(!probe.status.value("failed").toBool(), "old generation poisoned new stream");
        probe.scene(slash);
        probe.incoming = std::numeric_limits<quint64>::max() - 1;
        request = probe.request();
        const auto large = probe.query("uint64_request", request, slash).value("evaluation").toObject();
        require(large.value("can_confirm").toBool() && large.value("wire").toObject().value("reply_to")
                    == QJsonValue(QStringLiteral("18446744073709551615")), "uint64 request lost precision");
        const auto wire = large.value("wire").toObject();
        probe.frame("observe_reply", true, ProtocolMessageType::Reply, ProtocolEndpoint::Client,
                    ProtocolEndpoint::Room, wire.value("command").toInt(), wire.value("payload").toObject().toVariantMap(),
                    3, probe.incoming);
        probe.query("reply_invalidates_request", request, slash, false, "stream_no_matching_request");
        require(host.evaluate() == 2, "snapshot API remained enabled after stream opt-in");
        require(Sanguosha == engine && Sanguosha->currentRoomContext() == nullptr,
                "stream changed Engine or leaked room context");
        require(host.shutdown() == 0 && host.shutdown() == 0, "shutdown failed");
        require(!QFile::exists(work + "/stream-result.json"), "shutdown left stale stream result");
        require(host.stream() == 3, "closed host accepted stream input");
        const QJsonObject report{{"schema_version", 1}, {"status", "PASS"}, {"registry", registry},
            {"records", probe.records}, {"checks", QJsonArray{
                "native_handshake", "native_reducer_query", "stale_revision", "atomic_sync", "snapshot_rejected",
                "failure_rollback", "generation_isolation", "uint64", "reply_invalidation", "terminal_shutdown"}}};
        write(output, QJsonDocument(report).toJson(QJsonDocument::Compact));
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "RULES_INGRESS_FAIL: %s\n", error.what());
        return 1;
    }
}
