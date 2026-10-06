#pragma once

#include "controller-action.h"
#include <QObject>
#include <QPointer>
#include <QJsonObject>
#include <QFile>

class ControllerService;
class QWidget;
class RoomScene;
class ClientCore;
class QLocalServer;

// The router owns focus policy, not gameplay drafts or reply serialization.
class ControllerRouter final : public QObject
{
    Q_OBJECT
public:
    ControllerRouter(ControllerService *service, QObject *parent);
    static bool isControllerOnlyRun();
    static bool hasConnectedController();
private:
    bool eventFilter(QObject *object, QEvent *event) override;
    void dispatch(ControllerAction action, quint64 epoch, bool repeat);
    bool routeWidget(QWidget *scope, ControllerAction action);
    void recoverFocus(QWidget *scope);
    void moveFocus(QWidget *scope, ControllerAction action);
    void sendKey(QWidget *target, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    QWidget *widgetScope() const;
    void observe();
    void trace(const QString &kind, QJsonObject data);
    void startDiagnostics();
    ControllerService *m_service;
    QPointer<ClientCore> m_observedCore;
    QFile m_trace;
    QLocalServer *m_input = nullptr;
    quint64 m_sequence = 0;
};
