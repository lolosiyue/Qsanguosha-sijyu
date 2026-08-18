#pragma once

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class HomeController final : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY updateAvailableChanged)
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
    Q_INVOKABLE void openHome();
    Q_INVOKABLE void openGenerals();
    Q_INVOKABLE void openCards();
    Q_INVOKABLE void openReplays();
    Q_INVOKABLE void openSettings();
    Q_INVOKABLE void openAbout();
    Q_INVOKABLE void checkUpdates();

    // Translation/config bridge for QML. Game terms (including kingdom names)
    // use Engine translations; Qt UI labels use the existing builds/sanguosha.ts
    // contexts. Kingdom colors are sourced from lua/config.lua kingdom_colors.
    Q_INVOKABLE QString translate(const QString &key) const;
    Q_INVOKABLE QString qtTranslate(const QString &context, const QString &source) const;
    Q_INVOKABLE QString kingdomColor(const QString &kingdom) const;
    Q_INVOKABLE QVariantList kingdoms() const;

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
