#include "tui-log-text.h"

#include "card.h"
#include "client-log-formatter.h"
#include "engine.h"
#include "protocol.h"
#include "tui-card-text.h"
#include "tui-renderer.h"

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

ClientLogFormatStyle tuiLogStyle(const TuiPlayerNameResolver &playerName)
{
    ClientLogFormatStyle style;
    style.cardJoin = QStringLiteral("、");
    style.toJoin = QStringLiteral("、");
    style.phrases = engineUseCardPhrases();
    style.translate = [](const QString &key) { return translateOrKeep(key); };
    // Without this the formatter falls back to Engine::getCard(), which needs a
    // room context the text client never registers: %card then resolved to
    // nothing and equip / damage-source lines lost their card entirely. No room
    // means no filtered card either, so both branches read the engine table.
    style.cardById = [](int id, bool) -> const Card * {
        return Sanguosha != nullptr ? Sanguosha->getEngineCard(id) : nullptr;
    };
    style.cardLogName = [](const Card *card) {
        if (card->getId() >= 0)
            return tuiCardDisplayText(card->getId());
        QString name = translateOrKeep(card->objectName());
        const QString skill = translateOrKeep(card->getSkillName());
        if (!skill.isEmpty() && skill != name)
            name = QStringLiteral("%1（%2）").arg(name, skill);
        return name;
    };
    style.playerName = [playerName](const QString &name) {
        return resolveName(playerName, name);
    };
    return style;
}

ClientLogFormatRequest requestFromSkillLog(const QVariantMap &payload)
{
    ClientLogFormatRequest request;
    request.type = payload.value(QStringLiteral("log_type")).toString();
    request.from = payload.value(QStringLiteral("from_player")).toString();
    request.tos = payload.value(QStringLiteral("to_players")).toStringList();
    request.cardString = payload.value(QStringLiteral("card_string")).toString();
    const QStringList arguments = payload.value(QStringLiteral("arguments")).toStringList();
    if (arguments.size() > 0)
        request.arg = arguments.at(0);
    if (arguments.size() > 1)
        request.arg2 = arguments.at(1);
    if (arguments.size() > 2)
        request.arg3 = arguments.at(2);
    if (arguments.size() > 3)
        request.arg4 = arguments.at(3);
    if (arguments.size() > 4)
        request.arg5 = arguments.at(4);
    return request;
}

} // namespace

QString tuiSkillLogText(const QVariantMap &payload, const TuiPlayerNameResolver &playerName)
{
    // lang writes several templates for the desktop log box, tags and all --
    // "#AskForPeaches" asks for a <b><font>桃</font></b>.
    return TuiRenderer::plainText(
        formatClientLog(requestFromSkillLog(payload), tuiLogStyle(playerName)));
}

QString tuiGameEventText(const QVariantMap &payload, const TuiPlayerNameResolver &playerName)
{
    ClientLogFormatRequest request;
    request.from = payload.value(QStringLiteral("player_name")).toString();
    switch (payload.value(QStringLiteral("event")).toInt()) {
    case QSanProtocol::S_GAME_EVENT_PLAYER_QUITDYING:
        request.type = QStringLiteral("#QuitDying");
        break;
    case QSanProtocol::S_GAME_EVENT_PLAYER_REFORM:
        request.type = QStringLiteral("#PlayerReform");
        break;
    case QSanProtocol::S_GAME_EVENT_CHANGE_HERO:
        if (payload.value(QStringLiteral("send_log")).toBool())
            return QString();
        request.type = QStringLiteral("#ChangeHero");
        request.arg = payload.value(QStringLiteral("general_name")).toString();
        break;
    case QSanProtocol::S_GAME_EVENT_HUASHEN:
        request.type = QStringLiteral("#HuaShen");
        request.arg = payload.value(QStringLiteral("skill_name")).toString();
        request.arg2 = payload.value(QStringLiteral("general_name")).toString();
        break;
    default:
        return QString();
    }
    return TuiRenderer::plainText(formatClientLog(request, tuiLogStyle(playerName)));
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
        const QString said = TuiRenderer::plainText(chat.value(QStringLiteral("text")).toString());
        if (said.isEmpty())
            return QString();
        return QStringLiteral("%1: %2").arg(
            resolveName(playerName, chat.value(QStringLiteral("speaker")).toString()), said);
    }
    case QSanProtocol::S_COMMAND_ANIMATE:
    case QSanProtocol::S_COMMAND_SET_EMOTION:
        return QString();
    default:
        return fallbackText;
    }
}
