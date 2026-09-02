#ifndef CLIENT_PROMPT_H
#define CLIENT_PROMPT_H

#include <QString>
#include <QStringList>

#include <functional>

// Wire prompt colon list used by askForCard / askForDiscard / askForPlayerChosen.
// Same substitution order as Client::formatPromptList:
// key, then %arg2, %arg, %dest, %src.
QString formatClientPromptList(const QStringList &texts,
                               const std::function<QString(const QString &)> &translate,
                               const std::function<QString(const QString &)> &playerName = {});

QString formatClientPrompt(const QString &prompt,
                           const std::function<QString(const QString &)> &translate,
                           const std::function<QString(const QString &)> &playerName = {});

#endif
