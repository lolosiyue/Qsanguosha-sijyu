#include "customassigndialog.h"
#include "runtime-paths.h"
#include "miniscenarios.h"
#include "skin-bank.h"
#include "settings.h"
#include "engine.h"
#include "oracle_helper.h"
#include "standard.h"
#include <QSaveFile>
#include <QSignalBlocker>
#include <QRegularExpression>

static QLayout *HLay(QWidget *left, QWidget *right, QWidget *mid = nullptr,
    QWidget *rear = nullptr, bool is_vertically = false)
{
    QBoxLayout *layout;
    if (is_vertically) layout = new QVBoxLayout;
    else layout = new QHBoxLayout;

    layout->addWidget(left);
    if (mid)
        layout->addWidget(mid);
    layout->addWidget(right);
    if (rear) layout->addWidget(rear);

    return layout;
}

static bool sceneEquipmentFits(const QList<int> &cards, const QMap<QString, QString> &fields)
{
    QMap<int, int> capacity, occupied;
    for (const QString &entry : fields.value("equipArea").split(',', Qt::SkipEmptyParts)) {
        const QStringList pair = entry.split('*');
        if (pair.size() == 2)
            capacity[pair.first().toInt()] = pair.last().toInt();
    }
    for (int id : cards) {
        const Card *card = id >= 0 && id < Sanguosha->getCardCount() ? Sanguosha->getEngineCard(id) : nullptr;
        const EquipCard *equip = card ? qobject_cast<const EquipCard *>(card->getRealCard()) : nullptr;
        if (!equip)
            return false;
        // Multi-slot equipment consumes every location declared by the card.
        for (int slot : equip->getOccupyLocations()) {
            if (++occupied[slot] > capacity.value(slot, 1))
                return false;
        }
    }
    return true;
}

CustomAssignDialog::CustomAssignDialog(QWidget *parent)
    : QDialog(parent),
    choose_general2(false),
    is_ended_by_pile(false), is_single_turn(false), is_before_next(false)
{
    setWindowTitle(tr("Custom mini scene"));

    resize(1080, 720);
    setSizeGripEnabled(true);

    list = new QListWidget;
    list->setFlow(QListView::TopToBottom);
    list->setMovement(QListView::Static);

    QVBoxLayout *vlayout = new QVBoxLayout, *vlayout2 = new QVBoxLayout;
    num_ComboBox = new QComboBox;
    for (int i = 0; i <= 9; i++) {
        if (i < 9)
            num_ComboBox->addItem(tr("%1 persons").arg(QString::number(i + 2)), i + 2);

        QString player = (i == 0 ? "Player" : "AI");
        QString text = (i == 0 ? QString("%1[%2]").arg(Sanguosha->translate(player)).arg(tr("Unknown")) :
            QString("%1%2[%3]")
            .arg(Sanguosha->translate(player))
            .arg(QString::number(i))
            .arg(tr("Unknown")));
        if (i != 0)
            player.append(QString::number(i));
        player_mapping[i] = player;
        role_mapping[player] = "unknown";
        set_nationality[player] = false;

        QListWidgetItem *item = new QListWidgetItem(text);
        item->setData(Qt::UserRole, player);
        item_map[i] = item;
    }

    role_ComboBox = new QComboBox;
    role_ComboBox->addItem(tr("Unknown"), "unknown");
    role_ComboBox->addItem(tr("Lord"), "lord");
    role_ComboBox->addItem(tr("Loyalist"), "loyalist");
    role_ComboBox->addItem(tr("Renegade"), "renegade");
    role_ComboBox->addItem(tr("Rebel"), "rebel");

    for (int i = 0; i < num_ComboBox->currentIndex() + 2; i++)
        list->addItem(item_map[i]);
    list->setCurrentItem(item_map[0]);

    player_draw = new QSpinBox();
    player_draw->setRange(0, Sanguosha->getCardCount());
    player_draw->setValue(4);
    player_draw->setEnabled(true);

    QGroupBox *starter_group = new QGroupBox(tr("Start Info"));
    starter_box = new QCheckBox(tr("Set as Starter"));
    QLabel *draw_text = new QLabel(tr("Start Draw"));
    QLabel *mark_text = new QLabel(tr("marks"));
    QLabel *mark_num_text = new QLabel(tr("pieces"));

    marks_ComboBox = new QComboBox;
    marks_ComboBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    marks_ComboBox->setMinimumContentsLength(12);
    marks_ComboBox->addItem(tr("None"));
    static QDir dir("image/mark");
    QStringList filter;
    filter << "*.png";
    dir.setNameFilters(filter);
    QList<QFileInfo> file_info(dir.entryInfoList(filter));
    foreach (QFileInfo file, file_info) {
        QString mark_name = file.fileName().split(".").first();
        QString mark_translate = Sanguosha->translate(mark_name);
        if (!mark_translate.startsWith("@")) {
            marks_ComboBox->addItem(mark_translate, mark_name);
            QLabel *mark_icon = new QLabel(mark_translate);
            mark_icon->setPixmap(QPixmap(file.filePath()));
            mark_icon->setObjectName(mark_name);
            mark_icon->setToolTip(tr("%1 mark").arg(mark_translate));
            mark_icons << mark_icon;
        }
    }

    marks_count = new QSpinBox;
    marks_count->setRange(0, 999999);
    marks_count->setEnabled(false);

    QVBoxLayout *starter_lay = new QVBoxLayout();
    starter_group->setLayout(starter_lay);
    starter_lay->addWidget(starter_box);
    starter_lay->addLayout(HLay(draw_text, player_draw));
    starter_lay->addLayout(HLay(marks_ComboBox, marks_count, mark_text, mark_num_text));
    QPushButton *addMarkButton = new QPushButton(tr("Add mark by name"));
    starter_lay->addWidget(addMarkButton);
    connect(addMarkButton, &QPushButton::clicked, this, &CustomAssignDialog::addPlayerMark);

    QGridLayout *grid_layout = new QGridLayout;
    const int columns = mark_icons.length() > 10 ? 5 : 4;
    for (int i = 0; i < mark_icons.length(); i++) {
        int row = i / columns;
        int column = i % columns;
        grid_layout->addWidget(mark_icons.at(i), row, column + 1);
        mark_icons.at(i)->hide();
    }
    starter_lay->addLayout(grid_layout);

    general_label = new LabelButton;
    general_label->setPixmap(QPixmap("image/system/disabled.png"));
    general_label->setFixedSize(G_COMMON_LAYOUT.m_tinyAvatarSize);
    QGroupBox *general_box = new QGroupBox(tr("General"));
    general_box->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QVBoxLayout *general_lay = new QVBoxLayout();
    general_box->setLayout(general_lay);
    general_lay->addWidget(general_label);

    general_label2 = new LabelButton;
    general_label2->setPixmap(QPixmap("image/system/disabled.png"));
    general_label2->setFixedSize(G_COMMON_LAYOUT.m_tinyAvatarSize);
    QGroupBox *general_box2 = new QGroupBox(tr("General2"));
    general_box2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QVBoxLayout *general_lay2 = new QVBoxLayout();
    general_box2->setLayout(general_lay2);
    general_lay2->addWidget(general_label2);

    QPushButton *equipAssign = new QPushButton(tr("EquipAssign"));
    QPushButton *handcardAssign = new QPushButton(tr("HandcardAssign"));
    QPushButton *judgeAssign = new QPushButton(tr("JudgeAssign"));
    QPushButton *pileAssign = new QPushButton(tr("PileCardAssign"));

    random_roles_box = new QCheckBox(tr("RandomRoles"));
    rest_in_DP_box = new QCheckBox(tr("RestInDiscardPile"));

    max_hp_prompt = new QCheckBox(tr("Max Hp"));
    max_hp_prompt->setChecked(false);
    max_hp_spin = new QSpinBox();
    max_hp_spin->setRange(1, 999);
    max_hp_spin->setValue(4);
    max_hp_spin->setEnabled(false);

    hp_prompt = new QCheckBox(tr("Hp"));
    hp_prompt->setChecked(false);
    hp_spin = new QSpinBox();
    hp_spin->setRange(1, 999);
    hp_spin->setValue(4);
    hp_spin->setEnabled(false);

    self_select_general = new QCheckBox(tr("General Self Select"));
    self_select_general2 = new QCheckBox(tr("General2 Self Select"));

    set_turned = new QCheckBox(tr("Player Turned"));
    set_chained = new QCheckBox(tr("Player Chained"));

    choose_nationality = new QCheckBox(tr("Customize Nationality"));
    nationalities = new QComboBox;
    int index = 0;
    foreach (QString kingdom, Sanguosha->getKingdoms()) {
        nationalities->addItem(QIcon(QString("image/kingdom/icon/%1.png").arg(kingdom)), Sanguosha->translate(kingdom), kingdom);
        kingdom_index[kingdom] = index;
        index++;
    }
    nationalities->setEnabled(false);

    extra_skill_set = new QPushButton(tr("Set Extra Skills"));

    QFormLayout *advancedLayout = new QFormLayout;
    hujia_spin = new QSpinBox;
    hujia_spin->setRange(-1, 999999);
    hujia_spin->setSpecialValueText(tr("General default"));
    hujia_spin->setValue(-1);
    advancedLayout->addRow(tr("Initial armor"), hujia_spin);
    const QStringList areaNames = {tr("Weapon slots"), tr("Armor slots"),
        tr("Defensive horse slots"), tr("Offensive horse slots"), tr("Treasure slots")};
    for (int area = 0; area < 5; ++area) {
        equip_area_spins[area] = new QSpinBox;
        equip_area_spins[area]->setRange(-1, 99);
        equip_area_spins[area]->setSpecialValueText(tr("Default (1)"));
        equip_area_spins[area]->setValue(-1);
        equip_area_spins[area]->setToolTip(tr("0 abolishes this area; larger values expand it."));
        advancedLayout->addRow(areaNames.at(area), equip_area_spins[area]);
        connect(equip_area_spins[area], SIGNAL(valueChanged(int)), this, SLOT(updateAdvancedState()));
    }
    disable_judge_area = new QCheckBox(tr("Abolish judging area"));
    advancedLayout->addRow(disable_judge_area);
    connect(hujia_spin, SIGNAL(valueChanged(int)), this, SLOT(updateAdvancedState()));
    connect(disable_judge_area, &QCheckBox::toggled, this, &CustomAssignDialog::updateAdvancedState);

    ended_by_pile_text = new QLabel(tr("When pile ends"));
    ended_by_pile_text2 = new QLabel(tr("win"));
    ended_by_pile_box = new QComboBox();
    ended_by_pile = new QCheckBox(tr("Ended by pile ends"));
    ended_by_pile->setEnabled(set_pile.length() > 0);
    ended_by_pile_box->addItem(tr("Lord"), "lord+loyalist");
    ended_by_pile_box->addItem(tr("Renegade"), "renegade");
    ended_by_pile_box->addItem(tr("Rebel"), "rebel");

    single_turn_text = new QLabel(tr("After this turn "));
    single_turn_text2 = new QLabel(tr("win"));
    single_turn_box = new QComboBox();
    single_turn = new QCheckBox(tr("After this turn you lose"));
    single_turn_box->addItem(tr("Lord"), "lord+loyalist");
    single_turn_box->addItem(tr("Renegade"), "renegade");
    single_turn_box->addItem(tr("Rebel"), "rebel");

    before_next_text = new QLabel(tr("Before next turn "));
    before_next_text2 = new QLabel(tr("win"));
    before_next_box = new QComboBox();
    before_next = new QCheckBox(tr("Before next turn begin player lose"));
    before_next_box->addItem(tr("Lord"), "lord+loyalist");
    before_next_box->addItem(tr("Renegade"), "renegade");
    before_next_box->addItem(tr("Rebel"), "rebel");

    QPushButton *okButton = new QPushButton(tr("OK"));
    QPushButton *cancelButton = new QPushButton(tr("Cancel"));
    QPushButton *loadButton = new QPushButton(tr("load"));
    QPushButton *saveButton = new QPushButton(tr("save"));
    QPushButton *defaultLoadButton = new QPushButton(tr("Default load"));
    defaultLoadButton->setObjectName("default_load");

    vlayout->addWidget(role_ComboBox);
    QHBoxLayout *label_lay = new QHBoxLayout;
    label_lay->addWidget(general_box);
    label_lay->addWidget(general_box2);
    vlayout->addLayout(label_lay);
    vlayout->addLayout(HLay(self_select_general, self_select_general2));
    vlayout->addLayout(HLay(max_hp_prompt, max_hp_spin));
    vlayout->addLayout(HLay(hp_prompt, hp_spin));
    vlayout->addLayout(HLay(set_turned, set_chained));
    vlayout->addLayout(HLay(choose_nationality, nationalities));
    vlayout->addWidget(extra_skill_set);
    vlayout->addWidget(starter_group);
    vlayout->addStretch();
    vlayout2->addWidget(new QLabel(tr("Player count")));
    vlayout2->addWidget(num_ComboBox);
    vlayout2->addWidget(random_roles_box);
    vlayout2->addWidget(rest_in_DP_box);
    vlayout2->addWidget(ended_by_pile);
    vlayout2->addLayout(HLay(ended_by_pile_text, ended_by_pile_text2, ended_by_pile_box));
    vlayout2->addWidget(single_turn);
    vlayout2->addLayout(HLay(single_turn_text, single_turn_text2, single_turn_box));
    vlayout2->addWidget(before_next);
    vlayout2->addLayout(HLay(before_next_text, before_next_text2, before_next_box));
    vlayout2->addStretch();

    ended_by_pile_text->hide();
    ended_by_pile_text2->hide();
    ended_by_pile_box->hide();
    single_turn_text->hide();
    single_turn_text2->hide();
    single_turn_box->hide();
    before_next_text->hide();
    before_next_text2->hide();
    before_next_box->hide();

    equip_list = new QListWidget;
    hand_list = new QListWidget;
    judge_list = new QListWidget;
    pile_list = new QListWidget;
    QVBoxLayout *info_lay = new QVBoxLayout(), *equip_lay = new QVBoxLayout(), *hand_lay = new QVBoxLayout(),
        *judge_lay = new QVBoxLayout(), *pile_lay = new QVBoxLayout();

    move_list_up_button = new QPushButton(tr("Move Up"));
    move_list_down_button = new QPushButton(tr("Move Down"));
    move_list_check = new QCheckBox(tr("Move Player List"));
    move_pile_check = new QCheckBox(tr("Move Pile List"));

    move_list_check->setObjectName("list check");
    move_pile_check->setObjectName("pile check");
    move_list_up_button->setObjectName("list_up");
    move_list_down_button->setObjectName("list_down");
    move_list_up_button->setEnabled(false);
    move_list_down_button->setEnabled(false);
    QVBoxLayout *list_move_lay = new QVBoxLayout;
    list_move_lay->addWidget(move_list_check);
    list_move_lay->addWidget(move_pile_check);
    list_move_lay->addStretch();
    list_move_lay->addWidget(move_list_up_button);
    list_move_lay->addWidget(move_list_down_button);
    QHBoxLayout *list_lay = new QHBoxLayout;
    list_lay->addWidget(list);
    list_lay->addLayout(list_move_lay);
    info_lay->addLayout(list_lay);
    QGroupBox *equip_group = new QGroupBox(tr("Equips"));
    QGroupBox *hands_group = new QGroupBox(tr("Handcards"));
    QGroupBox *judge_group = new QGroupBox(tr("Judges"));
    QGroupBox *pile_group = new QGroupBox(tr("DrawPile"));
    equip_group->setLayout(equip_lay);
    hands_group->setLayout(hand_lay);
    judge_group->setLayout(judge_lay);
    pile_group->setLayout(pile_lay);

    removeEquipButton = new QPushButton(tr("Remove Equip"));
    removeHandButton = new QPushButton(tr("Remove Handcard"));
    removeJudgeButton = new QPushButton(tr("Remove Judge"));
    removePileButton = new QPushButton(tr("Remove Pilecard"));

    removeEquipButton->setEnabled(false);
    removeHandButton->setEnabled(false);
    removeJudgeButton->setEnabled(false);
    removePileButton->setEnabled(false);
    equip_lay->addWidget(equip_list);
    equip_lay->addLayout(HLay(equipAssign, removeEquipButton));
    hand_lay->addWidget(hand_list);
    hand_lay->addLayout(HLay(handcardAssign, removeHandButton));
    judge_lay->addWidget(judge_list);
    judge_lay->addLayout(HLay(judgeAssign, removeJudgeButton));
    pile_lay->addWidget(pile_list);
    pile_lay->addLayout(HLay(pileAssign, removePileButton));
    QTabWidget *cardTabs = new QTabWidget;
    cardTabs->addTab(hands_group, tr("Handcards"));
    cardTabs->addTab(equip_group, tr("Equips"));
    cardTabs->addTab(judge_group, tr("Judges"));
    cardTabs->addTab(pile_group, tr("DrawPile"));
    info_lay->addWidget(cardTabs, 1);

    QHBoxLayout *layout = new QHBoxLayout();
    layout->addLayout(info_lay, 1);
    QTabWidget *settingsTabs = new QTabWidget;
    auto addSettingsTab = [settingsTabs](QLayout *contents, const QString &title) {
        QWidget *page = new QWidget;
        page->setLayout(contents);
        QScrollArea *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(page);
        settingsTabs->addTab(scroll, title);
    };
    addSettingsTab(vlayout, tr("Player settings"));
    addSettingsTab(advancedLayout, tr("Advanced state"));
    addSettingsTab(vlayout2, tr("Scene rules"));
    layout->addWidget(settingsTabs, 1);
    QVBoxLayout *mainlayout = new QVBoxLayout();
    mainlayout->addLayout(layout);
    QHBoxLayout *fileActions = new QHBoxLayout;
    fileActions->addWidget(defaultLoadButton);
    fileActions->addWidget(loadButton);
    fileActions->addWidget(saveButton);
    fileActions->addStretch();
    fileActions->addWidget(okButton);
    fileActions->addWidget(cancelButton);
    mainlayout->addLayout(fileActions);
    setLayout(mainlayout);

    connect(role_ComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(updateRole(int)));
    connect(list, SIGNAL(currentItemChanged(QListWidgetItem *, QListWidgetItem *)),
        this, SLOT(on_list_itemSelectionChanged(QListWidgetItem *)));
    connect(move_list_up_button, SIGNAL(clicked()), this, SLOT(exchangeListItem()));
    connect(move_list_down_button, SIGNAL(clicked()), this, SLOT(exchangeListItem()));
    connect(move_list_check, SIGNAL(toggled(bool)), this, SLOT(setMoveButtonAvaliable(bool)));
    connect(move_pile_check, SIGNAL(toggled(bool)), this, SLOT(setMoveButtonAvaliable(bool)));
    connect(num_ComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(updateNumber(int)));
    connect(general_label, SIGNAL(clicked()), this, SLOT(doGeneralAssign()));
    connect(general_label2, SIGNAL(clicked()), this, SLOT(doGeneralAssign2()));
    connect(max_hp_prompt, SIGNAL(toggled(bool)), max_hp_spin, SLOT(setEnabled(bool)));
    connect(hp_prompt, SIGNAL(toggled(bool)), hp_spin, SLOT(setEnabled(bool)));
    connect(hp_prompt, SIGNAL(toggled(bool)), this, SLOT(setPlayerHpEnabled(bool)));
    connect(max_hp_prompt, SIGNAL(toggled(bool)), this, SLOT(setPlayerMaxHpEnabled(bool)));
    connect(self_select_general, SIGNAL(toggled(bool)), this, SLOT(freeChoose(bool)));
    connect(self_select_general2, SIGNAL(toggled(bool)), this, SLOT(freeChoose2(bool)));
    connect(self_select_general, SIGNAL(toggled(bool)), general_label, SLOT(setDisabled(bool)));
    connect(self_select_general2, SIGNAL(toggled(bool)), general_label2, SLOT(setDisabled(bool)));
    connect(set_turned, SIGNAL(toggled(bool)), this, SLOT(doPlayerTurns(bool)));
    connect(set_chained, SIGNAL(toggled(bool)), this, SLOT(doPlayerChains(bool)));
    connect(choose_nationality, SIGNAL(toggled(bool)), nationalities, SLOT(setEnabled(bool)));
    connect(choose_nationality, SIGNAL(toggled(bool)), this, SLOT(setNationalityEnable(bool)));
    connect(nationalities, SIGNAL(currentIndexChanged(int)), this, SLOT(setNationality(int)));
    connect(random_roles_box, SIGNAL(toggled(bool)), this, SLOT(updateAllRoles(bool)));
    connect(extra_skill_set, SIGNAL(clicked()), this, SLOT(doSkillSelect()));
    connect(hp_spin, SIGNAL(valueChanged(int)), this, SLOT(getPlayerHp(int)));
    connect(max_hp_spin, SIGNAL(valueChanged(int)), this, SLOT(getPlayerMaxHp(int)));
    connect(player_draw, SIGNAL(valueChanged(int)), this, SLOT(setPlayerStartDraw(int)));
    connect(starter_box, SIGNAL(toggled(bool)), this, SLOT(setStarter(bool)));
    connect(marks_count, SIGNAL(valueChanged(int)), this, SLOT(setPlayerMarks(int)));
    connect(marks_ComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(getPlayerMarks(int)));
    connect(pile_list, SIGNAL(currentRowChanged(int)), this, SLOT(updatePileInfo(int)));
    connect(removeEquipButton, SIGNAL(clicked()), this, SLOT(removeEquipCard()));
    connect(removeHandButton, SIGNAL(clicked()), this, SLOT(removeHandCard()));
    connect(removeJudgeButton, SIGNAL(clicked()), this, SLOT(removeJudgeCard()));
    connect(removePileButton, SIGNAL(clicked()), this, SLOT(removePileCard()));
    connect(equipAssign, SIGNAL(clicked()), this, SLOT(doEquipCardAssign()));
    connect(handcardAssign, SIGNAL(clicked()), this, SLOT(doHandCardAssign()));
    connect(judgeAssign, SIGNAL(clicked()), this, SLOT(doJudgeCardAssign()));
    connect(pileAssign, SIGNAL(clicked()), this, SLOT(doPileCardAssign()));
    connect(ended_by_pile, SIGNAL(toggled(bool)), this, SLOT(checkEndedByPileBox(bool)));
    connect(single_turn, SIGNAL(toggled(bool)), this, SLOT(checkBeforeNextBox(bool)));
    connect(before_next, SIGNAL(toggled(bool)), this, SLOT(checkSingleTurnBox(bool)));
    connect(okButton, SIGNAL(clicked()), this, SLOT(accept()));
    connect(loadButton, SIGNAL(clicked()), this, SLOT(load()));
    connect(saveButton, SIGNAL(clicked()), this, SLOT(save()));
    connect(defaultLoadButton, SIGNAL(clicked()), this, SLOT(load()));
    connect(cancelButton, SIGNAL(clicked()), this, SLOT(reject()));
}

CustomAssignDialog::~CustomAssignDialog()
{
    // Rows hidden by reducing the player count are not owned by QListWidget.
    for (QListWidgetItem *item : item_map) {
        if (item->listWidget() != list)
            delete item;
    }
}

void CustomAssignDialog::exchangePlayersInfo(QListWidgetItem *first, QListWidgetItem *second)
{
    QString first_name = first->data(Qt::UserRole).toString();
    QString second_name = second->data(Qt::UserRole).toString();

    QString role = role_mapping[first_name], general = general_mapping[first_name],
        general2 = general2_mapping[first_name];
    QList<int> judges(player_judges[first_name]), equips(player_equips[first_name]), hands(player_handcards[first_name]);
    int hp = player_hp[first_name], maxhp = player_maxhp[first_name], start_draw = player_start_draw.value(first_name, 4);
    bool turned = player_turned[first_name], chained = player_chained[first_name],
        free_general = free_choose_general[first_name], free_general2 = free_choose_general2[first_name];
    QStringList ex_skills(player_exskills[first_name]);
    QMap<QString, int> marks(player_marks[first_name]);
    bool setting_nationality = set_nationality.value(first_name, false);
    QString assigned_nationality = assign_nationality.value(first_name, "");

    role_mapping[first_name] = role_mapping[second_name];
    general_mapping[first_name] = general_mapping[second_name];
    general2_mapping[first_name] = general2_mapping[second_name];
    player_judges[first_name].clear();
    player_judges[first_name].append(player_judges[second_name]);
    player_equips[first_name].clear();
    player_equips[first_name].append(player_equips[second_name]);
    player_handcards[first_name].clear();
    player_handcards[first_name].append(player_handcards[second_name]);
    player_hp[first_name] = player_hp[second_name];
    player_maxhp[first_name] = player_maxhp[second_name];
    player_start_draw[first_name] = player_start_draw.value(second_name, 4);
    player_turned[first_name] = player_turned[second_name];
    player_chained[first_name] = player_chained[second_name];
    free_choose_general[first_name] = free_choose_general[second_name];
    free_choose_general2[first_name] = free_choose_general2[second_name];
    player_exskills[first_name].clear();
    player_exskills[first_name].append(player_exskills[second_name]);
    player_marks[first_name].clear();
    player_marks[first_name] = player_marks[second_name];
    set_nationality[first_name] = set_nationality[second_name];
    assign_nationality[first_name] = assign_nationality[second_name];

    role_mapping[second_name] = role;
    general_mapping[second_name] = general;
    general2_mapping[second_name] = general2;
    player_judges[second_name].clear();
    player_judges[second_name].append(judges);
    player_equips[second_name].clear();
    player_equips[second_name].append(equips);
    player_handcards[second_name].clear();
    player_handcards[second_name].append(hands);
    player_hp[second_name] = hp;
    player_maxhp[second_name] = maxhp;
    player_start_draw[second_name] = start_draw;
    player_turned[second_name] = turned;
    player_chained[second_name] = chained;
    free_choose_general[second_name] = free_general;
    free_choose_general2[second_name] = free_general2;
    player_exskills[second_name].clear();
    player_exskills[second_name].append(ex_skills);
    player_marks[second_name].clear();
    player_marks[second_name] = marks;
    set_nationality[second_name] = setting_nationality;
    assign_nationality[second_name] = assigned_nationality;
    qSwap(player_extra_fields[first_name], player_extra_fields[second_name]);
    if (starter == first_name)
        starter = second_name;
    else if (starter == second_name)
        starter = first_name;
}

QString CustomAssignDialog::setListText(QString name, QString role, int index)
{
    QString text = random_roles_box->isChecked() ? QString("[%1]").arg(Sanguosha->translate(role)) :
        QString("%1[%2]").arg(Sanguosha->translate(name))
        .arg(Sanguosha->translate(role));

    if (index >= 0)
        list->item(index)->setText(text);

    return text;
}

void CustomAssignDialog::updateListItems()
{
    for (int i = 0; i <= 9; i++) {
        QString name = (i == 0 ? "Player" : "AI");
        if (i != 0)
            name.append(QString::number(i));

        if (role_mapping[name].isEmpty()) role_mapping[name] = "unknown";
        item_map[i]->setText(setListText(name, role_mapping[name]));
    }
}

void CustomAssignDialog::doEquipCardAssign()
{
    QList<int> excluded;
    for (int i = 0; i < list->count(); i++) {
        excluded.append(player_equips[list->item(i)->data(Qt::UserRole).toString()]);
        excluded.append(player_handcards[list->item(i)->data(Qt::UserRole).toString()]);
        excluded.append(player_judges[list->item(i)->data(Qt::UserRole).toString()]);
        excluded.append(set_pile);
    }

    const QString name = list->currentItem()->data(Qt::UserRole).toString();
    for (int id = 0; id < Sanguosha->getCardCount(); ++id) {
        if (!sceneEquipmentFits(QList<int>{id}, player_extra_fields.value(name)))
            excluded << id;
    }
    CardAssignDialog dialog(this, "equip", "", excluded);
    connect(&dialog, SIGNAL(card_chosen(int)), this, SLOT(getEquipCard(int)));
    dialog.exec();
}

void CustomAssignDialog::getEquipCard(int card_id)
{
    QString name = list->currentItem()->data(Qt::UserRole).toString();
    if (player_equips[name].contains(card_id) || !sceneEquipmentFits(QList<int>{card_id}, player_extra_fields.value(name)))
        return;
    const EquipCard *added = qobject_cast<const EquipCard *>(Sanguosha->getEngineCard(card_id)->getRealCard());
    QList<int> replacement = player_equips.value(name);
    replacement << card_id;
    QList<int> removed;
    for (int id : player_equips.value(name)) {
        if (sceneEquipmentFits(replacement, player_extra_fields.value(name)))
            break;
        const EquipCard *old = qobject_cast<const EquipCard *>(Sanguosha->getEngineCard(id)->getRealCard());
        bool overlaps = false;
        for (int slot : added->getOccupyLocations())
            overlaps |= old && old->getOccupyLocations().contains(slot);
        if (overlaps) {
            replacement.removeOne(id);
            removed << id;
        }
    }
    player_equips[name] = replacement;
    for (int id : removed)
        emit card_addin(id);
    updatePlayerInfo(name);
    equip_list->setCurrentRow(0);
    removeEquipButton->setEnabled(true);
}

void CustomAssignDialog::doHandCardAssign()
{
    QList<int> excluded;
    for (int i = 0; i < list->count(); i++) {
        excluded.append(player_handcards[list->item(i)->data(Qt::UserRole).toString()]);
        excluded.append(player_equips[list->item(i)->data(Qt::UserRole).toString()]);
        excluded.append(player_judges[list->item(i)->data(Qt::UserRole).toString()]);
        excluded.append(set_pile);
    }

    CardAssignDialog dialog(this, "", "", excluded);
    connect(&dialog, SIGNAL(card_chosen(int)), this, SLOT(getHandCard(int)));
    dialog.exec();
}

void CustomAssignDialog::getHandCard(int card_id)
{
    QString name = list->currentItem()->data(Qt::UserRole).toString();
    if (player_handcards[name].contains(card_id))
        return;

    player_handcards[name] << card_id;
    updatePlayerInfo(name);
    hand_list->setCurrentRow(0);
    removeHandButton->setEnabled(true);
}

void CustomAssignDialog::doJudgeCardAssign()
{
    const QString name = list->currentItem()->data(Qt::UserRole).toString();
    const QString state = player_extra_fields.value(name).value("judgeArea");
    if (state == "0" || state == "false") {
        QMessageBox::warning(this, tr("Warning"), tr("Restore the judging area before assigning cards."));
        return;
    }
    QList<int> excluded;
    for (int i = 0; i < list->count(); i++) {
        excluded.append(player_judges[list->item(i)->data(Qt::UserRole).toString()]);
        excluded.append(player_handcards[list->item(i)->data(Qt::UserRole).toString()]);
        excluded.append(player_equips[list->item(i)->data(Qt::UserRole).toString()]);
        excluded.append(set_pile);
    }

    CardAssignDialog dialog(this, "", "DelayedTrick", excluded);
    connect(&dialog, SIGNAL(card_chosen(int)), this, SLOT(getJudgeCard(int)));
    dialog.exec();
}

void CustomAssignDialog::getJudgeCard(int card_id)
{
    QString name = list->currentItem()->data(Qt::UserRole).toString();
    QString card_name = Sanguosha->getEngineCard(card_id)->objectName();
    foreach (int id, player_judges[name]) {
        if (Sanguosha->getEngineCard(id)->objectName() == card_name) {
            emit card_addin(id);
            player_judges[name].removeOne(id);
            break;
        }
    }

    player_judges[name] << card_id;
    updatePlayerInfo(name);
    judge_list->setCurrentRow(0);
    removeJudgeButton->setEnabled(true);
}

void CustomAssignDialog::doPileCardAssign()
{
    QList<int> excluded;
    for (int i = 0; i < list->count(); i++) {
        excluded.append(player_handcards[list->item(i)->data(Qt::UserRole).toString()]);
        excluded.append(player_equips[list->item(i)->data(Qt::UserRole).toString()]);
        excluded.append(player_judges[list->item(i)->data(Qt::UserRole).toString()]);
        excluded.append(set_pile);
    }

    CardAssignDialog dialog(this, "", "", excluded);
    connect(&dialog, SIGNAL(card_chosen(int)), this, SLOT(getPileCard(int)));
    dialog.exec();
}

void CustomAssignDialog::getPileCard(int card_id)
{
    if (set_pile.contains(card_id))
        return;

    set_pile << card_id;
    updatePileInfo();
    pile_list->setCurrentRow(0);
    removePileButton->setEnabled(true);
}

void CustomAssignDialog::updateNumber(int num)
{
    int count = num_ComboBox->itemData(num).toInt();
    if (count < 2)
        return;
    const QSignalBlocker blocker(list);
    if (count < list->count()) {
        for (int i = list->count() - 1; i >= count; i--)
            list->takeItem(i);
    } else {
        for (int i = list->count(); i < count; i++)
            list->addItem(item_map[i]);
    }
    bool starterPresent = false;
    for (int row = 0; row < list->count(); ++row)
        starterPresent |= list->item(row)->data(Qt::UserRole).toString() == starter;
    if (!starterPresent)
        starter.clear();
    if (!list->currentItem() && list->count() > 0)
        list->setCurrentRow(0);
    on_list_itemSelectionChanged(list->currentItem());
}

void CustomAssignDialog::setNationalityEnable(bool toggled)
{
    QString name = list->currentItem()->data(Qt::UserRole).toString();
    set_nationality[name] = toggled;
    assign_nationality[name] = nationalities->itemData(nationalities->currentIndex()).toString();
}

void CustomAssignDialog::setNationality(int index)
{
    QString name = list->currentItem()->data(Qt::UserRole).toString();
    assign_nationality[name] = nationalities->itemData(index).toString();
}

void CustomAssignDialog::updatePlayerInfo(QString name)
{
    equip_list->clear();
    hand_list->clear();
    judge_list->clear();

    removeEquipButton->setEnabled(!player_equips[name].isEmpty());
    removeHandButton->setEnabled(!player_handcards[name].isEmpty());
    removeJudgeButton->setEnabled(!player_judges[name].isEmpty());

    foreach (int equip_id, player_equips[name]) {
        const Card *card = Sanguosha->getEngineCard(equip_id);
        QString card_name = Sanguosha->translate(card->objectName());
        QIcon suit_icon = QIcon(QString("image/system/cardsuit/%1.png").arg(card->getSuitString()));
        QString point = card->getNumberString();

        QString card_info = point + "  " + card_name + "\t\t" + Sanguosha->translate(card->getSubtype());
        QListWidgetItem *name_item = new QListWidgetItem(card_info, equip_list);
        name_item->setIcon(suit_icon);
        name_item->setData(Qt::UserRole, card->getId());
    }

    foreach (int hand_id, player_handcards[name]) {
        const Card *card = Sanguosha->getEngineCard(hand_id);
        QString card_name = Sanguosha->translate(card->objectName());
        QIcon suit_icon = QIcon(QString("image/system/cardsuit/%1.png").arg(card->getSuitString()));
        QString point = card->getNumberString();

        QString card_info = point + "  " + card_name + "\t\t" + Sanguosha->translate(card->getSubtype());
        QListWidgetItem *name_item = new QListWidgetItem(card_info, hand_list);
        name_item->setIcon(suit_icon);
        name_item->setData(Qt::UserRole, card->getId());
    }

    foreach (int judge_id, player_judges[name]) {
        const Card *card = Sanguosha->getEngineCard(judge_id);
        QString card_name = Sanguosha->translate(card->objectName());
        QIcon suit_icon = QIcon(QString("image/system/cardsuit/%1.png").arg(card->getSuitString()));
        QString point = card->getNumberString();

        QString card_info = point + "  " + card_name + "\t\t" + Sanguosha->translate(card->getSubtype());
        QListWidgetItem *name_item = new QListWidgetItem(card_info, judge_list);
        name_item->setIcon(suit_icon);
        name_item->setData(Qt::UserRole, card->getId());
    }

    equip_list->setCurrentRow(0);
    hand_list->setCurrentRow(0);
    judge_list->setCurrentRow(0);

    for (int i = 0; i < mark_icons.length(); i++)
        mark_icons.at(i)->hide();

    foreach (QString mark, player_marks[name].keys()) {
        if (player_marks[name][mark] > 0) {
            for (int i = 0; i < mark_icons.length(); i++) {
                if (mark_icons.at(i)->objectName() == mark) {
                    mark_icons.at(i)->show();
                    break;
                }
            }
        }
    }
}

void CustomAssignDialog::updatePileInfo(int row)
{
    if (row >= 0) {
        if (move_pile_check->isChecked()) {
            move_list_up_button->setEnabled(row != 0);
            move_list_down_button->setEnabled(row != pile_list->count() - 1);
        }
        return;
    }

    if (row == -1)
        return;

    pile_list->clear();

    removePileButton->setDisabled(set_pile.isEmpty());
    ended_by_pile->setDisabled(set_pile.isEmpty());

    foreach (int card_id, set_pile) {
        const Card *card = Sanguosha->getEngineCard(card_id);
        QString card_name = Sanguosha->translate(card->objectName());
        QIcon suit_icon = QIcon(QString("image/system/cardsuit/%1.png").arg(card->getSuitString()));
        QString point = card->getNumberString();

        QString card_info = point + "  " + card_name + "\t\t" + Sanguosha->translate(card->getSubtype());
        QListWidgetItem *name_item = new QListWidgetItem(card_info, pile_list);
        name_item->setIcon(suit_icon);
        name_item->setData(Qt::UserRole, card->getId());
    }

    if (pile_list->count() > 0)
        pile_list->setCurrentRow(0);

}

void CustomAssignDialog::updatePlayerHpInfo(QString name)
{
    // Refreshing widgets must never write the previous player's values into this row.
    const QSignalBlocker hpBlock(hp_spin), maxBlock(max_hp_spin);
    const QSignalBlocker hpPromptBlock(hp_prompt), maxPromptBlock(max_hp_prompt);
    max_hp_prompt->setChecked(player_maxhp.value(name) > 0);
    hp_prompt->setChecked(player_hp.value(name) > 0);
    max_hp_spin->setValue(player_maxhp.value(name, 4));
    hp_spin->setValue(player_hp.value(name, 4));
    max_hp_spin->setEnabled(max_hp_prompt->isChecked());
    hp_spin->setEnabled(hp_prompt->isChecked());
}

void CustomAssignDialog::updateAllRoles(bool)
{
    for (int i = 0; i < list->count(); i++) {
        QString name = player_mapping[i];
        QString role = role_mapping[name];
        item_map[i]->setText(setListText(name, role, i));
    }
}

void CustomAssignDialog::getPlayerHp(int hp)
{
    QString name = list->currentItem()->data(Qt::UserRole).toString();
    if (hp_prompt->isChecked())
        player_hp[name] = hp;
}

void CustomAssignDialog::getPlayerMaxHp(int maxhp)
{
    QString name = list->currentItem()->data(Qt::UserRole).toString();
    if (max_hp_prompt->isChecked())
        player_maxhp[name] = maxhp;
}

void CustomAssignDialog::setPlayerHpEnabled(bool toggled)
{
    QString name = list->currentItem()->data(Qt::UserRole).toString();
    if (!toggled)
        player_hp.remove(name);
    else
        player_hp[name] = hp_spin->value();
}

void CustomAssignDialog::setPlayerMaxHpEnabled(bool toggled)
{
    QString name = list->currentItem()->data(Qt::UserRole).toString();
    if (!toggled)
        player_maxhp.remove(name);
    else
        player_maxhp[name] = max_hp_spin->value();
}

void CustomAssignDialog::setPlayerStartDraw(int draw_num)
{
    QString name = list->currentItem()->data(Qt::UserRole).toString();
    player_start_draw[name] = draw_num;
}

void CustomAssignDialog::setStarter(bool toggled)
{
    if (toggled)
        starter = list->currentItem()->data(Qt::UserRole).toString();
    else
        starter.clear();
}

void CustomAssignDialog::addPlayerMark()
{
    bool accepted = false;
    const QString mark = QInputDialog::getText(this, tr("Add mark by name"),
        tr("Internal mark name (for example @HuJia)"), QLineEdit::Normal, QString(), &accepted).trimmed();
    if (!accepted || mark.isEmpty())
        return;
    if (mark.contains(QRegularExpression(QStringLiteral("[\\s,:*|]")))) {
        QMessageBox::warning(this, tr("Warning"), tr("Mark names cannot contain spaces or , : * |."));
        return;
    }
    int index = marks_ComboBox->findData(mark);
    if (index < 0) {
        marks_ComboBox->addItem(QStringLiteral("%1 [%2]").arg(Sanguosha->translate(mark), mark), mark);
        index = marks_ComboBox->count() - 1;
    }
    marks_ComboBox->setCurrentIndex(index);
    marks_count->setFocus();
}

void CustomAssignDialog::refreshAdvancedState(const QString &name)
{
    const QMap<QString, QString> fields = player_extra_fields.value(name);
    const QSignalBlocker armorBlock(hujia_spin), judgeBlock(disable_judge_area);
    hujia_spin->setValue(player_marks.value(name).contains("@HuJia")
        ? player_marks.value(name).value("@HuJia") : fields.value("hujia", "-1").toInt());
    QMap<int, int> counts;
    for (const QString &entry : fields.value("equipArea").split(',', Qt::SkipEmptyParts)) {
        const QStringList pair = entry.split('*');
        if (pair.size() == 2)
            counts[pair.first().toInt()] = pair.last().toInt();
    }
    for (int area = 0; area < 5; ++area) {
        const QSignalBlocker areaBlock(equip_area_spins[area]);
        equip_area_spins[area]->setValue(counts.value(area, -1));
    }
    const QString judge = fields.value("judgeArea");
    disable_judge_area->setChecked(judge == "0" || judge == "false");
}

void CustomAssignDialog::updateAdvancedState()
{
    if (!list->currentItem())
        return;
    const QString name = list->currentItem()->data(Qt::UserRole).toString();
    QMap<QString, QString> &fields = player_extra_fields[name];
    if (sender() == hujia_spin) {
        // Old scenes may encode armor as a mark; retain a single source when edited.
        player_marks[name].remove("@HuJia");
        if (hujia_spin->value() < 0)
            fields.remove("hujia");
        else
            fields["hujia"] = QString::number(hujia_spin->value());
        getPlayerMarks(marks_ComboBox->currentIndex());
        updatePlayerInfo(name);
    }
    QStringList areas;
    for (int area = 0; area < 5; ++area) {
        if (equip_area_spins[area]->value() >= 0)
            areas << QStringLiteral("%1*%2").arg(area).arg(equip_area_spins[area]->value());
    }
    if (areas.isEmpty())
        fields.remove("equipArea");
    else
        fields["equipArea"] = areas.join(',');
    if (disable_judge_area->isChecked())
        fields["judgeArea"] = "0";
    else
        fields.remove("judgeArea");
}

void CustomAssignDialog::setPlayerMarks(int value)
{
    QString mark_name = marks_ComboBox->itemData(marks_ComboBox->currentIndex()).toString();
    if (mark_name.isEmpty())
        return;
    QString player_name = list->item(list->currentRow())->data(Qt::UserRole).toString();
    player_marks[player_name][mark_name] = value;
    if (mark_name == "@HuJia")
        refreshAdvancedState(player_name);

    for (int i = 0; i < mark_icons.length(); i++) {
        if (mark_icons.at(i)->objectName() == mark_name) {
            if (value > 0)
                mark_icons.at(i)->show();
            else
                mark_icons.at(i)->hide();
            break;
        }
    }
}

void CustomAssignDialog::getPlayerMarks(int index)
{
    QString mark_name = marks_ComboBox->itemData(index).toString();
    QString player_name = list->item(list->currentRow())->data(Qt::UserRole).toString();

    marks_count->setEnabled(!mark_name.isEmpty());
    const QSignalBlocker blocker(marks_count);
    marks_count->setValue(player_marks.value(player_name).value(mark_name));
}

void CustomAssignDialog::updateRole(int index)
{
    QString name = list->currentItem()->data(Qt::UserRole).toString();
    QString role = role_ComboBox->itemData(index).toString();
    setListText(name, role, list->currentRow());
    role_mapping[name] = role;
}

void CustomAssignDialog::removeEquipCard()
{
    if (!equip_list->currentItem()) return;
    int card_id = equip_list->currentItem()->data(Qt::UserRole).toInt();
    QString name = list->currentItem()->data(Qt::UserRole).toString();
    if (player_equips[name].contains(card_id)) {
        player_equips[name].removeOne(card_id);
        int row = equip_list->currentRow();
        delete equip_list->takeItem(row);
        if (equip_list->count() > 0)
            equip_list->setCurrentRow(row >= equip_list->count() ? row - 1 : row);
        else
            removeEquipButton->setEnabled(false);
    }
}

void CustomAssignDialog::removeHandCard()
{
    if (!hand_list->currentItem()) return;
    int card_id = hand_list->currentItem()->data(Qt::UserRole).toInt();
    QString name = list->currentItem()->data(Qt::UserRole).toString();
    if (player_handcards[name].contains(card_id)) {
        player_handcards[name].removeOne(card_id);
        int row = hand_list->currentRow();
        delete hand_list->takeItem(row);
        if (hand_list->count() > 0)
            hand_list->setCurrentRow(row >= hand_list->count() ? row - 1 : row);
        else
            removeHandButton->setEnabled(false);
    }
}

void CustomAssignDialog::removeJudgeCard()
{
    if (!judge_list->currentItem()) return;
    int card_id = judge_list->currentItem()->data(Qt::UserRole).toInt();
    QString name = list->currentItem()->data(Qt::UserRole).toString();
    if (player_judges[name].contains(card_id)) {
        player_judges[name].removeOne(card_id);
        int row = judge_list->currentRow();
        delete judge_list->takeItem(row);
        if (judge_list->count() > 0)
            judge_list->setCurrentRow(row >= judge_list->count() ? row - 1 : row);
        else
            removeJudgeButton->setEnabled(false);
    }
}

void CustomAssignDialog::removePileCard()
{
    if (!pile_list->currentItem()) return;
    int card_id = pile_list->currentItem()->data(Qt::UserRole).toInt();
    if (set_pile.contains(card_id)) {
        int row = pile_list->currentRow();
        delete pile_list->takeItem(row);
        if (pile_list->count() > 0)
            pile_list->setCurrentRow(row >= pile_list->count() ? row - 1 : row);
        else {
            removePileButton->setEnabled(false);
            ended_by_pile->setEnabled(false);
            ended_by_pile->setChecked(false);
        }
        set_pile.removeOne(card_id);
    }
}

void CustomAssignDialog::doGeneralAssign()
{
    choose_general2 = false;
    GeneralAssignDialog dialog(this);
    connect(&dialog, SIGNAL(general_chosen(QString)), this, SLOT(getChosenGeneral(QString)));
    dialog.exec();
}

void CustomAssignDialog::doGeneralAssign2()
{
    choose_general2 = true;
    GeneralAssignDialog dialog(this, true);
    connect(&dialog, SIGNAL(general_chosen(QString)), this, SLOT(getChosenGeneral(QString)));
    connect(&dialog, SIGNAL(general_cleared()), this, SLOT(clearGeneral2()));
    dialog.exec();
}

void CustomAssignDialog::setMoveButtonAvaliable(bool toggled)
{
    if (sender()->objectName() == "list check") {
        move_pile_check->setChecked(false);
        move_list_check->setChecked(toggled);
        if (toggled) {
            move_list_up_button->setEnabled(list->currentRow() != 0);
            move_list_down_button->setEnabled(list->currentRow() != list->count() - 1);
        }
    } else {
        move_list_check->setChecked(false);
        move_pile_check->setChecked(toggled);
        if (toggled) {
            move_list_up_button->setEnabled(pile_list->count() > 0 && pile_list->currentRow() != 0);
            move_list_down_button->setEnabled(pile_list->count() > 0 && pile_list->currentRow() != pile_list->count() - 1);
        }
    }

    if (!move_list_check->isChecked() && !move_pile_check->isChecked()) {
        move_list_up_button->setEnabled(false);
        move_list_down_button->setEnabled(false);
    }
}

void CustomAssignDialog::accept()
{
    // User-custom scenarios are always stored in a user-writable directory: the install
    // tree/AppImage is read-only, and writing back there would only fail silently.
    const QString customScenario =
        QSanRuntimePaths::userDataPath(QStringLiteral("etc/customScenes/custom_scenario.txt"));
    if (save(customScenario)) {
        const Scenario *scene = Sanguosha->getScenario("custom_scenario");
        MiniSceneRule *rule = qobject_cast<MiniSceneRule *>(scene->getRule());
        Q_ASSERT(rule != nullptr);
        rule->loadSetting(customScenario);
        emit scenario_changed();
        QDialog::accept();
    }
}

void CustomAssignDialog::reject()
{
    QDialog::reject();
}

void CustomAssignDialog::clearGeneral2()
{
    QString name = list->currentItem()->data(Qt::UserRole).toString();
    general2_mapping[name].clear();

    general_label2->setPixmap(QPixmap("image/system/disabled.png"));
}

void CustomAssignDialog::getChosenGeneral(QString name)
{
    if (choose_general2) {
        QPixmap pixmap = G_ROOM_SKIN.getGeneralPixmap(name, QSanRoomSkin::S_GENERAL_ICON_SIZE_TINY);
        pixmap = pixmap.scaled(G_COMMON_LAYOUT.m_tinyAvatarSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        general_label2->setPixmap(pixmap);
        if (list->currentItem())
            general2_mapping[list->currentItem()->data(Qt::UserRole).toString()] = name;
    } else {
        QPixmap pixmap = G_ROOM_SKIN.getGeneralPixmap(name, QSanRoomSkin::S_GENERAL_ICON_SIZE_TINY);
        pixmap = pixmap.scaled(G_COMMON_LAYOUT.m_tinyAvatarSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        general_label->setPixmap(pixmap);
        if (list->currentItem())
            general_mapping[list->currentItem()->data(Qt::UserRole).toString()] = name;
    }
}

void CustomAssignDialog::freeChoose(bool toggled)
{
    QString name = list->currentItem()->data(Qt::UserRole).toString();
    free_choose_general[name] = toggled;
}

void CustomAssignDialog::freeChoose2(bool toggled)
{
    QString name = list->currentItem()->data(Qt::UserRole).toString();
    free_choose_general2[name] = toggled;
}

void CustomAssignDialog::doPlayerChains(bool toggled)
{
    QString name = list->currentItem()->data(Qt::UserRole).toString();
    player_chained[name] = toggled;
}

void CustomAssignDialog::doPlayerTurns(bool toggled)
{
    QString name = list->currentItem()->data(Qt::UserRole).toString();
    player_turned[name] = toggled;
}

void CustomAssignDialog::doSkillSelect()
{
    QString name = list->currentItem()->data(Qt::UserRole).toString();
    SkillAssignDialog dialog(this, name, player_exskills[name]);
    connect(&dialog, SIGNAL(skill_update(QStringList)), this, SLOT(updatePlayerExSkills(QStringList)));
    dialog.exec();
}

void CustomAssignDialog::updatePlayerExSkills(QStringList update_skills)
{
    QString name = list->currentItem()->data(Qt::UserRole).toString();
    player_exskills[name].clear();
    player_exskills[name].append(update_skills);
}

void CustomAssignDialog::exchangeListItem()
{
    int first_index = -1, second_index = -1;
    if (move_list_check->isChecked())
        first_index = list->currentRow();
    else if (move_pile_check->isChecked())
        first_index = pile_list->currentRow();

    if (sender()->objectName() == "list_up")
        second_index = first_index - 1;
    else if (sender()->objectName() == "list_down")
        second_index = first_index + 1;

    const int count = move_list_check->isChecked() ? list->count() : pile_list->count();
    if (first_index < 0 || second_index < 0 || first_index >= count || second_index >= count)
        return;

    if (move_list_check->isChecked()) {
        exchangePlayersInfo(item_map[first_index], item_map[second_index]);
        updateListItems();
        list->setCurrentRow(second_index);
    } else if (move_pile_check->isChecked()) {
        int id1 = pile_list->item(first_index)->data(Qt::UserRole).toInt();
        int id2 = pile_list->item(second_index)->data(Qt::UserRole).toInt();

        set_pile.swapItemsAt(set_pile.indexOf(id1), set_pile.indexOf(id2));
        updatePileInfo();
        pile_list->setCurrentRow(second_index);
    }
}

void CustomAssignDialog::on_list_itemSelectionChanged(QListWidgetItem *current)
{
    if (list->count() == 0 || current == nullptr) return;

    const QSignalBlocker roleBlock(role_ComboBox), firstGeneralBlock(self_select_general), secondGeneralBlock(self_select_general2);
    const QSignalBlocker turnedBlock(set_turned), chainedBlock(set_chained), drawBlock(player_draw);
    const QSignalBlocker nationalityBlock(nationalities), nationalityEnabledBlock(choose_nationality);
    const QSignalBlocker starterBlock(starter_box), marksBlock(marks_count);

    QString player_name = current->data(Qt::UserRole).toString();
    if (!general_mapping.value(player_name, "").isEmpty()) {
        QPixmap pixmap = G_ROOM_SKIN.getGeneralPixmap(general_mapping.value(player_name), QSanRoomSkin::S_GENERAL_ICON_SIZE_TINY);
        pixmap = pixmap.scaled(G_COMMON_LAYOUT.m_tinyAvatarSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        general_label->setPixmap(pixmap);
    } else
        general_label->setPixmap(QPixmap(QString("image/system/disabled.png")));


    if (!general2_mapping.value(player_name, "").isEmpty()) {
        QPixmap pixmap = G_ROOM_SKIN.getGeneralPixmap(general2_mapping.value(player_name), QSanRoomSkin::S_GENERAL_ICON_SIZE_TINY);
        pixmap = pixmap.scaled(G_COMMON_LAYOUT.m_tinyAvatarSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        general_label2->setPixmap(pixmap);
    } else
        general_label2->setPixmap(QPixmap(QString("image/system/disabled.png")));

    if (!role_mapping[player_name].isEmpty()) {
        for (int i = 0; i < role_ComboBox->count(); i++) {
            if (role_mapping[player_name] == role_ComboBox->itemData(i).toString()) {
                role_ComboBox->setCurrentIndex(i);
                updateRole(i);
                break;
            }
        }
    }

    self_select_general->setChecked(free_choose_general[player_name]);
    self_select_general2->setChecked(free_choose_general2[player_name]);
    general_label->setDisabled(self_select_general->isChecked());
    general_label2->setDisabled(self_select_general2->isChecked());

    set_turned->setChecked(player_turned.value(player_name, false));
    set_chained->setChecked(player_chained.value(player_name, false));

    ended_by_pile->setChecked(is_ended_by_pile);
    single_turn->setChecked(is_single_turn);
    before_next->setChecked(is_before_next);

    if (move_list_check->isChecked()) {
        move_list_up_button->setEnabled(list->currentRow() != 0);
        move_list_down_button->setEnabled(list->currentRow() != list->count() - 1);
    }

    int val = 4;
    if (player_start_draw.keys().contains(player_name)) val = player_start_draw[player_name];
    player_draw->setValue(val);

    starter_box->setEnabled(true);
    starter_box->setChecked(starter == player_name);

    QString kingdom = assign_nationality.value(player_name, "");
    if (!kingdom.isEmpty())
        nationalities->setCurrentIndex(kingdom_index[kingdom]);

    choose_nationality->setChecked(set_nationality.value(player_name, false));
    nationalities->setEnabled(choose_nationality->isChecked());

    QString mark_name = marks_ComboBox->itemData(marks_ComboBox->currentIndex()).toString();
    if (!mark_name.isEmpty())
        marks_count->setValue(player_marks.value(player_name)[mark_name]);
    else
        marks_count->setValue(0);

    updatePlayerInfo(player_name);
    updatePlayerHpInfo(player_name);
    refreshAdvancedState(player_name);
}

void CustomAssignDialog::checkBeforeNextBox(bool toggled)
{
    if (toggled) {
        before_next->setChecked(false);
        is_before_next = false;
        is_single_turn = true;

        single_turn_text->show();
        single_turn_text2->show();
        single_turn_box->show();
    } else {
        is_single_turn = false;

        single_turn_text->hide();
        single_turn_text2->hide();
        single_turn_box->hide();
    }
}

void CustomAssignDialog::checkSingleTurnBox(bool toggled)
{
    if (toggled) {
        single_turn->setChecked(false);
        is_before_next = true;
        is_single_turn = false;

        before_next_text->show();
        before_next_text2->show();
        before_next_box->show();
    } else {
        is_before_next = false;

        before_next_text->hide();
        before_next_text2->hide();
        before_next_box->hide();
    }
}

void CustomAssignDialog::checkEndedByPileBox(bool toggled)
{
    if (toggled) {
        is_ended_by_pile = true;

        ended_by_pile_text->show();
        ended_by_pile_text2->show();
        ended_by_pile_box->show();
    } else {
        is_ended_by_pile = false;

        ended_by_pile_text->hide();
        ended_by_pile_text2->hide();
        ended_by_pile_box->hide();
    }
}

void CustomAssignDialog::load()
{
    const QString filename = sender() && sender()->objectName() == "default_load"
        ? QSanRuntimePaths::readablePath(QStringLiteral("etc/customScenes/custom_scenario.txt"))
        : QFileDialog::getOpenFileName(this, tr("Open mini scenario settings"),
            QSanRuntimePaths::customSceneDir(), tr("Mini scenario settings (*.txt)"));
    if (filename.isEmpty())
        return;
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Warning"), tr("Cannot read scene: %1").arg(file.errorString()));
        return;
    }

    // Parse and validate a temporary document before touching the current editor.
    QList<QMap<QString, QString> > parsedPlayers;
    QList<int> parsedPile;
    QStringList parsedOptions;
    QSet<int> usedCards;
    int lineNumber = 0;
    auto fail = [this, &lineNumber](const QString &reason) {
        QMessageBox::warning(this, tr("Warning"), tr("Line %1: %2").arg(lineNumber).arg(reason));
    };
    auto integerInRange = [](const QString &text, int low, int high) {
        bool ok = false;
        const int value = text.toInt(&ok);
        return ok && value >= low && value <= high;
    };
    auto readCards = [&usedCards](const QString &text, const QString &zone, QStringList &ids) {
        if (text.isEmpty())
            return true;
        for (const QString &token : text.split(',')) {
            bool numeric = false;
            int id = token.toInt(&numeric);
            if (!numeric) {
                id = -1;
                for (int candidate = 0; candidate < Sanguosha->getCardCount(); ++candidate) {
                    const Card *card = Sanguosha->getEngineCard(candidate);
                    if (card && card->objectName() == token && !usedCards.contains(candidate)) {
                        id = candidate;
                        break;
                    }
                }
            }
            if (id < 0 || id >= Sanguosha->getCardCount() || usedCards.contains(id))
                return false;
            const Card *card = Sanguosha->getEngineCard(id);
            if (!card || (zone == "equip" && !card->isKindOf("EquipCard"))
                || (zone == "judge" && !card->isKindOf("DelayedTrick")))
                return false;
            usedCards.insert(id);
            ids << QString::number(id);
        }
        return true;
    };

    QTextStream input(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // New saves are UTF-8; keep Qt5's locale fallback for legacy ANSI scenes.
    const QByteArray encoded = file.peek(file.size());
    if (encoded.startsWith("\xEF\xBB\xBF") || QString::fromUtf8(encoded).toUtf8() == encoded)
        input.setCodec("UTF-8");
#endif
    bool hasPile = false;
    int starters = 0;
    while (!input.atEnd()) {
        const QString line = input.readLine().trimmed();
        ++lineNumber;
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        if (line.startsWith("extraOptions:")) {
            parsedOptions << line.mid(13).split(' ', Qt::SkipEmptyParts);
            continue;
        }
        if (line.startsWith("setPile:")) {
            QStringList ids;
            if (hasPile || !readCards(line.mid(8), QString(), ids)) {
                fail(tr("Invalid or duplicate draw-pile card."));
                return;
            }
            hasPile = true;
            for (const QString &id : ids)
                parsedPile.prepend(id.toInt());
            continue;
        }
        QMap<QString, QString> fields;
        const QStringList tokens = line.contains('|') ? line.split('|') : line.split(' ', Qt::SkipEmptyParts);
        for (const QString &token : tokens) {
            const int colon = token.indexOf(':');
            const QString key = token.left(colon).trimmed();
            const QString value = token.mid(colon + 1).trimmed();
            if (colon <= 0 || fields.contains(key) || key.contains(QRegularExpression("[\\s,|*]"))
                || value.contains(QRegularExpression("[\\s:|]"))) {
                fail(tr("Invalid or repeated field."));
                return;
            }
            fields.insert(key, value);
        }
        if (!fields.contains("general") || parsedPlayers.size() >= 10) {
            fail(tr("A scene needs 2 to 10 players with valid generals."));
            return;
        }
        for (const QString &key : {QString("general"), QString("general2"), QString("general3")}) {
            const QString general = fields.value(key);
            if ((key == "general" && general.isEmpty())
                || (!general.isEmpty() && general != "select" && !Sanguosha->getGeneral(general))) {
                fail(tr("Unknown general: %1").arg(general));
                return;
            }
        }
        if (!QStringList({"lord", "loyalist", "rebel", "renegade"}).contains(fields.value("role"))) {
            fail(tr("Unsupported player role: %1").arg(fields.value("role")));
            return;
        }
        const QMap<QString, QPair<int, int> > limits = {
            {"maxhp", qMakePair(1, 999)}, {"hp", qMakePair(1, 999)}, {"hpadj", qMakePair(-998, 998)},
            {"hujia", qMakePair(0, 999999)}, {"draw", qMakePair(0, Sanguosha->getCardCount())}
        };
        for (auto it = limits.cbegin(); it != limits.cend(); ++it) {
            if (fields.contains(it.key()) && !integerInRange(fields.value(it.key()), it.value().first, it.value().second)) {
                fail(tr("Invalid value for %1.").arg(it.key()));
                return;
            }
        }
        if (fields.contains("maxhp") && (fields.value("maxhp").toInt() + fields.value("hpadj").toInt() < 1
            || (fields.contains("hp") && fields.value("hp").toInt() > fields.value("maxhp").toInt() + fields.value("hpadj").toInt()))) {
            fail(tr("HP exceeds the configured maximum."));
            return;
        }
        if (fields.contains("judgeArea") && !QStringList({"0", "1", "false", "true"}).contains(fields.value("judgeArea"))) {
            fail(tr("Invalid judging-area state."));
            return;
        }
        QSet<QString> markNames;
        for (const QString &mark : fields.value("marks").split(',', Qt::SkipEmptyParts)) {
            const QStringList pair = mark.split('*');
            if (pair.size() != 2 || pair.first().isEmpty() || markNames.contains(pair.first())
                || !integerInRange(pair.last(), 0, 999999)) {
                fail(tr("Invalid mark; expected name*count."));
                return;
            }
            markNames.insert(pair.first());
        }
        QSet<int> areas;
        for (const QString &area : fields.value("equipArea").split(',', Qt::SkipEmptyParts)) {
            const QStringList pair = area.split('*');
            if (pair.size() != 2 || !integerInRange(pair.first(), 0, 4)
                || !integerInRange(pair.last(), 0, 99) || areas.contains(pair.first().toInt())) {
                fail(tr("Invalid equipment area; expected slot*count."));
                return;
            }
            areas.insert(pair.first().toInt());
        }
        if (!fields.value("nationality").isEmpty() && !kingdom_index.contains(fields.value("nationality"))) {
            fail(tr("Unknown kingdom: %1").arg(fields.value("nationality")));
            return;
        }
        for (const QString &key : {QString("endedByPile"), QString("singleTurn"), QString("beforeNext")}) {
            if (!fields.value(key).isEmpty()
                && !QStringList({"lord+loyalist", "renegade", "rebel"}).contains(fields.value(key))) {
                fail(tr("Unsupported winner: %1").arg(fields.value(key)));
                return;
            }
        }
        for (const QString &zone : {QString("equip"), QString("hand"), QString("judge")}) {
            QStringList ids;
            if (!readCards(fields.value(zone), zone, ids)) {
                fail(tr("Invalid or duplicate card in %1.").arg(zone));
                return;
            }
            if (fields.contains(zone))
                fields[zone] = ids.join(',');
        }
        QList<int> equipment;
        for (const QString &id : fields.value("equip").split(',', Qt::SkipEmptyParts))
            equipment << id.toInt();
        if (!sceneEquipmentFits(equipment, fields)) {
            fail(tr("Equipment exceeds the available slots."));
            return;
        }
        if ((fields.value("judgeArea") == "0" || fields.value("judgeArea") == "false") && !fields.value("judge").isEmpty()) {
            fail(tr("The abolished judging area still has cards."));
            return;
        }
        if (!fields.value("starter").isEmpty())
            ++starters;
        parsedPlayers << fields;
    }
    if (input.status() != QTextStream::Ok || file.error() != QFile::NoError) {
        fail(tr("Cannot read scene: %1").arg(file.errorString()));
        return;
    }
    if (parsedPlayers.size() < 2 || starters != 1) {
        fail(tr("A scene needs 2 to 10 players and exactly one starter."));
        return;
    }
    bool endsByPile = false, endsThisTurn = false, endsBeforeNext = false;
    QSet<QString> winners, camps;
    int lordCount = 0;
    for (const auto &fields : parsedPlayers) {
        endsByPile |= !fields.value("endedByPile").isEmpty();
        endsThisTurn |= !fields.value("singleTurn").isEmpty();
        endsBeforeNext |= !fields.value("beforeNext").isEmpty();
        const QString role = fields.value("role");
        lordCount += role == "lord" ? 1 : 0;
        camps.insert(role == "lord" ? "loyalist" : role);
        for (const QString &key : {QString("endedByPile"), QString("singleTurn"), QString("beforeNext")}) {
            if (!fields.value(key).isEmpty()) {
                if (winners.contains(key)) {
                    fail(tr("Conflicting or incomplete ending rules."));
                    return;
                }
                winners.insert(key);
            }
        }
    }
    if ((endsByPile && parsedPile.isEmpty()) || (endsThisTurn && endsBeforeNext)) {
        fail(tr("Conflicting or incomplete ending rules."));
        return;
    }
    if (lordCount > 1 || camps.size() < 2) {
        fail(lordCount > 1 ? tr("Two many lords in the game") : tr("No different camps in the game"));
        return;
    }

    const QSignalBlocker listBlock(list), numberBlock(num_ComboBox);
    const QSignalBlocker pileEndBlock(ended_by_pile), turnEndBlock(single_turn), nextTurnBlock(before_next);
    set_pile = parsedPile;
    role_mapping.clear();
    general_mapping.clear();
    general2_mapping.clear();
    player_maxhp.clear();
    player_hp.clear();
    player_start_draw.clear();
    player_chained.clear();
    player_turned.clear();
    player_marks.clear();
    player_exskills.clear();
    player_handcards.clear();
    player_equips.clear();
    player_judges.clear();
    player_extra_fields.clear();
    set_nationality.clear();
    assign_nationality.clear();
    free_choose_general.clear();
    free_choose_general2.clear();
    starter.clear();
    is_ended_by_pile = is_single_turn = is_before_next = false;
    extra_options = parsedOptions;
    extra_options.removeAll(MiniSceneRule::S_EXTRA_OPTION_RANDOM_ROLES);
    extra_options.removeAll(MiniSceneRule::S_EXTRA_OPTION_REST_IN_DISCARD_PILE);
    const QStringList editedFields = {"general", "general2", "role", "maxhp", "hp", "draw", "starter",
        "chained", "turned", "nationality", "acquireSkills", "marks", "equip", "hand", "judge",
        "endedByPile", "singleTurn", "beforeNext"};
    for (int row = 0; row < parsedPlayers.size(); ++row) {
        const QString name = player_mapping.value(row);
        const QMap<QString, QString> &fields = parsedPlayers.at(row);
        role_mapping[name] = fields.value("role");
        free_choose_general[name] = fields.value("general") == "select";
        free_choose_general2[name] = fields.value("general2") == "select";
        if (!free_choose_general[name])
            general_mapping[name] = fields.value("general");
        if (!free_choose_general2[name])
            general2_mapping[name] = fields.value("general2");
        if (fields.contains("maxhp"))
            player_maxhp[name] = fields.value("maxhp").toInt();
        if (fields.contains("hp"))
            player_hp[name] = fields.value("hp").toInt();
        player_start_draw[name] = fields.value("draw", "4").toInt();
        player_chained[name] = !fields.value("chained").isEmpty();
        player_turned[name] = !fields.value("turned").isEmpty();
        if (!fields.value("starter").isEmpty())
            starter = name;
        set_nationality[name] = !fields.value("nationality").isEmpty();
        assign_nationality[name] = fields.value("nationality");
        player_exskills[name] = fields.value("acquireSkills").split(',', Qt::SkipEmptyParts);
        for (const QString &mark : fields.value("marks").split(',', Qt::SkipEmptyParts)) {
            const QStringList pair = mark.split('*');
            player_marks[name][pair.first()] = pair.last().toInt();
            if (marks_ComboBox->findData(pair.first()) < 0)
                marks_ComboBox->addItem(QStringLiteral("%1 [%2]").arg(Sanguosha->translate(pair.first()), pair.first()), pair.first());
        }
        for (const QString &id : fields.value("equip").split(',', Qt::SkipEmptyParts))
            player_equips[name] << id.toInt();
        for (const QString &id : fields.value("hand").split(',', Qt::SkipEmptyParts))
            player_handcards[name] << id.toInt();
        for (const QString &id : fields.value("judge").split(',', Qt::SkipEmptyParts))
            player_judges[name] << id.toInt();
        const QString pileWinner = fields.value("endedByPile"), turnWinner = fields.value("singleTurn"),
            nextWinner = fields.value("beforeNext");
        if (!pileWinner.isEmpty()) {
            is_ended_by_pile = true;
            ended_by_pile_box->setCurrentIndex(ended_by_pile_box->findData(pileWinner));
        }
        if (!turnWinner.isEmpty()) {
            is_single_turn = true;
            single_turn_box->setCurrentIndex(single_turn_box->findData(turnWinner));
        }
        if (!nextWinner.isEmpty()) {
            is_before_next = true;
            before_next_box->setCurrentIndex(before_next_box->findData(nextWinner));
        }
        player_extra_fields[name] = fields;
        for (const QString &key : editedFields)
            player_extra_fields[name].remove(key);
    }
    random_roles_box->setChecked(parsedOptions.contains(MiniSceneRule::S_EXTRA_OPTION_RANDOM_ROLES));
    rest_in_DP_box->setChecked(parsedOptions.contains(MiniSceneRule::S_EXTRA_OPTION_REST_IN_DISCARD_PILE));
    updateListItems();
    num_ComboBox->setCurrentIndex(parsedPlayers.size() - 2);
    updateNumber(parsedPlayers.size() - 2);
    list->setCurrentRow(0);
    on_list_itemSelectionChanged(list->currentItem());
    updatePileInfo();
    checkEndedByPileBox(is_ended_by_pile);
    checkBeforeNextBox(is_single_turn);
    checkSingleTurnBox(is_before_next);
}

bool CustomAssignDialog::save(QString path)
{
    QSet<QString> activePlayers;
    QSet<int> assignedCards;
    QSet<QString> camps;
    int lords = 0;
    QStringList rows;
    auto fail = [this](const QString &reason) {
        QMessageBox::warning(this, tr("Warning"), reason);
        return false;
    };
    auto reserveCards = [&assignedCards](const QList<int> &cards) {
        for (int id : cards) {
            if (id < 0 || id >= Sanguosha->getCardCount() || !Sanguosha->getEngineCard(id) || assignedCards.contains(id))
                return false;
            assignedCards.insert(id);
        }
        return true;
    };
    if (!reserveCards(set_pile))
        return fail(tr("Invalid or duplicate draw-pile card."));
    for (int row = 0; row < list->count(); ++row) {
        const QString name = list->item(row)->data(Qt::UserRole).toString();
        activePlayers.insert(name);
        const QString role = role_mapping.value(name);
        if (!QStringList({"lord", "loyalist", "rebel", "renegade"}).contains(role))
            return fail(tr("%1's role cannot be unknown").arg(Sanguosha->translate(name)));
        lords += role == "lord" ? 1 : 0;
        camps.insert(role == "lord" ? "loyalist" : role);
        const QString general = free_choose_general.value(name) ? "select" : general_mapping.value(name);
        const QString general2 = free_choose_general2.value(name) ? "select" : general2_mapping.value(name);
        if (general.isEmpty() || (general != "select" && !Sanguosha->getGeneral(general)))
            return fail(tr("%1's general cannot be empty").arg(Sanguosha->translate(name)));
        if (!general2.isEmpty() && general2 != "select" && !Sanguosha->getGeneral(general2))
            return fail(tr("Unknown general: %1").arg(general2));
        const QMap<QString, QString> fields = player_extra_fields.value(name);
        const int maxHp = player_maxhp.value(name);
        const int adjustedMaxHp = maxHp + fields.value("hpadj").toInt();
        if (maxHp > 0 && (adjustedMaxHp < 1 || player_hp.value(name) > adjustedMaxHp))
            return fail(tr("HP exceeds the configured maximum."));
        if (!sceneEquipmentFits(player_equips.value(name), fields))
            return fail(tr("%1: equipment exceeds the available slots.").arg(Sanguosha->translate(name)));
        if ((fields.value("judgeArea") == "0" || fields.value("judgeArea") == "false") && !player_judges.value(name).isEmpty())
            return fail(tr("%1: the abolished judging area still has cards.").arg(Sanguosha->translate(name)));
        for (int id : player_judges.value(name)) {
            const Card *card = id >= 0 && id < Sanguosha->getCardCount() ? Sanguosha->getEngineCard(id) : nullptr;
            if (!card || !card->isKindOf("DelayedTrick"))
                return fail(tr("Invalid or duplicate card in %1.").arg(tr("Judges")));
        }
        if (!reserveCards(player_equips.value(name)) || !reserveCards(player_handcards.value(name))
            || !reserveCards(player_judges.value(name)))
            return fail(tr("%1: a card is assigned more than once or is unavailable.").arg(Sanguosha->translate(name)));

        QStringList parts;
        parts << "general:" + general;
        if (!general2.isEmpty())
            parts << "general2:" + general2;
        parts << "role:" + role;
        if (starter == name)
            parts << "starter:true";
        QStringList marks;
        const QMap<QString, int> playerMarks = player_marks.value(name);
        for (auto it = playerMarks.cbegin(); it != playerMarks.cend(); ++it) {
            if (!it.key().isEmpty())
                marks << QStringLiteral("%1*%2").arg(it.key()).arg(it.value());
        }
        if (!marks.isEmpty())
            parts << "marks:" + marks.join(',');
        if (maxHp > 0)
            parts << QStringLiteral("maxhp:%1").arg(maxHp);
        if (player_hp.value(name) > 0)
            parts << QStringLiteral("hp:%1").arg(player_hp.value(name));
        if (player_turned.value(name))
            parts << "turned:true";
        if (player_chained.value(name))
            parts << "chained:true";
        if (set_nationality.value(name))
            parts << "nationality:" + assign_nationality.value(name);
        if (!player_exskills.value(name).isEmpty())
            parts << "acquireSkills:" + player_exskills.value(name).join(',');
        if (player_start_draw.value(name, 4) != 4)
            parts << QStringLiteral("draw:%1").arg(player_start_draw.value(name));
        auto appendCards = [&parts](const QString &key, const QList<int> &cards) {
            if (cards.isEmpty())
                return;
            QStringList ids;
            for (int id : cards)
                ids << QString::number(id);
            parts << key + ':' + ids.join(',');
        };
        appendCards("equip", player_equips.value(name));
        appendCards("hand", player_handcards.value(name));
        appendCards("judge", player_judges.value(name));
        for (auto it = fields.cbegin(); it != fields.cend(); ++it)
            parts << it.key() + ':' + it.value();
        if (row == 0) {
            if (is_ended_by_pile)
                parts << "endedByPile:" + ended_by_pile_box->currentData().toString();
            if (is_single_turn)
                parts << "singleTurn:" + single_turn_box->currentData().toString();
            else if (is_before_next)
                parts << "beforeNext:" + before_next_box->currentData().toString();
        }
        rows << parts.join(' ');
    }
    if (starter.isEmpty() || !activePlayers.contains(starter))
        return fail(tr("There is not a starter"));
    if (lords > 1)
        return fail(tr("Two many lords in the game"));
    if (camps.size() < 2)
        return fail(tr("No different camps in the game"));
    if (is_ended_by_pile && set_pile.isEmpty())
        return fail(tr("The draw-pile ending rule needs a configured pile."));

    QStringList document;
    QStringList options = extra_options;
    if (random_roles_box->isChecked())
        options << MiniSceneRule::S_EXTRA_OPTION_RANDOM_ROLES;
    if (rest_in_DP_box->isChecked())
        options << MiniSceneRule::S_EXTRA_OPTION_REST_IN_DISCARD_PILE;
    if (!options.isEmpty())
        document << "extraOptions:" + options.join(' ');
    if (!set_pile.isEmpty()) {
        QStringList ids;
        // The existing format stores the fixed pile in reverse display order.
        for (int i = set_pile.size() - 1; i >= 0; --i)
            ids << QString::number(set_pile.at(i));
        document << "setPile:" + ids.join(',');
    }
    document << rows;
    if (path.isEmpty())
        path = QFileDialog::getSaveFileName(this, tr("Save mini scenario settings"),
            QSanRuntimePaths::customSceneDir(), tr("Mini scenario settings (*.txt)"));
    if (path.isEmpty())
        return false;
    // A failed write must leave the previously saved scene intact.
    QSaveFile file(path);
    const QByteArray bytes = (document.join('\n') + '\n').toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return fail(tr("Cannot save scene: %1").arg(file.errorString()));
    return true;
}

//---------------------------------------

GeneralAssignDialog::GeneralAssignDialog(QWidget *parent, bool can_ban)
 : QDialog(parent)
{
    setWindowTitle(tr("Mini choose generals"));

    tab_widget = new QTabWidget;
    input_general = new QLineEdit;
    input_general->setPlaceholderText(tr("Search generals by name, ID or package"));

    group = new QButtonGroup(this);
    group->setExclusive(true);

    QMap<QString, QList<const General *> > map;
    const QList<const General *> all_generals = Sanguosha->findChildren<const General *>();
    foreach(const General *general, all_generals) {
        foreach (QString kingd, general->getKingdoms().split("+"))
            map[kingd] << general;
    }

    foreach (QString kingdom, Sanguosha->getKingdoms()) {
		if(map[kingdom].isEmpty()) continue;

		QWidget *tab = createTab(map[kingdom]);

		QScrollArea *scrollArea = new QScrollArea(this);
		scrollArea->setBackgroundRole(QPalette::Light);
		scrollArea->setFrameShape(QFrame::NoFrame);
		scrollArea->setWidget(tab);
		scrollArea->setMinimumSize(0, 0);
		scrollArea->setWidgetResizable(true);
		scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);//原本隐藏水平滚动条；小視窗允許捲動，避免截斷。

		tab_widget->addTab(scrollArea, QIcon(G_ROOM_SKIN.getPixmap(QSanRoomSkin::S_SKIN_KEY_KINGDOM_ICON, kingdom)),
						Sanguosha->translate(kingdom));
    }

    ok_button = new QPushButton(tr("OK"));
    connect(ok_button, SIGNAL(clicked()), this, SLOT(chooseGeneral()));

    QPushButton *cancel_button = new QPushButton(tr("Cancel"));
    connect(cancel_button, SIGNAL(clicked()), this, SLOT(reject()));

    QHBoxLayout *button_layout = new QHBoxLayout;
    if (can_ban) {
        QPushButton *clear_button = new QPushButton(tr("Clear General"));
        connect(clear_button, SIGNAL(clicked()), this, SLOT(clearGeneral()));

        button_layout->addWidget(clear_button);
    }

    button_layout->addStretch();
    button_layout->addWidget(ok_button);
    button_layout->addWidget(cancel_button);

    QVBoxLayout *layout = new QVBoxLayout;
    layout->addWidget(input_general);
    layout->addWidget(tab_widget);
    layout->addLayout(button_layout);

    setLayout(layout);

    connect(input_general, SIGNAL(textChanged(QString)), this, SLOT(filterGenerals()));
    connect(tab_widget, SIGNAL(currentChanged(int)), this, SLOT(filterGenerals()));
    filterGenerals();
    resize(900, 600);
}

QWidget *GeneralAssignDialog::createTab(const QList<const General *> &generals)
{
    QWidget *tab = new QWidget;

    QGridLayout *layout = new QGridLayout;
    layout->setOriginCorner(Qt::TopLeftCorner);
    static QIcon lord_icon("image/system/roles/lord.png");

    const int columns = 5;

    for (int i = 0; i < generals.length(); i++) {
        if (generals[i]->isTotallyHidden())
            continue;

        QString package_name = Sanguosha->translate(generals[i]->getPackage());
        QAbstractButton *button = new QRadioButton(QString("%1[%2]").arg(generals[i]->getBriefName()).arg(package_name));
        button->setToolTip(buildOracleTooltip(generals[i]->getOracleText(), generals[i]->getSkillDescription(true)));
        if (generals[i]->isLord()) button->setIcon(lord_icon);
        button->setObjectName(generals[i]->objectName());
        button->setProperty("searchText", QString("%1 %2 %3 %4")
            .arg(generals[i]->getBriefName(), generals[i]->objectName(),
                 generals[i]->getPackage(), package_name));

        group->addButton(button);

        int row = i / columns;
        int column = i % columns;
        layout->addWidget(button, row, column);
    }

    tab->setLayout(layout);
    return tab;
}

void GeneralAssignDialog::filterGenerals()
{
    const QString filter = input_general->text().trimmed();
    QAbstractButton *first_visible = nullptr;
    QAbstractButton *first_current = nullptr;
    foreach (QAbstractButton *button, group->buttons()) {
        const bool visible = filter.isEmpty() || button->property("searchText").toString()
            .contains(filter, Qt::CaseInsensitive);
        button->setVisible(visible);
        if (visible && !first_visible)
            first_visible = button;
        if (visible && !first_current && tab_widget->currentWidget()->isAncestorOf(button))
            first_current = button;
    }
    ok_button->setEnabled(first_visible != nullptr);
    if (!first_visible)
        return;
    if (!first_current) {
        const QSignalBlocker tabBlock(tab_widget);
        for (int index = 0; index < tab_widget->count(); ++index) {
            if (tab_widget->widget(index)->isAncestorOf(first_visible)) {
                tab_widget->setCurrentIndex(index);
                break;
            }
        }
        first_current = first_visible;
    }
    QAbstractButton *checked = group->checkedButton();
    if (!checked || checked->isHidden() || !tab_widget->currentWidget()->isAncestorOf(checked))
        first_current->click();
}

void GeneralAssignDialog::chooseGeneral()
{
    QAbstractButton *button = group->checkedButton();
    if (!button || button->isHidden() || !ok_button->isEnabled())
        return;
    emit general_chosen(button->objectName());
    this->reject();
}

void GeneralAssignDialog::clearGeneral()
{
    emit general_cleared();
    this->reject();
}

//------------------------------

CardAssignDialog::CardAssignDialog(QWidget *parent, QString card_type, QString class_name, QList<int> excluded)
    : QDialog(parent), input_card(nullptr), get_card_button(nullptr),
    card_type(card_type), class_name(class_name),
    excluded_card(excluded)
{
    setWindowTitle(tr("Custom Card Chosen"));
    QVBoxLayout *vlayout = new QVBoxLayout;
    card_list = new QListWidget;
    input_card = new QLineEdit;
    input_card->setPlaceholderText(tr("Search cards by name, ID or package"));

    updateCardList();

    get_card_button = new QPushButton(tr("Get card"));
    get_card_button->setEnabled(false);
    QPushButton *back = new QPushButton(tr("Back"));

    vlayout->addWidget(get_card_button);
    vlayout->addWidget(back);

    QHBoxLayout *layout = new QHBoxLayout;
    QVBoxLayout *list_layout = new QVBoxLayout;
    list_layout->addWidget(input_card);
    list_layout->addWidget(card_list);
    layout->addLayout(list_layout);
    layout->addLayout(vlayout);
    QVBoxLayout *mainlayout = new QVBoxLayout;
    mainlayout->addLayout(layout);
    setLayout(mainlayout);

    connect(back, SIGNAL(clicked()), this, SLOT(reject()));
    connect(get_card_button, SIGNAL(clicked()), this, SLOT(askCard()));
    connect(card_list, SIGNAL(itemSelectionChanged()), this, SLOT(updateCardButton()));
    connect(input_card, SIGNAL(textChanged(QString)), this, SLOT(filterCards()));
    connect(parent, SIGNAL(card_addin(int)), this, SLOT(updateExcluded(int)));
    updateCardButton();
}

void CardAssignDialog::addCard(const Card *card)
{
    QString name = Sanguosha->translate(card->objectName());
    QIcon suit_icon = QIcon(QString("image/system/cardsuit/%1.png").arg(card->getSuitString()));
    QString point = card->getNumberString();

    QString card_info = point + "  " + name + "\t\t" + Sanguosha->translate(card->getSubtype());
    QListWidgetItem *name_item = new QListWidgetItem(card_info, card_list);
    name_item->setIcon(suit_icon);
    name_item->setData(Qt::UserRole, card->getId());
    name_item->setData(Qt::UserRole + 1, QString("%1 %2 %3 %4")
        .arg(name, card->objectName(), card->getPackage()).arg(card->getId()));
}

void CardAssignDialog::askCard()
{
    QListWidgetItem *card_item = card_list->currentItem();
    if (!card_item || card_item->isHidden())
        return;
    int card_id = card_item->data(Qt::UserRole).toInt();
    emit card_chosen(card_id);

    int row = card_list->currentRow();
    int id = card_list->item(row)->data(Qt::UserRole).toInt();
    excluded_card << id;
    updateCardList();
    card_list->setCurrentRow(row >= card_list->count() ? row - 1 : row);
    filterCards();
    updateCardButton();
}

void CardAssignDialog::updateExcluded(int card_id)
{
    excluded_card.removeOne(card_id);
}

void CardAssignDialog::updateCardList()
{
    card_list->clear();

    int n = Sanguosha->getCardCount();
    QList<const Card *> reasonable_cards;
    if (!card_type.isEmpty() || !class_name.isEmpty()) {
        for (int i = 0; i < n; i++) {
            if (excluded_card.contains(i))
                continue;

            const Card *card = Sanguosha->getEngineCard(i);
            if (Config.BanPackages.contains(card->getPackage()) || card->objectName().startsWith("__"))
                continue;
            if (card->getType() == card_type || card->isKindOf(class_name.toStdString().c_str()))
                reasonable_cards << card;
        }
    } else {
        for (int i = 0; i < n; i++) {
            if (excluded_card.contains(i))
                continue;

            const Card *card = Sanguosha->getEngineCard(i);
            if (Config.BanPackages.contains(card->getPackage()) || card->objectName().startsWith("__"))
                continue;
            reasonable_cards << card;
        }
    }

    for (int i = 0; i < reasonable_cards.length(); i++)
        addCard(reasonable_cards.at(i));

    if (reasonable_cards.length() > 0)
        card_list->setCurrentRow(0);
    filterCards();
    updateCardButton();
}

void CardAssignDialog::updateCardButton()
{
    if (get_card_button)
        get_card_button->setEnabled(card_list->currentItem() != nullptr
            && !card_list->currentItem()->isHidden());
}

void CardAssignDialog::filterCards()
{
    const QString filter = input_card->text().trimmed();
    for (int i = 0; i < card_list->count(); ++i) {
        QListWidgetItem *item = card_list->item(i);
        item->setHidden(!filter.isEmpty() && !item->data(Qt::UserRole + 1).toString()
            .contains(filter, Qt::CaseInsensitive));
    }
    if (card_list->currentItem() && card_list->currentItem()->isHidden()) {
        card_list->clearSelection();
        card_list->setCurrentRow(-1);
    }
    if (!card_list->currentItem()) {
        for (int i = 0; i < card_list->count(); ++i) {
            if (!card_list->item(i)->isHidden()) {
                card_list->setCurrentRow(i);
                break;
            }
        }
    }
    updateCardButton();
}

//-----------------------------------

SkillAssignDialog::SkillAssignDialog(QDialog *parent, QString player_name, QStringList &player_skills)
    : QDialog(parent), update_skills(player_skills)
{
    setWindowTitle(tr("Skill Chosen"));
    QHBoxLayout *layout = new QHBoxLayout;
    skill_list = new QListWidget;

    input_skill = new QLineEdit;
    input_skill->setPlaceholderText(tr("Search skills by name or ID"));
    input_skill->setToolTip(tr("Choose a completion to add a skill; typing also filters assigned skills."));

    QStringList skillChoices;
    for (const QString &id : Sanguosha->getSkillNames()) {
        const QString label = QStringLiteral("%1 [%2]").arg(Sanguosha->translate(id), id);
        skill_lookup[label] = id;
        skillChoices << label;
    }
    QCompleter *completer = new QCompleter(skillChoices, input_skill);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    input_skill->setCompleter(completer);

    QPushButton *add_skill = new QPushButton(tr("Add Skill"));
    add_skill->setObjectName("inline_add");

    select_skill = new QPushButton(tr("Select Skill from Generals"));
    delete_skill = new QPushButton(tr("Delete Current Skill"));

    QPushButton *ok_button = new QPushButton(tr("OK"));
    QPushButton *cancel_button = new QPushButton(tr("Cancel"));

    skill_info = new QTextEdit;
    skill_info->setReadOnly(true);

    updateSkillList();

    QVBoxLayout *vlayout = new QVBoxLayout;
    vlayout->addWidget(new QLabel(Sanguosha->translate(player_name)));
    vlayout->addWidget(skill_list);
    layout->addLayout(vlayout);
    QVBoxLayout *sided_lay = new QVBoxLayout;
    sided_lay->addWidget(skill_info);
    sided_lay->addStretch();
    sided_lay->addLayout(HLay(input_skill, add_skill));
    sided_lay->addLayout(HLay(select_skill, delete_skill));
    sided_lay->addLayout(HLay(ok_button, cancel_button));
    layout->addLayout(sided_lay);
    QVBoxLayout *mainlayout = new QVBoxLayout;
    mainlayout->addLayout(layout);
    setLayout(mainlayout);

    connect(add_skill, SIGNAL(clicked()), this, SLOT(addSkill()));
    connect(select_skill, SIGNAL(clicked()), this, SLOT(selectSkill()));
    connect(delete_skill, SIGNAL(clicked()), this, SLOT(deleteSkill()));
    connect(skill_list, SIGNAL(itemSelectionChanged()), this, SLOT(changeSkillInfo()));
    connect(input_skill, SIGNAL(textChanged(QString)), this, SLOT(filterSkills()));
    connect(ok_button, SIGNAL(clicked()), this, SLOT(accept()));
    connect(cancel_button, SIGNAL(clicked()), this, SLOT(reject()));
}

void SkillAssignDialog::changeSkillInfo()
{
    QListWidgetItem *item = skill_list->currentItem();
    skill_info->clear();
    if (!item)
        return;

    QString skill_name = item->data(Qt::UserRole).toString();

    skill_info->setText(Sanguosha->translate(":" + skill_name));
}

void SkillAssignDialog::selectSkill()
{
    GeneralAssignDialog dialog(this);
    connect(&dialog, SIGNAL(general_chosen(QString)), this, SLOT(getSkillFromGeneral(QString)));
    dialog.exec();
}

void SkillAssignDialog::deleteSkill()
{
    QListWidgetItem *item = skill_list->currentItem();
    if (!item || item->isHidden())
        return;
    QString skill_name = item->data(Qt::UserRole).toString();
    update_skills.removeOne(skill_name);

    updateSkillList();
}

void SkillAssignDialog::getSkillFromGeneral(QString general_name)
{
    const General *general = Sanguosha->getGeneral(general_name);
    if (!general)
        return;
    QDialog select_dialog(this);
    select_dialog.setWindowTitle(tr("Skill Chosen"));
    QVBoxLayout *layout = new QVBoxLayout;
    foreach (const Skill *skill, general->getVisibleSkillList()) {
        QCommandLinkButton *button = new QCommandLinkButton;
        button->setObjectName(skill->objectName());
        button->setText(Sanguosha->translate(skill->objectName()));
        button->setToolTip(buildOracleTooltip(skill->getOracleText(), Sanguosha->translate(":" + skill->objectName())));

        connect(button, SIGNAL(clicked()), &select_dialog, SLOT(accept()));
        connect(button, SIGNAL(clicked()), this, SLOT(addSkill()));

        layout->addWidget(button);
    }

    select_dialog.setLayout(layout);
    select_dialog.exec();
}

void SkillAssignDialog::addSkill()
{
    QString name = sender()->objectName();
    if (name == "inline_add") {
        name = input_skill->text().trimmed();
        name = skill_lookup.value(name, name);

        const Skill *skill = Sanguosha->getSkill(name);
        if (skill == nullptr) {
            QMessageBox::warning(this, tr("Warning"), tr("There is no skill that internal name is %1").arg(name));
            return;
        }
    }

    if (!update_skills.contains(name)) {
        update_skills << name;
        updateSkillList();
    }

    input_skill->clear();
}

void SkillAssignDialog::updateSkillList()
{
    int index = skill_list->count() > 0 ? skill_list->currentRow() : 0;

    skill_list->clear();
    skill_info->clear();

    foreach (QString skill_name, update_skills) {
        if (Sanguosha->getSkill(skill_name) != nullptr) {
            QListWidgetItem *item = new QListWidgetItem(Sanguosha->translate(skill_name));
            item->setData(Qt::UserRole, skill_name);
            skill_list->addItem(item);
        }
    }
    skill_list->setCurrentRow(index >= skill_list->count() ? skill_list->count() - 1 : index);

    if (skill_list->count() > 0) {
        changeSkillInfo();
        delete_skill->setEnabled(true);
    } else
        delete_skill->setEnabled(false);

    filterSkills();
}

void SkillAssignDialog::filterSkills()
{
    const QString input = input_skill->text().trimmed();
    const QString filter = skill_lookup.value(input, input);
    QListWidgetItem *firstVisible = nullptr;
    for (int i = 0; i < skill_list->count(); ++i) {
        QListWidgetItem *item = skill_list->item(i);
        const QString name = item->data(Qt::UserRole).toString();
        item->setHidden(!filter.isEmpty() && !name.contains(filter, Qt::CaseInsensitive)
            && !item->text().contains(filter, Qt::CaseInsensitive));
        if (!item->isHidden() && !firstVisible)
            firstVisible = item;
    }
    if (!skill_list->currentItem() || skill_list->currentItem()->isHidden())
        skill_list->setCurrentItem(firstVisible);
    delete_skill->setEnabled(firstVisible != nullptr);
    changeSkillInfo();
}

void SkillAssignDialog::accept()
{
    emit skill_update(update_skills);
    QDialog::accept();
}

