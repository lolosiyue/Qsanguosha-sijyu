#ifndef QSAN_TUI_LOG_TEXT_H
#define QSAN_TUI_LOG_TEXT_H

#include <QString>
#include <QVariantMap>

#include <functional>

// Turns a player object name (sgs1) into the name a reader recognises.
using TuiPlayerNameResolver = std::function<QString(const QString &objectName)>;

// One battle log line built from SkillLogPayload. Plain text: the localized
// template with its placeholders filled in, never HTML and never a raw key.
QString tuiSkillLogText(const QVariantMap &payload,
                        const TuiPlayerNameResolver &playerName);

// One line for the game-event channel (GameEventPayload). Most events drive
// desktop animation or state the text client already shows elsewhere; those
// return an empty string so they stay out of the transcript.
QString tuiGameEventText(const QVariantMap &payload,
                         const TuiPlayerNameResolver &playerName);

// Display text for one presentation event, live or replayed from the stored
// log. Empty means the event has nothing to say in a text transcript.
QString tuiPresentationEventText(int command, const QString &fallbackText,
                                 const QVariant &payload,
                                 const TuiPlayerNameResolver &playerName);

#endif
