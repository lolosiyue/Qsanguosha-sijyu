#include "cardoverview.h"
#include "ui_cardoverview.h"
#include "engine.h"
#include "settings.h"
#include "clientstruct.h"
#include "client.h"
#include "skin-bank.h"

static CardOverview *Overview;

CardOverview *CardOverview::getInstance(QWidget *main_window)
{
    if (Overview == nullptr)
        Overview = new CardOverview(main_window);
	else{
		//Overview->hide();
#ifdef ANDROID
		delete Overview;
        Overview = new CardOverview(main_window);
#endif // ANDROID
	}

    return Overview;
}

CardOverview::CardOverview(QWidget *parent)
    : QDialog(parent), ui(new Ui::CardOverview)
{
	ui->setupUi(this);

    ui->tableWidget->setColumnWidth(0, 80);
    ui->tableWidget->setColumnWidth(1, 60);
    ui->tableWidget->setColumnWidth(2, 30);
    ui->tableWidget->setColumnWidth(3, 60);
    ui->tableWidget->setColumnWidth(4, 70);

    connect(ui->getCardButton, SIGNAL(clicked()), this, SLOT(askCard()));
	ui->getCardButton->hide();

    ui->cardDescriptionBox->setProperty("description", true);
    ui->malePlayButton->hide();
    ui->femalePlayButton->hide();
    ui->playAudioEffectButton->hide();
}

void CardOverview::loadFromAll()
{
    /*int n = Sanguosha->getCardCount();
    ui->tableWidget->setRowCount(n);
    for (int i = 0; i < n; i++) {
        const Card *card = Sanguosha->getEngineCard(i);
        addCard(i, card);
    }

    if (n > 0) {
        ui->tableWidget->setCurrentItem(ui->tableWidget->item(0, 0));

        const Card *card = Sanguosha->getEngineCard(0);
        if (card->getTypeId() == Card::TypeEquip) {
            ui->playAudioEffectButton->show();
            ui->malePlayButton->hide();
            ui->femalePlayButton->hide();
        } else {
            ui->playAudioEffectButton->hide();
            ui->malePlayButton->show();
            ui->femalePlayButton->show();
        }
    }*/

	if(ServerInfo.DuringGame && ServerInfo.EnableCheat)
        ui->getCardButton->show();
	else
        ui->getCardButton->hide();

    QList<const Card *> list;
	bool cstring = !ServerInfo.DuringGame&&QFile::exists("lua/ai/cstring");
    QList<int> ava = Sanguosha->getRandomCards(true);
    for (int i = 0; i < Sanguosha->getCardCount(); i++) {
		if (ServerInfo.DuringGame&&!ava.contains(i)) continue;
		const Card *card = Sanguosha->getEngineCard(i);
		if (cstring){
			list << card;
			continue;
		}
		if (card->objectName().contains("_zhizhe_")||card->objectName().startsWith("__")) continue;
		list << card;
    }
    loadFromList(list);
}

void CardOverview::loadFromList(const QList<const Card *> &list)
{
    int n = list.length();
	ui->tableWidget->setRowCount(n);
    for (int i = 0; i < n; i++)
        addCard(i, list.at(i));

    if (n > 0) {
        ui->tableWidget->setCurrentItem(ui->tableWidget->item(0, 0));

        if (list.first()->getTypeId() == Card::TypeEquip) {
            ui->playAudioEffectButton->show();
            ui->malePlayButton->hide();
            ui->femalePlayButton->hide();
        } else {
            ui->playAudioEffectButton->hide();
            ui->malePlayButton->show();
            ui->femalePlayButton->show();
        }
    }
}

void CardOverview::addCard(int i, const Card *card)
{
    QString name = Sanguosha->translate(card->objectName());

    QString yingbian = card->property("YingBianEffects").toString();
    if (!yingbian.isEmpty()) name += QString("(%1)").arg(Sanguosha->translate(yingbian));
	foreach (QString tag, card->property("CharTag").toStringList())
		name += QString("(%1)").arg(Sanguosha->translate(tag));

    QTableWidgetItem *item = new QTableWidgetItem(name);
    item->setData(Qt::UserRole, card->getId());
    item->setTextAlignment(Qt::AlignCenter);
    ui->tableWidget->setItem(i, 0, item);

	name = card->getSuitString();
	item = new QTableWidgetItem(QIcon(QString("image/system/cardsuit/%1.png").arg(name)), Sanguosha->translate(name));
    item->setTextAlignment(Qt::AlignCenter);
    ui->tableWidget->setItem(i, 1, item);

	item = new QTableWidgetItem(card->getNumberString());
    item->setTextAlignment(Qt::AlignCenter);
    ui->tableWidget->setItem(i, 2, item);
    ui->tableWidget->setColumnWidth(2, 40);

	item = new QTableWidgetItem(Sanguosha->translate(card->getType()));
    item->setTextAlignment(Qt::AlignCenter);
    ui->tableWidget->setItem(i, 3, item);

	item = new QTableWidgetItem(Sanguosha->translate(card->getSubtype()));
    item->setTextAlignment(Qt::AlignCenter);
    ui->tableWidget->setItem(i, 4, item);

	name = card->getPackage();
    item = new QTableWidgetItem(Sanguosha->translate(name));

	static QStringList LuaPackages = Config.value("LuaPackages").toString().split("+");
    if (LuaPackages.contains(name)) {
        item->setBackground(QColor(0x66, 0xCC, 0xFF));
        item->setToolTip(tr("This is an Lua extension"));
    }
    item->setTextAlignment(Qt::AlignCenter);
    ui->tableWidget->setItem(i, 5, item);
}

CardOverview::~CardOverview()
{
    delete ui;
}

void CardOverview::on_tableWidget_itemSelectionChanged()
{
    int row = ui->tableWidget->currentRow();
    int card_id = ui->tableWidget->item(row, 0)->data(Qt::UserRole).toInt();
    const Card *card = Sanguosha->getEngineCard(card_id);

    ui->cardLabel->setPixmap(QString("image/card/%1.jpg").arg(card->objectName()));

    ui->cardDescriptionBox->setText(card->getDescription());

    if (card->getTypeId() == Card::TypeEquip) {
        ui->playAudioEffectButton->show();
        ui->malePlayButton->hide();
        ui->femalePlayButton->hide();
    } else {
        ui->playAudioEffectButton->hide();
        ui->malePlayButton->show();
        ui->femalePlayButton->show();
    }
}

void CardOverview::askCard()
{
	int row = ui->tableWidget->currentRow();
    if (row >= 0 && ServerInfo.DuringGame && ServerInfo.EnableCheat) {
        int card_id = ui->tableWidget->item(row, 0)->data(Qt::UserRole).toInt();
        if (ClientInstance->getAvailableCards().contains(card_id))
			ClientInstance->requestCheatGetOneCard(card_id);
        else
            QMessageBox::warning(this, tr("Warning"), tr("These packages don't contain this card"));
    }
}

void CardOverview::on_tableWidget_itemDoubleClicked(QTableWidgetItem *)
{
    if (Self) askCard();
}

void CardOverview::on_malePlayButton_clicked()
{
    int row = ui->tableWidget->currentRow();
    if (row >= 0) {
        int card_id = ui->tableWidget->item(row, 0)->data(Qt::UserRole).toInt();
        const Card *card = Sanguosha->getEngineCard(card_id);
        Sanguosha->playAudioEffect(G_ROOM_SKIN.getPlayerAudioEffectPath(card->objectName(), true), false);
    }
}

void CardOverview::on_femalePlayButton_clicked()
{
    int row = ui->tableWidget->currentRow();
    if (row >= 0) {
        int card_id = ui->tableWidget->item(row, 0)->data(Qt::UserRole).toInt();
        const Card *card = Sanguosha->getEngineCard(card_id);
        Sanguosha->playAudioEffect(G_ROOM_SKIN.getPlayerAudioEffectPath(card->objectName(), false), false);
    }
}

void CardOverview::on_playAudioEffectButton_clicked()
{
    int row = ui->tableWidget->currentRow();
    if (row >= 0) {
        int card_id = ui->tableWidget->item(row, 0)->data(Qt::UserRole).toInt();
        const Card *card = Sanguosha->getEngineCard(card_id);
        if (card->getTypeId() == Card::TypeEquip) {
            QString objectName = card->objectName();
            if (objectName == "vscrossbow")
                objectName = "crossbow";
            QString fileName = G_ROOM_SKIN.getPlayerAudioEffectPath(objectName, QString("equip"), -1);
            if (!QFile::exists(fileName))
                fileName = G_ROOM_SKIN.getPlayerAudioEffectPath(card->getCommonEffectName(), QString("common"), -1);
            Sanguosha->playAudioEffect(fileName, false);
        }
    }
}

