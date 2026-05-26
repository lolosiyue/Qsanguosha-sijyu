#include "choosegeneralbox.h"
#include "button.h"
#include "client.h"
#include "clientstruct.h"
#include "engine.h"
#include "skin-bank.h"

#include <QGraphicsProxyWidget>

using namespace QSanProtocol;

GeneralCardItem::GeneralCardItem(const QString &generalName)
    : CardItem(generalName)
    , m_hasCompanion(false)
{
    setAcceptHoverEvents(true);
    const General *general = Sanguosha->getGeneral(generalName);
    Q_ASSERT(general);
    setOuterGlowEffectEnabled(true);
    setOuterGlowColor(Sanguosha->getKingdomColor(general->getKingdom()));
}

void GeneralCardItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    painter->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    QRect rect = G_COMMON_LAYOUT.m_cardMainArea;
    if (isFrozen() || !isEnabled()) {
        painter->fillRect(rect, QColor(0, 0, 0));
        painter->setOpacity(0.4 * opacity());
    }
    if (!_m_isUnknownGeneral)
        painter->drawPixmap(rect, G_ROOM_SKIN.getCardMainPixmap(objectName(), false, false));
    else
        painter->drawPixmap(rect, G_ROOM_SKIN.getPixmap("generalCardBack"));

    if (!m_hasCompanion)
        return;

    QString kingdom = Sanguosha->getGeneral(objectName())->getKingdom();
    QPixmap icon = G_ROOM_SKIN.getPixmap("kingdomIcon", kingdom);
    if (!icon.isNull()) {
        int iconX = boundingRect().center().x() - (icon.width() / 2) + 3;
        int iconY = boundingRect().bottom() - icon.height() - 5;
        painter->drawPixmap(iconX, iconY, icon);
    }
}

void GeneralCardItem::showCompanion()
{
    if (m_hasCompanion)
        return;
    m_hasCompanion = true;
    update();
}

void GeneralCardItem::hideCompanion()
{
    if (!m_hasCompanion)
        return;
    m_hasCompanion = false;
    update();
}

ChooseGeneralBox::ChooseGeneralBox()
    : m_generalNumber(0)
    , m_singleResult(false)
    , m_viewOnly(false)
    , m_confirmButton(new Button(tr("fight"), 0.6))
    , m_progressBar(nullptr)
{
    m_confirmButton->setEnabled(ClientInstance->getReplayer() != nullptr);
    m_confirmButton->setParentItem(this);
    connect(m_confirmButton, &Button::clicked, this, &ChooseGeneralBox::reply);
}

void ChooseGeneralBox::paintLayout(QPainter *painter)
{
    if (m_viewOnly || m_singleResult)
        return;

    int split_line_y = top_blank_width + G_COMMON_LAYOUT.m_cardNormalHeight + card_bottom_to_split_line;
    if (m_generalNumber > 5)
        split_line_y += (card_to_center_line + G_COMMON_LAYOUT.m_cardNormalHeight);

    QPixmap line = G_ROOM_SKIN.getPixmap("chooseGeneralBoxSplitLine");
    if (line.isNull()) {
        painter->setPen(QPen(QColor(200, 200, 200), 2));
        int lineX = left_blank_width;
        int lineEndX = boundingRect().width() - left_blank_width;
        painter->drawLine(lineX, split_line_y, lineEndX, split_line_y);
    } else {
        const int line_length = boundingRect().width() - (2 * left_blank_width);
        const QRectF rect = boundingRect();
        painter->drawPixmap(left_blank_width, split_line_y, line, 
            (line.width() - line_length) / 2, rect.y(), line_length, line.height());
    }

    QPixmap seat = G_ROOM_SKIN.getPixmap("chooseGeneralBoxDestSeat");
    QRectF br = boundingRect();
    int cardWidth = G_COMMON_LAYOUT.m_cardNormalWidth;
    int cardHeight = G_COMMON_LAYOUT.m_cardNormalHeight;
    
    QRect seat1_rect(br.center().x() - cardWidth - card_to_center_line - 2, 
                     split_line_y + split_line_to_card_seat - 2,
                     cardWidth + 4, cardHeight + 4);
    if (!seat.isNull())
        painter->drawPixmap(seat1_rect, seat);
    IQSanComponentSkin::QSanSimpleTextFont font = G_COMMON_LAYOUT.m_chooseGeneralBoxDestSeatFont;
    font.paintText(painter, seat1_rect, Qt::AlignCenter, tr("head_general"));

    QRect seat2_rect(br.center().x() + card_to_center_line - 2, 
                     split_line_y + split_line_to_card_seat - 2,
                     cardWidth + 4, cardHeight + 4);
    if (!seat.isNull())
        painter->drawPixmap(seat2_rect, seat);
    font.paintText(painter, seat2_rect, Qt::AlignCenter, tr("deputy_general"));
}

QRectF ChooseGeneralBox::boundingRect() const
{
    int first_row = 0;
    int second_row = 0;

    if (m_generalNumber < 6) {
        first_row = m_generalNumber;
    } else {
        second_row = m_generalNumber / 2;
        first_row = m_generalNumber - second_row;
    }

    const int width = (first_row * G_COMMON_LAYOUT.m_cardNormalWidth) 
                    + ((first_row - 1) * card_to_center_line) 
                    + (left_blank_width * 2);

    int height = top_blank_width + G_COMMON_LAYOUT.m_cardNormalHeight + bottom_blank_width;
    if (second_row != 0)
        height += (card_to_center_line + G_COMMON_LAYOUT.m_cardNormalHeight);

    if (m_singleResult) {
        height -= 30;
    } else if (!m_viewOnly) {
        height += G_COMMON_LAYOUT.m_cardNormalHeight + card_bottom_to_split_line + split_line_to_card_seat;
    }

    return QRectF(0, 0, width, height);
}

static bool sortByKingdom(const QString &gen1, const QString &gen2)
{
    static QMap<QString, int> kingdom_priority_map;
    if (kingdom_priority_map.isEmpty()) {
        QStringList kingdoms = Sanguosha->getKingdoms();
        int i = 0;
        foreach (const QString &kingdom, kingdoms)
            kingdom_priority_map[kingdom] = i++;
    }
    const General *g1 = Sanguosha->getGeneral(gen1);
    const General *g2 = Sanguosha->getGeneral(gen2);
    if (g1 != nullptr && g2 != nullptr)
        return kingdom_priority_map[g1->getKingdom()] < kingdom_priority_map[g2->getKingdom()];
    return false;
}

void ChooseGeneralBox::chooseGeneral(const QStringList &_generals, bool viewOnly, bool singleResult, const QString &reason)
{
    QStringList generals = _generals;
    m_singleResult = singleResult;
    if (viewOnly)
        title = reason;
    if (m_viewOnly != viewOnly)
        m_viewOnly = viewOnly;

    foreach (const QString &general, _generals) {
        if (general.endsWith("(lord)"))
            generals.removeOne(general);
    }

    m_generalNumber = generals.length();
    if (!viewOnly) {
        title = singleResult ? tr("Please select one general") : tr("Please select the same nationality generals");
        if (!singleResult && Self->getSeat() > 0)
            title.prepend(Sanguosha->translate(QString("SEAT(%1)").arg(Self->getSeat())) + " ");
    }

    prepareGeometryChange();
    m_items.clear();
    m_selected.clear();
    int z = generals.length();

    qStableSort(generals.begin(), generals.end(), sortByKingdom);

    foreach (const QString &general, generals) {
        GeneralCardItem *general_item = new GeneralCardItem(general);
        general_item->setProperty("source", general);
        general_item->setFlag(QGraphicsItem::ItemIsFocusable);
        general_item->setZValue(z--);

        if (viewOnly || singleResult) {
            general_item->setFlag(QGraphicsItem::ItemIsMovable, false);
        } else {
            general_item->setAutoBack(true);
            connect(general_item, &GeneralCardItem::released, this, &ChooseGeneralBox::_adjust);
        }

        if (!viewOnly) {
            connect(general_item, &GeneralCardItem::clicked, this, &ChooseGeneralBox::_onItemClicked);
        }

        m_items << general_item;
        general_item->setParentItem(this);
    }

    if (!singleResult && !viewOnly)
        _showAllCompanions();

    moveToCenter();
    show();

    int card_width = G_COMMON_LAYOUT.m_cardNormalWidth;
    int card_height = G_COMMON_LAYOUT.m_cardNormalHeight;
    int first_row = (m_generalNumber < 6) ? m_generalNumber : ((m_generalNumber + 1) / 2);

    for (int i = 0; i < m_items.length(); ++i) {
        GeneralCardItem *card_item = m_items.at(i);
        QPointF pos;
        if (i < first_row) {
            pos.setX(left_blank_width + ((card_width + card_to_center_line) * i) + (card_width / 2));
            pos.setY(top_blank_width + (card_height / 2));
        } else {
            if (m_items.length() % 2 == 1) {
                pos.setX(left_blank_width + (card_width / 2) + (card_to_center_line / 2) 
                       + ((card_width + card_to_center_line) * (i - first_row)) + (card_width / 2));
            } else {
                pos.setX(left_blank_width + ((card_width + card_to_center_line) * (i - first_row)) + (card_width / 2));
            }
            pos.setY(top_blank_width + card_height + card_to_center_line + (card_height / 2));
        }

        card_item->setPos(25, 45);
        if (!singleResult && !viewOnly)
            card_item->setData(S_DATA_INITIAL_HOME_POS, pos);
        card_item->setHomePos(pos);
        card_item->goBack(true);
    }

    if (singleResult) {
        m_confirmButton->hide();
    } else {
        m_confirmButton->setPos(boundingRect().center().x() - (m_confirmButton->boundingRect().width() / 2), 
                                boundingRect().height() - 60);
        m_confirmButton->show();
    }
    if (!viewOnly && !singleResult)
        _initializeItems();

    if (viewOnly || ServerInfo.OperationTimeout != 0) {
        if (m_progressBar == nullptr) {
            m_progressBar = new QSanCommandProgressBar();
            m_progressBar->setMaximumWidth(boundingRect().width() - 10);
            m_progressBar->setMaximumHeight(12);
            m_progressBar->setTimerEnabled(true);
            m_progressBarItem = new QGraphicsProxyWidget(this);
            m_progressBarItem->setWidget(m_progressBar);
            m_progressBarItem->setPos(boundingRect().center().x() - (m_progressBarItem->boundingRect().width() / 2), 
                                       boundingRect().height() - 20);
            connect(m_progressBar, &QSanCommandProgressBar::timedOut, this, &ChooseGeneralBox::reply);
        }
        if (viewOnly) {
            Countdown countdown;
            countdown.max = 10000;
            countdown.type = Countdown::S_COUNTDOWN_USE_SPECIFIED;
            m_progressBar->setCountdown(countdown);
        } else {
            m_progressBar->setCountdown(S_COMMAND_CHOOSE_GENERAL);
        }
        m_progressBar->show();
    }
}

void ChooseGeneralBox::_showAllCompanions()
{
    foreach (GeneralCardItem *card, m_items) {
        card->hideCompanion();
        const General *hero = Sanguosha->getGeneral(card->objectName());
        if (!hero) continue;
        foreach (GeneralCardItem *other, m_items) {
            if (card == other) continue;
            if (hero->isCompanionWith(other->objectName())) {
                card->showCompanion();
                break;
            }
        }
    }
}

void ChooseGeneralBox::_hideAllCompanions()
{
    foreach (GeneralCardItem *card, m_items) {
        card->hideCompanion();
    }
}

void ChooseGeneralBox::_adjust()
{
    GeneralCardItem *item = qobject_cast<GeneralCardItem *>(sender());
    if (item == nullptr)
        return;

    int middle_y = top_blank_width + G_COMMON_LAYOUT.m_cardNormalHeight + card_bottom_to_split_line;
    if (m_generalNumber > 5)
        middle_y += (card_to_center_line + G_COMMON_LAYOUT.m_cardNormalHeight);

    if (m_selected.contains(item) && item->y() <= middle_y) {
        m_selected.removeOne(item);
        m_items << item;
        item->setHomePos(item->data(S_DATA_INITIAL_HOME_POS).toPointF());
        item->goBack(true);
    } else if (m_selected.length() == 2
               && ((!Sanguosha->getGeneral(m_selected.first()->objectName())->isLord() 
                    && m_selected.first() == item && item->x() > boundingRect().center().x())
                   || (m_selected.last() == item && item->x() < boundingRect().center().x()))) {
        qSwap(m_selected[0], m_selected[1]);
    } else if (m_items.contains(item) && item->y() > middle_y) {
        if (m_selected.length() > 1)
            return;
        m_items.removeOne(item);
        m_selected << item;
    }

    adjustItems();
}

void ChooseGeneralBox::adjustItems()
{
    if (!m_selected.isEmpty()) {
        const int card_width = G_COMMON_LAYOUT.m_cardNormalWidth;
        const int card_height = G_COMMON_LAYOUT.m_cardNormalHeight;
        int dest_seat_y = top_blank_width + G_COMMON_LAYOUT.m_cardNormalHeight 
                        + card_bottom_to_split_line + split_line_to_card_seat + (card_height / 2) - 1;
        if (m_generalNumber > 5)
            dest_seat_y += (card_to_center_line + card_height);

        m_selected.first()->setHomePos(QPointF(boundingRect().center().x() - card_to_center_line - (card_width / 2) - 2, dest_seat_y));
        m_selected.first()->goBack(true);
        if (m_selected.length() == 2) {
            m_selected.last()->setHomePos(QPointF(boundingRect().center().x() + card_to_center_line + (card_width / 2) - 1, dest_seat_y));
            m_selected.last()->goBack(true);
        }
    }

    if (m_selected.length() == 2) {
        foreach (GeneralCardItem *card, m_items)
            card->setFrozen(true);
        const General *gen1 = Sanguosha->getGeneral(m_selected.first()->objectName());
        const General *gen2 = Sanguosha->getGeneral(m_selected.last()->objectName());
        bool can = (gen1->getKingdom() == gen2->getKingdom() 
                 || gen1->getKingdom() == "zhu" 
                 || gen2->getKingdom() == "zhu");
        m_confirmButton->setEnabled(can);
    } else if (m_selected.length() == 1) {
        m_selected.first()->hideCompanion();
        const General *selected_general = Sanguosha->getGeneral(m_selected.first()->objectName());
        foreach (GeneralCardItem *card, m_items) {
            const General *general = Sanguosha->getGeneral(card->objectName());
            if (general->getKingdom() != selected_general->getKingdom() 
                && general->getKingdom() != "zhu" 
                && selected_general->getKingdom() != "zhu") {
                if (!card->isFrozen())
                    card->setFrozen(true);
                card->hideCompanion();
            } else {
                if (card->isFrozen())
                    card->setFrozen(false);
                if (general->isCompanionWith(m_selected.first()->objectName())) {
                    m_selected.first()->showCompanion();
                    card->showCompanion();
                } else {
                    card->hideCompanion();
                }
            }
        }
        m_confirmButton->setEnabled(false);
    } else {
        _initializeItems();
        _showAllCompanions();
        m_confirmButton->setEnabled(false);
    }
}

void ChooseGeneralBox::_initializeItems()
{
    foreach (GeneralCardItem *item, m_items)
        item->setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);

    int index = 0;
    foreach (GeneralCardItem *item, m_items) {
        if (item->isFrozen())
            item->setFrozen(false);
        if (Self->isDead() && item->isFrozen())
            item->setFrozen(false);
        ++index;
    }
}

void ChooseGeneralBox::reply()
{
    if (m_progressBar != nullptr) {
        m_progressBar->hide();
        m_progressBar->deleteLater();
        m_progressBar = nullptr;
    }

    if (m_viewOnly) {
        clear();
        return;
    }

    QString generals;
    if (!m_selected.isEmpty()) {
        generals = m_selected.first()->objectName();
        if (m_selected.length() == 2)
            generals += ("+" + m_selected.last()->objectName());
    }
    ClientInstance->onPlayerChooseGeneral(generals);
}

void ChooseGeneralBox::clear()
{
    foreach (GeneralCardItem *card_item, m_items)
        card_item->deleteLater();
    foreach (GeneralCardItem *card_item, m_selected)
        card_item->deleteLater();
    m_items.clear();
    m_selected.clear();
    disappear();
}

void ChooseGeneralBox::_onItemClicked()
{
    GeneralCardItem *item = qobject_cast<GeneralCardItem *>(sender());
    if (item == nullptr)
        return;

    if (m_singleResult) {
        m_selected << item;
        reply();
        return;
    }

    if (m_selected.contains(item)) {
        m_selected.removeOne(item);
        m_items << item;
        item->setHomePos(item->data(S_DATA_INITIAL_HOME_POS).toPointF());
        item->goBack(true);
    } else if (m_items.contains(item)) {
        if (m_selected.length() > 1)
            return;
        m_items.removeOne(item);
        m_selected << item;
    }

    adjustItems();
}
