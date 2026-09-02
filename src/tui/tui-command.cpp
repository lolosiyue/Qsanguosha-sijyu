#include "tui-command.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>
#include <QtGlobal>

namespace {

QString tr(const char *source)
{
    return QCoreApplication::translate("QSanguoshaTui", source);
}

bool reject(QString *error, const QString &detail)
{
    if (error != nullptr)
        *error = detail;
    return false;
}

bool rejectsArgument(TuiCommandType type)
{
    switch (type) {
    case TuiCommandType::Help:
    case TuiCommandType::Status:
    case TuiCommandType::Players:
    case TuiCommandType::Hand:
    case TuiCommandType::Equipment:
    case TuiCommandType::Piles:
    case TuiCommandType::Skills:
    case TuiCommandType::Log:
    case TuiCommandType::Surrender:
    case TuiCommandType::Reconnect:
    case TuiCommandType::Quit:
    case TuiCommandType::Cancel:
        return true;
    default:
        return false;
    }
}

const QHash<QString, TuiCommandType> &commandTable()
{
    static const QHash<QString, TuiCommandType> commands{
        {QStringLiteral("/help"), TuiCommandType::Help},
        {QStringLiteral("/status"), TuiCommandType::Status},
        {QStringLiteral("/players"), TuiCommandType::Players},
        {QStringLiteral("/hand"), TuiCommandType::Hand},
        {QStringLiteral("/equip"), TuiCommandType::Equipment},
        {QStringLiteral("/piles"), TuiCommandType::Piles},
        {QStringLiteral("/skills"), TuiCommandType::Skills},
        {QStringLiteral("/log"), TuiCommandType::Log},
        {QStringLiteral("/chat"), TuiCommandType::Chat},
        {QStringLiteral("/trust"), TuiCommandType::Trust},
        {QStringLiteral("/addrobot"), TuiCommandType::AddRobot},
        {QStringLiteral("/surrender"), TuiCommandType::Surrender},
        {QStringLiteral("/reconnect"), TuiCommandType::Reconnect},
        {QStringLiteral("/quit"), TuiCommandType::Quit},
        {QStringLiteral("/cancel"), TuiCommandType::Cancel}
    };
    return commands;
}

QStringList argumentCandidates(const QString &command)
{
    if (command == QLatin1String("/trust"))
        return {QStringLiteral("on"), QStringLiteral("off")};
    if (command == QLatin1String("/addrobot"))
        return {QStringLiteral("all")};
    return {};
}

QString commonPrefix(const QStringList &matches)
{
    if (matches.isEmpty())
        return {};
    QString prefix = matches.first();
    for (const QString &item : matches) {
        int n = 0;
        const int limit = int(qMin(prefix.size(), item.size()));
        while (n < limit && prefix.at(n).toLower() == item.at(n).toLower())
            ++n;
        prefix.truncate(n);
        if (prefix.isEmpty())
            break;
    }
    return prefix;
}

QStringList matchingTokens(const QStringList &candidates, const QString &prefix)
{
    QSet<QString> seen;
    QStringList matches;
    for (const QString &candidate : candidates) {
        if (candidate.isEmpty() || seen.contains(candidate))
            continue;
        if (!candidate.startsWith(prefix, Qt::CaseInsensitive))
            continue;
        seen.insert(candidate);
        matches.append(candidate);
    }
    matches.sort(Qt::CaseInsensitive);
    return matches;
}

} // namespace

bool TuiCommandParser::parse(const QString &line, TuiCommandIntent *intent,
                             QString *error)
{
    if (error != nullptr)
        error->clear();
    if (intent == nullptr)
        return reject(error, tr("命令 intent 输出不可为 null"));
    *intent = TuiCommandIntent();

    const QString input = line.trimmed();
    if (!input.startsWith(QLatin1Char('/')))
        return reject(error, tr("全域命令必须以 '/' 开头"));
    if (input.size() > 4096)
        return reject(error, tr("命令超过 4096 字元"));

    const qsizetype separator = input.indexOf(QLatin1Char(' '));
    const QString keyword = (separator < 0 ? input : input.left(separator)).toLower();
    const QString argument = separator < 0 ? QString() : input.mid(separator + 1).trimmed();
    const auto found = commandTable().constFind(keyword);
    if (found == commandTable().cend())
        return reject(error, tr("未知命令：%1").arg(keyword));

    TuiCommandIntent parsed;
    parsed.type = found.value();
    if (rejectsArgument(parsed.type) && !argument.isEmpty())
        return reject(error, tr("%1 不接受参数").arg(keyword));
    if (parsed.type == TuiCommandType::Chat) {
        if (argument.isEmpty())
            return reject(error, tr("/chat 必须包含文字"));
        if (argument.size() > 1000)
            return reject(error, tr("/chat 最多 1000 字元"));
        parsed.text = argument;
    } else if (parsed.type == TuiCommandType::Trust) {
        if (argument.isEmpty()) {
            parsed.trustMode = TuiTrustMode::Toggle;
        } else if (argument.compare(QStringLiteral("on"), Qt::CaseInsensitive) == 0) {
            parsed.trustMode = TuiTrustMode::Enable;
        } else if (argument.compare(QStringLiteral("off"), Qt::CaseInsensitive) == 0) {
            parsed.trustMode = TuiTrustMode::Disable;
        } else {
            return reject(error, tr("/trust 只接受 'on' 或 'off'"));
        }
    } else if (parsed.type == TuiCommandType::AddRobot) {
        if (argument.isEmpty()
            || argument.compare(QStringLiteral("all"), Qt::CaseInsensitive) == 0) {
            parsed.fillRemaining = true;
        } else {
            bool ok = false;
            parsed.count = argument.toInt(&ok);
            if (!ok || parsed.count <= 0 || parsed.count > 64)
                return reject(error, tr("/addrobot 只接受 'all' 或 1 至 64 的数量"));
        }
    }
    *intent = parsed;
    return true;
}

QStringList tuiCommandNames()
{
    QStringList names = commandTable().keys();
    names.sort(Qt::CaseInsensitive);
    return names;
}

TuiCompletion completeTuiLine(const QString &line, const QStringList &extraTokens)
{
    TuiCompletion result;
    result.line = line;
    const QString trimmed = line.trimmed();
    const bool trailingSpace = !line.isEmpty() && line.back().isSpace();
    const qsizetype firstSpace = trimmed.indexOf(QLatin1Char(' '));
    const QString firstWord = (firstSpace < 0 ? trimmed : trimmed.left(firstSpace)).toLower();
    const bool completingCommand = trimmed.startsWith(QLatin1Char('/'))
        && firstSpace < 0 && !trailingSpace;

    QStringList pool;
    if (completingCommand)
        pool = tuiCommandNames();
    else if (trimmed.startsWith(QLatin1Char('/')))
        pool = argumentCandidates(firstWord);
    else
        pool = extraTokens;

    QString prefix;
    QString token;
    if (line.isEmpty()) {
        prefix.clear();
        token.clear();
    } else if (trailingSpace) {
        prefix = line;
        token.clear();
    } else {
        qsizetype lastSpace = -1;
        for (qsizetype i = line.size() - 1; i >= 0; --i) {
            if (line.at(i).isSpace()) {
                lastSpace = i;
                break;
            }
        }
        if (lastSpace < 0) {
            prefix.clear();
            token = line;
        } else {
            prefix = line.left(lastSpace + 1);
            token = line.mid(lastSpace + 1);
        }
    }

    result.matches = matchingTokens(pool, token);
    if (result.matches.isEmpty())
        return result;
    if (result.matches.size() == 1) {
        result.line = prefix + result.matches.first();
        if (completingCommand)
            result.line.append(QLatin1Char(' '));
        return result;
    }
    const QString shared = commonPrefix(result.matches);
    if (!shared.isEmpty() && shared.size() > token.size())
        result.line = prefix + shared;
    return result;
}
