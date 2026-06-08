#include "fieldcardtransferbox.h"
#include "engine.h"
#include "banpair.h"
#include "button.h"
#include "client.h"
#include "clientplayer.h"
#include "standard.h"
#include "util.h"

#include <QApplication>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsProxyWidget>

FieldCardTransferBox::FieldCardTransferBox()
    : confirm(new Button(tr("confirm"), 0.6)), cancel(new Button(tr("cancel"), 0.6)),
    progress_bar(NULL)
{
    confirm->setEnabled(ClientInstance->getReplayer());
    confirm->setParentItem(this);
    confirm->setObjectName("confirm");
    connect(confirm, &Button::clicked, this, &FieldCardTransferBox::reply);

    cancel->setEnabled(ClientInstance->getReplayer());
    cancel->setParentItem(this);
    cancel->setObjectName("cancel");
    connect(cancel, &Button::clicked, this, &FieldCardTransferBox::reply);
}

static int getEquipAreaCount(const ClientPlayer *player)
{
    return player->getEquipArea(-1);
}

QRectF FieldCardTransferBox::boundingRect() const
{
    const int card_width = G_COMMON_LAYOUT.m_cardNormalWidth;
    const int card_height = G_COMMON_LAYOUT.m_cardNormalHeight;

    int width = 70+40;
    if (equipArea) {
        int playerA_slots = getEquipAreaCount(playerA);
        int playerB_slots = getEquipAreaCount(playerB);
        int max_slots = qMax(playerA_slots, playerB_slots);
        if (max_slots > 0)
            width += ((card_width + cardInterval) * max_slots - cardInterval);
        else
            width += ((card_width + cardInterval) * S_EQUIP_AREA_LENGTH - cardInterval);
    }

    if (judgingArea) {
        QList<Card *> cards = Sanguosha->getCards();
        QStringList names;
        foreach (const Card *card, cards) {
            QString card_name = card->objectName();
            if (card->isKindOf("DelayedTrick") && !names.contains(card_name))
                names << card->objectName();
        }
        if (!names.isEmpty())
            width += ((card_width + cardInterval) * names.length() - cardInterval);
    }

    if (equipArea && judgingArea)
        width += cardInterval*2;

    int height = card_height*2 + cardInterval*2 + 90 + 16;

    if (ServerInfo.OperationTimeout != 0)
        height += 24;

    return QRectF(0, 0, width, height);
}

static bool canSetEquip(const ClientPlayer *player, const Card *card)
{
    if (card->getTypeId() != Card::TypeEquip) return false;
    const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
    if (!equip) return false;
    
    QList<int> occupy_slots = equip->getOccupyLocations();
    foreach (int slot, occupy_slots) {
        if (!player->hasEquipArea(slot))
            return false;
    }
    
    QList<const Card *> existing_equips = player->getEquips();
    foreach (const Card *existing, existing_equips) {
        const EquipCard *existing_equip = qobject_cast<const EquipCard *>(existing->getRealCard());
        if (!existing_equip) continue;
        QList<int> existing_slots = existing_equip->getOccupyLocations();
        foreach (int existing_slot, existing_slots) {
            if (occupy_slots.contains(existing_slot))
                return false;
        }
    }
    
    return true;
}

void FieldCardTransferBox::doFieldCardTransferChoose(const ClientPlayer *playerA, const ClientPlayer *playerB, const QString &reason, bool equipArea, bool judgingArea)
{
    this->playerA = playerA;
    this->playerB = playerB;
    this->reason = reason;
    this->equipArea = equipArea;
    this->judgingArea = judgingArea;
    buttonstate = ClientInstance->m_isDiscardActionRefusable;

    prepareGeometryChange();
    GraphicsBox::moveToCenter(this);
    show();

    if (equipArea) {
        foreach (const Card *e, playerA->getEquips()) {
            createCardItem(e, canSetEquip(playerB, e));
        }
        foreach (const Card *e, playerB->getEquips()) {
            createCardItem(e, canSetEquip(playerA, e));
        }
    }

    if (judgingArea) {
        foreach (const Card *j, playerA->getJudgingArea()) {
            createCardItem(j, !Sanguosha->isProhibited(NULL, playerB, j));
        }
        foreach (const Card *j, playerB->getJudgingArea()) {
            createCardItem(j, !Sanguosha->isProhibited(NULL, playerA, j));
        }
    }

    adjust();

    confirm->setPos(boundingRect().center().x() - confirm->boundingRect().width() / 2-80, boundingRect().height() - ((ServerInfo.OperationTimeout == 0) ? 40 : 60));
    confirm->show();
    confirm->setEnabled(false);

    cancel->setPos(boundingRect().center().x() - cancel->boundingRect().width() / 2+80, boundingRect().height() - ((ServerInfo.OperationTimeout == 0) ? 40 : 60));
    cancel->show();
    cancel->setEnabled(true);

    if (ServerInfo.OperationTimeout != 0) {
        if (!progress_bar) {
            progress_bar = new QSanCommandProgressBar();
            progress_bar->setMinimumWidth(200);
            progress_bar->setMaximumHeight(12);
            progress_bar->setTimerEnabled(true);
            progress_bar_item = new QGraphicsProxyWidget(this);
            progress_bar_item->setWidget(progress_bar);
            progress_bar_item->setPos(boundingRect().center().x() - progress_bar_item->boundingRect().width() / 2, boundingRect().height() - 20);
            connect(progress_bar, &QSanCommandProgressBar::timedOut, this, &FieldCardTransferBox::reply);
        }
        progress_bar->setCountdown(QSanProtocol::S_COMMAND_SKILL_TRANSFERFIELDCARDS);
        progress_bar->show();
    }
}

void FieldCardTransferBox::createCardItem(const Card *card, bool enabled)
{
    CardItem *cardItem = new CardItem(card);
    if (card->objectName() != cardItem->objectName())
        cardItem->showSmallCard(card->objectName());

    cardItem->setAutoBack(false);
    cardItem->setFlag(QGraphicsItem::ItemIsFocusable);
    cardItem->setFlag(QGraphicsItem::ItemIsMovable, false);

    connect(cardItem, &CardItem::clicked, this, &FieldCardTransferBox::onItemClicked);

    cardItems[card->getId()] = cardItem;

    cardItem->setParentItem(this);
    cardItem->resetTransform();
    cardItem->setOuterGlowEffectEnabled(true);
    cardItem->setPos(45, 45);

    cardItem->setEnabled(enabled);
}

void FieldCardTransferBox::adjust()
{
    const int cardWidth = G_COMMON_LAYOUT.m_cardNormalWidth;
    const int cardHeight = G_COMMON_LAYOUT.m_cardNormalHeight;

    int up_app = 0;
    if (equipArea) {
        QList<const Card *> equipsA = playerA->getEquips();
        QList<const Card *> equipsB = playerB->getEquips();
        
        int slotIndex = 0;
        
        for (int slot = 0; slot < S_EQUIP_AREA_LENGTH; ++slot) {
            if (!playerA->hasEquipArea(slot) && !playerB->hasEquipArea(slot))
                continue;
            
            foreach (const Card *card, equipsA) {
                const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
                if (!equip) continue;
                QList<int> occupy_slots = equip->getOccupyLocations();
                if (occupy_slots.contains(slot)) {
                    int id = equip->getId();
                    CardItem *cardItem = cardItems.value(id);
                    if (cardItem) {
                        QPointF pos;
                        int X = 45 + (cardWidth + cardInterval) * slotIndex;
                        int Y = 45;
                        
                        if (selected.contains(id))
                            Y += (cardHeight + cardInterval);
                        
                        pos.setX(X);
                        pos.setY(Y);
                        
                        cardItem->setHomePos(pos);
                        cardItem->goBack(true);
                    }
                }
            }
            
            foreach (const Card *card, equipsB) {
                const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
                if (!equip) continue;
                QList<int> occupy_slots = equip->getOccupyLocations();
                if (occupy_slots.contains(slot)) {
                    int id = equip->getId();
                    CardItem *cardItem = cardItems.value(id);
                    if (cardItem) {
                        QPointF pos;
                        int X = 45 + (cardWidth + cardInterval) * slotIndex;
                        int Y = 45;
                        
                        if (!selected.contains(id))
                            Y += (cardHeight + cardInterval);
                        
                        pos.setX(X);
                        pos.setY(Y);
                        
                        cardItem->setHomePos(pos);
                        cardItem->goBack(true);
                    }
                }
            }
            
            slotIndex++;
        }
        
        up_app += ((cardWidth + cardInterval) * slotIndex + cardInterval);
    }

    if (judgingArea) {
        QList<Card *> cards = Sanguosha->getCards();
        QStringList names;
        foreach (const Card *card, cards) {
            QString card_name = card->objectName();
            if (card->isKindOf("DelayedTrick") && !names.contains(card_name))
                names << card->objectName();
        }
        int i = 0;
        foreach (QString card_name, names) {
            foreach (const Card *j, playerA->getJudgingArea()) {
                if (j->objectName() == card_name) {
                    CardItem *cardItem = cardItems.value(j->getId());
                    if (cardItem) {
                        QPointF pos;
                        int X, Y;

                        X = (45 + up_app + (cardWidth + cardInterval) * i);
                        Y = (45);

                        if (selected.contains(j->getId()))
                            Y += (cardHeight + cardInterval);

                        pos.setX(X);
                        pos.setY(Y);

                        cardItem->setHomePos(pos);
                        cardItem->goBack(true);
                    }
                }
            }
            foreach (const Card *j, playerB->getJudgingArea()) {
                if (j->objectName() == card_name) {
                    CardItem *cardItem = cardItems.value(j->getId());
                    if (cardItem) {
                        QPointF pos;
                        int X, Y;

                        X = (45 + up_app + (cardWidth + cardInterval) * i);
                        Y = (45);

                        if (!selected.contains(j->getId()))
                            Y += (cardHeight + cardInterval);

                        pos.setX(X);
                        pos.setY(Y);

                        cardItem->setHomePos(pos);
                        cardItem->goBack(true);
                    }
                }
            }
            i++;
        }

    }

    foreach (CardItem *cardItem, cardItems.values()) {
        cardItem->setChosen(selected.contains(cardItem->getId()));
    }

    confirm->setEnabled(!selected.isEmpty());
}

void FieldCardTransferBox::onItemClicked()
{
    CardItem *item = qobject_cast<CardItem *>(sender());
    if (item == NULL) return;

    int id = item->getId();
    if (selected.contains(id))
        selected.removeOne(id);
    else {
        if (!selected.isEmpty())
            selected.removeFirst();
        selected << item->getId();
    }

    adjust();
}

void FieldCardTransferBox::clear()
{
    foreach(CardItem *card_item, cardItems.values())
        card_item->deleteLater();

    cardItems.clear();
    selected.clear();

    if (progress_bar != NULL) {
        progress_bar->hide();
        progress_bar->deleteLater();
        progress_bar = NULL;
    }

    prepareGeometryChange();
    hide();
}

void FieldCardTransferBox::reply()
{
    QList<int> reply_cards;
    Button *button = qobject_cast<Button *>(sender());

    if (button && button->objectName() == "confirm")
        reply_cards = selected;

    ClientInstance->onPlayerReplyFieldCardTransfer(reply_cards);
    clear();
}

void FieldCardTransferBox::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    QString title = QString("%1: %2").arg(Sanguosha->translate(reason)).arg(Sanguosha->translate("@tarnsferfieldcard"));

    GraphicsBox::paintGraphicsBoxStyle(painter, title, boundingRect());

    const int card_width = G_COMMON_LAYOUT.m_cardNormalWidth;
    const int card_height = G_COMMON_LAYOUT.m_cardNormalHeight;

    QString description1 = ClientInstance->getPlayerName(playerA->objectName());
    QRect up_rect(15, 45, 20, card_height);
    G_COMMON_LAYOUT.playerCardBoxPlaceNameText.paintText(painter, up_rect, Qt::AlignCenter, description1);

    QString description2 = ClientInstance->getPlayerName(playerB->objectName());
    QRect down_rect(15, 45 + card_height + cardInterval, 20, card_height);
    G_COMMON_LAYOUT.playerCardBoxPlaceNameText.paintText(painter, down_rect, Qt::AlignCenter, description2);

    int up_app = 0;

    IQSanComponentSkin::QSanSimpleTextFont font = G_COMMON_LAYOUT.m_chooseGeneralBoxDestSeatFont;

    if (equipArea) {
        QStringList equipnames;
        equipnames << "weapon" << "armor" << "defensive_horse" << "offensive_horse" << "treasure";
        
        int slotIndex = 0;
        for (int slot = 0; slot < S_EQUIP_AREA_LENGTH; ++slot) {
            if (!playerA->hasEquipArea(slot) && !playerB->hasEquipArea(slot))
                continue;
            
            int x = 45 + (card_width + cardInterval) * slotIndex;
            int y = 45;
            QRect top_rect(x, y, card_width, card_height);
            painter->drawPixmap(top_rect, G_ROOM_SKIN.getPixmap(QSanRoomSkin::S_SKIN_KEY_CHOOSE_GENERAL_BOX_DEST_SEAT));
            font.paintText(painter, top_rect, Qt::AlignCenter, Sanguosha->translate(equipnames[slot]));
            top_rect.translate(0, card_height + cardInterval);
            painter->drawPixmap(top_rect, G_ROOM_SKIN.getPixmap(QSanRoomSkin::S_SKIN_KEY_CHOOSE_GENERAL_BOX_DEST_SEAT));
            font.paintText(painter, top_rect, Qt::AlignCenter, Sanguosha->translate(equipnames[slot]));
            
            slotIndex++;
        }
        up_app += ((card_width + cardInterval) * slotIndex + cardInterval);
    }

    if (judgingArea) {
        QList<Card *> cards = Sanguosha->getCards();
        QStringList names;
        foreach (const Card *card, cards) {
            QString card_name = card->objectName();
            if (card->isKindOf("DelayedTrick") && !names.contains(card_name))
                names << card->objectName();
        }
        int count = names.length();
        for (int i = 0; i < count; i++) {
            int x, y = 0;
            x = (45 + up_app + (card_width + cardInterval) * i);
            y = (45);
            QRect top_rect(x, y, card_width, card_height);
            painter->drawPixmap(top_rect, G_ROOM_SKIN.getPixmap(QSanRoomSkin::S_SKIN_KEY_CHOOSE_GENERAL_BOX_DEST_SEAT));
            font.paintText(painter, top_rect, Qt::AlignCenter, Sanguosha->translate(names[i]));
            top_rect.translate(0, card_height + cardInterval);
            painter->drawPixmap(top_rect, G_ROOM_SKIN.getPixmap(QSanRoomSkin::S_SKIN_KEY_CHOOSE_GENERAL_BOX_DEST_SEAT));
            font.paintText(painter, top_rect, Qt::AlignCenter, Sanguosha->translate(names[i]));
        }
    }
}
