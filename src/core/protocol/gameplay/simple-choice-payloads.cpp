#include "simple-choice-payloads.h"

#include "json.h"
#include "protocol.h"

#include <QMetaType>

#include <cmath>
#include <limits>

using namespace QSanProtocol;

namespace
{
bool fail(QString *error, const QString &detail)
{
    if (error != nullptr)
        *error = detail;
    return false;
}

bool isSchemaVersionOne(const QVariant &value)
{
    bool ok = false;
    const double number = value.toDouble(&ok);
    return JsonUtils::isNumber(value) && ok && std::isfinite(number)
        && std::trunc(number) == number && number == 1.0;
}

bool parseObject(const QVariant &value, const QString &context,
                 QVariantMap *object, QString *error)
{
    if (value.userType() != QMetaType::QVariantMap) {
        return fail(error,
                    QStringLiteral("Protocol V2 %1 must be an object").arg(context));
    }

    const QVariantMap parsed = value.toMap();
    if (!parsed.contains(QStringLiteral("schema_version"))) {
        return fail(error,
                    QStringLiteral("%1 schema_version is required").arg(context));
    }
    if (!isSchemaVersionOne(parsed.value(QStringLiteral("schema_version")))) {
        return fail(error,
                    QStringLiteral("%1 schema_version must be 1").arg(context));
    }

    *object = parsed;
    return true;
}

bool requireString(const QVariantMap &object, const QString &key,
                   const QString &context, QString *value, QString *error)
{
    if (!object.contains(key))
        return fail(error, QStringLiteral("%1 %2 is required").arg(context, key));
    const QVariant field = object.value(key);
    if (field.userType() != QMetaType::QString) {
        return fail(error,
                    QStringLiteral("%1 %2 must be a string").arg(context, key));
    }
    *value = field.toString();
    return true;
}

bool requireBool(const QVariantMap &object, const QString &key,
                 const QString &context, bool *value, QString *error)
{
    if (!object.contains(key))
        return fail(error, QStringLiteral("%1 %2 is required").arg(context, key));
    const QVariant field = object.value(key);
    if (field.userType() != QMetaType::Bool) {
        return fail(error,
                    QStringLiteral("%1 %2 must be a boolean").arg(context, key));
    }
    *value = field.toBool();
    return true;
}

QVariantList stringListVariant(const QStringList &values)
{
    QVariantList result;
    result.reserve(values.size());
    for (const QString &value : values)
        result.append(value);
    return result;
}

bool requireStringList(const QVariantMap &object, const QString &key,
                       const QString &context, QStringList *values,
                       QString *error)
{
    if (!object.contains(key))
        return fail(error, QStringLiteral("%1 %2 is required").arg(context, key));

    const QVariant field = object.value(key);
    if (field.userType() != QMetaType::QVariantList) {
        return fail(error,
                    QStringLiteral("%1 %2 must be an array of strings").arg(context, key));
    }

    QStringList parsed;
    const QVariantList entries = field.toList();
    parsed.reserve(entries.size());
    for (const QVariant &entry : entries) {
        if (entry.userType() != QMetaType::QString) {
            return fail(error,
                        QStringLiteral("%1 %2 must be an array of strings")
                            .arg(context, key));
        }
        parsed.append(entry.toString());
    }
    *values = parsed;
    return true;
}

bool parseLegacyString(const QVariant &value, const QString &context,
                       QString *parsed, QString *error)
{
    if (value.userType() != QMetaType::QString)
        return fail(error, QStringLiteral("Legacy %1 must be a string").arg(context));
    *parsed = value.toString();
    return true;
}

bool parseLegacyBool(const QVariant &value, const QString &context,
                     bool *parsed, QString *error)
{
    if (value.userType() != QMetaType::Bool)
        return fail(error, QStringLiteral("Legacy %1 must be a boolean").arg(context));
    *parsed = value.toBool();
    return true;
}

bool parseLegacyInteger(const QVariant &value, const QString &context,
                        int *parsed, QString *error)
{
    bool ok = false;
    const double number = value.toDouble(&ok);
    if (!JsonUtils::isNumber(value) || !ok || !std::isfinite(number)
        || std::trunc(number) != number
        || number < std::numeric_limits<int>::min()
        || number > std::numeric_limits<int>::max()) {
        return fail(error, QStringLiteral("Legacy %1 must be an integer").arg(context));
    }
    *parsed = static_cast<int>(number);
    return true;
}

bool parseLegacyStringArray(const QVariant &value, const QString &context,
                            QStringList *parsed, QString *error)
{
    if (value.userType() != QMetaType::QVariantList) {
        return fail(error,
                    QStringLiteral("Legacy %1 must be an array of strings").arg(context));
    }

    QStringList result;
    const QVariantList entries = value.toList();
    result.reserve(entries.size());
    for (const QVariant &entry : entries) {
        if (entry.userType() != QMetaType::QString) {
            return fail(error,
                        QStringLiteral("Legacy %1 must be an array of strings")
                            .arg(context));
        }
        result.append(entry.toString());
    }
    *parsed = result;
    return true;
}

bool parseLegacyTwoStrings(const QVariant &value, const QString &context,
                           QString *first, QString *second, QString *error)
{
    if (value.userType() != QMetaType::QVariantList)
        return fail(error, QStringLiteral("Legacy %1 must be an array").arg(context));
    const QVariantList entries = value.toList();
    if (entries.size() != 2 || entries.at(0).userType() != QMetaType::QString
        || entries.at(1).userType() != QMetaType::QString) {
        return fail(error,
                    QStringLiteral("Legacy %1 must contain two strings").arg(context));
    }
    *first = entries.at(0).toString();
    *second = entries.at(1).toString();
    return true;
}

QString orderReasonName(int reason)
{
    if (reason == S_REASON_CHOOSE_ORDER_TURN)
        return QStringLiteral("turn");
    if (reason == S_REASON_CHOOSE_ORDER_SELECT)
        return QStringLiteral("select");
    return QString();
}

bool parseOrderReason(const QString &name, int *reason, QString *error)
{
    if (name == QStringLiteral("turn")) {
        *reason = S_REASON_CHOOSE_ORDER_TURN;
        return true;
    }
    if (name == QStringLiteral("select")) {
        *reason = S_REASON_CHOOSE_ORDER_SELECT;
        return true;
    }
    return fail(error, QStringLiteral("Choose order reason is unknown"));
}

QString campName(int camp)
{
    if (camp == S_CAMP_WARM)
        return QStringLiteral("warm");
    if (camp == S_CAMP_COOL)
        return QStringLiteral("cool");
    return QString();
}

bool parseCamp(const QString &name, int *camp, QString *error)
{
    if (name == QStringLiteral("warm")) {
        *camp = S_CAMP_WARM;
        return true;
    }
    if (name == QStringLiteral("cool")) {
        *camp = S_CAMP_COOL;
        return true;
    }
    return fail(error, QStringLiteral("Choose order camp is unknown"));
}
}

QVariant ChooseGeneralRequestPayload::toLegacyVariant() const
{
    return stringListVariant(candidates);
}

QVariantMap ChooseGeneralRequestPayload::toV2Variant() const
{
    return QVariantMap{
        {QStringLiteral("schema_version"), SchemaVersion},
        {QStringLiteral("candidates"), stringListVariant(candidates)}
    };
}

bool ChooseGeneralRequestPayload::parseLegacy(
    const QVariant &value, ChooseGeneralRequestPayload *payload, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (payload == nullptr)
        return fail(error, QStringLiteral("Choose general request output is null"));
    ChooseGeneralRequestPayload parsed;
    if (!parseLegacyStringArray(value, QStringLiteral("choose general request"),
                                &parsed.candidates, error)) {
        return false;
    }
    *payload = parsed;
    return true;
}

bool ChooseGeneralRequestPayload::parseV2(
    const QVariant &value, ChooseGeneralRequestPayload *payload, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (payload == nullptr)
        return fail(error, QStringLiteral("Choose general request output is null"));
    QVariantMap object;
    ChooseGeneralRequestPayload parsed;
    if (!parseObject(value, QStringLiteral("choose general request"), &object, error)
        || !requireStringList(object, QStringLiteral("candidates"),
                              QStringLiteral("Choose general request"),
                              &parsed.candidates, error)) {
        return false;
    }
    *payload = parsed;
    return true;
}

QVariant ChooseGeneralReplyPayload::toLegacyVariant() const
{
    return general;
}

QVariantMap ChooseGeneralReplyPayload::toV2Variant() const
{
    return QVariantMap{
        {QStringLiteral("schema_version"), SchemaVersion},
        {QStringLiteral("general"), general}
    };
}

bool ChooseGeneralReplyPayload::parseLegacy(
    const QVariant &value, ChooseGeneralReplyPayload *payload, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (payload == nullptr)
        return fail(error, QStringLiteral("Choose general reply output is null"));
    ChooseGeneralReplyPayload parsed;
    if (!parseLegacyString(value, QStringLiteral("choose general reply"),
                           &parsed.general, error)) {
        return false;
    }
    *payload = parsed;
    return true;
}

bool ChooseGeneralReplyPayload::parseV2(
    const QVariant &value, ChooseGeneralReplyPayload *payload, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (payload == nullptr)
        return fail(error, QStringLiteral("Choose general reply output is null"));
    QVariantMap object;
    ChooseGeneralReplyPayload parsed;
    if (!parseObject(value, QStringLiteral("choose general reply"), &object, error)
        || !requireString(object, QStringLiteral("general"),
                          QStringLiteral("Choose general reply"),
                          &parsed.general, error)) {
        return false;
    }
    *payload = parsed;
    return true;
}

QVariantMap ChooseSuitRequestPayload::toV2Variant() const
{
    return QVariantMap{{QStringLiteral("schema_version"), SchemaVersion}};
}

bool ChooseSuitRequestPayload::parseV2(
    const QVariant &value, ChooseSuitRequestPayload *payload, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (payload == nullptr)
        return fail(error, QStringLiteral("Choose suit request output is null"));
    QVariantMap object;
    if (!parseObject(value, QStringLiteral("choose suit request"), &object, error))
        return false;
    *payload = ChooseSuitRequestPayload();
    return true;
}

QVariant ChooseSuitReplyPayload::toLegacyVariant() const
{
    return suit;
}

QVariantMap ChooseSuitReplyPayload::toV2Variant() const
{
    return QVariantMap{
        {QStringLiteral("schema_version"), SchemaVersion},
        {QStringLiteral("suit"), suit}
    };
}

bool ChooseSuitReplyPayload::parseLegacy(
    const QVariant &value, ChooseSuitReplyPayload *payload, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (payload == nullptr)
        return fail(error, QStringLiteral("Choose suit reply output is null"));
    ChooseSuitReplyPayload parsed;
    if (!parseLegacyString(value, QStringLiteral("choose suit reply"),
                           &parsed.suit, error)) {
        return false;
    }
    *payload = parsed;
    return true;
}

bool ChooseSuitReplyPayload::parseV2(
    const QVariant &value, ChooseSuitReplyPayload *payload, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (payload == nullptr)
        return fail(error, QStringLiteral("Choose suit reply output is null"));
    QVariantMap object;
    ChooseSuitReplyPayload parsed;
    if (!parseObject(value, QStringLiteral("choose suit reply"), &object, error)
        || !requireString(object, QStringLiteral("suit"),
                          QStringLiteral("Choose suit reply"), &parsed.suit, error)) {
        return false;
    }
    *payload = parsed;
    return true;
}

QVariant ChooseKingdomRequestPayload::toLegacyVariant() const
{
    return QVariantList{kingdoms.join(QLatin1Char('+'))};
}

QVariantMap ChooseKingdomRequestPayload::toV2Variant() const
{
    return QVariantMap{
        {QStringLiteral("schema_version"), SchemaVersion},
        {QStringLiteral("kingdoms"), stringListVariant(kingdoms)}
    };
}

bool ChooseKingdomRequestPayload::parseLegacy(
    const QVariant &value, ChooseKingdomRequestPayload *payload, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (payload == nullptr)
        return fail(error, QStringLiteral("Choose kingdom request output is null"));
    if (value.userType() != QMetaType::QVariantList) {
        return fail(error, QStringLiteral("Legacy choose kingdom request must be an array"));
    }
    const QVariantList entries = value.toList();
    if (entries.size() != 1 || entries.first().userType() != QMetaType::QString) {
        return fail(error,
                    QStringLiteral("Legacy choose kingdom request must contain one string"));
    }
    ChooseKingdomRequestPayload parsed;
    parsed.kingdoms = entries.first().toString().split(
        QLatin1Char('+'), Qt::KeepEmptyParts);
    *payload = parsed;
    return true;
}

bool ChooseKingdomRequestPayload::parseV2(
    const QVariant &value, ChooseKingdomRequestPayload *payload, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (payload == nullptr)
        return fail(error, QStringLiteral("Choose kingdom request output is null"));
    QVariantMap object;
    ChooseKingdomRequestPayload parsed;
    if (!parseObject(value, QStringLiteral("choose kingdom request"), &object, error)
        || !requireStringList(object, QStringLiteral("kingdoms"),
                              QStringLiteral("Choose kingdom request"),
                              &parsed.kingdoms, error)) {
        return false;
    }
    *payload = parsed;
    return true;
}

QVariant ChooseKingdomReplyPayload::toLegacyVariant() const
{
    return kingdom;
}

QVariantMap ChooseKingdomReplyPayload::toV2Variant() const
{
    return QVariantMap{
        {QStringLiteral("schema_version"), SchemaVersion},
        {QStringLiteral("kingdom"), kingdom}
    };
}

bool ChooseKingdomReplyPayload::parseLegacy(
    const QVariant &value, ChooseKingdomReplyPayload *payload, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (payload == nullptr)
        return fail(error, QStringLiteral("Choose kingdom reply output is null"));
    ChooseKingdomReplyPayload parsed;
    if (!parseLegacyString(value, QStringLiteral("choose kingdom reply"),
                           &parsed.kingdom, error)) {
        return false;
    }
    *payload = parsed;
    return true;
}

bool ChooseKingdomReplyPayload::parseV2(
    const QVariant &value, ChooseKingdomReplyPayload *payload, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (payload == nullptr)
        return fail(error, QStringLiteral("Choose kingdom reply output is null"));
    QVariantMap object;
    ChooseKingdomReplyPayload parsed;
    if (!parseObject(value, QStringLiteral("choose kingdom reply"), &object, error)
        || !requireString(object, QStringLiteral("kingdom"),
                          QStringLiteral("Choose kingdom reply"),
                          &parsed.kingdom, error)) {
        return false;
    }
    *payload = parsed;
    return true;
}

QVariant ChooseOrderRequestPayload::toLegacyVariant() const
{
    return reason;
}

QVariantMap ChooseOrderRequestPayload::toV2Variant() const
{
    return QVariantMap{
        {QStringLiteral("schema_version"), SchemaVersion},
        {QStringLiteral("reason"), orderReasonName(reason)}
    };
}

bool ChooseOrderRequestPayload::parseLegacy(
    const QVariant &value, ChooseOrderRequestPayload *payload, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (payload == nullptr)
        return fail(error, QStringLiteral("Choose order request output is null"));
    ChooseOrderRequestPayload parsed;
    if (!parseLegacyInteger(value, QStringLiteral("choose order request"),
                            &parsed.reason, error)) {
        return false;
    }
    if (orderReasonName(parsed.reason).isEmpty())
        return fail(error, QStringLiteral("Legacy choose order reason is unknown"));
    *payload = parsed;
    return true;
}

bool ChooseOrderRequestPayload::parseV2(
    const QVariant &value, ChooseOrderRequestPayload *payload, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (payload == nullptr)
        return fail(error, QStringLiteral("Choose order request output is null"));
    QVariantMap object;
    QString reason;
    ChooseOrderRequestPayload parsed;
    if (!parseObject(value, QStringLiteral("choose order request"), &object, error)
        || !requireString(object, QStringLiteral("reason"),
                          QStringLiteral("Choose order request"), &reason, error)
        || !parseOrderReason(reason, &parsed.reason, error)) {
        return false;
    }
    *payload = parsed;
    return true;
}

QVariant ChooseOrderReplyPayload::toLegacyVariant() const
{
    return camp;
}

QVariantMap ChooseOrderReplyPayload::toV2Variant() const
{
    return QVariantMap{
        {QStringLiteral("schema_version"), SchemaVersion},
        {QStringLiteral("camp"), campName(camp)}
    };
}

bool ChooseOrderReplyPayload::parseLegacy(
    const QVariant &value, ChooseOrderReplyPayload *payload, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (payload == nullptr)
        return fail(error, QStringLiteral("Choose order reply output is null"));
    ChooseOrderReplyPayload parsed;
    if (!parseLegacyInteger(value, QStringLiteral("choose order reply"),
                            &parsed.camp, error)) {
        return false;
    }
    if (campName(parsed.camp).isEmpty())
        return fail(error, QStringLiteral("Legacy choose order camp is unknown"));
    *payload = parsed;
    return true;
}

bool ChooseOrderReplyPayload::parseV2(
    const QVariant &value, ChooseOrderReplyPayload *payload, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (payload == nullptr)
        return fail(error, QStringLiteral("Choose order reply output is null"));
    QVariantMap object;
    QString camp;
    ChooseOrderReplyPayload parsed;
    if (!parseObject(value, QStringLiteral("choose order reply"), &object, error)
        || !requireString(object, QStringLiteral("camp"),
                          QStringLiteral("Choose order reply"), &camp, error)
        || !parseCamp(camp, &parsed.camp, error)) {
        return false;
    }
    *payload = parsed;
    return true;
}

QVariant InvokeSkillRequestPayload::toLegacyVariant() const
{
    return QVariantList{skillName, data};
}

QVariantMap InvokeSkillRequestPayload::toV2Variant() const
{
    return QVariantMap{
        {QStringLiteral("schema_version"), SchemaVersion},
        {QStringLiteral("skill_name"), skillName},
        {QStringLiteral("data"), data}
    };
}

bool InvokeSkillRequestPayload::parseLegacy(
    const QVariant &value, InvokeSkillRequestPayload *payload, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (payload == nullptr)
        return fail(error, QStringLiteral("Invoke skill request output is null"));
    InvokeSkillRequestPayload parsed;
    if (!parseLegacyTwoStrings(value, QStringLiteral("invoke skill request"),
                               &parsed.skillName, &parsed.data, error)) {
        return false;
    }
    *payload = parsed;
    return true;
}

bool InvokeSkillRequestPayload::parseV2(
    const QVariant &value, InvokeSkillRequestPayload *payload, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (payload == nullptr)
        return fail(error, QStringLiteral("Invoke skill request output is null"));
    QVariantMap object;
    InvokeSkillRequestPayload parsed;
    if (!parseObject(value, QStringLiteral("invoke skill request"), &object, error)
        || !requireString(object, QStringLiteral("skill_name"),
                          QStringLiteral("Invoke skill request"),
                          &parsed.skillName, error)
        || !requireString(object, QStringLiteral("data"),
                          QStringLiteral("Invoke skill request"), &parsed.data, error)) {
        return false;
    }
    *payload = parsed;
    return true;
}

QVariant InvokeSkillReplyPayload::toLegacyVariant() const
{
    return invoke;
}

QVariantMap InvokeSkillReplyPayload::toV2Variant() const
{
    return QVariantMap{
        {QStringLiteral("schema_version"), SchemaVersion},
        {QStringLiteral("invoke"), invoke}
    };
}

bool InvokeSkillReplyPayload::parseLegacy(
    const QVariant &value, InvokeSkillReplyPayload *payload, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (payload == nullptr)
        return fail(error, QStringLiteral("Invoke skill reply output is null"));
    InvokeSkillReplyPayload parsed;
    if (!parseLegacyBool(value, QStringLiteral("invoke skill reply"),
                         &parsed.invoke, error)) {
        return false;
    }
    *payload = parsed;
    return true;
}

bool InvokeSkillReplyPayload::parseV2(
    const QVariant &value, InvokeSkillReplyPayload *payload, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (payload == nullptr)
        return fail(error, QStringLiteral("Invoke skill reply output is null"));
    QVariantMap object;
    InvokeSkillReplyPayload parsed;
    if (!parseObject(value, QStringLiteral("invoke skill reply"), &object, error)
        || !requireBool(object, QStringLiteral("invoke"),
                        QStringLiteral("Invoke skill reply"), &parsed.invoke, error)) {
        return false;
    }
    *payload = parsed;
    return true;
}

QVariant SurrenderVoteRequestPayload::toLegacyVariant() const
{
    return initiatorGeneral;
}

QVariantMap SurrenderVoteRequestPayload::toV2Variant() const
{
    return QVariantMap{
        {QStringLiteral("schema_version"), SchemaVersion},
        {QStringLiteral("initiator_general"), initiatorGeneral}
    };
}

bool SurrenderVoteRequestPayload::parseLegacy(
    const QVariant &value, SurrenderVoteRequestPayload *payload, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (payload == nullptr)
        return fail(error, QStringLiteral("Surrender vote request output is null"));
    SurrenderVoteRequestPayload parsed;
    if (!parseLegacyString(value, QStringLiteral("surrender vote request"),
                           &parsed.initiatorGeneral, error)) {
        return false;
    }
    *payload = parsed;
    return true;
}

bool SurrenderVoteRequestPayload::parseV2(
    const QVariant &value, SurrenderVoteRequestPayload *payload, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (payload == nullptr)
        return fail(error, QStringLiteral("Surrender vote request output is null"));
    QVariantMap object;
    SurrenderVoteRequestPayload parsed;
    if (!parseObject(value, QStringLiteral("surrender vote request"), &object, error)
        || !requireString(object, QStringLiteral("initiator_general"),
                          QStringLiteral("Surrender vote request"),
                          &parsed.initiatorGeneral, error)) {
        return false;
    }
    *payload = parsed;
    return true;
}

QVariant SurrenderVoteReplyPayload::toLegacyVariant() const
{
    return surrender;
}

QVariantMap SurrenderVoteReplyPayload::toV2Variant() const
{
    return QVariantMap{
        {QStringLiteral("schema_version"), SchemaVersion},
        {QStringLiteral("surrender"), surrender}
    };
}

bool SurrenderVoteReplyPayload::parseLegacy(
    const QVariant &value, SurrenderVoteReplyPayload *payload, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (payload == nullptr)
        return fail(error, QStringLiteral("Surrender vote reply output is null"));
    SurrenderVoteReplyPayload parsed;
    if (!parseLegacyBool(value, QStringLiteral("surrender vote reply"),
                         &parsed.surrender, error)) {
        return false;
    }
    *payload = parsed;
    return true;
}

bool SurrenderVoteReplyPayload::parseV2(
    const QVariant &value, SurrenderVoteReplyPayload *payload, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (payload == nullptr)
        return fail(error, QStringLiteral("Surrender vote reply output is null"));
    QVariantMap object;
    SurrenderVoteReplyPayload parsed;
    if (!parseObject(value, QStringLiteral("surrender vote reply"), &object, error)
        || !requireBool(object, QStringLiteral("surrender"),
                        QStringLiteral("Surrender vote reply"),
                        &parsed.surrender, error)) {
        return false;
    }
    *payload = parsed;
    return true;
}
