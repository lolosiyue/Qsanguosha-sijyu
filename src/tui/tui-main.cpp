#include "card.h"
#include "core/engine-bootstrap.h"
#include "core/engine.h"
#include "core/runtime-paths.h"
#include "core/version.h"
#include "tui-application-controller.h"
#include "tui-text.h"
#include "tui-ui-mode.h"
#include "tui-terminal.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QStringConverter>
#include <QStringList>
#include <QTextStream>
#include <QTranslator>

#include <iostream>
#include <string>

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

bool inputIsTerminal()
{
#if defined(Q_OS_WIN)
    DWORD mode = 0;
    return GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) != FALSE;
#else
    return isatty(fileno(stdin)) != 0;
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
    parser.setApplicationDescription(tr("QSanguosha Protocol V2 终端客户端"));
    const QCommandLineOption helpOption(
        QStringList{QStringLiteral("?"), QStringLiteral("h"), QStringLiteral("help"),
                    QStringLiteral("help-all")},
        tr("显示命令行选项帮助"));
    const QCommandLineOption versionOption(
        QStringList{QStringLiteral("v"), QStringLiteral("version")},
        tr("显示版本信息"));

    const QCommandLineOption hostOption(QStringLiteral("host"),
        tr("服务器主机名称或地址"), QStringLiteral("host"),
        QStringLiteral("127.0.0.1"));
    const QCommandLineOption portOption(QStringLiteral("port"),
        tr("服务器 TCP 端口"), QStringLiteral("port"), QStringLiteral("9527"));
    const QCommandLineOption nameOption(QStringLiteral("name"),
        tr("玩家显示名称"), QStringLiteral("name"), QStringLiteral("TUI"));
    const QCommandLineOption avatarOption(QStringLiteral("avatar"),
        tr("玩家头像标识"), QStringLiteral("avatar"),
        QStringLiteral("caocao"));
    const QCommandLineOption reconnectOption(QStringLiteral("reconnect"),
        tr("初次登录时请求重连"));
    const QCommandLineOption plainOption(QStringLiteral("plain"),
        tr("使用确定性的纯文本输出"));
    const QCommandLineOption noColorOption(QStringLiteral("no-color"),
        tr("即使在终端也禁用 ANSI 色彩"));
    const QCommandLineOption languageOption(QStringLiteral("language"),
        tr("设置程序语言"), QStringLiteral("locale"));
    const QCommandLineOption logFileOption(QStringLiteral("log-file"),
        tr("将清理后的语义输出追加到文件"), QStringLiteral("path"));
    const QCommandLineOption scriptOption(QStringLiteral("script"),
        tr("从脚本执行命令与断言"), QStringLiteral("path"));
    const QCommandLineOption assetRootOption(QStringLiteral("asset-root"),
        tr("使用明确的运行时数据根目录"), QStringLiteral("directory"));
    const QCommandLineOption dumpTranslationsOption(
        QStringLiteral("dump-translations"),
        tr("把 Engine 翻译表写成 JSON 后结束"), QStringLiteral("path"));
    const QCommandLineOption uiOption(QStringLiteral("ui"),
        tr("界面模式：classic 或 board"), QStringLiteral("mode"));

    parser.addOptions({helpOption, versionOption, hostOption, portOption, nameOption,
        avatarOption, reconnectOption, plainOption, noColorOption, languageOption,
        logFileOption, scriptOption, assetRootOption, dumpTranslationsOption, uiOption});

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
        helpText.replace(QStringLiteral("Options:"), tr("选项："));
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

    const bool dumpTranslations = parser.isSet(dumpTranslationsOption);
    bool portOk = false;
    const int port = parser.value(portOption).toInt(&portOk);
    const QString host = parser.value(hostOption).trimmed();
    const QString screenName = parser.value(nameOption).trimmed();
    const QString avatar = parser.value(avatarOption).trimmed();
    if (!dumpTranslations) {
        if (!portOk || port < 1 || port > 65535)
            return usageError(tr("--port 必须是 1 至 65535 的整数"));
        if (host.isEmpty())
            return usageError(tr("--host 不可为空"));
        if (screenName.isEmpty())
            return usageError(tr("--name 不可为空"));
        if (avatar.isEmpty())
            return usageError(tr("--avatar 不可为空"));
    }

    // The mode decision itself (docs/tui-board-ui.md §6.1) is a pure
    // function of these five facts; gathering isatty()/QSettings state here,
    // once, is what keeps tuiResolveUiMode() itself free of environment
    // access and therefore testable with plain structs. A conflict is a
    // usage error like the ones just above, so it is checked here too --
    // before EngineBootstrap::initialize(), same as every other --xxx
    // validation in this function.
    TuiUiModeDecision uiDecision;
    if (!dumpTranslations) {
        TuiUiModeInputs uiInputs;
        uiInputs.flag = parser.isSet(uiOption) ? parser.value(uiOption) : QString();
        uiInputs.hasScript = parser.isSet(scriptOption);
        uiInputs.stdoutIsTty = outputIsTerminal();
        uiInputs.stdinIsTty = inputIsTerminal();
        uiInputs.plain = parser.isSet(plainOption) || parser.isSet(noColorOption)
            || qEnvironmentVariableIsSet("NO_COLOR");
        uiInputs.savedChoice = tuiSavedUiMode();
        uiDecision = tuiResolveUiMode(uiInputs);
        if (uiDecision.conflict)
            return usageError(uiDecision.conflictReason);

#if defined(Q_OS_WIN)
        // Only offer board when this console really accepts VT output. The
        // probe restores its mode; TuiTerminal owns the later actual takeover.
        if ((uiDecision.askUser || uiDecision.mode == TuiUiMode::Board)
            && !TuiTerminal::supportsWindowsConsole()) {
            if (uiInputs.flag == QStringLiteral("board")) {
                return usageError(
                    tr("board 模式需要支持虚拟终端输出的 Windows 控制台"));
            }
            uiDecision.askUser = false;
            uiDecision.mode = TuiUiMode::Classic;
            writeUtf8(stdout,
                tr("Windows 控制台不支持虚拟终端输出，已使用 classic 界面\n"));
        }
#endif
    }

    if (parser.isSet(languageOption)) {
        const QLocale locale(parser.value(languageOption));
        if (locale.language() == QLocale::C)
            return usageError(tr("--language 不是可识别的语言"));
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
    if (parser.isSet(dumpTranslationsOption)) {
        const QString path = parser.value(dumpTranslationsOption);
        if (path.trimmed().isEmpty()) {
            EngineBootstrap::shutdown();
            return usageError(tr("--dump-translations 需要输出路径"));
        }
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QTextStream(stderr) << "TUI_ERROR dump_translations: cannot write "
                                << path << '\n';
            EngineBootstrap::shutdown();
            return RuntimeExitCode;
        }
        const QJsonDocument document(
            QJsonObject::fromVariantMap(Sanguosha->translationTable()));
        const QByteArray bytes = document.toJson(QJsonDocument::Compact);
        if (file.write(bytes) != bytes.size()) {
            QTextStream(stderr) << "TUI_ERROR dump_translations: short write\n";
            EngineBootstrap::shutdown();
            return RuntimeExitCode;
        }

        QJsonObject cards;
        for (int id = 0; id < Sanguosha->getCardCount(); ++id) {
            const Card *card = Sanguosha->getEngineCard(id);
            if (card == nullptr)
                continue;
            QJsonObject entry;
            entry.insert(QStringLiteral("object_name"), card->objectName());
            entry.insert(QStringLiteral("suit"), card->getSuitString());
            entry.insert(QStringLiteral("number"), card->getNumber());
            cards.insert(QString::number(id), entry);
        }
        const QString cardsPath = QFileInfo(path).dir().filePath(QStringLiteral("cards.json"));
        QFile cardsFile(cardsPath);
        if (!cardsFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QTextStream(stderr) << "TUI_ERROR dump_translations: cannot write "
                                << cardsPath << '\n';
            EngineBootstrap::shutdown();
            return RuntimeExitCode;
        }
        const QByteArray cardBytes = QJsonDocument(cards).toJson(QJsonDocument::Compact);
        if (cardsFile.write(cardBytes) != cardBytes.size()) {
            QTextStream(stderr) << "TUI_ERROR dump_translations: cards short write\n";
            EngineBootstrap::shutdown();
            return RuntimeExitCode;
        }

        EngineBootstrap::shutdown();
        return 0;
    }
    QObject::disconnect(&app, SIGNAL(aboutToQuit()), Sanguosha, SLOT(deleteLater()));

    // The one-time startup question (§6.1's last row): only reached when
    // tuiResolveUiMode() found stdin *and* stdout to be real terminals, no
    // --script, no --plain/--no-color/NO_COLOR, no --ui and nothing saved --
    // so this is also the only place in main() that ever blocks on stdin,
    // and it does so before ClientLiveSession::connectToServer() runs
    // (inside controller.start(), further below), never after: there must
    // be no state where the client is connected but sitting at a menu.
    // tuiText() needs Sanguosha, so this can only run after
    // EngineBootstrap::initialize() above -- unlike the usage-error messages
    // near the top of main(), which run before the engine exists and so use
    // tr() instead.
    if (uiDecision.askUser) {
        writeUtf8(stdout, tuiText("tui_ui_mode_prompt"));
        std::string rawLine;
        std::getline(std::cin, rawLine);
        const QStringList tokens = QString::fromStdString(rawLine)
            .simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        const QString modeToken = tokens.value(0).toLower();
        const bool remember = tokens.size() > 1
            && tokens.at(1).compare(QStringLiteral("remember"), Qt::CaseInsensitive) == 0;
        uiDecision.mode = modeToken == QStringLiteral("board") ? TuiUiMode::Board
                                                                : TuiUiMode::Classic;
        if (remember && !tuiSaveUiMode(uiDecision.mode))
            writeUtf8(stdout, tuiText("tui_ui_mode_save_failed"));
    }

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
    options.boardMode = (uiDecision.mode == TuiUiMode::Board);

    int result = RuntimeExitCode;
    bool startupFailed = false;
    {
        TuiApplicationController controller(options);
        if (!controller.start(&error)) {
            // Do NOT write the error here: in board mode, start() may have
            // already taken the terminal into the alternate screen (e.g. it
            // fails later, at --log-file open, well after
            // TuiTerminal::enter() succeeded) and controller (with it, its
            // TuiTerminal member) is still alive at this point in the
            // block -- its RAII restore (~TuiTerminal(), §4.1) has not run
            // yet. Writing to stderr now would paint straight into the
            // alternate screen buffer and lose the message the moment the
            // screen is left, which is exactly what used to happen: a
            // startup failure produced a blank terminal with the real error
            // sitting, invisibly, in scrollback nobody ever sees again.
            // Record the failure and defer the write past the closing brace
            // below, where `controller` (and its terminal, if entered) has
            // already been destroyed and the primary screen is back.
            startupFailed = true;
        } else {
            result = app.exec();
        }
    }
    if (startupFailed)
        QTextStream(stderr) << "TUI_ERROR startup: " << error << '\n';
    EngineBootstrap::shutdown();
    return result;
}
