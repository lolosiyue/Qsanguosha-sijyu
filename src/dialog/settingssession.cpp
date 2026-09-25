#include "settingssession.h"
#include "build-features.h"
#include "settings.h"
#include "roomscene.h"
#include "mainwindow.h"
#include "engine.h"
#include "clientstruct.h"
#include "effects/effects-policy.h"
#include "effects/effects-profile.h"
#ifdef AUDIO_SUPPORT
#include "audio.h"
#endif

namespace {
const QString kResponsiveLayout = QStringLiteral("UI/ResponsiveLayout");
const QString kOneHandedness = QStringLiteral("UI/RoomHandedness");
const QString kPortraitBackground = QStringLiteral("UI/PortraitBackgroundImage");
const QString kDefaultBackground = QStringLiteral("image/system/backdrop/default.jpg");
const QString kDefaultPortrait = QStringLiteral("image/system/portrait/portrait-background.svg");
const QString kDefaultMusic = QStringLiteral("audio/system/background.ogg");

// 預覽鍵:revert() 依此順序復原。單手偏好在自適應版面之前,
// 因為選了單手會順帶開啟自適應版面,復原時必須先放掉它。
const QStringList &previewKeys()
{
    static const QStringList keys = {
        kOneHandedness, kResponsiveLayout, QStringLiteral("ColorScheme"),
        QStringLiteral("UIScale"), QStringLiteral("BackgroundImage"), kPortraitBackground,
        QStringLiteral("VisualMode"), QStringLiteral("NoIndicator"), QStringLiteral("NoEquipAnim"),
        QStringLiteral("NoCardMoveAnim"), QStringLiteral("EnableAnimatedGenerals"),
        QStringLiteral("EnablePointerEffect"), QStringLiteral("EffectsProfile")
    };
    return keys;
}

// QML 傳進來的數字可能是 int 或 double;依既有值的型別收斂,比較與寫入才一致。
QVariant coerced(const QVariant &current, const QVariant &value)
{
    switch (current.userType()) {
    case QMetaType::Bool: return value.toBool();
    case QMetaType::Int: return value.toInt();
    case QMetaType::Double: return value.toDouble();
    case QMetaType::QString: return value.toString();
    default: return value;
    }
}

QString relativeToApp(QString filename)
{
    const QString appPath = QApplication::applicationDirPath();
    if (filename.startsWith(appPath))
        filename = filename.right(filename.length() - appPath.length() - 1);
    return filename;
}

QWidget *dialogParent(QWidget *parent)
{
    return parent ? parent : QApplication::activeWindow();
}
}

SettingsSession::SettingsSession(QObject *parent)
    : QObject(parent)
{
    // 版面偏好也能從首頁的「版面與單手操作」改動;同步進草稿,外觀才不會顯示舊值。
    connect(&Config, &Settings::uiLayoutChanged, this, [this] {
        updateValue(kResponsiveLayout, Config.responsiveUiEnabled());
        updateValue(kOneHandedness, Config.oneHandedness());
    });
    load();
}

QString SettingsSession::fontLabel(const QFont &font)
{
    return QString("%1 %2").arg(font.family()).arg(font.pointSize());
}

void SettingsSession::load()
{
    QVariantMap v;
    // 顯示
    v.insert(QStringLiteral("ColorScheme"), qBound(0, Config.ColorScheme, 2));
    v.insert(kResponsiveLayout, Config.responsiveUiEnabled());
    v.insert(kOneHandedness, Config.oneHandedness());
    v.insert(QStringLiteral("UIScale"), double(Config.UIScale));
    v.insert(QStringLiteral("BackgroundImage"), Config.BackgroundImage);
    v.insert(kPortraitBackground, Config.value(kPortraitBackground, kDefaultPortrait).toString());
    v.insert(QStringLiteral("VisualMode"), Config.VisualMode);
    v.insert(QStringLiteral("NoIndicator"), Config.value("NoIndicator").toBool());
    v.insert(QStringLiteral("NoEquipAnim"), Config.value("NoEquipAnim").toBool());
    v.insert(QStringLiteral("NoCardMoveAnim"), Config.value("NoCardMoveAnim", false).toBool());
    v.insert(QStringLiteral("EnableAnimatedGenerals"), Config.value("EnableAnimatedGenerals", true).toBool());
    v.insert(QStringLiteral("EnablePointerEffect"), Config.EnablePointerEffect);
#if defined(QSAN_XP_LEGACY)
    G_EFFECTS.setProfile(EffectsProfile::None, false);
#endif
    v.insert(QStringLiteral("EffectsProfile"), G_EFFECTS.profileName());
    v.insert(QStringLiteral("AppFont"), UiConfig.AppFont);
    v.insert(QStringLiteral("UIFont"), UiConfig.UIFont);
    v.insert(QStringLiteral("TextEditColor"), UiConfig.TextEditColor);
    v.insert(QStringLiteral("EnableAutoBackgroundChange"), Config.EnableAutoBackgroundChange);
    v.insert(QStringLiteral("EnableBackgroundVideo"), Config.EnableBackgroundVideo);

    // 音訊
    v.insert(QStringLiteral("BackgroundMusic"), Config.value("BackgroundMusic", kDefaultMusic).toString());
    v.insert(QStringLiteral("EnableEffects"), Config.EnableEffects);
    v.insert(QStringLiteral("EnableLastWord"), Config.EnableLastWord);
    v.insert(QStringLiteral("AudioMuted"), Config.AudioMuted);
    v.insert(QStringLiteral("BGMVolume"), double(Config.BGMVolume));
    v.insert(QStringLiteral("EffectVolume"), double(Config.EffectVolume));
    v.insert(QStringLiteral("FrontBGMVolume"), double(Config.FrontBGMVolume));
    v.insert(QStringLiteral("MasterVolume"), double(Config.MasterVolume));
    v.insert(QStringLiteral("VoiceVolume"), double(Config.VoiceVolume));

    // 遊戲
    v.insert(QStringLiteral("NeverNullifyMyTrick"), Config.NeverNullifyMyTrick);
    v.insert(QStringLiteral("EnableAutoTarget"), Config.EnableAutoTarget);
    v.insert(QStringLiteral("EnableIntellectualSelection"), Config.EnableIntellectualSelection);
    v.insert(QStringLiteral("EnableDoubleClick"), Config.EnableDoubleClick);
    v.insert(QStringLiteral("EnableSuperDrag"), Config.EnableSuperDrag);
    v.insert(QStringLiteral("EnableCardDescription"), Config.EnableCardDescription);
    v.insert(QStringLiteral("EnableOracleConcepts"), Config.value("EnableOracleConcepts", true).toBool());
    v.insert(QStringLiteral("BubbleChatBoxKeepTime"), Config.BubbleChatBoxKeepTime);
    v.insert(QStringLiteral("recorder/autosave"), Config.value("recorder/autosave", true).toBool());
    v.insert(QStringLiteral("recorder/networkonly"), Config.value("recorder/networkonly", true).toBool());
    v.insert(QStringLiteral("recorder/eventsave"), Config.value("recorder/eventsave", false).toBool());
    m_values = v;
}

void SettingsSession::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    emit activeChanged();
}

void SettingsSession::begin()
{
    if (m_active)
        return;
    load();
    m_snapshot = m_values;
    setActive(true);
    emit valuesChanged();
}

void SettingsSession::updateValue(const QString &key, const QVariant &value)
{
    if (m_values.value(key) == value)
        return;
    m_values.insert(key, value);
    emit valuesChanged();
}

void SettingsSession::setValue(const QString &key, const QVariant &value)
{
    begin();
    const QVariant next = coerced(m_values.value(key), value);
    if (m_values.value(key) == next)
        return;
    m_values.insert(key, next);
    if (previewKeys().contains(key)) {
        applyPreview(key, next);
        // Choosing a hand must take effect immediately, so it also enables the responsive layout.
        if (key == kOneHandedness && next.toInt() != 0 && !m_values.value(kResponsiveLayout).toBool()) {
            m_values.insert(kResponsiveLayout, true);
            applyPreview(kResponsiveLayout, true);
        }
    }
    emit valuesChanged();
}

void SettingsSession::applyPreview(const QString &key, const QVariant &value)
{
    if (key == QLatin1String("ColorScheme")) {
        Config.ColorScheme = value.toInt();
        applyColorScheme(Config.ColorScheme);
        if (Config.VisualMode != "normal")
            applyVisualMode(Config.VisualMode);
        emit themeChanged();
    } else if (key == QLatin1String("UIScale")) {
        Config.UIScale = value.toDouble();
        emit uiScaleChanged(Config.UIScale);
    } else if (key == QLatin1String("BackgroundImage")) {
        Config.BackgroundImage = value.toString();
        Config.setValue("BackgroundImage", Config.BackgroundImage);
        emit backgroundChanged();
    } else if (key == kPortraitBackground) {
        Config.setValue(kPortraitBackground, value.toString());
        emit backgroundChanged();
    } else if (key == QLatin1String("VisualMode")) {
        Config.VisualMode = value.toString();
        Config.setValue("VisualMode", Config.VisualMode);
        applyVisualMode(Config.VisualMode);
        emit visualModeChanged();
    } else if (key == kResponsiveLayout) {
        Config.setResponsiveUiEnabled(value.toBool());
    } else if (key == kOneHandedness) {
        Config.setOneHandedness(value.toInt());
    } else if (key == QLatin1String("EnablePointerEffect")) {
        Config.EnablePointerEffect = value.toBool();
        Config.setValue("EnablePointerEffect", Config.EnablePointerEffect);
    } else if (key == QLatin1String("EffectsProfile")) {
        // The effect profile and --effects-profile share one VisualEffectsPolicy: what
        // changes here is the same object, not a second set of settings. XP is a fixed raster profile.
#if !defined(QSAN_XP_LEGACY)
        EffectsProfile profile = EffectsProfileContract::defaultProfile();
        if (EffectsProfileContract::parseProfileName(value.toString(), &profile))
            G_EFFECTS.setProfile(profile, true);
#endif
    } else {
        // NoIndicator 等勾選遊戲內以 Config.value() 即時讀取,預覽需立即寫入 QSettings。
        Config.setValue(key, value);
    }
}

void SettingsSession::revert()
{
    if (!m_active)
        return;
    for (const QString &key : previewKeys()) {
        const QVariant original = m_snapshot.value(key);
        if (m_values.value(key) != original)
            applyPreview(key, original);
    }
    setActive(false);
    load();
    emit valuesChanged();
}

void SettingsSession::commit()
{
    begin();
    const auto real = [this](const char *key) { return m_values.value(QLatin1String(key)).toDouble(); };
    const auto flag = [this](const char *key) { return m_values.value(QLatin1String(key)).toBool(); };

    Config.BGMVolume = real("BGMVolume");
    Config.setValue("BGMVolume", Config.BGMVolume);
    Config.EffectVolume = real("EffectVolume");
    Config.setValue("EffectVolume", Config.EffectVolume);
    Config.FrontBGMVolume = real("FrontBGMVolume");
    Config.setValue("FrontBGMVolume", Config.FrontBGMVolume);

    // M2B-A: master / voice / mute and video background. Key names are shared between
    // Windows and Linux; when older config files lack these keys, Settings::init() already provides stable defaults.
    Config.MasterVolume = real("MasterVolume");
    Config.setValue("MasterVolume", Config.MasterVolume);
    Config.VoiceVolume = real("VoiceVolume");
    Config.setValue("VoiceVolume", Config.VoiceVolume);
    Config.AudioMuted = flag("AudioMuted");
    Config.setValue("AudioMuted", Config.AudioMuted);
    Config.EnableBackgroundVideo = flag("EnableBackgroundVideo");
    Config.setValue("EnableBackgroundVideo", Config.EnableBackgroundVideo);

    Config.EnableEffects = flag("EnableEffects");
    Config.setValue("EnableEffects", Config.EnableEffects);
    Config.EnableLastWord = flag("EnableLastWord");
    Config.setValue("EnableLastWord", Config.EnableLastWord);

#ifdef AUDIO_SUPPORT
    // 先推新的 master／effect／voice／mute 落 backend，再決定 BGM 播定停：
    // 否則靜音之後 BGM 仲會用舊增益響一次。
    Audio::applyConfigVolumes();
    if (Config.FrontBGMVolume > 0) {
        if (!ServerInfo.DuringGame && QFile::exists("audio/system/BGM/front-bgm.ogg"))
            Audio::playBGM("audio/system/BGM/front-bgm.ogg");
        Audio::setBGMVolume(Config.FrontBGMVolume);
    } else
        Audio::stopBGM();
#endif

    // 預覽鍵已寫入 Config;縮放、主題與視覺模式在確定時一律持久化。
    Config.setValue("UIScale", Config.UIScale);
    Config.ColorScheme = m_values.value(QStringLiteral("ColorScheme")).toInt();
    Config.setValue("ColorScheme", Config.ColorScheme);
    Config.VisualMode = m_values.value(QStringLiteral("VisualMode")).toString();
    Config.setValue("VisualMode", Config.VisualMode);
    // 確保視覺模式(灰階/高對比)與目前主題疊加正確
    applyVisualMode(Config.VisualMode);
    emit visualModeChanged();

    Config.NeverNullifyMyTrick = flag("NeverNullifyMyTrick");
    Config.setValue("NeverNullifyMyTrick", Config.NeverNullifyMyTrick);
    Config.EnableAutoTarget = flag("EnableAutoTarget");
    Config.setValue("EnableAutoTarget", Config.EnableAutoTarget);
    Config.EnableIntellectualSelection = flag("EnableIntellectualSelection");
    Config.setValue("EnableIntellectualSelection", Config.EnableIntellectualSelection);
    Config.EnableDoubleClick = flag("EnableDoubleClick");
    Config.setValue("EnableDoubleClick", Config.EnableDoubleClick);
    Config.EnableSuperDrag = flag("EnableSuperDrag");
    Config.setValue("EnableSuperDrag", Config.EnableSuperDrag);
    Config.BubbleChatBoxKeepTime = m_values.value(QStringLiteral("BubbleChatBoxKeepTime")).toInt();
    Config.setValue("BubbleChatBoxKeepTime", Config.BubbleChatBoxKeepTime);
    Config.EnableAutoBackgroundChange = flag("EnableAutoBackgroundChange");
    Config.setValue("EnableAutoBackgroundChange", Config.EnableAutoBackgroundChange);
    Config.EnableCardDescription = flag("EnableCardDescription");
    Config.setValue("EnableCardDescription", Config.EnableCardDescription);
    Config.setValue("EnableOracleConcepts", flag("EnableOracleConcepts"));

    Config.setValue("recorder/autosave", flag("recorder/autosave"));
    Config.setValue("recorder/networkonly", flag("recorder/networkonly"));
    Config.setValue("recorder/eventsave", flag("recorder/eventsave"));

    if (RoomSceneInstance) {
        MainWindow *mw = static_cast<MainWindow *>(Sanguosha->parent());
        if (qobject_cast<RoomScene *>(mw->getScene()) == RoomSceneInstance) {
            RoomSceneInstance->updateVolumeConfig();
            mw->refitScene();
        }
    }

    m_snapshot = m_values;
    setActive(false);
    emit committed();
}

void SettingsSession::chooseBackgroundImage(QWidget *parent)
{
#if QSAN_ENABLE_VIDEO
    const QString filter = QCoreApplication::translate("ConfigDialog", "Images and videos (*.png *.bmp *.jpg *.jpeg *.gif *.webp *.mp4 *.webm *.mkv)");
#else
    const QString filter = QCoreApplication::translate("ConfigDialog", "Images (*.png *.bmp *.jpg *.jpeg *.gif *.webp)");
#endif
    const QString filename = QFileDialog::getOpenFileName(dialogParent(parent),
        QCoreApplication::translate("ConfigDialog", "Select a background image"),
        "image/system/backdrop/", filter);
    if (!filename.isEmpty())
        setValue(QStringLiteral("BackgroundImage"), relativeToApp(filename));
}

void SettingsSession::resetBackgroundImage()
{
    setValue(QStringLiteral("BackgroundImage"), kDefaultBackground);
}

void SettingsSession::choosePortraitBackground(QWidget *parent)
{
    const QString path = QFileDialog::getOpenFileName(dialogParent(parent),
        QCoreApplication::translate("ConfigDialog", "選擇直向背景"), QString(),
        QCoreApplication::translate("ConfigDialog", "Images (*.png *.bmp *.jpg *.jpeg *.webp *.svg)"));
    if (!path.isEmpty())
        setValue(kPortraitBackground, path);
}

void SettingsSession::resetPortraitBackground()
{
    setValue(kPortraitBackground, kDefaultPortrait);
}

void SettingsSession::chooseBackgroundMusic(QWidget *parent)
{
    const QString filename = QFileDialog::getOpenFileName(dialogParent(parent),
        QCoreApplication::translate("ConfigDialog", "Select a background music"),
        "audio/system/BGM",
        QCoreApplication::translate("ConfigDialog", "Audio files (*.wav *.mp3 *.ogg)"));
    if (filename.isEmpty())
        return;
    const QString path = relativeToApp(filename);
    Config.setValue("BackgroundMusic", path);
    updateValue(QStringLiteral("BackgroundMusic"), path);
}

void SettingsSession::resetBackgroundMusic()
{
    Config.setValue("BackgroundMusic", kDefaultMusic);
    updateValue(QStringLiteral("BackgroundMusic"), kDefaultMusic);
}

void SettingsSession::chooseAppFont(QWidget *parent)
{
    bool ok;
    const QFont font = QFontDialog::getFont(&ok, UiConfig.AppFont, dialogParent(parent));
    if (!ok)
        return;
    UiConfig.AppFont = font;
    Config.setValue("AppFont", font);
    QApplication::setFont(font);
    updateValue(QStringLiteral("AppFont"), font);
}

void SettingsSession::chooseTextEditFont(QWidget *parent)
{
    bool ok;
    const QFont font = QFontDialog::getFont(&ok, UiConfig.UIFont, dialogParent(parent));
    if (!ok)
        return;
    UiConfig.UIFont = font;
    Config.setValue("UIFont", font);
    QApplication::setFont(font, "QTextEdit");
    updateValue(QStringLiteral("UIFont"), font);
}

void SettingsSession::chooseTextEditColor(QWidget *parent)
{
    const QColor color = QColorDialog::getColor(UiConfig.TextEditColor, dialogParent(parent));
    if (!color.isValid())
        return;
    UiConfig.TextEditColor = color;
    Config.setValue("TextEditColor", color);
    updateValue(QStringLiteral("TextEditColor"), color);
}
