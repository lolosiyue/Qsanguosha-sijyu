#include "table-button-legend.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

namespace {
// Scene pixels; the table scene is scaled to the window (x1.5 at 1080p).
const qreal kHeight = 32;
const qreal kPadding = 12;
const qreal kCapHeight = 22;
const qreal kCapPadding = 7;
const qreal kGap = 6;
const qreal kEntrySpacing = 18;

QFont glyphFont()
{
    QFont font = QApplication::font();
    font.setPixelSize(15);
    font.setBold(true);
    return font;
}

QFont labelFont()
{
    QFont font = QApplication::font();
    font.setPixelSize(16);
    return font;
}
}

TableButtonLegend::TableButtonLegend(QGraphicsItem *parent)
    : QGraphicsObject(parent)
{
    setAcceptedMouseButtons(Qt::NoButton);
    setAcceptHoverEvents(false);
}

void TableButtonLegend::setEntries(const QList<Entry> &entries)
{
    if (entries == m_entries) return;
    m_entries = entries;
    relayout();
}

void TableButtonLegend::relayout()
{
    const QFontMetricsF glyphMetrics(glyphFont());
    const QFontMetricsF labelMetrics(labelFont());
    prepareGeometryChange();
    m_slots.clear();
    qreal x = kPadding;
    for (const Entry &entry : m_entries) {
        const qreal capWidth = qMax(kCapHeight, glyphMetrics.horizontalAdvance(entry.glyph) + 2 * kCapPadding);
        const QRectF cap(x, (kHeight - kCapHeight) / 2, capWidth, kCapHeight);
        x += capWidth + kGap;
        const qreal textWidth = labelMetrics.horizontalAdvance(entry.label);
        const QRectF text(x, 0, textWidth, kHeight);
        x += textWidth + kEntrySpacing;
        m_slots.append({ cap, text });
    }
    const qreal width = m_entries.isEmpty() ? 0 : x - kEntrySpacing + kPadding;
    m_rect = QRectF(0, 0, width, kHeight);
    update();
}

QRectF TableButtonLegend::boundingRect() const
{
    return m_rect;
}

void TableButtonLegend::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    if (m_entries.isEmpty()) return;
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(12, 14, 20, 200));
    painter->drawRoundedRect(m_rect, kHeight / 2, kHeight / 2);

    for (int i = 0; i < m_entries.size(); ++i) {
        const Slot &slot = m_slots.at(i);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(236, 238, 242));
        painter->drawRoundedRect(slot.cap, kCapHeight / 2, kCapHeight / 2);
        painter->setPen(QColor(18, 20, 26));
        painter->setFont(glyphFont());
        painter->drawText(slot.cap, Qt::AlignCenter, m_entries.at(i).glyph);
        painter->setPen(QColor(244, 246, 250));
        painter->setFont(labelFont());
        painter->drawText(slot.text, Qt::AlignVCenter | Qt::AlignLeft, m_entries.at(i).label);
    }
}
