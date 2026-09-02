#ifndef CLIENT_LOG_FORMATTER_H
#define CLIENT_LOG_FORMATTER_H

#include <QString>
#include <QStringList>
#include <functional>

class Card;

struct ClientLogFormatRequest
{
    QString type;
    QString from;
    QStringList tos;
    QString cardString;
    QString arg;
    QString arg2;
    QString arg3;
    QString arg4;
    QString arg5;
};

// Phrase strings for the #UseCard branch. GUI passes Qt tr() so the desktop
// log box does not change; TUI/Web pass Engine translation table keys.
struct ClientLogUseCardPhrases
{
    QString usingText = QStringLiteral("using");
    QString playingText = QStringLiteral("playing");
    QString recastingText = QStringLiteral("recasting");
    QString useSkillText = QStringLiteral("use skill");
    QString carryOutText = QStringLiteral("carry out");
    QString effectText = QStringLiteral("effect");
    QString skillNoCost = QStringLiteral("%from %2 [%1] %3");
    QString skillCost = QStringLiteral("%from %3 [%1] %4, and the cost is %2");
    QString asNoSub = QStringLiteral("%from %4 [%1] %5, %3 [%2]");
    QString asSub = QStringLiteral("%from %5 [%1] %6 %4 %2 as %3");
    QString filterAs = QStringLiteral("%from 的 %2 因“%1”效果视为 %3 %4");
    QString plain = QStringLiteral("%from %2 %1");
    QString targetSuffix = QStringLiteral(", target is %to");
    QString selfName = QStringLiteral("自己");
};

struct ClientLogFormatStyle
{
    QString cardJoin = QStringLiteral(", ");
    QString toJoin = QStringLiteral(", ");
    ClientLogUseCardPhrases phrases;
    std::function<QString(const QString &)> translate;
    std::function<QString(const Card *)> cardLogName;
    std::function<const Card *(int id, bool useRoomCard)> cardById;
    std::function<QString(const QString &)> playerName;
    std::function<QString(const QString &)> wrapFrom;
    std::function<QString(const QString &)> wrapTo;
    std::function<QString(const QString &)> wrapArg;
    std::function<QString(const QString &)> wrapCard;
    std::function<void(const QString &from, const QStringList &tos)> onUseCardTargets;
};

// Empty means the line is dropped (hidden #FooCard, unparsable #UseCard).
QString formatClientLog(const ClientLogFormatRequest &request,
                        const ClientLogFormatStyle &style);

ClientLogUseCardPhrases engineUseCardPhrases();

#endif
