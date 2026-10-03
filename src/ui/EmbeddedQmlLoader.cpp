#include "EmbeddedQmlLoader.h"
#include "filehandler.h"
//#include <QFile>
//#include <QQmlContext>
#include <QQuickItem>
//#include <QApplication>
#ifdef Q_OS_ANDROID
//#include "android_assets.h"
#endif

EmbeddedQmlLoader::EmbeddedQmlLoader(QObject *parent)
    : QObject(parent)
    , m_qmlWidget(nullptr)
    , m_parentWindow(nullptr)
    , m_autoCloseTimer(new QTimer(this))
    , m_enableClickThrough(false)
{



    m_autoCloseTimer->setSingleShot(true);
    connect(m_autoCloseTimer, &QTimer::timeout, this, &EmbeddedQmlLoader::close);
}

EmbeddedQmlLoader::~EmbeddedQmlLoader()
{
    if (m_qmlWidget) {
        m_qmlWidget->deleteLater();
		m_qmlWidget = nullptr;
    }

}

bool EmbeddedQmlLoader::loadQmlOverlay(QWidget *parentWindow,
                                      const QString &qmlFile,
                                      int width, int height,
                                      const QVariantMap &contextVars,
                                      bool enableClickThrough)
{

    
    if (!parentWindow) {
        m_lastError = "父窗口为空";
        return false;
    }
    

    QString fullPath = qmlFile;/*
    if (!QFile::exists(fullPath)) {
#ifdef Q_OS_ANDROID
        // Android: use the external-data path.
        QString androidDataPath = AndroidAssets::getWritableDataPath();
        fullPath = androidDataPath + "/" + qmlFile;
        // Resolve the Android data path.
#else
        // Desktop: use the application directory.
        fullPath = QApplication::applicationDirPath() + "/" + qmlFile;
#endif
    }*/
    
    if (!QFile::exists(fullPath)) {
        m_lastError = QString("QML文件不存在: %1").arg(qmlFile);
        return false;
    }
    
    


    m_parentWindow = parentWindow;
    m_enableClickThrough = enableClickThrough;


    m_originalFocusWidget = QApplication::focusWidget();


    m_qmlWidget = new QQuickWidget(parentWindow);

#ifdef Q_OS_ANDROID

    m_qmlWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_qmlWidget->setAttribute(Qt::WA_ShowWithoutActivating, true);
    m_qmlWidget->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
#endif
    

    setupQmlWidget();
    

    QQmlContext *context = m_qmlWidget->rootContext();
    FileHandler *fileHandler = new FileHandler(m_qmlWidget);
    context->setContextProperty("fileHandler", fileHandler);
    // Qt 6 resolves context properties while setSource() creates the component.
    context->setContextProperty("qmlLoader", this);
    

    for (auto it = contextVars.begin(); it != contextVars.end(); ++it) {
        context->setContextProperty(it.key(), it.value());

    }
    


    m_qmlWidget->resize(width, height);


    int x = (parentWindow->width() - width) / 2;
    int y = (parentWindow->height() - height) / 2;
    m_qmlWidget->move(x, y);
    
    


    connect(m_qmlWidget, &QQuickWidget::statusChanged, 
            this, &EmbeddedQmlLoader::onQmlStatusChanged);
    

    m_qmlWidget->setSource(QUrl::fromLocalFile(fullPath));
    return true;
}
void EmbeddedQmlLoader::setupQmlWidget()
{
    if (!m_qmlWidget) return;

#ifdef Q_OS_ANDROID

    m_qmlWidget->setClearColor(Qt::transparent);
    m_qmlWidget->setAttribute(Qt::WA_TranslucentBackground, true);

    // Do not activate the overlay; the underlying dialog must keep keyboard focus.
    m_qmlWidget->setAttribute(Qt::WA_ShowWithoutActivating, true);
    m_qmlWidget->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);

#else
    // Transparent QML overlays must composite correctly over the QOpenGLWidget viewport.
    
    // Use an alpha-capable render format.
    QSurfaceFormat format = m_qmlWidget->format();
    format.setAlphaBufferSize(8);
    m_qmlWidget->setFormat(format);

    // Clear the QML canvas to transparent.
    m_qmlWidget->setClearColor(Qt::transparent);

    // Enable transparent-window input handling so the overlay can pass clicks through.
    m_qmlWidget->setAttribute(Qt::WA_TranslucentBackground, true);

    // Keep the overlay above the game view.
    m_qmlWidget->setAttribute(Qt::WA_AlwaysStackOnTop, true);
#endif

    m_qmlWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_qmlWidget->setAttribute(Qt::WA_DeleteOnClose, false);

    // Only ghost effects pass mouse input through to the game.
    if (m_enableClickThrough) {
        m_qmlWidget->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }
}

void EmbeddedQmlLoader::onQmlStatusChanged(QQuickWidget::Status status)
{
    switch (status) {
    case QQuickWidget::Ready:{

			connectQmlSignals();
			show();
			break;
		}
    case QQuickWidget::Error:{
            QString errorMsg = "QML加载错误:\n";
            const auto errors = m_qmlWidget->errors();
            for (const auto &error : errors) {
                errorMsg += QString("第%1行: %2\n").arg(error.line()).arg(error.description());
            }
            m_lastError = errorMsg;
			QMessageBox::warning(nullptr, "", errorMsg);
            emit effectError(m_lastError);
        }
        break;
    default:
        break;
    }
}

void EmbeddedQmlLoader::connectQmlSignals()
{
    if (!m_qmlWidget) return;

    QQuickItem *rootItem = m_qmlWidget->rootObject();
    if (!rootItem) {
        return;
    }


    connect(rootItem, SIGNAL(animationCompleted()), this, SLOT(onAnimationCompleted()));
    connect(rootItem, SIGNAL(finished(QVariant)), this, SLOT(receiveQmlResult(QVariant)));

}

void EmbeddedQmlLoader::onAnimationCompleted()
{

    emit effectFinished();


    m_autoCloseTimer->start(100);
}

void EmbeddedQmlLoader::setPosition(int x, int y)
{
    if (m_qmlWidget) {
        m_qmlWidget->move(x, y);

    }
}

void EmbeddedQmlLoader::setOpacity(qreal opacity)
{
    if (m_qmlWidget) {
        m_qmlWidget->setWindowOpacity(opacity);

    }
}

void EmbeddedQmlLoader::show()
{
    if (m_qmlWidget) {
        m_qmlWidget->show();
        m_qmlWidget->raise();

#ifdef Q_OS_ANDROID

        if (m_parentWindow) {
            m_parentWindow->raise();
            m_parentWindow->activateWindow();
        }
        // Restore focus after the overlay is shown; do not activate it over the game buttons.
        QTimer::singleShot(100, [this]() {
            if (m_originalFocusWidget) {
                m_originalFocusWidget->setFocus();
            }
        });
#endif
    }
}

void EmbeddedQmlLoader::hide()
{
    if (m_qmlWidget) {
        m_qmlWidget->hide();

    }
}

void EmbeddedQmlLoader::close()
{
    if (m_qmlWidget) {
        m_qmlWidget->close();
        m_qmlWidget->deleteLater();
        m_qmlWidget = nullptr;
    }

#ifdef Q_OS_ANDROID

    if (m_parentWindow) {
        m_parentWindow->raise();
        m_parentWindow->activateWindow();
    }


    QTimer::singleShot(100, [this]() {
        if (m_originalFocusWidget) {
            m_originalFocusWidget->setFocus();
        }
    });
#endif

    emit effectFinished();
    deleteLater();
}

QString EmbeddedQmlLoader::getLastError() const
{
    return m_lastError;
}

void EmbeddedQmlLoader::closeFromQml()
{
    close();
}

void EmbeddedQmlLoader::receiveQmlResult(const QVariant &result)
{
    emit qmlResultReady(result);
    close();
}

void EmbeddedQmlLoader::timeout()
{
    emit qmlResultReady(QVariant());
    close();
}

