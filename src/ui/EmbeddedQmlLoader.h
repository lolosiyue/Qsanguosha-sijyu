#pragma once
//#include <QObject>
#include <QQuickWidget>
//#include <QWidget>
//#include <QVariantMap>
//#include <QTimer>

// 嵌入式QML加载器 - 直接在游戏窗口上渲染QML
class EmbeddedQmlLoader : public QObject {
    Q_OBJECT
    
public:
    explicit EmbeddedQmlLoader(QObject *parent = nullptr);
    ~EmbeddedQmlLoader();
    
    // 在父窗口上创建QML叠加层
    bool loadQmlOverlay(QWidget *parentWindow,
                       const QString &qmlFile,
                       int width, int height,
                       const QVariantMap &contextVars = QVariantMap(),
                       bool enableClickThrough = false);
    
    // 设置位置（相对于父窗口）
    void setPosition(int x, int y);
    
    // 设置透明度
    void setOpacity(qreal opacity);
    
    // 显示/隐藏
    void show();
    void hide();
    void close();
    
    // 获取错误信息
    QString getLastError() const;

public slots:
    // 供QML调用的关闭方法
    void closeFromQml();

signals:
    // 特效完成信号
    void effectFinished();
    void effectError(const QString &error);
    
private slots:
    void onQmlStatusChanged(QQuickWidget::Status status);
    void onAnimationCompleted();
    
private:
    QQuickWidget *m_qmlWidget;
    QWidget *m_parentWindow;
    QString m_lastError;
    QTimer *m_autoCloseTimer;
    bool m_enableClickThrough;
    QWidget *m_originalFocusWidget;  // 保存原始焦点窗口

    void setupQmlWidget();
    void connectQmlSignals();
};