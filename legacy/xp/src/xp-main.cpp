#include <cstdio>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QTime>
#include <QTranslator>
#include <QProcess>
#include <QProcessEnvironment>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include "audio.h"
#include "banpair.h"
#include "crashhandler.h"
#include "effects/effects-policy.h"
#include "engine.h"
#include "engine-bootstrap.h"
#include "game-rng.h"
#include "mainwindow.h"
#include "runtime-paths.h"
#include "server.h"
#include "settings.h"
#include "xp-control-protocol.h"
#include "legacy/xp/tests/xp-gui-acceptance.h"

namespace
{
QFile *xpStartupLog = Q_NULLPTR;

void appendEarlyStartupStage(const char *stage)
{
    const QString path = qEnvironmentVariable("QSAN_USER_DATA_ROOT") + "/QSanguoshaXP-early.log";
    FILE *log = _wfopen(reinterpret_cast<const wchar_t *>(path.utf16()), L"ab");
    if (log == Q_NULLPTR)
        return;
    fprintf(log, "%s\r\n", stage);
    fclose(log);
}

void xpEarlyMessageHandler(QtMsgType, const QMessageLogContext &,
    const QString &message)
{
    const QByteArray localMessage = message.toLocal8Bit();
    appendEarlyStartupStage(localMessage.constData());
}

void xpMessageHandler(QtMsgType, const QMessageLogContext &, const QString &message)
{
    const QByteArray localMessage = message.toLocal8Bit();
    fprintf(stderr, "%s\n", localMessage.constData());
    fflush(stderr);
    if (xpStartupLog == Q_NULLPTR)
        return;

    QTextStream stream(xpStartupLog);
    stream << message << '\n';
    stream.flush();
}

void installXpStartupLog(const QString &applicationDirectory)
{
    static QFile startupLog(QDir(applicationDirectory).filePath(
        QStringLiteral("QSanguoshaXP-startup.log")));
    if (!startupLog.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return;
    xpStartupLog = &startupLog;
    qInstallMessageHandler(xpMessageHandler);
}

bool hasArgument(const QStringList &arguments, const QString &argument)
{
    return arguments.contains(argument);
}

void loadTranslation(QTranslator &translator, const QString &fileName)
{
    if (translator.load(QSanRuntimePaths::assetPath(
            QStringLiteral("translations/") + fileName))) {
        return;
    }
    translator.load(QSanRuntimePaths::assetPath(fileName));
}

int configureSeed(const QStringList &arguments)
{
    const int seedIndex = arguments.indexOf(QStringLiteral("--seed"));
    if (seedIndex < 0)
        return 0;

    const QString candidate = seedIndex + 1 < arguments.size()
        ? arguments.at(seedIndex + 1) : QString();
    const QString seedText = candidate.startsWith(QStringLiteral("--"))
        ? QString() : candidate;
    QString error;
    if (Server::configureGameSeed(seedText, &error))
    {
        QCoreApplication::instance()->setProperty("xpGameSeed", seedText);
        return 0;
    }

    fprintf(stderr, "%s\n", qPrintable(error));
    return 1;
}
}

int main(int argc, char *argv[])
{
    QStringList earlyArguments;
    for (int i = 1; i < argc; ++i)
        earlyArguments << QString::fromLocal8Bit(argv[i]);
    const bool serverMode = hasArgument(earlyArguments, QStringLiteral("-server"));

    if (earlyArguments.contains("--xp-build-id")) {
        printf("%s\n", qPrintable(XpControl::buildIdentity()));
        return 0;
    }
    if (earlyArguments.contains("--xp-verify-pair")) {
        QCoreApplication verifier(argc, argv);
        const QDir root(verifier.applicationDirPath());
        QFile manifest(root.filePath("xp-payload-manifest.json"));
        if (!manifest.open(QIODevice::ReadOnly)) return 74;
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(manifest.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) return 74;
        const QJsonObject payload = document.object();
        const QJsonArray executables = payload.value("executables").toArray();
        if (payload.value("schema").toInt() != 1
            || payload.value("buildIdentity").toString() != XpControl::buildIdentity()
            || executables.size() != 2) return 74;
        const QStringList names{"QSanguoshaXP.exe", "QSanguoshaXPServer.exe"};
        for (int index = 0; index < names.size(); ++index) {
            const QJsonObject entry = executables.at(index).toObject();
            const QFileInfo file(root.filePath(names.at(index)));
            if (entry.value("path").toString() != names.at(index)
                || entry.value("bytes").toDouble() != double(file.size())
                || entry.value("sha256").toString() != XpControl::fileHash(file.absoluteFilePath()))
                return 74;
        }
        return 0;
    }

    if (serverMode) {
        // Compatibility launcher never initializes GUI or game Engine.
        QCoreApplication launcher(argc, argv);
        QProcess helper;
        QStringList arguments = launcher.arguments().mid(1);
        arguments.removeAll("-server");
        const QString executable = QDir(launcher.applicationDirPath()).absoluteFilePath("QSanguoshaXPServer.exe");
        helper.setProcessChannelMode(QProcess::ForwardedChannels);
        helper.setInputChannelMode(QProcess::ForwardedInputChannel);
        helper.setWorkingDirectory(launcher.applicationDirPath());
        QObject::connect(&helper, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            &launcher, [&launcher](int code, QProcess::ExitStatus status) {
                launcher.exit(status == QProcess::NormalExit ? code : 1);
            });
        QObject::connect(&helper, static_cast<void(QProcess::*)(QProcess::ProcessError)>(&QProcess::error),
            &launcher, [&launcher, &helper](QProcess::ProcessError) {
                fprintf(stderr, "XP server launch failed: %s\n", qPrintable(helper.errorString()));
                launcher.exit(1);
            });
        helper.start(executable, arguments);
        return launcher.exec();
    }

    // Writable profile data is explicit; assets may be on read-only media.
    if (qEnvironmentVariable("QSAN_USER_DATA_ROOT").isEmpty())
        qsanXpSetEnvironment("QSAN_USER_DATA_ROOT", qEnvironmentVariable("APPDATA") + "/QSanguoshaXP");
    QDir().mkpath(qEnvironmentVariable("QSAN_USER_DATA_ROOT"));
    appendEarlyStartupStage("main entered");
    CrashHandler::install();
    appendEarlyStartupStage("crash handler installed");

    const QDir executableDirectory = QFileInfo(
        QString::fromLocal8Bit(argv[0])).absoluteDir();
    qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", QFile::encodeName(
        executableDirectory.filePath(QStringLiteral("platforms"))));
    appendEarlyStartupStage("QPA path configured");
    qInstallMessageHandler(xpEarlyMessageHandler);

    // Qt 5.6 requires these attributes before the application object exists.
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    appendEarlyStartupStage("constructing application");
    QCoreApplication *application = new QApplication(argc, argv);
    appendEarlyStartupStage("application constructed");
    application->setApplicationName(QStringLiteral("QSanguoshaXP"));
    application->setApplicationVersion(QStringLiteral("XP-SP3-x86"));
    application->addLibraryPath(application->applicationDirPath());
    installXpStartupLog(qEnvironmentVariable("QSAN_USER_DATA_ROOT"));
    appendEarlyStartupStage("Qt startup log installed");
    qDebug("XP startup: application initialized");

    QString pathError;
    if (!QSanRuntimePaths::resolve(application->arguments(), &pathError)) {
        fprintf(stderr, "%s\n", qPrintable(pathError));
        return 6;
    }
    qDebug("XP startup: runtime paths resolved");

    // Preserve portable preferences on the first profile-based launch.
    if (Config.allKeys().isEmpty()) {
        QSettings previous(QSanRuntimePaths::assetPath("config.ini"), QSettings::IniFormat);
        for (const QString &key : previous.allKeys()) Config.setValue(key, previous.value(key));
        Config.sync();
    }

    qsanSeedRandom(QTime(0, 0, 0).secsTo(QTime::currentTime()));
    if (configureSeed(application->arguments()) != 0)
        return 1;

    QTranslator qtTranslator;
    QTranslator gameTranslator;
    loadTranslation(qtTranslator, QStringLiteral("qt_zh_CN.qm"));
    loadTranslation(gameTranslator, QStringLiteral("sanguosha.qm"));
    application->installTranslator(&qtTranslator);
    application->installTranslator(&gameTranslator);

    if (!EngineBootstrap::initialize()) {
        fprintf(stderr, "EngineBootstrap::initialize failed\n");
        return 1;
    }
    qDebug("XP startup: engine initialized");
    CrashHandler::setVersion(Sanguosha->getVersionNumber().toUtf8().constData());

    Config.init();
    G_EFFECTS.initialize(application->arguments());
    BanPair::loadBanPairs();

    QApplication *guiApplication = qobject_cast<QApplication *>(application);
    Q_ASSERT(guiApplication != Q_NULLPTR);
    UiConfig.init();
    applyColorScheme(Config.ColorScheme);
    applyVisualMode(Config.VisualMode);
    guiApplication->setFont(UiConfig.AppFont);

    QFile styleSheet(QSanRuntimePaths::assetPath(QStringLiteral("qss/sanguosha.qss")));
    if (styleSheet.open(QIODevice::ReadOnly)) {
        QTextStream stream(&styleSheet);
        guiApplication->setStyleSheet(stream.readAll());
    }

    MainWindow *mainWindow = new MainWindow;
    Sanguosha->setParent(mainWindow);
    mainWindow->show();
    XpGuiAcceptance::install(mainWindow);

#ifdef AUDIO_SUPPORT
    QObject::connect(Sanguosha, &Engine::audioEffectRequested,
        [](const QString &fileName, bool superpose) {
            Audio::play(fileName, superpose);
        });
    Audio::init();
    qDebug("XP startup: audio backend=%s initialized=%d version=%s output_device=%d",
        qPrintable(Audio::backendName()), Audio::isInitialized() ? 1 : 0,
        qPrintable(Audio::getVersion()), Audio::hasOutputDevice() ? 1 : 0);
    Config.FrontBGMVolume = Config.value(
        QStringLiteral("FrontBGMVolume"), 1.0f).toFloat();
    if (Config.FrontBGMVolume > 0
        && QFile::exists(QStringLiteral("audio/system/BGM/front-bgm.ogg"))) {
        Audio::playBGM(QStringLiteral("audio/system/BGM/front-bgm.ogg"));
        Audio::setBGMVolume(Config.FrontBGMVolume);
    }
#endif

    foreach (QString argument, application->arguments()) {
        if (!argument.startsWith(QStringLiteral("-connect:")))
            continue;
        argument.remove(0, QStringLiteral("-connect:").size());
        Config.HostAddress = argument;
        // A supplied endpoint may be an owned session's ephemeral port.
        // Keep command-line connection overrides out of persistent preferences.
        mainWindow->startConnection();
        break;
    }

    const int result = application->exec();
    CrashHandler::beginShutdown();
    const QVariant acceptanceResult = application->property("xpAcceptanceExitCode");
    return acceptanceResult.isValid() ? acceptanceResult.toInt() : result;
}
