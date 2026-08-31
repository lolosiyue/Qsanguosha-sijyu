#include "core/engine-bootstrap.h"
#include "core/engine.h"
#include "core/runtime-paths.h"
#include "core/version.h"
#include "tui-application-controller.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QLocale>
#include <QStringConverter>
#include <QTextStream>
#include <QTranslator>

#if defined(Q_OS_WIN)
#include <qt_windows.h>
#else
#include <cstdio>
#include <unistd.h>
#endif

namespace {

constexpr int UsageExitCode = 2;
constexpr int RuntimeExitCode = 6;

#if defined(Q_OS_WIN)
class ConsoleCodePageGuard
{
public:
    ConsoleCodePageGuard()
        : m_inputCodePage(GetConsoleCP()), m_outputCodePage(GetConsoleOutputCP())
    {
        SetConsoleCP(CP_UTF8);
        SetConsoleOutputCP(CP_UTF8);
    }

    ~ConsoleCodePageGuard()
    {
        if (m_inputCodePage != 0)
            SetConsoleCP(m_inputCodePage);
        if (m_outputCodePage != 0)
            SetConsoleOutputCP(m_outputCodePage);
    }

private:
    UINT m_inputCodePage;
    UINT m_outputCodePage;
};
#endif

QString tr(const char *source)
{
    return QCoreApplication::translate("QSanguoshaTui", source);
}

bool outputIsTerminal()
{
#if defined(Q_OS_WIN)
    DWORD mode = 0;
    return GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &mode) != FALSE;
#else
    return isatty(fileno(stdout)) != 0;
#endif
}

int usageError(const QString &message)
{
    QTextStream stream(stderr);
    stream.setEncoding(QStringConverter::Utf8);
    stream << "TUI_ERROR usage: " << message << '\n';
    return UsageExitCode;
}

void writeUtf8(FILE *device, const QString &text)
{
    QTextStream stream(device);
    stream.setEncoding(QStringConverter::Utf8);
    stream << text;
    stream.flush();
}

} // namespace

int main(int argc, char *argv[])
{
#if defined(Q_OS_WIN)
    ConsoleCodePageGuard consoleCodePage;
#endif
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("qsanguosha_tui"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(QSanVersion::Number));

    QCommandLineParser parser;
    parser.setApplicationDescription(tr("QSanguosha Protocol V2 終端客戶端"));
    const QCommandLineOption helpOption(
        QStringList{QStringLiteral("?"), QStringLiteral("h"), QStringLiteral("help"),
                    QStringLiteral("help-all")},
        tr("顯示命令列選項說明"));
    const QCommandLineOption versionOption(
        QStringList{QStringLiteral("v"), QStringLiteral("version")},
        tr("顯示版本資訊"));

    const QCommandLineOption hostOption(QStringLiteral("host"),
        tr("伺服器主機名稱或位址"), QStringLiteral("host"),
        QStringLiteral("127.0.0.1"));
    const QCommandLineOption portOption(QStringLiteral("port"),
        tr("伺服器 TCP 連接埠"), QStringLiteral("port"), QStringLiteral("9527"));
    const QCommandLineOption nameOption(QStringLiteral("name"),
        tr("玩家顯示名稱"), QStringLiteral("name"), QStringLiteral("TUI"));
    const QCommandLineOption avatarOption(QStringLiteral("avatar"),
        tr("玩家頭像識別字"), QStringLiteral("avatar"),
        QStringLiteral("caocao"));
    const QCommandLineOption reconnectOption(QStringLiteral("reconnect"),
        tr("初次登入時請求重連"));
    const QCommandLineOption plainOption(QStringLiteral("plain"),
        tr("使用確定性的純文字輸出"));
    const QCommandLineOption noColorOption(QStringLiteral("no-color"),
        tr("即使在終端也停用 ANSI 色彩"));
    const QCommandLineOption languageOption(QStringLiteral("language"),
        tr("設定程序語系"), QStringLiteral("locale"));
    const QCommandLineOption logFileOption(QStringLiteral("log-file"),
        tr("將清理後的語意輸出附加至檔案"), QStringLiteral("path"));
    const QCommandLineOption scriptOption(QStringLiteral("script"),
        tr("從腳本執行命令與斷言"), QStringLiteral("path"));
    const QCommandLineOption assetRootOption(QStringLiteral("asset-root"),
        tr("使用明確的執行期資料根目錄"), QStringLiteral("directory"));

    parser.addOptions({helpOption, versionOption, hostOption, portOption, nameOption,
        avatarOption, reconnectOption, plainOption, noColorOption, languageOption,
        logFileOption, scriptOption, assetRootOption});

    // QCommandLineParser's automatic help path uses the Windows local code page
    // when stdout is redirected. Emit these two process-local responses as UTF-8
    // so console and CI/package smoke output have identical bytes.
    const QStringList rawArguments = app.arguments();
    if (rawArguments.contains(QStringLiteral("--help"))
        || rawArguments.contains(QStringLiteral("-h"))
        || rawArguments.contains(QStringLiteral("-?"))
        || rawArguments.contains(QStringLiteral("--help-all"))) {
        QString helpText = parser.helpText();
        helpText.replace(QStringLiteral("Usage:"), tr("用法："));
        helpText.replace(QStringLiteral("Options:"), tr("選項："));
        writeUtf8(stdout, helpText);
        return 0;
    }
    if (rawArguments.contains(QStringLiteral("--version"))
        || rawArguments.contains(QStringLiteral("-v"))) {
        writeUtf8(stdout, QStringLiteral("%1 %2\n")
            .arg(QCoreApplication::applicationName(),
                 QCoreApplication::applicationVersion()));
        return 0;
    }
    parser.process(app);

    bool portOk = false;
    const int port = parser.value(portOption).toInt(&portOk);
    if (!portOk || port < 1 || port > 65535)
        return usageError(tr("--port 必須是 1 至 65535 的整數"));
    const QString host = parser.value(hostOption).trimmed();
    const QString screenName = parser.value(nameOption).trimmed();
    const QString avatar = parser.value(avatarOption).trimmed();
    if (host.isEmpty())
        return usageError(tr("--host 不可為空"));
    if (screenName.isEmpty())
        return usageError(tr("--name 不可為空"));
    if (avatar.isEmpty())
        return usageError(tr("--avatar 不可為空"));

    if (parser.isSet(languageOption)) {
        const QLocale locale(parser.value(languageOption));
        if (locale.language() == QLocale::C)
            return usageError(tr("--language 不是可識別的語系"));
        QLocale::setDefault(locale);
    }

    QString error;
    if (!QSanRuntimePaths::resolve(app.arguments(), &error)) {
        QTextStream err(stderr);
        err << "TUI_ERROR runtime_paths: " << error << '\n';
        for (const QString &candidate : QSanRuntimePaths::resolution().candidates)
            err << "  tried " << candidate << '\n';
        return RuntimeExitCode;
    }
    QTranslator tuiTranslator;
    const QString localeName = QLocale().name();
    const QStringList translationCandidates{
        QSanRuntimePaths::assetPath(
            QStringLiteral("translations/qsanguosha_tui_%1.qm").arg(localeName)),
        QSanRuntimePaths::assetPath(
            QStringLiteral("qsanguosha_tui_%1.qm").arg(localeName))};
    for (const QString &candidate : translationCandidates) {
        if (tuiTranslator.load(candidate)) {
            app.installTranslator(&tuiTranslator);
            break;
        }
    }
    if (!EngineBootstrap::initialize(false, &error)) {
        QTextStream(stderr) << "TUI_ERROR engine: " << error << '\n';
        return RuntimeExitCode;
    }
    QObject::disconnect(&app, SIGNAL(aboutToQuit()), Sanguosha, SLOT(deleteLater()));

    TuiApplicationOptions options;
    options.session.host = host;
    options.session.port = static_cast<quint16>(port);
    options.session.screenName = screenName;
    options.session.avatar = avatar;
    options.session.reconnectRequested = parser.isSet(reconnectOption);
    options.ansiEnabled = outputIsTerminal() && !parser.isSet(plainOption)
        && !parser.isSet(noColorOption) && !qEnvironmentVariableIsSet("NO_COLOR");
    options.logFile = parser.value(logFileOption);
    options.scriptFile = parser.value(scriptOption);

    int result = RuntimeExitCode;
    {
        TuiApplicationController controller(options);
        if (!controller.start(&error)) {
            QTextStream(stderr) << "TUI_ERROR startup: " << error << '\n';
        } else {
            result = app.exec();
        }
    }
    EngineBootstrap::shutdown();
    return result;
}
