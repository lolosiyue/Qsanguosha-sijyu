#ifndef _SETTINGS_H
#define _SETTINGS_H

#include <QSettings>
#include <QtGlobal>
#ifndef QSAN_ENGINE_BUILD
#include <QColor>
#include <QFont>
#include <QRectF>
#endif
//#include "protocol.h"
#include "structs.h"

class Settings : public QSettings
{
    Q_OBJECT
    Q_PROPERTY(bool responsiveUiEnabled READ responsiveUiEnabled WRITE setResponsiveUiEnabled NOTIFY uiLayoutChanged)
    Q_PROPERTY(int oneHandedness READ oneHandedness WRITE setOneHandedness NOTIFY uiLayoutChanged)

public:
    explicit Settings();
    void init();
    QVariant value(const QString &key, const QVariant &defaultValue = QVariant()) const;
    bool hasValueOverride(const QString &key) const;
    void setValueOverrides(const QVariantMap &overrides);
    QVariantMap valueOverrides() const;
    // First non-blank of: the stored UserName, the legacy Linux "USERNAME" key, the
    // login name (USERNAME, USER, LOGNAME), "Player".  Never empty: the server refuses
    // a signup without a screen name.
    static QString resolveUserName(const QString &stored, const QString &legacyStored);
    Q_INVOKABLE QVariant getValue(const QString &key, const QVariant &defaultValue = QVariant()) const {
        return value(key, defaultValue);
    }
#ifdef Q_OS_ANDROID
    void reinitializeConfigFile();
#endif

    // One persisted preference is shared by the lobby, dialogs and room.
    bool responsiveUiEnabled() const {
        return value(QStringLiteral("UI/ResponsiveLayout"), false).toBool();
    }
    void setResponsiveUiEnabled(bool enabled) {
        if (responsiveUiEnabled() == enabled) return;
        setValue(QStringLiteral("UI/ResponsiveLayout"), enabled);
        emit uiLayoutChanged();
    }
    int oneHandedness() const {
        return qBound(0, value(QStringLiteral("UI/RoomHandedness"), 0).toInt(), 2);
    }
    void setOneHandedness(int hand) {
        hand = qBound(0, hand, 2);
        if (oneHandedness() == hand) return;
        setValue(QStringLiteral("UI/RoomHandedness"), hand);
        emit uiLayoutChanged();
    }

signals:
    void uiLayoutChanged();

public:
    // server side
    QString ServerName;
    int CountDownSeconds;
    int NullificationCountDown;
    bool EnableMinimizeDialog;
    GameModeStruct GameMode;
    QStringList EnabledPackages;
    QStringList BanPackages;
    bool RandomSeat;
    bool EnableCheat;
    bool FreeChoose;
    bool ForbidSIMC;
    bool DisableChat;
    bool FreeAssignSelf;
    bool Enable2ndGeneral;
    bool EnableHegemony;
    bool EnableMeleeMode;
    int MaxHpScheme;
    int Scheme0Subtraction;
    bool PreventAwakenBelow3;
    QString Address;
    bool EnableAI;
    int AIDelay;
    int OriginAIDelay;
    bool AlterAIDelayAD;
    int AIDelayAD;
    bool SurrenderAtDeath;
    bool EnableLuckCard;
    ushort ServerPort;
    ushort WebSocketPort;
    QString BindAddress;
    bool DisableLua;
    bool AddGodGeneral;
    bool GeneralVersionDedup;

    QStringList BossGenerals;
    int BossLevel;
    QStringList BossEndlessSkills;
    QMap<QString, int> BossExpSkills;

    QMap<QString, QString> JianGeDefenseKingdoms;
    QMap<QString, QStringList> JianGeDefenseMachine;
    QMap<QString, QStringList> JianGeDefenseSoul;

    // client side
    QString HostAddress;
    QString UserName;
    QString UserAvatar;
    // Automated testing: --test-general selects a fixed lord; empty means random.
    QString AutoPickGeneral;
    // Automated testing: --test-general2 selects a fixed deputy; empty means random.
    QString AutoPickGeneral2;
    // Automated testing: --auto-robots fills seats and starts after the owner connects.
    bool AutoAddRobots;
    QStringList HistoryIPs;
    ushort DetectorPort;
    int MaxCards;

    bool EnableHotKey;
    bool NeverNullifyMyTrick;
    bool EnableAutoTarget;
    bool EnableIntellectualSelection;
    bool EnableDoubleClick;
    bool EnableSuperDrag;
    bool EnableAutoBackgroundChange;
    int OperationTimeout;
    bool OperationNoLimit;
    bool EnableEffects;
    bool EnablePointerEffect;
    bool EnableLastWord;
    bool EnableBgMusic;
    float BGMVolume, EffectVolume, FrontBGMVolume;
    // M2B-A: master is the total gain across all channels; VoiceVolume is an extra trim
    // stage for voice on top of EffectVolume (default 1.0, matching the old behavior).
    // Names are shared between Windows/Linux.
    float MasterVolume, VoiceVolume;
    bool AudioMuted;
    // Home-page video background. When off, the static background is always used and no
    // QML Video component is created.
    bool EnableBackgroundVideo;
    bool EnableCardDescription;
    bool BossModeExp;

    QString BackgroundImage;
    int BubbleChatBoxKeepTime;
    qreal UIScale;
    QString VisualMode;

    // Theme: 0 follows the system, 1 is light, and 2 is dark (Qt::ColorScheme values).
    int ColorScheme;

    // consts
    static const int S_SURRENDER_REQUEST_MIN_INTERVAL;
    static const int S_PROGRESS_BAR_UPDATE_INTERVAL;
    static const int S_SERVER_TIMEOUT_GRACIOUS_PERIOD;
    static const int S_MOVE_CARD_ANIMATION_DURATION;
    static const int S_JUDGE_ANIMATION_DURATION;
    static const int S_JUDGE_LONG_DELAY;
#ifdef Q_OS_ANDROID
private:
    static QString getAndroidConfigPath();
#endif

private:
    QVariantMap m_valueOverrides;
};

extern Settings Config;

#ifndef QSAN_ENGINE_BUILD
class UiSettings
{
public:
    UiSettings();
    void init();

    const QRectF Rect;
    QFont BigFont;
    QFont SmallFont;
    QFont TinyFont;
    QFont AppFont;
    QFont UIFont;
    QColor TextEditColor;
};

extern UiSettings UiConfig;
#endif

// Set or initialize the application theme:
// Qt 6 QStyle::standardPalette() follows the system color scheme, so a dark system
// always returns dark colors. styleHints->setColorScheme() is Qt 6.8+, unavailable
// in Qt 6.5.3. Build both palettes explicitly, independent of system state, then
// reapply Fusion to repolish the UI.
// scheme: 0 follows system, 1 is light, 2 is dark (Qt::ColorScheme values).
void applyColorScheme(int scheme);

// Visual mode: normal, grayscale, or highcontrast.
// Grayscale and highcontrast transform the base theme;
// normal restores the base palette.
void applyVisualMode(const QString &mode);

// Format the current Config state as UTF-8 for CrashHandler::setGameConfig().
// Settings::init() calls this at the end so crash reports use current settings.
QByteArray buildGameConfigSummary();
void stashGameConfigForCrash();

#endif
