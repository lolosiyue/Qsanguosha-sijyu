#include "settings.h"
//#include "photo.h"
//#include "card.h"
#include "engine.h"
#include "crashhandler.h"
#include <QMutexLocker>
#if !defined(QSAN_ENGINE_BUILD)
#include <QApplication>
#include <QStyleFactory>
#include <QStyleHints>
#include <QGuiApplication>
#endif

Settings Config;

#if !defined(QSAN_ENGINE_BUILD)
// 构建明暗主题的基础 palette(不依赖 standardPalette,详见 applyColorScheme 注释)。
// 0=跟随系统(读 getter 判断当前系统是亮还是暗),1=强制亮色,2=强制暗色。
static QPalette buildColorSchemePalette(int scheme)
{
    int s = qBound(0, scheme, 2);
    Qt::ColorScheme systemScheme = QGuiApplication::styleHints()->colorScheme();
    bool dark = (s == 2) || (s == 0 && systemScheme == Qt::ColorScheme::Dark);

    // 每次都新建 Fusion 实例,确保 standardPalette() 干净;直接复用旧 style 的
    // standardPalette 会拿到上一轮缓存的内容。
    QStyle *fusion = QStyleFactory::create("Fusion");
    qApp->setStyle(fusion);
    QPalette pal;
    if (dark) {
        // 暗色 palette:参考 Qt 官方 Dark Style 例子的配色,文字从纯白柔化为中灰,
        // 避免高对比刺眼。系统强调色(通常蓝色)在暗色下刺眼,高亮改回中性蓝灰。
        const QColor softText(0xcf, 0xcf, 0xcf);
        const QColor softDisabled(0x80, 0x80, 0x80);
        pal.setColor(QPalette::Window, QColor(0x35, 0x35, 0x35));
        pal.setColor(QPalette::WindowText, softText);
        pal.setColor(QPalette::Base, QColor(0x1e, 0x1e, 0x1e));
        pal.setColor(QPalette::AlternateBase, QColor(0x35, 0x35, 0x35));
        pal.setColor(QPalette::ToolTipBase, QColor(0x35, 0x35, 0x35));
        pal.setColor(QPalette::ToolTipText, softText);
        pal.setColor(QPalette::Text, softText);
        pal.setColor(QPalette::Button, QColor(0x35, 0x35, 0x35));
        pal.setColor(QPalette::ButtonText, softText);
        pal.setColor(QPalette::BrightText, QColor(0xff, 0x45, 0x45));
        pal.setColor(QPalette::Link, QColor(0x2a, 0x82, 0xda));
        pal.setColor(QPalette::Highlight, QColor(0x2a, 0x82, 0xda));
        pal.setColor(QPalette::HighlightedText, Qt::black);
        pal.setColor(QPalette::Disabled, QPalette::WindowText, softDisabled);
        pal.setColor(QPalette::Disabled, QPalette::Text, softDisabled);
        pal.setColor(QPalette::Disabled, QPalette::ButtonText, softDisabled);
        pal.setColor(QPalette::Disabled, QPalette::Button, QColor(0x2b, 0x2b, 0x2b));
    } else {
        // 亮色 palette:使用 Qt 标准 Fusion 亮色配色。即使系统处于暗色模式,
        // 也必须给出确定的浅色值,否则 standardPalette() 会带出暗色。
        const QColor button(0xef, 0xef, 0xef);
        const QColor text(Qt::black);
        const QColor disabled(0x80, 0x80, 0x80);
        pal.setColor(QPalette::Window, button);
        pal.setColor(QPalette::WindowText, text);
        pal.setColor(QPalette::Base, Qt::white);
        pal.setColor(QPalette::AlternateBase, QColor(0xe7, 0xe7, 0xe7));
        pal.setColor(QPalette::ToolTipBase, QColor(0xff, 0xff, 0xdc));
        pal.setColor(QPalette::ToolTipText, text);
        pal.setColor(QPalette::Text, text);
        pal.setColor(QPalette::Button, button);
        pal.setColor(QPalette::ButtonText, text);
        pal.setColor(QPalette::BrightText, Qt::white);
        pal.setColor(QPalette::Link, QColor(0x00, 0x00, 0xff));
        pal.setColor(QPalette::Highlight, QColor(0x30, 0x8c, 0xc6));
        pal.setColor(QPalette::HighlightedText, Qt::white);
        pal.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
        pal.setColor(QPalette::Disabled, QPalette::Text, disabled);
        pal.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
        pal.setColor(QPalette::Disabled, QPalette::Button, button);
    }
    // 不论条件分支都返回,调用方统一 setPalette,否则亮切暗再切亮时旧 palette 不更新。
    return pal;
}

// 统一入口:设置 palette 并重设 stylesheet 触发全局 repolish。
// sanguosha.qss 里 palette(base)/palette(window) 等是 setStyleSheet 时一次性解析的,
// 之后不重设 palette 不会反映到 stylesheet 上色。这里清空再重设触发全局 repolish,
// 让 tab 内容/按钮背景按新 palette 上色。
static void applyPalette(const QPalette &pal)
{
    qApp->setPalette(pal);
    QString ss = qApp->styleSheet();
    if (!ss.isEmpty()) {
        qApp->setStyleSheet(QString());
        qApp->setStyleSheet(ss);
    }
}

void applyColorScheme(int scheme)
{
    // -server / --lua-test / --headless 等 headless 模式只用 QCoreApplication,
    // 没有 QApplication 的 setStyle/setPalette/setStyleSheet 语义,直接跳过。
    if (!qobject_cast<QApplication *>(qApp))
        return;
    applyPalette(buildColorSchemePalette(scheme));
}

// 亮度转灰阶 (Rec.601 luma)
static QColor grayColor(const QColor &c)
{
    int lum = qRound(0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue());
    return QColor(lum, lum, lum, c.alpha());
}

void applyVisualMode(const QString &mode)
{
    if (!qobject_cast<QApplication *>(qApp))
        return;
    // normal 直接回到当前主题的明暗 palette。
    if (mode == "normal") {
        applyColorScheme(Config.ColorScheme);
        return;
    }

    QPalette pal = buildColorSchemePalette(Config.ColorScheme);
    if (mode == "grayscale") {
        // 灰阶:以当前明暗主题为基底,将所有角色颜色去饱和。
        const QList<QPalette::ColorRole> roles = {
            QPalette::Window, QPalette::WindowText, QPalette::Base,
            QPalette::AlternateBase, QPalette::ToolTipBase, QPalette::ToolTipText,
            QPalette::Text, QPalette::Button, QPalette::ButtonText,
            QPalette::BrightText, QPalette::Link, QPalette::Highlight,
            QPalette::HighlightedText
        };
        foreach (QPalette::ColorRole role, roles) {
            pal.setColor(role, grayColor(pal.color(role)));
            pal.setColor(QPalette::Disabled, role, grayColor(pal.color(QPalette::Disabled, role)));
        }
    } else {
        // highcontrast:纯黑白高对比 palette。
        pal.setColor(QPalette::Window, Qt::white);
        pal.setColor(QPalette::WindowText, Qt::black);
        pal.setColor(QPalette::Base, Qt::white);
        pal.setColor(QPalette::AlternateBase, QColor(0xe0, 0xe0, 0xe0));
        pal.setColor(QPalette::ToolTipBase, Qt::white);
        pal.setColor(QPalette::ToolTipText, Qt::black);
        pal.setColor(QPalette::Text, Qt::black);
        pal.setColor(QPalette::Button, Qt::white);
        pal.setColor(QPalette::ButtonText, Qt::black);
        pal.setColor(QPalette::BrightText, Qt::black);
        pal.setColor(QPalette::Link, Qt::black);
        pal.setColor(QPalette::Highlight, Qt::black);
        pal.setColor(QPalette::HighlightedText, Qt::white);
        pal.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x80, 0x80, 0x80));
        pal.setColor(QPalette::Disabled, QPalette::Text, QColor(0x80, 0x80, 0x80));
        pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x80, 0x80, 0x80));
        pal.setColor(QPalette::Disabled, QPalette::Button, QColor(0xe0, 0xe0, 0xe0));
    }
    applyPalette(pal);
}
#endif

static const qreal ViewWidth = 1280 * 0.8;
static const qreal ViewHeight = 800 * 0.8;

//consts
const int Settings::S_SURRENDER_REQUEST_MIN_INTERVAL = 5000;
const int Settings::S_PROGRESS_BAR_UPDATE_INTERVAL = 200;
const int Settings::S_SERVER_TIMEOUT_GRACIOUS_PERIOD = 1000;
const int Settings::S_MOVE_CARD_ANIMATION_DURATION = 600;
const int Settings::S_JUDGE_ANIMATION_DURATION = 1200;
const int Settings::S_JUDGE_LONG_DELAY = 800;

Settings::Settings()
#ifdef Q_OS_WIN32
    : QSettings("config.ini", QSettings::IniFormat)
#elif defined(ANDROID)
    : QSettings(getAndroidConfigPath(), QSettings::IniFormat)
#else
    : QSettings("QSanguosha.org", "QSanguosha")
#endif
{
}

#ifdef ANDROID
QString Settings::getAndroidConfigPath()
{
    // Fallback to standard Android path
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/config.ini";
}

void Settings::reinitializeConfigFile()
{
    // Check if we can now find the user's config.ini in the correct working directory
    QString userConfig = "config.ini";

    if (QFile::exists(userConfig)) {
        // Create a new QSettings object with the user's config
        QSettings userSettings(userConfig, QSettings::IniFormat);

        // Copy all values from the user settings to this object
        for (const QString &key : userSettings.allKeys())
            setValue(key, userSettings.value(key));

        // Force sync to ensure values are saved
        sync();
    }
}
#endif

void Settings::init()
{
#ifdef ANDROID
    // First, try to reinitialize with the correct config file
    reinitializeConfigFile();
#endif
    lua_State *lua = Sanguosha->getLuaState();
    LuaLocker lua_locker;
#if !defined(QSAN_ENGINE_BUILD)
    if (!qApp->arguments().contains("-server")) {
        QString font_path = value("DefaultFontPath", "font/simli.ttf").toString();
        int font_id = QFontDatabase::addApplicationFont(font_path);
        if (font_id != -1) {
            QString font_family = QFontDatabase::applicationFontFamilies(font_id).first();
            BigFont.setFamily(font_family);
            SmallFont.setFamily(font_family);
            TinyFont.setFamily(font_family);
        } else
            // 自動化測試/無 font 目錄環境: 改非阻塞警告, 避免 QMessageBox 卡死 client
            qWarning("Font file %s could not be loaded; falling back to system font", qPrintable(font_path));

		BigFont.setPixelSize(GetConfigFromLuaState(lua, "big_font").toInt());
        SmallFont.setPixelSize(GetConfigFromLuaState(lua, "small_font").toInt());
        TinyFont.setPixelSize(GetConfigFromLuaState(lua, "tiny_font").toInt());

        SmallFont.setWeight(QFont::Bold);

        AppFont = value("AppFont", QApplication::font("QMainWindow")).value<QFont>();
        UIFont = value("UIFont", QApplication::font("QTextEdit")).value<QFont>();
        TextEditColor = QColor(value("TextEditColor", "white").toString());
    }
#endif

    CountDownSeconds = value("CountDownSeconds", 3).toInt();
    const QString savedModeId = value("GameMode", "02p").toString();
    GameMode = Sanguosha->getGameMode(savedModeId);
    if (!GameMode.isValid()) {
        qWarning("Saved game mode '%s' is unavailable; falling back to '02p'.",
                 qPrintable(savedModeId));
        GameMode = Sanguosha->getGameMode("02p");
        setValue("GameMode", GameMode.mode_id);
    }

    BanPackages = value("BanPackages").toStringList();
    if (BanPackages.isEmpty()) {
        BanPackages << "ling" << "nostalgia"
            << "nostal_standard" << "nostal_general" << "nostal_wind"
            << "nostal_yjcm" << "nostal_yjcm2012" << "nostal_yjcm2013"
            << "Special3v3" << "Special1v1"
            << "BossMode" << "test" << "GreenHand" << "dragon"
            << "sp_cards" << "GreenHandCard"
            << "New3v3Card" << "New3v3_2013Card" << "New1v1Card"
            << "yitian" << "wisdom" << "BGM" << "BGMDIY"
            << "hegemony" << "h_formation" << "h_momentum";
		setValue("BanPackages", BanPackages);
    }

    RandomSeat = value("RandomSeat", true).toBool();
    EnableCheat = value("EnableCheat", false).toBool();
    FreeChoose = EnableCheat && value("FreeChoose", false).toBool();
    ForbidSIMC = value("ForbidSIMC", false).toBool();
    DisableChat = value("DisableChat", false).toBool();
    FreeAssignSelf = EnableCheat && value("FreeAssignSelf", false).toBool();
    Enable2ndGeneral = value("Enable2ndGeneral", false).toBool();
    EnableSame = value("EnableSame", false).toBool();
    EnableBasara = value("EnableBasara", false).toBool();
    EnableHegemony = value("EnableHegemony", false).toBool();
    EnableMeleeMode = value("EnableMeleeMode", false).toBool();
    MaxHpScheme = value("MaxHpScheme", 0).toInt();
    Scheme0Subtraction = value("Scheme0Subtraction", 3).toInt();
    PreventAwakenBelow3 = value("PreventAwakenBelow3", false).toBool();
    Address = value("Address", "").toString();
    EnableAI = value("EnableAI", true).toBool();
    OriginAIDelay = value("OriginAIDelay", 1000).toInt();
    AlterAIDelayAD = value("AlterAIDelayAD", false).toBool();
    AIDelayAD = value("AIDelayAD", 0).toInt();
    SurrenderAtDeath = value("SurrenderAtDeath", false).toBool();
    EnableLuckCard = value("EnableLuckCard", false).toBool();
    ServerPort = value("ServerPort", 9527u).toUInt();
    DisableLua = value("DisableLua", false).toBool();
    AddGodGeneral = value("AddGodGeneral", true).toBool();

#ifdef Q_OS_WIN32
    UserName = value("UserName", qgetenv("USERNAME")).toString();
#else
    UserName = value("USERNAME", qgetenv("USER")).toString();
#endif

    if (UserName == "Admin" || UserName == "Administrator")
        UserName = tr("Sanguosha-fans");
    ServerName = value("ServerName", tr("%1's server").arg(UserName)).toString();

    HostAddress = value("HostAddress", "127.0.0.1").toString();
    UserAvatar = value("UserAvatar", "shencaocao").toString();
    AutoPickGeneral = value("AutoPickGeneral", "").toString();
    AutoAddRobots = value("AutoAddRobots", false).toBool();
    HistoryIPs = value("HistoryIPs").toStringList();
    DetectorPort = value("DetectorPort", 9526u).toUInt();
    MaxCards = value("MaxCards", 15).toInt();

    EnableHotKey = value("EnableHotKey", true).toBool();
    NeverNullifyMyTrick = value("NeverNullifyMyTrick", true).toBool();
    EnableMinimizeDialog = value("EnableMinimizeDialog", false).toBool();
    EnableAutoTarget = value("EnableAutoTarget", true).toBool();
    EnableIntellectualSelection = value("EnableIntellectualSelection", true).toBool();
    EnableDoubleClick = value("EnableDoubleClick", false).toBool();
    EnableSuperDrag = value("EnableSuperDrag", false).toBool();
    EnableAutoBackgroundChange = value("EnableAutoBackgroundChange", true).toBool();
    NullificationCountDown = value("NullificationCountDown", 8).toInt();
    OperationTimeout = value("OperationTimeout", 15).toInt();
    OperationNoLimit = value("OperationNoLimit", false).toBool();
    EnableEffects = value("EnableEffects", true).toBool();
    EnableLastWord = value("EnableLastWord", true).toBool();
    EnableBgMusic = value("EnableBgMusic", true).toBool();
    EnableCardDescription = value("EnableCardDescription", true).toBool();
    BGMVolume = value("BGMVolume", 1.0f).toFloat();
    EffectVolume = value("EffectVolume", 1.0f).toFloat();
    FrontBGMVolume = value("FrontBGMVolume", 1.0f).toFloat();

    BackgroundImage = value("BackgroundImage", "image/system/backdrop/new-version.jpg").toString();

    BubbleChatBoxKeepTime = value("BubbleChatboxKeepTime", 2000).toInt();
    UIScale = qBound(1.0, value("UIScale", 1.0).toDouble(), 2.0);

    VisualMode = value("VisualMode", "normal").toString();
    if (VisualMode != "normal" && VisualMode != "grayscale" && VisualMode != "highcontrast")
        VisualMode = "normal";

    ColorScheme = qBound(0, value("ColorScheme", 0).toInt(), 2);


    //hulao_ban = GetConfigFromLuaState(lua, "hulao_ban").toStringList();
    //xmode_ban = GetConfigFromLuaState(lua, "xmode_ban").toStringList();

    if (value("Banlist/Roles").toStringList().isEmpty()) {
        setValue("Banlist/Roles", GetConfigFromLuaState(lua, "roles_ban").toStringList());
    }

    if (value("Banlist/1v1").toStringList().isEmpty()) {
        setValue("Banlist/1v1", GetConfigFromLuaState(lua, "kof_ban").toStringList());
    }

    if (value("Banlist/Doudizhu").toStringList().isEmpty()) {
        setValue("Banlist/Doudizhu", GetConfigFromLuaState(lua, "doudizhu_ban").toStringList());
    }

    if (value("Banlist/Happy2v2").toStringList().isEmpty()) {
        setValue("Banlist/Happy2v2", GetConfigFromLuaState(lua, "happy2v2_ban").toStringList());
    }

    if (value("Banlist/BossMode").toStringList().isEmpty()) {
        setValue("Banlist/BossMode", GetConfigFromLuaState(lua, "bossmode_ban").toStringList());
    }

    if (value("Banlist/05_ol").toStringList().isEmpty()) {
        setValue("Banlist/05_ol", GetConfigFromLuaState(lua, "god_ban").toStringList());
    }

    if (value("Banlist/06_ol").toStringList().isEmpty()) {
        setValue("Banlist/06_ol", GetConfigFromLuaState(lua, "god_ban").toStringList());
    }

    QStringList basara_ban = value("Banlist/Basara").toStringList();
    if (basara_ban.isEmpty()) {
		basara_ban = GetConfigFromLuaState(lua, "basara_ban").toStringList();
        setValue("Banlist/Basara", basara_ban);
    }

    if (value("Banlist/Hegemony").toStringList().isEmpty()) {
		basara_ban << GetConfigFromLuaState(lua, "hegemony_ban").toStringList();
		foreach (QString general, Sanguosha->getLimitedGeneralNames()) {
			if (!basara_ban.contains(general)&&Sanguosha->getGeneral(general)->getKingdom() == "god")
				basara_ban << general;
		}
		setValue("Banlist/Hegemony", basara_ban);
    }

    if (value("Banlist/Pairs").toStringList().isEmpty()) {
        setValue("Banlist/Pairs", GetConfigFromLuaState(lua, "pairs_ban").toStringList());
    }

    /*basara_ban = value("ForbidPackages").toStringList();
    if (basara_ban.isEmpty()) {
        basara_ban << "New3v3Card" << "New3v3_2013Card" << "New1v1Card" << "BossMode" << "JianGeDefense" << "test";
        setValue("ForbidPackages", basara_ban);
    }*/

    BossGenerals = GetConfigFromLuaState(lua, "bossmode_default_boss").toStringList();
    BossLevel = BossGenerals.length();
    BossEndlessSkills = GetConfigFromLuaState(lua, "bossmode_endless_skills").toStringList();

    QVariantMap jiange_defense_kingdoms = GetConfigFromLuaState(lua, "jiange_defense_kingdoms").toMap();
    foreach(QString key, jiange_defense_kingdoms.keys())
        JianGeDefenseKingdoms[key] = jiange_defense_kingdoms[key].toString();
    QVariantMap jiange_defense_machine = GetConfigFromLuaState(lua, "jiange_defense_machine").toMap();
    foreach(QString key, jiange_defense_machine.keys())
        JianGeDefenseMachine[key] = jiange_defense_machine[key].toString().split("+");
    QVariantMap jiange_defense_soul = GetConfigFromLuaState(lua, "jiange_defense_soul").toMap();
    foreach(QString key, jiange_defense_soul.keys())
        JianGeDefenseSoul[key] = jiange_defense_soul[key].toString().split("+");

    QMap<QString, int> exp_skill_map;
    foreach (QString skill, GetConfigFromLuaState(lua, "bossmode_exp_skills").toStringList()) {
        basara_ban = skill.split(":");
		exp_skill_map.insert(basara_ban.first(), basara_ban.last().toInt());
    }
    BossExpSkills = exp_skill_map;

    // Qt 狀態正常,把配置摘要暫存給 crash handler(崩潰上下文只寫 Win32 API,
    // 不碰 Engine/Config,故必須在這裡預先格式化好)
    stashGameConfigForCrash();
}

// 把當前 Config 狀態格式化為 UTF-8 配置摘要,供 CrashHandler::setGameConfig。
// 崩潰摘要據此說明"玩家開了哪些包、遊戲模式等"。僅用 Settings::init()
// 已確定的欄位,避免引用不存在於本項目的欄位。
QByteArray buildGameConfigSummary()
{
    auto onOff = [](bool b) { return b ? "開" : "關"; };

    QStringList lines;
    lines << QString("伺服器: 名稱='%1'  端口=%2  局時=%3 秒  無懈=%4 秒")
        .arg(Config.ServerName)
        .arg(Config.ServerPort)
        .arg(Config.CountDownSeconds)
        .arg(Config.NullificationCountDown);
    lines << QString("雙將: %1  軍爭: %2  國戰: %3  同將: %4")
        .arg(onOff(Config.Enable2ndGeneral),
             onOff(Config.EnableBasara),
             onOff(Config.EnableHegemony),
             onOff(Config.EnableSame));
    lines << QString("座次/操作: 隨機座次=%1  自由選將=%2  自由分配=%3  作弊=%4")
        .arg(onOff(Config.RandomSeat),
             onOff(Config.FreeChoose),
             onOff(Config.FreeAssignSelf),
             onOff(Config.EnableCheat));
    lines << QString("規則: 禁 IP 多開=%1  禁聊=%2  防覺醒=%3  操作=%4")
        .arg(onOff(Config.ForbidSIMC),
             onOff(Config.DisableChat),
             onOff(Config.PreventAwakenBelow3),
             Config.OperationNoLimit ? "不限時" : QString("%1 秒").arg(Config.OperationTimeout));

    // 本項目以 BanPackages 表達包池增減,不具 gitee 的 EnabledPackages 欄位
    const QStringList &ban = Config.BanPackages;
    QStringList pkgs;
    foreach (const QString &name, ban) {
        QString cn = Sanguosha->translate(name);
        if (cn.isEmpty() || cn == name)
            cn = name;
        pkgs << cn;
    }
    lines << QString("禁止包(%1):").arg(pkgs.size());

    QString cur = "  ";
    foreach (const QString &p, pkgs) {
        if (cur.length() > 2 && cur.length() + p.length() + 2 > 80) {
            lines << cur;
            cur = "  ";
        }
        if (cur.length() > 2) cur += ", ";
        cur += p;
    }
    if (cur.length() > 2) lines << cur;

    return lines.join("\r\n").toUtf8();
}

void stashGameConfigForCrash()
{
    QByteArray ba = buildGameConfigSummary();
    CrashHandler::setGameConfig(ba.constData());
}

