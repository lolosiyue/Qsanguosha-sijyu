#include "configdialog.h"
#include "ui_configdialog.h"
#include "settings.h"
#include "roomscene.h"
#include "mainwindow.h"
#include "engine.h"
#include "clientstruct.h"
#ifdef AUDIO_SUPPORT
#include "audio.h"
#endif

ConfigDialog::ConfigDialog(QWidget *parent)
    : QDialog(parent), ui(new Ui::ConfigDialog)
{
    ui->setupUi(this);
    loadConfig();

    connect(ui->enableEffectCheckBox, SIGNAL(toggled(bool)), ui->enableLastWordCheckBox, SLOT(setEnabled(bool)));
    connect(ui->checkBoxRecorderAutoSave, SIGNAL(toggled(bool)), ui->checkBoxRecorderNetworkOnly, SLOT(setEnabled(bool)));

    // 「顯示」分頁視角元素:變動即時預覽,按確定才寫入設定檔,取消復原
    connect(ui->themeSystemRadio, &QRadioButton::toggled, this, [this](bool on) { if (on && !m_loading) previewTheme(0); });
    connect(ui->themeLightRadio, &QRadioButton::toggled, this, [this](bool on) { if (on && !m_loading) previewTheme(1); });
    connect(ui->themeDarkRadio, &QRadioButton::toggled, this, [this](bool on) { if (on && !m_loading) previewTheme(2); });
    connect(ui->uiScaleSlider, &QSlider::valueChanged, this, [this](int value) {
        ui->uiScaleValueLabel->setText(QString::number(value / 20.0, 'f', 2) + "x");
        if (!m_loading)
            applyUiScalePreview();
    });
    connect(ui->visualModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { if (!m_loading) previewVisualMode(); });

    // 這四個勾選遊戲內以 Config.value() 即時讀取,預覽需立即寫入 QSettings,取消時再復原
    connect(ui->noIndicatorCheckBox, &QCheckBox::toggled, this, [this](bool v) { if (!m_loading) Config.setValue("NoIndicator", v); });
    connect(ui->noEquipAnimCheckBox, &QCheckBox::toggled, this, [this](bool v) { if (!m_loading) Config.setValue("NoEquipAnim", v); });
    connect(ui->noCardMoveAnimCheckBox, &QCheckBox::toggled, this, [this](bool v) { if (!m_loading) Config.setValue("NoCardMoveAnim", v); });
    connect(ui->enableAnimatedGeneralsCheckBox, &QCheckBox::toggled, this, [this](bool v) { if (!m_loading) Config.setValue("EnableAnimatedGenerals", v); });

    connect(this, SIGNAL(accepted()), this, SLOT(saveConfig()));
    connect(this, SIGNAL(rejected()), this, SLOT(restoreVisualSettings()));

    QFont font = UiConfig.AppFont;
    showFont(ui->appFontLineEdit, font);

    font = UiConfig.UIFont;
    showFont(ui->textEditFontLineEdit, font);

    QPalette palette;
    palette.setColor(QPalette::Text, UiConfig.TextEditColor);
    QColor color = UiConfig.TextEditColor;
    int aver = (color.red() + color.green() + color.blue()) / 3;
    palette.setColor(QPalette::Base, aver >= 208 ? Qt::black : Qt::white);
    ui->textEditFontLineEdit->setPalette(palette);
}

void ConfigDialog::loadConfig()
{
    // 程式設定 widget 值時不觸發預覽(避免每次開啟 dialog 就重套 palette / 重載主頁)
    m_loading = true;
    // 主题:0/1/2 直对 Qt::ColorScheme {Unknown(跟随系统), Light, Dark}
    switch (qBound(0, Config.ColorScheme, 2)) {
    case 1: ui->themeLightRadio->setChecked(true); break;
    case 2: ui->themeDarkRadio->setChecked(true); break;
    default: ui->themeSystemRadio->setChecked(true); break;
    }

    QString bg_path = Config.value("BackgroundImage").toString();
    if (bg_path.startsWith(":"))
        ui->bgPathLineEdit->clear();
    else
        ui->bgPathLineEdit->setText(bg_path);

    ui->bgMusicPathLineEdit->setText(Config.value("BackgroundMusic", "audio/system/background.ogg").toString());

    ui->enableEffectCheckBox->setChecked(Config.EnableEffects);
    ui->enableLastWordCheckBox->setEnabled(Config.EnableEffects);
    ui->enableLastWordCheckBox->setChecked(Config.EnableLastWord);

    //ui->enableBgMusicCheckBox->setChecked(Config.EnableBgMusic);

    ui->fullSkinCheckBox->setEnabled(false);
    ui->fullSkinCheckBox->setChecked(true);
    ui->noIndicatorCheckBox->setChecked(Config.value("NoIndicator").toBool());
    ui->noEquipAnimCheckBox->setChecked(Config.value("NoEquipAnim").toBool());
    ui->noCardMoveAnimCheckBox->setChecked(Config.value("NoCardMoveAnim", false).toBool());
    ui->enableAnimatedGeneralsCheckBox->setChecked(Config.value("EnableAnimatedGenerals", true).toBool());

    ui->uiScaleSlider->setValue(qRound(Config.UIScale * 20.0));
    ui->uiScaleValueLabel->setText(QString::number(ui->uiScaleSlider->value() / 20.0, 'f', 2) + "x");

    const QString visualMode = Config.VisualMode;
    if (visualMode == "grayscale")
        ui->visualModeCombo->setCurrentIndex(1);
    else if (visualMode == "highcontrast")
        ui->visualModeCombo->setCurrentIndex(2);
    else
        ui->visualModeCombo->setCurrentIndex(0);

    ui->bgmVolumeSlider->setValue(Config.BGMVolume * 100);
    ui->effectVolumeSlider->setValue(Config.EffectVolume * 100);
    ui->frontVolumeSlider->setValue(Config.FrontBGMVolume * 100);
    ui->frontVolumeSlider->setToolTip(tr("音频文件地址：audio/system/BGM/front-bgm.ogg 可替换为自己喜欢的音频"));

    // tab 2
    ui->neverNullifyMyTrickCheckBox->setChecked(Config.NeverNullifyMyTrick);
    ui->autoTargetCheckBox->setChecked(Config.EnableAutoTarget);
    ui->intellectualSelectionCheckBox->setChecked(Config.EnableIntellectualSelection);
    ui->doubleClickCheckBox->setChecked(Config.EnableDoubleClick);
    ui->superDragCheckBox->setChecked(Config.EnableSuperDrag);
    ui->bubbleChatBoxKeepSpinBox->setSuffix(tr(" millisecond"));
    ui->bubbleChatBoxKeepSpinBox->setValue(Config.BubbleChatBoxKeepTime);
    ui->backgroundChangeCheckBox->setChecked(Config.EnableAutoBackgroundChange);
    ui->backgroundCardDescription->setChecked(Config.EnableCardDescription);
    ui->enableOracleConceptsCheckBox->setChecked(Config.value("EnableOracleConcepts", true).toBool());

    ui->checkBoxRecorderAutoSave->setChecked(Config.value("recorder/autosave", true).toBool());
    ui->checkBoxRecorderNetworkOnly->setChecked(Config.value("recorder/networkonly", true).toBool());
    ui->checkBoxRecorderEventSave->setChecked(Config.value("recorder/eventsave", false).toBool());
    m_loading = false;
}

void ConfigDialog::showEvent(QShowEvent *event)
{
    // 每次開起都重新讀取已確認的設定並拍快照,取消時才能復原到上次確認的狀態
    loadConfig();
    snapshotVisualSettings();
    QDialog::showEvent(event);
}

void ConfigDialog::snapshotVisualSettings()
{
    m_visual.colorScheme = Config.ColorScheme;
    m_visual.uiScale = Config.UIScale;
    m_visual.backgroundImage = Config.BackgroundImage;
    m_visual.visualMode = Config.VisualMode;
    m_visual.noIndicator = Config.value("NoIndicator").toBool();
    m_visual.noEquipAnim = Config.value("NoEquipAnim").toBool();
    m_visual.noCardMoveAnim = Config.value("NoCardMoveAnim", false).toBool();
    m_visual.enableAnimatedGenerals = Config.value("EnableAnimatedGenerals", true).toBool();
}

void ConfigDialog::refitRoomScene()
{
    if (RoomSceneInstance) {
        MainWindow *mw = static_cast<MainWindow *>(Sanguosha->parent());
        if (qobject_cast<RoomScene *>(mw->getScene()) == RoomSceneInstance)
            mw->refitScene();
    }
}

void ConfigDialog::applyUiScalePreview()
{
    Config.UIScale = ui->uiScaleSlider->value() / 20.0;
    refitRoomScene();
}

void ConfigDialog::previewTheme(int scheme)
{
    Config.ColorScheme = scheme;
    applyColorScheme(scheme);
    if (Config.VisualMode != "normal")
        applyVisualMode(Config.VisualMode);
}

void ConfigDialog::previewVisualMode()
{
    switch (ui->visualModeCombo->currentIndex()) {
    case 1: Config.VisualMode = "grayscale"; break;
    case 2: Config.VisualMode = "highcontrast"; break;
    default: Config.VisualMode = "normal"; break;
    }
    // QML 主頁用 Config.getValue("VisualMode") 讀取,必須即時寫入才看得到效果
    Config.setValue("VisualMode", Config.VisualMode);
    applyVisualMode(Config.VisualMode);
    emit previewChanged();
}

void ConfigDialog::restoreVisualSettings()
{
    if (m_visual.colorScheme != Config.ColorScheme) {
        Config.ColorScheme = m_visual.colorScheme;
        applyColorScheme(m_visual.colorScheme);
    }
    if (!qFuzzyCompare(m_visual.uiScale, Config.UIScale)) {
        Config.UIScale = m_visual.uiScale;
        refitRoomScene();
    }
    if (m_visual.backgroundImage != Config.BackgroundImage) {
        Config.BackgroundImage = m_visual.backgroundImage;
        Config.setValue("BackgroundImage", m_visual.backgroundImage);
        emit bg_changed();
    }
    if (m_visual.visualMode != Config.VisualMode) {
        Config.VisualMode = m_visual.visualMode;
        Config.setValue("VisualMode", Config.VisualMode);
        applyVisualMode(Config.VisualMode);
        emit previewChanged();
    }
    Config.setValue("NoIndicator", m_visual.noIndicator);
    Config.setValue("NoEquipAnim", m_visual.noEquipAnim);
    Config.setValue("NoCardMoveAnim", m_visual.noCardMoveAnim);
    Config.setValue("EnableAnimatedGenerals", m_visual.enableAnimatedGenerals);
}

void ConfigDialog::showFont(QLineEdit *lineedit, const QFont &font)
{
    lineedit->setFont(font);
    lineedit->setText(QString("%1 %2").arg(font.family()).arg(font.pointSize()));
}

ConfigDialog::~ConfigDialog()
{
    delete ui;
}

void ConfigDialog::on_browseBgButton_clicked()
{
    QString filename = QFileDialog::getOpenFileName(this,
        tr("Select a background image"),
        "image/system/backdrop/",
        tr("Images and videos (*.png *.bmp *.jpg *.jpeg *.gif *.webp *.mp4 *.webm *.mkv)"));

    if (!filename.isEmpty()) {
        QString app_path = QApplication::applicationDirPath();
        if (filename.startsWith(app_path))
            filename = filename.right(filename.length() - app_path.length() - 1);
        ui->bgPathLineEdit->setText(filename);

        Config.BackgroundImage = filename;
        Config.setValue("BackgroundImage", filename);

        emit bg_changed();
        emit previewChanged();
    }
}

void ConfigDialog::on_resetBgButton_clicked()
{
    ui->bgPathLineEdit->clear();

    QString filename = "image/system/backdrop/default.jpg";
    Config.BackgroundImage = filename;
    Config.setValue("BackgroundImage", filename);

    emit bg_changed();
    emit previewChanged();
}

void ConfigDialog::saveConfig()
{
    float volume = ui->bgmVolumeSlider->value() / 100.0;
    Config.BGMVolume = volume;
    Config.setValue("BGMVolume", volume);
    volume = ui->effectVolumeSlider->value() / 100.0;
    Config.EffectVolume = volume;
    Config.setValue("EffectVolume", volume);
    volume = ui->frontVolumeSlider->value() / 100.0;
    Config.FrontBGMVolume = volume;
    Config.setValue("FrontBGMVolume", volume);

    bool enabled = ui->enableEffectCheckBox->isChecked();
    Config.EnableEffects = enabled;
    Config.setValue("EnableEffects", enabled);

    enabled = ui->enableLastWordCheckBox->isChecked();
    Config.EnableLastWord = enabled;
    Config.setValue("EnableLastWord", enabled);

    /*enabled = ui->enableBgMusicCheckBox->isChecked();
    Config.EnableBgMusic = enabled;
    Config.setValue("EnableBgMusic", enabled);*/

#ifdef AUDIO_SUPPORT
	if(volume>0){
		if (!ServerInfo.DuringGame&&QFile::exists("audio/system/BGM/front-bgm.ogg"))
			Audio::playBGM("audio/system/BGM/front-bgm.ogg");
		Audio::setBGMVolume(volume);
	}else
		Audio::stopBGM();
#endif

    Config.setValue("NoIndicator", ui->noIndicatorCheckBox->isChecked());
    Config.setValue("NoEquipAnim", ui->noEquipAnimCheckBox->isChecked());
    Config.setValue("NoCardMoveAnim", ui->noCardMoveAnimCheckBox->isChecked());
    Config.setValue("EnableAnimatedGenerals", ui->enableAnimatedGeneralsCheckBox->isChecked());
    Config.UIScale = ui->uiScaleSlider->value() / 20.0;
    Config.setValue("UIScale", Config.UIScale);

    // 主題預覽時已寫入 Config.ColorScheme 並套用 palette,確定時一律持久化
    int newScheme = ui->themeLightRadio->isChecked() ? 1
                  : ui->themeDarkRadio->isChecked()  ? 2 : 0;
    Config.ColorScheme = newScheme;
    Config.setValue("ColorScheme", newScheme);

    switch (ui->visualModeCombo->currentIndex()) {
    case 1:
        Config.VisualMode = "grayscale";
        break;
    case 2:
        Config.VisualMode = "highcontrast";
        break;
    default:
        Config.VisualMode = "normal";
        break;
    }
    Config.setValue("VisualMode", Config.VisualMode);
    // 確保視覺模式(灰階/高對比)與目前主題疊加正確
    applyVisualMode(Config.VisualMode);

    Config.NeverNullifyMyTrick = ui->neverNullifyMyTrickCheckBox->isChecked();
    Config.setValue("NeverNullifyMyTrick", Config.NeverNullifyMyTrick);

    Config.EnableAutoTarget = ui->autoTargetCheckBox->isChecked();
    Config.setValue("EnableAutoTarget", Config.EnableAutoTarget);

    Config.EnableIntellectualSelection = ui->intellectualSelectionCheckBox->isChecked();
    Config.setValue("EnableIntellectualSelection", Config.EnableIntellectualSelection);

    Config.EnableDoubleClick = ui->doubleClickCheckBox->isChecked();
    Config.setValue("EnableDoubleClick", Config.EnableDoubleClick);

    Config.EnableSuperDrag = ui->superDragCheckBox->isChecked();
    Config.setValue("EnableSuperDrag", Config.EnableSuperDrag);

    Config.BubbleChatBoxKeepTime = ui->bubbleChatBoxKeepSpinBox->value();
    Config.setValue("BubbleChatBoxKeepTime", Config.BubbleChatBoxKeepTime);

    Config.EnableAutoBackgroundChange = ui->backgroundChangeCheckBox->isChecked();
    Config.setValue("EnableAutoBackgroundChange", Config.EnableAutoBackgroundChange);

    Config.EnableCardDescription = ui->backgroundCardDescription->isChecked();
    Config.setValue("EnableCardDescription", Config.EnableCardDescription);

    Config.setValue("EnableOracleConcepts", ui->enableOracleConceptsCheckBox->isChecked());

    enabled = ui->checkBoxRecorderAutoSave->isChecked();
    Config.setValue("recorder/autosave", enabled);
    enabled = ui->checkBoxRecorderNetworkOnly->isChecked();
    Config.setValue("recorder/networkonly", enabled);
    enabled = ui->checkBoxRecorderEventSave->isChecked();
    Config.setValue("recorder/eventsave", enabled);

    if (RoomSceneInstance){
		MainWindow *mw = static_cast<MainWindow*>(Sanguosha->parent());
		if (qobject_cast<RoomScene*>(mw->getScene()) == RoomSceneInstance) {
			RoomSceneInstance->updateVolumeConfig();
			mw->refitScene();
		}
	}
}

void ConfigDialog::on_browseBgMusicButton_clicked()
{
    QString filename = QFileDialog::getOpenFileName(this,
        tr("Select a background music"),
        "audio/system/BGM",
        tr("Audio files (*.wav *.mp3 *.ogg)"));
    if (!filename.isEmpty()) {
        QString app_path = QApplication::applicationDirPath();
        if (filename.startsWith(app_path))
            filename = filename.right(filename.length() - app_path.length() - 1);
        ui->bgMusicPathLineEdit->setText(filename);
        Config.setValue("BackgroundMusic", filename);
    }
    /*QStringList fileNames = QFileDialog::getOpenFileNames(this, tr("Select a background music"), "audio/system", tr("Audio files (*.wav *.mp3 *.ogg)"));
	if(fileNames.isEmpty()) return;
    QString app_path = QApplication::applicationDirPath();
    app_path.replace("\\", "/");
    int app_path_len = app_path.length();
    foreach (const QString &name, fileNames) {
        const_cast<QString &>(name).replace("\\", "/");
        if (name.startsWith(app_path)) {
            const_cast<QString &>(name) = name.right(name.length() - app_path_len - 1);
        }
    }
    ui->bgMusicPathLineEdit->setText(fileNames.join(";"));
	Config.setValue("BackgroundMusic", fileNames.join(";"));*/
}

void ConfigDialog::on_resetBgMusicButton_clicked()
{
    QString default_music = "audio/system/background.ogg";
    Config.setValue("BackgroundMusic", default_music);
    ui->bgMusicPathLineEdit->setText(default_music);
}

void ConfigDialog::on_changeAppFontButton_clicked()
{
    bool ok;
    QFont font = QFontDialog::getFont(&ok, UiConfig.AppFont, this);
    if (ok) {
        UiConfig.AppFont = font;
        showFont(ui->appFontLineEdit, font);

        Config.setValue("AppFont", font);
        QApplication::setFont(font);
    }
}

void ConfigDialog::on_setTextEditFontButton_clicked()
{
    bool ok;
    QFont font = QFontDialog::getFont(&ok, UiConfig.UIFont, this);
    if (ok) {
        UiConfig.UIFont = font;
        showFont(ui->textEditFontLineEdit, font);

        Config.setValue("UIFont", font);
        QApplication::setFont(font, "QTextEdit");
    }
}

void ConfigDialog::on_setTextEditColorButton_clicked()
{
    QColor color = QColorDialog::getColor(UiConfig.TextEditColor, this);
    if (color.isValid()) {
        UiConfig.TextEditColor = color;
        Config.setValue("TextEditColor", color);
        QPalette palette;
        palette.setColor(QPalette::Text, color);
        int aver = (color.red() + color.green() + color.blue()) / 3;
        palette.setColor(QPalette::Base, aver >= 208 ? Qt::black : Qt::white);
        ui->textEditFontLineEdit->setPalette(palette);
    }
}

