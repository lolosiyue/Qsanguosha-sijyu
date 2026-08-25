#ifndef _SETTINGS_H
#define _SETTINGS_H

#include <QSettings>
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

public:
    explicit Settings();
    void init();
    Q_INVOKABLE QVariant getValue(const QString &key, const QVariant &defaultValue = QVariant()) const {
        return value(key, defaultValue);
    }
#ifdef ANDROID
    void reinitializeConfigFile();
#endif

    // server side
    QString ServerName;
    int CountDownSeconds;
    int NullificationCountDown;
    bool EnableMinimizeDialog;
    GameModeStruct GameMode;
    QStringList BanPackages;
    bool RandomSeat;
    bool EnableCheat;
    bool FreeChoose;
    bool ForbidSIMC;
    bool DisableChat;
    bool FreeAssignSelf;
    bool Enable2ndGeneral;
    bool EnableSame;
    bool EnableBasara;
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
    QString BindAddress;
    bool DisableLua;
    bool AddGodGeneral;

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
    // 自動化測試: --test-general 指定自動選將 (空字串 = 不啟用)
    QString AutoPickGeneral;
    // 自動化測試: --test-general2 指定雙將模式副將 (空字串 = 副將清單隨機)
    QString AutoPickGeneral2;
    // 自動化測試: --auto-robots owner 連線後自動填滿 AI 並開局
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
    bool EnableCardDescription;
    bool BossModeExp;

    QString BackgroundImage;
    int BubbleChatBoxKeepTime;
    qreal UIScale;
    QString VisualMode;

    // 主题:0=跟随系统,1=亮色,2=暗色。值即 Qt::ColorScheme 枚举值。
    int ColorScheme;

    // consts
    static const int S_SURRENDER_REQUEST_MIN_INTERVAL;
    static const int S_PROGRESS_BAR_UPDATE_INTERVAL;
    static const int S_SERVER_TIMEOUT_GRACIOUS_PERIOD;
    static const int S_MOVE_CARD_ANIMATION_DURATION;
    static const int S_JUDGE_ANIMATION_DURATION;
    static const int S_JUDGE_LONG_DELAY;
#ifdef ANDROID
private:
    static QString getAndroidConfigPath();
#endif
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

// 切换/初始化应用主题:
//   Qt 6 的 QStyle::standardPalette() 会跟随系统 colorScheme 返回明暗色,
//   系统为暗色时必然拿到暗色 palette;且 styleHints->setColorScheme() 是
//   Qt 6.8+ API,Qt 6.5.3 不可用。因此亮/暗两套 palette 都在代码里手动
//   构建,完全脱离系统状态,再重设 Fusion style 触发全局 repolish。
// scheme: 0=跟随系统,1=亮色,2=暗色 (同 Qt::ColorScheme 枚举值)
void applyColorScheme(int scheme);

// 视觉模式: normal / grayscale / highcontrast。
// 在 applyColorScheme 的明暗基底上再做灰阶或高对比 palette 变换;
// normal 则回到纯主题 palette。
void applyVisualMode(const QString &mode);

// 把当前 Config 状态格式化为 UTF-8 配置摘要,供 CrashHandler::setGameConfig
// (Settings::init() 末尾调用,使崩溃上报始终带最新配置)。
QByteArray buildGameConfigSummary();
void stashGameConfigForCrash();

#endif
