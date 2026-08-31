#include <cstdio>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QTime>
#include <QTranslator>

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

namespace
{
QFile *xpStartupLog = Q_NULLPTR;

void appendEarlyStartupStage(const char *stage)
{
    FILE *log = fopen("QSanguoshaXP-early.log", "ab");
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
        return 0;

    fprintf(stderr, "%s\n", qPrintable(error));
    return 1;
}
}

int main(int argc, char *argv[])
{
    appendEarlyStartupStage("main entered");
    CrashHandler::install();
    appendEarlyStartupStage("crash handler installed");

    QStringList earlyArguments;
    for (int i = 1; i < argc; ++i)
        earlyArguments << QString::fromLocal8Bit(argv[i]);
    const bool serverMode = hasArgument(earlyArguments, QStringLiteral("-server"));

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
    QCoreApplication *application = serverMode
        ? static_cast<QCoreApplication *>(new QCoreApplication(argc, argv))
        : static_cast<QCoreApplication *>(new QApplication(argc, argv));
    appendEarlyStartupStage("application constructed");
    application->setApplicationName(QStringLiteral("QSanguoshaXP"));
    application->setApplicationVersion(QStringLiteral("XP-SP3-x86"));
    application->addLibraryPath(application->applicationDirPath());
    installXpStartupLog(application->applicationDirPath());
    appendEarlyStartupStage("Qt startup log installed");
    qDebug("XP startup: application initialized");

    QString pathError;
    if (!QSanRuntimePaths::resolve(application->arguments(), &pathError)) {
        fprintf(stderr, "%s\n", qPrintable(pathError));
        return 6;
    }
    qDebug("XP startup: runtime paths resolved");

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

    if (serverMode) {
        Server *server = new Server(application);
        printf("Server is starting on port %u\n", Config.ServerPort);
        fflush(stdout);
        if (!server->listen()) {
            fprintf(stderr, "Starting failed\n");
            delete server;
            CrashHandler::beginShutdown();
            return 2;
        }
        printf("Starting successfully\n");
        fflush(stdout);
        const int result = application->exec();
        CrashHandler::beginShutdown();
        return result;
    }

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
        Config.setValue(QStringLiteral("HostAddress"), argument);
        mainWindow->startConnection();
        break;
    }

    const int result = application->exec();
    CrashHandler::beginShutdown();
    return result;
}
