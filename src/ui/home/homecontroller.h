#pragma once

#include <QObject>
#include <QUrl>

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

public:
    explicit HomeController(QObject *parent = nullptr);

    QString version() const;
    bool updateAvailable() const;
    QUrl backgroundImage() const;
    QUrl characterImage() const;
    QUrl logoImage() const;
    bool hasVideoSupport() const;

    // 有效明暗：ColorScheme 0=跟隨系統 / 1=亮色 / 2=暗色
    bool isDarkTheme() const;
    Q_INVOKABLE void toggleTheme();

    Q_INVOKABLE void quickJoin();
    Q_INVOKABLE void joinGame();
    Q_INVOKABLE void startServer();

    Q_INVOKABLE void openGenerals();
    Q_INVOKABLE void openCards();
    Q_INVOKABLE void openReplays();
    Q_INVOKABLE void openSettings();
    Q_INVOKABLE void openAbout();
    Q_INVOKABLE void checkUpdates();

    Q_INVOKABLE QUrl randomBackdrop() const;

    Q_INVOKABLE void refreshCharacterImage();

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

    void updateAvailableChanged();
    void characterImageChanged();
    void themeChanged();

private:
    bool m_updateAvailable = false;
    uint m_characterVersion = 0;
};
