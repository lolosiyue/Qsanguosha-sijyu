#pragma once
//#include <QObject>
#include <QQuickWidget>
//#include <QWidget>
//#include <QVariantMap>
//#include <QTimer>

// Embedded QML overlay rendered over the game window.
class EmbeddedQmlLoader : public QObject {
    Q_OBJECT
    
public:
    explicit EmbeddedQmlLoader(QObject *parent = nullptr);
    ~EmbeddedQmlLoader();
    
    // Create the overlay on the parent window.
    bool loadQmlOverlay(QWidget *parentWindow,
                       const QString &qmlFile,
                       int width, int height,
                       const QVariantMap &contextVars = QVariantMap(),
                       bool enableClickThrough = false);
    
    // Set the position relative to the parent.
    void setPosition(int x, int y);
    
    // Set the opacity.
    void setOpacity(qreal opacity);
    
    // Show or hide the overlay.
    void show();
    void hide();
    void close();
    
    // Return the load error, if any.
    QString getLastError() const;

public slots:
    // Close the overlay from QML.
    void closeFromQml();
    void receiveQmlResult(const QVariant &result);
    void timeout();

signals:
    // Emitted when the effect completes.
    void effectFinished();
    void effectError(const QString &error);
    void qmlResultReady(const QVariant &result);
    
private slots:
    void onQmlStatusChanged(QQuickWidget::Status status);
    void onAnimationCompleted();
    
private:
    QQuickWidget *m_qmlWidget;
    QWidget *m_parentWindow;
    QString m_lastError;
    QTimer *m_autoCloseTimer;
    bool m_enableClickThrough;
    QWidget *m_originalFocusWidget;  // Original focus window, restored when the overlay closes.

    void setupQmlWidget();
    void connectQmlSignals();
};