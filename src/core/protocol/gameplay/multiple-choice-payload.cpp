#include "multiple-choice-payload.h"

#include "json.h"

#include <QMetaType>

#include <cmath>

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
        && std::trunc(number) == number
        && number == MultipleChoiceRequestPayload::SchemaVersion;
}

QVariantList stringListVariant(const QStringList &values)
{
    QVariantList result;
    result.reserve(values.size());
    for (const QString &value : values)
        result.append(value);
    return result;
}

bool parseStringList(const QVariantMap &object, const QString &key,
                     QStringList *values, QString *error)
{
    if (!object.contains(key))
        return fail(error, QStringLiteral("Multiple choice %1 is required").arg(key));

    const QVariant value = object.value(key);
    if (value.userType() != QMetaType::QVariantList) {
        return fail(error,
                    QStringLiteral("Multiple choice %1 must be an array of strings").arg(key));
    }

    QStringList parsed;
    const QVariantList entries = value.toList();
    parsed.reserve(entries.size());
    for (const QVariant &entry : entries) {
        if (entry.userType() != QMetaType::QString) {
            return fail(error,
                        QStringLiteral("Multiple choice %1 must be an array of strings").arg(key));
        }
        parsed.append(entry.toString());
    }
    *values = parsed;
    return true;
}

bool requireString(const QVariantMap &object, const QString &key,
                   QString *value, QString *error)
{
    if (!object.contains(key))
        return fail(error, QStringLiteral("Multiple choice %1 is required").arg(key));
    const QVariant field = object.value(key);
    if (field.userType() != QMetaType::QString)
        return fail(error, QStringLiteral("Multiple choice %1 must be a string").arg(key));
    *value = field.toString();
    return true;
}

bool requireSchema(const QVariantMap &object, QString *error)
{
    if (!object.contains(QStringLiteral("schema_version"))) {
        return fail(error,
                    QStringLiteral("Multiple choice schema_version is required"));
    }
    if (!isSchemaVersionOne(object.value(QStringLiteral("schema_version")))) {
        return fail(error,
                    QStringLiteral("Multiple choice schema_version must be 1"));
    }
    return true;
}
}

QVariant MultipleChoiceRequestPayload::toDomainVariant() const
{
    return QVariantList{
        skillName,
        options.join(QLatin1Char('+')),
        disabledOptions.join(QLatin1Char('+')),
        tip
    };
}

QVariantMap MultipleChoiceRequestPayload::toV2Variant() const
{
    QVariantMap result;
    result.insert(QStringLiteral("schema_version"), SchemaVersion);
    result.insert(QStringLiteral("skill_name"), skillName);
    result.insert(QStringLiteral("options"), stringListVariant(options));
    result.insert(QStringLiteral("disabled_options"), stringListVariant(disabledOptions));
    result.insert(QStringLiteral("tip"), tip);
    return result;
}

bool MultipleChoiceRequestPayload::parseDomain(
    const QVariant &value, MultipleChoiceRequestPayload *payload, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (payload == nullptr)
        return fail(error, QStringLiteral("Multiple choice request output is null"));
    if (value.userType() != QMetaType::QVariantList)
        return fail(error, QStringLiteral("Domain multiple choice request must be an array"));

    const QVariantList array = value.toList();
    if (array.size() != 4)
        return fail(error, QStringLiteral("Domain multiple choice request must contain 4 fields"));
    for (const QVariant &field : array) {
        if (field.userType() != QMetaType::QString) {
            return fail(error,
                        QStringLiteral("Domain multiple choice request fields must be strings"));
        }
    }

    MultipleChoiceRequestPayload parsed;
    parsed.skillName = array.at(0).toString();
    parsed.options = array.at(1).toString().split(QLatin1Char('+'), Qt::KeepEmptyParts);
    parsed.disabledOptions = array.at(2).toString().split(
        QLatin1Char('+'), Qt::KeepEmptyParts);
    parsed.disabledOptions.removeAll(QString());
    parsed.tip = array.at(3).toString();
    *payload = parsed;
    return true;
}

bool MultipleChoiceRequestPayload::parseV2(
    const QVariant &value, MultipleChoiceRequestPayload *payload, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (payload == nullptr)
        return fail(error, QStringLiteral("Multiple choice request output is null"));
    if (value.userType() != QMetaType::QVariantMap)
        return fail(error, QStringLiteral("Protocol V2 multiple choice request must be an object"));

    const QVariantMap object = value.toMap();
    MultipleChoiceRequestPayload parsed;
    if (!requireSchema(object, error)
        || !requireString(object, QStringLiteral("skill_name"), &parsed.skillName, error)
        || !parseStringList(object, QStringLiteral("options"), &parsed.options, error)
        || !parseStringList(object, QStringLiteral("disabled_options"),
                            &parsed.disabledOptions, error)
        || !requireString(object, QStringLiteral("tip"), &parsed.tip, error)) {
        return false;
    }

    *payload = parsed;
    return true;
}

QVariant MultipleChoiceReplyPayload::toDomainVariant() const
{
    return choice;
}

QVariantMap MultipleChoiceReplyPayload::toV2Variant() const
{
    QVariantMap result;
    result.insert(QStringLiteral("schema_version"), SchemaVersion);
    result.insert(QStringLiteral("choice"), choice);
    return result;
}

bool MultipleChoiceReplyPayload::parseDomain(
    const QVariant &value, MultipleChoiceReplyPayload *payload, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (payload == nullptr)
        return fail(error, QStringLiteral("Multiple choice reply output is null"));
    if (value.userType() != QMetaType::QString)
        return fail(error, QStringLiteral("Domain multiple choice reply must be a string"));

    MultipleChoiceReplyPayload parsed;
    parsed.choice = value.toString();
    *payload = parsed;
    return true;
}

bool MultipleChoiceReplyPayload::parseV2(
    const QVariant &value, MultipleChoiceReplyPayload *payload, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (payload == nullptr)
        return fail(error, QStringLiteral("Multiple choice reply output is null"));
    if (value.userType() != QMetaType::QVariantMap)
        return fail(error, QStringLiteral("Protocol V2 multiple choice reply must be an object"));

    const QVariantMap object = value.toMap();
    MultipleChoiceReplyPayload parsed;
    if (!requireSchema(object, error)
        || !requireString(object, QStringLiteral("choice"), &parsed.choice, error)) {
        return false;
    }

    *payload = parsed;
    return true;
}
