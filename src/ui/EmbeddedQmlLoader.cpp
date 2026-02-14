#include "EmbeddedQmlLoader.h"
#include "filehandler.h"
//#include <QFile>
//#include <QQmlContext>
#include <QQuickItem>
//#include <QApplication>
#ifdef ANDROID
//#include "android_assets.h"
#endif

EmbeddedQmlLoader::EmbeddedQmlLoader(QObject *parent)
    : QObject(parent)
    , m_qmlWidget(nullptr)
    , m_parentWindow(nullptr)
    , m_autoCloseTimer(new QTimer(this))
    , m_enableClickThrough(false)
{
    // 移除创建日志

    // 设置自动关闭定时器
    m_autoCloseTimer->setSingleShot(true);
    connect(m_autoCloseTimer, &QTimer::timeout, this, &EmbeddedQmlLoader::close);
}

EmbeddedQmlLoader::~EmbeddedQmlLoader()
{
    if (m_qmlWidget) {
        m_qmlWidget->deleteLater();
		m_qmlWidget = nullptr;
    }
    // 移除销毁日志
}

bool EmbeddedQmlLoader::loadQmlOverlay(QWidget *parentWindow,
                                      const QString &qmlFile,
                                      int width, int height,
                                      const QVariantMap &contextVars,
                                      bool enableClickThrough)
{
    // 移除开始加载日志
    
    if (!parentWindow) {
        m_lastError = "父窗口为空";
        return false;
    }
    
    // 检查文件存在性 - 支持PC和安卓平台
    QString fullPath = qmlFile;/*
    if (!QFile::exists(fullPath)) {
#ifdef ANDROID
        // 安卓平台：使用外部存储路径
        QString androidDataPath = AndroidAssets::getWritableDataPath();
        fullPath = androidDataPath + "/" + qmlFile;
        // 安卓平台路径处理
#else
        // PC平台：使用应用程序目录
        fullPath = QApplication::applicationDirPath() + "/" + qmlFile;
#endif
    }*/
    
    if (!QFile::exists(fullPath)) {
        m_lastError = QString("QML文件不存在: %1").arg(qmlFile);
        return false;
    }
    
    // 移除找到文件日志
    
    // 保存父窗口引用和点击穿透设置
    m_parentWindow = parentWindow;
    m_enableClickThrough = enableClickThrough;

    // 保存当前焦点窗口，用于后续恢复
    m_originalFocusWidget = QApplication::focusWidget();

    // 创建QQuickWidget（直接在父窗口上）
    m_qmlWidget = new QQuickWidget(parentWindow);

#ifdef ANDROID
    // 安卓平台特殊设置 - 防止抢夺焦点
    m_qmlWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_qmlWidget->setAttribute(Qt::WA_ShowWithoutActivating, true);  // 显示但不激活
    m_qmlWidget->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
#endif
    
    // 设置QML组件属性
    setupQmlWidget();
    
    // 设置上下文变量
    QQmlContext *context = m_qmlWidget->rootContext();
    FileHandler *fileHandler = new FileHandler(m_qmlWidget);
    context->setContextProperty("fileHandler", fileHandler);
    
    // 设置自定义上下文变量
    for (auto it = contextVars.begin(); it != contextVars.end(); ++it) {
        context->setContextProperty(it.key(), it.value());
        // 移除上下文变量设置日志
    }
    
    // 设置尺寸和位置

    m_qmlWidget->resize(width, height);

    // 居中显示
    int x = (parentWindow->width() - width) / 2;
    int y = (parentWindow->height() - height) / 2;
    m_qmlWidget->move(x, y);
    
    // 移除位置尺寸日志
    
    // 连接状态变化信号
    connect(m_qmlWidget, &QQuickWidget::statusChanged, 
            this, &EmbeddedQmlLoader::onQmlStatusChanged);
    
    // 加载QML文件
    m_qmlWidget->setSource(QUrl::fromLocalFile(fullPath));
    return true;
}

void EmbeddedQmlLoader::setupQmlWidget()
{
    if (!m_qmlWidget) return;

#ifdef ANDROID
    // 安卓平台特殊设置
    m_qmlWidget->setClearColor(Qt::transparent);  // 恢复透明背景
    m_qmlWidget->setAttribute(Qt::WA_TranslucentBackground, true);

    // 关键：防止抢夺焦点
    m_qmlWidget->setAttribute(Qt::WA_ShowWithoutActivating, true);
    m_qmlWidget->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);

#else
    // PC平台：使用透明背景
    m_qmlWidget->setClearColor(Qt::transparent);
    m_qmlWidget->setAttribute(Qt::WA_AlwaysStackOnTop);
#endif

    m_qmlWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_qmlWidget->setAttribute(Qt::WA_DeleteOnClose, false);

    // 设置点击穿透（ghost=特效专用）
    if (m_enableClickThrough) {
        m_qmlWidget->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }
}

void EmbeddedQmlLoader::onQmlStatusChanged(QQuickWidget::Status status)
{
    switch (status) {
    case QQuickWidget::Ready:{
			// QML加载成功
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

    // 连接QML中的完成信号
    connect(rootItem, SIGNAL(animationCompleted()), this, SLOT(onAnimationCompleted()));

    // 暴露控制对象到QML
    m_qmlWidget->rootContext()->setContextProperty("qmlLoader", this);
}

void EmbeddedQmlLoader::onAnimationCompleted()
{
    // QML动画完成
    emit effectFinished();

    // 延迟关闭，给QML一点时间完成清理
    m_autoCloseTimer->start(100);
}

void EmbeddedQmlLoader::setPosition(int x, int y)
{
    if (m_qmlWidget) {
        m_qmlWidget->move(x, y);
        // 设置位置
    }
}

void EmbeddedQmlLoader::setOpacity(qreal opacity)
{
    if (m_qmlWidget) {
        m_qmlWidget->setWindowOpacity(opacity);
        // 设置透明度
    }
}

void EmbeddedQmlLoader::show()
{
    if (m_qmlWidget) {
        m_qmlWidget->show();
        m_qmlWidget->raise();

#ifdef ANDROID
        // 安卓平台：显示QML后恢复焦点，但不影响按钮
        if (m_parentWindow) {
            m_parentWindow->raise();
            m_parentWindow->activateWindow();
        }
        // 延迟恢复原始焦点，避免立即影响按钮显示
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
        // QML组件已隐藏
    }
}

void EmbeddedQmlLoader::close()
{
    if (m_qmlWidget) {
        m_qmlWidget->close();
        m_qmlWidget->deleteLater();
        m_qmlWidget = nullptr;
    }

#ifdef ANDROID
    // 安卓平台：QML关闭后恢复焦点
    if (m_parentWindow) {
        m_parentWindow->raise();
        m_parentWindow->activateWindow();
    }

    // 简单的延迟恢复焦点
    QTimer::singleShot(100, [this]() {
        if (m_originalFocusWidget) {
            m_originalFocusWidget->setFocus();
        }
    });
#endif

    emit effectFinished();
    deleteLater();  // 自动销毁加载器
}

QString EmbeddedQmlLoader::getLastError() const
{
    return m_lastError;
}

void EmbeddedQmlLoader::closeFromQml()
{
    // QML请求关闭特效
    close();
}

