#ifndef _SERVER_SETUP_SESSION_H
#define _SERVER_SETUP_SESSION_H

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

// Shared start-server backend for ServerDialog and the home ServerScene.qml.
// Values use Config/QSettings keys; commit() owns the rules for writing them back.
class ServerSetupSession : public QObject
{
    Q_OBJECT

public:
    explicit ServerSetupSession(QObject *parent = nullptr);

    Q_INVOKABLE QVariantMap load() const;
    // Mode entries in dialog order: grouped modes, single modes, then scenarios and mini scenes.
    Q_INVOKABLE QVariantList modes() const;
    Q_INVOKABLE QVariantList packageSections() const;
    // Built on hover; walking every package's generals and cards up front stalls the page.
    Q_INVOKABLE QString packageTooltip(const QString &name) const;
    Q_INVOKABLE QString detectAddress() const;

    // Sub-editors stay dialogs; QML calls them after its click handler returns.
    Q_INVOKABLE void editBanlist();
    Q_INVOKABLE void select3v3Generals();
    // Returns true when a custom mini scene was saved.
    Q_INVOKABLE bool editCustomMiniScene();
    Q_INVOKABLE void editBossMode();

    // acceptType -1 starts a console game; 1 hosts a server.
    Q_INVOKABLE void start(const QVariantMap &values, int acceptType);

    static void commit(const QVariantMap &values);

signals:
    void startRequested(int acceptType);
};

#endif
