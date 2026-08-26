#include "local-response-ui-case.h"

#include "json.h"

#include <QCoreApplication>
#include <QJsonDocument>
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
            && testCase.makeRequestPacket(&packet, &command, &body, &error);
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
            && testCase.makeRequestPacket(&packet, &command, &body, &error);
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
            && testCase.makeRequestPacket(&packet, &command, &body, &error);
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
