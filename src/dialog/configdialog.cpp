#include "configdialog.h"
#include "ui_configdialog.h"
#include "settingssession.h"
#include "settings.h"

ConfigDialog::ConfigDialog(SettingsSession *session, QWidget *parent)
    : QDialog(parent), ui(new Ui::ConfigDialog), m_session(session)
{
    ui->setupUi(this);
#if !defined(QSAN_XP_LEGACY)
    auto *layoutGroup = new QGroupBox(tr("直向與單手操作"), this);
    auto *layoutOptions = new QVBoxLayout(layoutGroup);
    m_responsiveLayout = new QCheckBox(tr("自適應版面（首頁、對話框與牌桌）"), layoutGroup);
    m_oneHandedness = new QComboBox(layoutGroup);
    m_oneHandedness->setAccessibleName(tr("單手操作"));
    m_oneHandedness->addItems({tr("雙手／無偏好"), tr("左手操作"), tr("右手操作")});
    layoutOptions->addWidget(m_responsiveLayout);
    layoutOptions->addWidget(m_oneHandedness);
    layoutOptions->addWidget(new QLabel(tr("直向背景（獨立保存）"), layoutGroup));
    m_portraitBackground = new QLineEdit(layoutGroup);
    m_portraitBackground->setReadOnly(true);
    layoutOptions->addWidget(m_portraitBackground);
    auto *portraitButtons = new QHBoxLayout;
    auto *browsePortrait = new QPushButton(tr("選擇直向背景"), layoutGroup);
    auto *resetPortrait = new QPushButton(tr("恢復直向預設"), layoutGroup);
    portraitButtons->addWidget(browsePortrait);
    portraitButtons->addWidget(resetPortrait);
    layoutOptions->addLayout(portraitButtons);
    connect(browsePortrait, &QPushButton::clicked, this, [this] { m_session->choosePortraitBackground(this); });
    connect(resetPortrait, &QPushButton::clicked, m_session, &SettingsSession::resetPortraitBackground);
    ui->envLayout->insertWidget(0, layoutGroup);
    connect(m_responsiveLayout, &QCheckBox::toggled, this, [this](bool enabled) {
        bindValue("UI/ResponsiveLayout", enabled);
    });
    connect(m_oneHandedness, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int hand) {
        bindValue("UI/RoomHandedness", hand);
    });
#endif
    ui->fullSkinCheckBox->setEnabled(false);
    ui->fullSkinCheckBox->setChecked(true);
    ui->bubbleChatBoxKeepSpinBox->setSuffix(tr(" millisecond"));
    ui->frontVolumeSlider->setToolTip(tr("音频文件地址：audio/system/BGM/front-bgm.ogg 可替换为自己喜欢的音频"));
#if defined(QSAN_XP_LEGACY)
    // XP is a fixed raster profile; do not expose an option that cannot take effect.
    ui->effectsProfileLabel->hide();
    ui->effectsProfileComboBox->hide();
#else
    ui->effectsProfileComboBox->addItem(tr("Full effects"), QStringLiteral("full"));
    ui->effectsProfileComboBox->addItem(tr("Reduced effects"), QStringLiteral("reduced"));
    ui->effectsProfileComboBox->addItem(tr("No decorative effects"), QStringLiteral("none"));
    connect(ui->effectsProfileComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, [this](int index) {
            bindValue("EffectsProfile", ui->effectsProfileComboBox->itemData(index));
        });
#endif

    connect(ui->enableEffectCheckBox, SIGNAL(toggled(bool)), ui->enableLastWordCheckBox, SLOT(setEnabled(bool)));
    connect(ui->checkBoxRecorderAutoSave, SIGNAL(toggled(bool)), ui->checkBoxRecorderNetworkOnly, SLOT(setEnabled(bool)));

    // 「顯示」分頁視角元素由 session 即時預覽,按確定才寫入設定檔,取消復原
    connect(ui->themeSystemRadio, &QRadioButton::toggled, this, [this](bool on) { if (on) bindValue("ColorScheme", 0); });
    connect(ui->themeLightRadio, &QRadioButton::toggled, this, [this](bool on) { if (on) bindValue("ColorScheme", 1); });
    connect(ui->themeDarkRadio, &QRadioButton::toggled, this, [this](bool on) { if (on) bindValue("ColorScheme", 2); });
    connect(ui->uiScaleSlider, &QSlider::valueChanged, this, [this](int value) {
        ui->uiScaleValueLabel->setText(QString::number(value / 20.0, 'f', 2) + "x");
        bindValue("UIScale", value / 20.0);
    });
    connect(ui->visualModeCombo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
        this, [this](int index) {
            bindValue("VisualMode", index == 1 ? "grayscale" : index == 2 ? "highcontrast" : "normal");
        });

    const struct { QAbstractButton *button; const char *key; } checks[] = {
        { ui->noIndicatorCheckBox, "NoIndicator" },
        { ui->noEquipAnimCheckBox, "NoEquipAnim" },
        { ui->noCardMoveAnimCheckBox, "NoCardMoveAnim" },
        { ui->enableAnimatedGeneralsCheckBox, "EnableAnimatedGenerals" },
        { ui->enablePointerEffectCheckBox, "EnablePointerEffect" },
        { ui->backgroundChangeCheckBox, "EnableAutoBackgroundChange" },
        { ui->backgroundVideoCheckBox, "EnableBackgroundVideo" },
        { ui->enableEffectCheckBox, "EnableEffects" },
        { ui->enableLastWordCheckBox, "EnableLastWord" },
        { ui->muteCheckBox, "AudioMuted" },
        { ui->neverNullifyMyTrickCheckBox, "NeverNullifyMyTrick" },
        { ui->autoTargetCheckBox, "EnableAutoTarget" },
        { ui->intellectualSelectionCheckBox, "EnableIntellectualSelection" },
        { ui->doubleClickCheckBox, "EnableDoubleClick" },
        { ui->superDragCheckBox, "EnableSuperDrag" },
        { ui->backgroundCardDescription, "EnableCardDescription" },
        { ui->enableOracleConceptsCheckBox, "EnableOracleConcepts" },
        { ui->checkBoxRecorderAutoSave, "recorder/autosave" },
        { ui->checkBoxRecorderNetworkOnly, "recorder/networkonly" },
        { ui->checkBoxRecorderEventSave, "recorder/eventsave" },
    };
    for (const auto &check : checks) {
        const QString key = QString::fromLatin1(check.key);
        connect(check.button, &QAbstractButton::toggled, this, [this, key](bool on) { bindValue(key, on); });
    }

    const struct { QSlider *slider; const char *key; } volumes[] = {
        { ui->bgmVolumeSlider, "BGMVolume" },
        { ui->effectVolumeSlider, "EffectVolume" },
        { ui->frontVolumeSlider, "FrontBGMVolume" },
        { ui->masterVolumeSlider, "MasterVolume" },
        { ui->voiceVolumeSlider, "VoiceVolume" },
    };
    for (const auto &volume : volumes) {
        const QString key = QString::fromLatin1(volume.key);
        connect(volume.slider, &QSlider::valueChanged, this, [this, key](int value) { bindValue(key, value / 100.0); });
    }
    connect(ui->bubbleChatBoxKeepSpinBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
        this, [this](int value) { bindValue("BubbleChatBoxKeepTime", value); });

    // Pointer-based connect: a SLOT() string to a non-slot only fails at runtime ("No such slot")
    // and would silently leave Cancel unable to restore the already-applied preview settings.
    connect(this, &QDialog::accepted, m_session, &SettingsSession::commit);
    connect(this, &QDialog::rejected, m_session, &SettingsSession::revert);
    // 首頁設定頁或版面浮窗改動同一份 session 時,widget 跟著更新。
    connect(m_session, &SettingsSession::valuesChanged, this, &ConfigDialog::loadConfig);

    loadConfig();
}

void ConfigDialog::bindValue(const QString &key, const QVariant &value)
{
    if (!m_loading)
        m_session->setValue(key, value);
}

void ConfigDialog::loadConfig()
{
    // 程式設定 widget 值時不回寫 session(避免每次開啟 dialog 就重套 palette / 重載主頁)
    m_loading = true;
    const QVariantMap v = m_session->values();
    if (m_responsiveLayout) m_responsiveLayout->setChecked(v.value("UI/ResponsiveLayout").toBool());
    if (m_oneHandedness) m_oneHandedness->setCurrentIndex(v.value("UI/RoomHandedness").toInt());
    if (m_portraitBackground) m_portraitBackground->setText(v.value("UI/PortraitBackgroundImage").toString());
    // 主题:0/1/2 直对 Qt::ColorScheme {Unknown(跟随系统), Light, Dark}
    switch (v.value("ColorScheme").toInt()) {
    case 1: ui->themeLightRadio->setChecked(true); break;
    case 2: ui->themeDarkRadio->setChecked(true); break;
    default: ui->themeSystemRadio->setChecked(true); break;
    }

    const QString bg_path = v.value("BackgroundImage").toString();
    ui->bgPathLineEdit->setText(bg_path.startsWith(":") ? QString() : bg_path);
    ui->bgMusicPathLineEdit->setText(v.value("BackgroundMusic").toString());

    ui->enableEffectCheckBox->setChecked(v.value("EnableEffects").toBool());
    ui->enableLastWordCheckBox->setEnabled(v.value("EnableEffects").toBool());
    ui->enableLastWordCheckBox->setChecked(v.value("EnableLastWord").toBool());

    ui->noIndicatorCheckBox->setChecked(v.value("NoIndicator").toBool());
    ui->noEquipAnimCheckBox->setChecked(v.value("NoEquipAnim").toBool());
    ui->noCardMoveAnimCheckBox->setChecked(v.value("NoCardMoveAnim").toBool());
    ui->enableAnimatedGeneralsCheckBox->setChecked(v.value("EnableAnimatedGenerals").toBool());
    ui->enablePointerEffectCheckBox->setChecked(v.value("EnablePointerEffect").toBool());

#if !defined(QSAN_XP_LEGACY)
    {
        const int index = ui->effectsProfileComboBox->findData(v.value("EffectsProfile"));
        ui->effectsProfileComboBox->setCurrentIndex(index >= 0 ? index : 0);
    }
#endif

    ui->uiScaleSlider->setValue(qRound(v.value("UIScale").toDouble() * 20.0));
    ui->uiScaleValueLabel->setText(QString::number(ui->uiScaleSlider->value() / 20.0, 'f', 2) + "x");

    const QString visualMode = v.value("VisualMode").toString();
    if (visualMode == "grayscale")
        ui->visualModeCombo->setCurrentIndex(1);
    else if (visualMode == "highcontrast")
        ui->visualModeCombo->setCurrentIndex(2);
    else
        ui->visualModeCombo->setCurrentIndex(0);

    ui->bgmVolumeSlider->setValue(qRound(v.value("BGMVolume").toDouble() * 100));
    ui->effectVolumeSlider->setValue(qRound(v.value("EffectVolume").toDouble() * 100));
    ui->frontVolumeSlider->setValue(qRound(v.value("FrontBGMVolume").toDouble() * 100));
    ui->masterVolumeSlider->setValue(qRound(v.value("MasterVolume").toDouble() * 100));
    ui->voiceVolumeSlider->setValue(qRound(v.value("VoiceVolume").toDouble() * 100));
    ui->muteCheckBox->setChecked(v.value("AudioMuted").toBool());
    ui->backgroundVideoCheckBox->setChecked(v.value("EnableBackgroundVideo").toBool());

    // tab 2
    ui->neverNullifyMyTrickCheckBox->setChecked(v.value("NeverNullifyMyTrick").toBool());
    ui->autoTargetCheckBox->setChecked(v.value("EnableAutoTarget").toBool());
    ui->intellectualSelectionCheckBox->setChecked(v.value("EnableIntellectualSelection").toBool());
    ui->doubleClickCheckBox->setChecked(v.value("EnableDoubleClick").toBool());
    ui->superDragCheckBox->setChecked(v.value("EnableSuperDrag").toBool());
    ui->bubbleChatBoxKeepSpinBox->setValue(v.value("BubbleChatBoxKeepTime").toInt());
    ui->backgroundChangeCheckBox->setChecked(v.value("EnableAutoBackgroundChange").toBool());
    ui->backgroundCardDescription->setChecked(v.value("EnableCardDescription").toBool());
    ui->enableOracleConceptsCheckBox->setChecked(v.value("EnableOracleConcepts").toBool());

    ui->checkBoxRecorderAutoSave->setChecked(v.value("recorder/autosave").toBool());
    ui->checkBoxRecorderNetworkOnly->setChecked(v.value("recorder/networkonly").toBool());
    ui->checkBoxRecorderEventSave->setChecked(v.value("recorder/eventsave").toBool());

    showFont(ui->appFontLineEdit, v.value("AppFont").value<QFont>());
    showFont(ui->textEditFontLineEdit, v.value("UIFont").value<QFont>());
    showTextEditColor(v.value("TextEditColor").value<QColor>());
    m_loading = false;
}

void ConfigDialog::showEvent(QShowEvent *event)
{
    // 每次開啟都開始一次編輯(拍快照);首頁設定頁已在編輯中則沿用同一份草稿
    m_session->begin();
    loadConfig();
    QDialog::showEvent(event);
}

void ConfigDialog::showFont(QLineEdit *lineedit, const QFont &font)
{
    lineedit->setFont(font);
    lineedit->setText(SettingsSession::fontLabel(font));
}

void ConfigDialog::showTextEditColor(const QColor &color)
{
    QPalette palette;
    palette.setColor(QPalette::Text, color);
    int aver = (color.red() + color.green() + color.blue()) / 3;
    palette.setColor(QPalette::Base, aver >= 208 ? Qt::black : Qt::white);
    ui->textEditFontLineEdit->setPalette(palette);
}

ConfigDialog::~ConfigDialog()
{
    delete ui;
}

void ConfigDialog::on_browseBgButton_clicked()
{
    m_session->chooseBackgroundImage(this);
}

void ConfigDialog::on_resetBgButton_clicked()
{
    m_session->resetBackgroundImage();
}

void ConfigDialog::on_browseBgMusicButton_clicked()
{
    m_session->chooseBackgroundMusic(this);
}

void ConfigDialog::on_resetBgMusicButton_clicked()
{
    m_session->resetBackgroundMusic();
}

void ConfigDialog::on_changeAppFontButton_clicked()
{
    m_session->chooseAppFont(this);
}

void ConfigDialog::on_setTextEditFontButton_clicked()
{
    m_session->chooseTextEditFont(this);
}

void ConfigDialog::on_setTextEditColorButton_clicked()
{
    m_session->chooseTextEditColor(this);
}
