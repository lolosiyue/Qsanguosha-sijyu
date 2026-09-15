#include "tui-log-text.h"
#include "client-log-formatter.h"

// Keep the public TUI entry points; all text clients share their implementation.
QString tuiSkillLogText(const QVariantMap &payload, const TuiPlayerNameResolver &playerName)
{
    return formatClientSkillLogText(payload, playerName);
}

QString tuiGameEventText(const QVariantMap &payload, const TuiPlayerNameResolver &playerName)
{
    return formatClientGameEventText(payload, playerName);
}

QString tuiPresentationEventText(int command, const QString &fallbackText,
                               const QVariant &payload, const TuiPlayerNameResolver &playerName)
{
    return formatClientPresentationText(command, fallbackText, payload, playerName);
}
