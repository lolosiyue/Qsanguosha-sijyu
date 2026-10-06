#pragma once

#include "controller-action.h"
#include <QObject>
#include <memory>

// QApplication owns this adapter. SDL never owns a Qt window or pumps Qt events.
class ControllerService final : public QObject
{
    Q_OBJECT
public:
    explicit ControllerService(QObject *parent);
    ~ControllerService() override;
    bool isAvailable() const;
    bool hasConnectedDevice() const;
    QString error() const;
    quint64 deviceEpoch() const;
    // Diagnostic injection attaches a real SDL virtual gamepad. It does not emit
    // semantic actions, call RoomScene helpers or write to a game socket.
    bool attachVirtualDevice();
    bool setVirtualButton(const QString &button, bool down);
    bool setVirtualAxis(const QString &axis, int value);
    void detachVirtualDevice();
signals:
    void action(ControllerAction action, quint64 deviceEpoch, bool repeat);
    void deviceChanged(const QString &name, quint64 deviceEpoch);
    void deviceInput(const QString &control, int value, quint64 deviceEpoch);
private:
    struct Impl;
    std::unique_ptr<Impl> d;
    void poll();
};
