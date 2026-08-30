#include "local-response-ui-case.h"

#include "card.h"
#include "json.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

using namespace QSanProtocol;

namespace {

bool requireObject(const QJsonObject &root, const QString &key, QString *error)
{
    if (root.value(key).isObject())
        return true;
    *error = QStringLiteral("'%1' must be an object").arg(key);
    return false;
}

int handlingMethod(const QJsonValue &value, bool *ok)
{
    if (value.isDouble()) {
        *ok = true;
        return value.toInt();
    }

    const QString method = value.toString().toLower();
    static const QMap<QString, int> methods {
        { QStringLiteral("none"), Card::MethodNone },
        { QStringLiteral("use"), Card::MethodUse },
        { QStringLiteral("response"), Card::MethodResponse },
        { QStringLiteral("discard"), Card::MethodDiscard },
        { QStringLiteral("recast"), Card::MethodRecast },
        { QStringLiteral("play"), Card::MethodPlay }
    };
    *ok = methods.contains(method);
    return methods.value(method, Card::MethodNone);
}

bool resolveCardAliases(const QJsonValue &value, const QMap<QString, int> &cardAliases,
    JsonArray *ids, QString *error)
{
    if (!value.isArray()) {
        *error = QStringLiteral("card aliases must be an array");
        return false;
    }
    for (const QJsonValue &item : value.toArray()) {
        const QString alias = item.toString();
        if (alias.isEmpty() || !cardAliases.contains(alias)) {
            *error = QStringLiteral("unknown card alias '%1'").arg(alias);
            return false;
        }
        *ids << cardAliases.value(alias);
    }
    return true;
}

QVariant makeAdaptedBody(const QJsonObject &request, const QMap<QString, int> &cardAliases,
    QString *error)
{
    const QString api = request.value(QStringLiteral("api")).toString();
    const QJsonObject args = request.value(QStringLiteral("args")).toObject();
    JsonArray body;

    if (api == QStringLiteral("askForSkillInvoke")) {
        body << args.value(QStringLiteral("skill")).toString()
             << args.value(QStringLiteral("data")).toString();
    } else if (api == QStringLiteral("askForChoice")) {
        body << args.value(QStringLiteral("skill")).toString()
             << args.value(QStringLiteral("choices")).toString()
             << args.value(QStringLiteral("except")).toString()
             << args.value(QStringLiteral("tip")).toString();
    } else if (api == QStringLiteral("askForCard")
        || api == QStringLiteral("askForUseCard")) {
        bool methodOk = false;
        const int method = handlingMethod(args.value(QStringLiteral("method")), &methodOk);
        if (!methodOk) {
            *error = QStringLiteral("request.args.method is invalid");
            return QVariant();
        }
        body << args.value(QStringLiteral("pattern")).toString()
             << args.value(QStringLiteral("prompt")).toString()
             << method
             << args.value(QStringLiteral("notice_index")).toInt(-1);
    } else if (api == QStringLiteral("askForDiscard")) {
        body << args.value(QStringLiteral("discard_num")).toInt()
             << args.value(QStringLiteral("min_num")).toInt()
             << args.value(QStringLiteral("optional")).toBool()
             << args.value(QStringLiteral("include_equip")).toBool()
             << args.value(QStringLiteral("prompt")).toString()
             << args.value(QStringLiteral("pattern")).toString(QStringLiteral("."));
    } else if (api == QStringLiteral("askForExchange")) {
        body << args.value(QStringLiteral("exchange_num")).toInt()
             << args.value(QStringLiteral("min_num")).toInt()
             << args.value(QStringLiteral("include_equip")).toBool()
             << args.value(QStringLiteral("prompt")).toString()
             << args.value(QStringLiteral("optional")).toBool()
             << args.value(QStringLiteral("pattern")).toString(QStringLiteral("."));
    } else if (api == QStringLiteral("askForPlayerChosen")) {
        body << QVariant(args.value(QStringLiteral("players")).toArray().toVariantList())
             << args.value(QStringLiteral("reason")).toString()
             << args.value(QStringLiteral("prompt")).toString()
             << args.value(QStringLiteral("max")).toInt(1)
             << args.value(QStringLiteral("min")).toInt(1);
    } else if (api == QStringLiteral("askForCardChosen")) {
        bool methodOk = false;
        const int method = handlingMethod(args.value(QStringLiteral("method")), &methodOk);
        JsonArray disabledIds;
        if (!methodOk) {
            *error = QStringLiteral("request.args.method is invalid");
            return QVariant();
        }
        if (!resolveCardAliases(args.value(QStringLiteral("disabled_cards")), cardAliases,
            &disabledIds, error)) {
            return QVariant();
        }
        body << args.value(QStringLiteral("player")).toString()
             << args.value(QStringLiteral("flags")).toString(QStringLiteral("hej"))
             << args.value(QStringLiteral("reason")).toString()
             << args.value(QStringLiteral("handcard_visible")).toBool()
             << method << QVariant(disabledIds)
             << args.value(QStringLiteral("can_cancel")).toBool();
    } else if (api == QStringLiteral("askForAG")) {
        body << args.value(QStringLiteral("refusable")).toBool()
             << args.value(QStringLiteral("reason")).toString()
             << args.value(QStringLiteral("prompt")).toString();
    } else if (api == QStringLiteral("askForYiji")) {
        JsonArray cardIds;
        if (!resolveCardAliases(args.value(QStringLiteral("cards")), cardAliases,
            &cardIds, error)) {
            return QVariant();
        }
        body << QVariant(cardIds)
             << args.value(QStringLiteral("optional")).toBool()
             << args.value(QStringLiteral("max_num")).toInt(1)
             << QVariant(args.value(QStringLiteral("players")).toArray().toVariantList())
             << args.value(QStringLiteral("prompt")).toString();
    } else if (api == QStringLiteral("askForGuanxing")) {
        JsonArray cardIds;
        if (!resolveCardAliases(args.value(QStringLiteral("cards")), cardAliases,
            &cardIds, error)) {
            return QVariant();
        }
        body << QVariant(cardIds) << args.value(QStringLiteral("type")).toInt();
    } else if (api == QStringLiteral("askForGongxin")) {
        JsonArray cardIds;
        JsonArray enabledIds;
        if (!resolveCardAliases(args.value(QStringLiteral("cards")), cardAliases,
                &cardIds, error)
            || !resolveCardAliases(args.value(QStringLiteral("enabled_cards")), cardAliases,
                &enabledIds, error)) {
            return QVariant();
        }
        body << args.value(QStringLiteral("player")).toString()
             << args.value(QStringLiteral("enable_heart")).toBool()
             << QVariant(cardIds) << QVariant(enabledIds);
    } else {
        *error = QStringLiteral("unsupported request.api '%1'").arg(api);
        return QVariant();
    }

    return QVariant::fromValue(body);
}

} // namespace

bool LocalResponseUiCase::load(const QString &path, LocalResponseUiCase *result, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("cannot open case '%1': %2").arg(path, file.errorString());
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *error = QStringLiteral("invalid case JSON: %1").arg(parseError.errorString());
        return false;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schema_version")).toInt() != 1) {
        *error = QStringLiteral("schema_version must be 1");
        return false;
    }
    if (root.value(QStringLiteral("name")).toString().isEmpty()) {
        *error = QStringLiteral("'name' must be a non-empty string");
        return false;
    }
    if (!requireObject(root, QStringLiteral("bootstrap"), error)
        || !requireObject(root, QStringLiteral("request"), error)) {
        return false;
    }
    if (!root.value(QStringLiteral("actions")).isArray()) {
        *error = QStringLiteral("'actions' must be an array");
        return false;
    }

    result->m_path = path;
    result->m_root = root;
    return true;
}

QString LocalResponseUiCase::path() const
{
    return m_path;
}

QString LocalResponseUiCase::name() const
{
    return m_root.value(QStringLiteral("name")).toString();
}

QJsonObject LocalResponseUiCase::root() const
{
    return m_root;
}

QJsonObject LocalResponseUiCase::bootstrap() const
{
    return m_root.value(QStringLiteral("bootstrap")).toObject();
}

QJsonObject LocalResponseUiCase::request() const
{
    return m_root.value(QStringLiteral("request")).toObject();
}

QJsonArray LocalResponseUiCase::actions() const
{
    return m_root.value(QStringLiteral("actions")).toArray();
}

QJsonObject LocalResponseUiCase::presentedExpectation() const
{
    return m_root.value(QStringLiteral("expect_presented")).toObject();
}

QJsonObject LocalResponseUiCase::replyExpectation() const
{
    return m_root.value(QStringLiteral("expect_reply")).toObject();
}

QJsonObject LocalResponseUiCase::finalExpectation() const
{
    return m_root.value(QStringLiteral("expect_final")).toObject();
}

bool LocalResponseUiCase::makeRequestMessage(ProtocolMessage *message, QString *actualCommandName,
    QVariant *actualBody, const QMap<QString, int> &cardAliases, QString *error) const
{
    const QJsonObject requestObject = request();
    const QString requestedCommand = requestObject.value(QStringLiteral("command")).toString();
    CommandType command;
    if (!commandFromName(requestedCommand, &command)) {
        *error = QStringLiteral("unsupported request.command '%1'").arg(requestedCommand);
        return false;
    }

    QVariant body;
    if (requestObject.contains(QStringLiteral("raw_body")))
        body = requestObject.value(QStringLiteral("raw_body")).toVariant();
    else
        body = makeAdaptedBody(requestObject, cardAliases, error);
    if (!body.isValid() && !error->isEmpty())
        return false;

    ProtocolMessage requestMessage;
    requestMessage.type = ProtocolMessageType::Request;
    requestMessage.source = ProtocolEndpoint::Room;
    requestMessage.destination = ProtocolEndpoint::Client;
    requestMessage.command = static_cast<int>(command);
    requestMessage.messageId = requestObject.value(QStringLiteral("serial")).toInt();
    if (requestMessage.messageId == 0) {
        *error = QStringLiteral("request.serial must be positive");
        return false;
    }
    requestMessage.hasPayload = !body.isNull();
    if (requestMessage.hasPayload)
        requestMessage.payload = body;

    *message = requestMessage;
    *actualCommandName = commandName(command);
    *actualBody = body;
    return true;
}

bool LocalResponseUiCase::commandFromName(const QString &name, CommandType *command)
{
    static const QMap<QString, CommandType> commands {
        { QStringLiteral("S_COMMAND_CHOOSE_ROLE"), S_COMMAND_CHOOSE_ROLE },
        { QStringLiteral("S_COMMAND_CHOOSE_GENERAL"), S_COMMAND_CHOOSE_GENERAL },
        { QStringLiteral("S_COMMAND_CHOOSE_DIRECTION"), S_COMMAND_CHOOSE_DIRECTION },
        { QStringLiteral("S_COMMAND_ASK_PEACH"), S_COMMAND_ASK_PEACH },
        { QStringLiteral("S_COMMAND_INVOKE_SKILL"), S_COMMAND_INVOKE_SKILL },
        { QStringLiteral("S_COMMAND_MULTIPLE_CHOICE"), S_COMMAND_MULTIPLE_CHOICE },
        { QStringLiteral("S_COMMAND_RESPONSE_CARD"), S_COMMAND_RESPONSE_CARD },
        { QStringLiteral("S_COMMAND_DISCARD_CARD"), S_COMMAND_DISCARD_CARD },
        { QStringLiteral("S_COMMAND_EXCHANGE_CARD"), S_COMMAND_EXCHANGE_CARD },
        { QStringLiteral("S_COMMAND_CHOOSE_PLAYER"), S_COMMAND_CHOOSE_PLAYER },
        { QStringLiteral("S_COMMAND_CHOOSE_CARD"), S_COMMAND_CHOOSE_CARD },
        { QStringLiteral("S_COMMAND_AMAZING_GRACE"), S_COMMAND_AMAZING_GRACE },
        { QStringLiteral("S_COMMAND_SKILL_YIJI"), S_COMMAND_SKILL_YIJI },
        { QStringLiteral("S_COMMAND_SKILL_GUANXING"), S_COMMAND_SKILL_GUANXING },
        { QStringLiteral("S_COMMAND_SKILL_GONGXIN"), S_COMMAND_SKILL_GONGXIN },
        { QStringLiteral("S_COMMAND_PLAY_CARD"), S_COMMAND_PLAY_CARD },
        { QStringLiteral("S_COMMAND_CHOOSE_SUIT"), S_COMMAND_CHOOSE_SUIT },
        { QStringLiteral("S_COMMAND_CHOOSE_KINGDOM"), S_COMMAND_CHOOSE_KINGDOM },
        { QStringLiteral("S_COMMAND_TRIGGER_ORDER"), S_COMMAND_TRIGGER_ORDER },
        { QStringLiteral("S_COMMAND_NULLIFICATION"), S_COMMAND_NULLIFICATION },
        { QStringLiteral("S_COMMAND_SHOW_CARD"), S_COMMAND_SHOW_CARD },
        { QStringLiteral("S_COMMAND_PINDIAN"), S_COMMAND_PINDIAN },
        { QStringLiteral("S_COMMAND_CHOOSE_ORDER"), S_COMMAND_CHOOSE_ORDER },
        { QStringLiteral("S_COMMAND_CHOOSE_ROLE_3V3"), S_COMMAND_CHOOSE_ROLE_3V3 },
        { QStringLiteral("S_COMMAND_SURRENDER"), S_COMMAND_SURRENDER },
        { QStringLiteral("S_COMMAND_LUCK_CARD"), S_COMMAND_LUCK_CARD },
        { QStringLiteral("S_COMMAND_ASK_GENERAL"), S_COMMAND_ASK_GENERAL },
        { QStringLiteral("S_COMMAND_ARRANGE_GENERAL"), S_COMMAND_ARRANGE_GENERAL },
        { QStringLiteral("S_COMMAND_QML_INTERACT"), S_COMMAND_QML_INTERACT }
    };
    if (!commands.contains(name))
        return false;
    *command = commands.value(name);
    return true;
}

QString LocalResponseUiCase::commandName(CommandType command)
{
    static const QMap<CommandType, QString> names {
        { S_COMMAND_CHOOSE_ROLE, QStringLiteral("S_COMMAND_CHOOSE_ROLE") },
        { S_COMMAND_CHOOSE_GENERAL, QStringLiteral("S_COMMAND_CHOOSE_GENERAL") },
        { S_COMMAND_CHOOSE_DIRECTION, QStringLiteral("S_COMMAND_CHOOSE_DIRECTION") },
        { S_COMMAND_ASK_PEACH, QStringLiteral("S_COMMAND_ASK_PEACH") },
        { S_COMMAND_INVOKE_SKILL, QStringLiteral("S_COMMAND_INVOKE_SKILL") },
        { S_COMMAND_MULTIPLE_CHOICE, QStringLiteral("S_COMMAND_MULTIPLE_CHOICE") },
        { S_COMMAND_RESPONSE_CARD, QStringLiteral("S_COMMAND_RESPONSE_CARD") },
        { S_COMMAND_DISCARD_CARD, QStringLiteral("S_COMMAND_DISCARD_CARD") },
        { S_COMMAND_EXCHANGE_CARD, QStringLiteral("S_COMMAND_EXCHANGE_CARD") },
        { S_COMMAND_CHOOSE_PLAYER, QStringLiteral("S_COMMAND_CHOOSE_PLAYER") },
        { S_COMMAND_CHOOSE_CARD, QStringLiteral("S_COMMAND_CHOOSE_CARD") },
        { S_COMMAND_AMAZING_GRACE, QStringLiteral("S_COMMAND_AMAZING_GRACE") },
        { S_COMMAND_SKILL_YIJI, QStringLiteral("S_COMMAND_SKILL_YIJI") },
        { S_COMMAND_SKILL_GUANXING, QStringLiteral("S_COMMAND_SKILL_GUANXING") },
        { S_COMMAND_SKILL_GONGXIN, QStringLiteral("S_COMMAND_SKILL_GONGXIN") },
        { S_COMMAND_PLAY_CARD, QStringLiteral("S_COMMAND_PLAY_CARD") },
        { S_COMMAND_CHOOSE_SUIT, QStringLiteral("S_COMMAND_CHOOSE_SUIT") },
        { S_COMMAND_CHOOSE_KINGDOM, QStringLiteral("S_COMMAND_CHOOSE_KINGDOM") },
        { S_COMMAND_TRIGGER_ORDER, QStringLiteral("S_COMMAND_TRIGGER_ORDER") },
        { S_COMMAND_NULLIFICATION, QStringLiteral("S_COMMAND_NULLIFICATION") },
        { S_COMMAND_SHOW_CARD, QStringLiteral("S_COMMAND_SHOW_CARD") },
        { S_COMMAND_PINDIAN, QStringLiteral("S_COMMAND_PINDIAN") },
        { S_COMMAND_CHOOSE_ORDER, QStringLiteral("S_COMMAND_CHOOSE_ORDER") },
        { S_COMMAND_CHOOSE_ROLE_3V3, QStringLiteral("S_COMMAND_CHOOSE_ROLE_3V3") },
        { S_COMMAND_SURRENDER, QStringLiteral("S_COMMAND_SURRENDER") },
        { S_COMMAND_LUCK_CARD, QStringLiteral("S_COMMAND_LUCK_CARD") },
        { S_COMMAND_ASK_GENERAL, QStringLiteral("S_COMMAND_ASK_GENERAL") },
        { S_COMMAND_ARRANGE_GENERAL, QStringLiteral("S_COMMAND_ARRANGE_GENERAL") },
        { S_COMMAND_QML_INTERACT, QStringLiteral("S_COMMAND_QML_INTERACT") },
        { S_COMMAND_MIRROR_GUANXING_STEP, QStringLiteral("S_COMMAND_MIRROR_GUANXING_STEP") }
    };
    return names.value(command, QStringLiteral("UNKNOWN_COMMAND"));
}
