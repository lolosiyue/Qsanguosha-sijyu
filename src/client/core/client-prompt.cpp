#include "client-prompt.h"

namespace {

QString translateOrKeep(const std::function<QString(const QString &)> &translate,
                        const QString &key)
{
    return translate ? translate(key) : key;
}

} // namespace

QString formatClientPromptList(const QStringList &texts,
                               const std::function<QString(const QString &)> &translate,
                               const std::function<QString(const QString &)> &playerName)
{
    if (texts.isEmpty())
        return QString();

    QString prompt = translateOrKeep(translate, texts.at(0));
    if (texts.size() >= 5)
        prompt.replace(QStringLiteral("%arg2"), translateOrKeep(translate, texts.at(4)));
    if (texts.size() >= 4)
        prompt.replace(QStringLiteral("%arg"), translateOrKeep(translate, texts.at(3)));
    if (texts.size() >= 3) {
        prompt.replace(QStringLiteral("%dest"),
                       playerName ? playerName(texts.at(2))
                                  : translateOrKeep(translate, texts.at(2)));
    }
    if (texts.size() >= 2) {
        prompt.replace(QStringLiteral("%src"),
                       playerName ? playerName(texts.at(1))
                                  : translateOrKeep(translate, texts.at(1)));
    }
    return prompt;
}

QString formatClientPrompt(const QString &prompt,
                           const std::function<QString(const QString &)> &translate,
                           const std::function<QString(const QString &)> &playerName)
{
    if (prompt.isEmpty())
        return prompt;
    return formatClientPromptList(prompt.split(QLatin1Char(':')), translate, playerName);
}
