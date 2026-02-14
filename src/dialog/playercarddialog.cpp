#include "playercarddialog.h"
//#include "carditem.h"
//#include "standard.h"
#include "engine.h"
#include "client.h"
#include "wrapped-card.h"
//#include "clientplayer.h"
#include "skin-bank.h"

QList<int> PlayerCardDialog::dummy_list;

PlayerCardButton::PlayerCardButton(const QString &name)
    : QCommandLinkButton(name), scale(1.0)
{
}

PlayerCardDialog::PlayerCardDialog(const ClientPlayer *player, const QString &flags,
    bool handcard_visible, Card::HandlingMethod method, QList<int> &disabled_ids, bool can_cancel)
    : player(player), handcard_visible(handcard_visible), method(method), disabled_ids(disabled_ids), can_cancel(can_cancel)
{
    QVBoxLayout *vlayout1 = new QVBoxLayout;
    vlayout1->addWidget(createAvatar());
    vlayout1->addStretch();
    QHBoxLayout *layout = new QHBoxLayout;
    layout->addLayout(vlayout1);

	vlayout1 = new QVBoxLayout;

    if (flags.contains("j"))
        vlayout1->addWidget(createJudgingArea());
    if (flags.contains("e"))
        vlayout1->addWidget(createEquipArea());
    if (flags.contains("h"))
        vlayout1->addWidget(createHandcardButton());

    layout->addLayout(vlayout1);

    if (can_cancel) {
		QPushButton *cancel_button = new QPushButton(tr("Cancel"));
        connect(cancel_button, SIGNAL(clicked()), this, SLOT(reject()));
        layout->addWidget(cancel_button);
    }

    setLayout(layout);
}

QWidget *PlayerCardDialog::createAvatar()
{
    QGroupBox *box = new QGroupBox(ClientInstance->getPlayerName(player->objectName()));
    box->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    QLabel *avatar = new QLabel(box);
    avatar->setPixmap(QPixmap(G_ROOM_SKIN.getGeneralPixmap(player->getGeneralName(), QSanRoomSkin::S_GENERAL_ICON_SIZE_LARGE)));

    QHBoxLayout *layout = new QHBoxLayout;
    layout->addWidget(avatar);

    if (player->getGeneral2()) {
        QLabel *avatar2 = new QLabel(box);
        avatar2->setPixmap(QPixmap(G_ROOM_SKIN.getGeneralPixmap(player->getGeneral2Name(), QSanRoomSkin::S_GENERAL_ICON_SIZE_LARGE)));
        layout->addWidget(avatar2);
    }

    box->setLayout(layout);

    return box;
}

QWidget *PlayerCardDialog::createHandcardButton()
{
    int n = player->getHandcardNum();
    QVBoxLayout *layout = new QVBoxLayout;
	if (n < 1) {
		/*PlayerCardButton *button = new PlayerCardButton(tr("Handcard"));
		button->setObjectName("handcard_button");
        button->setDescription(tr("This guy has no any hand cards"));
        button->setEnabled(false);
		return button;*/
		PlayerCardButton *button = new PlayerCardButton(tr("This guy has no any hand cards"));
        button->setEnabled(false);
		layout->addWidget(button);
	}else if(Self == player || handcard_visible){
		QList<int> ids = player->handCards();
        for (int i = 0; i < n; i += 2) {
            const Card *card = Sanguosha->getEngineCard(ids[i]);
            PlayerCardButton *button1 = new PlayerCardButton(card->getFullName());
            button1->setIcon(G_ROOM_SKIN.getCardSuitPixmap(card->getSuit()));
			button1->setEnabled(!disabled_ids.contains(ids[i])&&(method!=Card::MethodDiscard||Self->canDiscard(player,ids[i])));

            mapper.insert(button1, ids[i]);
            connect(button1, SIGNAL(clicked()), this, SLOT(emitId()));

            PlayerCardButton *button2 = nullptr;
            if (i < n-1) {
                card = Sanguosha->getEngineCard(ids[i+1]);;
                button2 = new PlayerCardButton(card->getFullName());
                button2->setIcon(G_ROOM_SKIN.getCardSuitPixmap(card->getSuit()));
				button2->setEnabled(!disabled_ids.contains(ids[i+1])&&(method!=Card::MethodDiscard||Self->canDiscard(player,ids[i+1])));

                mapper.insert(button2, ids[i+1]);
                connect(button2, SIGNAL(clicked()), this, SLOT(emitId()));
            }
            if (button2) {
                QHBoxLayout *hlayout = new QHBoxLayout;
                button1->setScale(0.55);
                button2->setScale(0.55);
                hlayout->addWidget(button1);
                hlayout->addWidget(button2);
                layout->addLayout(hlayout);
            } else
                layout->addWidget(button1);
        }
    } else {/*
        button->setDescription(tr("This guy has %1 hand card(s)").arg(n));
        button->setEnabled(method != Card::MethodDiscard || Self->canDiscard(player, "h"));
        mapper.insert(button, -1);
        connect(button, SIGNAL(clicked()), this, SLOT(emitId()));*/

		int m = 1;
		QHBoxLayout *hlayout = new QHBoxLayout;
		foreach (int id, player->handCards()) {
			PlayerCardButton *button = new PlayerCardButton(tr("Handcard"));
			button->setIcon(QIcon("image/system/card-back.png"));
			if(Sanguosha->getCard(id)->hasTip("mingzhi")&&!player->hasSkill("xichang")){
				const Card *card = Sanguosha->getEngineCard(id);
				delete button;
				button = new PlayerCardButton(card->getFullName());
				button->setIcon(G_ROOM_SKIN.getCardSuitPixmap(card->getSuit()));
			}
			button->setEnabled(!disabled_ids.contains(id)&&(method!=Card::MethodDiscard||Self->canDiscard(player,id)));
			connect(button, SIGNAL(clicked()), this, SLOT(emitId()));
			button->setScale(0.4);
			//layout->addWidget(button);
			mapper.insert(button, id);
			if(m>=n){
                hlayout->addWidget(button);
                layout->addLayout(hlayout);
			}else if(m%4>=3){
                layout->addLayout(hlayout);
				hlayout = new QHBoxLayout;
                hlayout->addWidget(button);
			}else
                hlayout->addWidget(button);
			m++;
		}
    }
    QGroupBox *area = new QGroupBox(tr("Handcard area"));
    area->setLayout(layout);
    return area;
}

QWidget *PlayerCardDialog::createEquipArea()
{
    QVBoxLayout *layout = new QVBoxLayout;/*

    WrappedCard *weapon = player->getWeapon();
    if (weapon) {
        PlayerCardButton *button = new PlayerCardButton(weapon->getFullName());
        button->setIcon(G_ROOM_SKIN.getCardSuitPixmap(Sanguosha->getEngineCard(weapon->getId())->getSuit()));
        button->setEnabled(!disabled_ids.contains(weapon->getId())
            && (method != Card::MethodDiscard || Self->canDiscard(player, weapon->getId())));
        mapper.insert(button, weapon->getId());
        connect(button, SIGNAL(clicked()), this, SLOT(emitId()));
        layout->addWidget(button);
    }

    WrappedCard *armor = player->getArmor();
    if (armor) {
        PlayerCardButton *button = new PlayerCardButton(armor->getFullName());
        button->setIcon(G_ROOM_SKIN.getCardSuitPixmap(Sanguosha->getEngineCard(armor->getId())->getSuit()));
        button->setEnabled(!disabled_ids.contains(armor->getId())
            && (method != Card::MethodDiscard || Self->canDiscard(player, armor->getId())));
        mapper.insert(button, armor->getId());
        connect(button, SIGNAL(clicked()), this, SLOT(emitId()));
        layout->addWidget(button);
    }

    WrappedCard *horse = player->getDefensiveHorse();
    if (horse) {
        PlayerCardButton *button = new PlayerCardButton(horse->getFullName() + tr("(+1 horse)"));
        button->setIcon(G_ROOM_SKIN.getCardSuitPixmap(Sanguosha->getEngineCard(horse->getId())->getSuit()));
        button->setEnabled(!disabled_ids.contains(horse->getId())
            && (method != Card::MethodDiscard || Self->canDiscard(player, horse->getId())));
        mapper.insert(button, horse->getId());
        connect(button, SIGNAL(clicked()), this, SLOT(emitId()));
        layout->addWidget(button);
    }

    horse = player->getOffensiveHorse();
    if (horse) {
        PlayerCardButton *button = new PlayerCardButton(horse->getFullName() + tr("(-1 horse)"));
        button->setIcon(G_ROOM_SKIN.getCardSuitPixmap(Sanguosha->getEngineCard(horse->getId())->getSuit()));
        button->setEnabled(!disabled_ids.contains(horse->getId())
            && (method != Card::MethodDiscard || Self->canDiscard(player, horse->getId())));
        mapper.insert(button, horse->getId());
        connect(button, SIGNAL(clicked()), this, SLOT(emitId()));
        layout->addWidget(button);
    }

    WrappedCard *treasure = player->getTreasure();
    if (treasure) {
        PlayerCardButton *button = new PlayerCardButton(treasure->getFullName());
        button->setIcon(G_ROOM_SKIN.getCardSuitPixmap(Sanguosha->getEngineCard(treasure->getId())->getSuit()));
        button->setEnabled(!disabled_ids.contains(treasure->getId())
            && (method != Card::MethodDiscard || Self->canDiscard(player, treasure->getId())));
        mapper.insert(button, treasure->getId());
        connect(button, SIGNAL(clicked()), this, SLOT(emitId()));
        layout->addWidget(button);
    }*/
	foreach (int id, player->getEquipsId()) {
        const Card *card = Sanguosha->getEngineCard(id);
		PlayerCardButton *button = new PlayerCardButton(card->getFullName());
		button->setIcon(G_ROOM_SKIN.getCardSuitPixmap(card->getSuit()));
		button->setEnabled(!disabled_ids.contains(id)&&(method!=Card::MethodDiscard||Self->canDiscard(player,id)));
		button->setToolTip(Sanguosha->getCard(id)->getDescription());
		connect(button, SIGNAL(clicked()), this, SLOT(emitId()));
		layout->addWidget(button);
		mapper.insert(button, id);
	}
    if (layout->count() == 0) {
        /*delete layout;
        PlayerCardButton *button = new PlayerCardButton(tr("No equip"));
        button->setObjectName("noequip_button");
        button->setEnabled(false);
        return button;*/
		PlayerCardButton *button = new PlayerCardButton(tr("No equip"));
        button->setEnabled(false);
		layout->addWidget(button);
    }
    QGroupBox *area = new QGroupBox(tr("Equip area"));
	area->setLayout(layout);
	return area;
}

QWidget *PlayerCardDialog::createJudgingArea()
{
    QVBoxLayout *layout = new QVBoxLayout;
    foreach (int id, player->getJudgingAreaID()) {
        const Card *card = Sanguosha->getEngineCard(id);
        PlayerCardButton *button = new PlayerCardButton(card->getFullName());
        button->setIcon(G_ROOM_SKIN.getCardSuitPixmap(card->getSuit()));
		button->setToolTip(Sanguosha->getCard(id)->getDescription());
        button->setEnabled(!disabled_ids.contains(id) && (method != Card::MethodDiscard || Self->canDiscard(player, id)));
        connect(button, SIGNAL(clicked()), this, SLOT(emitId()));
        layout->addWidget(button);
        mapper.insert(button, id);
    }
    if (layout->count() == 0) {
        /*delete layout;
        PlayerCardButton *button = new PlayerCardButton(tr("No judging cards"));
        button->setObjectName("nojuding_button");
        button->setEnabled(false);
        return button;*/
		PlayerCardButton *button = new PlayerCardButton(tr("No judging cards"));
        button->setEnabled(false);
		layout->addWidget(button);
    }
    QGroupBox *area = new QGroupBox(tr("Judging Area"));
	area->setLayout(layout);
	return area;
}

void PlayerCardDialog::emitId()
{
    int id = mapper.value(sender(), -2);
    if (id != -2) emit card_id_chosen(id);
}