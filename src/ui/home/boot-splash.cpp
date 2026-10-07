#include "boot-splash.h"
#include "boot-cosmos-item.h"
#include "boot-video-item.h"
#include "homecontroller.h"
#include "mainwindow.h"
#include "runtime-paths.h"
#include "settings.h"
#include "ui-rng.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEvent>
#include <QEventLoop>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPropertyAnimation>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickView>
#include <QScreen>
#include <QTimer>
#include <QtQml>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {

// The drawn Big Bang has become a black hole under the title by then; it never
// leaves earlier.
constexpr int IntroMs = 4200;
// Fade after the drawn intro.
constexpr int OutroMs = 560;
// A clip cross-dissolves into the main window.
constexpr int ClipOutroMs = 1200;
// Also covers a splash window that stopped rendering.
constexpr int OutroSlackMs = 640;
// A home page that never reports ready must not keep the splash up.
constexpr int WarmupLimitMs = 20000;
// A clip that opens but shows no picture by then counts as broken.
constexpr int FirstFrameLimitMs = 6000;
// How long the engine waits for the clip to start before it blocks the GUI thread.
constexpr int ClipStartWaitMs = 1500;
// Part of the progress bar for the engine; the catalog pages fill the rest.
constexpr qreal LoadShare = 0.7;

BootSplash *s_splash = nullptr;
bool s_covering = false;

// Clips in video/boot in random order; the clip shown last time is never first.
QStringList clipPool()
{
    static const QStringList suffixes = {
        QStringLiteral("mp4"), QStringLiteral("webm"), QStringLiteral("mkv"),
        QStringLiteral("mov"), QStringLiteral("m4v")
    };
    QStringList clips;
    const QDir dir(QSanRuntimePaths::assetPath(QStringLiteral("video/boot")));
    for (const QFileInfo &info : dir.entryInfoList(QDir::Files, QDir::Name)) {
        if (suffixes.contains(info.suffix().toLower()))
            clips << info.absoluteFilePath();
    }
    QStringList order;
    while (!clips.isEmpty())
        order << clips.takeAt(UiRng::bounded(clips.size()));
    if (order.size() > 1
        && QFileInfo(order.first()).fileName() == Config.value("BootSplash/LastClip").toString())
        order.move(0, order.size() - 1);
    return order;
}

}

bool BootSplash::wanted(const QStringList &arguments)
{
    for (const QString &argument : arguments) {
        if (argument.startsWith(QLatin1String("-connect:"))
            || argument.startsWith(QLatin1String("--test-scenario"))
            || argument.startsWith(QLatin1String("--local-response-ui-case")))
            return false;
    }
    return true;
}

BootSplash *BootSplash::show()
{
    static const bool registered = []() {
        qmlRegisterType<BootVideoItem>("QSanguosha.Boot", 1, 0, "BootVideo");
        qmlRegisterType<BootCosmosItem>("QSanguosha.Boot", 1, 0, "BootCosmos");
        return true;
    }();
    Q_UNUSED(registered);
    const QStringList clips = clipPool();

    auto *view = new QQuickView;
    view->setFlags(Qt::Window | Qt::FramelessWindowHint);
    view->setTitle(QCoreApplication::translate("MainWindow", "Sanguosha"));
    // Transparent, so the outro fades into the main window underneath.
    view->setColor(Qt::transparent);
    view->setResizeMode(QQuickView::SizeRootObjectToView);
    // A 16:9 card centred where the main window will open.
    const QPoint position = Config.value("WindowPosition", QPoint(0, 0)).toPoint();
    QScreen *screen = QGuiApplication::screenAt(position);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect available = screen->availableGeometry();
        QRect window(position, Config.value("WindowSize", QSize(1366, 706)).toSize());
        if (Config.value("WindowState").toInt() != Qt::WindowNoState || !available.contains(window))
            window = available;
        const int width = qMin(960, available.width() * 7 / 10);
        QRect card(0, 0, width, width * 9 / 16);
        card.moveCenter(window.center());
        view->setGeometry(card);
    }
    view->setInitialProperties({{QStringLiteral("videoMode"), !clips.isEmpty()}});
    view->setSource(QUrl(QStringLiteral("qrc:/QSanguosha/Home/BootSplash.qml")));
    if (view->status() != QQuickView::Ready) {
        for (const QQmlError &error : view->errors())
            qWarning().noquote() << "Boot splash:" << error.toString();
        delete view;
        return nullptr;
    }

    auto *splash = new BootSplash(view, clips);
    view->show();
    // Animators reach the render thread at the first sync. Wait for that frame, so the
    // animation already runs when the engine blocks this thread.
    QEventLoop firstFrame;
    connect(view, &QQuickWindow::frameSwapped, &firstFrame, &QEventLoop::quit, Qt::QueuedConnection);
    QTimer::singleShot(1500, &firstFrame, &QEventLoop::quit);
    firstFrame.exec(QEventLoop::ExcludeUserInputEvents);
    // A clip opens through this thread's event loop; let it start before the engine blocks it.
    if (splash->m_video) {
        QEventLoop clipStart;
        connect(splash->m_video, &BootVideoItem::firstFrame, &clipStart, &QEventLoop::quit);
        connect(splash->m_video, &BootVideoItem::failed, &clipStart, &QEventLoop::quit);
        QTimer::singleShot(ClipStartWaitMs, &clipStart, &QEventLoop::quit);
        clipStart.exec(QEventLoop::ExcludeUserInputEvents);
    }
    splash->m_shown.start();
    s_splash = splash;
    s_covering = true;
    return splash;
}

bool BootSplash::isCovering()
{
    return s_covering;
}

void BootSplash::pulse()
{
    // During the engine the bar moves by the time the last boot took to get this far.
    if (s_splash)
        s_splash->setProgress(LoadShare * qMin(0.95, double(s_splash->m_shown.elapsed()) / s_splash->m_loadMs));
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

BootSplash::BootSplash(QQuickView *view, const QStringList &clips)
    : m_view(view)
    , m_clips(clips)
    , m_loadMs(qMax(1000, Config.value("BootSplash/LoadMs", 15000).toInt()))
{
    QObject *root = view->rootObject();
    connect(root, SIGNAL(outroFinished()), this, SLOT(deleteLater()));

    // A clip that ends before the pages are ready keeps showing its last frame.
    m_video = root->findChild<BootVideoItem *>();
    if (m_video) {
        // Queued: falling back destroys the item that emits this.
        connect(m_video, &BootVideoItem::failed, this, &BootSplash::clipFailed, Qt::QueuedConnection);
        playNextClip();
    }
}

BootSplash::~BootSplash()
{
    s_splash = nullptr;
    if (m_mainWindow) {
        m_mainWindow->removeEventFilter(this);
        // The outro fades it in; any earlier exit shows it at once.
        if (!m_outroStarted)
            m_mainWindow->setWindowOpacity(1.0);
    }
    releaseHome();
    delete m_view;
}

bool BootSplash::playNextClip()
{
    if (!m_video || m_clips.isEmpty())
        return false;
    const QString clip = m_clips.takeFirst();
    Config.setValue("BootSplash/LastClip", QFileInfo(clip).fileName());
    m_video->play(QUrl::fromLocalFile(clip),
                  qBound(0.0, Config.value("FrontBGMVolume", 1.0).toDouble(), 1.0));
    QTimer::singleShot(FirstFrameLimitMs, this, [this, attempt = ++m_attempt]() {
        if (attempt == m_attempt && m_video && !m_video->hasFrame())
            clipFailed();
    });
    return true;
}

void BootSplash::clipFailed()
{
    if (playNextClip())
        return;
    // No clip plays: the drawn animation takes over.
    if (m_video) {
        m_video->disconnect(this);
        m_video = nullptr;
    }
    m_view->rootObject()->setProperty("videoMode", false);
}

void BootSplash::setProgress(qreal progress)
{
    if (progress <= m_progress)
        return;
    m_progress = progress;
    m_view->rootObject()->setProperty("progress", progress);
}

void BootSplash::cover(MainWindow *mainWindow)
{
    m_mainWindow = mainWindow;
    Config.setValue("BootSplash/LoadMs", m_shown.elapsed());
    setProgress(LoadShare);
    HomeController *home = mainWindow->homeSceneController();
    if (!home || !mainWindow->isHomeSceneReady()) {
        leave();
        return;
    }

    // The main window stays invisible while HomeScene opens each page under it, and
    // fades in as the splash fades out.
    mainWindow->setWindowOpacity(0.0);
    mainWindow->winId();
#ifdef Q_OS_WIN
    // An owned window stays above its owner, so the main window opens underneath.
    SetWindowLongPtrW(reinterpret_cast<HWND>(m_view->winId()), GWLP_HWNDPARENT,
                      static_cast<LONG_PTR>(mainWindow->winId()));
#endif
    m_view->setTransientParent(mainWindow->windowHandle());
    // Elsewhere ownership may only apply at map time; raise once the main window shows.
    QTimer::singleShot(0, m_view, [view = m_view]() {
        view->raise();
        view->requestActivate();
    });

    connect(home, &HomeController::bootProgress, this, [this](qreal done) {
        setProgress(LoadShare + (1 - LoadShare) * done);
    });
    connect(home, &HomeController::bootPagesReady, this, &BootSplash::leave);
    connect(mainWindow, &MainWindow::homeSceneFailed, this, &BootSplash::leave);
    QTimer::singleShot(WarmupLimitMs, this, &BootSplash::leave);
    mainWindow->installEventFilter(this);
}

bool BootSplash::eventFilter(QObject *watched, QEvent *event)
{
    // Closing or minimizing the main window ends the boot at once.
    if (watched == m_mainWindow && event->type() == QEvent::Hide) {
        releaseHome();
        deleteLater();
    }
    return QObject::eventFilter(watched, event);
}

void BootSplash::leave()
{
    if (m_leaving)
        return;
    m_leaving = true;
    setProgress(1.0);
    scheduleOutro();
}

void BootSplash::scheduleOutro()
{
    if (m_outroStarted)
        return;
    // A clip dissolves into the main window at once; the drawn intro plays out first.
    const qint64 wait = m_video ? 0 : IntroMs - m_shown.elapsed();
    if (wait > 0) {
        QTimer::singleShot(int(wait), this, &BootSplash::scheduleOutro);
        return;
    }
    playOutro(m_video ? ClipOutroMs : OutroMs);
}

void BootSplash::playOutro(int durationMs)
{
    if (m_outroStarted)
        return;
    m_outroStarted = true;
    if (m_mainWindow && m_mainWindow->windowOpacity() < 1.0) {
        auto *fadeIn = new QPropertyAnimation(m_mainWindow, "windowOpacity", m_mainWindow);
        fadeIn->setDuration(durationMs);
        fadeIn->setEndValue(1.0);
        fadeIn->start(QAbstractAnimation::DeleteWhenStopped);
    }
    if (m_video)
        m_video->fadeOutAudio(durationMs);
    releaseHome();
    QMetaObject::invokeMethod(m_view->rootObject(), "playOutro", Q_ARG(QVariant, durationMs));
    QTimer::singleShot(durationMs + OutroSlackMs, this, &QObject::deleteLater);
}

void BootSplash::releaseHome()
{
    s_covering = false;
    if (!m_mainWindow)
        return;
    if (HomeController *home = m_mainWindow->homeSceneController())
        home->setBootSplashActive(false);
}
