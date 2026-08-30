#if defined(QSAN_SERVER_CORE_ONLY)
#include "server-core.h"
#else
#include "server.h"
#endif
#include "settings.h"
#include "room.h"
#include "roomthread.h"
#include "engine.h"
#include "nativesocket.h"
#include "banpair.h"
#include "server-info.h"
#include "server-connection-context.h"
#include "protocol/session/session-payloads.h"
//#include "scenario.h"
#if !defined(QSAN_SERVER_CORE_ONLY)
#include "choosegeneraldialog.h"
#include "collapsible-section.h"
#include "customassigndialog.h"
#include "package.h"
#endif
#include "miniscenarios.h"
#if !defined(QSAN_SERVER_CORE_ONLY)
#include "skin-bank.h"
#endif
//#include "json.h"
#include "gamerule.h"
#include "game-rng.h"
#if !defined(QSAN_SERVER_CORE_ONLY)
#include "clientstruct.h"
#endif
#include "qtupnpportmapping.h"
#include "defines.h"

#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QHashSeed>
#include <QTimer>
#include <QPointer>
#include <QCoreApplication>
#include <QElapsedTimer>
#if !defined(QSAN_SERVER_CORE_ONLY)
#include <QScrollArea>
#endif

#include <algorithm>

using namespace QSanProtocol;

namespace
{
SetupPayload currentSetupPayload()
{
	SetupPayload payload;
	payload.serverName = Config.ServerName;
	payload.gameMode = Config.GameMode.mode_id;
	if (payload.gameMode == QLatin1String("02_1v1"))
		payload.gameRuleMode = Config.value("1v1/Rule", "2013").toString();
	else if (payload.gameMode == QLatin1String("06_3v3"))
		payload.gameRuleMode = Config.value("3v3/OfficialRule", "2013").toString();
	payload.operationTimeout = Config.OperationNoLimit ? 0 : Config.OperationTimeout;
	payload.nullificationCountdown = Config.NullificationCountDown;
	payload.serverTimeoutGraciousPeriod = Settings::S_SERVER_TIMEOUT_GRACIOUS_PERIOD;
	payload.banPackages = Sanguosha->getBanPackages();
	payload.randomSeat = Config.RandomSeat;
	payload.enableCheat = Config.EnableCheat;
	payload.freeChoose = Config.EnableCheat && Config.FreeChoose;
	payload.enableSecondGeneral = Config.Enable2ndGeneral;
	payload.enableSame = Config.EnableSame;
	payload.enableBasara = Config.EnableBasara;
	payload.enableHegemony = Config.EnableHegemony;
	payload.enableMeleeMode = Config.EnableMeleeMode;
	payload.enableAi = Config.EnableAI;
	payload.disableChat = Config.DisableChat;
	payload.maxHpScheme = Config.MaxHpScheme;
	payload.scheme0Subtraction = Config.Scheme0Subtraction;
	payload.playerCount = Config.GameMode.player_count;
	return payload;
}

bool sendSetup(ServerPlayer *player, QString *error)
{
	if (player == nullptr)
		return false;
	ProtocolMessage message;
	message.type = ProtocolMessageType::Notification;
	message.source = ProtocolEndpoint::Lobby;
	message.destination = ProtocolEndpoint::Client;
	message.command = S_COMMAND_SETUP;
	message.hasPayload = true;
	message.payload = currentSetupPayload().toVariant();
	if (player->sendProtocolMessage(message) == 0) {
		if (error != nullptr)
			*error = QStringLiteral("Unable to send typed V2 SETUP");
		return false;
	}
	return true;
}
}

#if !defined(QSAN_SERVER_CORE_ONLY)
static QLayout *HLay(QWidget *left, QWidget *right)
{
	QHBoxLayout *layout = new QHBoxLayout;
	layout->addWidget(left);
	layout->addWidget(right);
	return layout;
}

ServerDialog::ServerDialog(QWidget *parent)
	: QDialog(parent), boss_mode_button(nullptr), accept_type(0)
{
	setWindowTitle(tr("Start server"));

#ifdef ANDROID
    // Android: Adjust dialog size based on device type
    QScreen *screen = QApplication::primaryScreen();
    if (screen) {
        QRect screenGeometry = screen->availableGeometry();

        // Detect device type
        bool isTablet = (screenGeometry.width() >= 800 && screenGeometry.height() >= 600) ||
                       (screenGeometry.width() >= 600 && screenGeometry.height() >= 800);

        int width, height;
        if (isTablet) {
            // Tablet: Use 85% of screen
            width = screenGeometry.width() * 0.85;
            height = screenGeometry.height() * 0.85;
        } else {
            // Phone: Use 95% of screen
            width = screenGeometry.width() * 0.95;
            height = screenGeometry.height() * 0.95;
        }

        resize(width, height);
        setMinimumSize(300, 600); // Increased minimum height for package selection tab
        qDebug() << "ServerDialog size:" << QSize(width, height) << "Device:" << (isTablet ? "Tablet" : "Phone");
    } else {
        setMinimumWidth(300);
    }
#else
    setMinimumWidth(300);
#endif

	QTabWidget *tab_widget = new QTabWidget;
	tab_widget->addTab(createBasicTab(), tr("Basic"));
	tab_widget->addTab(createPackageTab(), tr("Game Pacakge Selection"));
	tab_widget->addTab(createAdvancedTab(), tr("Advanced"));
	tab_widget->addTab(createMiscTab(), tr("Miscellaneous"));

#ifdef ANDROID
    // Android: Ensure tab widget uses full width
    tab_widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
#endif

    QVBoxLayout *layout = new QVBoxLayout;
    layout->addWidget(tab_widget);
    layout->addLayout(createButtonLayout());

#ifdef ANDROID
    // Android: Minimize margins to use more space
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(5);
#endif

    setLayout(layout);
}

QWidget *ServerDialog::createBasicTab()
{
	server_name_edit = new QLineEdit;
	server_name_edit->setText(Config.ServerName);

	timeout_spinbox = new QSpinBox;
	timeout_spinbox->setMinimum(5);
	timeout_spinbox->setMaximum(60);
	timeout_spinbox->setValue(Config.OperationTimeout);
	timeout_spinbox->setSuffix(tr(" seconds"));
	nolimit_checkbox = new QCheckBox(tr("No limit"));
	nolimit_checkbox->setChecked(Config.OperationNoLimit);
	connect(nolimit_checkbox, SIGNAL(toggled(bool)), timeout_spinbox, SLOT(setDisabled(bool)));

	QHBoxLayout *lay = new QHBoxLayout;
	lay->addWidget(timeout_spinbox);
	lay->addWidget(nolimit_checkbox);
	// add 1v1 banlist edit button
	QPushButton *edit_button = new QPushButton(tr("Banlist ..."));
	edit_button->setFixedWidth(100);
	connect(edit_button, SIGNAL(clicked()), this, SLOT(edit1v1Banlist()));
	lay->addWidget(edit_button);
	QFormLayout *form_layout = new QFormLayout;
	form_layout->addRow(tr("Server name"), server_name_edit);
	form_layout->addRow(tr("Operation timeout"), lay);
	form_layout->addRow(createGameModeBox());

	QWidget *widget = new QWidget;
	widget->setLayout(form_layout);
	return widget;
}

QWidget *ServerDialog::createPackageTab()
{
	disable_lua_checkbox = new QCheckBox(tr("Disable Lua"));
	disable_lua_checkbox->setChecked(Config.DisableLua);
	disable_lua_checkbox->setToolTip(tr("The setting takes effect after reboot"));

	extension_group = new QButtonGroup;
	extension_group->setExclusive(false);

	auto makeCheckbox = [this](const Package *package) {
		QCheckBox *checkbox = new QCheckBox;
		checkbox->setObjectName(package->objectName());
		checkbox->setText(Sanguosha->translate(package->objectName()));
		checkbox->setChecked(
			!Config.BanPackages.contains(package->objectName()) && !package->isForbid());
		checkbox->setEnabled(!package->isForbid());

		QString contents;
		foreach (const General *general, package->findChildren<const General *>()) {
			if (general->isTotallyHidden())
				continue;
			const QString translatedName = general->getBriefName();
			if (contents.contains(translatedName))
				continue;
			contents.append(contents.isEmpty() ? "<b>武将</b>：" : "，");
			contents.append(translatedName);
		}
		if (!contents.isEmpty())
			contents += "<br/><br/>";
		foreach (const Card *card, package->findChildren<const Card *>()) {
			const QString translatedName = Sanguosha->translate(card->objectName());
			if (contents.contains(translatedName))
				continue;
			contents.append(contents.contains("<b>卡牌</b>：") ? "，" : "<b>卡牌</b>：");
			contents.append(translatedName);
		}
		checkbox->setToolTip(contents);
		extension_group->addButton(checkbox);
		return checkbox;
	};

	QVBoxLayout *sectionsLayout = new QVBoxLayout;
	QHash<QString, const Package *> packagesByAdder;
	foreach (const Package *package, Sanguosha->getPackages()) {
		if (!package->adderName().isEmpty())
			packagesByAdder.insert(package->adderName(), package);
	}

	const QMap<QString, QStringList> packageMap = Sanguosha->getPackageMap();
	for (auto it = packageMap.cbegin(); it != packageMap.cend(); ++it) {
		if (it.key() == QStringLiteral("g_special_play"))
			continue;
		CollapsibleSection *section =
			new CollapsibleSection(Sanguosha->translate(it.key()));
		foreach (const QString &adderName, it.value()) {
			const Package *package = packagesByAdder.value(adderName, nullptr);
			if (!package)
				continue;
			section->addPackageCheckbox(makeCheckbox(package));
		}
		if (section->isEmpty())
			delete section;
		else
			sectionsLayout->addWidget(section);
	}

	CollapsibleSection *luaGeneralSection =
		new CollapsibleSection(Sanguosha->translate(QStringLiteral("lua_package")));
	CollapsibleSection *luaCardSection =
		new CollapsibleSection(Sanguosha->translate(QStringLiteral("lua_card")));
	const QStringList luaPackages = Config.value("LuaPackages").toString()
		.split("+", Qt::SkipEmptyParts);
	foreach (const QString &packageName, luaPackages) {
		const Package *package =
			Sanguosha->findChild<const Package *>(packageName);
		if (!package)
			continue;
		QCheckBox *checkbox = makeCheckbox(package);
		if (package->getType() == Package::CardPack)
			luaCardSection->addPackageCheckbox(checkbox);
		else
			luaGeneralSection->addPackageCheckbox(checkbox);
	}
	if (luaGeneralSection->isEmpty())
		delete luaGeneralSection;
	else
		sectionsLayout->addWidget(luaGeneralSection);
	if (luaCardSection->isEmpty())
		delete luaCardSection;
	else
		sectionsLayout->addWidget(luaCardSection);
	sectionsLayout->addStretch();

	QWidget *sectionsContainer = new QWidget;
	sectionsContainer->setLayout(sectionsLayout);

	QScrollArea *scroll = new QScrollArea;
	scroll->setWidget(sectionsContainer);
	scroll->setWidgetResizable(true);
#ifndef ANDROID
	scroll->setMinimumWidth(sectionsContainer->sizeHint().width() + 24);
#endif

	add_god_general = new QCheckBox("加入神将");
	add_god_general->setChecked(Config.AddGodGeneral);
	add_god_general->setToolTip("不启用此项则游戏中不会加入神势力武将");

	general_version_dedup = new QCheckBox("同名武将只保留高版本");
	general_version_dedup->setChecked(Config.GeneralVersionDedup);
	general_version_dedup->setToolTip(
		"三版 > 二版 > 谋攻篇 > 界·OL > 十周年 > 新版 > 手杀 > OL > 翼 > 怀旧 > 基础");

	QHBoxLayout *optionRow = new QHBoxLayout;
	optionRow->addWidget(disable_lua_checkbox);
	optionRow->addWidget(add_god_general);
	optionRow->addWidget(general_version_dedup);
	optionRow->addStretch();

	QVBoxLayout *layout = new QVBoxLayout;
	layout->addLayout(optionRow);
	layout->addWidget(scroll);

	QWidget *widget = new QWidget;
	widget->setLayout(layout);
	return widget;
}

void ServerDialog::setMaxHpSchemeBox()
{
	if (!second_general_checkbox->isChecked()) {
		prevent_awaken_below3_checkbox->setVisible(false);

		scheme0_subtraction_label->setVisible(false);
		scheme0_subtraction_spinbox->setVisible(false);

		return;
	}
	int index = max_hp_scheme_ComboBox->currentIndex();
	if (index == 0) {
		prevent_awaken_below3_checkbox->setVisible(false);

		scheme0_subtraction_label->setVisible(true);
		scheme0_subtraction_spinbox->setVisible(true);
		scheme0_subtraction_spinbox->setValue(Config.value("Scheme0Subtraction", 3).toInt());
		scheme0_subtraction_spinbox->setEnabled(true);
	} else {
		prevent_awaken_below3_checkbox->setVisible(true);
		prevent_awaken_below3_checkbox->setChecked(Config.value("PreventAwakenBelow3", false).toBool());
		prevent_awaken_below3_checkbox->setEnabled(true);

		scheme0_subtraction_label->setVisible(false);
		scheme0_subtraction_spinbox->setVisible(false);
	}
}

QWidget *ServerDialog::createAdvancedTab()
{
	QVBoxLayout *layout = new QVBoxLayout;

	random_seat_checkbox = new QCheckBox(tr("Arrange the seats randomly"));
	random_seat_checkbox->setChecked(Config.RandomSeat);

	enable_cheat_checkbox = new QCheckBox(tr("Enable cheat"));
	enable_cheat_checkbox->setToolTip(tr("This option enables the cheat menu"));
	enable_cheat_checkbox->setChecked(Config.EnableCheat);

	free_choose_checkbox = new QCheckBox(tr("Choose generals and cards freely"));
	free_choose_checkbox->setChecked(Config.FreeChoose);
	free_choose_checkbox->setVisible(Config.EnableCheat);

	free_assign_checkbox = new QCheckBox(tr("Assign role and seat freely"));
	free_assign_checkbox->setChecked(Config.value("FreeAssign").toBool());
	free_assign_checkbox->setVisible(Config.EnableCheat);

	free_assign_self_checkbox = new QCheckBox(tr("Assign only your own role"));
	free_assign_self_checkbox->setChecked(Config.FreeAssignSelf);
	free_assign_self_checkbox->setEnabled(free_assign_checkbox->isChecked());
	free_assign_self_checkbox->setVisible(Config.EnableCheat);

	connect(enable_cheat_checkbox, SIGNAL(toggled(bool)), free_choose_checkbox, SLOT(setVisible(bool)));
	connect(enable_cheat_checkbox, SIGNAL(toggled(bool)), free_assign_checkbox, SLOT(setVisible(bool)));
	connect(enable_cheat_checkbox, SIGNAL(toggled(bool)), free_assign_self_checkbox, SLOT(setVisible(bool)));
	connect(free_assign_checkbox, SIGNAL(toggled(bool)), free_assign_self_checkbox, SLOT(setEnabled(bool)));

	pile_swapping_label = new QLabel(tr("Pile-swapping limitation"));
	pile_swapping_label->setToolTip(tr("-1 means no limitations"));
	pile_swapping_spinbox = new QSpinBox;
	pile_swapping_spinbox->setRange(-1, 15);
	pile_swapping_spinbox->setValue(Config.value("PileSwappingLimitation", 5).toInt());

	without_lordskill_checkbox = new QCheckBox(tr("Without Lordskill"));
	without_lordskill_checkbox->setChecked(Config.value("WithoutLordskill", false).toBool());

	sp_convert_checkbox = new QCheckBox(tr("Enable SP Convert"));
	sp_convert_checkbox->setChecked(Config.value("EnableSPConvert", true).toBool());

	maxchoice_spinbox = new QSpinBox;
	maxchoice_spinbox->setRange(3, 21);
	maxchoice_spinbox->setValue(Config.value("MaxChoice", 5).toInt());

	lord_maxchoice_label = new QLabel(tr("Upperlimit for lord"));
	lord_maxchoice_label->setToolTip(tr("-1 means that all lords are available"));
	lord_maxchoice_spinbox = new QSpinBox;
	lord_maxchoice_spinbox->setRange(-1, 15);
	lord_maxchoice_spinbox->setValue(Config.value("LordMaxChoice", -1).toInt());

	nonlord_maxchoice_spinbox = new QSpinBox;
	nonlord_maxchoice_spinbox->setRange(0, 15);
	nonlord_maxchoice_spinbox->setValue(Config.value("NonLordMaxChoice", 2).toInt());

	forbid_same_ip_checkbox = new QCheckBox(tr("Forbid same IP with multiple connection"));
	forbid_same_ip_checkbox->setChecked(Config.ForbidSIMC);

	disable_chat_checkbox = new QCheckBox(tr("Disable chat"));
	disable_chat_checkbox->setChecked(Config.DisableChat);

	second_general_checkbox = new QCheckBox(tr("Enable second general"));
	second_general_checkbox->setChecked(Config.Enable2ndGeneral);

	same_checkbox = new QCheckBox(tr("Enable Same"));
	same_checkbox->setChecked(Config.EnableSame);

	max_hp_label = new QLabel(tr("Max HP scheme"));
	max_hp_scheme_ComboBox = new QComboBox;
	max_hp_scheme_ComboBox->addItem(tr("Sum - X"));
	max_hp_scheme_ComboBox->addItem(tr("Minimum"));
	max_hp_scheme_ComboBox->addItem(tr("Maximum"));
	max_hp_scheme_ComboBox->addItem(tr("Average"));
	max_hp_scheme_ComboBox->setCurrentIndex(Config.MaxHpScheme);

	prevent_awaken_below3_checkbox = new QCheckBox(tr("Prevent maxhp being less than 3 for awaken skills"));
	prevent_awaken_below3_checkbox->setChecked(Config.PreventAwakenBelow3);
	prevent_awaken_below3_checkbox->setEnabled(max_hp_scheme_ComboBox->currentIndex() != 0);

	scheme0_subtraction_label = new QLabel(tr("Subtraction for scheme 0"));
	scheme0_subtraction_label->setVisible(max_hp_scheme_ComboBox->currentIndex() == 0);
	scheme0_subtraction_spinbox = new QSpinBox;
	scheme0_subtraction_spinbox->setRange(-5, 12);
	scheme0_subtraction_spinbox->setValue(Config.Scheme0Subtraction);
	scheme0_subtraction_spinbox->setVisible(max_hp_scheme_ComboBox->currentIndex() == 0);

	connect(max_hp_scheme_ComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(setMaxHpSchemeBox()));

	basara_checkbox = new QCheckBox(tr("Enable Basara"));
	basara_checkbox->setChecked(Config.EnableBasara);
	updateButtonEnablility(mode_group->checkedButton());
	connect(mode_group, SIGNAL(buttonClicked(QAbstractButton *)), this, SLOT(updateButtonEnablility(QAbstractButton *)));

	hegemony_checkbox = new QCheckBox(tr("Enable Hegemony"));
	hegemony_checkbox->setChecked(Config.EnableBasara && Config.EnableHegemony);
	hegemony_checkbox->setEnabled(basara_checkbox->isChecked());
	connect(basara_checkbox, SIGNAL(toggled(bool)), hegemony_checkbox, SLOT(setChecked(bool)));
	connect(basara_checkbox, SIGNAL(toggled(bool)), hegemony_checkbox, SLOT(setEnabled(bool)));

	melee_mode_checkbox = new QCheckBox(tr("Enable Melee Mode (Peach as Slash/Jink in late game)"));
	melee_mode_checkbox->setChecked(Config.EnableMeleeMode);

	hegemony_maxchoice_label = new QLabel(tr("Upperlimit for hegemony"));
	hegemony_maxchoice_spinbox = new QSpinBox;
	hegemony_maxchoice_spinbox->setRange(5, 21);
	hegemony_maxchoice_spinbox->setValue(Config.value("HegemonyMaxChoice", 7).toInt());

	hegemony_maxshown_label = new QLabel(tr("Max shown num for hegemony"));
	hegemony_maxshown_spinbox = new QSpinBox;
	hegemony_maxshown_spinbox->setRange(1, 11);
	hegemony_maxshown_spinbox->setValue(Config.value("HegemonyMaxShown", 2).toInt());

	address_edit = new QLineEdit;
	address_edit->setText(Config.Address);
	address_edit->setPlaceholderText(tr("Public IP or domain"));

	QPushButton *detect_button = new QPushButton(tr("Detect my WAN IP"));
	connect(detect_button, SIGNAL(clicked()), this, SLOT(onDetectButtonClicked()));

	port_edit = new QLineEdit;
	port_edit->setText(QString::number(Config.ServerPort));
	port_edit->setValidator(new QIntValidator(1000, 65535, port_edit));

	checkBoxUpnp = new QCheckBox("启用UPNP端口映射");
	checkBoxUpnp->setChecked(Config.value("serverconfig/upnp",true).toBool());

	checkBoxAddToListServer = new QCheckBox("加入列表服务器");
	checkBoxAddToListServer->setToolTip("让其他人能够通过“查找服务器”功能找到本服务器，只有能被外网访问的服务器才会加入列表中。");
	checkBoxAddToListServer->setChecked(Config.value("serverconfig/addtolistserver",false).toBool());

	layout->addWidget(forbid_same_ip_checkbox);
	layout->addWidget(disable_chat_checkbox);
	layout->addWidget(random_seat_checkbox);
	layout->addLayout(HLay(without_lordskill_checkbox, sp_convert_checkbox));
	layout->addLayout(HLay(lord_maxchoice_label, lord_maxchoice_spinbox));
	layout->addLayout(HLay(new QLabel(tr("Upperlimit for non-lord")), nonlord_maxchoice_spinbox));
	layout->addLayout(HLay(new QLabel(tr("Upperlimit for general")), maxchoice_spinbox));
	layout->addLayout(HLay(pile_swapping_label, pile_swapping_spinbox));
	layout->addWidget(enable_cheat_checkbox);
	layout->addWidget(free_choose_checkbox);
	layout->addLayout(HLay(free_assign_checkbox, free_assign_self_checkbox));
	layout->addWidget(second_general_checkbox);
	layout->addLayout(HLay(max_hp_label, max_hp_scheme_ComboBox));
	layout->addLayout(HLay(scheme0_subtraction_label, scheme0_subtraction_spinbox));
	layout->addWidget(prevent_awaken_below3_checkbox);
	layout->addLayout(HLay(basara_checkbox, hegemony_checkbox));
	layout->addWidget(melee_mode_checkbox);
	layout->addLayout(HLay(hegemony_maxchoice_label, hegemony_maxchoice_spinbox));
	layout->addLayout(HLay(hegemony_maxshown_label, hegemony_maxshown_spinbox));
	layout->addWidget(same_checkbox);
	layout->addLayout(HLay(new QLabel(tr("Address")), address_edit));
	layout->addWidget(detect_button);
	layout->addLayout(HLay(new QLabel(tr("Port")), port_edit));
	layout->addWidget(checkBoxUpnp);
	layout->addWidget(checkBoxAddToListServer);
	layout->addStretch();

	QWidget *widget = new QWidget;
	widget->setLayout(layout);

	max_hp_label->setVisible(Config.Enable2ndGeneral);
	connect(second_general_checkbox, SIGNAL(toggled(bool)), max_hp_label, SLOT(setVisible(bool)));
	max_hp_scheme_ComboBox->setVisible(Config.Enable2ndGeneral);
	connect(second_general_checkbox, SIGNAL(toggled(bool)), max_hp_scheme_ComboBox, SLOT(setVisible(bool)));

	if (Config.Enable2ndGeneral) {
		prevent_awaken_below3_checkbox->setVisible(max_hp_scheme_ComboBox->currentIndex() != 0);
		scheme0_subtraction_label->setVisible(max_hp_scheme_ComboBox->currentIndex() == 0);
		scheme0_subtraction_spinbox->setVisible(max_hp_scheme_ComboBox->currentIndex() == 0);
	} else {
		prevent_awaken_below3_checkbox->setVisible(false);
		scheme0_subtraction_label->setVisible(false);
		scheme0_subtraction_spinbox->setVisible(false);
	}
	connect(second_general_checkbox, SIGNAL(toggled(bool)), this, SLOT(setMaxHpSchemeBox()));

	hegemony_maxchoice_label->setVisible(Config.EnableHegemony);
	connect(hegemony_checkbox, SIGNAL(toggled(bool)), hegemony_maxchoice_label, SLOT(setVisible(bool)));
	hegemony_maxchoice_spinbox->setVisible(Config.EnableHegemony);
	connect(hegemony_checkbox, SIGNAL(toggled(bool)), hegemony_maxchoice_spinbox, SLOT(setVisible(bool)));

	hegemony_maxshown_label->setVisible(Config.EnableHegemony);
	connect(hegemony_checkbox, SIGNAL(toggled(bool)), hegemony_maxshown_label, SLOT(setVisible(bool)));
	hegemony_maxshown_spinbox->setVisible(Config.EnableHegemony);
	connect(hegemony_checkbox, SIGNAL(toggled(bool)), hegemony_maxshown_spinbox, SLOT(setVisible(bool)));

	hegemony_companion = new QComboBox;
	hegemony_companion->addItem(tr("Instant"), "Instant");
	hegemony_companion->addItem(tr("Postponed"), "Postponed");
	hegemony_companion->setCurrentIndex(Config.value("HegemonyCompanionReward", "Postponed").toString() == "Postponed" ? 1 : 0);
	hegemony_companion_label = new QLabel(tr("Companion Reward"));
	layout->addLayout(HLay(hegemony_companion_label, hegemony_companion));
	hegemony_companion_label->setVisible(Config.EnableHegemony);
	connect(hegemony_checkbox, SIGNAL(toggled(bool)), hegemony_companion_label, SLOT(setVisible(bool)));
	hegemony_companion->setVisible(Config.EnableHegemony);
	connect(hegemony_checkbox, SIGNAL(toggled(bool)), hegemony_companion, SLOT(setVisible(bool)));

	return widget;
}

QWidget *ServerDialog::createMiscTab()
{
	game_start_spinbox = new QSpinBox;
	game_start_spinbox->setRange(0, 10);
	game_start_spinbox->setValue(Config.CountDownSeconds);
	game_start_spinbox->setSuffix(tr(" seconds"));

	nullification_spinbox = new QSpinBox;
	nullification_spinbox->setRange(5, 15);
	nullification_spinbox->setValue(Config.NullificationCountDown);
	nullification_spinbox->setSuffix(tr(" seconds"));

	minimize_dialog_checkbox = new QCheckBox(tr("Minimize the dialog when server runs"));
	minimize_dialog_checkbox->setChecked(Config.EnableMinimizeDialog);

	surrender_at_death_checkbox = new QCheckBox(tr("Surrender at the time of Death"));
	surrender_at_death_checkbox->setChecked(Config.SurrenderAtDeath);

	luck_card_label = new QLabel(tr("Enable the luck card"));
	//luck_card_label->setToolTip(tr("-1 means no limit"));
	luck_card_spinbox = new QSpinBox;
	luck_card_spinbox->setRange(-1, 10);
	luck_card_spinbox->setValue(Config.value("LuckCardTimes", -1).toInt());
	luck_card_spinbox->setSuffix(QString(" 次"));

	QVBoxLayout *layout = new QVBoxLayout;

	//layout->addLayout(HLay(luck_card_label, luck_card_spinbox));

	QGroupBox *ai_groupbox = new QGroupBox(tr("Artificial intelligence"));
	ai_groupbox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	ai_enable_checkbox = new QCheckBox(tr("Enable AI"));
	ai_enable_checkbox->setChecked(Config.EnableAI);
	//ai_enable_checkbox->setEnabled(false); // Force to enable AI for disabling it causes crashes!!

	ai_chat_checkbox = new QCheckBox(tr("AI Chat"));
	ai_chat_checkbox->setChecked(Config.value("AIChat", true).toBool());
	ai_chat_checkbox->setEnabled(ai_enable_checkbox->isChecked());
	connect(ai_enable_checkbox, SIGNAL(toggled(bool)), ai_chat_checkbox, SLOT(setEnabled(bool)));

	ai_humanized_checkbox = new QCheckBox("人性化AI");
	ai_humanized_checkbox->setChecked(Config.value("AIHumanized", true).toBool());
	ai_humanized_checkbox->setToolTip(QString("AI开启聊天并出现小概率决策错误，在AI被询问操作时会加入延迟"));
	ai_humanized_checkbox->setEnabled(ai_enable_checkbox->isChecked());
	connect(ai_enable_checkbox, SIGNAL(toggled(bool)), ai_humanized_checkbox, SLOT(setEnabled(bool)));

	ai_delay_spinbox = new QSpinBox;
	ai_delay_spinbox->setMinimum(0);
	ai_delay_spinbox->setMaximum(5000);
	ai_delay_spinbox->setValue(Config.OriginAIDelay);
	ai_delay_spinbox->setSuffix(tr(" millisecond"));
	ai_delay_spinbox->setEnabled(ai_enable_checkbox->isChecked());
	connect(ai_enable_checkbox, SIGNAL(toggled(bool)), ai_delay_spinbox, SLOT(setEnabled(bool)));

	ai_delay_altered_checkbox = new QCheckBox(tr("Alter AI Delay After Death"));
	ai_delay_altered_checkbox->setChecked(Config.AlterAIDelayAD);
	ai_delay_altered_checkbox->setEnabled(ai_enable_checkbox->isChecked());
	connect(ai_enable_checkbox, SIGNAL(toggled(bool)), ai_delay_altered_checkbox, SLOT(setEnabled(bool)));

	ai_delay_ad_spinbox = new QSpinBox;
	ai_delay_ad_spinbox->setMinimum(0);
	ai_delay_ad_spinbox->setMaximum(5000);
	ai_delay_ad_spinbox->setValue(Config.AIDelayAD);
	ai_delay_ad_spinbox->setSuffix(tr(" millisecond"));
	ai_delay_ad_spinbox->setEnabled(ai_delay_altered_checkbox->isChecked());
	connect(ai_delay_altered_checkbox, SIGNAL(toggled(bool)), ai_delay_ad_spinbox, SLOT(setEnabled(bool)));

	//layout->addLayout(HLay(ai_enable_checkbox, ai_chat_checkbox));
	layout->addLayout(HLay(ai_enable_checkbox, ai_humanized_checkbox));
	layout->addLayout(HLay(new QLabel(tr("AI delay")), ai_delay_spinbox));
	layout->addWidget(ai_delay_altered_checkbox);
	layout->addLayout(HLay(new QLabel(tr("AI delay After Death")), ai_delay_ad_spinbox));

	ai_groupbox->setLayout(layout);

	QVBoxLayout *tablayout = new QVBoxLayout;
	tablayout->addLayout(HLay(new QLabel(tr("Game start count down")), game_start_spinbox));
	tablayout->addLayout(HLay(new QLabel(tr("Nullification count down")), nullification_spinbox));
	tablayout->addLayout(HLay(luck_card_label, luck_card_spinbox));
	tablayout->addWidget(minimize_dialog_checkbox);
	tablayout->addWidget(surrender_at_death_checkbox);
	tablayout->addWidget(ai_groupbox);
	tablayout->addStretch();

	QWidget *widget = new QWidget;
	widget->setLayout(tablayout);
	return widget;
}

void ServerDialog::updateButtonEnablility(QAbstractButton *button)
{
	if (!button) return;
	if (button->objectName().contains("scenario")
		|| button->objectName().contains("mini")
		|| button->objectName().contains("1v1")
		|| button->objectName().contains("1v3")) {
		basara_checkbox->setChecked(false);
		basara_checkbox->setEnabled(false);
	} else
		basara_checkbox->setEnabled(true);

	if (button->objectName().contains("mini")
		||button->objectName()=="02_1v1"
		||button->objectName()=="06_3v3"
		||button->objectName()=="06_XMode"
		||button->objectName()=="04_1v3") {
		mini_scene_button->setEnabled(true);
		second_general_checkbox->setChecked(false);
		second_general_checkbox->setEnabled(false);
	} else {
		second_general_checkbox->setEnabled(true);
		mini_scene_button->setEnabled(false);
	}
	if (boss_mode_button)
		boss_mode_button->setEnabled(button->objectName() == "04_boss");
}

void ServerDialog::updateModeGroupSelection(int index)
{
	QComboBox *combo = qobject_cast<QComboBox *>(sender());
	if (!combo || index < 0)
		return;

	QRadioButton *button = qobject_cast<QRadioButton *>(
		combo->property("modeGroupButton").value<QObject *>());
	if (!button)
		return;

	button->setObjectName(combo->itemData(index).toString());
	button->setChecked(true);
	updateButtonEnablility(button);
}

void BanlistDialog::switchTo(int item)
{
	this->item = item;
	list = lists.at(item);
	if (add2nd) add2nd->setVisible((list->objectName() == "Pairs"));
}

BanlistDialog::BanlistDialog(QWidget *parent, bool view)
	: QDialog(parent), add2nd(nullptr), card_to_ban(nullptr)
{
	setWindowTitle(tr("Select generals that are excluded"));
	setMinimumWidth(455);

	if (ban_list.isEmpty())
		ban_list << "Roles" << "1v1" << "Doudizhu" << "Happy2v2" << "BossMode"
		<< "Basara" << "Hegemony" << "Pairs" << "Cards" << "05_ol" << "06_ol";
	QVBoxLayout *layout = new QVBoxLayout;

	QTabWidget *tab = new QTabWidget;
	layout->addWidget(tab);
	connect(tab, SIGNAL(currentChanged(int)), this, SLOT(switchTo(int)));

	foreach (QString item, ban_list) {
		QWidget *apage = new QWidget;

		list = new QListWidget;
		list->setObjectName(item);

		if (item == "Pairs") {
			foreach(QString banned, BanPair::getAllBanSet().values())
				addGeneral(banned);
			foreach(QString banned, BanPair::getSecondBanSet().values())
				add2ndGeneral(banned);
			foreach(BanPair pair, BanPair::getBanPairSet().values())
				addPair(pair.first, pair.second);
		} else {
			foreach(QString name,  Config.value("Banlist/"+item).toStringList())
				addGeneral(name);
		}

		lists << list;

		QVBoxLayout *vlay = new QVBoxLayout;
		vlay->addWidget(list);
		if (item == "Cards" && !view) {
			vlay->addWidget(new QLabel(tr("Input card pattern to ban:"), this));
			card_to_ban = new QLineEdit(this);
			vlay->addWidget(card_to_ban);
		}
		apage->setLayout(vlay);

		tab->addTab(apage, Sanguosha->translate(item));
	}

	QPushButton *add = new QPushButton(tr("Add ..."));
	QPushButton *remove = new QPushButton(tr("Remove"));
	if (!view) add2nd = new QPushButton(tr("Add 2nd general ..."));
	QPushButton *ok = new QPushButton(tr("OK"));

	connect(ok, SIGNAL(clicked()), this, SLOT(accept()));
	connect(this, SIGNAL(accepted()), this, SLOT(saveAll()));
	connect(remove, SIGNAL(clicked()), this, SLOT(doRemoveButton()));
	connect(add, SIGNAL(clicked()), this, SLOT(doAddButton()));
	if (!view) connect(add2nd, SIGNAL(clicked()), this, SLOT(doAdd2ndButton()));

	QHBoxLayout *hlayout = new QHBoxLayout;
	hlayout->addStretch();
	if (!view) {
		hlayout->addWidget(add2nd);
		add2nd->hide();
		hlayout->addWidget(add);
		hlayout->addWidget(remove);
		list = lists.first();
	}

	hlayout->addWidget(ok);
	layout->addLayout(hlayout);

	setLayout(layout);

	foreach (QListWidget *alist, lists) {
		if (alist->objectName() == "Pairs" || alist->objectName() == "Cards")
			continue;
		alist->setViewMode(QListView::IconMode);
		alist->setDragDropMode(QListView::NoDragDrop);
		alist->setResizeMode(QListView::Adjust);
	}
}

void BanlistDialog::addGeneral(const QString &name)
{
	if (list->objectName() == "Pairs") {
		if (banned_items["Pairs"].contains(name)) return;
		banned_items["Pairs"].append(name);
		QString text = QString(tr("Banned for all: %1")).arg(Sanguosha->translate(name));
		QListWidgetItem *item = new QListWidgetItem(text);
		item->setData(Qt::UserRole, QVariant::fromValue(name));
		list->addItem(item);
	} else if (list->objectName() == "Cards") {
		if (banned_items["Cards"].contains(name)) return;
		banned_items["Cards"].append(name);
		QListWidgetItem *item = new QListWidgetItem(name);
		item->setData(Qt::UserRole, QVariant::fromValue(name));
		list->addItem(item);
	} else {
		foreach (QString general_name, name.split("+")) {
			if (banned_items[list->objectName()].contains(general_name)) continue;
			banned_items[list->objectName()].append(general_name);
			QIcon icon(G_ROOM_SKIN.getGeneralPixmap(general_name, QSanRoomSkin::S_GENERAL_ICON_SIZE_TINY));
			QString text = Sanguosha->translate(general_name);
			QListWidgetItem *item = new QListWidgetItem(icon, text, list);
			item->setSizeHint(QSize(60, 60));
			item->setData(Qt::UserRole, general_name);
		}
	}
}

void BanlistDialog::add2ndGeneral(const QString &name)
{
	foreach (QString general_name, name.split("+")) {
		if (banned_items["Pairs"].contains("+" + general_name)) continue;
		banned_items["Pairs"].append("+" + general_name);
		QString text = QString(tr("Banned for second general: %1")).arg(Sanguosha->translate(general_name));
		QListWidgetItem *item = new QListWidgetItem(text);
		item->setData(Qt::UserRole, QVariant::fromValue(QString("+%1").arg(general_name)));
		list->addItem(item);
	}
}

void BanlistDialog::addPair(const QString &first, const QString &second)
{
	if (banned_items["Pairs"].contains(QString("%1+%2").arg(first, second))
		|| banned_items["Pairs"].contains(QString("%1+%2").arg(second, first))) return;
	banned_items["Pairs"].append(QString("%1+%2").arg(first, second));
	QString trfirst = Sanguosha->translate(first);
	QString trsecond = Sanguosha->translate(second);
	QListWidgetItem *item = new QListWidgetItem(QString("%1 + %2").arg(trfirst, trsecond));
	item->setData(Qt::UserRole, QVariant::fromValue(QString("%1+%2").arg(first, second)));
	list->addItem(item);
}

void BanlistDialog::doAddButton()
{
	if (list->objectName() == "Cards") {
		QString pattern;
		if (card_to_ban) {
			pattern = card_to_ban->text();
			card_to_ban->clear();
		}
		if (!pattern.isEmpty())
			addGeneral(pattern);
	} else {
		FreeChooseDialog *chooser = new FreeChooseDialog("", this,
			(list->objectName() == "Pairs") ? FreeChooseDialog::Pair : FreeChooseDialog::Multi);
		connect(chooser, SIGNAL(general_chosen(QString)), this, SLOT(addGeneral(QString)));
		connect(chooser, SIGNAL(pair_chosen(QString, QString)), this, SLOT(addPair(QString, QString)));
		chooser->exec();
	}
}

void BanlistDialog::doAdd2ndButton()
{
	static FreeChooseDialog *chooser = new FreeChooseDialog("", this, FreeChooseDialog::Multi);
	connect(chooser, SIGNAL(general_chosen(QString)), this, SLOT(add2ndGeneral(QString)));
	chooser->exec();
}

void BanlistDialog::doRemoveButton()
{
	int row = list->currentRow();
	if (row != -1) {
		banned_items[list->objectName()].removeOne(list->item(row)->data(Qt::UserRole).toString());
		delete list->takeItem(row);
	}
}

void BanlistDialog::save()
{
	QSet<QString> banset;

	for (int i = 0; i < list->count(); i++)
		banset << list->item(i)->data(Qt::UserRole).toString();

	QStringList banlist = banset.values();
	Config.setValue(QString("Banlist/%1").arg(ban_list.at(item)), banlist);
}

void BanlistDialog::saveAll()
{
	for (int i = 0; i < lists.length(); i++) {
		switchTo(i);
		save();
	}
	BanPair::loadBanPairs();
}

void ServerDialog::edit1v1Banlist()
{
	static BanlistDialog *dialog = new BanlistDialog(this);
	dialog->exec();
}

QGroupBox *ServerDialog::create1v1Box()
{
	QGroupBox *box = new QGroupBox(tr("1v1 options"));
	box->setEnabled(Config.GameMode.mode_id == "02_1v1");
	box->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	QVBoxLayout *vlayout = new QVBoxLayout;

	QComboBox *officialComboBox = new QComboBox;
	officialComboBox->addItem(tr("Classical"), "Classical");
	officialComboBox->addItem("2013", "2013");
	officialComboBox->addItem(tr("WZZZ"), "WZZZ");

	official_1v1_ComboBox = officialComboBox;

	QString rule = Config.value("1v1/Rule", "2013").toString();
	if (rule == "2013")
		officialComboBox->setCurrentIndex(1);
	else if (rule == "WZZZ")
		officialComboBox->setCurrentIndex(2);

	kof_using_extension_checkbox = new QCheckBox(tr("General extensions"));
	kof_using_extension_checkbox->setChecked(Config.value("1v1/UsingExtension", false).toBool());

	kof_card_extension_checkbox = new QCheckBox(tr("Card extensions"));
	kof_card_extension_checkbox->setChecked(Config.value("1v1/UsingCardExtension", false).toBool());

	vlayout->addLayout(HLay(new QLabel(tr("Rule option")), official_1v1_ComboBox));

	QHBoxLayout *hlayout = new QHBoxLayout;
	hlayout->addWidget(new QLabel(tr("Extension setting")));
	hlayout->addStretch();
	hlayout->addWidget(kof_using_extension_checkbox);
	hlayout->addWidget(kof_card_extension_checkbox);

	vlayout->addLayout(hlayout);
	box->setLayout(vlayout);

	return box;
}

QGroupBox *ServerDialog::create3v3Box()
{
	QGroupBox *box = new QGroupBox(tr("3v3 options"));
	box->setEnabled(Config.GameMode.mode_id == "06_3v3");
	box->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	QVBoxLayout *vlayout = new QVBoxLayout;

	official_3v3_radiobutton = new QRadioButton(tr("Official mode"));

	QComboBox *officialComboBox = new QComboBox;
	officialComboBox->addItem(tr("Classical"), "Classical");
	officialComboBox->addItem("2012", "2012");
	officialComboBox->addItem("2013", "2013");

	official_3v3_ComboBox = officialComboBox;

	QString rule = Config.value("3v3/OfficialRule", "2013").toString();
	if (rule == "2012")
		officialComboBox->setCurrentIndex(1);
	else if (rule == "2013")
		officialComboBox->setCurrentIndex(2);

	QRadioButton *extend = new QRadioButton(tr("Extension mode"));
	QPushButton *extend_edit_button = new QPushButton(tr("General selection ..."));
	extend_edit_button->setEnabled(false);
	connect(extend, SIGNAL(toggled(bool)), extend_edit_button, SLOT(setEnabled(bool)));
	connect(extend_edit_button, SIGNAL(clicked()), this, SLOT(select3v3Generals()));

	exclude_disaster_checkbox = new QCheckBox(tr("Exclude disasters"));
	exclude_disaster_checkbox->setChecked(Config.value("3v3/ExcludeDisasters", true).toBool());

	QComboBox *roleChooseComboBox = new QComboBox;
	roleChooseComboBox->addItem(tr("Normal"), "Normal");
	roleChooseComboBox->addItem(tr("Random"), "Random");
	roleChooseComboBox->addItem(tr("All roles"), "AllRoles");

	role_choose_ComboBox = roleChooseComboBox;

	QString scheme = Config.value("3v3/RoleChoose", "Normal").toString();
	if (scheme == "Random")
		roleChooseComboBox->setCurrentIndex(1);
	else if (scheme == "AllRoles")
		roleChooseComboBox->setCurrentIndex(2);

	vlayout->addLayout(HLay(official_3v3_radiobutton, official_3v3_ComboBox));
	vlayout->addLayout(HLay(extend, extend_edit_button));
	vlayout->addWidget(exclude_disaster_checkbox);
	vlayout->addLayout(HLay(new QLabel(tr("Role choose")), role_choose_ComboBox));
	box->setLayout(vlayout);

	bool using_extension = Config.value("3v3/UsingExtension", false).toBool();
	if (using_extension)
		extend->setChecked(true);
	else
		official_3v3_radiobutton->setChecked(true);

	return box;
}

QGroupBox *ServerDialog::createXModeBox()
{
	QGroupBox *box = new QGroupBox(tr("XMode options"));
	box->setEnabled(Config.GameMode.mode_id == "06_XMode");
	box->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	QComboBox *roleChooseComboBox = new QComboBox;
	roleChooseComboBox->addItem(tr("Normal"), "Normal");
	roleChooseComboBox->addItem(tr("Random"), "Random");
	roleChooseComboBox->addItem(tr("All roles"), "AllRoles");

	role_choose_xmode_ComboBox = roleChooseComboBox;

	QString scheme = Config.value("XMode/RoleChooseX", "Normal").toString();
	if (scheme == "Random")
		roleChooseComboBox->setCurrentIndex(1);
	else if (scheme == "AllRoles")
		roleChooseComboBox->setCurrentIndex(2);

	box->setLayout(HLay(new QLabel(tr("Role choose")), role_choose_xmode_ComboBox));
	return box;
}

QGroupBox *ServerDialog::createGameModeBox()
{
	QGroupBox *mode_box = new QGroupBox(tr("Game mode"));
	mode_group = new QButtonGroup;

	QObjectList item_list;

	// normal modes
	//QString n = "2";
	//QStringList modenames;
	//QRadioButton *button0;
	QMap<QString, GameModeStruct> modes = Sanguosha->getAvailableModes();
	QSet<QString> groupedModes;
	QSet<QString> groupNameSet;
	QMapIterator<QString, GameModeStruct> groupIt(modes);
	while (groupIt.hasNext()) {
		groupIt.next();
		QString groupName = Sanguosha->getModeGroup(groupIt.key());
		if (!groupName.isEmpty())
			groupNameSet.insert(groupName);
	}

	QStringList groupNames = groupNameSet.values();
	groupNames.sort();
	foreach (const QString &groupName, groupNames) {
		QStringList modeIds = Sanguosha->getGroupModes(groupName);
		if (modeIds.isEmpty())
			continue;

		QRadioButton *button = new QRadioButton(groupName);
		QComboBox *combo = new QComboBox;
		int selectedIndex = 0;
		foreach (const QString &modeId, modeIds) {
			if (!modes.contains(modeId))
				continue;
			combo->addItem(Sanguosha->getModeName(modeId), modeId);
			groupedModes.insert(modeId);
			if (modeId == Config.GameMode.mode_id)
				selectedIndex = combo->count() - 1;
		}
		if (combo->count() == 0) {
			delete button;
			delete combo;
			continue;
		}

		combo->setCurrentIndex(selectedIndex);
		button->setObjectName(combo->itemData(selectedIndex).toString());
		combo->setProperty("modeGroupButton", QVariant::fromValue((QObject *)button));
		connect(combo, SIGNAL(currentIndexChanged(int)), this, SLOT(updateModeGroupSelection(int)));
		mode_group->addButton(button);
		item_list << HLay(button, combo);
		if (modeIds.contains(Config.GameMode.mode_id))
			button->setChecked(true);
	}

	QMapIterator<QString, GameModeStruct> itor(modes);
	while (itor.hasNext()) {
		itor.next();
		if (groupedModes.contains(itor.key()))
			continue;

		QRadioButton *button = new QRadioButton(Sanguosha->getModeName(itor.key()));
		button->setObjectName(itor.key());
		mode_group->addButton(button);

		if (itor.key() == "02_1v1") {
			QGroupBox *box = create1v1Box();
			connect(button, SIGNAL(toggled(bool)), box, SLOT(setEnabled(bool)));
			item_list << button << box;
		} else if (itor.key() == "06_3v3") {
			QGroupBox *box = create3v3Box();
			connect(button, SIGNAL(toggled(bool)), box, SLOT(setEnabled(bool)));
			item_list << button << box;
		} else if (itor.key() == "06_XMode") {
			QGroupBox *box = createXModeBox();
			connect(button, SIGNAL(toggled(bool)), box, SLOT(setEnabled(bool)));
			item_list << button << box;
		} else if (itor.key() == "04_boss") {
			boss_mode_button = new QPushButton("自定义驱鬼逐邪");//tr("Custom Boss Mode")
			boss_mode_button->setChecked(itor.key() == Config.GameMode.mode_id);
			connect(boss_mode_button, SIGNAL(clicked()), this, SLOT(doBossModeCustomAssign()));
			item_list << HLay(button, boss_mode_button);/*
		} else if (itor.key() == "08_defense") {
			jiange_ComboBox = new QComboBox;
			for (int i = 0; i < 4; i++) {
				QString js = QString("jiange_seat%1").arg(i);
				jiange_ComboBox->addItem(Sanguosha->translate(js), js);
				if(Config.value("jiange_seat", 0).toInt()==i)
					jiange_ComboBox->setCurrentIndex(i);
			}
			item_list << HLay(button, jiange_ComboBox);*/
		} else {
			item_list << button;/*
			if (itor.key().at(1)==n){
				modenames << itor.key();
				button0 = button;
			}else{
				n = itor.key().at(1);
				item_list << button0;
				prevent_ComboBox = new QComboBox;
				foreach (QString name, modenames) {
					prevent_ComboBox->addItem(itor.value(), name);
				}
				item_list << HLay(button0, prevent_ComboBox);
				modenames.clear();
				button0 = button;
			}*/
		}
		button->setChecked(itor.key() == Config.GameMode.mode_id);
	}

	// add scenario modes
	QRadioButton *scenario_button = new QRadioButton(tr("Scenario mode"));
	scenario_button->setObjectName("scenario");
	mode_group->addButton(scenario_button);

	scenario_ComboBox = new QComboBox;
	QStringList names = Sanguosha->getModScenarioNames();
	foreach (QString name, names) {
		QString scenario_name = Sanguosha->translate(name);
		const Scenario *scenario = Sanguosha->getScenario(name);
		int count = scenario->getPlayerCount();
		QString text = tr("%1 (%2 persons)").arg(scenario_name).arg(count);
		scenario_ComboBox->addItem(text, name);
	}

	if (mode_group->checkedButton() == nullptr) {
		int index = names.indexOf(Config.GameMode.mode_id);
		if (index != -1) {
			scenario_button->setChecked(true);
			scenario_ComboBox->setCurrentIndex(index);
		}
	}

	//mini scenes
	QRadioButton *mini_scenes = new QRadioButton(tr("Mini Scenes"));
	mini_scenes->setObjectName("mini");
	mode_group->addButton(mini_scenes);

	mini_scene_ComboBox = new QComboBox;
	int index = -1, stage = qMin(Sanguosha->getMiniSceneCounts(), Config.value("MiniSceneStage", 1).toInt());

	for (int i = 1; i <= stage; i++) {
		QString name = QString(MiniScene::S_KEY_MINISCENE).arg(i);
		QString scenario_name = Sanguosha->translate(name);
		const Scenario *scenario = Sanguosha->getScenario(name);
		int count = scenario->getPlayerCount();
		QString text = tr("%1 (%2 persons)").arg(scenario_name).arg(count);
		mini_scene_ComboBox->addItem(text, name);

		if (name == Config.GameMode.mode_id) index = i - 1;
	}

	if (index >= 0) {
		mini_scene_ComboBox->setCurrentIndex(index);
		mini_scenes->setChecked(true);
	} else if (Config.GameMode.mode_id == "custom_scenario")
		mini_scenes->setChecked(true);

	mini_scene_button = new QPushButton(tr("Custom Mini Scene"));
	connect(mini_scene_button, SIGNAL(clicked()), this, SLOT(doCustomAssign()));

	mini_scene_button->setEnabled(mode_group->checkedButton() ?
		mode_group->checkedButton()->objectName() == "mini" :
		false);

	item_list << HLay(scenario_button, scenario_ComboBox);
	item_list << HLay(mini_scenes, mini_scene_ComboBox);
	item_list << HLay(mini_scenes, mini_scene_button);

	// ============

	QVBoxLayout *left = new QVBoxLayout;
	QVBoxLayout *middle = new QVBoxLayout;
	QVBoxLayout *right = new QVBoxLayout;

	for (int i = 0; i < item_list.length(); i++) {
		QObject *item = item_list.at(i);

		QVBoxLayout *side = i <= 10 ? left : (i <= 17 ? middle : right); // WARNING: Magic Number

		if (item->isWidgetType()) {
			QWidget *widget = qobject_cast<QWidget *>(item);
			side->addWidget(widget);
		} else {
			QLayout *item_layout = qobject_cast<QLayout *>(item);
			side->addLayout(item_layout);
		}
//         if (i == item_list.length() / 2 - 4)
//             side->addStretch();
	}
	left->addStretch();
	middle->addStretch();
	right->addStretch();

	QHBoxLayout *layout = new QHBoxLayout;
	layout->addLayout(left);
	layout->addLayout(middle);
	layout->addLayout(right);

	mode_box->setLayout(layout);

	return mode_box;
}

QLayout *ServerDialog::createButtonLayout()
{
	QHBoxLayout *button_layout = new QHBoxLayout;
	button_layout->addStretch();

	QPushButton *console_button = new QPushButton(tr("PC Console Start"));
	QPushButton *server_button = new QPushButton(tr("Start Server"));
	QPushButton *cancel_button = new QPushButton(tr("Cancel"));

	button_layout->addWidget(console_button);
	button_layout->addWidget(server_button);
	button_layout->addWidget(cancel_button);

	connect(console_button, SIGNAL(clicked()), this, SLOT(onConsoleButtonClicked()));
	connect(server_button, SIGNAL(clicked()), this, SLOT(onServerButtonClicked()));
	connect(cancel_button, SIGNAL(clicked()), this, SLOT(reject()));

	return button_layout;
}

void ServerDialog::onDetectButtonClicked()
{
	QHostInfo vHostInfo = QHostInfo::fromName(QHostInfo::localHostName());
	foreach (QHostAddress address, vHostInfo.addresses()) {
		if (!address.isNull() && address != QHostAddress::LocalHost
			&& address.protocol() == QAbstractSocket::IPv4Protocol) {
			address_edit->setText(address.toString());
			return;
		}
	}
}

void ServerDialog::onConsoleButtonClicked()
{
	accept_type = -1;
	accept();
}

void ServerDialog::onServerButtonClicked()
{
	accept_type = 1;
	accept();
}

Select3v3GeneralDialog::Select3v3GeneralDialog(QDialog *parent)
	: QDialog(parent)
{
	setWindowTitle(tr("Select generals in extend 3v3 mode"));
	QStringList gs = Config.value("3v3/ExtensionGenerals").toStringList();
	ex_generals = QSet<QString>(gs.begin(), gs.end());
	QVBoxLayout *layout = new QVBoxLayout;
	tab_widget = new QTabWidget;
	fillTabWidget();

	QPushButton *ok_button = new QPushButton(tr("OK"));
	connect(ok_button, SIGNAL(clicked()), this, SLOT(accept()));
	QHBoxLayout *hlayout = new QHBoxLayout;
	hlayout->addStretch();
	hlayout->addWidget(ok_button);

	layout->addWidget(tab_widget);
	layout->addLayout(hlayout);
	setLayout(layout);
	setMinimumWidth(550);

	connect(this, SIGNAL(accepted()), this, SLOT(save3v3Generals()));
}

void Select3v3GeneralDialog::fillTabWidget()
{
    static QList<const Package *> packages = Sanguosha->findChildren<const Package *>();
	foreach (const Package *package, packages) {
		if (package->getType() == Package::GeneralPack) {
			QListWidget *list = new QListWidget;
			list->setViewMode(QListView::IconMode);
			list->setDragDropMode(QListView::NoDragDrop);
			fillListWidget(list, package);

			tab_widget->addTab(list, Sanguosha->translate(package->objectName()));
		}
	}
}

void Select3v3GeneralDialog::fillListWidget(QListWidget *list, const Package *pack)
{
	foreach (const General *general, pack->findChildren<const General *>()) {
		if (Sanguosha->isGeneralHidden(general->objectName())) continue;

		QListWidgetItem *item = new QListWidgetItem(list);
		item->setData(Qt::UserRole, general->objectName());
		item->setIcon(QIcon(G_ROOM_SKIN.getGeneralPixmap(general->objectName(), QSanRoomSkin::S_GENERAL_ICON_SIZE_TINY)));

		bool checked = ex_generals.contains(general->objectName());
		if (ex_generals.isEmpty())
			checked = general->objectName() != "yuji"&&(pack->objectName() == "standard" || pack->objectName() == "wind");

		item->setCheckState(checked?Qt::Checked:Qt::Unchecked);
	}

	QAction *action = new QAction(tr("Check/Uncheck all"), list);
	list->addAction(action);
	list->setContextMenuPolicy(Qt::ActionsContextMenu);
	list->setResizeMode(QListView::Adjust);

	connect(action, SIGNAL(triggered()), this, SLOT(toggleCheck()));
}

void ServerDialog::doCustomAssign()
{
	CustomAssignDialog *dialog = new CustomAssignDialog(this);

	connect(dialog, SIGNAL(scenario_changed()), this, SLOT(setMiniCheckBox()));
	dialog->exec();
}

void ServerDialog::doBossModeCustomAssign()
{
	BossModeCustomAssignDialog *dialog = new BossModeCustomAssignDialog(this);
	dialog->config();
}

void ServerDialog::setMiniCheckBox()
{
	mini_scene_ComboBox->setEnabled(false);
}

void Select3v3GeneralDialog::toggleCheck()
{
	QWidget *widget = tab_widget->currentWidget();
	QListWidget *list = qobject_cast<QListWidget *>(widget);

	if (list == nullptr || list->item(0) == nullptr) return;

	bool checked = list->item(0)->checkState() != Qt::Checked;

	for (int i = 0; i < list->count(); i++)
		list->item(i)->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
}

void Select3v3GeneralDialog::save3v3Generals()
{
	ex_generals.clear();

	for (int i = 0; i < tab_widget->count(); i++) {
		QWidget *widget = tab_widget->widget(i);
		QListWidget *list = qobject_cast<QListWidget *>(widget);
		if (list) {
			for (int j = 0; j < list->count(); j++) {
				QListWidgetItem *item = list->item(j);
				if (item->checkState() == Qt::Checked)
					ex_generals << item->data(Qt::UserRole).toString();
			}
		}
	}

	QStringList list = ex_generals.values();
	QVariant data = QVariant::fromValue(list);
	Config.setValue("3v3/ExtensionGenerals", data);
}

void ServerDialog::select3v3Generals()
{
	static QDialog *dialog = new Select3v3GeneralDialog(this);
	dialog->exec();
}

BossModeCustomAssignDialog::BossModeCustomAssignDialog(QWidget *parent)
	: QDialog(parent)
{
	setWindowTitle("自定义驱鬼逐邪");//tr("Custom boss mode")

	// Difficulty group box
	QGroupBox *box = new QGroupBox(tr("Difficulty options"));
	box->setEnabled(true);
	box->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	QVBoxLayout *boxvlayout = new QVBoxLayout;

	int difficulty = Config.value("BossModeDifficulty", 0).toInt();

	diff_revive_checkBox = new QCheckBox(tr("BMD Revive"));
	diff_revive_checkBox->setToolTip(tr("Tootip of BMD Revive"));
	diff_revive_checkBox->setChecked((difficulty & (1 << GameRule::BMDRevive)) > 0);

	diff_recover_checkBox = new QCheckBox(tr("BMD Recover"));
	diff_recover_checkBox->setToolTip(tr("Tootip of BMD Recover"));
	diff_recover_checkBox->setChecked((difficulty & (1 << GameRule::BMDRecover)) > 0);

	boxvlayout->addLayout(HLay(diff_revive_checkBox, diff_recover_checkBox));

	diff_draw_checkBox = new QCheckBox(tr("BMD Draw"));
	diff_draw_checkBox->setToolTip(tr("Tootip of BMD Draw"));
	diff_draw_checkBox->setChecked((difficulty & (1 << GameRule::BMDDraw)) > 0);

	diff_reward_checkBox = new QCheckBox(tr("BMD Reward"));
	diff_reward_checkBox->setToolTip(tr("Tootip of BMD Reward"));
	diff_reward_checkBox->setChecked((difficulty & (1 << GameRule::BMDReward)) > 0);

	boxvlayout->addLayout(HLay(diff_draw_checkBox, diff_reward_checkBox));

	diff_incMaxHp_checkBox = new QCheckBox(tr("BMD Inc Max HP"));
	diff_incMaxHp_checkBox->setToolTip(tr("Tootip of BMD Inc Max HP"));
	diff_incMaxHp_checkBox->setChecked((difficulty & (1 << GameRule::BMDIncMaxHp)) > 0);

	diff_decMaxHp_checkBox = new QCheckBox(tr("BMD Dec Max HP"));
	diff_decMaxHp_checkBox->setToolTip(tr("Tootip of BMD Dec Max HP"));
	diff_decMaxHp_checkBox->setChecked((difficulty & (1 << GameRule::BMDDecMaxHp)) > 0);

	boxvlayout->addLayout(HLay(diff_incMaxHp_checkBox, diff_decMaxHp_checkBox));

	box->setLayout(boxvlayout);

	// Other settings
	yanluo_checkBox = new QCheckBox("十殿阎罗模式");
	yanluo_checkBox->setToolTip("将四关中出现的Boss改为十殿阎罗，且不受“削弱Boss”影响");
	yanluo_checkBox->setChecked(Config.value("BossYanluo").toBool());

	experience_checkBox = new QCheckBox(tr("Boss Mode Experience Mode"));
	experience_checkBox->setChecked(Config.value("BossModeExp").toBool());

	optional_boss_checkBox = new QCheckBox(tr("Boss Mode Optional Boss"));
	optional_boss_checkBox->setChecked(Config.value("OptionalBoss").toBool());

	endless_checkBox = new QCheckBox(tr("Boss Mode Endless"));
	endless_checkBox->setChecked(Config.value("BossModeEndless").toBool());

	turn_limit_label = new QLabel(tr("Boss Mode Turn Limitation"));
	turn_limit_spinBox = new QSpinBox(this);
	turn_limit_spinBox->setRange(-1, 200);
	turn_limit_spinBox->setValue(Config.value("BossModeTurnLimit", 70).toInt());

	QVBoxLayout *vlayout = new QVBoxLayout;
	vlayout->addWidget(box);
	if (Sanguosha->getPackage("BossYanluo")!=nullptr)
		vlayout->addWidget(yanluo_checkBox);
	vlayout->addWidget(experience_checkBox);
	vlayout->addWidget(optional_boss_checkBox);
	vlayout->addWidget(endless_checkBox);
	vlayout->addLayout(HLay(turn_limit_label, turn_limit_spinBox));

	QPushButton *okButton = new QPushButton(tr("OK"));
	QPushButton *cancelButton = new QPushButton(tr("Cancel"));
	connect(okButton, SIGNAL(clicked()), this, SLOT(accept()));
	connect(cancelButton, SIGNAL(clicked()), this, SLOT(reject()));
	vlayout->addLayout(HLay(okButton, cancelButton));

	setLayout(vlayout);
}

void BossModeCustomAssignDialog::config()
{
	exec();

	if (result() != Accepted)
		return;

	int difficulty = 0;
	if (diff_revive_checkBox->isChecked())
		difficulty |= (1 << GameRule::BMDRevive);
	if (diff_recover_checkBox->isChecked())
		difficulty |= (1 << GameRule::BMDRecover);
	if (diff_draw_checkBox->isChecked())
		difficulty |= (1 << GameRule::BMDDraw);
	if (diff_reward_checkBox->isChecked())
		difficulty |= (1 << GameRule::BMDReward);
	if (diff_incMaxHp_checkBox->isChecked())
		difficulty |= (1 << GameRule::BMDIncMaxHp);
	if (diff_decMaxHp_checkBox->isChecked())
		difficulty |= (1 << GameRule::BMDDecMaxHp);
	Config.setValue("BossModeDifficulty", difficulty);

	Config.setValue("BossYanluo", yanluo_checkBox->isChecked());
	Config.setValue("BossModeExp", experience_checkBox->isChecked());
	Config.setValue("BossModeEndless", endless_checkBox->isChecked());
	Config.setValue("OptionalBoss", optional_boss_checkBox->isChecked());

	Config.setValue("BossModeTurnLimit", turn_limit_spinBox->value());
}

int ServerDialog::config()
{
	exec();

	if (result() != Accepted)
		return 0;

	Config.ServerName = server_name_edit->text();
	Config.OperationTimeout = timeout_spinbox->value();
	Config.OperationNoLimit = nolimit_checkbox->isChecked();
	Config.RandomSeat = random_seat_checkbox->isChecked();
	Config.EnableCheat = enable_cheat_checkbox->isChecked();
	Config.FreeChoose = Config.EnableCheat && free_choose_checkbox->isChecked();
	Config.FreeAssignSelf = Config.EnableCheat && free_assign_self_checkbox->isChecked() && free_assign_checkbox->isEnabled();
	Config.ForbidSIMC = forbid_same_ip_checkbox->isChecked();
	Config.DisableChat = disable_chat_checkbox->isChecked();
	Config.Enable2ndGeneral = second_general_checkbox->isChecked();
	Config.EnableSame = same_checkbox->isChecked();
	Config.EnableBasara = basara_checkbox->isChecked() && basara_checkbox->isEnabled();
	Config.EnableHegemony = hegemony_checkbox->isChecked() && hegemony_checkbox->isEnabled();
	Config.EnableMeleeMode = melee_mode_checkbox->isChecked();
	Config.MaxHpScheme = max_hp_scheme_ComboBox->currentIndex();
	if (Config.MaxHpScheme == 0) {
		Config.Scheme0Subtraction = scheme0_subtraction_spinbox->value();
		Config.PreventAwakenBelow3 = false;
	} else {
		Config.Scheme0Subtraction = 3;
		Config.PreventAwakenBelow3 = prevent_awaken_below3_checkbox->isChecked();
	}
	Config.Address = address_edit->text();
	Config.CountDownSeconds = game_start_spinbox->value();
	Config.NullificationCountDown = nullification_spinbox->value();
	Config.EnableMinimizeDialog = minimize_dialog_checkbox->isChecked();
	Config.EnableAI = ai_enable_checkbox->isChecked();
	Config.OriginAIDelay = ai_delay_spinbox->value();
	Config.AIDelay = Config.OriginAIDelay;
	Config.AIDelayAD = ai_delay_ad_spinbox->value();
	Config.AlterAIDelayAD = ai_delay_altered_checkbox->isChecked();
	Config.ServerPort = port_edit->text().toInt();
	Config.DisableLua = disable_lua_checkbox->isChecked();
	Config.AddGodGeneral = add_god_general->isChecked();
	Config.GeneralVersionDedup = general_version_dedup->isChecked();
	Config.SurrenderAtDeath = surrender_at_death_checkbox->isChecked();

	// game mode
	if (mode_group->checkedButton()) {
		QString objname = mode_group->checkedButton()->objectName();
		if (objname == "scenario")
			Config.GameMode = Sanguosha->getGameMode(scenario_ComboBox->itemData(scenario_ComboBox->currentIndex()).toString());
		else if (objname == "mini") {
			if (mini_scene_ComboBox->isEnabled())
				Config.GameMode = Sanguosha->getGameMode(mini_scene_ComboBox->itemData(mini_scene_ComboBox->currentIndex()).toString());
			else
				Config.GameMode = Sanguosha->getGameMode("custom_scenario");
		} else
			Config.GameMode = Sanguosha->getGameMode(objname);
	}

	Config.setValue("ServerName", Config.ServerName);
	Config.setValue("GameMode", Config.GameMode.mode_id);
	Config.setValue("OperationTimeout", Config.OperationTimeout);
	Config.setValue("OperationNoLimit", Config.OperationNoLimit);
	Config.setValue("RandomSeat", Config.RandomSeat);
	Config.setValue("EnableCheat", Config.EnableCheat);
	Config.setValue("FreeChoose", Config.FreeChoose);
	Config.setValue("FreeAssign", Config.EnableCheat && free_assign_checkbox->isChecked());
	Config.setValue("FreeAssignSelf", Config.FreeAssignSelf);
	Config.setValue("PileSwappingLimitation", pile_swapping_spinbox->value());
	Config.setValue("WithoutLordskill", without_lordskill_checkbox->isChecked());
	Config.setValue("EnableSPConvert", sp_convert_checkbox->isChecked());
	Config.setValue("MaxChoice", maxchoice_spinbox->value());
	Config.setValue("LordMaxChoice", lord_maxchoice_spinbox->value());
	Config.setValue("NonLordMaxChoice", nonlord_maxchoice_spinbox->value());
	Config.setValue("ForbidSIMC", Config.ForbidSIMC);
	Config.setValue("DisableChat", Config.DisableChat);
	Config.setValue("Enable2ndGeneral", Config.Enable2ndGeneral);
	Config.setValue("EnableSame", Config.EnableSame);
	Config.setValue("EnableBasara", Config.EnableBasara);
	Config.setValue("EnableHegemony", Config.EnableHegemony);
	Config.setValue("EnableMeleeMode", Config.EnableMeleeMode);
	Config.setValue("HegemonyMaxChoice", hegemony_maxchoice_spinbox->value());
	Config.setValue("HegemonyMaxShown", hegemony_maxshown_spinbox->value());
	Config.setValue("HegemonyCompanionReward", hegemony_companion->itemData(hegemony_companion->currentIndex()).toString());
	Config.setValue("MaxHpScheme", Config.MaxHpScheme);
	Config.setValue("Scheme0Subtraction", Config.Scheme0Subtraction);
	Config.setValue("PreventAwakenBelow3", Config.PreventAwakenBelow3);
	Config.setValue("CountDownSeconds", game_start_spinbox->value());
	Config.setValue("NullificationCountDown", nullification_spinbox->value());
	Config.setValue("EnableMinimizeDialog", Config.EnableMinimizeDialog);
	Config.setValue("EnableAI", Config.EnableAI);
	Config.setValue("AIChat", ai_chat_checkbox->isChecked());
	Config.setValue("AIHumanized", ai_humanized_checkbox->isChecked());
	Config.setValue("OriginAIDelay", Config.OriginAIDelay);
	Config.setValue("AlterAIDelayAD", ai_delay_altered_checkbox->isChecked());
	Config.setValue("AIDelayAD", Config.AIDelayAD);
	Config.setValue("SurrenderAtDeath", Config.SurrenderAtDeath);
	Config.setValue("LuckCardTimes", luck_card_spinbox->value());
	Config.setValue("ServerPort", Config.ServerPort);
	Config.setValue("Address", Config.Address);
	Config.setValue("DisableLua", disable_lua_checkbox->isChecked());
	Config.setValue("AddGodGeneral", add_god_general->isChecked());
	Config.setValue("GeneralVersionDedup", Config.GeneralVersionDedup);
	Config.setValue("serverconfig/upnp",checkBoxUpnp->isChecked());
	Config.setValue("serverconfig/addtolistserver",checkBoxAddToListServer->isChecked());

	Config.beginGroup("3v3");
	Config.setValue("UsingExtension", !official_3v3_radiobutton->isChecked());
	Config.setValue("RoleChoose", role_choose_ComboBox->itemData(role_choose_ComboBox->currentIndex()).toString());
	Config.setValue("ExcludeDisasters", exclude_disaster_checkbox->isChecked());
	Config.setValue("OfficialRule", official_3v3_ComboBox->itemData(official_3v3_ComboBox->currentIndex()).toString());
	Config.endGroup();

	Config.beginGroup("1v1");
	Config.setValue("Rule", official_1v1_ComboBox->itemData(official_1v1_ComboBox->currentIndex()).toString());
	Config.setValue("UsingExtension", kof_using_extension_checkbox->isChecked());
	Config.setValue("UsingCardExtension", kof_card_extension_checkbox->isChecked());
	Config.endGroup();

	Config.beginGroup("XMode");
	Config.setValue("RoleChooseX", role_choose_xmode_ComboBox->itemData(role_choose_xmode_ComboBox->currentIndex()).toString());
	Config.endGroup();

	Config.EnabledPackages.clear();
	Config.BanPackages.clear();
	foreach (QAbstractButton *checkbox, extension_group->buttons()) {
		if (checkbox->isChecked())
			Config.EnabledPackages << checkbox->objectName();
		else
			Config.BanPackages << checkbox->objectName();
	}
	const QStringList specialPackageAdders =
		Sanguosha->getPackageMap().value(QStringLiteral("g_special_play"));
	foreach (const Package *package, Sanguosha->getPackages()) {
		if ((package->inherits("Scenario")
			 || specialPackageAdders.contains(package->adderName()))
			 && !Config.BanPackages.contains(package->objectName())) {
			Config.BanPackages << package->objectName();
		}
	}
	Config.setValue("EnabledPackages", Config.EnabledPackages);
	Config.setValue("EnabledPackagesMigrationVersion", 2);
	Config.remove("BanPackages");
	Config.sync();

	return accept_type;
}

#endif

#if !defined(QSAN_SERVER_DIALOGS_ONLY)

Server::Server(QObject *parent)
	: QObject(parent)//, created_successfully(true)
{
	m_uptimeTimer.start();
	connect(this, SIGNAL(server_message(QString)), this, SIGNAL(logMessage(QString)));
	server = new NativeServerSocket;
	server->setParent(this);
	playerCount = 0;
	m_nextGameSeedIndex = 0;

	upnpPortMapping=nullptr;
	networkReply=nullptr;
	serverListFirstReg=true;

	//synchronize ServerInfo on the server side to avoid ambiguous usage of Config and ServerInfo
	ServerInfo.parse(Sanguosha->getSetupString());

	created_successfully = createNewRoom()!=nullptr;

	connect(server, SIGNAL(new_connection(ClientSocket *)), this, SLOT(processNewConnection(ClientSocket *)));
}

void Server::broadcast(const QString &msg)
{
	ChatMessagePayload payload;
	payload.speaker = QStringLiteral("server");
	payload.text = msg;
	foreach(Room *room, rooms)
		room->doBroadcastNotify(S_COMMAND_SPEAK, payload.toVariant());
}

ServerStatusSnapshot Server::statusSnapshot() const
{
	ServerStatusSnapshot snapshot;
	snapshot.uptimeMs = m_uptimeTimer.isValid() ? m_uptimeTimer.elapsed() : 0;
	snapshot.bindAddress = server->listeningAddress();
	snapshot.port = server->listeningPort();
	snapshot.gameMode = Config.GameMode.mode_id;

	const QList<RoomStatusSnapshot> roomItems = roomSnapshots();
	snapshot.roomCount = roomItems.size();
	foreach (const RoomStatusSnapshot &room, roomItems) {
		if (room.state == QLatin1String("playing"))
			++snapshot.gamesRunning;
	}

	const QList<PlayerStatusSnapshot> playerItems = playerSnapshots();
	snapshot.playerCount = playerItems.size();
	foreach (const PlayerStatusSnapshot &player, playerItems) {
		if (player.state == QLatin1String("online"))
			++snapshot.onlineCount;
		if (player.state == QLatin1String("robot"))
			++snapshot.robotCount;
	}

	snapshot.aiEnabled = Config.EnableAI;
	snapshot.luaEnabled = !Config.DisableLua;
	return snapshot;
}

QList<RoomStatusSnapshot> Server::roomSnapshots() const
{
	QList<RoomStatusSnapshot> snapshots;
	QSet<Room *> seen;
	auto appendRoom = [this, &snapshots, &seen](Room *room, bool disposing) {
		if (!room || seen.contains(room))
			return;
		seen.insert(room);

		RoomStatusSnapshot snapshot;
		snapshot.id = room->getId();
		snapshot.state = disposing || room->isFinished()
			? QStringLiteral("disposing")
			: (room->isRunning() ? QStringLiteral("playing") : QStringLiteral("waiting"));
		snapshot.gameMode = room->getMode();
		snapshot.playerCount = room->getPlayers().size();
		snapshot.playerCapacity = snapshot.playerCount + room->getLack();
		if (!disposing) {
			const qint64 createdAt = m_roomCreatedAtMs.value(room, m_uptimeTimer.elapsed());
			snapshot.uptimeMs = qMax<qint64>(0, m_uptimeTimer.elapsed() - createdAt);
		}
		snapshots.append(snapshot);
	};

	foreach (Room *room, rooms)
		appendRoom(room, false);
	foreach (const QPointer<Room> &room, m_disposingRooms)
		appendRoom(room.data(), true);

	std::sort(snapshots.begin(), snapshots.end(),
		[](const RoomStatusSnapshot &left, const RoomStatusSnapshot &right) {
			return left.id < right.id;
		});
	return snapshots;
}

QList<PlayerStatusSnapshot> Server::playerSnapshots() const
{
	QList<PlayerStatusSnapshot> snapshots;
	QSet<ServerPlayer *> seen;
	auto appendPlayer = [&snapshots, &seen](ServerPlayer *player) {
		if (!player || player->objectName().isEmpty() || seen.contains(player))
			return;
		seen.insert(player);

		PlayerStatusSnapshot snapshot;
		snapshot.id = player->objectName();
		snapshot.name = player->screenName();
		Room *room = player->getRoom();
		if (room)
			snapshot.roomId = room->getId();
		snapshot.state = player->getState();
		snapshots.append(snapshot);
	};

	foreach (Room *room, rooms) {
		if (!room)
			continue;
		foreach (ServerPlayer *player, room->getPlayers())
			appendPlayer(player);
	}
	for (auto it = players.cbegin(); it != players.cend(); ++it)
		appendPlayer(it.value());

	std::sort(snapshots.begin(), snapshots.end(),
		[](const PlayerStatusSnapshot &left, const PlayerStatusSnapshot &right) {
			return left.id < right.id;
		});
	return snapshots;
}

bool Server::kickPlayer(const QString &id)
{
	ServerPlayer *player = players.value(id, nullptr);
	if (!player) {
		foreach (Room *room, rooms) {
			if (!room)
				continue;
			foreach (ServerPlayer *candidate, room->getPlayers()) {
				if (candidate && candidate->objectName() == id) {
					player = candidate;
					break;
				}
			}
			if (player)
				break;
		}
	}
	if (!player)
		return false;
	player->kick();
	emit server_message(tr("Administrator kicked player %1").arg(id));
	return true;
}

void Server::broadcastAdminMessage(const QString &message)
{
	broadcast(message);
	emit server_message(tr("Administrator broadcast: %1").arg(message));
}

bool Server::listen()
{
	return created_successfully && server->listen();
}

QStringList Server::startupMessages() const
{
    QStringList addresses;
    foreach (const QHostAddress &address, QNetworkInterface::allAddresses()) {
        const QString item = address.toString();
        const quint32 ipv4 = address.toIPv4Address();
        if (!ipv4)
            continue;

        addresses << item;
    }

    addresses.sort();
    QStringList items;
    foreach (const QString &item, addresses) {
        bool lanAddress = item.startsWith("192.168.") || item.startsWith("10.");
        if (!lanAddress && item.startsWith("172.")) {
            const QStringList octets = item.split('.');
            bool ok = false;
            const int secondOctet = octets.value(1).toInt(&ok);
            lanAddress = ok && secondOctet >= 16 && secondOctet < 32;
        }

        if (lanAddress)
            items << tr("Your LAN address: %1, this address is available only for hosts that in the same LAN").arg(item);
        else if (item == "127.0.0.1")
            items << tr("Your loopback address %1, this address is available only for your host").arg(item);
        else if (item.startsWith("5.") || item.startsWith("25."))
            items << tr("Your Hamachi address: %1, the address is available for users that joined the same Hamachi network").arg(item);
        else if (!item.startsWith("169.254."))
            items << tr("Your other address: %1, if this is a public IP, that will be available for all cases").arg(item);
    }

    items << tr("Listening on %1:%2")
        .arg(server->listeningAddress())
        .arg(server->listeningPort());
    items << tr("Game mode is %1").arg(Sanguosha->getModeName(Config.GameMode.mode_id));
    items << tr("Player count is %1").arg(Sanguosha->getPlayerCount(Config.GameMode.mode_id));
    items << (Config.OperationNoLimit ? tr("There is no time limit")
        : tr("Operation timeout is %1 seconds").arg(Config.OperationTimeout));
    items << (Config.EnableCheat ? tr("Cheat is enabled") : tr("Cheat is disabled"));
    if (Config.EnableCheat)
        items << (Config.FreeChoose ? tr("Free choose is enabled") : tr("Free choose is disabled"));

    if (Config.Enable2ndGeneral) {
        QString scheme;
        switch (Config.MaxHpScheme) {
        case 0: scheme = tr("Sum - %1").arg(Config.Scheme0Subtraction); break;
        case 1: scheme = tr("Minimum"); break;
        case 2: scheme = tr("Maximum"); break;
        case 3: scheme = tr("Average"); break;
        }
        items << tr("Secondary general is enabled, max hp scheme is %1").arg(scheme);
    } else {
        items << tr("Seconardary general is disabled");
    }

    items << (Config.EnableSame ? tr("Same Mode is enabled") : tr("Same Mode is disabled"));
    items << (Config.EnableBasara ? tr("Basara Mode is enabled") : tr("Basara Mode is disabled"));
    items << (Config.EnableHegemony ? tr("Hegemony Mode is enabled") : tr("Hegemony Mode is disabled"));
    if (Config.EnableAI)
        items << tr("This server is AI enabled, AI delay is %1 milliseconds").arg(Config.AIDelay);
    else
        items << tr("This server is AI disabled");
    return items;
}

void Server::daemonize()
{
	server->daemonize();
}

Room *Server::createNewRoom()
{
	waitForDisposingRooms();
	const GameSessionConfig sessionConfig = gameSessionConfig(m_nextGameSeedIndex++);
	qInfo().noquote() << "Game Seed:" << QString::number(sessionConfig.seed);
	current = new Room(this, Config.GameMode.mode_id, sessionConfig);
	if (!current->hasLuaRuntime()) {
		delete current;
		return nullptr;
	}
	rooms.insert(current);
	m_roomCreatedAtMs.insert(current, m_uptimeTimer.elapsed());
	Room *createdRoom = current;
	connect(createdRoom, &QObject::destroyed, this,
		[this, createdRoom]() { m_roomCreatedAtMs.remove(createdRoom); });

	connect(createdRoom, &Room::room_message, this,
		[this, createdRoom](const QString &message) {
			if (isHeadlessMode)
				emit roomLogMessage(createdRoom->getId(), message);
			else
				emit server_message(message);
		});
	connect(current, SIGNAL(game_over(QString)), this, SLOT(gameOver()));
	connect(createdRoom, &Room::game_start, this, [this, createdRoom]() {
		emit roomGameStarted(createdRoom->getId(), createdRoom->getMode());
	});
	connect(createdRoom, &Room::game_over, this,
		[this, createdRoom](const QString &winner) {
			emit roomGameOver(createdRoom->getId(), createdRoom->getMode(), winner);
		});

	return current;
}

void Server::processNewConnection(ClientSocket *socket)
{
	if (socket == nullptr)
		return;
	QString addr = socket->peerAddress();

	if (Config.value("BannedIP").toStringList().contains(addr)) {
		socket->disconnectFromHost();
		emit server_message(tr("Forbid the connection of address %1").arg(addr));
		return;
	}

	if (Config.ForbidSIMC) {
		if (addresses.contains(addr)) {
			socket->disconnectFromHost();
			emit server_message(tr("Forbid the connection of address %1").arg(addr));
			return;
		}
		addresses.insert(addr);
	}

	connect(socket, SIGNAL(disconnected()), this, SLOT(cleanup()));
	const quint64 generation = m_nextConnectionGeneration++;
	ServerConnectionContext *context = new ServerConnectionContext(
		socket, generation == 0 ? m_nextConnectionGeneration++ : generation, this);
	m_connectionContexts.insert(socket, context);

	ServerHelloPayload hello;
	hello.gameVersion = Sanguosha->getVersionNumber();
	hello.modName = Sanguosha->getMODName();
	hello.cardCount = Sanguosha->getCardCount();
	QString error;
	if (!context->sendHello(hello, &error)) {
		rejectConnection(context, QStringLiteral("server_hello_failed"), error);
		return;
	}
	playerCount++;

	emit server_message(tr("%1 connected").arg(socket->peerName()));

	connect(socket, SIGNAL(message_got(QByteArray)), this, SLOT(processRequest(QByteArray)));
	socket->timerSignup.start(30000);
}

void Server::processRequest(const QByteArray &request)
{
	ClientSocket *socket = qobject_cast<ClientSocket *>(sender());
	ServerConnectionContext *context = m_connectionContexts.value(socket, nullptr);
	if (socket == nullptr || context == nullptr)
		return;
	SignupRequestPayload signup;
	quint64 requestId = 0;
	QString error;
	if (!context->acceptSignupFrame(request, &signup, &requestId, &error)) {
		rejectConnection(context, QStringLiteral("invalid_signup"), error);
		return;
	}
	socket->disconnect(this, SLOT(processRequest(QByteArray)));
	socket->timerSignup.stop();
	finalizeSignup(context, signup, requestId);
}

void Server::rejectConnection(ServerConnectionContext *context,
	const QString &code, const QString &detail)
{
	if (context == nullptr)
		return;
	ClientSocket *socket = context->socket();
	DiagnosticPayload diagnostic;
	diagnostic.code = code;
	diagnostic.message = detail;
	diagnostic.fatal = true;
	QString ignored;
	context->sendDiagnostic(diagnostic, &ignored);
	context->fail();
	if (socket == nullptr)
		return;
	emit server_message(tr("Protocol V2 connection failed for %1: %2")
		.arg(socket->peerName(), detail));
	socket->timerSignup.stop();
	QPointer<ClientSocket> guarded(socket);
	QTimer::singleShot(0, this, [guarded]() {
		if (guarded)
			guarded->disconnectFromHost();
	});
}

void Server::finalizeSignup(ServerConnectionContext *context,
	const SignupRequestPayload &signup, quint64 requestId)
{
	if (context == nullptr || context->socket() == nullptr)
		return;
	ClientSocket *socket = context->socket();
	auto rejectSignup = [this, context, requestId](const QString &code,
		const QString &message) {
		SignupReplyPayload reply;
		reply.accepted = false;
		reply.errorCode = code;
		reply.message = message;
		QString ignored;
		context->sendSignupReply(reply, requestId, &ignored);
		rejectConnection(context, code, message);
	};

	if (signup.reconnectRequested) {
		bool has = false;
		foreach (QString objname, name2objname.values(signup.screenName)) {
			ServerPlayer *player = players.value(objname,nullptr);
			if (player) {
				has = true;
				QString state = player->getState();
				if (state != "offline" && state != "robot") continue;
				if (player->getRoom()->isFinished()) continue;
				SignupReplyPayload reply;
				reply.accepted = true;
				reply.reconnected = true;
				reply.playerId = player->objectName();
				QString error;
				if (!context->sendSignupReply(reply, requestId, &error)) {
					rejectConnection(context, QStringLiteral("signup_reply_failed"), error);
					return;
				}
				player->adoptProtocolConnectionState(context->releaseProtocolState());
				m_connectionContexts.remove(socket);
				context->deleteLater();
				player->setSocket(socket);
				if (!sendSetup(player, &error)) {
					socket->disconnectFromHost();
					return;
				}
				player->getRoom()->reconnect(player, nullptr);
				return;
			}
		}
		if (has) {
			rejectSignup(QStringLiteral("reconnect_target_unavailable"),
				QStringLiteral("Matching player is not available for reconnect"));
			return;
		}
		rejectSignup(QStringLiteral("reconnect_target_missing"),
			QStringLiteral("No matching player exists for reconnect"));
		return;
	}
	if (name2objname.contains(signup.screenName)) {
		rejectSignup(QStringLiteral("name_in_use"),
			QStringLiteral("Screen name is already in use"));
		return;
	}

	if (!current || current->isFull() || current->isFinished()) {
		if (!createNewRoom()) {
			rejectSignup(QStringLiteral("server_full"),
				QStringLiteral("No room is available"));
			return;
		}
	}

	ServerPlayer *player = current->addSocket(socket);
	SignupReplyPayload reply;
	reply.accepted = true;
	reply.reconnected = false;
	reply.playerId = player->objectName();
	QString error;
	if (!context->sendSignupReply(reply, requestId, &error)) {
		rejectConnection(context, QStringLiteral("signup_reply_failed"), error);
		return;
	}
	player->adoptProtocolConnectionState(context->releaseProtocolState());
	m_connectionContexts.remove(socket);
	context->deleteLater();
	if (!sendSetup(player, &error)) {
		socket->disconnectFromHost();
		return;
	}
	current->signup(player, signup.screenName, signup.avatar, false);
	emit newPlayer(player);
	emit playerJoined(player->objectName(), player->screenName(), current->getId());
}

void Server::cleanup()
{
	ClientSocket *socket = qobject_cast<ClientSocket *>(sender());
	if (ServerConnectionContext *context = m_connectionContexts.take(socket)) {
		context->fail();
		context->deleteLater();
	}
	playerCount--;
	if (Config.ForbidSIMC){
		addresses.remove(socket->peerAddress());
	}
}

void Server::signupPlayer(ServerPlayer *player)
{
	name2objname.insert(player->screenName(), player->objectName());
	players.insert(player->objectName(), player);
}

void Server::gameOver()
{
	Room *room = qobject_cast<Room *>(sender());
	if (!room)
		return;
	rooms.remove(room);

	foreach(ServerPlayer *player, room->findChildren<ServerPlayer *>()) {
		name2objname.remove(player->screenName(), player->objectName());
		players.remove(player->objectName());
    }

	if (current == room)
		current = nullptr;
	room->abortWaitingRequests();
	scheduleDisposeRoom(room);
}

bool Server::disposingRoomStillRunning() const
{
	foreach (const QPointer<Room> &roomPtr, m_disposingRooms) {
		Room *room = roomPtr.data();
		if (!room)
			continue;
		if (room->isRunning())
			return true;
		RoomThread *rt = room->getThread();
		if (rt && rt->isRunning())
			return true;
	}
	return false;
}

void Server::waitForDisposingRooms()
{
	if (!disposingRoomStillRunning())
		return;
	// Room ctor 在 main 同步執行, 期間無法處理 leftover RoomThread 的
	// BlockingQueuedConnection。先泵 queued slot, 等舊 worker 結束再 new Room。
	QElapsedTimer timer;
	timer.start();
	while (disposingRoomStillRunning() && timer.elapsed() < 10000)
		QCoreApplication::processEvents(QEventLoop::ExcludeSocketNotifiers);
	if (disposingRoomStillRunning())
		qWarning("waitForDisposingRooms: leftover RoomThread still running");
}

void Server::scheduleDisposeRoom(Room *room)
{
	if (!room)
		return;
	m_disposingRooms.append(QPointer<Room>(room));
	QPointer<Room> roomPtr(room);
	QTimer::singleShot(500, this, [this, roomPtr]() {
		if (!roomPtr) {
			QList<QPointer<Room> >::iterator it = m_disposingRooms.begin();
			while (it != m_disposingRooms.end()) {
				if (it->isNull())
					it = m_disposingRooms.erase(it);
				else
					++it;
			}
			return;
		}
		RoomThread *rt = roomPtr->getThread();
		const bool workerRunning = roomPtr->isRunning() || (rt && rt->isRunning());
		if (workerRunning) {
			QTimer *pollTimer = new QTimer(this);
			int *elapsedMs = new int(0);
			connect(pollTimer, &QTimer::timeout, this,
				[this, roomPtr, rt, pollTimer, elapsedMs]() {
					*elapsedMs += 100;
					const bool stillRunning = roomPtr
						&& (roomPtr->isRunning() || (rt && rt->isRunning()));
					if (!stillRunning || *elapsedMs >= 10000) {
						pollTimer->stop();
						pollTimer->deleteLater();
						delete elapsedMs;
						m_disposingRooms.removeAll(roomPtr);
						if (roomPtr && !stillRunning)
							roomPtr->deleteLater();
						else if (stillRunning)
							qWarning("scheduleDisposeRoom: timeout, leaking Room");
					}
				});
			pollTimer->start(100);
			return;
		}
		m_disposingRooms.removeAll(roomPtr);
		roomPtr->deleteLater();
	});
}

#endif


#if !defined(QSAN_SERVER_DIALOGS_ONLY)

void Server::checkUpnpAndListServer()
{
	if(Config.value("serverconfig/upnp",true).toBool()) {
		if(upnpPortMapping) upnpPortMapping->deleteLater();
		upnpPortMapping = new QtUpnpPortMapping();
		connect(upnpPortMapping,SIGNAL(finished()),this,SLOT(upnpFinished()));
		upnpPortMapping->addPortMapping(Config.ServerPort,Config.ServerPort,"Sanguosha",true);
		QTimer::singleShot(10000,this,SLOT(upnpTimeout()));
	} else if(Config.value("serverconfig/addtolistserver").toBool())
		addToListServer();
}

void Server::upnpFinished()
{
	disconnect(upnpPortMapping,0,0,0);
	if(Config.value("serverconfig/addtolistserver").toBool())
		addToListServer();
}

void Server::addToListServer()
{
	if(Config.value("OfficialServer").toBool())
		tryTimes=5;
	else
		tryTimes=3;
	sendListServerRequest();
}

void Server::sendListServerRequest()
{
	QString regUrl=Config.value("slconfig/regurl",SERVERLIST_URL_DEFAULTREG).toString();
	regUrl+="?p="+QString::number(Config.ServerPort);
	if(!serverListFirstReg)
		regUrl+="&r=1";
	if(networkReply) networkReply->deleteLater();
	networkReply = networkAccessManager.get(QNetworkRequest(QUrl(regUrl)));
	connect(networkReply,SIGNAL(finished()),this,SLOT(listServerReply()));
}

void Server::upnpTimeout()
{
	if(upnpPortMapping) {
		upnpPortMapping->deleteLater();
		upnpPortMapping = nullptr;
	}
}

void Server::listServerReply()
{
	char buf;
	bool isOK = false, isOfficialServer = Config.value("OfficialServer",false).toBool();
	if(networkReply->bytesAvailable()==1) {
		networkReply->read(&buf,1);
		if(buf == '1') {
			emit server_message("加入列表服务器失败 失败原因：外网无法访问此服务器。");
			if(!isOfficialServer) {
				networkReply->deleteLater();
				networkReply = nullptr;
				return;
			}
		} else if(buf == '0') {
			isOK = true;
			serverListFirstReg = false;
			emit server_message("加入“查找服务器”列表成功！");
		}else
			emit server_message("加入列表服务器失败 失败原因：列表服务器异常。");
	} else
		emit server_message("加入列表服务器失败 失败原因：列表服务器异常。");
	if(!isOK) {
		tryTimes--;
		if(tryTimes>0) {
			emit server_message(QString("重新尝试 剩余次数 %1 次").arg(tryTimes));
			QTimer::singleShot(1000,this,SLOT(sendListServerRequest()));
			return;
		} else
			serverListFirstReg = true;
	}
	int time = 3540000;
	if(isOfficialServer) time = 600000;
	QTimer::singleShot(time,Qt::VeryCoarseTimer,this,SLOT(addToListServer()));
	networkReply->deleteLater();
	networkReply = nullptr;
}

bool Server::isHeadlessMode = false;
int Server::headlessGameLimit = 10000;
QString Server::forcedHeadlessGeneral;
QString Server::forcedHeadlessGeneral2;
bool Server::s_hasGameSeed = false;
quint64 Server::s_gameSeedBase = 0;
static QString s_headlessLogFile;

bool Server::configureGameSeed(const QString &seedText, QString *error)
{
    if (seedText.isEmpty()) {
        if (error) *error = QStringLiteral("--seed requires an unsigned decimal integer");
        return false;
    }
    for (const QChar c : seedText) {
        if (c < QLatin1Char('0') || c > QLatin1Char('9')) {
            if (error) *error = QStringLiteral("Invalid --seed '%1'").arg(seedText);
            return false;
        }
    }
    bool ok = false;
    const quint64 seed = seedText.toULongLong(&ok, 10);
    if (!ok) {
        if (error) *error = QStringLiteral("Invalid --seed '%1'").arg(seedText);
        return false;
    }
    s_gameSeedBase = seed;
    s_hasGameSeed = true;
    qsanSeedRandom(seed);
    QHashSeed::setDeterministicGlobalSeed();
    return true;
}

GameSessionConfig Server::gameSessionConfig(quint64 sessionIndex) const
{
    return s_hasGameSeed ? GameSessionConfig(s_gameSeedBase + sessionIndex)
                         : GameSessionConfig();
}

void Server::setHeadlessLogFile(const QString &path)
{
    s_headlessLogFile = path;
}

void Server::writeHeadlessLog(const QString &msg)
{
    static QFile *logFile = nullptr;
    static QTextStream *logStream = nullptr;

    if (logStream == nullptr) {
        QString filename = s_headlessLogFile;
        if (filename.isEmpty())
            filename = QString("headless_log_%1.txt")
                .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
        logFile = new QFile(filename);
        if (logFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
            logStream = new QTextStream(logFile);
            logStream->setEncoding(QStringConverter::Utf8);
        }
    }

    if (logStream) {
        *logStream << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz")
                    << " " << msg << "\n";
        // 自動化測試: 緩衝寫入, 每 100 行或每 1 秒 flush 一次, 降低儲存裝置寫入次數
        // (逐行 flush 對 HDD/SSD 的寫入放大; 閃退最多丟失最後 ~1 秒的普通 log,
        //  正常結束時 static QFile 析構會 flush, 啟動失敗 exit=1 同)
        // 標記行 (>>> / [AUTOTEST] / ERROR) 立即 flush: headless 完成後退出階段會
        // 0xC0000409 崩潰, 若 done 標記留在緩衝, runner 會誤判遊戲未完成
        static int pending = 0;
        static QElapsedTimer flushTimer;
        static bool timerStarted = false;
        ++pending;
        const bool markerLine = msg.startsWith(">>>") || msg.startsWith("[AUTOTEST]")
            || msg.startsWith("ERROR");
        if (markerLine || pending >= 100 || (timerStarted && flushTimer.elapsed() >= 1000)) {
            logStream->flush();
            logFile->flush();
            pending = 0;
            flushTimer.restart();
            timerStarted = true;
        }
    }
    qDebug().noquote() << msg;
}

void Server::startHeadlessGame()
{
    isHeadlessMode = true;

    static int gameCount = 0;
    const QString mode = Config.GameMode.mode_id;
    const int playerCount = Sanguosha->getPlayerCount(mode);
    const int gameLimit = headlessGameLimit;

    if (gameCount == 0) {
        Server::writeHeadlessLog(QString(">>> Headless stress test started - %1 games, mode %2, %3 players <<<")
            .arg(gameLimit).arg(mode).arg(playerCount));
    }

    gameCount++;
    Server::writeHeadlessLog(QString(">>> Starting headless game %1 <<<").arg(gameCount));

    const GameSessionConfig sessionConfig = gameSessionConfig(quint64(gameCount - 1));
    Server::writeHeadlessLog(QString("[AUTOTEST] Game Seed: %1").arg(sessionConfig.seed));
    Room *room = new Room(this, mode, sessionConfig);
    if (!room->hasLuaRuntime()) {
        delete room;
        Server::writeHeadlessLog(QString("Game %1 FAILED - Lua state is null").arg(gameCount));
        return;
    }

    // 自動化測試: headless 加速 — 跳過 AI 節奏延遲與開局倒數 (to_test 檢查見
    // roomthread.cpp delay() 與 room.cpp run() 的 using_countdown), 並清除
    // AIDelay (1v1/3v3 選將的 msleep(Config.AIDelay) 也會受影響)。
    room->setProperty("to_test", "headless");
    Config.AIDelay = Config.OriginAIDelay = 0;

    QPointer<Room> roomPtr(room);
    int currentGameCount = gameCount;
    connect(room, &Room::game_over, this, [this, roomPtr, currentGameCount, gameLimit](const QString &winner) {
        Server::writeHeadlessLog(QString(">>> Game %1 finished. Winner: %2 <<<").arg(currentGameCount).arg(winner));

        // 局間等待保留 500ms: 實測 0ms 會讓 game N+1 開局 ~9 秒時 fail-fast
        // 閃退 (0xC0000409, 無 minidump) — old room 資源釋放與新局啟動需緩衝。
        // 家族 C 防護: 絕不在局間 blocking wait — RoomThread 收尾可能需要
        // main thread 處理 queued/blocking 事件 (room.cpp BlockingQueuedConnection),
        // blocking 會互等死鎖。改非阻塞輪詢: main thread 每 100ms 檢查
        // RoomThread 是否已結束, 結束後才 deleteLater 並啟動下一局;
        // 逾時 10s 強制推進 (writeHeadlessLog ERROR 標記)。
        QTimer::singleShot(500, this, [this, roomPtr, currentGameCount, gameLimit]() {
            if (roomPtr) {
                RoomThread *rt = roomPtr->getThread();
                if (rt && rt->isRunning()) {
                    QTimer *pollTimer = new QTimer(this);
                    int *elapsedMs = new int(0);
                    connect(pollTimer, &QTimer::timeout, this,
                        [this, roomPtr, rt, pollTimer, elapsedMs, currentGameCount, gameLimit]() {
                            *elapsedMs += 100;
                            const bool finished = !rt->isRunning();
                            if (finished || *elapsedMs >= 10000) {
                                if (!finished) {
                                    Server::writeHeadlessLog("ERROR: RoomThread wait timeout (game "
                                        + QString::number(currentGameCount) + ")");
                                }
                                pollTimer->stop();
                                pollTimer->deleteLater();
                                delete elapsedMs;
                                if (roomPtr) {
                                    roomPtr->deleteLater();
                                }
                                if (currentGameCount < gameLimit) {
                                    startHeadlessGame();
                                } else {
                                    Server::writeHeadlessLog(">>> All games completed. Exiting. <<<");
                                    qApp->quit();
                                }
                            }
                        });
                    pollTimer->start(100);
                    return;
                }
                roomPtr->deleteLater();
            }
            if (currentGameCount < gameLimit) {
                startHeadlessGame();
            } else {
                Server::writeHeadlessLog(">>> All games completed. Exiting. <<<");
                qApp->quit();
            }
        });
    });

    for (int i = 0; i < playerCount; i++) {
        ServerPlayer *player = room->addAIPlayer();
        player->setAI(new TrustAI(player));
        if (i == 0)
            player->setOwner(true);
        room->signup(player, QString("AI_Bot_%1").arg(i), "", true);
    }

    room->start();
}

void Server::startTestGame(const QString &scenarioFile, bool headless)
{
    Q_UNUSED(scenarioFile);

    if (headless) {
        isHeadlessMode = true;
        writeHeadlessLog(">>> Test Scenario Headless Mode Started <<<");
    }

    if (!headless) {
        if (!listen()) {
            qDebug() << "Failed to start server for test game";
            qApp->quit();
            return;
        }
        qDebug() << "Server listening on port" << Config.ServerPort;
    }

    int playerCount = Sanguosha->getTestScenarioPlayerCount();
    if (playerCount <= 0) {
        qDebug() << "Invalid test scenario: no players defined";
        qApp->quit();
        return;
    }

    qDebug() << "Starting test game with" << playerCount << "players";
    if (headless) {
        writeHeadlessLog(QString("Starting test game with %1 players").arg(playerCount));
    }

    const GameSessionConfig sessionConfig = gameSessionConfig(0);
    if (headless)
        writeHeadlessLog(QString("[AUTOTEST] Game Seed: %1").arg(sessionConfig.seed));
    Room *room = new Room(this, "test_scenario", sessionConfig);
    if (!room->hasLuaRuntime()) {
        delete room;
        qDebug() << "Test game FAILED - Lua state is null";
        qApp->quit();
        return;
    }

    if (headless) {
        // 自動化測試: headless 加速, 同 startHeadlessGame
        room->setProperty("to_test", "headless");
        Config.AIDelay = Config.OriginAIDelay = 0;
    }

    QPointer<Room> roomPtr(room);
    connect(room, &Room::game_over, this, [this, roomPtr, headless](const QString &winner) {
        qDebug() << "Test game finished. Winner:" << winner;

        // 局間等待保留 500ms, 同 startHeadlessGame (0ms 會引致次局 fail-fast 閃退)
        // 家族 C 防護, 同 startHeadlessGame: 非阻塞輪詢等 RoomThread 結束再刪 room
        QTimer::singleShot(500, this, [this, roomPtr, headless]() {
            if (roomPtr) {
                RoomThread *rt = roomPtr->getThread();
                if (rt && rt->isRunning()) {
                    QTimer *pollTimer = new QTimer(this);
                    int *elapsedMs = new int(0);
                    connect(pollTimer, &QTimer::timeout, this,
                        [this, roomPtr, rt, pollTimer, elapsedMs, headless]() {
                            *elapsedMs += 100;
                            const bool finished = !rt->isRunning();
                            if (finished || *elapsedMs >= 10000) {
                                pollTimer->stop();
                                pollTimer->deleteLater();
                                delete elapsedMs;
                                if (roomPtr) {
                                    roomPtr->deleteLater();
                                }
                                if (headless) {
                                    qDebug() << "Test completed. Exiting.";
                                    qApp->quit();
                                }
                            }
                        });
                    pollTimer->start(100);
                    return;
                }
                roomPtr->deleteLater();
            }
            if (headless) {
                qDebug() << "Test completed. Exiting.";
                qApp->quit();
            }
        });
    });

    for (int i = 0; i < playerCount; i++) {
        ServerPlayer *player = room->addAIPlayer();
        player->setAI(new TrustAI(player));
        if (i == 0) {
            player->setOwner(true);
        }
        QString screenName = (i == 0 && !headless) ? "Player" : QString("TestBot_%1").arg(i);
        room->signup(player, screenName, "", true);
        if (i == 0 && !headless) {
            name2objname.insert(screenName, player->objectName());
            players.insert(player->objectName(), player);
        }
    }

    room->start();
}

#endif
