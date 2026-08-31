#include "tui-log-text.h"

#include "card.h"
#include "engine.h"
#include "protocol.h"
#include "tui-card-text.h"

#include <QCoreApplication>
#include <QStringList>

namespace {

QString translateOrKeep(const QString &key)
{
    if (key.isEmpty() || Sanguosha == nullptr)
        return key;
    const QString translated = Sanguosha->translate(key);
    return translated.isEmpty() ? key : translated;
}

QString resolveName(const TuiPlayerNameResolver &playerName, const QString &objectName)
{
    if (objectName.isEmpty())
        return QString();
    const QString resolved = playerName ? playerName(objectName) : QString();
    return resolved.isEmpty() ? objectName : resolved;
}

// A log carries either one card string (possibly virtual) or a "+" joined list
// of concrete ids, the same two shapes the desktop log box accepts.
QString cardsText(const QString &cardString)
{
    if (cardString.isEmpty() || Sanguosha == nullptr)
        return QString();

    QStringList names;
    bool allNumeric = true;
    const QStringList tokens = cardString.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    for (const QString &token : tokens) {
        bool ok = false;
        const int id = token.toInt(&ok);
        if (!ok) {
            allNumeric = false;
            break;
        }
        names << tuiCardDisplayText(id);
    }
    if (allNumeric && !names.isEmpty())
        return names.join(QStringLiteral("、"));

    const Card *card = Card::Parse(cardString);
    if (card == nullptr)
        return cardString;
    if (!card->isVirtualCard())
        return tuiCardDisplayText(card->getId());

    QString text = translateOrKeep(card->objectName());
    const QString skill = translateOrKeep(card->getSkillName());
    if (!skill.isEmpty())
        text = QCoreApplication::translate("QSanguoshaTui", "%1（%2）").arg(text, skill);
    return text;
}

// The server wraps chat and system notices in the markup the desktop log box
// renders. A text transcript must not show tags.
QString stripMarkup(const QString &text)
{
    QString result;
    result.reserve(text.size());
    bool inTag = false;
    for (QChar character : text) {
        if (character == QLatin1Char('<')) {
            inTag = true;
        } else if (character == QLatin1Char('>')) {
            inTag = false;
        } else if (!inTag) {
            result.append(character);
        }
    }
    return result.trimmed();
}

} // namespace

QString tuiSkillLogText(const QVariantMap &payload, const TuiPlayerNameResolver &playerName)
{
    const QString type = payload.value(QStringLiteral("log_type")).toString();
    if (type.isEmpty())
        return QString();
    // The desktop log box draws a rule here; lang carries no template for it.
    if (type == QLatin1String("$AppendSeparator"))
        return QStringLiteral("--------");

    // The localized template carries the sentence; the payload only fills it in.
    QString text = translateOrKeep(type);

    const QString from = resolveName(playerName,
        payload.value(QStringLiteral("from_player")).toString());
    if (!from.isEmpty())
        text.replace(QStringLiteral("%from"), from);

    QStringList targets;
    for (const QString &objectName
         : payload.value(QStringLiteral("to_players")).toStringList()) {
        targets << resolveName(playerName, objectName);
    }
    if (!targets.isEmpty())
        text.replace(QStringLiteral("%to"), targets.join(QStringLiteral("、")));

    const QString cards = cardsText(payload.value(QStringLiteral("card_string")).toString());
    if (!cards.isEmpty())
        text.replace(QStringLiteral("%card"), cards);

    // arg5 first: replacing %arg before %arg2 would eat the shared prefix.
    const QStringList arguments = payload.value(QStringLiteral("arguments")).toStringList();
    static const char *placeholders[] = {"%arg", "%arg2", "%arg3", "%arg4", "%arg5"};
    for (int i = arguments.size() - 1; i >= 0; --i) {
        if (i >= int(sizeof(placeholders) / sizeof(placeholders[0])))
            continue;
        text.replace(QLatin1String(placeholders[i]), translateOrKeep(arguments.at(i)));
    }

    return text.trimmed();
}

QString tuiGameEventText(const QVariantMap &payload, const TuiPlayerNameResolver &playerName)
{
    const auto text = [](const char *source) {
        return QCoreApplication::translate("QSanguoshaTui", source);
    };
    const auto who = [&](const char *field) {
        return resolveName(playerName, payload.value(QLatin1String(field)).toString());
    };
    const auto named = [&](const char *field) {
        return translateOrKeep(payload.value(QLatin1String(field)).toString());
    };

    // Only events the battle log cannot express reach the transcript. Dying,
    // skill changes, judge results and pindian all have log templates of their
    // own, so rendering them here would print every line twice.
    switch (payload.value(QStringLiteral("event")).toInt()) {
    case QSanProtocol::S_GAME_EVENT_PLAYER_QUITDYING:
        return text("%1 脫離瀕死").arg(who("player_name"));
    case QSanProtocol::S_GAME_EVENT_PLAYER_REFORM:
        return text("%1 重整").arg(who("player_name"));
    case QSanProtocol::S_GAME_EVENT_CHANGE_HERO:
        // send_log tells us the server already narrated this one.
        if (payload.value(QStringLiteral("send_log")).toBool())
            return QString();
        return text("%1 變更武將為 %2").arg(who("player_name"), named("general_name"));
    case QSanProtocol::S_GAME_EVENT_HUASHEN:
        return text("%1 因「%2」化身為 %3")
            .arg(who("player_name"), named("skill_name"), named("general_name"));
    default:
        // Audio, animation, avatars, hand sorting and the skill-cache events
        // drive desktop presentation or state the text client shows elsewhere.
        return QString();
    }
}

QString tuiPresentationEventText(int command, const QString &fallbackText,
                                 const QVariant &payload,
                                 const TuiPlayerNameResolver &playerName)
{
    switch (command) {
    case QSanProtocol::S_COMMAND_LOG_SKILL:
        return tuiSkillLogText(payload.toMap(), playerName);
    case QSanProtocol::S_COMMAND_LOG_EVENT:
        return tuiGameEventText(payload.toMap(), playerName);
    case QSanProtocol::S_COMMAND_SPEAK: {
        const QVariantMap chat = payload.toMap();
        const QString said = stripMarkup(chat.value(QStringLiteral("text")).toString());
        if (said.isEmpty())
            return QString();
        return QStringLiteral("%1: %2").arg(
            resolveName(playerName, chat.value(QStringLiteral("speaker")).toString()), said);
    }
    case QSanProtocol::S_COMMAND_ANIMATE:
        return QString();
    default:
        return fallbackText;
    }
}
