#include "protocol.h"
#include "json.h"
#include "protocol/protocol-message-utils.h"

using namespace std;
using namespace QSanProtocol;

const char *QSanProtocol::S_PLAYER_SELF_REFERENCE_ID = "MG_SELF";

const int QSanProtocol::S_ALL_ALIVE_PLAYERS = 0;

bool QSanProtocol::Countdown::tryParse(const QVariant &var)
{
    if (var.userType() != QMetaType::QVariantMap)
        return false;
    const QVariantMap value = var.toMap();
    int rawType = 0;
    if (!ProtocolMessageUtils::tryParseInt(
            value.value(QStringLiteral("type")), rawType)
        || rawType < S_COUNTDOWN_NO_LIMIT || rawType > S_COUNTDOWN_USE_DEFAULT) {
        return false;
    }
    Countdown parsed;
    parsed.type = static_cast<CountdownType>(rawType);
    if (parsed.type == S_COUNTDOWN_USE_SPECIFIED) {
        int parsedCurrent = 0;
        int parsedMaximum = 0;
        if (!ProtocolMessageUtils::tryParseInt(
                value.value(QStringLiteral("current")), parsedCurrent)
            || !ProtocolMessageUtils::tryParseInt(
                value.value(QStringLiteral("maximum")), parsedMaximum)
            || parsedCurrent < 0 || parsedMaximum < 0) {
            return false;
        }
        parsed.current = static_cast<time_t>(parsedCurrent);
        parsed.max = static_cast<time_t>(parsedMaximum);
    }
    *this = parsed;
    return true;
}

QVariant QSanProtocol::Countdown::toVariant() const
{
    QVariantMap value{{QStringLiteral("type"), static_cast<int>(type)}};
    if (type == S_COUNTDOWN_USE_SPECIFIED) {
        value.insert(QStringLiteral("current"), static_cast<qlonglong>(current));
        value.insert(QStringLiteral("maximum"), static_cast<qlonglong>(max));
    }
    return value;
}

