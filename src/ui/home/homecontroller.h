#pragma once

#include "homecardmodel.h"

#include <QAbstractListModel>
#include <QHash>
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class HomeGeneralModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY filterChanged)
    Q_PROPERTY(bool loaded READ isLoaded NOTIFY filterChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        DisplayNameRole,
        NicknameRole,
        KingdomRole,
        KingdomsRole,
        GenderRole,
        GenderDisplayRole,
        KingdomDisplayRole,
        MaxHpRole,
        StartHpRole,
        PackageRole,
        PackageNameRole,
        HiddenRole,
        LordRole
    };

    struct Row {
        QString name;
        QString displayName;
        QString nickname;
        QString kingdom;
        QString kingdoms;
        QString gender;
        QString genderDisplay;
        QString kingdomDisplay;
        QString package;
        QString packageName;
        int maxHp = 0;
        int startHp = 0;
        bool hidden = false;
        bool lord = false;
    };

    explicit HomeGeneralModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    bool isLoaded() const;
    void ensureLoaded();
    void applyFilter(const QVariantMap &filters);

    Q_INVOKABLE bool containsName(const QString &name) const;
    Q_INVOKABLE QString nameAt(int row) const;
    Q_INVOKABLE int indexOfName(const QString &name) const;

signals:
    void filterChanged();

private:
    QVector<Row> m_all;
    QVector<int> m_shown;
    bool m_loaded = false;
};

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
    // 使用者設定的影片背景開關；關咗就完全唔會建立 QML Video component。
    Q_PROPERTY(bool videoBackgroundEnabled READ videoBackgroundEnabled CONSTANT)
    // 影片背景的結構化結果。QML 一有結論就寫返落嚟，--multimedia-smoke 直接讀
    // 呢個 map，唔會靠「有冇 console error」判斷成敗。
    Q_PROPERTY(QVariantMap videoStatus READ videoStatus NOTIFY videoStatusChanged)
    Q_PROPERTY(bool isDarkTheme READ isDarkTheme NOTIFY themeChanged)
    Q_PROPERTY(QString playerName READ playerName NOTIFY playerInfoChanged)
    Q_PROPERTY(QUrl playerAvatar READ playerAvatar NOTIFY playerInfoChanged)
    Q_PROPERTY(QString currentGameModeName READ currentGameModeName NOTIFY gameModeChanged)
    Q_PROPERTY(QString currentPage READ currentPage NOTIFY currentPageChanged)
    Q_PROPERTY(HomeGeneralModel *generalModel READ generalModel CONSTANT)
    Q_PROPERTY(HomeCardModel *cardModel READ cardModel CONSTANT)
    Q_PROPERTY(int artRevision READ artRevision NOTIFY artRevisionChanged)
    Q_PROPERTY(qreal uiScale READ uiScale NOTIFY visualSettingsChanged)
    Q_PROPERTY(QString visualMode READ visualMode NOTIFY visualSettingsChanged)

public:
    explicit HomeController(QObject *parent = nullptr);

    QString version() const;
    bool updateAvailable() const;
    QUrl backgroundImage() const;
    QUrl characterImage() const;
    QUrl logoImage() const;
    bool hasVideoSupport() const;
    bool videoBackgroundEnabled() const;
    QVariantMap videoStatus() const;
    // QML 報告影片背景的最終狀態。reason 用 MultimediaSmokeReport 嗰套字串
    // （ok / not_requested / disabled / asset_missing / backend_unavailable /
    // codec_unsupported / playback_error / fallback_ok）。
    Q_INVOKABLE void reportVideoStatus(const QString &reason, const QString &error);
    // 靜態背景真係頂上咗之後叫一次。刻意同 reportVideoStatus() 分開：原本嘅失敗
    // 原因唔可以被 "fallback_ok" 蓋走，否則 CI 分唔出係缺資產定係 codec 唔支援。
    Q_INVOKABLE void confirmVideoFallback();
    Q_INVOKABLE bool localFileExists(const QUrl &url) const;

    QString playerName() const;
    QUrl playerAvatar() const;
    QString currentGameModeName() const;

    bool isDarkTheme() const;
    Q_INVOKABLE void toggleTheme();

    QString currentPage() const;
    HomeGeneralModel *generalModel();
    HomeCardModel *cardModel();

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

    Q_INVOKABLE QString translate(const QString &key) const;
    Q_INVOKABLE QString qtTranslate(const QString &context, const QString &source) const;
    Q_INVOKABLE QString kingdomColor(const QString &kingdom) const;
    Q_INVOKABLE QVariantList kingdoms() const;
    Q_INVOKABLE QUrl kingdomIcon(const QString &kingdom) const;

    Q_INVOKABLE QVariantList generals() const;
    Q_INVOKABLE QVariantList generalPackages() const;
    Q_INVOKABLE QVariantMap generalDetails(const QString &generalName) const;
    Q_INVOKABLE QUrl generalCardImage(const QString &generalName) const;
    Q_INVOKABLE QUrl generalFullImage(const QString &generalName) const;
    Q_INVOKABLE QUrl magatamaImage(int index) const;
    Q_INVOKABLE QUrl hujiaImage() const;
    Q_INVOKABLE QUrl lordIcon() const;
    Q_INVOKABLE QUrl navButtonImage(const QString &name) const;
    Q_INVOKABLE qreal generalOverlayLuma(const QString &generalName) const;
    Q_INVOKABLE void playAudio(const QString &path) const;
    Q_INVOKABLE void playCardAudio(int cardId, const QString &variant) const;
    Q_INVOKABLE void applyGeneralFilter(const QVariantMap &filters);
    // 僅 GUI 首頁 idle 呼叫：預設篩選目錄。已載入則略過，避免蓋掉玩家篩選。
    Q_INVOKABLE void warmGeneralCatalog();
    Q_INVOKABLE QUrl prefetchArtUrl(int index) const;

    int artRevision() const;
    Q_INVOKABLE QVariantList heroSkinList(const QString &generalName) const;
    Q_INVOKABLE void setHeroSkin(const QString &generalName, int skinIndex);
    Q_INVOKABLE void setGeneralBanned(const QString &generalName, bool banned);
    Q_INVOKABLE void setUserAvatar(const QString &generalName);
    Q_INVOKABLE int generalGridColumns() const;
    Q_INVOKABLE void setGeneralGridColumns(int columns);

    Q_INVOKABLE QUrl randomBackdrop() const;

    Q_INVOKABLE void refreshCharacterImage();

    // 重新發送玩家資訊變更信號（回到首頁時由 MainWindow 呼叫）
    Q_INVOKABLE void refreshPlayerInfo();

    qreal uiScale() const;
    QString visualMode() const;
    Q_INVOKABLE void notifyVisualSettings();

signals:
    void videoStatusChanged();
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
    void gameModeChanged();
    void currentPageChanged();
    void artRevisionChanged();
    void visualSettingsChanged();

private:
    void switchQmlScene(const QUrl &source);
    void setCurrentPage(const QString &page);

    bool m_updateAvailable = false;
    uint m_characterVersion = 0;
    int m_artRevision = 0;
    QString m_currentPage = QStringLiteral("home");
    HomeGeneralModel m_generalModel;
    HomeCardModel m_cardModel;
    mutable QHash<QString, QUrl> m_cardImageCache;
    mutable QHash<QString, QUrl> m_fullImageCache;
    QVariantMap m_videoStatus;
};
