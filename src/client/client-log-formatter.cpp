#include "client-log-formatter.h"

#include "card.h"
#include "engine.h"

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
