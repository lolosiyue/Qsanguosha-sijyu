#include "controller-interaction-contract.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QMetaType>
#include <QSet>

#include <cmath>

namespace ControllerInteractionContract {
namespace {

enum class FieldType
{
    Choice,
    Boolean,
    Integer,
    Text,
    Selection
};

struct Field
{
    QString key;
    FieldType type = FieldType::Text;
    QSet<QString> options;
    int minimum = 0;
    int maximum = 0;
    int minLength = 0;
    int maxLength = 0;
};

struct Contract
{
    QList<Field> fields;
    bool canCancel = false;
    bool hasCancelValue = false;
    QVariant cancelValue;
};

bool fail(QString *error, const QString &message)
{
    if (error != nullptr)
        *error = message;
    return false;
}

bool isJsonNumber(const QJsonValue &value, double *number = nullptr)
{
    if (!value.isDouble())
        return false;
    const double parsed = value.toDouble();
    if (!std::isfinite(parsed))
        return false;
    if (number != nullptr)
        *number = parsed;
    return true;
}

bool jsonInteger(const QJsonValue &value, int minimum, int maximum, int *result)
{
    double parsed = 0.0;
    if (!isJsonNumber(value, &parsed) || std::trunc(parsed) != parsed
        || parsed < minimum || parsed > maximum) {
        return false;
    }
    if (result != nullptr)
        *result = static_cast<int>(parsed);
    return true;
}

bool variantNumber(const QVariant &value, double *number)
{
    switch (value.userType()) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
    case QMetaType::Float:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::Char:
    case QMetaType::UChar:
    case QMetaType::SChar:
        break;
    default:
        return false;
    }

    bool ok = false;
    const double parsed = value.toDouble(&ok);
    if (!ok || !std::isfinite(parsed))
        return false;
    if (number != nullptr)
        *number = parsed;
    return true;
}

bool variantInteger(const QVariant &value, int minimum, int maximum)
{
    double parsed = 0.0;
    return variantNumber(value, &parsed) && std::trunc(parsed) == parsed
        && parsed >= minimum && parsed <= maximum;
}

bool fieldType(const QString &name, FieldType *type)
{
    if (name == QLatin1String("choice"))
        *type = FieldType::Choice;
    else if (name == QLatin1String("boolean"))
        *type = FieldType::Boolean;
    else if (name == QLatin1String("integer"))
        *type = FieldType::Integer;
    else if (name == QLatin1String("text"))
        *type = FieldType::Text;
    else if (name == QLatin1String("selection"))
        *type = FieldType::Selection;
    else
        return false;
    return true;
}

bool parseBound(const QJsonObject &object, const QString &name, int fallback,
                int minimum, int maximum, int *result)
{
    if (!object.contains(name)) {
        *result = fallback;
        return true;
    }
    return jsonInteger(object.value(name), minimum, maximum, result);
}

bool parseOptions(const QJsonObject &fieldObject, Field *field)
{
    const QJsonValue optionsValue = fieldObject.value(QStringLiteral("options"));
    if (!optionsValue.isArray())
        return false;
    const QJsonArray options = optionsValue.toArray();
    if (options.isEmpty() || options.size() > 1000)
        return false;

    for (const QJsonValue &entry : options) {
        if (!entry.isObject())
            return false;
        const QJsonObject option = entry.toObject();
        const QJsonValue value = option.value(QStringLiteral("value"));
        if (!value.isString())
            return false;
        const QString text = value.toString();
        if (text.isEmpty() || text.size() > 256 || field->options.contains(text))
            return false;
        const QJsonValue label = option.value(QStringLiteral("label"));
        if (!label.isUndefined() && !label.isString())
            return false;
        field->options.insert(text);
    }
    return true;
}

bool variantStringList(const QVariant &value, QStringList *strings)
{
    QStringList parsed;
    if (value.userType() == QMetaType::QStringList) {
        parsed = value.toStringList();
    } else if (value.userType() == QMetaType::QVariantList) {
        const QVariantList list = value.toList();
        parsed.reserve(list.size());
        for (const QVariant &entry : list) {
            if (entry.userType() != QMetaType::QString)
                return false;
            parsed.append(entry.toString());
        }
    } else {
        return false;
    }
    *strings = parsed;
    return true;
}

bool validateFieldValue(const Field &field, const QVariant &value)
{
    switch (field.type) {
    case FieldType::Choice:
        return value.userType() == QMetaType::QString
            && field.options.contains(value.toString());
    case FieldType::Boolean:
        return value.userType() == QMetaType::Bool;
    case FieldType::Integer:
        return variantInteger(value, field.minimum, field.maximum);
    case FieldType::Text:
        return value.userType() == QMetaType::QString
            && value.toString().size() >= field.minLength
            && value.toString().size() <= field.maxLength;
    case FieldType::Selection: {
        QStringList selected;
        if (!variantStringList(value, &selected)
            || selected.size() < field.minimum || selected.size() > field.maximum) {
            return false;
        }
        QSet<QString> seen;
        for (const QString &item : selected) {
            if (!field.options.contains(item) || seen.contains(item))
                return false;
            seen.insert(item);
        }
        return true;
    }
    }
    return false;
}

bool isJsonVariant(const QVariant &value)
{
    switch (value.userType()) {
    case QMetaType::UnknownType:
    case QMetaType::Nullptr:
    case QMetaType::QString:
    case QMetaType::Bool:
        return true;
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
    case QMetaType::Float:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::Char:
    case QMetaType::UChar:
    case QMetaType::SChar: {
        double number = 0.0;
        return variantNumber(value, &number);
    }
    case QMetaType::QVariantList:
        for (const QVariant &entry : value.toList())
            if (!isJsonVariant(entry)) return false;
        return true;
    case QMetaType::QVariantMap:
    {
        const QVariantMap object = value.toMap();
        for (auto it = object.cbegin(); it != object.cend(); ++it) {
            if (!isJsonVariant(it.value())) return false;
        }
        return true;
    }
    default:
        return false;
    }
}

bool parseContract(const QJsonObject &descriptor, Contract *contract, QString *error)
{
    const QJsonValue version = descriptor.value(QStringLiteral("contract_version"));
    if (!jsonInteger(version, 1, 1, nullptr))
        return fail(error, QStringLiteral("contract_version must be integer 1"));

    const QJsonValue fieldsValue = descriptor.value(QStringLiteral("fields"));
    if (!fieldsValue.isArray())
        return fail(error, QStringLiteral("fields must be an array"));
    const QJsonArray fieldEntries = fieldsValue.toArray();
    if (fieldEntries.isEmpty() || fieldEntries.size() > 32)
        return fail(error, QStringLiteral("fields count is out of range"));

    Contract parsed;
    QSet<QString> keys;
    for (const QJsonValue &entry : fieldEntries) {
        if (!entry.isObject())
            return fail(error, QStringLiteral("field definition must be an object"));
        const QJsonObject object = entry.toObject();
        const QJsonValue keyValue = object.value(QStringLiteral("key"));
        const QJsonValue typeValue = object.value(QStringLiteral("type"));
        if (!keyValue.isString() || !typeValue.isString())
            return fail(error, QStringLiteral("field key and type must be strings"));

        Field field;
        field.key = keyValue.toString();
        if (field.key.isEmpty() || field.key.size() > 64 || keys.contains(field.key))
            return fail(error, QStringLiteral("field keys must be unique and bounded"));
        if (!fieldType(typeValue.toString(), &field.type))
            return fail(error, QStringLiteral("unsupported field type"));
        keys.insert(field.key);

        if (field.type == FieldType::Choice || field.type == FieldType::Selection) {
            if (!parseOptions(object, &field))
                return fail(error, QStringLiteral("field options are invalid"));
        } else if (object.contains(QStringLiteral("options"))) {
            return fail(error, QStringLiteral("options are not valid for this field type"));
        }

        switch (field.type) {
        case FieldType::Choice:
        case FieldType::Boolean:
            if (object.contains(QStringLiteral("minimum"))
                || object.contains(QStringLiteral("maximum"))
                || object.contains(QStringLiteral("min_selection"))
                || object.contains(QStringLiteral("max_selection"))
                || object.contains(QStringLiteral("min_length"))
                || object.contains(QStringLiteral("max_length"))) {
                return fail(error, QStringLiteral("field bounds do not match its type"));
            }
            break;
        case FieldType::Integer:
            if (object.contains(QStringLiteral("min_selection"))
                || object.contains(QStringLiteral("max_selection"))
                || object.contains(QStringLiteral("min_length"))
                || object.contains(QStringLiteral("max_length"))
                || !parseBound(object, QStringLiteral("minimum"), 0,
                               -1000000, 1000000, &field.minimum)
                || !parseBound(object, QStringLiteral("maximum"), 100,
                               -1000000, 1000000, &field.maximum)
                || field.minimum > field.maximum) {
                return fail(error, QStringLiteral("integer bounds are invalid"));
            }
            break;
        case FieldType::Text:
            if (object.contains(QStringLiteral("minimum"))
                || object.contains(QStringLiteral("maximum"))
                || object.contains(QStringLiteral("min_selection"))
                || object.contains(QStringLiteral("max_selection"))
                || !parseBound(object, QStringLiteral("min_length"), 0,
                               0, 4096, &field.minLength)
                || !parseBound(object, QStringLiteral("max_length"), 512,
                               0, 4096, &field.maxLength)
                || field.minLength > field.maxLength) {
                return fail(error, QStringLiteral("text bounds are invalid"));
            }
            break;
        case FieldType::Selection:
            if (object.contains(QStringLiteral("minimum"))
                || object.contains(QStringLiteral("maximum"))
                || object.contains(QStringLiteral("min_length"))
                || object.contains(QStringLiteral("max_length"))
                || !parseBound(object, QStringLiteral("min_selection"), 0,
                               0, field.options.size(), &field.minimum)
                || !parseBound(object, QStringLiteral("max_selection"), field.options.size(),
                               0, field.options.size(), &field.maximum)
                || field.minimum > field.maximum) {
                return fail(error, QStringLiteral("selection bounds are invalid"));
            }
            break;
        }

        const QJsonValue defaultValue = object.value(QStringLiteral("default"));
        if (!defaultValue.isUndefined()) {
            if (!validateFieldValue(field, defaultValue.toVariant()))
                return fail(error, QStringLiteral("field default is invalid"));
        }
        parsed.fields.append(field);
    }

    const QJsonValue canCancel = descriptor.value(QStringLiteral("can_cancel"));
    if (!canCancel.isUndefined() && !canCancel.isBool())
        return fail(error, QStringLiteral("can_cancel must be a boolean"));
    parsed.canCancel = canCancel.isBool() && canCancel.toBool();

    const QJsonValue cancelValue = descriptor.value(QStringLiteral("cancel_value"));
    parsed.hasCancelValue = !cancelValue.isUndefined();
    if (parsed.canCancel && !parsed.hasCancelValue)
        return fail(error, QStringLiteral("cancel_value is required when can_cancel is true"));
    if (parsed.hasCancelValue) {
        parsed.cancelValue = cancelValue.toVariant();
        if (!isJsonVariant(parsed.cancelValue))
            return fail(error, QStringLiteral("cancel_value must be a JSON value"));
    }

    *contract = parsed;
    return true;
}

} // namespace

bool validateDescriptor(const QJsonObject &descriptor, QString *error)
{
    if (error != nullptr)
        error->clear();
    Contract contract;
    return parseContract(descriptor, &contract, error);
}

bool validateResponse(const QJsonObject &descriptor, const QVariant &response,
                      QString *error)
{
    if (error != nullptr)
        error->clear();
    Contract contract;
    if (!parseContract(descriptor, &contract, error))
        return false;

    // QVariant equality coerces numeric strings and numbers, including values
    // nested in lists/maps. Compare JSON values to preserve the declared types;
    // numeric QVariant representations still share JSON's single number type.
    if (contract.canCancel && isJsonVariant(response)
        && QJsonValue::fromVariant(response) == QJsonValue::fromVariant(contract.cancelValue))
        return true;

    if (response.userType() != QMetaType::QVariantMap)
        return fail(error, QStringLiteral("response must be an object"));
    const QVariantMap object = response.toMap();
    if (object.size() != contract.fields.size())
        return fail(error, QStringLiteral("response fields do not match contract"));

    for (const Field &field : contract.fields) {
        if (!object.contains(field.key))
            return fail(error, QStringLiteral("response fields do not match contract"));
        if (!validateFieldValue(field, object.value(field.key)))
            return fail(error, QStringLiteral("response field value is invalid"));
    }
    return true;
}

} // namespace ControllerInteractionContract
