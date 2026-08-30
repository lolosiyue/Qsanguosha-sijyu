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

bool encodeArray(TypedInteractionPayloadKind kind, bool hasDomainPayload,
                 const QVariant &domainPayload, QVariant *v2Payload, QString *error)
{
    const FieldSpec *fields = nullptr;
    int fieldCount = 0;
    int requiredCount = 0;
    QString context;
    if (!arraySchema(kind, &fields, &fieldCount, &requiredCount, &context))
        return false;
    if (!hasDomainPayload || domainPayload.userType() != QMetaType::QVariantList)
        return fail(error, QStringLiteral("Domain %1 must be an array").arg(context));
    const QVariantList entries = domainPayload.toList();
    if (entries.size() < requiredCount || entries.size() > fieldCount)
        return fail(error, QStringLiteral("Domain %1 has the wrong field count").arg(context));

    QVariantMap object = typedObject();
    for (int index = 0; index < entries.size(); ++index) {
        const QString fieldName = QString::fromLatin1(fields[index].name);
        if (!validateField(entries.at(index), fields[index].type,
                           QStringLiteral("Domain %1 %2").arg(context, fieldName), error)) {
            return false;
        }
        object.insert(fieldName, entries.at(index));
    }
    *v2Payload = object;
    return true;
}

bool decodeArray(TypedInteractionPayloadKind kind, const QVariantMap &object,
                 bool *hasDomainPayload, QVariant *domainPayload, QString *error)
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
    *hasDomainPayload = true;
    *domainPayload = entries;
    return true;
}

bool encodeNoPayload(bool hasDomainPayload, const QString &context,
                     QVariant *v2Payload, QString *error)
{
    if (hasDomainPayload)
        return fail(error, QStringLiteral("Domain %1 must not contain a payload").arg(context));
    *v2Payload = typedObject();
    return true;
}

bool decodeNoPayload(const QVariantMap &, bool *hasDomainPayload,
                     QVariant *domainPayload)
{
    *hasDomainPayload = false;
    *domainPayload = QVariant();
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

bool encodeScalar(bool hasDomainPayload, const QVariant &domainPayload,
                  const char *fieldName, FieldType type, const QString &context,
                  QVariant *v2Payload, QString *error)
{
    if (!hasDomainPayload)
        return fail(error, QStringLiteral("Domain %1 requires a payload").arg(context));
    if (!validateField(domainPayload, type, QStringLiteral("Domain %1").arg(context), error))
        return false;
    QVariantMap object = typedObject();
    object.insert(QString::fromLatin1(fieldName), domainPayload);
    *v2Payload = object;
    return true;
}

bool decodeScalar(const QVariantMap &object, const char *fieldName, FieldType type,
                  const QString &context, bool *hasDomainPayload,
                  QVariant *domainPayload, QString *error)
{
    QVariant value;
    if (!requireField(object, fieldName, type, context, &value, error))
        return false;
    *hasDomainPayload = true;
    *domainPayload = value;
    return true;
}

bool encodeOptionalCardId(bool hasDomainPayload, const QVariant &domainPayload,
                          QVariant *v2Payload, QString *error)
{
    QVariantMap object = typedObject();
    object.insert(QStringLiteral("cancelled"), !hasDomainPayload);
    if (hasDomainPayload) {
        if (!validateField(domainPayload, FieldType::Integer,
                           QStringLiteral("Domain card id reply"), error)) {
            return false;
        }
        object.insert(QStringLiteral("card_id"), domainPayload);
    }
    *v2Payload = object;
    return true;
}

bool decodeOptionalCardId(const QVariantMap &object, bool *hasDomainPayload,
                          QVariant *domainPayload, QString *error)
{
    QVariant cancelledValue;
    if (!requireField(object, "cancelled", FieldType::Boolean,
                      QStringLiteral("card id reply"), &cancelledValue, error)) {
        return false;
    }
    if (cancelledValue.toBool()) {
        *hasDomainPayload = false;
        *domainPayload = QVariant();
        return true;
    }
    return decodeScalar(object, "card_id", FieldType::Integer,
                        QStringLiteral("card id reply"),
                        hasDomainPayload, domainPayload, error);
}

bool encodeCancelableList(bool hasDomainPayload, const QVariant &domainPayload,
                          const char *fieldName, FieldType type,
                          const QString &context, QVariant *v2Payload, QString *error)
{
    QVariantMap object = typedObject();
    object.insert(QStringLiteral("cancelled"), !hasDomainPayload);
    if (hasDomainPayload) {
        if (!validateField(domainPayload, type,
                           QStringLiteral("Domain %1").arg(context), error)) {
            return false;
        }
        object.insert(QString::fromLatin1(fieldName), domainPayload);
    }
    *v2Payload = object;
    return true;
}

bool decodeCancelableList(const QVariantMap &object, const char *fieldName,
                          FieldType type, const QString &context,
                          bool *hasDomainPayload, QVariant *domainPayload, QString *error)
{
    QVariant cancelledValue;
    if (!requireField(object, "cancelled", FieldType::Boolean,
                      context, &cancelledValue, error)) {
        return false;
    }
    if (cancelledValue.toBool()) {
        *hasDomainPayload = false;
        *domainPayload = QVariant();
        return true;
    }
    return decodeScalar(object, fieldName, type, context,
                        hasDomainPayload, domainPayload, error);
}

bool encodeChooseRoleReply(bool hasDomainPayload, const QVariant &domainPayload,
                           QVariant *v2Payload, QString *error)
{
    QVariantMap object = typedObject();
    object.insert(QStringLiteral("cancelled"), !hasDomainPayload);
    if (!hasDomainPayload) {
        *v2Payload = object;
        return true;
    }
    if (domainPayload.userType() != QMetaType::QVariantList)
        return fail(error, QStringLiteral("Domain choose role reply must be an array"));
    const QVariantList entries = domainPayload.toList();
    if (entries.size() != 2
        || !validateArray(entries.at(0), FieldType::StringArray,
                          QStringLiteral("Domain choose role players"), error)
        || !validateArray(entries.at(1), FieldType::StringArray,
                          QStringLiteral("Domain choose role roles"), error)) {
        return false;
    }
    object.insert(QStringLiteral("players"), entries.at(0));
    object.insert(QStringLiteral("roles"), entries.at(1));
    *v2Payload = object;
    return true;
}

bool decodeChooseRoleReply(const QVariantMap &object, bool *hasDomainPayload,
                           QVariant *domainPayload, QString *error)
{
    QVariant cancelled;
    if (!requireField(object, "cancelled", FieldType::Boolean,
                      QStringLiteral("choose role reply"), &cancelled, error)) {
        return false;
    }
    if (cancelled.toBool()) {
        *hasDomainPayload = false;
        *domainPayload = QVariant();
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
    *hasDomainPayload = true;
    *domainPayload = QVariantList{players, roles};
    return true;
}

bool encodeGuanxingRequest(bool hasDomainPayload, const QVariant &domainPayload,
                           QVariant *v2Payload, QString *error)
{
    if (!hasDomainPayload || domainPayload.userType() != QMetaType::QVariantList)
        return fail(error, QStringLiteral("Domain guanxing request must be an array"));
    const QVariantList entries = domainPayload.toList();
    if (entries.isEmpty() || entries.size() > 2
        || !validateArray(entries.at(0), FieldType::IntegerArray,
                          QStringLiteral("Domain guanxing card_ids"), error)) {
        return false;
    }
    QVariantMap object = typedObject();
    object.insert(QStringLiteral("card_ids"), entries.at(0));
    if (entries.size() == 2) {
        int mode = 0;
        if (!isInteger(entries.at(1), &mode) || mode < -1 || mode > 1)
            return fail(error, QStringLiteral("Domain guanxing mode is unknown"));
        object.insert(QStringLiteral("mode"), mode > 0 ? QStringLiteral("up_only")
            : mode < 0 ? QStringLiteral("down_only") : QStringLiteral("both_sides"));
    }
    *v2Payload = object;
    return true;
}

bool decodeGuanxingRequest(const QVariantMap &object, bool *hasDomainPayload,
                           QVariant *domainPayload, QString *error)
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
    *hasDomainPayload = true;
    *domainPayload = entries;
    return true;
}

bool encodeTwoIntegerLists(bool hasDomainPayload, const QVariant &domainPayload,
                           const char *firstName, const char *secondName,
                           const QString &context, QVariant *v2Payload, QString *error)
{
    if (!hasDomainPayload || domainPayload.userType() != QMetaType::QVariantList)
        return fail(error, QStringLiteral("Domain %1 must be an array").arg(context));
    const QVariantList entries = domainPayload.toList();
    if (entries.size() != 2
        || !validateArray(entries.at(0), FieldType::IntegerArray,
                          QStringLiteral("Domain %1 first array").arg(context), error)
        || !validateArray(entries.at(1), FieldType::IntegerArray,
                          QStringLiteral("Domain %1 second array").arg(context), error)) {
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
                           const QString &context, bool *hasDomainPayload,
                           QVariant *domainPayload, QString *error)
{
    QVariant first;
    QVariant second;
    if (!requireField(object, firstName, FieldType::IntegerArray, context, &first, error)
        || !requireField(object, secondName, FieldType::IntegerArray,
                         context, &second, error)) {
        return false;
    }
    *hasDomainPayload = true;
    *domainPayload = QVariantList{first, second};
    return true;
}

bool encodeYijiReply(bool hasDomainPayload, const QVariant &domainPayload,
                     QVariant *v2Payload, QString *error)
{
    QVariantMap object = typedObject();
    object.insert(QStringLiteral("cancelled"), !hasDomainPayload);
    if (!hasDomainPayload) {
        *v2Payload = object;
        return true;
    }
    if (domainPayload.userType() != QMetaType::QVariantList)
        return fail(error, QStringLiteral("Domain yiji reply must be an array"));
    const QVariantList entries = domainPayload.toList();
    if (entries.size() != 2
        || !validateArray(entries.at(0), FieldType::IntegerArray,
                          QStringLiteral("Domain yiji card_ids"), error)
        || entries.at(1).userType() != QMetaType::QString) {
        return fail(error, QStringLiteral("Domain yiji reply has invalid fields"));
    }
    object.insert(QStringLiteral("card_ids"), entries.at(0));
    object.insert(QStringLiteral("target_player"), entries.at(1));
    *v2Payload = object;
    return true;
}

bool decodeYijiReply(const QVariantMap &object, bool *hasDomainPayload,
                     QVariant *domainPayload, QString *error)
{
    QVariant cancelled;
    if (!requireField(object, "cancelled", FieldType::Boolean,
                      QStringLiteral("yiji reply"), &cancelled, error)) {
        return false;
    }
    if (cancelled.toBool()) {
        *hasDomainPayload = false;
        *domainPayload = QVariant();
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
    *hasDomainPayload = true;
    *domainPayload = QVariantList{cardIds, target};
    return true;
}

bool encodeResponseCardReply(bool hasDomainPayload, const QVariant &domainPayload,
                             QVariant *v2Payload, QString *error)
{
    QVariantMap object = typedObject();
    object.insert(QStringLiteral("cancelled"), !hasDomainPayload);
    if (!hasDomainPayload) {
        *v2Payload = object;
        return true;
    }
    if (domainPayload.userType() != QMetaType::QVariantList)
        return fail(error, QStringLiteral("Domain response card reply must be an array"));
    const QVariantList entries = domainPayload.toList();
    if (entries.size() != 4
        || entries.at(0).userType() != QMetaType::QString
        || !validateArray(entries.at(1), FieldType::StringArray,
                          QStringLiteral("Domain response card targets"), error)
        || entries.at(2).userType() != QMetaType::QString
        || !isInteger(entries.at(3))) {
        return fail(error, QStringLiteral("Domain response card reply has invalid fields"));
    }
    object.insert(QStringLiteral("card_text"), entries.at(0));
    object.insert(QStringLiteral("targets"), entries.at(1));
    object.insert(QStringLiteral("activation_skill_name"), entries.at(2));
    object.insert(QStringLiteral("activation_skill_instance_id"), entries.at(3));
    *v2Payload = object;
    return true;
}

bool decodeResponseCardReply(const QVariantMap &object, bool *hasDomainPayload,
                             QVariant *domainPayload, QString *error)
{
    QVariant cancelled;
    if (!requireField(object, "cancelled", FieldType::Boolean,
                      QStringLiteral("response card reply"), &cancelled, error)) {
        return false;
    }
    if (cancelled.toBool()) {
        *hasDomainPayload = false;
        *domainPayload = QVariant();
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
    *hasDomainPayload = true;
    *domainPayload = QVariantList{cardText, targets, skillName, instanceId};
    return true;
}

bool encodeChoosePlayerReply(bool hasDomainPayload, const QVariant &domainPayload,
                             QVariant *v2Payload, QString *error)
{
    QVariantMap object = typedObject();
    object.insert(QStringLiteral("cancelled"), !hasDomainPayload);
    if (hasDomainPayload) {
        if (domainPayload.userType() != QMetaType::QString)
            return fail(error, QStringLiteral("Domain choose player reply must be a string"));
        QVariantList players;
        for (const QString &name : domainPayload.toString().split(QLatin1Char('+'), Qt::KeepEmptyParts))
            players.append(name);
        object.insert(QStringLiteral("players"), players);
    }
    *v2Payload = object;
    return true;
}

bool decodeChoosePlayerReply(const QVariantMap &object, bool *hasDomainPayload,
                             QVariant *domainPayload, QString *error)
{
    QVariant cancelled;
    if (!requireField(object, "cancelled", FieldType::Boolean,
                      QStringLiteral("choose player reply"), &cancelled, error)) {
        return false;
    }
    if (cancelled.toBool()) {
        *hasDomainPayload = false;
        *domainPayload = QVariant();
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
    *hasDomainPayload = true;
    *domainPayload = names.join(QLatin1Char('+'));
    return true;
}

bool encodeTriggerOrderRequest(bool hasDomainPayload, const QVariant &domainPayload,
                               QVariant *v2Payload, QString *error)
{
    if (!hasDomainPayload || domainPayload.userType() != QMetaType::QVariantList)
        return fail(error, QStringLiteral("Domain trigger order request must be an array"));
    const QVariantList entries = domainPayload.toList();
    if (entries.size() != 2 || entries.at(0).userType() != QMetaType::QVariantList
        || entries.at(1).userType() != QMetaType::Bool) {
        return fail(error, QStringLiteral("Domain trigger order request has invalid fields"));
    }
    for (const QVariant &option : entries.at(0).toList()) {
        if (option.userType() != QMetaType::QVariantMap)
            return fail(error, QStringLiteral("Domain trigger order options must be objects"));
    }
    QVariantMap object = typedObject();
    object.insert(QStringLiteral("options"), entries.at(0));
    object.insert(QStringLiteral("optional"), entries.at(1));
    *v2Payload = object;
    return true;
}

bool decodeTriggerOrderRequest(const QVariantMap &object, bool *hasDomainPayload,
                               QVariant *domainPayload, QString *error)
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
    *hasDomainPayload = true;
    *domainPayload = QVariantList{options, optional};
    return true;
}

bool encodeArrangeRequest(bool hasDomainPayload, const QVariant &domainPayload,
                          QVariant *v2Payload, QString *error)
{
    QVariantMap object = typedObject();
    if (hasDomainPayload) {
        if (!validateArray(domainPayload, FieldType::StringArray,
                           QStringLiteral("Domain arrange general request"), error)) {
            return false;
        }
        object.insert(QStringLiteral("generals"), domainPayload);
    }
    *v2Payload = object;
    return true;
}

bool decodeArrangeRequest(const QVariantMap &object, bool *hasDomainPayload,
                          QVariant *domainPayload, QString *error)
{
    if (!object.contains(QStringLiteral("generals")))
        return decodeNoPayload(object, hasDomainPayload, domainPayload);
    return decodeScalar(object, "generals", FieldType::StringArray,
                        QStringLiteral("arrange general request"),
                        hasDomainPayload, domainPayload, error);
}

bool encodeQmlRequest(bool hasDomainPayload, const QVariant &domainPayload,
                      QVariant *v2Payload, QString *error)
{
    if (!hasDomainPayload)
        return fail(error, QStringLiteral("QML interaction request requires a payload"));
    QVariantMap object = typedObject();
    if (domainPayload.userType() != QMetaType::QVariantMap)
        return fail(error, QStringLiteral("QML interaction request must be an object"));
    object.insert(QStringLiteral("interaction"), domainPayload);
    *v2Payload = object;
    return true;
}

bool decodeQmlRequest(const QVariantMap &object, bool *hasDomainPayload,
                      QVariant *domainPayload, QString *error)
{
    if (!object.contains(QStringLiteral("interaction"))
        || object.value(QStringLiteral("interaction")).userType() != QMetaType::QVariantMap) {
        return fail(error, QStringLiteral("QML interaction must be an object"));
    }
    *hasDomainPayload = true;
    *domainPayload = object.value(QStringLiteral("interaction"));
    return true;
}

bool encodeQmlReply(bool hasDomainPayload, const QVariant &domainPayload,
                    QVariant *v2Payload)
{
    QVariantMap object = typedObject();
    object.insert(QStringLiteral("has_value"), hasDomainPayload);
    if (hasDomainPayload)
        object.insert(QStringLiteral("value"), domainPayload);
    *v2Payload = object;
    return true;
}

bool decodeQmlReply(const QVariantMap &object, bool *hasDomainPayload,
                    QVariant *domainPayload, QString *error)
{
    QVariant hasValue;
    if (!requireField(object, "has_value", FieldType::Boolean,
                      QStringLiteral("QML interaction reply"), &hasValue, error)) {
        return false;
    }
    if (!hasValue.toBool()) {
        *hasDomainPayload = false;
        *domainPayload = QVariant();
        return true;
    }
    if (!object.contains(QStringLiteral("value")))
        return fail(error, QStringLiteral("QML interaction reply value is required"));
    *hasDomainPayload = true;
    *domainPayload = object.value(QStringLiteral("value"));
    return true;
}
}

bool TypedInteractionPayloads::encode(
    TypedInteractionPayloadKind kind, bool hasDomainPayload,
    const QVariant &domainPayload, QVariant *v2Payload, QString *error)
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
        return encodeArray(kind, hasDomainPayload, domainPayload, v2Payload, error);
    }

    switch (kind) {
    case TypedInteractionPayloadKind::ChooseRoleRequest:
        return encodeNoPayload(hasDomainPayload, QStringLiteral("choose role request"), v2Payload, error);
    case TypedInteractionPayloadKind::ChooseRoleReply:
        return encodeChooseRoleReply(hasDomainPayload, domainPayload, v2Payload, error);
    case TypedInteractionPayloadKind::ChooseDirectionRequest:
        return encodeNoPayload(hasDomainPayload, QStringLiteral("choose direction request"), v2Payload, error);
    case TypedInteractionPayloadKind::ChooseDirectionReply:
        return encodeScalar(hasDomainPayload, domainPayload, "direction", FieldType::String,
                            QStringLiteral("choose direction reply"), v2Payload, error);
    case TypedInteractionPayloadKind::GuanxingRequest:
        return encodeGuanxingRequest(hasDomainPayload, domainPayload, v2Payload, error);
    case TypedInteractionPayloadKind::GuanxingReply:
        return encodeTwoIntegerLists(hasDomainPayload, domainPayload, "top_card_ids",
            "bottom_card_ids", QStringLiteral("guanxing reply"), v2Payload, error);
    case TypedInteractionPayloadKind::OptionalCardIdReply:
        return encodeOptionalCardId(hasDomainPayload, domainPayload, v2Payload, error);
    case TypedInteractionPayloadKind::YijiReply:
        return encodeYijiReply(hasDomainPayload, domainPayload, v2Payload, error);
    case TypedInteractionPayloadKind::PlayCardRequest:
        return encodeScalar(hasDomainPayload, domainPayload, "player", FieldType::String,
                            QStringLiteral("play card request"), v2Payload, error);
    case TypedInteractionPayloadKind::ResponseCardReply:
        return encodeResponseCardReply(hasDomainPayload, domainPayload, v2Payload, error);
    case TypedInteractionPayloadKind::CardIdsReply:
        return encodeCancelableList(hasDomainPayload, domainPayload, "card_ids",
            FieldType::IntegerArray, QStringLiteral("card ids reply"), v2Payload, error);
    case TypedInteractionPayloadKind::ChoosePlayerReply:
        return encodeChoosePlayerReply(hasDomainPayload, domainPayload, v2Payload, error);
    case TypedInteractionPayloadKind::TriggerOrderRequest:
        return encodeTriggerOrderRequest(hasDomainPayload, domainPayload, v2Payload, error);
    case TypedInteractionPayloadKind::TriggerOrderReply:
        return encodeScalar(hasDomainPayload, domainPayload, "trigger", FieldType::String,
                            QStringLiteral("trigger order reply"), v2Payload, error);
    case TypedInteractionPayloadKind::ShowCardRequest:
        return encodeScalar(hasDomainPayload, domainPayload, "requestor", FieldType::String,
                            QStringLiteral("show card request"), v2Payload, error);
    case TypedInteractionPayloadKind::AmazingGraceReply:
        return encodeOptionalCardId(hasDomainPayload, domainPayload, v2Payload, error);
    case TypedInteractionPayloadKind::ChooseRole3v3Reply:
        return encodeScalar(hasDomainPayload, domainPayload, "role", FieldType::String,
                            QStringLiteral("choose role 3v3 reply"), v2Payload, error);
    case TypedInteractionPayloadKind::LuckCardRequest:
        return encodeNoPayload(hasDomainPayload, QStringLiteral("luck card request"), v2Payload, error);
    case TypedInteractionPayloadKind::LuckCardReply:
        return encodeScalar(hasDomainPayload, domainPayload, "use_luck_card", FieldType::Boolean,
                            QStringLiteral("luck card reply"), v2Payload, error);
    case TypedInteractionPayloadKind::AskGeneralRequest:
        return encodeNoPayload(hasDomainPayload, QStringLiteral("ask general request"), v2Payload, error);
    case TypedInteractionPayloadKind::AskGeneralReply:
        return encodeScalar(hasDomainPayload, domainPayload, "general", FieldType::String,
                            QStringLiteral("ask general reply"), v2Payload, error);
    case TypedInteractionPayloadKind::ArrangeGeneralRequest:
        return encodeArrangeRequest(hasDomainPayload, domainPayload, v2Payload, error);
    case TypedInteractionPayloadKind::ArrangeGeneralReply:
        return encodeCancelableList(hasDomainPayload, domainPayload, "generals",
            FieldType::StringArray, QStringLiteral("arrange general reply"), v2Payload, error);
    case TypedInteractionPayloadKind::QmlInteractRequest:
        return encodeQmlRequest(hasDomainPayload, domainPayload, v2Payload, error);
    case TypedInteractionPayloadKind::QmlInteractReply:
        return encodeQmlReply(hasDomainPayload, domainPayload, v2Payload);
    default:
        break;
    }
    return fail(error, QStringLiteral("Unsupported typed interaction payload kind"));
}

bool TypedInteractionPayloads::decode(
    TypedInteractionPayloadKind kind, const QVariant &v2Payload,
    bool *hasDomainPayload, QVariant *domainPayload, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (hasDomainPayload == nullptr || domainPayload == nullptr)
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
        return decodeArray(kind, object, hasDomainPayload, domainPayload, error);
    }

    switch (kind) {
    case TypedInteractionPayloadKind::ChooseRoleRequest:
    case TypedInteractionPayloadKind::ChooseDirectionRequest:
    case TypedInteractionPayloadKind::LuckCardRequest:
    case TypedInteractionPayloadKind::AskGeneralRequest:
        return decodeNoPayload(object, hasDomainPayload, domainPayload);
    case TypedInteractionPayloadKind::ChooseRoleReply:
        return decodeChooseRoleReply(object, hasDomainPayload, domainPayload, error);
    case TypedInteractionPayloadKind::ChooseDirectionReply:
        return decodeScalar(object, "direction", FieldType::String,
            QStringLiteral("choose direction reply"), hasDomainPayload, domainPayload, error);
    case TypedInteractionPayloadKind::GuanxingRequest:
        return decodeGuanxingRequest(object, hasDomainPayload, domainPayload, error);
    case TypedInteractionPayloadKind::GuanxingReply:
        return decodeTwoIntegerLists(object, "top_card_ids", "bottom_card_ids",
            QStringLiteral("guanxing reply"), hasDomainPayload, domainPayload, error);
    case TypedInteractionPayloadKind::OptionalCardIdReply:
        return decodeOptionalCardId(object, hasDomainPayload, domainPayload, error);
    case TypedInteractionPayloadKind::YijiReply:
        return decodeYijiReply(object, hasDomainPayload, domainPayload, error);
    case TypedInteractionPayloadKind::PlayCardRequest:
        return decodeScalar(object, "player", FieldType::String,
            QStringLiteral("play card request"), hasDomainPayload, domainPayload, error);
    case TypedInteractionPayloadKind::ResponseCardReply:
        return decodeResponseCardReply(object, hasDomainPayload, domainPayload, error);
    case TypedInteractionPayloadKind::CardIdsReply:
        return decodeCancelableList(object, "card_ids", FieldType::IntegerArray,
            QStringLiteral("card ids reply"), hasDomainPayload, domainPayload, error);
    case TypedInteractionPayloadKind::ChoosePlayerReply:
        return decodeChoosePlayerReply(object, hasDomainPayload, domainPayload, error);
    case TypedInteractionPayloadKind::TriggerOrderRequest:
        return decodeTriggerOrderRequest(object, hasDomainPayload, domainPayload, error);
    case TypedInteractionPayloadKind::TriggerOrderReply:
        return decodeScalar(object, "trigger", FieldType::String,
            QStringLiteral("trigger order reply"), hasDomainPayload, domainPayload, error);
    case TypedInteractionPayloadKind::ShowCardRequest:
        return decodeScalar(object, "requestor", FieldType::String,
            QStringLiteral("show card request"), hasDomainPayload, domainPayload, error);
    case TypedInteractionPayloadKind::AmazingGraceReply:
        return decodeOptionalCardId(object, hasDomainPayload, domainPayload, error);
    case TypedInteractionPayloadKind::ChooseRole3v3Reply:
        return decodeScalar(object, "role", FieldType::String,
            QStringLiteral("choose role 3v3 reply"), hasDomainPayload, domainPayload, error);
    case TypedInteractionPayloadKind::LuckCardReply:
        return decodeScalar(object, "use_luck_card", FieldType::Boolean,
            QStringLiteral("luck card reply"), hasDomainPayload, domainPayload, error);
    case TypedInteractionPayloadKind::AskGeneralReply:
        return decodeScalar(object, "general", FieldType::String,
            QStringLiteral("ask general reply"), hasDomainPayload, domainPayload, error);
    case TypedInteractionPayloadKind::ArrangeGeneralRequest:
        return decodeArrangeRequest(object, hasDomainPayload, domainPayload, error);
    case TypedInteractionPayloadKind::ArrangeGeneralReply:
        return decodeCancelableList(object, "generals", FieldType::StringArray,
            QStringLiteral("arrange general reply"), hasDomainPayload, domainPayload, error);
    case TypedInteractionPayloadKind::QmlInteractRequest:
        return decodeQmlRequest(object, hasDomainPayload, domainPayload, error);
    case TypedInteractionPayloadKind::QmlInteractReply:
        return decodeQmlReply(object, hasDomainPayload, domainPayload, error);
    default:
        break;
    }
    return fail(error, QStringLiteral("Unsupported typed interaction payload kind"));
}
