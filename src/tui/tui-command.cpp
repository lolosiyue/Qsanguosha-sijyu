#include "tui-command.h"

#include <QCoreApplication>
#include <QHash>

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

} // namespace

bool TuiCommandParser::parse(const QString &line, TuiCommandIntent *intent,
                             QString *error)
{
    if (error != nullptr)
        error->clear();
    if (intent == nullptr)
        return reject(error, tr("命令 intent 輸出不可為 null"));
    *intent = TuiCommandIntent();

    const QString input = line.trimmed();
    if (!input.startsWith(QLatin1Char('/')))
        return reject(error, tr("全域命令必須以 '/' 開頭"));
    if (input.size() > 4096)
        return reject(error, tr("命令超過 4096 字元"));

    const qsizetype separator = input.indexOf(QLatin1Char(' '));
    const QString keyword = (separator < 0 ? input : input.left(separator)).toLower();
    const QString argument = separator < 0 ? QString() : input.mid(separator + 1).trimmed();
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
    const auto found = commands.constFind(keyword);
    if (found == commands.cend())
        return reject(error, tr("未知命令：%1").arg(keyword));

    TuiCommandIntent parsed;
    parsed.type = found.value();
    if (rejectsArgument(parsed.type) && !argument.isEmpty())
        return reject(error, tr("%1 不接受參數").arg(keyword));
    if (parsed.type == TuiCommandType::Chat) {
        if (argument.isEmpty())
            return reject(error, tr("/chat 必須包含文字"));
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
                return reject(error, tr("/addrobot 只接受 'all' 或 1 至 64 的數量"));
        }
    }
    *intent = parsed;
    return true;
}
