#include "ui-startup-smoke-controller.h"
#include "runtime-paths.h"

#include "engine.h"
#include "homecontroller.h"
#include "mainwindow.h"
#include "settings.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLibraryInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QQuickItem>
#include <QQuickWidget>
#include <QSaveFile>
#include <QTimer>
#include <QWindow>

#include <cstdio>
#include <cstdlib>

namespace {

QMutex uiStartupSmokeMessageMutex;
QtMessageHandler uiStartupSmokePreviousHandler = nullptr;

QString qtMessageTypeName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return QStringLiteral("debug");
    case QtInfoMsg: return QStringLiteral("info");
    case QtWarningMsg: return QStringLiteral("warning");
    case QtCriticalMsg: return QStringLiteral("critical");
    case QtFatalMsg: return QStringLiteral("fatal");
    }
    return QStringLiteral("unknown");
}

void writeMarker(const QString &line)
{
    const QByteArray utf8 = line.toUtf8();
    fwrite(utf8.constData(), 1, static_cast<size_t>(utf8.size()), stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

QString environmentValue(const char *name)
{
    return qEnvironmentVariableIsSet(name) ? qEnvironmentVariable(name) : QString();
}

} // namespace

UiStartupSmokeController *UiStartupSmokeController::s_active = nullptr;

void uiStartupSmokeMessageHandler(QtMsgType type, const QMessageLogContext &context,
    const QString &message)
{
    {
        QMutexLocker locker(&uiStartupSmokeMessageMutex);
        UiStartupSmokeController *controller = UiStartupSmokeController::s_active;
        if (controller) {
            QJsonObject item;
            item.insert(QStringLiteral("type"), qtMessageTypeName(type));
            item.insert(QStringLiteral("category"), QString::fromUtf8(context.category));
            item.insert(QStringLiteral("message"), message);
            const bool optionalAsset = UiStartupSmokeReport::isOptionalAssetWarning(message);
            if (optionalAsset)
                controller->m_optionalAssetWarnings.append(message);
            item.insert(QStringLiteral("optional_asset"), optionalAsset);
            controller->m_qtMessages.append(item);
            if (type == QtCriticalMsg && !optionalAsset)
                ++controller->m_criticalCount;
        }
    }

    if (uiStartupSmokePreviousHandler)
        uiStartupSmokePreviousHandler(type, context, message);

    if (type == QtFatalMsg) {
        // qFatal 之後 Qt 直接 abort，冇機會行返 finish()：喺度先補一行 result，
        // 令 CI 見到明確的失敗 stage，而唔係「marker 缺失」。
        QMutexLocker locker(&uiStartupSmokeMessageMutex);
        UiStartupSmokeController *controller = UiStartupSmokeController::s_active;
        if (controller && !controller->m_finished) {
            controller->m_finished = true;
            writeMarker(UiStartupSmokeReport::resultLine(false,
                controller->m_pendingStage.isEmpty()
                    ? QStringLiteral("application") : controller->m_pendingStage,
                QStringLiteral("Qt fatal: ") + message,
                UiStartupSmokeReport::SetupFailed));
        }
    }
}

UiStartupSmokeController::UiStartupSmokeController(QObject *parent)
    : QObject(parent)
{
}

UiStartupSmokeController::~UiStartupSmokeController()
{
    QMutexLocker locker(&uiStartupSmokeMessageMutex);
    if (s_active == this)
        s_active = nullptr;
}

bool UiStartupSmokeController::isRequested(const QStringList &arguments)
{
    return UiStartupSmokeReport::isRequested(arguments);
}

bool UiStartupSmokeController::begin(const QStringList &arguments, int *exitCode)
{
    if (exitCode)
        *exitCode = UiStartupSmokeReport::Passed;
    if (!isRequested(arguments))
        return true;

    UiStartupSmokeController *controller = new UiStartupSmokeController(qApp);
    controller->m_arguments = arguments;
    controller->m_pendingStage = QStringLiteral("application");
    controller->m_elapsed.start();
    {
        QMutexLocker locker(&uiStartupSmokeMessageMutex);
        s_active = controller;
    }
    uiStartupSmokePreviousHandler = qInstallMessageHandler(uiStartupSmokeMessageHandler);
    // Engine 建構失敗等舊有路徑會直接 exit(1)，唔會行返 finish()。登記 atexit 兜底，
    // 保證任何退出路徑都留低一行 UI_STARTUP_RESULT，CI 唔會見到「marker 缺失」。
    std::atexit(&UiStartupSmokeController::reportUnfinishedAtExit);

    QString error;
    if (!UiStartupSmokeReport::parseTimeoutMs(arguments, &controller->m_timeoutMs, &error)) {
        controller->finish(false, QStringLiteral("arguments"), error,
            UiStartupSmokeReport::InvalidArguments);
        if (exitCode)
            *exitCode = controller->m_exitCode;
        return false;
    }
    controller->m_reportPath = UiStartupSmokeReport::parseReportPath(arguments);
    if (!UiStartupSmokeReport::parseStartupPage(arguments, &controller->m_startupPage, &error)) {
        controller->finish(false, QStringLiteral("arguments"), error,
            UiStartupSmokeReport::InvalidArguments);
        if (exitCode)
            *exitCode = controller->m_exitCode;
        return false;
    }

    // 到呢一步 QApplication 一定已經建立好：--ui-startup-smoke 唔會喺 QApplication
    // 之前 return，呢個係同 --local-response-ui-capabilities 最大的分別。
    QApplication *application = qobject_cast<QApplication *>(qApp);
    if (!application) {
        controller->finish(false, QStringLiteral("application"),
            QStringLiteral("QApplication was not created (headless QCoreApplication only)"),
            UiStartupSmokeReport::SetupFailed);
        if (exitCode)
            *exitCode = controller->m_exitCode;
        return false;
    }

    controller->emitStage(QStringLiteral("application"), true, controller->environmentDetails());
    controller->m_pendingStage = QStringLiteral("engine");
    return true;
}

int UiStartupSmokeController::abortEarly(const QString &stage, const QString &error,
    int fallbackExitCode)
{
    UiStartupSmokeController *controller = s_active;
    if (!controller)
        return fallbackExitCode;
    controller->emitStage(stage, false, QJsonObject{{QStringLiteral("error"), error}});
    controller->finish(false, stage, error,
        UiStartupSmokeReport::exitCodeForFailedStage(stage));
    return controller->m_exitCode;
}

int UiStartupSmokeController::run()
{
    UiStartupSmokeController *controller = s_active;
    if (!controller) {
        writeMarker(UiStartupSmokeReport::resultLine(false, QStringLiteral("application"),
            QStringLiteral("UiStartupSmokeController::begin() was not called"),
            UiStartupSmokeReport::InternalError));
        return UiStartupSmokeReport::InternalError;
    }
    return controller->execute();
}

QJsonObject UiStartupSmokeController::environmentDetails() const
{
    QJsonObject details;
    details.insert(QStringLiteral("qt_version"), QString::fromLatin1(qVersion()));
    details.insert(QStringLiteral("qt_build_version"), QStringLiteral(QT_VERSION_STR));
    details.insert(QStringLiteral("platform"), qApp ? qApp->platformName() : QString());
    details.insert(QStringLiteral("qt_qpa_platform"), environmentValue("QT_QPA_PLATFORM"));
    details.insert(QStringLiteral("qt_quick_backend"), environmentValue("QT_QUICK_BACKEND"));
    details.insert(QStringLiteral("display"), environmentValue("DISPLAY"));
    details.insert(QStringLiteral("wayland_display"), environmentValue("WAYLAND_DISPLAY"));
    details.insert(QStringLiteral("xdg_runtime_dir"), environmentValue("XDG_RUNTIME_DIR"));
    details.insert(QStringLiteral("qml_import_path"),
        QLibraryInfo::path(QLibraryInfo::QmlImportsPath));
    details.insert(QStringLiteral("working_directory"), QDir::currentPath());
    details.insert(QStringLiteral("asset_root"), QSanRuntimePaths::assetRoot());
    details.insert(QStringLiteral("asset_root_source"),
        QSanRuntimePaths::sourceName(QSanRuntimePaths::resolution().assetRootSource));
    details.insert(QStringLiteral("user_data_root"), QSanRuntimePaths::userDataRoot());
    details.insert(QStringLiteral("timeout_ms"), m_timeoutMs);
    details.insert(QStringLiteral("startup_page"), m_startupPage);
    return details;
}

int UiStartupSmokeController::execute()
{
    m_pendingStage = QStringLiteral("engine");
    if (failIfDeadlineExceeded(QStringLiteral("engine")))
        return m_exitCode;
    if (!Sanguosha) {
        emitStage(QStringLiteral("engine"), false);
        finish(false, QStringLiteral("engine"), QStringLiteral("Engine instance is null"),
            UiStartupSmokeReport::SetupFailed);
        return m_exitCode;
    }
    emitStage(QStringLiteral("engine"), true, QJsonObject{
        {QStringLiteral("version"), Sanguosha->getVersionNumber()},
        {QStringLiteral("general_count"), Sanguosha->getGeneralCount()},
        {QStringLiteral("ui_font"), UiConfig.AppFont.family()}
    });

    m_pendingStage = QStringLiteral("main_window");
    // 直接用產品的 MainWindow，唔另外複製一份 HomeScene 啟動流程。
    MainWindow *window = new MainWindow;
    m_mainWindow = window;
    Sanguosha->setParent(window);
    window->show();

    if (failIfDeadlineExceeded(QStringLiteral("main_window")))
        return m_exitCode;

    QWindow *handle = window->windowHandle();
    if (!window->isVisible() || !handle) {
        emitStage(QStringLiteral("main_window"), false);
        finish(false, QStringLiteral("main_window"),
            QStringLiteral("MainWindow did not obtain a native window handle"),
            UiStartupSmokeReport::SetupFailed);
        return m_exitCode;
    }
    emitStage(QStringLiteral("main_window"), true, QJsonObject{
        {QStringLiteral("title"), window->windowTitle()},
        {QStringLiteral("width"), window->width()},
        {QStringLiteral("height"), window->height()},
        {QStringLiteral("visible"), window->isVisible()},
        {QStringLiteral("native_handle"), handle != nullptr}
    });

    m_pendingStage = QStringLiteral("event_loop");
    connect(window, &MainWindow::homeSceneReady,
        this, &UiStartupSmokeController::onHomeSceneReady);
    connect(window, &MainWindow::homeSceneFailed,
        this, &UiStartupSmokeController::onHomeSceneFailed);

    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    m_timeoutTimer->setInterval(qMax(1, remainingMs()));
    connect(m_timeoutTimer, &QTimer::timeout, this, &UiStartupSmokeController::onTimeout);
    m_timeoutTimer->start();

    // queued callback：真正行入 event loop 之後先會執行，係 event loop 已啟動的證明。
    QTimer::singleShot(0, this, &UiStartupSmokeController::onEventLoopEntered);

    const int rc = qApp->exec();
    if (!m_finished) {
        // event loop 提早結束（例如視窗被關）而未有結論。
        finish(false, m_pendingStage,
            QStringLiteral("event loop exited before the startup smoke completed"),
            UiStartupSmokeReport::SetupFailed);
    }
    return m_exitCode != UiStartupSmokeReport::Passed ? m_exitCode : rc;
}

void UiStartupSmokeController::onEventLoopEntered()
{
    if (m_finished)
        return;
    m_eventLoopEntered = true;
    emitStage(QStringLiteral("event_loop"), true, QJsonObject{
        {QStringLiteral("elapsed_ms"), static_cast<int>(m_elapsed.elapsed())}
    });

    m_pendingStage = QStringLiteral("home_scene");
    if (!m_mainWindow) {
        finish(false, QStringLiteral("main_window"),
            QStringLiteral("MainWindow was destroyed during startup"),
            UiStartupSmokeReport::SetupFailed);
        return;
    }
    // setSource() 對 qrc 係同步的，signal 通常喺 MainWindow 建構期間就已經發出，
    // 所以要主動查一次目前狀態，唔可以淨係等 signal。
    if (m_mainWindow->isHomeSceneReady())
        onHomeSceneReady();
    else if (m_mainWindow->hasHomeSceneError())
        onHomeSceneFailed(m_mainWindow->homeSceneError());
    // 否則等 signal / timeout。
}

void UiStartupSmokeController::onHomeSceneReady()
{
    if (m_finished || m_homeSceneReady)
        return;
    if (!m_mainWindow) {
        finish(false, QStringLiteral("home_scene"),
            QStringLiteral("MainWindow was destroyed before HomeScene became ready"),
            UiStartupSmokeReport::SetupFailed);
        return;
    }
    QQuickWidget *view = m_mainWindow->homeSceneView();
    QQuickItem *root = view ? view->rootObject() : nullptr;
    if (!view || !root) {
        finish(false, QStringLiteral("home_scene"),
            QStringLiteral("HomeScene reported Ready without a QML root object"),
            UiStartupSmokeReport::QmlLoadFailed);
        return;
    }

    m_homeSceneReady = true;
    emitStage(QStringLiteral("home_scene"), true, QJsonObject{
        {QStringLiteral("source"), view->source().toString()},
        {QStringLiteral("root_class"), QString::fromLatin1(root->metaObject()->className())},
        {QStringLiteral("root_width"), root->width()},
        {QStringLiteral("root_height"), root->height()},
        {QStringLiteral("optional_asset_warnings"), int(m_optionalAssetWarnings.size())},
        {QStringLiteral("elapsed_ms"), static_cast<int>(m_elapsed.elapsed())}
    });

    m_pendingStage = QStringLiteral("shutdown");
    if (m_startupPage == QLatin1String("cards")) {
        HomeController *controller = m_mainWindow->findChild<HomeController *>();
        if (!controller) {
            finish(false, QStringLiteral("home_scene"),
                QStringLiteral("HomeController was not found below MainWindow"),
                UiStartupSmokeReport::SetupFailed);
            return;
        }
        controller->openCards();
        QTimer::singleShot(0, this, &UiStartupSmokeController::onSettled);
    } else {
        QTimer::singleShot(250, this, &UiStartupSmokeController::onSettled);
    }
}

void UiStartupSmokeController::onHomeSceneFailed(const QString &error)
{
    if (m_finished)
        return;
    emitStage(QStringLiteral("home_scene"), false,
        QJsonObject{{QStringLiteral("error"), error}});
    finish(false, QStringLiteral("home_scene"), error, UiStartupSmokeReport::QmlLoadFailed);
}

void UiStartupSmokeController::onSettled()
{
    if (m_finished)
        return;
    if (!m_mainWindow) {
        finish(false, QStringLiteral("shutdown"),
            QStringLiteral("MainWindow did not survive startup"),
            UiStartupSmokeReport::SetupFailed);
        return;
    }
    QQuickWidget *view = m_mainWindow->homeSceneView();
    if (!view || !view->rootObject()) {
        finish(false, QStringLiteral("shutdown"),
            QStringLiteral("HomeScene root object did not survive startup"),
            UiStartupSmokeReport::QmlLoadFailed);
        return;
    }
    QQuickItem *root = view->rootObject();
    if (m_startupPage == QLatin1String("cards")) {
        if (!root->property("cardsReadyForSmoke").toBool()) {
            if (!failIfDeadlineExceeded(QStringLiteral("shutdown")))
                QTimer::singleShot(25, this, &UiStartupSmokeController::onSettled);
            return;
        }
        const int modelCount = root->property("cardsModelCount").toInt();
        const int detailCardId = root->property("cardsDetailCardId").toInt();
        if (modelCount < 1 || detailCardId < 0) {
            finish(false, QStringLiteral("shutdown"),
                QStringLiteral("CardScene became ready without a populated model and exact detail card"),
                UiStartupSmokeReport::QmlLoadFailed);
            return;
        }
    }
    if (m_criticalCount > 0) {
        finish(false, QStringLiteral("shutdown"),
            QStringLiteral("Qt emitted critical messages during startup"),
            UiStartupSmokeReport::QmlLoadFailed);
        return;
    }
    emitStage(QStringLiteral("shutdown"), true, QJsonObject{
        {QStringLiteral("main_window_visible"), m_mainWindow->isVisible()},
        {QStringLiteral("startup_page"), m_startupPage},
        {QStringLiteral("cards_model_count"), root->property("cardsModelCount").toInt()},
        {QStringLiteral("cards_detail_card_id"), root->property("cardsDetailCardId").toInt()},
        {QStringLiteral("elapsed_ms"), static_cast<int>(m_elapsed.elapsed())}
    });
    finish(true, QStringLiteral("shutdown"), QString(), UiStartupSmokeReport::Passed);
}

void UiStartupSmokeController::onTimeout()
{
    if (m_finished)
        return;
    failIfDeadlineExceeded(m_pendingStage.isEmpty()
        ? QStringLiteral("event_loop") : m_pendingStage, true);
}

int UiStartupSmokeController::remainingMs() const
{
    return m_timeoutMs - static_cast<int>(m_elapsed.elapsed());
}

bool UiStartupSmokeController::failIfDeadlineExceeded(const QString &stage)
{
    return failIfDeadlineExceeded(stage, false);
}

bool UiStartupSmokeController::failIfDeadlineExceeded(const QString &stage, bool force)
{
    if (m_finished)
        return true;
    if (!force && remainingMs() > 0)
        return false;

    const int elapsed = static_cast<int>(m_elapsed.elapsed());
    emitStage(stage, false, QJsonObject{
        {QStringLiteral("error"), QStringLiteral("timeout")},
        {QStringLiteral("timeout_ms"), m_timeoutMs},
        {QStringLiteral("elapsed_ms"), elapsed}
    });
    finish(false, stage,
        QStringLiteral("startup smoke timed out after %1 ms (limit %2 ms) while waiting "
                       "for stage '%3'").arg(elapsed).arg(m_timeoutMs).arg(stage),
        UiStartupSmokeReport::Timeout);
    return true;
}

void UiStartupSmokeController::reportUnfinishedAtExit()
{
    UiStartupSmokeController *controller = s_active;
    if (!controller || controller->m_finished)
        return;
    controller->m_finished = true;
    const QString stage = controller->m_pendingStage.isEmpty()
        ? QStringLiteral("application") : controller->m_pendingStage;
    writeMarker(UiStartupSmokeReport::resultLine(false, stage,
        QStringLiteral("process exited during stage '%1' without completing the startup "
                       "smoke").arg(stage),
        UiStartupSmokeReport::exitCodeForFailedStage(stage)));
}

void UiStartupSmokeController::emitStage(const QString &stage, bool ok,
    const QJsonObject &details)
{
    m_stages.append(UiStartupSmokeReport::stagePayload(stage, ok, details));
    writeMarker(UiStartupSmokeReport::stageLine(stage, ok, details));
}

void UiStartupSmokeController::finish(bool ok, const QString &stage, const QString &error,
    UiStartupSmokeReport::ExitCode exitCode)
{
    if (m_finished)
        return;
    m_finished = true;
    m_exitCode = exitCode;
    if (m_timeoutTimer)
        m_timeoutTimer->stop();

    QJsonObject details = environmentDetails();
    details.insert(QStringLiteral("event_loop_entered"), m_eventLoopEntered);
    details.insert(QStringLiteral("home_scene_ready"), m_homeSceneReady);
    details.insert(QStringLiteral("elapsed_ms"), static_cast<int>(m_elapsed.elapsed()));
    details.insert(QStringLiteral("optional_asset_warnings"), int(m_optionalAssetWarnings.size()));
    details.insert(QStringLiteral("qt_critical_messages"), m_criticalCount);

    writeMarker(UiStartupSmokeReport::resultLine(ok, stage, error, exitCode, details));
    writeReportFile();

    if (qApp && m_eventLoopEntered)
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
}

void UiStartupSmokeController::writeReportFile()
{
    if (m_reportPath.isEmpty())
        return;
    QJsonObject report;
    report.insert(QStringLiteral("schema_version"), UiStartupSmokeReport::schemaVersion());
    report.insert(QStringLiteral("ok"), m_exitCode == UiStartupSmokeReport::Passed);
    report.insert(QStringLiteral("exit_code"), m_exitCode);
    report.insert(QStringLiteral("stages"), m_stages);
    report.insert(QStringLiteral("environment"), environmentDetails());
    report.insert(QStringLiteral("qt_messages"), m_qtMessages);
    report.insert(QStringLiteral("optional_asset_warnings"), m_optionalAssetWarnings);

    const QFileInfo info(m_reportPath);
    QDir().mkpath(info.absolutePath());
    QSaveFile file(m_reportPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    file.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
    file.commit();
}
