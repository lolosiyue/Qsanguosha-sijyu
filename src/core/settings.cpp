#include "settings.h"
//#include "photo.h"
//#include "card.h"
#include "engine.h"
#include <QMutexLocker>

Settings Config;

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
    : QSettings("config.ini", QSettings::IniFormat),
#elif defined(ANDROID)
    : QSettings(getAndroidConfigPath(), QSettings::IniFormat),
#else
    : QSettings("QSanguosha.org", "QSanguosha"),
#endif
    Rect(-ViewWidth / 2, -ViewHeight / 2, ViewWidth, ViewHeight)
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
    if (!qApp->arguments().contains("-server")) {
        QString font_path = value("DefaultFontPath", "font/simli.ttf").toString();
        int font_id = QFontDatabase::addApplicationFont(font_path);
        if (font_id != -1) {
            QString font_family = QFontDatabase::applicationFontFamilies(font_id).first();
            BigFont.setFamily(font_family);
            SmallFont.setFamily(font_family);
            TinyFont.setFamily(font_family);
        } else
            QMessageBox::warning(nullptr, tr("Warning"), tr("Font file %1 could not be loaded!").arg(font_path));

		BigFont.setPixelSize(GetConfigFromLuaState(lua, "big_font").toInt());
        SmallFont.setPixelSize(GetConfigFromLuaState(lua, "small_font").toInt());
        TinyFont.setPixelSize(GetConfigFromLuaState(lua, "tiny_font").toInt());

        SmallFont.setWeight(QFont::Bold);

        AppFont = value("AppFont", QApplication::font("QMainWindow")).value<QFont>();
        UIFont = value("UIFont", QApplication::font("QTextEdit")).value<QFont>();
        TextEditColor = QColor(value("TextEditColor", "white").toString());
    }

    CountDownSeconds = value("CountDownSeconds", 3).toInt();
    GameMode = value("GameMode", "02p").toString();

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
}

