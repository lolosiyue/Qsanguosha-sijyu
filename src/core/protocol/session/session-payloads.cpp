#include "session-payloads.h"

#include "protocol.h"
#include "protocol/protocol-message-utils.h"

#include <QMetaType>

using namespace QSanProtocol;

namespace
{
bool fail(QString *error, const QString &detail)
{
    if (error != nullptr)
        *error = detail;
    return false;
}

bool isCanonicalPositiveDecimal(const QString &value)
{
    if (value.isEmpty() || value == QLatin1String("0")
        || (value.size() > 1 && value.startsWith(QLatin1Char('0')))) {
        return false;
    }
    for (QChar character : value) {
        if (character < QLatin1Char('0') || character > QLatin1Char('9'))
            return false;
    }
    bool fitsUnsigned64 = false;
    value.toULongLong(&fitsUnsigned64, 10);
    return fitsUnsigned64;
}

bool objectWithSchema(const QVariant &value, QVariantMap *object,
                      const QString &name, QString *error)
{
    if (object == nullptr)
        return fail(error, name + QStringLiteral(" output is null"));
    if (value.userType() != QMetaType::QVariantMap)
        return fail(error, name + QStringLiteral(" must be an object"));
    const QVariantMap parsed = value.toMap();
    int schemaVersion = 0;
    if (!ProtocolMessageUtils::tryParseInt(
            parsed.value(QStringLiteral("schema_version")), schemaVersion)
        || schemaVersion != 1) {
        return fail(error, name + QStringLiteral(" schema_version must be integral 1"));
    }
    *object = parsed;
    return true;
}

bool requiredString(const QVariantMap &object, const QString &key,
                    QString *output, const QString &name, QString *error)
{
    QString parsed;
    if (!ProtocolMessageUtils::tryParseString(object.value(key), parsed))
        return fail(error, QStringLiteral("%1.%2 must be a string").arg(name, key));
    *output = parsed;
    return true;
}

bool optionalString(const QVariantMap &object, const QString &key,
                    QString *output, const QString &name, QString *error)
{
    if (!object.contains(key)) {
        output->clear();
        return true;
    }
    return requiredString(object, key, output, name, error);
}

bool requiredBool(const QVariantMap &object, const QString &key,
                  bool *output, const QString &name, QString *error)
{
    bool parsed = false;
    if (!ProtocolMessageUtils::tryParseBool(object.value(key), parsed))
        return fail(error, QStringLiteral("%1.%2 must be a boolean").arg(name, key));
    *output = parsed;
    return true;
}

bool optionalBool(const QVariantMap &object, const QString &key,
                  bool defaultValue, bool *output,
                  const QString &name, QString *error)
{
    if (!object.contains(key)) {
        *output = defaultValue;
        return true;
    }
    return requiredBool(object, key, output, name, error);
}

bool requiredInt(const QVariantMap &object, const QString &key,
                 int *output, const QString &name, QString *error)
{
    int parsed = 0;
    if (!ProtocolMessageUtils::tryParseInt(object.value(key), parsed))
        return fail(error, QStringLiteral("%1.%2 must be an integer").arg(name, key));
    *output = parsed;
    return true;
}

bool requiredStringList(const QVariantMap &object, const QString &key,
                        QStringList *output, const QString &name, QString *error)
{
    const QVariant value = object.value(key);
    if (value.userType() != QMetaType::QVariantList)
        return fail(error, QStringLiteral("%1.%2 must be a string array").arg(name, key));
    QStringList parsed;
    for (const QVariant &entry : value.toList()) {
        QString text;
        if (!ProtocolMessageUtils::tryParseString(entry, text))
            return fail(error, QStringLiteral("%1.%2 must be a string array").arg(name, key));
        parsed.append(text);
    }
    *output = parsed;
    return true;
}

QVariantList stringArray(const QStringList &values)
{
    QVariantList result;
    for (const QString &value : values)
        result.append(value);
    return result;
}
}

QVariantMap EmptyPayload::toVariant() const
{
    return {{QStringLiteral("schema_version"), SchemaVersion}};
}

bool EmptyPayload::parse(const QVariant &value, EmptyPayload *payload, QString *error)
{
    QVariantMap object;
    if (payload == nullptr)
        return fail(error, QStringLiteral("EmptyPayload output is null"));
    if (!objectWithSchema(value, &object, QStringLiteral("EmptyPayload"), error))
        return false;
    *payload = EmptyPayload();
    return true;
}

QVariantMap StateSyncPayload::toVariant() const
{
    return {{QStringLiteral("schema_version"), SchemaVersion},
            {QStringLiteral("sync_id"), syncId},
            {QStringLiteral("phase"), phase},
            {QStringLiteral("reconnect"), reconnect}};
}

bool StateSyncPayload::parse(const QVariant &value, StateSyncPayload *payload,
                             QString *error)
{
    if (payload == nullptr)
        return fail(error, QStringLiteral("StateSyncPayload output is null"));
    QVariantMap object;
    StateSyncPayload parsed;
    if (!objectWithSchema(value, &object, QStringLiteral("StateSyncPayload"), error)
        || !requiredString(object, QStringLiteral("sync_id"), &parsed.syncId,
                           QStringLiteral("StateSyncPayload"), error)
        || !requiredString(object, QStringLiteral("phase"), &parsed.phase,
                           QStringLiteral("StateSyncPayload"), error)
        || !requiredBool(object, QStringLiteral("reconnect"), &parsed.reconnect,
                         QStringLiteral("StateSyncPayload"), error)) {
        return false;
    }
    if (!isCanonicalPositiveDecimal(parsed.syncId))
        return fail(error, QStringLiteral("StateSyncPayload.sync_id must be a canonical positive decimal string"));
    if (parsed.phase != QLatin1String("begin") && parsed.phase != QLatin1String("end"))
        return fail(error, QStringLiteral("StateSyncPayload.phase must be begin or end"));
    *payload = parsed;
    return true;
}

QVariantMap ServerHelloPayload::toVariant() const
{
    return {{QStringLiteral("schema_version"), SchemaVersion},
            {QStringLiteral("game_version"), gameVersion},
            {QStringLiteral("mod_name"), modName},
            {QStringLiteral("card_count"), cardCount}};
}

bool ServerHelloPayload::parse(const QVariant &value, ServerHelloPayload *payload,
                               QString *error)
{
    if (payload == nullptr)
        return fail(error, QStringLiteral("ServerHelloPayload output is null"));
    QVariantMap object;
    ServerHelloPayload parsed;
    if (!objectWithSchema(value, &object, QStringLiteral("ServerHelloPayload"), error)
        || !requiredString(object, QStringLiteral("game_version"), &parsed.gameVersion,
                           QStringLiteral("ServerHelloPayload"), error)
        || !requiredString(object, QStringLiteral("mod_name"), &parsed.modName,
                           QStringLiteral("ServerHelloPayload"), error)
        || !requiredInt(object, QStringLiteral("card_count"), &parsed.cardCount,
                        QStringLiteral("ServerHelloPayload"), error)) {
        return false;
    }
    if (parsed.cardCount < 0)
        return fail(error, QStringLiteral("ServerHelloPayload.card_count must be non-negative"));
    *payload = parsed;
    return true;
}

QVariantMap SignupRequestPayload::toVariant() const
{
    QVariantMap object{{QStringLiteral("schema_version"), SchemaVersion},
                       {QStringLiteral("reconnect_requested"), reconnectRequested},
                       {QStringLiteral("screen_name"), screenName},
                       {QStringLiteral("avatar"), avatar}};
    if (hasRoomId)
        object.insert(QStringLiteral("room_id"), roomId);
    return object;
}

bool SignupRequestPayload::parse(const QVariant &value, SignupRequestPayload *payload,
                                 QString *error)
{
    if (payload == nullptr)
        return fail(error, QStringLiteral("SignupRequestPayload output is null"));
    if (value.userType() != QMetaType::QVariantMap)
        return fail(error, QStringLiteral("SignupRequestPayload must be an object"));
    const QVariantMap object = value.toMap();
    int schemaVersion = 0;
    if (!ProtocolMessageUtils::tryParseInt(
            object.value(QStringLiteral("schema_version")), schemaVersion)
        || (schemaVersion != 1 && schemaVersion != SchemaVersion)) {
        return fail(error, QStringLiteral("SignupRequestPayload schema_version must be integral 1 or 2"));
    }
    SignupRequestPayload parsed;
    if (!requiredBool(object, QStringLiteral("reconnect_requested"),
                      &parsed.reconnectRequested, QStringLiteral("SignupRequestPayload"), error)
        || !requiredString(object, QStringLiteral("screen_name"), &parsed.screenName,
                           QStringLiteral("SignupRequestPayload"), error)
        || !requiredString(object, QStringLiteral("avatar"), &parsed.avatar,
                           QStringLiteral("SignupRequestPayload"), error)) {
        return false;
    }
    if (parsed.screenName.trimmed().isEmpty())
        return fail(error, QStringLiteral("SignupRequestPayload.screen_name must not be empty"));
    if (object.contains(QStringLiteral("room_id"))) {
        if (schemaVersion != SchemaVersion) {
            return fail(error, QStringLiteral("SignupRequestPayload.room_id requires schema_version 2"));
        }
        int parsedRoomId = 0;
        if (!ProtocolMessageUtils::tryParseInt(object.value(QStringLiteral("room_id")), parsedRoomId)
            || parsedRoomId < 0) {
            return fail(error, QStringLiteral("SignupRequestPayload.room_id must be a non-negative integer"));
        }
        parsed.hasRoomId = true;
        parsed.roomId = parsedRoomId;
    }
    *payload = parsed;
    return true;
}

QVariantMap SignupReplyPayload::toVariant() const
{
    QVariantMap result{{QStringLiteral("schema_version"), SchemaVersion},
                       {QStringLiteral("accepted"), accepted}};
    if (accepted) {
        result.insert(QStringLiteral("reconnected"), reconnected);
        result.insert(QStringLiteral("player_id"), playerId);
        result.insert(QStringLiteral("room_id"), roomId);
    } else {
        result.insert(QStringLiteral("error_code"), errorCode);
        result.insert(QStringLiteral("message"), message);
    }
    return result;
}

bool SignupReplyPayload::parse(const QVariant &value, SignupReplyPayload *payload,
                               QString *error)
{
    if (payload == nullptr)
        return fail(error, QStringLiteral("SignupReplyPayload output is null"));
    if (value.userType() != QMetaType::QVariantMap)
        return fail(error, QStringLiteral("SignupReplyPayload must be an object"));
    const QVariantMap object = value.toMap();
    int schemaVersion = 0;
    if (!ProtocolMessageUtils::tryParseInt(
            object.value(QStringLiteral("schema_version")), schemaVersion)
        || (schemaVersion != 1 && schemaVersion != SchemaVersion)) {
        return fail(error, QStringLiteral("SignupReplyPayload schema_version must be integral 1 or 2"));
    }
    SignupReplyPayload parsed;
    if (!requiredBool(object, QStringLiteral("accepted"), &parsed.accepted,
                      QStringLiteral("SignupReplyPayload"), error)) {
        return false;
    }
    if (parsed.accepted) {
        if (!requiredBool(object, QStringLiteral("reconnected"), &parsed.reconnected,
                          QStringLiteral("SignupReplyPayload"), error)
            || !requiredString(object, QStringLiteral("player_id"), &parsed.playerId,
                               QStringLiteral("SignupReplyPayload"), error)
            || parsed.playerId.isEmpty()) {
            return fail(error, QStringLiteral("accepted SignupReplyPayload requires player_id and reconnected"));
        }
        if (object.contains(QStringLiteral("room_id"))) {
            if (schemaVersion != SchemaVersion) {
                return fail(error, QStringLiteral("SignupReplyPayload.room_id requires schema_version 2"));
            }
            int parsedRoomId = 0;
            if (!ProtocolMessageUtils::tryParseInt(object.value(QStringLiteral("room_id")), parsedRoomId)
                || parsedRoomId < 0) {
                return fail(error, QStringLiteral("SignupReplyPayload.room_id must be a non-negative integer"));
            }
            parsed.roomId = parsedRoomId;
        } else if (schemaVersion == SchemaVersion) {
            return fail(error, QStringLiteral("accepted SignupReplyPayload schema 2 requires room_id"));
        }
    } else if (!requiredString(object, QStringLiteral("error_code"), &parsed.errorCode,
                               QStringLiteral("SignupReplyPayload"), error)
               || !requiredString(object, QStringLiteral("message"), &parsed.message,
                                  QStringLiteral("SignupReplyPayload"), error)
               || parsed.errorCode.isEmpty()) {
        return fail(error, QStringLiteral("rejected SignupReplyPayload requires error_code and message"));
    }
    *payload = parsed;
    return true;
}

QVariantMap SetupPayload::toVariant() const
{
    return {{QStringLiteral("schema_version"), SchemaVersion},
            {QStringLiteral("server_name"), serverName},
            {QStringLiteral("game_mode"), gameMode},
            {QStringLiteral("game_rule_mode"), gameRuleMode},
            {QStringLiteral("operation_timeout"), operationTimeout},
            {QStringLiteral("nullification_countdown"), nullificationCountdown},
            {QStringLiteral("server_timeout_gracious_period"), serverTimeoutGraciousPeriod},
            {QStringLiteral("ban_packages"), stringArray(banPackages)},
            {QStringLiteral("random_seat"), randomSeat},
            {QStringLiteral("enable_cheat"), enableCheat},
            {QStringLiteral("free_choose"), freeChoose},
            {QStringLiteral("enable_second_general"), enableSecondGeneral},
            {QStringLiteral("enable_same"), enableSame},
            {QStringLiteral("enable_basara"), enableBasara},
            {QStringLiteral("enable_hegemony"), enableHegemony},
            {QStringLiteral("enable_melee_mode"), enableMeleeMode},
            {QStringLiteral("enable_ai"), enableAi},
            {QStringLiteral("disable_chat"), disableChat},
            {QStringLiteral("max_hp_scheme"), maxHpScheme},
            {QStringLiteral("scheme0_subtraction"), scheme0Subtraction},
            {QStringLiteral("player_count"), playerCount}};
}

bool SetupPayload::parse(const QVariant &value, SetupPayload *payload, QString *error)
{
    if (payload == nullptr)
        return fail(error, QStringLiteral("SetupPayload output is null"));
    QVariantMap o;
    SetupPayload p;
    const QString n = QStringLiteral("SetupPayload");
    if (!objectWithSchema(value, &o, n, error)
        || !requiredString(o, QStringLiteral("server_name"), &p.serverName, n, error)
        || !requiredString(o, QStringLiteral("game_mode"), &p.gameMode, n, error)
        || !requiredString(o, QStringLiteral("game_rule_mode"), &p.gameRuleMode, n, error)
        || !requiredInt(o, QStringLiteral("operation_timeout"), &p.operationTimeout, n, error)
        || !requiredInt(o, QStringLiteral("nullification_countdown"), &p.nullificationCountdown, n, error)
        || !requiredInt(o, QStringLiteral("server_timeout_gracious_period"), &p.serverTimeoutGraciousPeriod, n, error)
        || !requiredStringList(o, QStringLiteral("ban_packages"), &p.banPackages, n, error)
        || !requiredBool(o, QStringLiteral("random_seat"), &p.randomSeat, n, error)
        || !requiredBool(o, QStringLiteral("enable_cheat"), &p.enableCheat, n, error)
        || !requiredBool(o, QStringLiteral("free_choose"), &p.freeChoose, n, error)
        || !requiredBool(o, QStringLiteral("enable_second_general"), &p.enableSecondGeneral, n, error)
        || !requiredBool(o, QStringLiteral("enable_same"), &p.enableSame, n, error)
        || !requiredBool(o, QStringLiteral("enable_basara"), &p.enableBasara, n, error)
        || !requiredBool(o, QStringLiteral("enable_hegemony"), &p.enableHegemony, n, error)
        || !requiredBool(o, QStringLiteral("enable_melee_mode"), &p.enableMeleeMode, n, error)
        || !requiredBool(o, QStringLiteral("enable_ai"), &p.enableAi, n, error)
        || !requiredBool(o, QStringLiteral("disable_chat"), &p.disableChat, n, error)
        || !requiredInt(o, QStringLiteral("max_hp_scheme"), &p.maxHpScheme, n, error)
        || !requiredInt(o, QStringLiteral("scheme0_subtraction"), &p.scheme0Subtraction, n, error)
        || !requiredInt(o, QStringLiteral("player_count"), &p.playerCount, n, error)) {
        return false;
    }
    if (p.operationTimeout < 0 || p.nullificationCountdown < 0
        || p.serverTimeoutGraciousPeriod < 0 || p.playerCount < 0
        || p.maxHpScheme < 0 || p.maxHpScheme > 3) {
        return fail(error, QStringLiteral("SetupPayload contains an out-of-range numeric field"));
    }
    *payload = p;
    return true;
}

QVariantMap ReadyPayload::toVariant() const
{
    return {{QStringLiteral("schema_version"), SchemaVersion},
            {QStringLiteral("ready"), ready}};
}

bool ReadyPayload::parse(const QVariant &value, ReadyPayload *payload, QString *error)
{
    if (payload == nullptr)
        return fail(error, QStringLiteral("ReadyPayload output is null"));
    QVariantMap object;
    ReadyPayload parsed;
    if (!objectWithSchema(value, &object, QStringLiteral("ReadyPayload"), error)
        || !requiredBool(object, QStringLiteral("ready"), &parsed.ready,
                         QStringLiteral("ReadyPayload"), error)) {
        return false;
    }
    *payload = parsed;
    return true;
}

QVariantMap DiagnosticPayload::toVariant() const
{
    return {{QStringLiteral("schema_version"), SchemaVersion},
            {QStringLiteral("code"), code},
            {QStringLiteral("message"), message},
            {QStringLiteral("fatal"), fatal}};
}

bool DiagnosticPayload::parse(const QVariant &value, DiagnosticPayload *payload,
                              QString *error)
{
    if (payload == nullptr)
        return fail(error, QStringLiteral("DiagnosticPayload output is null"));
    QVariantMap object;
    DiagnosticPayload parsed;
    if (!objectWithSchema(value, &object, QStringLiteral("DiagnosticPayload"), error)
        || !requiredString(object, QStringLiteral("code"), &parsed.code,
                           QStringLiteral("DiagnosticPayload"), error)
        || !requiredString(object, QStringLiteral("message"), &parsed.message,
                           QStringLiteral("DiagnosticPayload"), error)
        || !requiredBool(object, QStringLiteral("fatal"), &parsed.fatal,
                         QStringLiteral("DiagnosticPayload"), error)
        || parsed.code.isEmpty()) {
        return fail(error, QStringLiteral("DiagnosticPayload requires a stable non-empty code"));
    }
    *payload = parsed;
    return true;
}

QVariantMap NetworkDelayPayload::toVariant() const
{
    return {{QStringLiteral("schema_version"), SchemaVersion},
            {QStringLiteral("nonce"), nonce}};
}

bool NetworkDelayPayload::parse(const QVariant &value, NetworkDelayPayload *payload,
                                QString *error)
{
    if (payload == nullptr)
        return fail(error, QStringLiteral("NetworkDelayPayload output is null"));
    QVariantMap object;
    NetworkDelayPayload parsed;
    if (!objectWithSchema(value, &object, QStringLiteral("NetworkDelayPayload"), error)
        || !requiredString(object, QStringLiteral("nonce"), &parsed.nonce,
                           QStringLiteral("NetworkDelayPayload"), error)
        || parsed.nonce.isEmpty()) {
        return fail(error, QStringLiteral("NetworkDelayPayload.nonce must be a non-empty string"));
    }
    *payload = parsed;
    return true;
}

QVariantMap CommandResultPayload::toVariant() const
{
    QVariantMap result{{QStringLiteral("schema_version"), SchemaVersion},
                       {QStringLiteral("success"), success}};
    if (!success) {
        result.insert(QStringLiteral("error_code"), errorCode);
        result.insert(QStringLiteral("message"), message);
    }
    return result;
}

bool CommandResultPayload::parse(const QVariant &value, CommandResultPayload *payload,
                                 QString *error)
{
    if (payload == nullptr)
        return fail(error, QStringLiteral("CommandResultPayload output is null"));
    QVariantMap object;
    CommandResultPayload parsed;
    if (!objectWithSchema(value, &object, QStringLiteral("CommandResultPayload"), error)
        || !requiredBool(object, QStringLiteral("success"), &parsed.success,
                         QStringLiteral("CommandResultPayload"), error)
        || !optionalString(object, QStringLiteral("error_code"), &parsed.errorCode,
                           QStringLiteral("CommandResultPayload"), error)
        || !optionalString(object, QStringLiteral("message"), &parsed.message,
                           QStringLiteral("CommandResultPayload"), error)) {
        return false;
    }
    if (!parsed.success && parsed.errorCode.isEmpty())
        return fail(error, QStringLiteral("failed CommandResultPayload requires error_code"));
    *payload = parsed;
    return true;
}

QVariantMap ChatPayload::toVariant() const
{
    return {{QStringLiteral("schema_version"), SchemaVersion},
            {QStringLiteral("text"), text}};
}

bool ChatPayload::parse(const QVariant &value, ChatPayload *payload, QString *error)
{
    if (payload == nullptr)
        return fail(error, QStringLiteral("ChatPayload output is null"));
    QVariantMap object;
    ChatPayload parsed;
    if (!objectWithSchema(value, &object, QStringLiteral("ChatPayload"), error)
        || !requiredString(object, QStringLiteral("text"), &parsed.text,
                           QStringLiteral("ChatPayload"), error)) {
        return false;
    }
    *payload = parsed;
    return true;
}

QVariantMap ChatMessagePayload::toVariant() const
{
    return {{QStringLiteral("schema_version"), SchemaVersion},
            {QStringLiteral("speaker"), speaker},
            {QStringLiteral("text"), text}};
}

bool ChatMessagePayload::parse(const QVariant &value,
                               ChatMessagePayload *payload, QString *error)
{
    if (payload == nullptr)
        return fail(error, QStringLiteral("ChatMessagePayload output is null"));
    QVariantMap object;
    ChatMessagePayload parsed;
    if (!objectWithSchema(value, &object, QStringLiteral("ChatMessagePayload"), error)
        || !requiredString(object, QStringLiteral("speaker"), &parsed.speaker,
                           QStringLiteral("ChatMessagePayload"), error)
        || !requiredString(object, QStringLiteral("text"), &parsed.text,
                           QStringLiteral("ChatMessagePayload"), error)) {
        return false;
    }
    if (parsed.speaker.isEmpty())
        return fail(error, QStringLiteral("ChatMessagePayload.speaker must not be empty"));
    *payload = parsed;
    return true;
}

QVariantMap AddRobotPayload::toVariant() const
{
    return {{QStringLiteral("schema_version"), SchemaVersion},
            {QStringLiteral("fill_remaining"), fillRemaining},
            {QStringLiteral("count"), count}};
}

bool AddRobotPayload::parse(const QVariant &value, AddRobotPayload *payload,
                            QString *error)
{
    if (payload == nullptr)
        return fail(error, QStringLiteral("AddRobotPayload output is null"));
    QVariantMap object;
    AddRobotPayload parsed;
    if (!objectWithSchema(value, &object, QStringLiteral("AddRobotPayload"), error)
        || !requiredBool(object, QStringLiteral("fill_remaining"), &parsed.fillRemaining,
                         QStringLiteral("AddRobotPayload"), error)
        || !requiredInt(object, QStringLiteral("count"), &parsed.count,
                        QStringLiteral("AddRobotPayload"), error)) {
        return false;
    }
    if (parsed.count < 0 || (parsed.fillRemaining && parsed.count != 0)) {
        return fail(error, QStringLiteral(
            "AddRobotPayload requires a non-negative count and zero when filling"));
    }
    *payload = parsed;
    return true;
}

QVariantMap TrustPayload::toVariant() const
{
    return {{QStringLiteral("schema_version"), SchemaVersion},
            {QStringLiteral("trusted"), trusted}};
}

bool TrustPayload::parse(const QVariant &value, TrustPayload *payload,
                         QString *error)
{
    if (payload == nullptr)
        return fail(error, QStringLiteral("TrustPayload output is null"));
    QVariantMap object;
    TrustPayload parsed;
    if (!objectWithSchema(value, &object, QStringLiteral("TrustPayload"), error)
        || !requiredBool(object, QStringLiteral("trusted"), &parsed.trusted,
                         QStringLiteral("TrustPayload"), error)) {
        return false;
    }
    *payload = parsed;
    return true;
}

QVariantMap PausePayload::toVariant() const
{
    return {{QStringLiteral("schema_version"), SchemaVersion},
            {QStringLiteral("paused"), paused}};
}

bool PausePayload::parse(const QVariant &value, PausePayload *payload,
                         QString *error)
{
    if (payload == nullptr)
        return fail(error, QStringLiteral("PausePayload output is null"));
    QVariantMap object;
    PausePayload parsed;
    if (!objectWithSchema(value, &object, QStringLiteral("PausePayload"), error)
        || !requiredBool(object, QStringLiteral("paused"), &parsed.paused,
                         QStringLiteral("PausePayload"), error)) {
        return false;
    }
    *payload = parsed;
    return true;
}

QVariantMap AnytimeSkillPayload::toVariant() const
{
    return {{QStringLiteral("schema_version"), SchemaVersion},
            {QStringLiteral("skill_name"), skillName}};
}

bool AnytimeSkillPayload::parse(const QVariant &value,
                                AnytimeSkillPayload *payload, QString *error)
{
    if (payload == nullptr)
        return fail(error, QStringLiteral("AnytimeSkillPayload output is null"));
    QVariantMap object;
    AnytimeSkillPayload parsed;
    if (!objectWithSchema(value, &object, QStringLiteral("AnytimeSkillPayload"), error)
        || !requiredString(object, QStringLiteral("skill_name"), &parsed.skillName,
                           QStringLiteral("AnytimeSkillPayload"), error)
        || parsed.skillName.isEmpty()) {
        return fail(error, QStringLiteral("AnytimeSkillPayload.skill_name must not be empty"));
    }
    *payload = parsed;
    return true;
}

QVariantMap SurrenderRequestPayload::toVariant() const
{
    return {{QStringLiteral("schema_version"), SchemaVersion},
            {QStringLiteral("requested"), requested}};
}

bool SurrenderRequestPayload::parse(const QVariant &value,
                                    SurrenderRequestPayload *payload,
                                    QString *error)
{
    if (payload == nullptr)
        return fail(error, QStringLiteral("SurrenderRequestPayload output is null"));
    QVariantMap object;
    SurrenderRequestPayload parsed;
    if (!objectWithSchema(value, &object, QStringLiteral("SurrenderRequestPayload"), error)
        || !requiredBool(object, QStringLiteral("requested"), &parsed.requested,
                         QStringLiteral("SurrenderRequestPayload"), error)
        || !parsed.requested) {
        return fail(error, QStringLiteral("SurrenderRequestPayload.requested must be true"));
    }
    *payload = parsed;
    return true;
}

QVariantMap CheatRequestPayload::toVariant() const
{
    QVariantMap object {{QStringLiteral("schema_version"), SchemaVersion},
                        {QStringLiteral("action"), action}};
    if (action == QLatin1String("run_script")) {
        object.insert(QStringLiteral("script_data"), scriptData);
    } else if (action == QLatin1String("revive")) {
        object.insert(QStringLiteral("player_name"), playerName);
    } else if (action == QLatin1String("damage")) {
        object.insert(QStringLiteral("source_player"), sourcePlayer);
        object.insert(QStringLiteral("target_player"), targetPlayer);
        object.insert(QStringLiteral("nature"), nature);
        object.insert(QStringLiteral("points"), points);
    } else if (action == QLatin1String("state_editor")) {
        object.insert(QStringLiteral("target_player"), targetPlayer);
        object.insert(QStringLiteral("state_type"), stateType);
        object.insert(QStringLiteral("points"), points);
    } else if (action == QLatin1String("kill")) {
        object.insert(QStringLiteral("source_player"), sourcePlayer);
        object.insert(QStringLiteral("target_player"), targetPlayer);
    } else if (action == QLatin1String("get_card")) {
        object.insert(QStringLiteral("card_id"), cardId);
    } else if (action == QLatin1String("change_general")) {
        object.insert(QStringLiteral("general_name"), generalName);
        object.insert(QStringLiteral("secondary_general"), secondaryGeneral);
    }
    return object;
}

bool CheatRequestPayload::parse(const QVariant &value, CheatRequestPayload *payload,
                                QString *error)
{
    if (payload == nullptr)
        return fail(error, QStringLiteral("CheatRequestPayload output is null"));
    QVariantMap object;
    CheatRequestPayload parsed;
    const QString name = QStringLiteral("CheatRequestPayload");
    if (!objectWithSchema(value, &object, name, error)
        || !requiredString(object, QStringLiteral("action"), &parsed.action, name, error)) {
        return false;
    }

    if (parsed.action == QLatin1String("run_script")) {
        if (!requiredString(object, QStringLiteral("script_data"), &parsed.scriptData, name, error))
            return false;
    } else if (parsed.action == QLatin1String("revive")) {
        if (!requiredString(object, QStringLiteral("player_name"), &parsed.playerName, name, error))
            return false;
    } else if (parsed.action == QLatin1String("damage")) {
        if (!requiredString(object, QStringLiteral("source_player"), &parsed.sourcePlayer, name, error)
            || !requiredString(object, QStringLiteral("target_player"), &parsed.targetPlayer, name, error)
            || !requiredInt(object, QStringLiteral("nature"), &parsed.nature, name, error)
            || !requiredInt(object, QStringLiteral("points"), &parsed.points, name, error)) {
            return false;
        }
        if (parsed.nature < S_CHEAT_FIRE_DAMAGE || parsed.nature > S_CHEAT_HUJIA_LOSE)
            return fail(error, QStringLiteral("CheatRequestPayload.nature is unknown"));
    } else if (parsed.action == QLatin1String("state_editor")) {
        if (!requiredString(object, QStringLiteral("target_player"), &parsed.targetPlayer, name, error)
            || !requiredInt(object, QStringLiteral("state_type"), &parsed.stateType, name, error)
            || !requiredInt(object, QStringLiteral("points"), &parsed.points, name, error)) {
            return false;
        }
        if (parsed.stateType < S_CHEAT_CHANGE_MAXCARDS
            || parsed.stateType > S_CHEAT_UseAnaleptic) {
            return fail(error, QStringLiteral("CheatRequestPayload.state_type is unknown"));
        }
    } else if (parsed.action == QLatin1String("kill")) {
        if (!requiredString(object, QStringLiteral("source_player"), &parsed.sourcePlayer, name, error)
            || !requiredString(object, QStringLiteral("target_player"), &parsed.targetPlayer, name, error)) {
            return false;
        }
    } else if (parsed.action == QLatin1String("get_card")) {
        if (!requiredInt(object, QStringLiteral("card_id"), &parsed.cardId, name, error)
            || parsed.cardId < 0) {
            return fail(error, QStringLiteral("CheatRequestPayload.card_id must be non-negative"));
        }
    } else if (parsed.action == QLatin1String("change_general")) {
        if (!requiredString(object, QStringLiteral("general_name"), &parsed.generalName, name, error)
            || !requiredBool(object, QStringLiteral("secondary_general"),
                             &parsed.secondaryGeneral, name, error)) {
            return false;
        }
    } else if (parsed.action != QLatin1String("reverse_play_order")) {
        return fail(error, QStringLiteral("CheatRequestPayload.action is unknown"));
    }

    *payload = parsed;
    return true;
}
