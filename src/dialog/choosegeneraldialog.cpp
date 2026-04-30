#include "choosegeneraldialog.h"
//#include "general.h"
#include "engine.h"
#include "oracle_helper.h"
#include "client.h"
//#include "settings.h"
//#include "protocol.h"
#include "skin-bank.h"
//#include "clientplayer.h"
#include "clientstruct.h"
#include "timed-progressbar.h"

using namespace QSanProtocol;

OptionButton::OptionButton(QString icon_path, const QString &caption, QWidget *parent)
    : QToolButton(parent)
{
    QPixmap pixmap(icon_path);
    QIcon icon(pixmap);

    setIcon(icon);
    setIconSize(pixmap.size());

    if (!caption.isEmpty()) {
        setText(caption);
        setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

        QFont font = Config.SmallFont;
        font.setPixelSize(Config.SmallFont.pixelSize() - 8);
        setFont(font);
    }
}

void OptionButton::mouseDoubleClickEvent(QMouseEvent *)
{
    emit double_clicked();
}

ChooseGeneralDialog::ChooseGeneralDialog(const QStringList &general_names, QWidget *parent, bool view_only, const QString &title)
    : QDialog(parent)
{
    m_freeChooseDialog = nullptr;
    if (title.isEmpty())
        setWindowTitle(tr("Choose general"));
    else
        setWindowTitle(title);

    QString lord_name;
    QList<const General *> generals;
    foreach (QString general_name, general_names) {
        if (general_name.contains("(lord)")) {
            general_name.chop(6);
            lord_name = general_name;
            if (Sanguosha->getGeneral(lord_name)->hasHideSkill())
                lord_name = "yinni_hide";
            continue;
        }
        generals << Sanguosha->getGeneral(general_name);
    }

    QSize icon_size;
    QSignalMapper *mapper = new QSignalMapper(this);
    bool tooManyManyGenerals = (generals.length() > G_COMMON_LAYOUT.m_chooseGeneralBoxNoIconThreshold);
    bool tooManyGenerals = (generals.length() > G_COMMON_LAYOUT.m_chooseGeneralBoxSwitchIconSizeThreshold);
    QSanRoomSkin::GeneralIconSize icon_type = QSanRoomSkin::S_GENERAL_ICON_SIZE_CARD;
    if (!tooManyManyGenerals){
        if (tooManyGenerals) {
            icon_type = QSanRoomSkin::S_GENERAL_ICON_SIZE_LARGE;
            icon_size = G_COMMON_LAYOUT.m_chooseGeneralBoxDenseIconSize;
        } else {
            icon_type = QSanRoomSkin::S_GENERAL_ICON_SIZE_CARD;
            icon_size = G_COMMON_LAYOUT.m_chooseGeneralBoxSparseIconSize;
        }
    }
    QList<OptionButton *> buttons;
    foreach (const General *general, generals) {
        OptionButton *button = new OptionButton("", Sanguosha->translate(general->objectName()));
        if (tooManyManyGenerals) {
            button->setIcon(QIcon("image/system/no-general-icon.png"));
            button->setIconSize(QSize(G_COMMON_LAYOUT.m_chooseGeneralBoxDenseIconSize.width(), 1));
        } else {
            button->setIcon(QIcon(G_ROOM_SKIN.getGeneralPixmap(general->objectName(), icon_type)));
            button->setIconSize(icon_size);
        }
        button->setToolTip(buildOracleTooltip(general->getOracleText(), general->getSkillDescription(true)));
        buttons << button;

        if (!view_only) {
            mapper->setMapping(button, general->objectName());
            connect(button, SIGNAL(double_clicked()), mapper, SLOT(map()));
            connect(button, SIGNAL(double_clicked()), this, SLOT(accept()));
        }
    }

    if (!view_only && ServerInfo.EnableHegemony && ServerInfo.Enable2ndGeneral && generals.length() > 2) {
        for (int i = 0; i < generals.length(); i++) {
            int party = 0;
            foreach(const General *other, generals)
                if (other->getKingdom() == generals.at(i)->getKingdom())
                    party++;
            if (party < 2)
                buttons.at(i)->setEnabled(false);
            else if (Self->getGeneral())
                buttons.at(i)->setEnabled(Self->getGeneral()!=generals.at(i)&&Self->getGeneral()->getKingdom()==generals.at(i)->getKingdom());
		}
    }

    QHBoxLayout *layout = new QHBoxLayout;
    const int columns = tooManyGenerals ? G_COMMON_LAYOUT.m_chooseGeneralBoxSwitchIconEachRowForTooManyGenerals :
        G_COMMON_LAYOUT.m_chooseGeneralBoxSwitchIconEachRow;
    if (generals.length() <= columns) {

        if (lord_name.size()>0 && !ServerInfo.EnableHegemony && !tooManyManyGenerals) {
            QLabel *label = new QLabel;
            label->setPixmap(G_ROOM_SKIN.getGeneralPixmap(lord_name, icon_type));
            const General *lord = Sanguosha->getGeneral(lord_name);
            label->setToolTip(buildOracleTooltip(lord ? lord->getOracleText() : QString(), lord ? lord->getSkillDescription(true) : QString()));
            layout->addWidget(label);
        }

        foreach(OptionButton *button, buttons)
            layout->addWidget(button);
    } else {
        QGridLayout *grid_layout = new QGridLayout;
        QVBoxLayout *lord_layout = new QVBoxLayout;

        if (lord_name.size()>0 && !ServerInfo.EnableHegemony && !tooManyManyGenerals) {
            QLabel *label = new QLabel;
            label->setPixmap(G_ROOM_SKIN.getCardMainPixmap(lord_name));
            const General *lord = Sanguosha->getGeneral(lord_name);
            label->setToolTip(buildOracleTooltip(lord ? lord->getOracleText() : QString(), lord ? lord->getSkillDescription(true) : QString()));
            lord_layout->addWidget(label);
        }
        lord_layout->addStretch();
        layout->addLayout(lord_layout);

        int columns_x = qMin(columns, (buttons.length() + 1) / 2);
        for (int i = 0; i < buttons.length(); i++) {
            int row = i / columns_x;
            int column = i % columns_x;
            grid_layout->addWidget(buttons.at(i), row, column);
        }
        layout->addLayout(grid_layout);
    }

    QStringList _generals;
    for (int i = 0; i < buttons.size(); i++) {
        if (buttons.at(i)->isEnabled())
            _generals << generals.at(i)->objectName();
    }
    QString default_name = generals.first()->objectName();
    if (!_generals.isEmpty())
        default_name = _generals.at(qrand() % _generals.length());

    if (!view_only) {
        mapper->setMapping(this, default_name);
        connect(this, SIGNAL(rejected()), mapper, SLOT(map()));

        connect(mapper, SIGNAL(mapped(QString)), ClientInstance, SLOT(onPlayerChooseGeneral(QString)));
    }

    QVBoxLayout *dialog_layout = new QVBoxLayout;
    dialog_layout->addLayout(layout);

    if (!view_only && !ServerInfo.EnableHegemony) {
        // role prompt
		default_name = QString("CAPITAL(%1)").arg(Self->getSeat());
        QLabel *role_label = new QLabel(tr("Your role is %1").arg(Sanguosha->translate(Self->getRole())));
        if (lord_name.size()>0)
			role_label->setText(tr("The lord has chosen %1. Your seat is %2. %3")
            .arg(Sanguosha->translate(lord_name))
            .arg(Sanguosha->translate(default_name))
            .arg(role_label->text()));
        else
            role_label->setText(tr("Your seat is %1. %2")
			.arg(Sanguosha->translate(default_name))
			.arg(role_label->text()));
        dialog_layout->addWidget(role_label);
    }

    // progress bar & free choose button
    QHBoxLayout *last_layout = new QHBoxLayout;
    if (view_only || ServerInfo.OperationTimeout <= 0) {
        progress_bar = nullptr;
    } else {
        progress_bar = new QSanCommandProgressBar();
        progress_bar->setFixedWidth(300);
        progress_bar->setTimerEnabled(true);
        progress_bar->setCountdown(S_COMMAND_CHOOSE_GENERAL);
        progress_bar->show();
        last_layout->addWidget(progress_bar);
    }

    bool free_choose = ServerInfo.FreeChoose
        || ServerInfo.GameMode.startsWith("_mini_") || ServerInfo.GameMode == "custom_scenario";

    if (!view_only && free_choose) {
        name_edit = new QLineEdit;
        name_edit->clear();
        QPushButton *free_choose_button = new QPushButton(tr("Free choose ..."));
        connect(free_choose_button, SIGNAL(clicked()), this, SLOT(freeChoose()));
        last_layout->addWidget(name_edit);
        last_layout->addWidget(free_choose_button);
    }

    last_layout->addStretch();

    if (last_layout->count() != 0)
        dialog_layout->addLayout(last_layout);

    setLayout(dialog_layout);
}

void ChooseGeneralDialog::done(int result)
{
    if (m_freeChooseDialog != nullptr) {
        // 安全釋放：將銷毀任務排入事件迴圈，確保 FreeChooseDialog 執行完所有程式碼後才真正死亡
        m_freeChooseDialog->deleteLater();
        m_freeChooseDialog = nullptr;
    }
    QDialog::done(result);
}

void ChooseGeneralDialog::freeChoose()
{
    QString name = name_edit->text();
    static QList<const General *> all_generals = Sanguosha->findChildren<const General *>();
    foreach (const General *general, all_generals) {
        if (general->isTotallyHidden()) continue;
        if (name.isEmpty() || general->objectName().contains(name) || Sanguosha->translate(general->objectName()).contains(name)) {
			QDialog *dialog = new FreeChooseDialog(name, this);

			connect(dialog, SIGNAL(accepted()), this, SLOT(accept()));
			connect(dialog, SIGNAL(general_chosen(QString)), ClientInstance, SLOT(onPlayerChooseGeneral(QString)));

			m_freeChooseDialog = dialog;

			dialog->exec();
            return;
        }
    }
	QMessageBox::warning(this, tr("Warning"), tr("No generals are found"));
}

FreeChooseDialog::FreeChooseDialog(const QString &name, QWidget *parent, ButtonGroupType type)
    : QDialog(parent), type(type)
{
    setWindowTitle(tr("Free choose generals"));

    QTabWidget *tab_widget = new QTabWidget;

    group = new QButtonGroup(this);
    group->setExclusive(type == Exclusive);

    QMap<QString, QList<const General *> > map;
    static QList<const General *> all_generals = Sanguosha->findChildren<const General *>();
    foreach (const General *general, all_generals) {
        if (general->isTotallyHidden())
            continue;

        if (name.isEmpty() || (general->objectName().contains(name) || Sanguosha->translate(general->objectName()).contains(name))) {
            foreach (QString kingd, general->getKingdoms().split("+"))
                map[kingd] << general;
        }
    }

    foreach (QString kingdom, Sanguosha->getKingdoms()) {
        if(map[kingdom].isEmpty()) continue;

		QWidget *tab = createTab(map[kingdom]);

		QScrollArea *scrollArea = new QScrollArea(this);
		scrollArea->setBackgroundRole(QPalette::Light);
		scrollArea->setFrameShape(QFrame::NoFrame);
		scrollArea->setWidget(tab);
		scrollArea->setMinimumSize(1500, 564);
		scrollArea->setWidgetResizable(true);
		scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);//隐藏水平滚动条

		tab_widget->addTab(scrollArea, QIcon(G_ROOM_SKIN.getPixmap(QSanRoomSkin::S_SKIN_KEY_KINGDOM_ICON, kingdom)),
						Sanguosha->translate(kingdom));
    }

    QPushButton *ok_button = new QPushButton(tr("OK"));
    connect(ok_button, SIGNAL(clicked()), this, SLOT(chooseGeneral()));

    QPushButton *cancel_button = new QPushButton(tr("Cancel"));
    connect(cancel_button, SIGNAL(clicked()), this, SLOT(reject()));

    QHBoxLayout *button_layout = new QHBoxLayout;
    button_layout->addStretch();
    button_layout->addWidget(ok_button);
    button_layout->addWidget(cancel_button);

    QVBoxLayout *layout = new QVBoxLayout;
    layout->addWidget(tab_widget);
    layout->addLayout(button_layout);

    setLayout(layout);

    if (type == Exclusive)
        group->buttons().first()->click();
}
void FreeChooseDialog::chooseGeneral()
{
    if (type == Pair) {
        QString first, second;
        foreach(QAbstractButton * button, group->buttons()) {
            if (!button->isChecked())
                continue;

            if (first.isEmpty())
                first = button->objectName();
            else {
                second = button->objectName();
                emit pair_chosen(first, second);
                break;
            }
        }
        if (second.isEmpty() && !first.isEmpty())
            emit general_chosen(first);

    }
    else if (type == Multi) {
        QStringList general_names;
        foreach(QAbstractButton * button, group->buttons()) {
            if (button->isChecked())
                general_names << button->objectName();
        }
        if (!general_names.isEmpty())
            emit general_chosen(general_names.join("+"));

    }
    else {
        QAbstractButton* button = group->checkedButton();
        if (button) emit general_chosen(button->objectName());
    }

    // 在修改了 done() 使用 deleteLater() 後，這裡的 accept() 就絕對安全了！
    accept();
}

void FreeChooseDialog::onAvatarHoverEnter()
{
	QAbstractButton *button = (QAbstractButton *)sender();
    const General *g = Sanguosha->getGeneral(button->objectName());
    button->setToolTip(buildOracleTooltip(g ? g->getOracleText() : QString(), g ? g->getSkillDescription(true) : QString()));
}

QWidget *FreeChooseDialog::createTab(const QList<const General *> &generals)
{
    QWidget *tab = new QWidget;

    QGridLayout *layout = new QGridLayout;
    layout->setOriginCorner(Qt::TopLeftCorner);
    static QIcon lord_icon("image/system/roles/lord.png");

    const int columns = 5;

    for (int i = 0; i < generals.length(); i++) {
        QString text = QString("%1[%2]").arg(generals[i]->getBriefName()).arg(Sanguosha->translate(generals[i]->getPackage()));

        QAbstractButton *button;
        if (type == Exclusive){
			button = new TextButtonItem(text);
		}else
			button = new QCheckBox(text);

        button->setObjectName(generals[i]->objectName());
        button->setToolTip(buildOracleTooltip(generals[i]->getOracleText(), generals[i]->getSkillDescription(true)));
		//connect(button, SIGNAL(hover_enter()), this, SLOT(onAvatarHoverEnter()));

        if (generals[i]->isLord())
			button->setIcon(lord_icon);

        group->addButton(button);

        int row = i / columns;
        int column = i % columns;
        layout->addWidget(button, row, column);
    }

    QVBoxLayout *layout2 = new QVBoxLayout;
    layout2->addStretch();

    QVBoxLayout *tablayout = new QVBoxLayout;
    tablayout->addLayout(layout);
    tablayout->addLayout(layout2);

    tab->setLayout(tablayout);

    if (type == Pair)
        connect(group, SIGNAL(buttonClicked(QAbstractButton *)), this, SLOT(uncheckExtraButton(QAbstractButton *)));

    return tab;
}

void FreeChooseDialog::uncheckExtraButton(QAbstractButton *click_button)
{
    QAbstractButton *first = nullptr;
    foreach (QAbstractButton *button, group->buttons()) {
        if (click_button==button||!button->isChecked())
            continue;

		if(first){
            first->setChecked(false);
            break;
		}else
            first = button;
    }
}

TextButtonItem::TextButtonItem(QString parent)
    : QRadioButton(parent)
{
    setAcceptDrops(true);
}

void TextButtonItem::hoverEnterEvent(QGraphicsSceneHoverEvent *)
{
    emit hover_enter();
}

void TextButtonItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *)
{
    emit hover_leave();
}