#include "client-log-formatter.h"

#include "card.h"
#include "engine.h"
#include "protocol.h"
#include <QList>
#include <QPair>
#include <QSet>

namespace {

QString wrapOr(const std::function<QString(const QString &)> &wrap, const QString &text)
{
    return wrap ? wrap(text) : text;
}

QString translateOr(const ClientLogFormatStyle &style, const QString &key)
{
    if (key.isEmpty())
        return key;
    if (style.translate)
        return style.translate(key);
    if (Sanguosha == nullptr)
        return key;
    const QString translated = Sanguosha->translate(key);
    return translated.isEmpty() ? key : translated;
}

QString cardNameOf(const ClientLogFormatStyle &style, const Card *card)
{
    if (card == nullptr)
        return QString();
    const QString name = style.cardLogName ? style.cardLogName(card) : card->getLogName();
    return wrapOr(style.wrapCard, name);
}

QString dollarCards(const ClientLogFormatRequest &request, const ClientLogFormatStyle &style)
{
    QStringList names;
    const bool useRoomCard = request.type == QLatin1String("$JudgeResult")
        || request.type == QLatin1String("$PasteCard");
    for (const QString &token : request.cardString.split(QLatin1Char('+'), Qt::SkipEmptyParts)) {
        const Card *card = nullptr;
        if (style.cardById)
            card = style.cardById(token.toInt(), useRoomCard);
        else if (Sanguosha != nullptr) {
            card = useRoomCard ? Sanguosha->getCard(token.toInt())
                               : Sanguosha->getEngineCard(token.toInt());
        }
        if (card != nullptr)
            names << cardNameOf(style, card);
    }
    return names.join(style.cardJoin);
}

QString genericCards(const ClientLogFormatRequest &request, const ClientLogFormatStyle &style)
{
    const Card *parsed = Card::Parse(request.cardString);
    if (parsed != nullptr)
        return cardNameOf(style, parsed);
    QStringList names;
    for (const QString &token : request.cardString.split(QLatin1Char('+'), Qt::SkipEmptyParts)) {
        const Card *card = nullptr;
        if (style.cardById)
            card = style.cardById(token.toInt(), true);
        else if (Sanguosha != nullptr)
            card = Sanguosha->getCard(token.toInt());
        if (card != nullptr)
            names << cardNameOf(style, card);
    }
    return names.join(style.cardJoin);
}

QString useCardSentence(const ClientLogFormatRequest &request, const ClientLogFormatStyle &style)
{
    const Card *card = Card::Parse(request.cardString);
    if (card == nullptr) {
        bool ok = false;
        const int id = request.cardString.toInt(&ok);
        if (ok) {
            if (style.cardById)
                card = style.cardById(id, false);
            else if (Sanguosha != nullptr)
                card = Sanguosha->getEngineCard(id);
        }
    }
    if (card == nullptr)
        return QString();
    if (card->objectName().startsWith(QLatin1Char('#')))
        return QString();
    if (style.onUseCardTargets)
        style.onUseCardTargets(request.from, request.tos);

    const ClientLogUseCardPhrases &phrase = style.phrases;
    QString reason = phrase.usingText;
    if (request.type.endsWith(QLatin1String("_Resp")))
        reason = phrase.playingText;
    if (request.type.endsWith(QLatin1String("_Recast")))
        reason = phrase.recastingText;
    const QString cardName = cardNameOf(style, card);
    QString skillName = card->getSkillName();
    // Parsing @ActiveSkillCard alone derives "activeskill". Use the server's
    // explicit log identity; ordinary card logs retain their existing names.
    if (qobject_cast<const ActiveSkillCard *>(card) && !request.arg.isEmpty())
        skillName = request.arg;
    QString log;
    if (card->isVirtualCard()) {
        const bool eff = card->getTypeId() > 0 && card->getSkillName(false) != skillName;
        const QString meth = eff ? phrase.carryOutText : phrase.useSkillText;
        const QString suffix = eff ? phrase.effectText : QString();
        QStringList subcards;
        foreach (int id, card->getSubcards()) {
            const Card *sub = nullptr;
            if (style.cardById)
                sub = style.cardById(id, false);
            else if (Sanguosha != nullptr)
                sub = Sanguosha->getEngineCard(id);
            if (sub != nullptr)
                subcards << cardNameOf(style, sub);
        }
        skillName = wrapOr(style.wrapCard, translateOr(style, skillName));
        if (card->inherits("SkillCard") && !card->isKindOf("YanxiaoCard")) {
            if (subcards.isEmpty() || !card->willThrow())
                log = phrase.skillNoCost.arg(skillName).arg(meth).arg(suffix);
            else
                log = phrase.skillCost.arg(skillName).arg(subcards.join(style.cardJoin))
                          .arg(meth).arg(suffix);
        } else if (subcards.isEmpty()) {
            log = phrase.asNoSub.arg(skillName).arg(cardName).arg(reason).arg(meth).arg(suffix);
        } else {
            log = phrase.asSub.arg(skillName).arg(subcards.join(style.cardJoin)).arg(cardName)
                      .arg(reason).arg(meth).arg(suffix);
        }
    } else if (!skillName.isEmpty()) {
        skillName = wrapOr(style.wrapCard, translateOr(style, skillName));
        QString subcard = cardName;
        if (style.cardById) {
            const Card *engineCard = style.cardById(card->getId(), false);
            if (engineCard != nullptr)
                subcard = cardNameOf(style, engineCard);
        } else if (Sanguosha != nullptr) {
            const Card *engineCard = Sanguosha->getEngineCard(card->getId());
            if (engineCard != nullptr)
                subcard = cardNameOf(style, engineCard);
        }
        log = phrase.filterAs.arg(skillName).arg(subcard).arg(cardName).arg(reason);
    } else {
        log = phrase.plain.arg(cardName).arg(reason);
    }
    if (!request.tos.isEmpty())
        log.append(phrase.targetSuffix);
    return log;
}

} // namespace

ClientLogUseCardPhrases engineUseCardPhrases()
{
    ClientLogUseCardPhrases phrases;
    if (Sanguosha == nullptr)
        return phrases;
    const auto take = [](const QString &key, const QString &fallback) {
        const QString translated = Sanguosha->translate(key);
        return translated.isEmpty() || translated == key ? fallback : translated;
    };
    phrases.usingText = take(QStringLiteral("#UseCardPhrase_using"), phrases.usingText);
    phrases.playingText = take(QStringLiteral("#UseCardPhrase_playing"), phrases.playingText);
    phrases.recastingText = take(QStringLiteral("#UseCardPhrase_recasting"), phrases.recastingText);
    phrases.useSkillText = take(QStringLiteral("#UseCardPhrase_useSkill"), phrases.useSkillText);
    phrases.carryOutText = take(QStringLiteral("#UseCardPhrase_carryOut"), phrases.carryOutText);
    phrases.effectText = take(QStringLiteral("#UseCardPhrase_effect"), phrases.effectText);
    phrases.skillNoCost = take(QStringLiteral("#UseCardPhrase_skill"), phrases.skillNoCost);
    phrases.skillCost = take(QStringLiteral("#UseCardPhrase_skillCost"), phrases.skillCost);
    phrases.asNoSub = take(QStringLiteral("#UseCardPhrase_as"), phrases.asNoSub);
    phrases.asSub = take(QStringLiteral("#UseCardPhrase_asSub"), phrases.asSub);
    phrases.filterAs = take(QStringLiteral("#UseCardPhrase_filter"), phrases.filterAs);
    phrases.plain = take(QStringLiteral("#UseCardPhrase_plain"), phrases.plain);
    phrases.targetSuffix = take(QStringLiteral("#UseCardPhrase_target"), phrases.targetSuffix);
    phrases.selfName = take(QStringLiteral("#LogSelf"), phrases.selfName);
    return phrases;
}

QString formatClientLog(const ClientLogFormatRequest &request, const ClientLogFormatStyle &style)
{
    if (request.type.isEmpty())
        return QString();
    if (request.type == QLatin1String("$AppendSeparator"))
        return QStringLiteral("--------");

    QString log;
    if (request.type.startsWith(QLatin1String("#UseCard")) && !request.from.isEmpty()) {
        log = useCardSentence(request, style);
        if (log.isEmpty())
            return QString();
    } else {
        log = translateOr(style, request.type);
        if (!request.cardString.isEmpty()) {
            if (request.type.startsWith(QLatin1Char('$')))
                log.replace(QStringLiteral("%card"), dollarCards(request, style));
            else
                log.replace(QStringLiteral("%card"), genericCards(request, style));
        }
    }

    if (!request.from.isEmpty()) {
        const QString from = style.playerName ? style.playerName(request.from) : request.from;
        log.replace(QStringLiteral("%from"), wrapOr(style.wrapFrom, from));
    }
    if (!request.tos.isEmpty()) {
        QStringList names;
        for (const QString &to : request.tos) {
            if (to == request.from)
                names << style.phrases.selfName;
            else
                names << (style.playerName ? style.playerName(to) : to);
        }
        log.replace(QStringLiteral("%to"), wrapOr(style.wrapTo, names.join(style.toJoin)));
    }

    const QString args[] = {request.arg, request.arg2, request.arg3, request.arg4, request.arg5};
    static const char *placeholders[] = {"%arg", "%arg2", "%arg3", "%arg4", "%arg5"};
    for (int i = 4; i >= 0; --i) {
        if (!log.contains(QLatin1String(placeholders[i])))
            continue;
        log.replace(QLatin1String(placeholders[i]),
                    wrapOr(style.wrapArg, translateOr(style, args[i])));
    }
    return log.trimmed();
}

// Shared text frontend presentation, originally implemented by the TUI.
QString clientPlainLogText(const QString &text)
{
    static const QSet<QString> breaks{QStringLiteral("br"), QStringLiteral("p"),
        QStringLiteral("div"), QStringLiteral("tr")};

    QString result;
    result.reserve(text.size());
    QString tag;
    bool inTag = false;
    for (QChar character : text) {
        if (character == QLatin1Char('<')) {
            inTag = true;
            tag.clear();
        } else if (inTag && character == QLatin1Char('>')) {
            inTag = false;
            // "br", "br/", "br /" and "/br" all name the same tag.
            const QString name = tag.trimmed().section(QLatin1Char(' '), 0, 0)
                .remove(QLatin1Char('/')).toLower();
            if (breaks.contains(name))
                result.append(QLatin1Char(' '));
        } else if (inTag) {
            tag.append(character);
        } else {
            result.append(character);
        }
    }
    // A lone "<" a player typed in chat is text, not the start of a tag.
    if (inTag)
        result.append(QLatin1Char('<')).append(tag);

    // The same templates escape the characters they cannot spell literally.
    static const QList<QPair<QString, QString>> entities{
        {QStringLiteral("&nbsp;"), QStringLiteral(" ")},
        {QStringLiteral("&lt;"), QStringLiteral("<")},
        {QStringLiteral("&gt;"), QStringLiteral(">")},
        {QStringLiteral("&quot;"), QStringLiteral("\"")},
        {QStringLiteral("&#39;"), QStringLiteral("'")},
        {QStringLiteral("&amp;"), QStringLiteral("&")}};
    for (const auto &entity : entities)
        result.replace(entity.first, entity.second, Qt::CaseInsensitive);

    return result.simplified();
}

QString clientCardDisplayText(int cardId)
{
    // Prefer the room's own copy: a card the server has rewritten (a suit or a
    // name changed by a skill) only differs from the printed card there.
    // Engine::getCard() resolves through the registered room context, so it is
    // null whenever no game is running -- getEngineCard() reads the engine's own
    // table and needs no room, which is the right answer then.
    const Card *card = nullptr;
    if (Sanguosha != nullptr) {
        card = Sanguosha->getCard(cardId);
        if (card == nullptr)
            card = Sanguosha->getEngineCard(cardId);
    }
    if (card == nullptr) {
        const QString key = QStringLiteral("tui_card_unknown");
        const QString text = Sanguosha ? Sanguosha->translate(key) : key;
        return (text.isEmpty() ? key : text).arg(cardId);
    }

    QString name = Sanguosha->translate(card->objectName());
    if (name.isEmpty())
        name = card->objectName();

    QString suit;
    if (card->getSuit() != Card::NoSuit) {
        suit = Sanguosha->translate(card->getSuitString());
        if (suit.isEmpty())
            suit = card->getSuitString();
    }
    const QString number = card->getNumber() > 0 ? card->getNumberString() : QString();

    if (suit.isEmpty() && number.isEmpty())
        return name;
    return QStringLiteral("%1[%2%3]").arg(name, suit, number);
}

QString clientLogPlayerName(const QVariantMap &player, const QString &objectName)
{
    QString general = player.value(QStringLiteral("general")).toString();
    if (general.isEmpty())
        general = player.value(QStringLiteral("avatar")).toString();
    if (general.isEmpty())
        return objectName;
    auto translate = [](const QString &name) {
        if (Sanguosha == nullptr)
            return name;
        const QString translated = Sanguosha->translate(name);
        return translated.isEmpty() ? name : translated;
    };
    QString name = translate(general);
    const QString deputy = player.value(QStringLiteral("deputy_general")).toString();
    if (!deputy.isEmpty())
        name += QLatin1Char('/') + translate(deputy);
    return name;
}


namespace {

QString translateOrKeep(const QString &key)
{
    if (key.isEmpty() || Sanguosha == nullptr)
        return key;
    const QString translated = Sanguosha->translate(key);
    return translated.isEmpty() ? key : translated;
}

QString resolveName(const ClientLogPlayerNameResolver &playerName, const QString &objectName)
{
    if (objectName.isEmpty())
        return QString();
    const QString resolved = playerName ? playerName(objectName) : QString();
    return resolved.isEmpty() ? objectName : resolved;
}

ClientLogFormatStyle clientTextLogStyle(const ClientLogPlayerNameResolver &playerName)
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
    style.cardById = [](int id, bool useRoomCard) -> const Card * {
        if (Sanguosha == nullptr)
            return nullptr;
        // $JudgeResult / $PasteCard want the card as the room currently holds
        // it; the room is only registered while a game is running, so fall back
        // to the printed card either way.
        const Card *card = useRoomCard ? Sanguosha->getCard(id) : nullptr;
        return card != nullptr ? card : Sanguosha->getEngineCard(id);
    };
    style.cardLogName = [](const Card *card) {
        if (card->getId() >= 0)
            return clientCardDisplayText(card->getId());
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

QString formatClientSkillLogText(const QVariantMap &payload, const ClientLogPlayerNameResolver &playerName)
{
    // lang writes several templates for the desktop log box, tags and all --
    // "#AskForPeaches" asks for a <b><font>桃</font></b>.
    return clientPlainLogText(
        formatClientLog(requestFromSkillLog(payload), clientTextLogStyle(playerName)));
}

QString formatClientGameEventText(const QVariantMap &payload, const ClientLogPlayerNameResolver &playerName)
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
    return clientPlainLogText(formatClientLog(request, clientTextLogStyle(playerName)));
}

QString formatClientPresentationText(int command, const QString &fallbackText,
                                 const QVariant &payload,
                                 const ClientLogPlayerNameResolver &playerName)
{
    switch (command) {
    case QSanProtocol::S_COMMAND_LOG_SKILL:
        return formatClientSkillLogText(payload.toMap(), playerName);
    case QSanProtocol::S_COMMAND_LOG_EVENT:
        return formatClientGameEventText(payload.toMap(), playerName);
    case QSanProtocol::S_COMMAND_SPEAK: {
        const QVariantMap chat = payload.toMap();
        const QString said = clientPlainLogText(chat.value(QStringLiteral("text")).toString());
        if (said.isEmpty())
            return QString();
        return QStringLiteral("%1: %2").arg(
            resolveName(playerName, chat.value(QStringLiteral("speaker")).toString()), said);
    }
    case QSanProtocol::S_COMMAND_ANIMATE:
    case QSanProtocol::S_COMMAND_SET_EMOTION:
    // The desktop answers these with a skill bubble on the avatar and a table
    // repaint; its log box stays silent, and the battle log already carries
    // #InvokeSkill / #TriggerSkill for anything worth reading. Without this the
    // transcript shows the reducer's own debug text ("sgs2 invoked eight_diagram").
    case QSanProtocol::S_COMMAND_INVOKE_SKILL:
    case QSanProtocol::S_COMMAND_CHANGE_TABLE_BG:
        return QString();
    default:
        return fallbackText;
    }
}
