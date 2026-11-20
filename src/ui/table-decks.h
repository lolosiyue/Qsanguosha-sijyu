#ifndef _TABLE_DECKS_H
#define _TABLE_DECKS_H

#include "player.h"
#include <QGraphicsObject>
#include <QList>
#include <QMap>

class CardItem;
class TablePile;

// Table furniture around the processing area: the draw pile, the discard pile,
// the processing tray and one small stack per public table zone (ren_pile, ...).
// Pure presentation: RoomScene pushes counts in, TablePile/RoomScene read anchors.
class TableDecks : public QGraphicsObject
{
    Q_OBJECT

public:
    TableDecks();

    // Takes the area the processing pile would have used and returns the part
    // left for it once the two piles are placed beside it. Hides the decks and
    // returns the area unchanged when it is too narrow.
    QSize setTrayArea(const QSize &area);

    void setDrawCount(int count);
    void setDiscard(const QList<int> &discardTopFirst);
    // Cards still shown on the processing area are not on top of the discard pile yet.
    void setTablePile(const TablePile *pile)
    {
        m_tablePile = pile;
    }
    void refreshTopDiscard();
    void setZone(const QString &name, const QList<int> &cardIds);
    void clearZones();

    // Scene-space centre of the pile a card leaves from (DrawPile, DiscardPile,
    // or a table zone that holds cardId).
    bool sourceAnchor(Player::Place place, int cardId, QPointF *scenePos) const;
    // Scene-space centre of the pile a card on the processing area rests in now.
    bool restingAnchor(int cardId, QPointF *scenePos) const;

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

signals:
    void discardPileClicked();
    void zoneClicked(const QString &name);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;

private:
    QRectF drawSlot() const;
    QRectF discardSlot() const;
    QRectF zoneSlot(int index) const;
    QSizeF slotCardSize() const;
    void paintStack(QPainter *painter, const QRectF &slot, int count, bool faceDown) const;
    void paintLabel(QPainter *painter, const QRectF &slot, const QString &text) const;
    void updateTopDiscard();

    QSizeF m_tray;
    qreal m_cardScale = 0.8;
    int m_drawCount = 0;
    QList<int> m_discard;
    QMap<QString, QList<int>> m_zones;
    CardItem *m_topDiscard = nullptr;
    const TablePile *m_tablePile = nullptr;
};

#endif
