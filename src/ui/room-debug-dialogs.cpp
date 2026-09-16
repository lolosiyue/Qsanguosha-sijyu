#include "room-debug-dialogs.h"

#include "client.h"
#include "clientplayer.h"
#include "engine.h"
#include "protocol.h"
#include "skin-bank.h"
#include "structs.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTextEdit>
#include <QVBoxLayout>

#include <climits>

using namespace QSanProtocol;

ScriptExecutor::ScriptExecutor(QWidget*parent)
	: QDialog(parent)
{
	setWindowTitle(tr("Script execution"));
	QVBoxLayout*vlayout = new QVBoxLayout;
	vlayout->addWidget(new QLabel(tr("Please input the script that should be executed at server side:\n P = you,R = your room")));

	QTextEdit*box = new QTextEdit;
	box->setObjectName("scriptBox");
	vlayout->addWidget(box);

	QHBoxLayout*hlayout = new QHBoxLayout;
	hlayout->addStretch();

	QPushButton*ok_button = new QPushButton(tr("OK"));
	hlayout->addWidget(ok_button);

	vlayout->addLayout(hlayout);

	connect(ok_button,SIGNAL(clicked()),this,SLOT(accept()));
	connect(this,SIGNAL(accepted()),this,SLOT(doScript()));

	setLayout(vlayout);
}

void ScriptExecutor::doScript()
{
	QTextEdit*box = findChild<QTextEdit*>("scriptBox");
	if(box==nullptr) return;

	QString script = box->toPlainText();
	ClientInstance->requestCheatRunScript(script);
}

DeathNoteDialog::DeathNoteDialog(QWidget*parent)
	: QDialog(parent)
{
	setWindowTitle(tr("Death note"));

	killer = new QComboBox;
	RoomDebugDialogs::fillPlayerNames(killer,true);

	victim = new QComboBox;
	RoomDebugDialogs::fillPlayerNames(victim,false);

	QPushButton*ok_button = new QPushButton(tr("OK"));
	connect(ok_button,SIGNAL(clicked()),this,SLOT(accept()));

	QFormLayout*layout = new QFormLayout;
	layout->addRow(tr("Killer"),killer);
	layout->addRow(tr("Victim"),victim);

	QHBoxLayout*hlayout = new QHBoxLayout;
	hlayout->addStretch();
	hlayout->addWidget(ok_button);
	layout->addRow(hlayout);

	setLayout(layout);
}

void DeathNoteDialog::accept()
{
	QDialog::accept();
	ClientInstance->requestCheatKill(killer->itemData(killer->currentIndex()).toString(),
		victim->itemData(victim->currentIndex()).toString());
}

DamageMakerDialog::DamageMakerDialog(QWidget*parent)
	: QDialog(parent)
{
	setWindowTitle(tr("Damage maker"));

	damage_source = new QComboBox;
	RoomDebugDialogs::fillPlayerNames(damage_source,true);

	damage_target = new QComboBox;
	RoomDebugDialogs::fillPlayerNames(damage_target,false);

	damage_nature = new QComboBox;
	damage_nature->addItem(tr("Normal"),S_CHEAT_NORMAL_DAMAGE);
	damage_nature->addItem(tr("Thunder"),S_CHEAT_THUNDER_DAMAGE);
	damage_nature->addItem(tr("Fire"),S_CHEAT_FIRE_DAMAGE);
	damage_nature->addItem(tr("Ice"),S_CHEAT_ICE_DAMAGE);
	damage_nature->addItem(tr("God"),S_CHEAT_GOD_DAMAGE);
	damage_nature->addItem(tr("Recover HP"),S_CHEAT_HP_RECOVER);
	damage_nature->addItem(tr("Lose HP"),S_CHEAT_HP_LOSE);
	damage_nature->addItem(tr("Lose Max HP"),S_CHEAT_MAX_HP_LOSE);
	damage_nature->addItem(tr("Reset Max HP"),S_CHEAT_MAX_HP_RESET);
	damage_nature->addItem(tr("Gain Hujia"),S_CHEAT_HUJIA_GET);
	damage_nature->addItem(tr("Lose Hujia"),S_CHEAT_HUJIA_LOSE);

	damage_point = new QSpinBox;
	damage_point->setRange(1,INT_MAX);
	damage_point->setValue(1);

	QPushButton*ok_button = new QPushButton(tr("OK"));
	connect(ok_button,SIGNAL(clicked()),this,SLOT(accept()));
	QHBoxLayout*hlayout = new QHBoxLayout;
	hlayout->addStretch();
	hlayout->addWidget(ok_button);

	QFormLayout*layout = new QFormLayout;

	layout->addRow(tr("Damage source"),damage_source);
	layout->addRow(tr("Damage target"),damage_target);
	layout->addRow(tr("Damage nature"),damage_nature);
	layout->addRow(tr("Damage point"),damage_point);
	layout->addRow(hlayout);

	setLayout(layout);

	connect(damage_nature,SIGNAL(currentIndexChanged(int)),this,SLOT(disableSource()));
}

void DamageMakerDialog::disableSource()
{
	QString nature = damage_nature->itemData(damage_nature->currentIndex()).toString();
	damage_source->setEnabled(nature!="L");
}

StateEditorDialog::StateEditorDialog(QWidget*parent)
	: QDialog(parent)
{
	setWindowTitle("状态编辑器");//(tr("State editor"));

	target = new QComboBox;
	RoomDebugDialogs::fillPlayerNames(target,false);

	type = new QComboBox;
	type->addItem(QString("改变手牌上限"),S_CHEAT_CHANGE_MAXCARDS);//tr("Change maxcards")
	type->addItem(QString("改变其余角色到目标的距离"),S_CHEAT_CHANGE_DISTANCE);//tr("Change distance")
	type->addItem(QString("改变目标到其余角色的距离"),S_CHEAT_CHANGE_DISTANCE_TO_OTHERS);//tr("Change distance to others")
	type->addItem(QString("改变攻击范围"),S_CHEAT_CHANGE_ATTACKRANGE);//tr("Change attack range")
	type->addItem(QString("改变【杀】上限"),S_CHEAT_CHANGE_SLASHCISHU);//tr("Change slash cishu")
	type->addItem(QString("改变【杀】范围"),S_CHEAT_CHANGE_SLASHJULI);//tr("Change slash juli")
	type->addItem(QString("改变【杀】目标数"),S_CHEAT_CHANGE_SLASHMUBIAO);//tr("Change slash mubiao")
	type->addItem(QString("摸牌"),S_CHEAT_DrawCards);//tr("Draw Cards")
	type->addItem(QString("弃置所有牌"),S_CHEAT_ThrowAllHandCardsAndEquips);//tr("Throw All HandCardsAndEquips")
	type->addItem(QString("弃置所有手牌"),S_CHEAT_ThrowAllHandCards);//tr("Throw All HandCards")
	type->addItem(QString("弃置所有装备区牌"),S_CHEAT_ThrowAllEquips);//tr("Throw All Equips")
	type->addItem(QString("弃置区域内所有牌"),S_CHEAT_ThrowAllCards);//tr("Throw All Cards")
	type->addItem(QString("弃置牌"),S_CHEAT_ThrowCards);//tr("Throw Cards")
	type->addItem(QString("弃置手牌"),S_CHEAT_ThrowCardsWithoutEquips);//tr("Throw Cards Without Equips")
	type->addItem(QString("横置或重置"),S_CHEAT_SetChained);//tr("Set Chained")
	type->addItem(QString("翻面"),S_CHEAT_TurnOver);//tr("Turn Over")
	type->addItem(QString("视为使用【酒】（不计次）"),S_CHEAT_UseAnaleptic);//tr("Use Analeptic")

	point = new QSpinBox;
	point->setRange(INT_MIN,INT_MAX);
	point->setValue(1);

	QPushButton*ok_button = new QPushButton(tr("OK"));
	connect(ok_button,SIGNAL(clicked()),this,SLOT(accept()));
	QHBoxLayout*hlayout = new QHBoxLayout;
	hlayout->addStretch();
	hlayout->addWidget(ok_button);

	QFormLayout*layout = new QFormLayout;

	layout->addRow(QString("目标"),target);//tr("Editor target")
	layout->addRow(QString("类型"),type);//tr("Editor type")
	layout->addRow(QString("数量"),point);//tr("Editor point")
	layout->addRow(hlayout);

	setLayout(layout);
}

void RoomDebugDialogs::fillPlayerNames(QComboBox*ComboBox,bool add_none)
{
	if(add_none) ComboBox->addItem(tr("None"),".");
	ComboBox->setIconSize(G_COMMON_LAYOUT.m_tinyAvatarSize);
	foreach (const ClientPlayer*player,ClientInstance->getPlayers()){
		if(!player->getGeneral()) continue;
		QString general_name = Sanguosha->translate(player->getGeneralName());
		QPixmap pixmap = G_ROOM_SKIN.getGeneralPixmap(player->getGeneralName(),QSanRoomSkin::S_GENERAL_ICON_SIZE_TINY);
		ComboBox->addItem(QIcon(pixmap),
			QString("%1 [%2]").arg(general_name).arg(player->screenName()),
			player->objectName());
	}
}

void DamageMakerDialog::accept()
{
	QDialog::accept();

	ClientInstance->requestCheatDamage(damage_source->itemData(damage_source->currentIndex()).toString(),
		damage_target->itemData(damage_target->currentIndex()).toString(),
		(DamageStruct::Nature)damage_nature->itemData(damage_nature->currentIndex()).toInt(),
		damage_point->value());
}

void StateEditorDialog::accept()
{
	QDialog::accept();

	ClientInstance->requestCheatchangestate(target->itemData(target->currentIndex()).toString(),
		type->itemData(type->currentIndex()).toInt(),
		point->value());
}

void RoomDebugDialogs::makeDamage(QWidget *parent_window)
{
	if(Self->getPhase()!=Player::Play){
		QMessageBox::warning(parent_window,tr("Warning"),tr("This function is only allowed at your play phase!"));
		return;
	}

	DamageMakerDialog*damage_maker = new DamageMakerDialog(parent_window);
	damage_maker->exec();
}

void RoomDebugDialogs::changeState(QWidget *parent_window)
{
	if(Self->getPhase()!=Player::Play){
		QMessageBox::warning(parent_window,tr("Warning"),tr("This function is only allowed at your play phase!"));
		return;
	}

	StateEditorDialog*state_editor = new StateEditorDialog(parent_window);
	state_editor->exec();
}

void RoomDebugDialogs::makeKilling(QWidget *parent_window)
{
	if(Self->getPhase()!=Player::Play){
		QMessageBox::warning(parent_window,tr("Warning"),tr("This function is only allowed at your play phase!"));
		return;
	}

	DeathNoteDialog*dialog = new DeathNoteDialog(parent_window);
	dialog->exec();
}

void RoomDebugDialogs::makeReviving(QWidget *parent_window)
{
	if(Self->getPhase()!=Player::Play){
		QMessageBox::warning(parent_window,tr("Warning"),tr("This function is only allowed at your play phase!"));
		return;
	}

	QStringList items;
	QList<const ClientPlayer*> victims;
	foreach (const ClientPlayer*player,ClientInstance->getPlayers()){
		if(player->isDead()){
			QString general_name = Sanguosha->translate(player->getGeneralName());
			items << QString("%1 [%2]").arg(player->screenName()).arg(general_name);
			victims << player;
		}
	}

	if(items.isEmpty()){
		QMessageBox::warning(parent_window,tr("Warning"),tr("No victims now!"));
		return;
	}

	bool ok;
	QString item = QInputDialog::getItem(parent_window,tr("Reviving wand"),
		tr("Please select a player to revive"),items,0,false,&ok);
	if(ok){
		int index = items.indexOf(item);
		ClientInstance->requestCheatRevive(victims.at(index)->objectName());
	}
}

void RoomDebugDialogs::doScript(QWidget *parent_window)
{
	ScriptExecutor*dialog = new ScriptExecutor(parent_window);
	dialog->exec();
}
