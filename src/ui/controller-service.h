#pragma once

#include "controller-action.h"
#include <QObject>
#include <QVariantMap>
#include <memory>

// QApplication owns this adapter. Platform input shares one action/epoch contract.
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
    // Names printed on the connected pad: south, west, east, north, shoulders, menu.
    QVariantMap buttonLabels() const;
    bool isDirectionalRemote() const;
    bool acceptsNativeKeyEvent(const QEvent *event) const;
#ifdef Q_OS_ANDROID
    // Also permits --android-tv preview on an ordinary Android emulator.
    static bool isTelevision();
#endif
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
#ifdef Q_OS_ANDROID
    bool eventFilter(QObject *object, QEvent *event) override;
#endif
    struct Impl;
    std::unique_ptr<Impl> d;
    void poll();
};
