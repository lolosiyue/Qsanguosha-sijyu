#include "roleassigndialog.h"

//#include "general.h"
//#include "player.h"
//#include "client.h"
#include "engine.h"
#include "roomscene.h"
#include "settings.h"
//#include "clientplayer.h"
#include "clientstruct.h"

using namespace QSanProtocol;

RoleAssignDialog::RoleAssignDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Assign roles and seats"));

    list = new QListWidget;
    list->setFlow(QListView::TopToBottom);
    list->setMovement(QListView::Static);

    QStringList role_list = Sanguosha->getRoleList(Config.GameMode);

    if (Config.FreeAssignSelf) {
        QString text = QString("%1[%2]").arg(Self->screenName()).arg(Sanguosha->translate(role_list.first()));

        QListWidgetItem *item = new QListWidgetItem(text, list);
        item->setData(Qt::UserRole, Self->objectName());

        role_mapping.insert(Self->objectName(), "lord");
    } else {
        QList<const ClientPlayer *> players = ClientInstance->getPlayers();
        for (int i = 0; i < players.length(); i++) {
            QString text = QString("%1[%2]").arg(players[i]->screenName()).arg(Sanguosha->translate(role_list[i]));

            QListWidgetItem *item = new QListWidgetItem(text, list);
            item->setData(Qt::UserRole, players[i]->objectName());

            role_mapping.insert(players[i]->objectName(), role_list[i]);
        }
    }

    QVBoxLayout *vlayout = new QVBoxLayout;

    role_ComboBox = new QComboBox;/*
	QString roles = Sanguosha->getRoles(Config.GameMode);
	if (roles.contains("Z"))
		role_ComboBox->addItem(tr("Lord"), "lord");
	if (roles.contains("C"))
		role_ComboBox->addItem(tr("Loyalist"), "loyalist");
	if (roles.contains("N"))
		role_ComboBox->addItem(tr("Renegade"), "renegade");
	if (roles.contains("F"))
		role_ComboBox->addItem(tr("Rebel"), "rebel");*/
	foreach (QString r, role_list)
		role_ComboBox->addItem(Sanguosha->translate(r), r);

    QPushButton *moveUpButton = new QPushButton(tr("Move up"));
    QPushButton *moveDownButton = new QPushButton(tr("Move down"));
    QPushButton *okButton = new QPushButton(tr("OK"));
    QPushButton *cancelButton = new QPushButton(tr("Cancel"));

    if (Config.FreeAssignSelf) {
        moveUpButton->setEnabled(false);
        moveDownButton->setEnabled(false);
    }

    vlayout->addWidget(role_ComboBox);
    vlayout->addWidget(moveUpButton);
    vlayout->addWidget(moveDownButton);
    vlayout->addStretch();
    vlayout->addWidget(okButton);
    vlayout->addWidget(cancelButton);

    QHBoxLayout *layout = new QHBoxLayout();
    layout->addWidget(list);
    layout->addLayout(vlayout);
    QVBoxLayout *mainlayout = new QVBoxLayout();
    mainlayout->addLayout(layout);
    setLayout(mainlayout);

    connect(role_ComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(updateRole(int)));
    connect(list, SIGNAL(currentItemChanged(QListWidgetItem *, QListWidgetItem *)),
        this, SLOT(updateRole(QListWidgetItem *)));
    connect(moveUpButton, SIGNAL(clicked()), this, SLOT(moveUp()));
    connect(moveDownButton, SIGNAL(clicked()), this, SLOT(moveDown()));
    connect(okButton, SIGNAL(clicked()), this, SLOT(accept()));
    connect(cancelButton, SIGNAL(clicked()), this, SLOT(reject()));
}

void RoleAssignDialog::accept()
{
    QStringList real_list,role_list = Sanguosha->getRoleList(Config.GameMode);

    QList<QString> roles,names;
    if (Config.FreeAssignSelf) {
        QString name = list->item(0)->data(Qt::UserRole).toString();
        names.push_back(name);
        roles.push_back(role_mapping.value(name));
        ClientInstance->onPlayerAssignRole(names, roles);
        QDialog::accept();
        return;
    }

    for (int i = 0; i < list->count(); i++) {
        QString name = list->item(i)->data(Qt::UserRole).toString();

        /*if (i == 0 && role_mapping.value(name) != "lord") {
            QMessageBox::warning(this, tr("Warning"), tr("The first assigned role must be lord!"));
            return;
        }*/

        real_list << role_mapping.value(name);
        names.push_back(name);
        roles.push_back(role_mapping.value(name));
    }

    role_list.sort();
    real_list.sort();

    if (role_list == real_list) {
        ClientInstance->onPlayerAssignRole(names, roles);
        QDialog::accept();
    } else {
        QMessageBox::warning(this, tr("Warning"),
            tr("The roles that you assigned do not comform with the current game mode"));
    }
}

void RoleAssignDialog::reject()
{
    ClientInstance->replyToServer(S_COMMAND_CHOOSE_ROLE);
    QDialog::reject();
}

void RoleAssignDialog::updateRole(int index)
{
    QString name = list->currentItem()->data(Qt::UserRole).toString();
    QString role = role_ComboBox->itemData(index).toString();
    ClientPlayer *player = ClientInstance->getPlayer(name);
    QString text = QString("%1[%2]").arg(player->screenName()).arg(Sanguosha->translate(role));
    list->currentItem()->setText(text);
    role_mapping[name] = role;
}

void RoleAssignDialog::updateRole(QListWidgetItem *current)
{
    static QMap<QString, int> mapping;
    if (mapping.isEmpty()) {
        mapping["lord"] = 0;
        mapping["loyalist"] = 1;
        mapping["renegade"] = 2;
        mapping["rebel"] = 3;
    }

    QString name = current->data(Qt::UserRole).toString();
    QString role = role_mapping.value(name);
    int index = mapping.value(role);
    role_ComboBox->setCurrentIndex(index);
}

void RoleAssignDialog::moveUp()
{
    int index = list->currentRow();
    QListWidgetItem *item = list->takeItem(index);
    list->insertItem(index - 1, item);
    list->setCurrentItem(item);
}

void RoleAssignDialog::moveDown()
{
    int index = list->currentRow();
    QListWidgetItem *item = list->takeItem(index);
    list->insertItem(index + 1, item);
    list->setCurrentItem(item);
}

void RoomScene::startAssign()
{
    RoleAssignDialog *dialog = new RoleAssignDialog(main_window);
    dialog->exec();
}

