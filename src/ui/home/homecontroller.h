#pragma once

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class HomeController final : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(bool updateAvailable
               READ updateAvailable
               NOTIFY updateAvailableChanged)
    Q_PROPERTY(QUrl backgroundImage READ backgroundImage CONSTANT)
    Q_PROPERTY(QUrl characterImage READ characterImage NOTIFY characterImageChanged)
    Q_PROPERTY(QUrl logoImage READ logoImage CONSTANT)
    Q_PROPERTY(bool hasVideoSupport READ hasVideoSupport CONSTANT)
    Q_PROPERTY(bool isDarkTheme READ isDarkTheme NOTIFY themeChanged)
    Q_PROPERTY(QString playerName READ playerName NOTIFY playerInfoChanged)
    Q_PROPERTY(QUrl playerAvatar READ playerAvatar NOTIFY playerInfoChanged)

public:
    explicit HomeController(QObject *parent = nullptr);

    QString version() const;
    bool updateAvailable() const;
    QUrl backgroundImage() const;
    QUrl characterImage() const;
    QUrl logoImage() const;
    bool hasVideoSupport() const;

    QString playerName() const;
    QUrl playerAvatar() const;

    bool isDarkTheme() const;
    Q_INVOKABLE void toggleTheme();

    Q_INVOKABLE void quickJoin();
    Q_INVOKABLE void joinGame();
    Q_INVOKABLE void startServer();

    // Home and General Overview share the same QQuickWidget and switch QML scenes.
    Q_INVOKABLE void openHome();
    Q_INVOKABLE void openGenerals();
    Q_INVOKABLE void openCards();
    Q_INVOKABLE void openReplays();
    Q_INVOKABLE void openSettings();
    Q_INVOKABLE void openAbout();
    Q_INVOKABLE void checkUpdates();

    // Shared engine data used by GeneralScene.qml. Kingdom names come from the
    // Lua translation tables (Common.lua for the common kingdoms), while colors
    // come from config.kingdom_colors via Engine::getKingdomColor().
    Q_INVOKABLE QString translate(const QString &key) const;
    Q_INVOKABLE QString kingdomColor(const QString &kingdom) const;
    Q_INVOKABLE QVariantList kingdoms() const;

    // Lightweight general data bridge used by GeneralScene.qml.
    Q_INVOKABLE QVariantList generals() const;
    Q_INVOKABLE QVariantMap generalDetails(const QString &generalName) const;
    Q_INVOKABLE QUrl generalPortrait(const QString &generalName) const;

    Q_INVOKABLE QUrl randomBackdrop() const;
    Q_INVOKABLE void refreshCharacterImage();
    Q_INVOKABLE void refreshPlayerInfo();

signals:
    void quickJoinRequested();
    void joinGameRequested();
    void startServerRequested();

    void generalsRequested();
    void cardsRequested();
    void replaysRequested();
    void settingsRequested();
    void aboutRequested();
    void updateCheckRequested();

    void qmlSceneRequested(const QUrl &source);

    void updateAvailableChanged();
    void characterImageChanged();
    void themeChanged();
    void playerInfoChanged();

private:
    void switchQmlScene(const QUrl &source);

    bool m_updateAvailable = false;
    uint m_characterVersion = 0;
};
