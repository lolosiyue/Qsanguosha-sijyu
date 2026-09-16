#include "arrange-seats-message.h"

#include "json.h"

QVariant ArrangeSeatsMessage::toVariant() const
{
    return QVariantMap{{QStringLiteral("schema_version"), SchemaVersion},
                       {QStringLiteral("player_names"), JsonUtils::toJsonArray(playerNames)},
                       {QStringLiteral("play_order_reversed"), playOrderReversed}};
}
