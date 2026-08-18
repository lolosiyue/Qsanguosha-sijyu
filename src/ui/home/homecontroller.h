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

    // 玩家資訊：名稱＋頭像（與快速加入對話框同一資料源）
    QString playerName() const;
    QUrl playerAvatar() const;

    // 有效明暗：ColorScheme 0=跟隨系統 / 1=亮色 / 2=暗色
    bool isDarkTheme() const;
    Q_INVOKABLE void toggleTheme();

    Q_INVOKABLE void quickJoin();
    Q_INVOKABLE void joinGame();
    Q_INVOKABLE void startServer();

    // 首頁 / 武將一覽使用同一個 QQuickWidget，在 QML Scene 之間切換。
    Q_INVOKABLE void openHome();
    Q_INVOKABLE void openGenerals();
    Q_INVOKABLE void openCards();
    Q_INVOKABLE void openReplays();
    Q_INVOKABLE void openSettings();
    Q_INVOKABLE void openAbout();
    Q_INVOKABLE void checkUpdates();

    // 武將 Scene 資料橋接。列表只傳輕量欄位；右側詳情按選中武將再取。
    Q_INVOKABLE QVariantList generals() const;
    Q_INVOKABLE QVariantMap generalDetails(const QString &generalName) const;
    Q_INVOKABLE QUrl generalPortrait(const QString &generalName) const;

    Q_INVOKABLE QUrl randomBackdrop() const;

    Q_INVOKABLE void refreshCharacterImage();

    // 重新發送玩家資訊變更信號（回到首頁時由 MainWindow 呼叫）
    Q_INVOKABLE void refreshPlayerInfo();

signals:
    void quickJoinRequested();
    void joinGameRequested();
    void startServerRequested();

    // 保留既有 signal 給其他舊介面使用；首頁的武將按鈕現在直接切換 QML Scene。
    void generalsRequested();
    void cardsRequested();
    void replaysRequested();
    void settingsRequested();
    void aboutRequested();
    void updateCheckRequested();

    void updateAvailableChanged();
    void characterImageChanged();
    void themeChanged();
    void playerInfoChanged();

private:
    void switchQmlScene(const QUrl &source);

    bool m_updateAvailable = false;
    uint m_characterVersion = 0;
};
