#include "table-decks.h"
#include "carditem.h"
#include "engine.h"
#include "skin-bank.h"
#include "table-pile.h"

#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace {
const qreal kGap = 14.0;
const qreal kLabelHeight = 18.0;
const qreal kMaxCardScale = 0.8;
const qreal kMinCardScale = 0.45;
const qreal kZoneCardScale = 0.34;
const qreal kZoneWidth = 84.0;
const qreal kZoneSpacing = 8.0;
const int kMaxLayers = 6;

int layersFor(int count)
{
    return count <= 0 ? 0 : qBound(1, (count + 9) / 10, kMaxLayers);
}
}

TableDecks::TableDecks()
{
    setFlag(QGraphicsItem::ItemHasNoContents, false);
    setVisible(false);
}

QSizeF TableDecks::slotCardSize() const
{
    return QSizeF(G_COMMON_LAYOUT.m_cardNormalWidth, G_COMMON_LAYOUT.m_cardNormalHeight) * m_cardScale;
}

QSize TableDecks::setTrayArea(const QSize &area)
{
    const qreal cardW = G_COMMON_LAYOUT.m_cardNormalWidth;
    const qreal cardH = G_COMMON_LAYOUT.m_cardNormalHeight;
    const qreal scale = qMin(kMaxCardScale, (area.height() - kLabelHeight - 6.0) / cardH);
    const qreal side = cardW * scale + kGap * 2;
    const qreal processing = area.width() - side * 2;
    prepareGeometryChange();
    if (scale < kMinCardScale || processing < cardW * 1.5) {
        setVisible(false);
        return area;
    }
    m_cardScale = scale;
    m_tray = QSizeF(processing, area.height());
    setVisible(true);
    updateTopDiscard();
    update();
    return QSize(int(processing), area.height());
}

void TableDecks::setDrawCount(int count)
{
    if (m_drawCount == count)
        return;
    m_drawCount = count;
    update();
}

void TableDecks::setDiscard(const QList<int> &discardTopFirst)
{
    m_discard = discardTopFirst;
    updateTopDiscard();
    update();
}

void TableDecks::setZone(const QString &name, const QList<int> &cardIds)
{
    prepareGeometryChange();
    if (cardIds.isEmpty())
        m_zones.remove(name);
    else
        m_zones.insert(name, cardIds);
    update();
}

void TableDecks::clearZones()
{
    prepareGeometryChange();
    m_zones.clear();
    update();
}

QRectF TableDecks::drawSlot() const
{
    const QSizeF card = slotCardSize();
    return QRectF(QPointF(-m_tray.width() / 2 - kGap - card.width(), -(card.height() + kLabelHeight) / 2), card);
}

QRectF TableDecks::discardSlot() const
{
    const QSizeF card = slotCardSize();
    return QRectF(QPointF(m_tray.width() / 2 + kGap, -(card.height() + kLabelHeight) / 2), card);
}

QRectF TableDecks::zoneSlot(int index) const
{
    const qreal height = G_COMMON_LAYOUT.m_cardNormalHeight * kZoneCardScale;
    const int count = m_zones.size();
    const qreal total = count * kZoneWidth + qMax(0, count - 1) * kZoneSpacing;
    return QRectF(-total / 2 + index * (kZoneWidth + kZoneSpacing), m_tray.height() / 2 + 10.0,
        kZoneWidth, height);
}

QRectF TableDecks::boundingRect() const
{
    const qreal halfW = m_tray.width() / 2 + kGap * 2 + slotCardSize().width() + 8.0;
    const qreal zoneH = G_COMMON_LAYOUT.m_cardNormalHeight * kZoneCardScale + 20.0;
    const qreal halfH = m_tray.height() / 2 + (m_zones.isEmpty() ? 8.0 : zoneH);
    return QRectF(-halfW, -halfH, halfW * 2, halfH * 2);
}

void TableDecks::refreshTopDiscard()
{
    updateTopDiscard();
    update();
}

void TableDecks::updateTopDiscard()
{
    // Skip unknown ids and cards still on the processing area; they fly in later.
    int topId = -1;
    for (int id : m_discard) {
        if (id >= 0 && !(m_tablePile && m_tablePile->isShowingCard(id))) {
            topId = id;
            break;
        }
    }
    const Card *card = topId >= 0 ? Sanguosha->getEngineCard(topId) : nullptr;
    if (m_topDiscard && (!card || m_topDiscard->getId() != topId)) {
        m_topDiscard->setVisible(false);
        m_topDiscard->deleteLater();
        m_topDiscard = nullptr;
    }
    if (card && !m_topDiscard) {
        m_topDiscard = new CardItem(card);
        m_topDiscard->setParentItem(this);
        m_topDiscard->setAcceptHoverEvents(false);
        m_topDiscard->setAcceptedMouseButtons(Qt::NoButton);
        m_topDiscard->setFlag(QGraphicsItem::ItemIsMovable, false);
    }
    if (m_topDiscard) {
        // CardItem's origin is its centre (base transform), so scale and place by centre.
        m_topDiscard->setScale(m_cardScale);
        m_topDiscard->setPos(discardSlot().center());
        m_topDiscard->setOpacity(1.0);
    }
}

void TableDecks::paintStack(QPainter *painter, const QRectF &slot, int count, bool faceDown) const
{
    const qreal radius = slot.width() * 0.06;
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0, 0, 0, 70));
    painter->drawRoundedRect(slot.adjusted(-5, -5, 5, 5), radius + 4, radius + 4);
    if (count <= 0) {
        QPen dashed(QColor(255, 235, 200, 80), 1.5, Qt::DashLine);
        painter->setPen(dashed);
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(slot, radius, radius);
        return;
    }
    const int layers = layersFor(count);
    const QPixmap back = G_ROOM_SKIN.getPixmap(QSanRoomSkin::S_SKIN_KEY_HAND_CARD_BACK, QString(), true);
    painter->setBrush(QColor(0, 0, 0, 90));
    painter->drawRoundedRect(slot.translated(3 + layers, 4 + layers * 2), radius, radius);
    for (int k = layers - 1; k >= 0; --k) {
        const QRectF r = slot.translated(k * 0.8, k * 2.0);
        if (faceDown && !back.isNull()) {
            painter->drawPixmap(r, back, back.rect());
        } else {
            painter->setPen(QPen(QColor(110, 90, 60), 1.0));
            painter->setBrush(QColor(236, 226, 200));
            painter->drawRoundedRect(r, radius, radius);
            painter->setPen(Qt::NoPen);
        }
    }
}

void TableDecks::paintLabel(QPainter *painter, const QRectF &slot, const QString &text) const
{
    QFont font = painter->font();
    font.setBold(true);
    font.setPixelSize(12);
    painter->setFont(font);
    const qreal width = qMax(slot.width() + 8.0, QFontMetricsF(font).horizontalAdvance(text) + 14.0);
    const QRectF pill(slot.center().x() - width / 2, slot.bottom() + kMaxLayers * 2.0 - 4.0, width, kLabelHeight);
    painter->setPen(QPen(QColor(230, 190, 120, 140), 1.0));
    painter->setBrush(QColor(20, 12, 6, 170));
    painter->drawRoundedRect(pill, kLabelHeight / 2, kLabelHeight / 2);
    painter->setPen(QColor(245, 230, 195));
    painter->drawText(pill, Qt::AlignCenter, text);
}

void TableDecks::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

    // Processing tray: a recessed frame the played cards land in.
    const QRectF tray(-m_tray.width() / 2, -m_tray.height() / 2, m_tray.width(), m_tray.height());
    QLinearGradient fill(tray.topLeft(), tray.bottomLeft());
    fill.setColorAt(0.0, QColor(0, 0, 0, 75));
    fill.setColorAt(1.0, QColor(0, 0, 0, 35));
    painter->setBrush(fill);
    painter->setPen(QPen(QColor(230, 190, 120, 110), 1.5));
    painter->drawRoundedRect(tray.adjusted(-4, -4, 4, 4), 12, 12);
    painter->setPen(QPen(QColor(255, 255, 255, 25), 1.0));
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(tray.adjusted(-1, -1, 1, 1), 10, 10);

    paintStack(painter, drawSlot(), m_drawCount, true);
    paintLabel(painter, drawSlot(), tr("牌堆 %1").arg(m_drawCount));

    // The top discard is a child CardItem; paint only the layers under it.
    const QRectF discard = discardSlot();
    // Until a discard has settled there is no face to show; keep the slot empty.
    paintStack(painter, discard, m_topDiscard ? m_discard.size() : 0, false);
    paintLabel(painter, discard, tr("棄牌堆 %1").arg(m_discard.size()));

    int index = 0;
    for (auto it = m_zones.cbegin(); it != m_zones.cend(); ++it, ++index) {
        const QRectF chip = zoneSlot(index);
        const QRectF card(chip.topLeft(), QSizeF(G_COMMON_LAYOUT.m_cardNormalWidth * kZoneCardScale, chip.height()));
        paintStack(painter, card, it.value().size(), true);
        QFont font = painter->font();
        font.setBold(true);
        font.setPixelSize(12);
        painter->setFont(font);
        painter->setPen(QColor(245, 230, 195));
        painter->drawText(QRectF(card.right() + 8, chip.top(), chip.right() - card.right() - 8, chip.height()),
            Qt::AlignLeft | Qt::AlignVCenter,
            QStringLiteral("%1\n%2").arg(Sanguosha->translate(it.key())).arg(it.value().size()));
    }
}

bool TableDecks::sourceAnchor(Player::Place place, int cardId, QPointF *scenePos) const
{
    if (!isVisible())
        return false;
    if (place == Player::DrawPile) {
        *scenePos = mapToScene(drawSlot().center());
        return true;
    }
    if (place == Player::DiscardPile) {
        *scenePos = mapToScene(discardSlot().center());
        return true;
    }
    if (place == Player::PlaceTable) {
        int index = 0;
        for (auto it = m_zones.cbegin(); it != m_zones.cend(); ++it, ++index) {
            if (it.value().contains(cardId)) {
                *scenePos = mapToScene(zoneSlot(index).topLeft()
                    + QPointF(G_COMMON_LAYOUT.m_cardNormalWidth * kZoneCardScale / 2, zoneSlot(index).height() / 2));
                return true;
            }
        }
    }
    return false;
}

bool TableDecks::restingAnchor(int cardId, QPointF *scenePos) const
{
    if (!isVisible() || cardId < 0)
        return false;
    if (sourceAnchor(Player::PlaceTable, cardId, scenePos))
        return true;
    if (m_discard.contains(cardId))
        return sourceAnchor(Player::DiscardPile, cardId, scenePos);
    return false;
}

void TableDecks::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (discardSlot().contains(event->pos()) && !m_discard.isEmpty()) {
            emit discardPileClicked();
            return;
        }
        int index = 0;
        for (auto it = m_zones.cbegin(); it != m_zones.cend(); ++it, ++index) {
            if (zoneSlot(index).contains(event->pos())) {
                emit zoneClicked(it.key());
                return;
            }
        }
    }
    event->ignore();
}
