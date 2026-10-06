#include "boot-splash.h"
#include "homecontroller.h"
#include "mainwindow.h"
#include "settings.h"

#include <QCoreApplication>
#include <QDebug>
#include <QEvent>
#include <QEventLoop>
#include <QGuiApplication>
#include <QPropertyAnimation>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickView>
#include <QScreen>
#include <QTimer>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {

// The intro in BootSplash.qml settles by then; the splash never leaves earlier.
constexpr int IntroMs = 2200;
// Matches the outro in BootSplash.qml.
constexpr int OutroMs = 560;
// Also covers a splash window that stopped rendering.
constexpr int OutroLimitMs = 1200;
// A home page that never reports ready must not keep the splash up.
constexpr int WarmupLimitMs = 20000;

bool s_covering = false;

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
    view->setSource(QUrl(QStringLiteral("qrc:/QSanguosha/Home/BootSplash.qml")));
    if (view->status() != QQuickView::Ready) {
        for (const QQmlError &error : view->errors())
            qWarning().noquote() << "Boot splash:" << error.toString();
        delete view;
        return nullptr;
    }

    auto *splash = new BootSplash(view);
    view->show();
    // Animators reach the render thread at the first sync. Wait for that frame, so the
    // animation already runs when the engine blocks this thread.
    QEventLoop firstFrame;
    connect(view, &QQuickWindow::frameSwapped, &firstFrame, &QEventLoop::quit, Qt::QueuedConnection);
    QTimer::singleShot(1500, &firstFrame, &QEventLoop::quit);
    firstFrame.exec(QEventLoop::ExcludeUserInputEvents);
    splash->m_shown.start();
    s_covering = true;
    return splash;
}

bool BootSplash::isCovering()
{
    return s_covering;
}

BootSplash::BootSplash(QQuickView *view)
    : m_view(view)
{
    connect(view->rootObject(), SIGNAL(outroFinished()), this, SLOT(deleteLater()));
}

BootSplash::~BootSplash()
{
    if (m_mainWindow) {
        m_mainWindow->removeEventFilter(this);
        // The outro fades it in; any earlier exit shows it at once.
        if (!m_outroStarted)
            m_mainWindow->setWindowOpacity(1.0);
    }
    releaseHome();
    delete m_view;
}

void BootSplash::cover(MainWindow *mainWindow)
{
    m_mainWindow = mainWindow;
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
    QTimer::singleShot(qMax<qint64>(0, IntroMs - m_shown.elapsed()), this, &BootSplash::playOutro);
}

void BootSplash::playOutro()
{
    m_outroStarted = true;
    if (m_mainWindow && m_mainWindow->windowOpacity() < 1.0) {
        auto *fadeIn = new QPropertyAnimation(m_mainWindow, "windowOpacity", m_mainWindow);
        fadeIn->setDuration(OutroMs);
        fadeIn->setEndValue(1.0);
        fadeIn->start(QAbstractAnimation::DeleteWhenStopped);
    }
    releaseHome();
    QMetaObject::invokeMethod(m_view->rootObject(), "playOutro");
    QTimer::singleShot(OutroLimitMs, this, &QObject::deleteLater);
}

void BootSplash::releaseHome()
{
    s_covering = false;
    if (!m_mainWindow)
        return;
    if (HomeController *home = m_mainWindow->homeSceneController())
        home->setBootSplashActive(false);
}
