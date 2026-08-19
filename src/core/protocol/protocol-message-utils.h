#ifndef PROTOCOL_MESSAGE_UTILS_H
#define PROTOCOL_MESSAGE_UTILS_H

#include <QMetaType>
#include <QVariant>
#include <limits>
#include <cmath>

namespace ProtocolMessageUtils
{
inline bool tryParseInt(const QVariant &value, int &result)
{
    switch (value.userType()) {
    case QMetaType::Int:
        result = value.toInt();
        return true;
    case QMetaType::UInt: {
        const uint number = value.toUInt();
        if (number > static_cast<uint>(std::numeric_limits<int>::max()))
            return false;
        result = static_cast<int>(number);
        return true;
    }
    case QMetaType::LongLong: {
        const qlonglong number = value.toLongLong();
        if (number < std::numeric_limits<int>::min()
            || number > std::numeric_limits<int>::max())
            return false;
        result = static_cast<int>(number);
        return true;
    }
    case QMetaType::ULongLong: {
        const qulonglong number = value.toULongLong();
        if (number > static_cast<qulonglong>(std::numeric_limits<int>::max()))
            return false;
        result = static_cast<int>(number);
        return true;
    }
    case QMetaType::Double: {
        const double number = value.toDouble();
        if (!std::isfinite(number) || std::trunc(number) != number
            || number < std::numeric_limits<int>::min()
            || number > std::numeric_limits<int>::max())
            return false;
        result = static_cast<int>(number);
        return true;
    }
    default:
        return false;
    }
}

inline bool tryParseString(const QVariant &value, QString &result)
{
    if (value.userType() != QMetaType::QString)
        return false;
    result = value.toString();
    return true;
}

inline bool tryParseBool(const QVariant &value, bool &result)
{
    if (value.userType() != QMetaType::Bool)
        return false;
    result = value.toBool();
    return true;
}

inline bool tryParseIntList(const QVariant &value, QList<int> &result)
{
    if (value.userType() != QMetaType::QVariantList)
        return false;

    QList<int> parsed;
    foreach (const QVariant &entry, value.toList()) {
        int number = 0;
        if (!tryParseInt(entry, number))
            return false;
        parsed << number;
    }
    result = parsed;
    return true;
}
}

#endif
