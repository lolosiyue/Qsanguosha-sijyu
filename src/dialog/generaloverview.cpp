#include "generaloverview.h"
#include "ui_generaloverview.h"
#include "engine.h"
#include "settings.h"
#include "skin-bank.h"
#include "clientstruct.h"
#include "client.h"
//#include "clientplayer.h"
//#include "package.h"

static QLayout *HLay(QWidget *left, QWidget *right)
{
    QHBoxLayout *layout = new QHBoxLayout;
    layout->addWidget(left);
    layout->addWidget(right);
    return layout;
}

GeneralSearch::GeneralSearch(GeneralOverview *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Search..."));

    QVBoxLayout *layout = new QVBoxLayout;
    layout->addWidget(createInfoTab());
    layout->addLayout(createButtonLayout());
    setLayout(layout);

    connect(this, SIGNAL(search(bool, QString, QString, QStringList, QStringList, int, int, QStringList)),
        parent, SLOT(startSearch(bool, QString, QString, QStringList, QStringList, int, int, QStringList)));
}

QWidget *GeneralSearch::createInfoTab()
{
    QVBoxLayout *layout = new QVBoxLayout;

    include_hidden_checkbox = new QCheckBox;
    include_hidden_checkbox->setText(tr("Include hidden generals"));
    include_hidden_checkbox->setChecked(true);
    layout->addWidget(include_hidden_checkbox);

    nickname_label = new QLabel(tr("Nickname"));
    nickname_label->setToolTip(tr("Input characters included by the nickname. '?' and '*' is available. Every nickname meets the condition if the line is empty."));
    nickname_edit = new QLineEdit;
    nickname_edit->clear();
    layout->addLayout(HLay(nickname_label, nickname_edit));

    name_label = new QLabel(tr("Name"));
    name_label->setToolTip(tr("Input characters included by the name. '?' and '*' is available. Every name meets the condition if the line is empty."));
    name_edit = new QLineEdit;
    name_edit->clear();
    layout->addLayout(HLay(name_label, name_edit));

    maxhp_lower_label = new QLabel(tr("MaxHp Min"));
    maxhp_lower_label->setToolTip(tr("Set lowerlimit and upperlimit of max HP. 0 ~ 0 meets all conditions."));
    maxhp_upper_label = new QLabel(tr("MaxHp Max"));
    maxhp_upper_label->setToolTip(tr("Set lowerlimit and upperlimit of max HP. 0 ~ 0 meets all conditions."));

    maxhp_lower_spinbox = new QSpinBox;
    maxhp_lower_spinbox->setRange(0, 10);
    maxhp_upper_spinbox = new QSpinBox;
    maxhp_upper_spinbox->setRange(0, 10);

    QHBoxLayout *maxhp_hlay = new QHBoxLayout;
    maxhp_hlay->addWidget(maxhp_lower_label);
    maxhp_hlay->addWidget(maxhp_lower_spinbox);
    maxhp_hlay->addWidget(maxhp_upper_label);
    maxhp_hlay->addWidget(maxhp_upper_spinbox);

    layout->addLayout(maxhp_hlay);

    QGroupBox *gender_group = new QGroupBox(tr("Gender"));
    gender_group->setToolTip(tr("Select genders. Every gender meets the condition if none is selected."));
    gender_buttons = new QButtonGroup;
    gender_buttons->setExclusive(false);

    QCheckBox *male = new QCheckBox;
    male->setObjectName("male");
    male->setText(tr("Male"));
    male->setChecked(false);

    QCheckBox *female = new QCheckBox;
    female->setObjectName("female");
    female->setText(tr("Female"));
    female->setChecked(false);

    QCheckBox *neuter = new QCheckBox;
    neuter->setObjectName("neuter");
    neuter->setText(tr("Neuter"));
    neuter->setChecked(false);

    QCheckBox *sexless = new QCheckBox;
    sexless->setObjectName("sexless");
    sexless->setText(tr("Sexless"));
    sexless->setChecked(false);

    QCheckBox *genderless = new QCheckBox;
    genderless->setObjectName("nogender");
    genderless->setText(tr("NoGender"));
    genderless->setChecked(false);

    gender_buttons->addButton(male);
    gender_buttons->addButton(female);
    gender_buttons->addButton(neuter);
    gender_buttons->addButton(sexless);
    gender_buttons->addButton(genderless);

    QGridLayout *gender_layout = new QGridLayout;
    gender_group->setLayout(gender_layout);
    gender_layout->addWidget(male, 0, 1);
    gender_layout->addWidget(female, 0, 2);
    gender_layout->addWidget(neuter, 0, 3);
    gender_layout->addWidget(sexless, 0, 4);
    gender_layout->addWidget(genderless, 0, 5);

    layout->addWidget(gender_group);

    kingdom_buttons = new QButtonGroup;
    kingdom_buttons->setExclusive(false);

    QGroupBox *kingdom_box = new QGroupBox(tr("Kingdoms"));
    kingdom_box->setToolTip(tr("Select kingdoms. Every kingdom meets the condition if none is selected."));

    QGridLayout *kingdom_layout = new QGridLayout;
    kingdom_box->setLayout(kingdom_layout);

    int i = 0;
    foreach (QString kingdom, Sanguosha->getKingdoms()) {
        QCheckBox *checkbox = new QCheckBox;
        checkbox->setObjectName(kingdom);
        checkbox->setIcon(QIcon(QString("image/kingdom/icon/%1.png").arg(kingdom)));
        checkbox->setChecked(false);

        kingdom_buttons->addButton(checkbox);

        int row = i/5, column = i%5;
        kingdom_layout->addWidget(checkbox, row, column + 1);
        i++;
    }
    layout->addWidget(kingdom_box);

    package_buttons = new QButtonGroup;
    package_buttons->setExclusive(false);

    QGroupBox *package_box = new QGroupBox(tr("Packages"));
    package_box->setToolTip(tr("Select packages. Every package meets the condition if none is selected."));

    QVBoxLayout *package_layout = new QVBoxLayout;

    QHBoxLayout *package_button_layout = new QHBoxLayout;
    select_all_button = new QPushButton(tr("Select All"));
    connect(select_all_button, SIGNAL(clicked()), this, SLOT(selectAllPackages()));
    unselect_all_button = new QPushButton(tr("Unselect All"));
    connect(unselect_all_button, SIGNAL(clicked()), this, SLOT(unselectAllPackages()));
    package_button_layout->addWidget(select_all_button);
    package_button_layout->addWidget(unselect_all_button);
    package_button_layout->addStretch();

    QGridLayout *packages_layout = new QGridLayout;

    i = 0;
    foreach (QString extension, Sanguosha->getExtensions()) {
        const Package *package = Sanguosha->findChild<const Package *>(extension);
        if (package->getType() != Package::GeneralPack) continue;
        QCheckBox *checkbox = new QCheckBox;
        checkbox->setObjectName(extension);
        checkbox->setText(Sanguosha->translate(extension));
        checkbox->setChecked(false);

        package_buttons->addButton(checkbox);

        int row = i/5,column = i%5;
        packages_layout->addWidget(checkbox, row, column + 1);
        i++;
    }
    package_layout->addLayout(package_button_layout);
    package_layout->addLayout(packages_layout);
    package_box->setLayout(package_layout);

    QScrollArea *scroll = new QScrollArea;
    scroll->setWidget(package_box);
    layout->addWidget(scroll);
    //layout->addWidget(package_box);

    QWidget *widget = new QWidget;
    widget->setLayout(layout);
    return widget;
}

QLayout *GeneralSearch::createButtonLayout()
{
    QHBoxLayout *button_layout = new QHBoxLayout;

    QPushButton *clear_button = new QPushButton(tr("Clear"));
    QPushButton *ok_button = new QPushButton(tr("OK"));

    button_layout->addWidget(clear_button);
    button_layout->addWidget(ok_button);

    connect(ok_button, SIGNAL(clicked()), this, SLOT(accept()));
    connect(clear_button, SIGNAL(clicked()), this, SLOT(clearAll()));

    return button_layout;
}

void GeneralSearch::accept()
{
    QString nickname = nickname_edit->text();
    QString name = name_edit->text();
    QStringList genders;
    foreach (QAbstractButton *button, gender_buttons->buttons()) {
        if (button->isChecked())
            genders << button->objectName();
    }
    QStringList kingdoms;
    foreach (QAbstractButton *button, kingdom_buttons->buttons()) {
        if (button->isChecked())
            kingdoms << button->objectName();
    }
    int lower = maxhp_lower_spinbox->value();
    int upper = qMax(lower, maxhp_upper_spinbox->value());
    QStringList packages;
    foreach (QAbstractButton *button, package_buttons->buttons()) {
        if (button->isChecked())
            packages << button->objectName();
    }
    emit search(include_hidden_checkbox->isChecked(), nickname, name, genders, kingdoms, lower, upper, packages);
    QDialog::accept();
}

void GeneralSearch::clearAll()
{
    include_hidden_checkbox->setChecked(true);
    nickname_edit->clear();
    name_edit->clear();
    foreach(QAbstractButton *button, gender_buttons->buttons())
        button->setChecked(false);
    foreach(QAbstractButton *button, kingdom_buttons->buttons())
        button->setChecked(false);
    maxhp_lower_spinbox->setValue(0);
    maxhp_upper_spinbox->setValue(0);
    foreach(QAbstractButton *button, package_buttons->buttons())
        button->setChecked(false);
}

void GeneralSearch::selectAllPackages()
{
    foreach(QAbstractButton *button, package_buttons->buttons())
        button->setChecked(true);
}

void GeneralSearch::unselectAllPackages()
{
    foreach(QAbstractButton *button, package_buttons->buttons())
        button->setChecked(false);
}

static GeneralOverview *Overview;

GeneralOverview *GeneralOverview::getInstance(QWidget *main_window)
{
    if (Overview == nullptr)
        Overview = new GeneralOverview(main_window);
	else{
		//Overview->hide();
#ifdef ANDROID
		delete Overview;
        Overview = new GeneralOverview(main_window);
#endif // ANDROID
	}

    return Overview;
}

GeneralOverview::GeneralOverview(QWidget *parent)
    : QDialog(parent), ui(new Ui::GeneralOverview), m_previewMode(false)
{
	ui->setupUi(this);
    origin_window_title = windowTitle();

    button_layout = new QVBoxLayout;

    QGroupBox *group_box = new QGroupBox;
    group_box->setTitle(tr("Effects"));
    group_box->setLayout(button_layout);
    ui->scrollArea->setWidget(group_box);
    ui->skillTextEdit->setProperty("description", true);
	connect(ui->changeGeneralButton, SIGNAL(clicked()), this, SLOT(askTransfiguration()));
	connect(ui->changeGeneral2Button, SIGNAL(clicked()), this, SLOT(askTransfiguration()));
	ui->changeGeneralButton->hide();
	ui->changeGeneral2Button->hide();
    connect(ui->changeHeroSkinButton, SIGNAL(clicked()), this, SLOT(askChangeSkin()));
	ui->banGeneral->hide();
	//ui->untieGeneral->hide();
    connect(ui->banGeneral, SIGNAL(clicked()), this, SLOT(banGeneral()));
    connect(ui->untieGeneral, SIGNAL(clicked()), this, SLOT(untieGeneral()));

    general_search = new GeneralSearch(this);
    connect(ui->searchButton, SIGNAL(clicked()), general_search, SLOT(show()));
    ui->returnButton->hide();
    connect(ui->returnButton, SIGNAL(clicked()), this, SLOT(fillAllGenerals()));
}

void GeneralOverview::setPreviewMode(bool preview)
{
    m_previewMode = preview;
    if (preview) {
        ui->changeGeneralButton->hide();
        ui->changeGeneral2Button->hide();
        ui->changeHeroSkinButton->hide();
        ui->banGeneral->hide();
        ui->untieGeneral->hide();
        ui->searchButton->hide(); // 把搜尋按鈕也隱藏起來，保持介面乾淨
    }
}

void GeneralOverview::fillGenerals(const QList<const General *> &generals, bool init)
{
    QList<const General *> copy_generals;
    foreach (const General *general, generals) {
        if (general->isTotallyHidden()) continue;
		copy_generals.append(general);
    }
    if (init) {
        ui->returnButton->hide();
        setWindowTitle(origin_window_title);
        all_generals = copy_generals;
    }

    ui->tableWidget->clearContents();
    ui->tableWidget->setRowCount(copy_generals.length());
    ui->tableWidget->setIconSize(QSize(20, 20));
    static QIcon lord_icon("image/system/roles/lord.png");

	static QStringList LuaPackages = Config.value("LuaPackages").toString().split("+");

    for (int i = 0; i < copy_generals.length(); i++) {
        const General *general = copy_generals[i];
        QString name, kingdom, gender, max_hp, package;

        name = Sanguosha->translate(general->objectName());
        foreach (QString kin, general->getKingdoms().split("+"))
            kingdom.append(Sanguosha->translate(kin)).append("/");
        if (kingdom.endsWith("/"))
            kingdom.chop(1);
        if (general->isMale())
            gender = tr("Male");
        else if (general->isFemale())
            gender = tr("Female");
        else if (general->isNeuter())
            gender = tr("Neuter");
        else if (general->isSexless())
            gender = tr("Sexless");
        else
            gender = tr("NoGender");
        int maxhp = general->getMaxHp();
        int start_hp = qMin(general->getStartHp(),maxhp);
        if (start_hp != maxhp) max_hp = QString("%1/%2").arg(start_hp).arg(maxhp);
        else max_hp = QString::number(maxhp);
        package = Sanguosha->translate(general->getPackage());

        QString nickname = Sanguosha->translate("#"+general->objectName());
        if (nickname.contains("_")) nickname = Sanguosha->translate("#"+nickname.split("_").last());
        if (nickname.contains("#")) nickname = "";
        QTableWidgetItem *g_item = new QTableWidgetItem(nickname);
        g_item->setData(Qt::UserRole, general->objectName());
        g_item->setTextAlignment(Qt::AlignCenter);

        if (Sanguosha->isGeneralHidden(general->objectName())) {
            g_item->setBackground(Qt::gray);
            g_item->setToolTip(tr("This general is hidden"));
        }
        ui->tableWidget->setItem(i, 0, g_item);

        g_item = new QTableWidgetItem(name);
        g_item->setTextAlignment(Qt::AlignCenter);
        g_item->setData(Qt::UserRole, general->objectName());
        if (general->isLord()) {
            g_item->setIcon(lord_icon);
            g_item->setTextAlignment(Qt::AlignCenter);
        }

        if (Sanguosha->isGeneralHidden(general->objectName())) {
            g_item->setBackground(Qt::gray);
            g_item->setToolTip(tr("This general is hidden"));
        }
        ui->tableWidget->setItem(i, 1, g_item);

        g_item = new QTableWidgetItem(kingdom);
        g_item->setTextAlignment(Qt::AlignCenter);
        ui->tableWidget->setItem(i, 2, g_item);

        g_item = new QTableWidgetItem(gender);
        g_item->setTextAlignment(Qt::AlignCenter);
        ui->tableWidget->setItem(i, 3, g_item);

        g_item = new QTableWidgetItem(max_hp);
        g_item->setTextAlignment(Qt::AlignCenter);
        ui->tableWidget->setItem(i, 4, g_item);

        /*name = general->getSubPackage();
		if(!name.isEmpty()) package.append("["+Sanguosha->translate(name)+"]");*/
		g_item = new QTableWidgetItem(package);
        g_item->setTextAlignment(Qt::AlignCenter);
        if (LuaPackages.contains(general->getPackage())) {
            g_item->setBackground(QColor(0x66, 0xCC, 0xFF));
            g_item->setToolTip(tr("This is an Lua extension"));
        }
        ui->tableWidget->setItem(i, 5, g_item);
    }

    ui->tableWidget->setColumnWidth(0, 80);
    ui->tableWidget->setColumnWidth(1, 88);
    ui->tableWidget->setColumnWidth(2, 40);
    ui->tableWidget->setColumnWidth(3, 40);
    ui->tableWidget->setColumnWidth(4, 40);
    ui->tableWidget->setColumnWidth(5, 111);

    if (!copy_generals.isEmpty())
        ui->tableWidget->setCurrentItem(ui->tableWidget->item(0, 0));
    else {
        ui->generalPhoto->setPixmap(QPixmap());
        ui->skillTextEdit->clear();
        resetButtons();
    }
}

void GeneralOverview::resetButtons()
{
    QLayoutItem *child;
    while ((child = button_layout->takeAt(0))) {
        QWidget *widget = child->widget();
        if (widget) delete widget;
    }
}

GeneralOverview::~GeneralOverview()
{
    delete button_layout;
    delete general_search;
    delete ui;
}

bool GeneralOverview::hasSkin(const QString &general_name)
{
    int skin_index = Config.value("HeroSkin/"+general_name, 0).toInt();
    if (skin_index<1) {
        Config.beginGroup("HeroSkin");
        Config.setValue(general_name, 1);
        Config.endGroup();
        QPixmap pixmap = G_ROOM_SKIN.getCardMainPixmap(general_name);
        Config.beginGroup("HeroSkin");
        Config.remove(general_name);
        Config.endGroup();
        return pixmap.width()>1 || pixmap.height()>1;
    }
    return true;
}

QString GeneralOverview::getIllustratorInfo(const QString &general_name)
{
    int skin_index = Config.value("HeroSkin/"+general_name, 0).toInt();
	if(skin_index > 0){
		QString suffix = Sanguosha->translate(QString("illustrator:%1_%2").arg(general_name).arg(skin_index));
		if (!suffix.startsWith("illustrator:")) return suffix;
	}
	QString text = Sanguosha->translate("illustrator:" + general_name);
	if (!text.startsWith("illustrator:")) return text;
	return Sanguosha->translate("DefaultIllustrator");
}

void GeneralOverview::addLines(const Skill *skill)
{
    QStringList sources = skill->getSources();

    int skin_index = 0;
	bool has_files = false;

    int row = ui->tableWidget->currentRow();
    QString general_name = ui->tableWidget->item(row, 0)->data(Qt::UserRole).toString();
    if (Sanguosha->getGeneral(general_name)) {
        skin_index = Config.value("HeroSkin/"+general_name, 0).toInt();
        if (skin_index > 0) {
            QString heroskin = QString("image/heroskin/audio/%1_%2/skill").arg(general_name).arg(skin_index);
            if (QFile::exists(heroskin)) {
                QStringList oggs, files;
                oggs << "*.ogg";
                QDir dir(heroskin);
                foreach (QString file, dir.entryList(oggs, QDir::Files|QDir::Readable, QDir::Name)) {
					if (file.startsWith(skill->objectName()) && file.endsWith(".ogg"))
                        files << file;
                }
                if (files.length()>0){
					has_files = true;
                    sources = files;
				}
            }
        }
    }
    QString skill_name = Sanguosha->translate(skill->objectName());

    if (sources.isEmpty()) {
		bool has = false;
		for (int i = 1; i < 99; i++) {
			QString skill_line = Sanguosha->translate(QString("$%1%2").arg(skill->objectName()).arg(i));
			if (skill_line.startsWith("$")) break;
			QCommandLinkButton *button = new QCommandLinkButton(skill_name+QString(" (%1)").arg(i));
			button->setEnabled(false);
			button_layout->addWidget(button);
			button->setDescription(skill_line);
			has = true;
		}
		if(!has){
			QCommandLinkButton *button = new QCommandLinkButton(skill_name);
			button->setEnabled(false);
			button_layout->addWidget(button);
			QString skill_line = Sanguosha->translate("$"+skill->objectName());
			if (!skill_line.startsWith("$")) button->setDescription(skill_line);
		}
    } else {
        if (has_files) {
            for (int i = 0; i < sources.length(); i++) {
                QString source = sources[i];
                source.chop(4);
                QString filename = QString("%1-%2_%3").arg(source).arg(general_name).arg(skin_index);

                QString button_text = skill_name;
                if (sources.length()>1) button_text.append(QString(" (%1)").arg(i+1));

                QCommandLinkButton *button = new QCommandLinkButton(button_text);
                button->setObjectName(QString("image/heroskin/audio/%1_%2/skill/%3.ogg").arg(general_name).arg(skin_index).arg(source));
                button_layout->addWidget(button);

                QString skill_line = Sanguosha->translate("$" + filename);
                if (skill_line == "$" + filename) skill_line = tr("Translation missing.");

                button->setDescription(skill_line);
                connect(button, SIGNAL(clicked()), this, SLOT(playAudioEffect()));
                addCopyAction(button);
            }
        } else {
            static QRegExp rx(".+/(\\w+\\d?).ogg");
            for (int i = 0; i < sources.length(); i++) {
                if (!rx.exactMatch(sources[i]))
                    continue;

                QString button_text = skill_name;
                if (sources.length()>1) button_text.append(QString(" (%1)").arg(i + 1));

                QCommandLinkButton *button = new QCommandLinkButton(button_text);
                button->setObjectName(sources[i]);
                button_layout->addWidget(button);

                QString filename = rx.capturedTexts().at(1);
                QString skill_line = Sanguosha->translate("$" + filename);
                if (skill_line == "$" + filename) skill_line = tr("Translation missing.");

                button->setDescription(skill_line);
                connect(button, SIGNAL(clicked()), this, SLOT(playAudioEffect()));
                addCopyAction(button);
            }
        }
    }
}

void GeneralOverview::addCopyAction(QCommandLinkButton *button)
{
    QAction *action = new QAction(button);
    action->setData(button->description());
    button->addAction(action);
    action->setText(tr("Copy lines"));
    button->setContextMenuPolicy(Qt::ActionsContextMenu);

    connect(action, SIGNAL(triggered()), this, SLOT(copyLines()));
}

void GeneralOverview::copyLines()
{
    QAction *action = qobject_cast<QAction *>(sender());
    if (action) {
        QClipboard *clipboard = QApplication::clipboard();
        clipboard->setText(action->data().toString());
    }
}

void GeneralOverview::on_tableWidget_itemSelectionChanged()
{
	int row = ui->tableWidget->currentRow();
    QTableWidgetItem *item = ui->tableWidget->item(row, 0);
    if (row < 0 || item == nullptr)
        return;

    QString general_name = item->data(Qt::UserRole).toString();
    if (general_name.isEmpty())
        return;

	ui->generalPhoto->setPixmap(G_ROOM_SKIN.getCardMainPixmap(general_name));

	// 【修改區塊 1】如果是預覽模式，強制隱藏；否則正常判斷
    if (m_previewMode) {
        ui->changeHeroSkinButton->hide();
        ui->banGeneral->hide();
        ui->untieGeneral->hide();
    } else {
        ui->changeHeroSkinButton->setVisible(hasSkin(general_name));
        ui->banGeneral->setVisible(!Config.value("Banlist/Roles").toStringList().contains(general_name));
        ui->untieGeneral->setVisible(Config.value("Banlist/Roles").toStringList().contains(general_name));
    }

	const General *general = Sanguosha->getGeneral(general_name);
    if (general == nullptr)
        return;

	QList<const Skill *> skills = general->getVisibleSkillList();

	foreach (const Skill *skill, skills) {
		if(skill->getWakedSkills().isEmpty()) continue;
		foreach (QString skn, skill->getWakedSkills().split(",")) {
			const Skill *sk = Sanguosha->getSkill(skn);
			if (sk && sk->isVisible() && !skills.contains(sk)) skills << sk;
		}
	}

	foreach (QString skn, general->getRelatedSkillNames()) {
		const Skill *skill = Sanguosha->getSkill(skn);
		if (skill && skill->isVisible() && !skills.contains(skill)) skills << skill;
	}

	ui->skillTextEdit->clear();

	resetButtons();

	foreach(const Skill *skill, skills)
		addLines(skill);

	QString oggtxt = "audio/death/"+general_name+".ogg";
	QString last_word = Sanguosha->translate("~" + general_name);
	int skin_index = Config.value("HeroSkin/"+general_name, 0).toInt();
	QString actualGn = Sanguosha->getResourceAlias("heroskin", general_name);
	if (skin_index > 0) {
		QString hero_skin = Sanguosha->translate(QString("~%1-%2_%3").arg(general_name).arg(general_name).arg(skin_index));
		if (!hero_skin.startsWith("~")){
			oggtxt = QString("image/heroskin/audio/%1_%2/death/%3.ogg").arg(actualGn).arg(skin_index).arg(actualGn);
			last_word = hero_skin;
		}
	}
	if (last_word.startsWith("~") && general_name.contains("_")) {
		QString new_general_name = general_name.split("_").last();
		oggtxt = "audio/death/"+new_general_name+".ogg";
		last_word = Sanguosha->translate("~" + new_general_name);
		skin_index = Config.value("HeroSkin/"+new_general_name, 0).toInt();
		QString actualNewGn = Sanguosha->getResourceAlias("heroskin", new_general_name);
		if (skin_index > 0) {
			QString hero_skin = Sanguosha->translate(QString("~%1-%2_%3").arg(new_general_name).arg(new_general_name).arg(skin_index));
			if (!hero_skin.startsWith("~")){
				oggtxt = QString("image/heroskin/audio/%1_%2/death/%3.ogg").arg(actualNewGn).arg(skin_index).arg(actualNewGn);
				last_word = hero_skin;
			}
		}
	}

	if(last_word.startsWith("~")) last_word = "";
	else if(last_word == " ") last_word = tr("Translation missing.");
	QCommandLinkButton *button = new QCommandLinkButton(tr("Death"), last_word);
	button_layout->addWidget(button);

	button->setObjectName(oggtxt);
	button->setEnabled(QFile::exists(oggtxt));
	connect(button, SIGNAL(clicked()), this, SLOT(playAudioEffect()));

	addCopyAction(button);
	oggtxt = "audio/win/"+general_name+".ogg";
	if (QFile::exists(oggtxt)){
		button = new QCommandLinkButton(tr("Victory"), Sanguosha->translate("$" + general_name));

		button_layout->addWidget(button);

		button->setObjectName(oggtxt);
		connect(button, SIGNAL(clicked()), this, SLOT(playAudioEffect()));
		addCopyAction(button);
	} else if (general_name.contains("caocao")) {
		button = new QCommandLinkButton(tr("Victory"),
			tr("Six dragons lead my chariot, "
			"I will ride the wind with the greatest speed."
			"With all of the feudal lords under my command,"
			"to rule the world with one name!"));

		button_layout->addWidget(button);
		addCopyAction(button);

		button->setObjectName("audio/win/caocao.ogg");
		connect(button, SIGNAL(clicked()), this, SLOT(playAudioEffect()));
	}

	if (general_name == "shenlvbu1" || general_name == "shenlvbu2" || general_name == "shenlvbu3") {
		button = new QCommandLinkButton(tr("Stage Change"),tr("Trashes, the real fun is just beginning!"));

		button_layout->addWidget(button);
		addCopyAction(button);

		button->setObjectName("audio/system/stagechange.ogg");
		connect(button, SIGNAL(clicked()), this, SLOT(playAudioEffect()));
	}

	QString designer_text = Sanguosha->translate("designer:" + general_name);
	if (designer_text.contains("designer:"))
		ui->designerLineEdit->setText(tr("Official"));
	else
		ui->designerLineEdit->setText(designer_text);

	QString cv_text = Sanguosha->translate("cv:" + general_name);
	if (cv_text.contains("cv:"))
		cv_text = Sanguosha->translate("cv:" + general_name.split("_").last());
	if (cv_text.contains("cv:"))
		ui->cvLineEdit->setText(tr("Official"));
	else
		ui->cvLineEdit->setText(cv_text);

	ui->illustratorLineEdit->setText(getIllustratorInfo(general_name));

	button_layout->addStretch();
	ui->skillTextEdit->append(general->getSkillDescription(true));
	if (!m_previewMode && ServerInfo.DuringGame && ServerInfo.EnableCheat) {
        ui->changeGeneralButton->show();
        ui->changeGeneral2Button->show();
        ui->changeGeneralButton->setEnabled(Self && Self->getGeneralName() != general_name);
        ui->changeGeneral2Button->setEnabled(Self && Self->getGeneral2Name() != general_name);
    } else {
        ui->changeGeneralButton->hide();
        ui->changeGeneral2Button->hide();
    }
}

void GeneralOverview::playAudioEffect()
{
    QObject *button = sender();
    if (button) Sanguosha->playAudioEffect(button->objectName(), false);
}

void GeneralOverview::askTransfiguration()
{
    if (ServerInfo.EnableCheat && ServerInfo.DuringGame && Self) {
		QPushButton *button = qobject_cast<QPushButton *>(sender());
		bool isSecondaryHero = (button && button->objectName() == ui->changeGeneral2Button->objectName());
        if (isSecondaryHero) ui->changeGeneral2Button->setEnabled(false);
        else ui->changeGeneralButton->setEnabled(false);
        int row = ui->tableWidget->currentRow();
        QString general_name = ui->tableWidget->item(row, 0)->data(Qt::UserRole).toString();
        ClientInstance->requestCheatChangeGeneral(general_name, isSecondaryHero);
    }
}

void GeneralOverview::on_tableWidget_itemDoubleClicked(QTableWidgetItem *)
{
    askTransfiguration();
}

void GeneralOverview::askChangeSkin()
{
    int row = ui->tableWidget->currentRow();
    QString general_name = ui->tableWidget->item(row, 0)->data(Qt::UserRole).toString();

    int n = Config.value("HeroSkin/"+general_name, 0).toInt();
    n++;
    Config.beginGroup("HeroSkin");
    Config.setValue(general_name, n);
    Config.endGroup();
    QPixmap pixmap = G_ROOM_SKIN.getCardMainPixmap(general_name);
    if (pixmap.width() <= 1 && pixmap.height() <= 1) {
        Config.beginGroup("HeroSkin");
        Config.remove(general_name);
        Config.endGroup();
        if (n > 1)
            pixmap = G_ROOM_SKIN.getCardMainPixmap(general_name);
        else
            return;
    }
    ui->generalPhoto->setPixmap(pixmap);
    ui->illustratorLineEdit->setText(getIllustratorInfo(general_name));

    on_tableWidget_itemSelectionChanged();
}

void GeneralOverview::banGeneral()
{
	ui->banGeneral->hide();
	ui->untieGeneral->show();
    int row = ui->tableWidget->currentRow();
    QString general_name = ui->tableWidget->item(row, 0)->data(Qt::UserRole).toString();
	QStringList Roles = Config.value("Banlist/Roles").toStringList();
	Roles << general_name;
    Config.setValue("Banlist/Roles", Roles);
}

void GeneralOverview::untieGeneral()
{
	ui->untieGeneral->hide();
	ui->banGeneral->show();
    int row = ui->tableWidget->currentRow();
    QString general_name = ui->tableWidget->item(row, 0)->data(Qt::UserRole).toString();
	QStringList Roles = Config.value("Banlist/Roles").toStringList();
	Roles.removeAll(general_name);
    Config.setValue("Banlist/Roles", Roles);
}

void GeneralOverview::startSearch(bool include_hidden, const QString &nickname, const QString &name, const QStringList &genders,
    const QStringList &kingdoms, int lower, int upper, const QStringList &packages)
{
    QList<const General *> generals;
    foreach (const General *general, all_generals) {
        if (!include_hidden && general->isTotallyHidden())
            continue;
        QString general_name = general->objectName();
        if (!nickname.isEmpty()) {
            QString v_nickname = nickname;
            v_nickname.replace("?", ".");
            v_nickname.replace("*", ".*");
            QRegExp rx(v_nickname);

            QString g_nickname = Sanguosha->translate("#" + general_name);
            if (g_nickname.startsWith("#"))
                g_nickname = Sanguosha->translate("#" + general_name.split("_").last());
            if (!rx.exactMatch(g_nickname))
                continue;
        }
        if (!name.isEmpty()) {
            QString v_name = name;
            v_name.replace("?", ".");
            v_name.replace("*", ".*");
            //QRegExp rx(v_name);

            QString g_name = Sanguosha->translate(general_name);
            //if (!rx.exactMatch(g_name))
                //continue;
            if (!g_name.contains(v_name) && !general_name.contains(v_name))
                continue;
        }
        if (!genders.isEmpty()) {
            if (general->isMale() && !genders.contains("male"))
                continue;
            if (general->isFemale() && !genders.contains("female"))
                continue;
            if (general->isNeuter() && !genders.contains("neuter") && !genders.contains("nogender"))
                continue;
            if (general->isSexless() && !genders.contains("sexless") && !genders.contains("nogender"))
                continue;
        }
        if (!kingdoms.isEmpty() && !kingdoms.contains(general->getKingdom()))
            continue;
        if (!(lower == 0 && upper == 0) && (general->getMaxHp() < lower || general->getMaxHp() > upper))
            continue;
        if (!packages.isEmpty() && !packages.contains(general->getPackage()))
            continue;
        generals << general;
    }
    if (generals.isEmpty())
        QMessageBox::warning(this, tr("Warning"), tr("No generals are found"));
    else {
        ui->returnButton->show();
        if (windowTitle() == origin_window_title)
            setWindowTitle(windowTitle() + " " + tr("Search..."));
        fillGenerals(generals, false);
    }
}

void GeneralOverview::fillAllGenerals()
{
    ui->returnButton->hide();
    setWindowTitle(origin_window_title);
    fillGenerals(all_generals, false);
}

