#include "local-response-ui-case.h"

#include "card.h"
#include "json.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryFile>

using namespace QSanProtocol;

namespace {

bool loadCase(const QByteArray &json, LocalResponseUiCase *testCase, QString *error)
{
    QTemporaryFile file;
    if (!file.open()) {
        *error = QStringLiteral("cannot create temporary case file");
        return false;
    }
    if (file.write(json) != json.size() || !file.flush()) {
        *error = QStringLiteral("cannot write temporary case file");
        return false;
    }
    return LocalResponseUiCase::load(file.fileName(), testCase, error);
}

bool check(bool condition, const QString &message)
{
    if (!condition)
        qCritical().noquote() << message;
    return condition;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int failures = 0;

    {
        const QStringList commands {
            QStringLiteral("S_COMMAND_CHOOSE_ROLE"),
            QStringLiteral("S_COMMAND_CHOOSE_GENERAL"),
            QStringLiteral("S_COMMAND_CHOOSE_DIRECTION"),
            QStringLiteral("S_COMMAND_EXCHANGE_CARD"),
            QStringLiteral("S_COMMAND_ASK_PEACH"),
            QStringLiteral("S_COMMAND_SKILL_GUANXING"),
            QStringLiteral("S_COMMAND_SKILL_GONGXIN"),
            QStringLiteral("S_COMMAND_SKILL_YIJI"),
            QStringLiteral("S_COMMAND_PLAY_CARD"),
            QStringLiteral("S_COMMAND_RESPONSE_CARD"),
            QStringLiteral("S_COMMAND_DISCARD_CARD"),
            QStringLiteral("S_COMMAND_MULTIPLE_CHOICE"),
            QStringLiteral("S_COMMAND_CHOOSE_SUIT"),
            QStringLiteral("S_COMMAND_CHOOSE_KINGDOM"),
            QStringLiteral("S_COMMAND_CHOOSE_PLAYER"),
            QStringLiteral("S_COMMAND_INVOKE_SKILL"),
            QStringLiteral("S_COMMAND_TRIGGER_ORDER"),
            QStringLiteral("S_COMMAND_NULLIFICATION"),
            QStringLiteral("S_COMMAND_SHOW_CARD"),
            QStringLiteral("S_COMMAND_AMAZING_GRACE"),
            QStringLiteral("S_COMMAND_PINDIAN"),
            QStringLiteral("S_COMMAND_CHOOSE_CARD"),
            QStringLiteral("S_COMMAND_CHOOSE_ORDER"),
            QStringLiteral("S_COMMAND_CHOOSE_ROLE_3V3"),
            QStringLiteral("S_COMMAND_SURRENDER"),
            QStringLiteral("S_COMMAND_LUCK_CARD"),
            QStringLiteral("S_COMMAND_ASK_GENERAL"),
            QStringLiteral("S_COMMAND_ARRANGE_GENERAL"),
            QStringLiteral("S_COMMAND_QML_INTERACT")
        };
        bool passed = commands.size() == 29;
        int serial = 3000;
        for (const QString &expectedCommand : commands) {
            QJsonObject request;
            request.insert(QStringLiteral("command"), expectedCommand);
            request.insert(QStringLiteral("serial"), ++serial);
            request.insert(QStringLiteral("raw_body"), QJsonArray());
            QJsonObject root;
            root.insert(QStringLiteral("schema_version"), 1);
            root.insert(QStringLiteral("name"), expectedCommand.toLower());
            root.insert(QStringLiteral("bootstrap"), QJsonObject());
            root.insert(QStringLiteral("request"), request);
            root.insert(QStringLiteral("actions"), QJsonArray());

            LocalResponseUiCase testCase;
            QString error;
            Packet packet;
            QString command;
            QVariant body;
            const bool mapped = loadCase(QJsonDocument(root).toJson(QJsonDocument::Compact),
                    &testCase, &error)
                && testCase.makeRequestPacket(&packet, &command, &body, {}, &error)
                && command == expectedCommand && packet.globalSerial == static_cast<uint>(serial);
            passed = check(mapped,
                QStringLiteral("interactive command fixture failed for %1: %2")
                    .arg(expectedCommand, error)) && passed;
        }
        failures += passed ? 0 : 1;
    }

    {
        const QByteArray json = R"json({
            "schema_version": 1,
            "name": "raw",
            "bootstrap": {},
            "request": {
                "command": "S_COMMAND_RESPONSE_CARD",
                "serial": 2001,
                "raw_body": ["@@skill!", "@prompt", 2, 1]
            },
            "actions": []
        })json";
        LocalResponseUiCase testCase;
        QString error;
        Packet packet;
        QString command;
        QVariant body;
        bool passed = loadCase(json, &testCase, &error)
            && testCase.makeRequestPacket(&packet, &command, &body, {}, &error);
        passed = check(passed, QStringLiteral("raw_body case failed: %1").arg(error)) && passed;
        passed = check(command == QStringLiteral("S_COMMAND_RESPONSE_CARD"),
            QStringLiteral("raw_body command mismatch")) && passed;
        passed = check(packet.globalSerial == 2001,
            QStringLiteral("raw_body serial mismatch")) && passed;
        const JsonArray values = body.toList();
        passed = check(values.size() == 4 && values.at(0).toString() == QStringLiteral("@@skill!"),
            QStringLiteral("raw_body payload mismatch")) && passed;
        failures += passed ? 0 : 1;
    }

    {
        const QByteArray json = R"json({
            "schema_version": 1,
            "name": "discard-adapter",
            "bootstrap": {},
            "request": {
                "api": "askForDiscard",
                "command": "S_COMMAND_DISCARD_CARD",
                "serial": 1005,
                "args": {
                    "discard_num": 2,
                    "min_num": 1,
                    "optional": true,
                    "include_equip": false,
                    "prompt": "@discard",
                    "pattern": "."
                }
            },
            "actions": []
        })json";
        LocalResponseUiCase testCase;
        QString error;
        Packet packet;
        QString command;
        QVariant body;
        bool passed = loadCase(json, &testCase, &error)
            && testCase.makeRequestPacket(&packet, &command, &body, {}, &error);
        const JsonArray values = body.toList();
        passed = check(passed, QStringLiteral("discard adapter failed: %1").arg(error)) && passed;
        passed = check(values.size() == 6 && values.at(0).toInt() == 2
                && values.at(1).toInt() == 1 && values.at(2).toBool()
                && !values.at(3).toBool() && values.at(5).toString() == QStringLiteral("."),
            QStringLiteral("discard adapter body mismatch")) && passed;
        failures += passed ? 0 : 1;
    }

    {
        const QByteArray json = R"json({
            "schema_version": 1,
            "name": "player-chosen-adapter",
            "bootstrap": {},
            "request": {
                "api": "askForPlayerChosen",
                "command": "S_COMMAND_CHOOSE_PLAYER",
                "serial": 1008,
                "args": {
                    "players": ["target", "other"],
                    "reason": "hujia",
                    "prompt": "@choose-player",
                    "max": 1,
                    "min": 1
                }
            },
            "actions": []
        })json";
        LocalResponseUiCase testCase;
        QString error;
        Packet packet;
        QString command;
        QVariant body;
        bool passed = loadCase(json, &testCase, &error)
            && testCase.makeRequestPacket(&packet, &command, &body, {}, &error);
        const JsonArray values = body.toList();
        const JsonArray players = values.value(0).toList();
        passed = check(passed, QStringLiteral("player-chosen adapter failed: %1").arg(error)) && passed;
        passed = check(values.size() == 5 && players.size() == 2
                && players.at(0).toString() == QStringLiteral("target")
                && players.at(1).toString() == QStringLiteral("other")
                && values.at(3).toInt() == 1 && values.at(4).toInt() == 1,
            QStringLiteral("player-chosen adapter body mismatch")) && passed;
        failures += passed ? 0 : 1;
    }

    {
        const QMap<QString, int> aliases {
            { QStringLiteral("first"), 41 },
            { QStringLiteral("second"), 42 },
            { QStringLiteral("third"), 43 }
        };
        auto adaptedBody = [&aliases](const QByteArray &json, const QString &expectedCommand,
                               JsonArray *values, QString *error) {
            LocalResponseUiCase testCase;
            Packet packet;
            QString command;
            QVariant body;
            if (!loadCase(json, &testCase, error)
                || !testCase.makeRequestPacket(&packet, &command, &body, aliases, error)) {
                return false;
            }
            if (command != expectedCommand) {
                *error = QStringLiteral("command mismatch: %1").arg(command);
                return false;
            }
            *values = body.toList();
            return true;
        };

        JsonArray values;
        QString error;
        bool passed = adaptedBody(R"json({
            "schema_version": 1, "name": "card-chosen", "bootstrap": {},
            "request": {"api": "askForCardChosen", "command": "S_COMMAND_CHOOSE_CARD", "serial": 2002,
                "args": {"player": "self", "flags": "h", "reason": "test", "handcard_visible": true,
                    "method": "discard", "disabled_cards": ["second"], "can_cancel": true}}, "actions": []
        })json", QStringLiteral("S_COMMAND_CHOOSE_CARD"), &values, &error);
        passed = check(passed, QStringLiteral("card-chosen adapter failed: %1").arg(error)) && passed;
        passed = check(values.size() == 7 && values.at(0).toString() == QStringLiteral("self")
                && values.at(3).toBool() && values.at(4).toInt() == Card::MethodDiscard
                && values.at(5).toList() == QVariantList { 42 } && values.at(6).toBool(),
            QStringLiteral("card-chosen adapter body mismatch")) && passed;
        failures += passed ? 0 : 1;

        values.clear();
        error.clear();
        passed = adaptedBody(R"json({
            "schema_version": 1, "name": "ag", "bootstrap": {},
            "request": {"api": "askForAG", "command": "S_COMMAND_AMAZING_GRACE", "serial": 2003,
                "args": {"refusable": true, "reason": "test", "prompt": "@ag",
                    "cards": ["first", "second"], "disabled_cards": ["first"]}}, "actions": []
        })json", QStringLiteral("S_COMMAND_AMAZING_GRACE"), &values, &error);
        passed = check(passed, QStringLiteral("AG adapter failed: %1").arg(error)) && passed;
        passed = check(values.size() == 3 && values.at(0).toBool()
                && values.at(1).toString() == QStringLiteral("test"),
            QStringLiteral("AG adapter body mismatch")) && passed;
        failures += passed ? 0 : 1;

        values.clear();
        error.clear();
        passed = adaptedBody(R"json({
            "schema_version": 1, "name": "yiji", "bootstrap": {},
            "request": {"api": "askForYiji", "command": "S_COMMAND_SKILL_YIJI", "serial": 2004,
                "args": {"cards": ["first", "third"], "optional": false, "max_num": 2,
                    "players": ["target"], "prompt": "@yiji"}}, "actions": []
        })json", QStringLiteral("S_COMMAND_SKILL_YIJI"), &values, &error);
        passed = check(passed, QStringLiteral("Yiji adapter failed: %1").arg(error)) && passed;
        passed = check(values.size() == 5 && values.at(0).toList() == QVariantList { 41, 43 }
                && !values.at(1).toBool() && values.at(2).toInt() == 2
                && values.at(3).toList() == QVariantList { QStringLiteral("target") },
            QStringLiteral("Yiji adapter body mismatch")) && passed;
        failures += passed ? 0 : 1;

        values.clear();
        error.clear();
        passed = adaptedBody(R"json({
            "schema_version": 1, "name": "guanxing", "bootstrap": {},
            "request": {"api": "askForGuanxing", "command": "S_COMMAND_SKILL_GUANXING", "serial": 2005,
                "args": {"cards": ["first", "second", "third"], "type": 0}}, "actions": []
        })json", QStringLiteral("S_COMMAND_SKILL_GUANXING"), &values, &error);
        passed = check(passed, QStringLiteral("Guanxing adapter failed: %1").arg(error)) && passed;
        passed = check(values.size() == 2 && values.at(0).toList() == QVariantList { 41, 42, 43 }
                && values.at(1).toInt() == 0,
            QStringLiteral("Guanxing adapter body mismatch")) && passed;
        failures += passed ? 0 : 1;

        values.clear();
        error.clear();
        passed = adaptedBody(R"json({
            "schema_version": 1, "name": "gongxin", "bootstrap": {},
            "request": {"api": "askForGongxin", "command": "S_COMMAND_SKILL_GONGXIN", "serial": 2006,
                "args": {"player": "target", "enable_heart": true, "cards": ["first", "second"],
                    "enabled_cards": ["second"]}}, "actions": []
        })json", QStringLiteral("S_COMMAND_SKILL_GONGXIN"), &values, &error);
        passed = check(passed, QStringLiteral("Gongxin adapter failed: %1").arg(error)) && passed;
        passed = check(values.size() == 4 && values.at(0).toString() == QStringLiteral("target")
                && values.at(1).toBool() && values.at(2).toList() == QVariantList { 41, 42 }
                && values.at(3).toList() == QVariantList { 42 },
            QStringLiteral("Gongxin adapter body mismatch")) && passed;
        failures += passed ? 0 : 1;
    }

    {
        const QByteArray json = R"json({
            "schema_version": 2,
            "name": "bad-schema",
            "bootstrap": {},
            "request": {},
            "actions": []
        })json";
        LocalResponseUiCase testCase;
        QString error;
        const bool rejected = !loadCase(json, &testCase, &error)
            && error.contains(QStringLiteral("schema_version"));
        failures += check(rejected, QStringLiteral("invalid schema was not rejected")) ? 0 : 1;
    }

    return failures == 0 ? 0 : 1;
}
