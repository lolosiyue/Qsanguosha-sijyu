#include "typed-interaction-payloads.h"

#include "json.h"

#include <QMetaType>

#include <cmath>
#include <limits>

using namespace QSanProtocol;

namespace
{
constexpr int SchemaVersion = 1;

enum class FieldType
{
    String,
    Boolean,
    Integer,
    StringArray,
    IntegerArray
};

struct FieldSpec
{
    const char *name;
    FieldType type;
    bool optional;
};

bool fail(QString *error, const QString &detail)
{
    if (error != nullptr)
        *error = detail;
    return false;
}

bool isInteger(const QVariant &value, int *result = nullptr)
{
    bool ok = false;
    const double number = value.toDouble(&ok);
    if (!JsonUtils::isNumber(value) || !ok || !std::isfinite(number)
        || std::trunc(number) != number
        || number < std::numeric_limits<int>::min()
        || number > std::numeric_limits<int>::max()) {
        return false;
    }
    if (result != nullptr)
        *result = static_cast<int>(number);
    return true;
}

bool parseObject(const QVariant &value, const QString &context,
                 QVariantMap *object, QString *error)
{
    if (value.userType() != QMetaType::QVariantMap)
        return fail(error, QStringLiteral("Protocol V2 %1 must be an object").arg(context));
    const QVariantMap parsed = value.toMap();
    if (!parsed.contains(QStringLiteral("schema_version")))
        return fail(error, QStringLiteral("%1 schema_version is required").arg(context));
    int version = 0;
    if (!isInteger(parsed.value(QStringLiteral("schema_version")), &version)
        || version != SchemaVersion) {
        return fail(error, QStringLiteral("%1 schema_version must be 1").arg(context));
    }
    *object = parsed;
    return true;
}

QVariantMap typedObject()
{
    return QVariantMap{{QStringLiteral("schema_version"), SchemaVersion}};
}

bool validateArray(const QVariant &value, FieldType elementType,
                   const QString &context, QString *error)
{
    if (value.userType() != QMetaType::QVariantList)
        return fail(error, QStringLiteral("%1 must be an array").arg(context));
    for (const QVariant &entry : value.toList()) {
        const bool valid = elementType == FieldType::StringArray
            ? entry.userType() == QMetaType::QString : isInteger(entry);
        if (!valid) {
            return fail(error, QStringLiteral("%1 contains an invalid element").arg(context));
        }
    }
    return true;
}

bool validateField(const QVariant &value, FieldType type,
                   const QString &context, QString *error)
{
    switch (type) {
    case FieldType::String:
        if (value.userType() == QMetaType::QString)
            return true;
        break;
    case FieldType::Boolean:
        if (value.userType() == QMetaType::Bool)
            return true;
        break;
    case FieldType::Integer:
        if (isInteger(value))
            return true;
        break;
    case FieldType::StringArray:
    case FieldType::IntegerArray:
        return validateArray(value, type, context, error);
    }
    return fail(error, QStringLiteral("%1 has the wrong type").arg(context));
}

bool arraySchema(TypedInteractionPayloadKind kind,
                 const FieldSpec **fields, int *fieldCount, int *requiredCount,
                 QString *context)
{
    static const FieldSpec exchange[] = {
        {"max_cards", FieldType::Integer, false},
        {"min_cards", FieldType::Integer, false},
        {"include_equip", FieldType::Boolean, false},
        {"prompt", FieldType::String, false},
        {"optional", FieldType::Boolean, false},
        {"pattern", FieldType::String, false}
    };
    static const FieldSpec askPeach[] = {
        {"dying_player", FieldType::String, false},
        {"peach_count", FieldType::Integer, false}
    };
    static const FieldSpec gongxin[] = {
        {"player", FieldType::String, false},
        {"enable_heart", FieldType::Boolean, false},
        {"card_ids", FieldType::IntegerArray, false},
        {"enabled_card_ids", FieldType::IntegerArray, false}
    };
    static const FieldSpec yiji[] = {
        {"card_ids", FieldType::IntegerArray, false},
        {"optional", FieldType::Boolean, false},
        {"max_cards", FieldType::Integer, false},
        {"players", FieldType::StringArray, false},
        {"prompt", FieldType::String, false}
    };
    static const FieldSpec responseCard[] = {
        {"pattern", FieldType::String, false},
        {"prompt", FieldType::String, false},
        {"handling_method", FieldType::Integer, true},
        {"notice_index", FieldType::Integer, true}
    };
    static const FieldSpec discard[] = {
        {"max_cards", FieldType::Integer, false},
        {"min_cards", FieldType::Integer, false},
        {"optional", FieldType::Boolean, false},
        {"include_equip", FieldType::Boolean, false},
        {"prompt", FieldType::String, false},
        {"pattern", FieldType::String, false}
    };
    static const FieldSpec choosePlayer[] = {
        {"players", FieldType::StringArray, false},
        {"skill_name", FieldType::String, false},
        {"prompt", FieldType::String, false},
        {"max_players", FieldType::Integer, false},
        {"min_players", FieldType::Integer, false}
    };
    static const FieldSpec nullification[] = {
        {"trick_name", FieldType::String, false},
        {"source_player", FieldType::String, false},
        {"target_player", FieldType::String, false}
    };
    static const FieldSpec amazingGrace[] = {
        {"refusable", FieldType::Boolean, false},
        {"reason", FieldType::String, false},
        {"prompt", FieldType::String, false}
    };
    static const FieldSpec pindian[] = {
        {"requestor", FieldType::String, false},
        {"player", FieldType::String, false}
    };
    static const FieldSpec chooseCard[] = {
        {"player", FieldType::String, false},
        {"zone_flags", FieldType::String, false},
        {"reason", FieldType::String, false},
        {"hand_cards_visible", FieldType::Boolean, false},
        {"handling_method", FieldType::Integer, false},
        {"disabled_card_ids", FieldType::IntegerArray, false},
        {"can_cancel", FieldType::Boolean, false}
    };
    static const FieldSpec role3v3[] = {
        {"scheme", FieldType::String, false},
        {"roles", FieldType::StringArray, false}
    };

    switch (kind) {
    case TypedInteractionPayloadKind::ExchangeCardRequest:
        *fields = exchange; *fieldCount = 6; *requiredCount = 6;
        *context = QStringLiteral("exchange card request"); return true;
    case TypedInteractionPayloadKind::AskPeachRequest:
        *fields = askPeach; *fieldCount = 2; *requiredCount = 2;
        *context = QStringLiteral("ask peach request"); return true;
    case TypedInteractionPayloadKind::GongxinRequest:
        *fields = gongxin; *fieldCount = 4; *requiredCount = 4;
        *context = QStringLiteral("gongxin request"); return true;
    case TypedInteractionPayloadKind::YijiRequest:
        *fields = yiji; *fieldCount = 5; *requiredCount = 5;
        *context = QStringLiteral("yiji request"); return true;
    case TypedInteractionPayloadKind::ResponseCardRequest:
        *fields = responseCard; *fieldCount = 4; *requiredCount = 2;
        *context = QStringLiteral("response card request"); return true;
    case TypedInteractionPayloadKind::DiscardCardRequest:
        *fields = discard; *fieldCount = 6; *requiredCount = 6;
        *context = QStringLiteral("discard card request"); return true;
    case TypedInteractionPayloadKind::ChoosePlayerRequest:
        *fields = choosePlayer; *fieldCount = 5; *requiredCount = 5;
        *context = QStringLiteral("choose player request"); return true;
    case TypedInteractionPayloadKind::NullificationRequest:
        *fields = nullification; *fieldCount = 3; *requiredCount = 3;
        *context = QStringLiteral("nullification request"); return true;
    case TypedInteractionPayloadKind::AmazingGraceRequest:
        *fields = amazingGrace; *fieldCount = 3; *requiredCount = 3;
        *context = QStringLiteral("amazing grace request"); return true;
    case TypedInteractionPayloadKind::PindianRequest:
        *fields = pindian; *fieldCount = 2; *requiredCount = 2;
        *context = QStringLiteral("pindian request"); return true;
    case TypedInteractionPayloadKind::ChooseCardRequest:
        *fields = chooseCard; *fieldCount = 7; *requiredCount = 7;
        *context = QStringLiteral("choose card request"); return true;
    case TypedInteractionPayloadKind::ChooseRole3v3Request:
        *fields = role3v3; *fieldCount = 2; *requiredCount = 2;
        *context = QStringLiteral("choose role 3v3 request"); return true;
    default:
        return false;
    }
}

bool encodeArray(TypedInteractionPayloadKind kind, bool hasLegacyPayload,
                 const QVariant &legacyPayload, QVariant *v2Payload, QString *error)
{
    const FieldSpec *fields = nullptr;
    int fieldCount = 0;
    int requiredCount = 0;
    QString context;
    if (!arraySchema(kind, &fields, &fieldCount, &requiredCount, &context))
        return false;
    if (!hasLegacyPayload || legacyPayload.userType() != QMetaType::QVariantList)
        return fail(error, QStringLiteral("Legacy %1 must be an array").arg(context));
    const QVariantList entries = legacyPayload.toList();
    if (entries.size() < requiredCount || entries.size() > fieldCount)
        return fail(error, QStringLiteral("Legacy %1 has the wrong field count").arg(context));

    QVariantMap object = typedObject();
    for (int index = 0; index < entries.size(); ++index) {
        const QString fieldName = QString::fromLatin1(fields[index].name);
        if (!validateField(entries.at(index), fields[index].type,
                           QStringLiteral("Legacy %1 %2").arg(context, fieldName), error)) {
            return false;
        }
        object.insert(fieldName, entries.at(index));
    }
    *v2Payload = object;
    return true;
}

bool decodeArray(TypedInteractionPayloadKind kind, const QVariantMap &object,
                 bool *hasLegacyPayload, QVariant *legacyPayload, QString *error)
{
    const FieldSpec *fields = nullptr;
    int fieldCount = 0;
    int requiredCount = 0;
    QString context;
    if (!arraySchema(kind, &fields, &fieldCount, &requiredCount, &context))
        return false;

    QVariantList entries;
    for (int index = 0; index < fieldCount; ++index) {
        const QString fieldName = QString::fromLatin1(fields[index].name);
        if (!object.contains(fieldName)) {
            if (index < requiredCount)
                return fail(error, QStringLiteral("%1 %2 is required").arg(context, fieldName));
            for (int later = index + 1; later < fieldCount; ++later) {
                if (object.contains(QString::fromLatin1(fields[later].name))) {
                    return fail(error, QStringLiteral("%1 optional fields must be contiguous").arg(context));
                }
            }
            break;
        }
        const QVariant field = object.value(fieldName);
        if (!validateField(field, fields[index].type,
                           QStringLiteral("%1 %2").arg(context, fieldName), error)) {
            return false;
        }
        entries.append(field);
    }
    *hasLegacyPayload = true;
    *legacyPayload = entries;
    return true;
}

bool encodeNoPayload(bool hasLegacyPayload, const QString &context,
                     QVariant *v2Payload, QString *error)
{
    if (hasLegacyPayload)
        return fail(error, QStringLiteral("Legacy %1 must not contain a payload").arg(context));
    *v2Payload = typedObject();
    return true;
}

bool decodeNoPayload(const QVariantMap &, bool *hasLegacyPayload,
                     QVariant *legacyPayload)
{
    *hasLegacyPayload = false;
    *legacyPayload = QVariant();
    return true;
}

bool requireField(const QVariantMap &object, const char *name, FieldType type,
                  const QString &context, QVariant *value, QString *error)
{
    const QString key = QString::fromLatin1(name);
    if (!object.contains(key))
        return fail(error, QStringLiteral("%1 %2 is required").arg(context, key));
    const QVariant field = object.value(key);
    if (!validateField(field, type, QStringLiteral("%1 %2").arg(context, key), error))
        return false;
    if (value != nullptr)
        *value = field;
    return true;
}

bool encodeScalar(bool hasLegacyPayload, const QVariant &legacyPayload,
                  const char *fieldName, FieldType type, const QString &context,
                  QVariant *v2Payload, QString *error)
{
    if (!hasLegacyPayload)
        return fail(error, QStringLiteral("Legacy %1 requires a payload").arg(context));
    if (!validateField(legacyPayload, type, QStringLiteral("Legacy %1").arg(context), error))
        return false;
    QVariantMap object = typedObject();
    object.insert(QString::fromLatin1(fieldName), legacyPayload);
    *v2Payload = object;
    return true;
}

bool decodeScalar(const QVariantMap &object, const char *fieldName, FieldType type,
                  const QString &context, bool *hasLegacyPayload,
                  QVariant *legacyPayload, QString *error)
{
    QVariant value;
    if (!requireField(object, fieldName, type, context, &value, error))
        return false;
    *hasLegacyPayload = true;
    *legacyPayload = value;
    return true;
}

bool encodeOptionalCardId(bool hasLegacyPayload, const QVariant &legacyPayload,
                          QVariant *v2Payload, QString *error)
{
    QVariantMap object = typedObject();
    object.insert(QStringLiteral("cancelled"), !hasLegacyPayload);
    if (hasLegacyPayload) {
        if (!validateField(legacyPayload, FieldType::Integer,
                           QStringLiteral("Legacy card id reply"), error)) {
            return false;
        }
        object.insert(QStringLiteral("card_id"), legacyPayload);
    }
    *v2Payload = object;
    return true;
}

bool decodeOptionalCardId(const QVariantMap &object, bool *hasLegacyPayload,
                          QVariant *legacyPayload, QString *error)
{
    QVariant cancelledValue;
    if (!requireField(object, "cancelled", FieldType::Boolean,
                      QStringLiteral("card id reply"), &cancelledValue, error)) {
        return false;
    }
    if (cancelledValue.toBool()) {
        *hasLegacyPayload = false;
        *legacyPayload = QVariant();
        return true;
    }
    return decodeScalar(object, "card_id", FieldType::Integer,
                        QStringLiteral("card id reply"),
                        hasLegacyPayload, legacyPayload, error);
}

bool encodeCancelableList(bool hasLegacyPayload, const QVariant &legacyPayload,
                          const char *fieldName, FieldType type,
                          const QString &context, QVariant *v2Payload, QString *error)
{
    QVariantMap object = typedObject();
    object.insert(QStringLiteral("cancelled"), !hasLegacyPayload);
    if (hasLegacyPayload) {
        if (!validateField(legacyPayload, type,
                           QStringLiteral("Legacy %1").arg(context), error)) {
            return false;
        }
        object.insert(QString::fromLatin1(fieldName), legacyPayload);
    }
    *v2Payload = object;
    return true;
}

bool decodeCancelableList(const QVariantMap &object, const char *fieldName,
                          FieldType type, const QString &context,
                          bool *hasLegacyPayload, QVariant *legacyPayload, QString *error)
{
    QVariant cancelledValue;
    if (!requireField(object, "cancelled", FieldType::Boolean,
                      context, &cancelledValue, error)) {
        return false;
    }
    if (cancelledValue.toBool()) {
        *hasLegacyPayload = false;
        *legacyPayload = QVariant();
        return true;
    }
    return decodeScalar(object, fieldName, type, context,
                        hasLegacyPayload, legacyPayload, error);
}

bool encodeChooseRoleReply(bool hasLegacyPayload, const QVariant &legacyPayload,
                           QVariant *v2Payload, QString *error)
{
    QVariantMap object = typedObject();
    object.insert(QStringLiteral("cancelled"), !hasLegacyPayload);
    if (!hasLegacyPayload) {
        *v2Payload = object;
        return true;
    }
    if (legacyPayload.userType() != QMetaType::QVariantList)
        return fail(error, QStringLiteral("Legacy choose role reply must be an array"));
    const QVariantList entries = legacyPayload.toList();
    if (entries.size() != 2
        || !validateArray(entries.at(0), FieldType::StringArray,
                          QStringLiteral("Legacy choose role players"), error)
        || !validateArray(entries.at(1), FieldType::StringArray,
                          QStringLiteral("Legacy choose role roles"), error)) {
        return false;
    }
    object.insert(QStringLiteral("players"), entries.at(0));
    object.insert(QStringLiteral("roles"), entries.at(1));
    *v2Payload = object;
    return true;
}

bool decodeChooseRoleReply(const QVariantMap &object, bool *hasLegacyPayload,
                           QVariant *legacyPayload, QString *error)
{
    QVariant cancelled;
    if (!requireField(object, "cancelled", FieldType::Boolean,
                      QStringLiteral("choose role reply"), &cancelled, error)) {
        return false;
    }
    if (cancelled.toBool()) {
        *hasLegacyPayload = false;
        *legacyPayload = QVariant();
        return true;
    }
    QVariant players;
    QVariant roles;
    if (!requireField(object, "players", FieldType::StringArray,
                      QStringLiteral("choose role reply"), &players, error)
        || !requireField(object, "roles", FieldType::StringArray,
                         QStringLiteral("choose role reply"), &roles, error)) {
        return false;
    }
    *hasLegacyPayload = true;
    *legacyPayload = QVariantList{players, roles};
    return true;
}

bool encodeGuanxingRequest(bool hasLegacyPayload, const QVariant &legacyPayload,
                           QVariant *v2Payload, QString *error)
{
    if (!hasLegacyPayload || legacyPayload.userType() != QMetaType::QVariantList)
        return fail(error, QStringLiteral("Legacy guanxing request must be an array"));
    const QVariantList entries = legacyPayload.toList();
    if (entries.isEmpty() || entries.size() > 2
        || !validateArray(entries.at(0), FieldType::IntegerArray,
                          QStringLiteral("Legacy guanxing card_ids"), error)) {
        return false;
    }
    QVariantMap object = typedObject();
    object.insert(QStringLiteral("card_ids"), entries.at(0));
    if (entries.size() == 2) {
        int mode = 0;
        if (!isInteger(entries.at(1), &mode) || mode < -1 || mode > 1)
            return fail(error, QStringLiteral("Legacy guanxing mode is unknown"));
        object.insert(QStringLiteral("mode"), mode > 0 ? QStringLiteral("up_only")
            : mode < 0 ? QStringLiteral("down_only") : QStringLiteral("both_sides"));
    }
    *v2Payload = object;
    return true;
}

bool decodeGuanxingRequest(const QVariantMap &object, bool *hasLegacyPayload,
                           QVariant *legacyPayload, QString *error)
{
    QVariant cardIds;
    if (!requireField(object, "card_ids", FieldType::IntegerArray,
                      QStringLiteral("guanxing request"), &cardIds, error)) {
        return false;
    }
    QVariantList entries{cardIds};
    if (object.contains(QStringLiteral("mode"))) {
        QVariant modeValue;
        if (!requireField(object, "mode", FieldType::String,
                          QStringLiteral("guanxing request"), &modeValue, error)) {
            return false;
        }
        const QString mode = modeValue.toString();
        if (mode == QStringLiteral("up_only"))
            entries.append(1);
        else if (mode == QStringLiteral("both_sides"))
            entries.append(0);
        else if (mode == QStringLiteral("down_only"))
            entries.append(-1);
        else
            return fail(error, QStringLiteral("Guanxing request mode is unknown"));
    }
    *hasLegacyPayload = true;
    *legacyPayload = entries;
    return true;
}

bool encodeTwoIntegerLists(bool hasLegacyPayload, const QVariant &legacyPayload,
                           const char *firstName, const char *secondName,
                           const QString &context, QVariant *v2Payload, QString *error)
{
    if (!hasLegacyPayload || legacyPayload.userType() != QMetaType::QVariantList)
        return fail(error, QStringLiteral("Legacy %1 must be an array").arg(context));
    const QVariantList entries = legacyPayload.toList();
    if (entries.size() != 2
        || !validateArray(entries.at(0), FieldType::IntegerArray,
                          QStringLiteral("Legacy %1 first array").arg(context), error)
        || !validateArray(entries.at(1), FieldType::IntegerArray,
                          QStringLiteral("Legacy %1 second array").arg(context), error)) {
        return false;
    }
    QVariantMap object = typedObject();
    object.insert(QString::fromLatin1(firstName), entries.at(0));
    object.insert(QString::fromLatin1(secondName), entries.at(1));
    *v2Payload = object;
    return true;
}

bool decodeTwoIntegerLists(const QVariantMap &object,
                           const char *firstName, const char *secondName,
                           const QString &context, bool *hasLegacyPayload,
                           QVariant *legacyPayload, QString *error)
{
    QVariant first;
    QVariant second;
    if (!requireField(object, firstName, FieldType::IntegerArray, context, &first, error)
        || !requireField(object, secondName, FieldType::IntegerArray,
                         context, &second, error)) {
        return false;
    }
    *hasLegacyPayload = true;
    *legacyPayload = QVariantList{first, second};
    return true;
}

bool encodeYijiReply(bool hasLegacyPayload, const QVariant &legacyPayload,
                     QVariant *v2Payload, QString *error)
{
    QVariantMap object = typedObject();
    object.insert(QStringLiteral("cancelled"), !hasLegacyPayload);
    if (!hasLegacyPayload) {
        *v2Payload = object;
        return true;
    }
    if (legacyPayload.userType() != QMetaType::QVariantList)
        return fail(error, QStringLiteral("Legacy yiji reply must be an array"));
    const QVariantList entries = legacyPayload.toList();
    if (entries.size() != 2
        || !validateArray(entries.at(0), FieldType::IntegerArray,
                          QStringLiteral("Legacy yiji card_ids"), error)
        || entries.at(1).userType() != QMetaType::QString) {
        return fail(error, QStringLiteral("Legacy yiji reply has invalid fields"));
    }
    object.insert(QStringLiteral("card_ids"), entries.at(0));
    object.insert(QStringLiteral("target_player"), entries.at(1));
    *v2Payload = object;
    return true;
}

bool decodeYijiReply(const QVariantMap &object, bool *hasLegacyPayload,
                     QVariant *legacyPayload, QString *error)
{
    QVariant cancelled;
    if (!requireField(object, "cancelled", FieldType::Boolean,
                      QStringLiteral("yiji reply"), &cancelled, error)) {
        return false;
    }
    if (cancelled.toBool()) {
        *hasLegacyPayload = false;
        *legacyPayload = QVariant();
        return true;
    }
    QVariant cardIds;
    QVariant target;
    if (!requireField(object, "card_ids", FieldType::IntegerArray,
                      QStringLiteral("yiji reply"), &cardIds, error)
        || !requireField(object, "target_player", FieldType::String,
                         QStringLiteral("yiji reply"), &target, error)) {
        return false;
    }
    *hasLegacyPayload = true;
    *legacyPayload = QVariantList{cardIds, target};
    return true;
}

bool encodeResponseCardReply(bool hasLegacyPayload, const QVariant &legacyPayload,
                             QVariant *v2Payload, QString *error)
{
    QVariantMap object = typedObject();
    object.insert(QStringLiteral("cancelled"), !hasLegacyPayload);
    if (!hasLegacyPayload) {
        *v2Payload = object;
        return true;
    }
    if (legacyPayload.userType() != QMetaType::QVariantList)
        return fail(error, QStringLiteral("Legacy response card reply must be an array"));
    const QVariantList entries = legacyPayload.toList();
    if (entries.size() != 4
        || entries.at(0).userType() != QMetaType::QString
        || !validateArray(entries.at(1), FieldType::StringArray,
                          QStringLiteral("Legacy response card targets"), error)
        || entries.at(2).userType() != QMetaType::QString
        || !isInteger(entries.at(3))) {
        return fail(error, QStringLiteral("Legacy response card reply has invalid fields"));
    }
    object.insert(QStringLiteral("card_text"), entries.at(0));
    object.insert(QStringLiteral("targets"), entries.at(1));
    object.insert(QStringLiteral("activation_skill_name"), entries.at(2));
    object.insert(QStringLiteral("activation_skill_instance_id"), entries.at(3));
    *v2Payload = object;
    return true;
}

bool decodeResponseCardReply(const QVariantMap &object, bool *hasLegacyPayload,
                             QVariant *legacyPayload, QString *error)
{
    QVariant cancelled;
    if (!requireField(object, "cancelled", FieldType::Boolean,
                      QStringLiteral("response card reply"), &cancelled, error)) {
        return false;
    }
    if (cancelled.toBool()) {
        *hasLegacyPayload = false;
        *legacyPayload = QVariant();
        return true;
    }
    QVariant cardText;
    QVariant targets;
    QVariant skillName;
    QVariant instanceId;
    if (!requireField(object, "card_text", FieldType::String,
                      QStringLiteral("response card reply"), &cardText, error)
        || !requireField(object, "targets", FieldType::StringArray,
                         QStringLiteral("response card reply"), &targets, error)
        || !requireField(object, "activation_skill_name", FieldType::String,
                         QStringLiteral("response card reply"), &skillName, error)
        || !requireField(object, "activation_skill_instance_id", FieldType::Integer,
                         QStringLiteral("response card reply"), &instanceId, error)) {
        return false;
    }
    *hasLegacyPayload = true;
    *legacyPayload = QVariantList{cardText, targets, skillName, instanceId};
    return true;
}

bool encodeChoosePlayerReply(bool hasLegacyPayload, const QVariant &legacyPayload,
                             QVariant *v2Payload, QString *error)
{
    QVariantMap object = typedObject();
    object.insert(QStringLiteral("cancelled"), !hasLegacyPayload);
    if (hasLegacyPayload) {
        if (legacyPayload.userType() != QMetaType::QString)
            return fail(error, QStringLiteral("Legacy choose player reply must be a string"));
        QVariantList players;
        for (const QString &name : legacyPayload.toString().split(QLatin1Char('+'), Qt::KeepEmptyParts))
            players.append(name);
        object.insert(QStringLiteral("players"), players);
    }
    *v2Payload = object;
    return true;
}

bool decodeChoosePlayerReply(const QVariantMap &object, bool *hasLegacyPayload,
                             QVariant *legacyPayload, QString *error)
{
    QVariant cancelled;
    if (!requireField(object, "cancelled", FieldType::Boolean,
                      QStringLiteral("choose player reply"), &cancelled, error)) {
        return false;
    }
    if (cancelled.toBool()) {
        *hasLegacyPayload = false;
        *legacyPayload = QVariant();
        return true;
    }
    QVariant players;
    if (!requireField(object, "players", FieldType::StringArray,
                      QStringLiteral("choose player reply"), &players, error)) {
        return false;
    }
    QStringList names;
    for (const QVariant &entry : players.toList())
        names.append(entry.toString());
    *hasLegacyPayload = true;
    *legacyPayload = names.join(QLatin1Char('+'));
    return true;
}

bool encodeTriggerOrderRequest(bool hasLegacyPayload, const QVariant &legacyPayload,
                               QVariant *v2Payload, QString *error)
{
    if (!hasLegacyPayload || legacyPayload.userType() != QMetaType::QVariantList)
        return fail(error, QStringLiteral("Legacy trigger order request must be an array"));
    const QVariantList entries = legacyPayload.toList();
    if (entries.size() != 2 || entries.at(0).userType() != QMetaType::QVariantList
        || entries.at(1).userType() != QMetaType::Bool) {
        return fail(error, QStringLiteral("Legacy trigger order request has invalid fields"));
    }
    for (const QVariant &option : entries.at(0).toList()) {
        if (option.userType() != QMetaType::QVariantMap)
            return fail(error, QStringLiteral("Legacy trigger order options must be objects"));
    }
    QVariantMap object = typedObject();
    object.insert(QStringLiteral("options"), entries.at(0));
    object.insert(QStringLiteral("optional"), entries.at(1));
    *v2Payload = object;
    return true;
}

bool decodeTriggerOrderRequest(const QVariantMap &object, bool *hasLegacyPayload,
                               QVariant *legacyPayload, QString *error)
{
    if (!object.contains(QStringLiteral("options"))
        || object.value(QStringLiteral("options")).userType() != QMetaType::QVariantList) {
        return fail(error, QStringLiteral("Trigger order request options must be an array"));
    }
    const QVariantList options = object.value(QStringLiteral("options")).toList();
    for (const QVariant &option : options) {
        if (option.userType() != QMetaType::QVariantMap)
            return fail(error, QStringLiteral("Trigger order request options must be objects"));
    }
    QVariant optional;
    if (!requireField(object, "optional", FieldType::Boolean,
                      QStringLiteral("trigger order request"), &optional, error)) {
        return false;
    }
    *hasLegacyPayload = true;
    *legacyPayload = QVariantList{options, optional};
    return true;
}

bool encodeArrangeRequest(bool hasLegacyPayload, const QVariant &legacyPayload,
                          QVariant *v2Payload, QString *error)
{
    QVariantMap object = typedObject();
    if (hasLegacyPayload) {
        if (!validateArray(legacyPayload, FieldType::StringArray,
                           QStringLiteral("Legacy arrange general request"), error)) {
            return false;
        }
        object.insert(QStringLiteral("generals"), legacyPayload);
    }
    *v2Payload = object;
    return true;
}

bool decodeArrangeRequest(const QVariantMap &object, bool *hasLegacyPayload,
                          QVariant *legacyPayload, QString *error)
{
    if (!object.contains(QStringLiteral("generals")))
        return decodeNoPayload(object, hasLegacyPayload, legacyPayload);
    return decodeScalar(object, "generals", FieldType::StringArray,
                        QStringLiteral("arrange general request"),
                        hasLegacyPayload, legacyPayload, error);
}

bool encodeQmlRequest(bool hasLegacyPayload, const QVariant &legacyPayload,
                      QVariant *v2Payload, QString *error)
{
    if (!hasLegacyPayload)
        return fail(error, QStringLiteral("Legacy QML interaction request requires a payload"));
    QVariantMap object = typedObject();
    if (legacyPayload.userType() == QMetaType::QVariantList) {
        const QVariantList entries = legacyPayload.toList();
        if (entries.size() != 2 || entries.at(0).userType() != QMetaType::QString
            || entries.at(1).userType() != QMetaType::QVariantMap) {
            return fail(error, QStringLiteral("Legacy QML interaction request is invalid"));
        }
        object.insert(QStringLiteral("kind"), QStringLiteral("legacy_qml"));
        object.insert(QStringLiteral("qml_path"), entries.at(0));
        object.insert(QStringLiteral("parameters"), entries.at(1));
    } else if (legacyPayload.userType() == QMetaType::QVariantMap) {
        object.insert(QStringLiteral("kind"), QStringLiteral("structured"));
        object.insert(QStringLiteral("interaction"), legacyPayload);
    } else {
        return fail(error, QStringLiteral("Legacy QML interaction request is invalid"));
    }
    *v2Payload = object;
    return true;
}

bool decodeQmlRequest(const QVariantMap &object, bool *hasLegacyPayload,
                      QVariant *legacyPayload, QString *error)
{
    QVariant kindValue;
    if (!requireField(object, "kind", FieldType::String,
                      QStringLiteral("QML interaction request"), &kindValue, error)) {
        return false;
    }
    if (kindValue.toString() == QStringLiteral("legacy_qml")) {
        QVariant path;
        if (!requireField(object, "qml_path", FieldType::String,
                          QStringLiteral("QML interaction request"), &path, error)
            || !object.contains(QStringLiteral("parameters"))
            || object.value(QStringLiteral("parameters")).userType() != QMetaType::QVariantMap) {
            return fail(error, QStringLiteral("QML interaction parameters must be an object"));
        }
        *hasLegacyPayload = true;
        *legacyPayload = QVariantList{path, object.value(QStringLiteral("parameters"))};
        return true;
    }
    if (kindValue.toString() == QStringLiteral("structured")) {
        if (!object.contains(QStringLiteral("interaction"))
            || object.value(QStringLiteral("interaction")).userType() != QMetaType::QVariantMap) {
            return fail(error, QStringLiteral("Structured QML interaction must be an object"));
        }
        *hasLegacyPayload = true;
        *legacyPayload = object.value(QStringLiteral("interaction"));
        return true;
    }
    return fail(error, QStringLiteral("QML interaction kind is unknown"));
}

bool encodeQmlReply(bool hasLegacyPayload, const QVariant &legacyPayload,
                    QVariant *v2Payload)
{
    QVariantMap object = typedObject();
    object.insert(QStringLiteral("has_value"), hasLegacyPayload);
    if (hasLegacyPayload)
        object.insert(QStringLiteral("value"), legacyPayload);
    *v2Payload = object;
    return true;
}

bool decodeQmlReply(const QVariantMap &object, bool *hasLegacyPayload,
                    QVariant *legacyPayload, QString *error)
{
    QVariant hasValue;
    if (!requireField(object, "has_value", FieldType::Boolean,
                      QStringLiteral("QML interaction reply"), &hasValue, error)) {
        return false;
    }
    if (!hasValue.toBool()) {
        *hasLegacyPayload = false;
        *legacyPayload = QVariant();
        return true;
    }
    if (!object.contains(QStringLiteral("value")))
        return fail(error, QStringLiteral("QML interaction reply value is required"));
    *hasLegacyPayload = true;
    *legacyPayload = object.value(QStringLiteral("value"));
    return true;
}
}

bool TypedInteractionPayloads::encode(
    TypedInteractionPayloadKind kind, bool hasLegacyPayload,
    const QVariant &legacyPayload, QVariant *v2Payload, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (v2Payload == nullptr)
        return fail(error, QStringLiteral("Typed interaction payload output is null"));

    if (kind == TypedInteractionPayloadKind::ExchangeCardRequest
        || kind == TypedInteractionPayloadKind::AskPeachRequest
        || kind == TypedInteractionPayloadKind::GongxinRequest
        || kind == TypedInteractionPayloadKind::YijiRequest
        || kind == TypedInteractionPayloadKind::ResponseCardRequest
        || kind == TypedInteractionPayloadKind::DiscardCardRequest
        || kind == TypedInteractionPayloadKind::ChoosePlayerRequest
        || kind == TypedInteractionPayloadKind::NullificationRequest
        || kind == TypedInteractionPayloadKind::AmazingGraceRequest
        || kind == TypedInteractionPayloadKind::PindianRequest
        || kind == TypedInteractionPayloadKind::ChooseCardRequest
        || kind == TypedInteractionPayloadKind::ChooseRole3v3Request) {
        return encodeArray(kind, hasLegacyPayload, legacyPayload, v2Payload, error);
    }

    switch (kind) {
    case TypedInteractionPayloadKind::ChooseRoleRequest:
        return encodeNoPayload(hasLegacyPayload, QStringLiteral("choose role request"), v2Payload, error);
    case TypedInteractionPayloadKind::ChooseRoleReply:
        return encodeChooseRoleReply(hasLegacyPayload, legacyPayload, v2Payload, error);
    case TypedInteractionPayloadKind::ChooseDirectionRequest:
        return encodeNoPayload(hasLegacyPayload, QStringLiteral("choose direction request"), v2Payload, error);
    case TypedInteractionPayloadKind::ChooseDirectionReply:
        return encodeScalar(hasLegacyPayload, legacyPayload, "direction", FieldType::String,
                            QStringLiteral("choose direction reply"), v2Payload, error);
    case TypedInteractionPayloadKind::GuanxingRequest:
        return encodeGuanxingRequest(hasLegacyPayload, legacyPayload, v2Payload, error);
    case TypedInteractionPayloadKind::GuanxingReply:
        return encodeTwoIntegerLists(hasLegacyPayload, legacyPayload, "top_card_ids",
            "bottom_card_ids", QStringLiteral("guanxing reply"), v2Payload, error);
    case TypedInteractionPayloadKind::OptionalCardIdReply:
        return encodeOptionalCardId(hasLegacyPayload, legacyPayload, v2Payload, error);
    case TypedInteractionPayloadKind::YijiReply:
        return encodeYijiReply(hasLegacyPayload, legacyPayload, v2Payload, error);
    case TypedInteractionPayloadKind::PlayCardRequest:
        return encodeScalar(hasLegacyPayload, legacyPayload, "player", FieldType::String,
                            QStringLiteral("play card request"), v2Payload, error);
    case TypedInteractionPayloadKind::ResponseCardReply:
        return encodeResponseCardReply(hasLegacyPayload, legacyPayload, v2Payload, error);
    case TypedInteractionPayloadKind::CardIdsReply:
        return encodeCancelableList(hasLegacyPayload, legacyPayload, "card_ids",
            FieldType::IntegerArray, QStringLiteral("card ids reply"), v2Payload, error);
    case TypedInteractionPayloadKind::ChoosePlayerReply:
        return encodeChoosePlayerReply(hasLegacyPayload, legacyPayload, v2Payload, error);
    case TypedInteractionPayloadKind::TriggerOrderRequest:
        return encodeTriggerOrderRequest(hasLegacyPayload, legacyPayload, v2Payload, error);
    case TypedInteractionPayloadKind::TriggerOrderReply:
        return encodeScalar(hasLegacyPayload, legacyPayload, "trigger", FieldType::String,
                            QStringLiteral("trigger order reply"), v2Payload, error);
    case TypedInteractionPayloadKind::ShowCardRequest:
        return encodeScalar(hasLegacyPayload, legacyPayload, "requestor", FieldType::String,
                            QStringLiteral("show card request"), v2Payload, error);
    case TypedInteractionPayloadKind::AmazingGraceReply:
        return encodeScalar(hasLegacyPayload, legacyPayload, "card_id", FieldType::Integer,
                            QStringLiteral("amazing grace reply"), v2Payload, error);
    case TypedInteractionPayloadKind::ChooseRole3v3Reply:
        return encodeScalar(hasLegacyPayload, legacyPayload, "role", FieldType::String,
                            QStringLiteral("choose role 3v3 reply"), v2Payload, error);
    case TypedInteractionPayloadKind::LuckCardRequest:
        return encodeNoPayload(hasLegacyPayload, QStringLiteral("luck card request"), v2Payload, error);
    case TypedInteractionPayloadKind::LuckCardReply:
        return encodeScalar(hasLegacyPayload, legacyPayload, "use_luck_card", FieldType::Boolean,
                            QStringLiteral("luck card reply"), v2Payload, error);
    case TypedInteractionPayloadKind::AskGeneralRequest:
        return encodeNoPayload(hasLegacyPayload, QStringLiteral("ask general request"), v2Payload, error);
    case TypedInteractionPayloadKind::AskGeneralReply:
        return encodeScalar(hasLegacyPayload, legacyPayload, "general", FieldType::String,
                            QStringLiteral("ask general reply"), v2Payload, error);
    case TypedInteractionPayloadKind::ArrangeGeneralRequest:
        return encodeArrangeRequest(hasLegacyPayload, legacyPayload, v2Payload, error);
    case TypedInteractionPayloadKind::ArrangeGeneralReply:
        return encodeCancelableList(hasLegacyPayload, legacyPayload, "generals",
            FieldType::StringArray, QStringLiteral("arrange general reply"), v2Payload, error);
    case TypedInteractionPayloadKind::QmlInteractRequest:
        return encodeQmlRequest(hasLegacyPayload, legacyPayload, v2Payload, error);
    case TypedInteractionPayloadKind::QmlInteractReply:
        return encodeQmlReply(hasLegacyPayload, legacyPayload, v2Payload);
    default:
        break;
    }
    return fail(error, QStringLiteral("Unsupported typed interaction payload kind"));
}

bool TypedInteractionPayloads::decode(
    TypedInteractionPayloadKind kind, const QVariant &v2Payload,
    bool *hasLegacyPayload, QVariant *legacyPayload, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (hasLegacyPayload == nullptr || legacyPayload == nullptr)
        return fail(error, QStringLiteral("Typed interaction logical output is null"));

    QVariantMap object;
    if (!parseObject(v2Payload, QStringLiteral("interaction payload"), &object, error))
        return false;

    if (kind == TypedInteractionPayloadKind::ExchangeCardRequest
        || kind == TypedInteractionPayloadKind::AskPeachRequest
        || kind == TypedInteractionPayloadKind::GongxinRequest
        || kind == TypedInteractionPayloadKind::YijiRequest
        || kind == TypedInteractionPayloadKind::ResponseCardRequest
        || kind == TypedInteractionPayloadKind::DiscardCardRequest
        || kind == TypedInteractionPayloadKind::ChoosePlayerRequest
        || kind == TypedInteractionPayloadKind::NullificationRequest
        || kind == TypedInteractionPayloadKind::AmazingGraceRequest
        || kind == TypedInteractionPayloadKind::PindianRequest
        || kind == TypedInteractionPayloadKind::ChooseCardRequest
        || kind == TypedInteractionPayloadKind::ChooseRole3v3Request) {
        return decodeArray(kind, object, hasLegacyPayload, legacyPayload, error);
    }

    switch (kind) {
    case TypedInteractionPayloadKind::ChooseRoleRequest:
    case TypedInteractionPayloadKind::ChooseDirectionRequest:
    case TypedInteractionPayloadKind::LuckCardRequest:
    case TypedInteractionPayloadKind::AskGeneralRequest:
        return decodeNoPayload(object, hasLegacyPayload, legacyPayload);
    case TypedInteractionPayloadKind::ChooseRoleReply:
        return decodeChooseRoleReply(object, hasLegacyPayload, legacyPayload, error);
    case TypedInteractionPayloadKind::ChooseDirectionReply:
        return decodeScalar(object, "direction", FieldType::String,
            QStringLiteral("choose direction reply"), hasLegacyPayload, legacyPayload, error);
    case TypedInteractionPayloadKind::GuanxingRequest:
        return decodeGuanxingRequest(object, hasLegacyPayload, legacyPayload, error);
    case TypedInteractionPayloadKind::GuanxingReply:
        return decodeTwoIntegerLists(object, "top_card_ids", "bottom_card_ids",
            QStringLiteral("guanxing reply"), hasLegacyPayload, legacyPayload, error);
    case TypedInteractionPayloadKind::OptionalCardIdReply:
        return decodeOptionalCardId(object, hasLegacyPayload, legacyPayload, error);
    case TypedInteractionPayloadKind::YijiReply:
        return decodeYijiReply(object, hasLegacyPayload, legacyPayload, error);
    case TypedInteractionPayloadKind::PlayCardRequest:
        return decodeScalar(object, "player", FieldType::String,
            QStringLiteral("play card request"), hasLegacyPayload, legacyPayload, error);
    case TypedInteractionPayloadKind::ResponseCardReply:
        return decodeResponseCardReply(object, hasLegacyPayload, legacyPayload, error);
    case TypedInteractionPayloadKind::CardIdsReply:
        return decodeCancelableList(object, "card_ids", FieldType::IntegerArray,
            QStringLiteral("card ids reply"), hasLegacyPayload, legacyPayload, error);
    case TypedInteractionPayloadKind::ChoosePlayerReply:
        return decodeChoosePlayerReply(object, hasLegacyPayload, legacyPayload, error);
    case TypedInteractionPayloadKind::TriggerOrderRequest:
        return decodeTriggerOrderRequest(object, hasLegacyPayload, legacyPayload, error);
    case TypedInteractionPayloadKind::TriggerOrderReply:
        return decodeScalar(object, "trigger", FieldType::String,
            QStringLiteral("trigger order reply"), hasLegacyPayload, legacyPayload, error);
    case TypedInteractionPayloadKind::ShowCardRequest:
        return decodeScalar(object, "requestor", FieldType::String,
            QStringLiteral("show card request"), hasLegacyPayload, legacyPayload, error);
    case TypedInteractionPayloadKind::AmazingGraceReply:
        return decodeScalar(object, "card_id", FieldType::Integer,
            QStringLiteral("amazing grace reply"), hasLegacyPayload, legacyPayload, error);
    case TypedInteractionPayloadKind::ChooseRole3v3Reply:
        return decodeScalar(object, "role", FieldType::String,
            QStringLiteral("choose role 3v3 reply"), hasLegacyPayload, legacyPayload, error);
    case TypedInteractionPayloadKind::LuckCardReply:
        return decodeScalar(object, "use_luck_card", FieldType::Boolean,
            QStringLiteral("luck card reply"), hasLegacyPayload, legacyPayload, error);
    case TypedInteractionPayloadKind::AskGeneralReply:
        return decodeScalar(object, "general", FieldType::String,
            QStringLiteral("ask general reply"), hasLegacyPayload, legacyPayload, error);
    case TypedInteractionPayloadKind::ArrangeGeneralRequest:
        return decodeArrangeRequest(object, hasLegacyPayload, legacyPayload, error);
    case TypedInteractionPayloadKind::ArrangeGeneralReply:
        return decodeCancelableList(object, "generals", FieldType::StringArray,
            QStringLiteral("arrange general reply"), hasLegacyPayload, legacyPayload, error);
    case TypedInteractionPayloadKind::QmlInteractRequest:
        return decodeQmlRequest(object, hasLegacyPayload, legacyPayload, error);
    case TypedInteractionPayloadKind::QmlInteractReply:
        return decodeQmlReply(object, hasLegacyPayload, legacyPayload, error);
    default:
        break;
    }
    return fail(error, QStringLiteral("Unsupported typed interaction payload kind"));
}
