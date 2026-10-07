#ifndef TABLE_BUTTON_LEGEND_H
#define TABLE_BUTTON_LEGEND_H

#include <QGraphicsObject>
#include <QList>
#include <QString>

// Big-picture button legend for the game table ("South Select  East Cancel ...").
// Presentation only: it mirrors the actions DesktopGamePresentation accepts.
class TableButtonLegend final : public QGraphicsObject
{
    Q_OBJECT
public:
    struct Entry
    {
        QString glyph;
        QString label;
        bool operator==(const Entry &other) const { return glyph == other.glyph && label == other.label; }
    };

    explicit TableButtonLegend(QGraphicsItem *parent = nullptr);

    void setEntries(const QList<Entry> &entries);
    const QList<Entry> &entries() const { return m_entries; }

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    void relayout();

    struct Slot { QRectF cap; QRectF text; };
    QList<Entry> m_entries;
    QList<Slot> m_slots;
    QRectF m_rect;
};

#endif
