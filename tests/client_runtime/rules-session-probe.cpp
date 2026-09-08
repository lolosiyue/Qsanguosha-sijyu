// Direct consumer of the production host. No TUI/fixture evaluator or rule stubs.
#include "client-rules-host.h"
#include "client-player-model.h"
#include "engine.h"
#include "protocol.h"
#include "server-info.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QSaveFile>
#include <cstdio>
#include <stdexcept>

namespace {
void check(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}
QByteArray read(const QString &path)
{
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "cannot read host output");
    return file.readAll();
}
void write(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit(),
          "cannot write probe data");
}
QByteArray encode(const QJsonObject &value) { return QJsonDocument(value).toJson(QJsonDocument::Compact); }
QJsonObject parse(const QByteArray &bytes)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    check(error.error == QJsonParseError::NoError && document.isObject(), "invalid host JSON");
    return document.object();
}
QJsonObject globals()
{
    return {{"mode", ServerInfo.GameMode}, {"rule", ServerInfo.GameRuleMode},
        {"bans", QJsonArray::fromStringList(ServerInfo.BanPackages)},
        {"during", ServerInfo.DuringGame}, {"second", ServerInfo.Enable2ndGeneral},
        {"same", ServerInfo.EnableSame}, {"basara", ServerInfo.EnableBasara},
        {"hegemony", ServerInfo.EnableHegemony}, {"melee", ServerInfo.EnableMeleeMode},
        {"hp", ServerInfo.MaxHpScheme}, {"subtraction", ServerInfo.Scheme0Subtraction}};
}
QJsonObject player(const QString &name, int seat, const QString &general)
{
    return {{"object_name", name}, {"seat", seat}, {"general", general},
        {"role", "loyalist"}, {"alive", true}, {"hp", 4}, {"max_hp", 4},
        {"phase", "play"}, {"skills", QJsonArray()}};
}
QJsonObject request(int count, int cardId, bool skill = false)
{
    auto self = player("sgs1", 1, "guanyu");
    self.insert("hand_count", 1);
    self.insert("skills", skill ? QJsonArray{"wusheng"} : QJsonArray());
    QJsonObject state{{"card_id_space", count}, {"connection", QJsonObject()},
        {"setup", QJsonObject{{"game_mode", "02p"}}}, {"game", QJsonObject()},
        {"self_name", "sgs1"}, {"player_names", QJsonArray{"sgs1", "sgs2"}},
        {"players", QJsonArray{self, player("sgs2", 2, "liubei")}},
        {"cards", QJsonArray{QJsonObject{{"id", cardId}, {"owner", "sgs1"},
            {"place", static_cast<int>(Player::PlaceHand)}}}}};
    return {{"schema_version", 1}, {"generation", 7}, {"revision", 11},
        {"request_id", "18446744073709551615"},
        {"command", static_cast<int>(skill ? QSanProtocol::S_COMMAND_RESPONSE_CARD
                                          : QSanProtocol::S_COMMAND_PLAY_CARD)},
        {"payload", skill ? QJsonObject{{"pattern", "slash"},
            {"handling_method", static_cast<int>(Card::MethodResponse)}} : QJsonObject()},
        {"state", state}, {"selection", QJsonObject{{"card_ids", QJsonArray{cardId}},
            {"targets", skill ? QJsonArray() : QJsonArray{"sgs2"}},
            {"skill_name", skill ? "wusheng" : ""}, {"skill_instance_id", 0}, {"user_string", ""}}}};
}
}

int main(int argc, char **argv)
{
    try {
        // No QCoreApplication here: the production host must create and own it.
        check(argc == 5 && QString::fromLocal8Bit(argv[1]) == "--asset-root"
            && QString::fromLocal8Bit(argv[3]) == "--output", "expected --asset-root DIR --output FILE");
        const QString assets = QDir(QString::fromLocal8Bit(argv[2])).absolutePath();
        const QString output = QDir::current().absoluteFilePath(QString::fromLocal8Bit(argv[4]));
        const QString work = QDir::current().absoluteFilePath("work");
        const QString user = QDir::current().absoluteFilePath("userdata");
        check(QDir().mkpath(work), "cannot create isolated work directory");
        const QString resultPath = work + "/result.json";
        const QString inputPath = work + "/request.json";
        ClientRulesHost host(assets, work, user);
        write(resultPath, "{\"can_confirm\":true}");
        check(host.evaluate() == 3 && parse(read(resultPath)).value("known") == QJsonValue(false),
              "evaluate before initialize must fail without stale success");
        check(host.initialize() == 0, "production initialization failed");
        const QByteArray registryBytes = read(work + "/init.json");
        const QJsonObject registry = parse(registryBytes);
        Engine *const engine = Sanguosha;
        auto *const lua = engine->getLuaState();
        QCoreApplication *const application = QCoreApplication::instance();
        check(lua && application, "production host did not initialize Engine/Lua/application");
        check(host.initialize() == 0 && read(work + "/init.json") == registryBytes
            && Sanguosha == engine && QCoreApplication::instance() == application,
              "repeated initialize must preserve the live instance and registry");
        int slash = -1, red = -1;
        for (const auto &entry : registry.value("registry").toArray()) {
            const auto card = entry.toObject();
            if (slash < 0 && card.value("object_name") == QJsonValue("slash")) slash = card.value("id").toInt();
            const int suit = card.value("suit").toInt();
            if (red < 0 && (suit == Card::Heart || suit == Card::Diamond)) red = card.value("id").toInt();
        }
        check(slash >= 0 && red >= 0, "builtin registry must supply Slash and a red subcard");
        const int count = registry.value("card_count").toInt();
        ServerInfo.GameMode = "host-sentinel";
        ServerInfo.GameRuleMode = "sentinel-rule";
        ServerInfo.BanPackages = QStringList{"sentinel-package"};
        ServerInfo.DuringGame = false;
        ServerInfo.EnableBasara = true;
        ServerInfo.MaxHpScheme = 17;
        const auto before = globals();
        QJsonArray calls;
        auto evaluate = [&](const char *label, const QJsonObject &input, bool known, bool confirm) {
            write(inputPath, encode(input));
            check(host.evaluate() == 0, "production query returned a transport error");
            const QByteArray bytes = read(resultPath);
            const auto result = parse(bytes);
            check(result.value("known") == QJsonValue(known)
                && result.value("can_confirm") == QJsonValue(confirm), "query semantic assertion failed");
            check(result.value("request_id") == input.value("request_id"), "uint64 request identity changed");
            if (confirm) check(result.value("wire").toObject().value("reply_to") == input.value("request_id"),
                               "canonical wire identity changed");
            else check(result.value("wire").isNull(), "failed/incomplete selection retained a reply");
            check(QSanEngine::Self == nullptr && engine->currentRoomContext() == nullptr,
                  "projected Self or RoomContext leaked out of a query");
            check(globals() == before, "ServerInfo leaked out of successful or rejected query");
            check(Sanguosha == engine && engine->getLuaState() == lua
                && QCoreApplication::instance() == application, "query recreated the persistent runtime");
            calls.append(QJsonObject{{"label", label}, {"request", input},
                {"response_utf8", QString::fromUtf8(bytes)}});
            return bytes;
        };
        const auto a = request(count, slash);
        QPointer<QObject> deferred = new QObject;
        deferred->deleteLater();
        const auto first = evaluate("A", a, true, true);
        check(deferred.isNull(), "safe query boundary did not process DeferredDelete");
        auto b = a;
        auto selection = b.value("selection").toObject();
        selection.insert("targets", QJsonArray());
        b.insert("selection", selection);
        evaluate("B_incomplete", b, true, false);
        check(evaluate("A_after_B", a, true, true) == first, "A-B-A query order changed A");
        auto bad = a;
        auto state = bad.value("state").toObject();
        state.insert("players", QJsonArray()); // reject AFTER ServerInfo has been changed
        bad.insert("state", state);
        evaluate("invalid_scene", bad, false, false);
        check(evaluate("A_after_invalid", a, true, true) == first, "failed scene contaminated recovery");
        bad = a;
        bad.insert("request_id", 42);
        evaluate("numeric_request_id", bad, false, false);
        bad = a;
        bad.insert("schema_version", 2);
        evaluate("wrong_schema", bad, false, false);
        const auto viewAs = request(count, red, true);
        evaluate("wusheng_response", viewAs, true, true);
        check(evaluate("A_after_ViewAs", a, true, true) == first, "temporary ViewAs contaminated A");
        // Transport failures are checked natively; the Worker intentionally
        // treats them as fatal and must be replaced rather than reused.
        for (const QByteArray &invalid : {QByteArray("{"), QByteArray("[]"), QByteArray(4 * 1024 * 1024 + 1, ' ')}) {
            write(inputPath, invalid);
            check(host.evaluate() == 2, "malformed/oversized JSON did not fail");
            const auto result = parse(read(resultPath));
            check(result.value("known") == QJsonValue(false) && result.value("wire").isNull(),
                  "transport failure left a prior reply");
        }
        check(evaluate("A_after_bad_json", a, true, true) == first, "invalid bytes poisoned the Engine");
        check(host.shutdown() == 0 && Sanguosha == nullptr && QCoreApplication::instance() == nullptr
            && QSanEngine::Self == nullptr, "graceful shutdown retained native owners");
        check(!QFile::exists(resultPath) && !QFile::exists(work + "/init.json"), "shutdown left successful output");
        check(host.shutdown() == 0, "repeated shutdown is not idempotent");
        check(host.evaluate() == 3 && host.initialize() == 64, "closed host restarted or evaluated");
        const auto terminal = parse(read(resultPath));
        check(terminal.value("wire").isNull() && terminal.value("can_confirm") == QJsonValue(false),
              "closed host exposed a successful reply");
        write(output, encode({{"schema_version", 1}, {"status", "PASS"},
            {"registry", registry}, {"calls", calls},
            {"native_checks", QJsonArray{"preinit", "idempotent_init", "same_engine_lua",
                "server_info_restore", "self_context_cleanup", "deferred_delete", "A_B_A",
                "invalid_scene_recovery", "ViewAs_cleanup", "bad_json_recovery",
                "graceful_shutdown", "terminal_shutdown"}}}));
        std::fprintf(stderr, "[AUTOTEST] RULES_SESSION_LIFECYCLE status=PASS\n");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "[AUTOTEST] RULES_SESSION_LIFECYCLE status=FAIL detail=%s\n", error.what());
        return 1;
    }
}
