#include "guhuo-box.h"

#include "carditem.h"
#include "client.h"
#include "clientplayer.h"
#include "engine.h"
#include "skin-bank.h"

#include <QPainter>
#include <QPropertyAnimation>

namespace GuhuoFrame {
const int kBorder = 9;
const int kTitleH = 30;
const int kPad = 14;

void paintFrame(QPainter *painter, const QRectF &outer, const QString &title)
{
    const QColor panel(8, 9, 4, 198);
    const QColor gold(166, 150, 122, 220);
    const QColor dark(5, 3, 2, 150);
    painter->save();
    painter->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    painter->fillRect(outer, panel);
    painter->setBrush(Qt::NoBrush);
    const QRectF rect = outer.adjusted(0.5, 0.5, -0.5, -0.5);
    painter->setPen(QPen(QColor(0, 0, 0, 70), 1.0));
    painter->drawRect(rect);
    painter->setPen(QPen(dark, 1.0));
    painter->drawRect(rect.adjusted(2, 2, -2, -2));
    painter->setPen(QPen(gold, 1.0));
    painter->drawRect(rect.adjusted(3, 3, -3, -3));
    painter->setPen(QPen(dark, 1.0));
    painter->drawRect(rect.adjusted(4, 4, -4, -4));
    QFont font;
    font.setFamily(QStringLiteral("Microsoft JhengHei"));
    font.setPixelSize(18);
    font.setBold(true);
    painter->setFont(font);
    painter->setPen(QColor(232, 210, 150));
    painter->drawText(QRectF(outer.left(), outer.top() + kBorder, outer.width(), 28),
                      Qt::AlignCenter, title);
    painter->restore();
}

void paintSlot(QPainter *painter, const QRectF &slot)
{
    painter->save();
    painter->fillRect(slot, QColor(0, 0, 0, 90));
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(QColor(166, 150, 122, 90), 1.0));
    painter->drawRect(slot.adjusted(0.5, 0.5, -0.5, -0.5));
    painter->restore();
}
}

const int GuhuoBox::kCardW = 93;
const int GuhuoBox::kCardH = 130;

GuhuoBox::GuhuoBox()
    : QSanSelectableItem(false), m_card(nullptr), m_flip(0)
{
    _m_width = kCardW + 2 * (GuhuoFrame::kBorder + GuhuoFrame::kPad);
    _m_height = kCardH + GuhuoFrame::kBorder + GuhuoFrame::kPad + GuhuoFrame::kTitleH
        + GuhuoFrame::kBorder + GuhuoFrame::kPad;
    setTransform(QTransform::fromTranslate(-_m_width / 2.0, -_m_height / 2.0));
    prepareGeometryChange();
    setAcceptedMouseButtons(Qt::NoButton);
}

QString GuhuoBox::translatedDeclared(const QString &raw) const
{
    if (raw == QStringLiteral("peach+analeptic"))
        return tr("Peach or Analeptic");
    QString name = raw;
    if (name == QStringLiteral("normal_slash"))
        name = QStringLiteral("slash");
    return Sanguosha->translate(name);
}

void GuhuoBox::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    GuhuoFrame::paintFrame(painter, QRectF(0, 0, _m_width, _m_height), m_title);

    const QRectF slot(GuhuoFrame::kBorder + GuhuoFrame::kPad,
                      GuhuoFrame::kBorder + GuhuoFrame::kPad + GuhuoFrame::kTitleH,
                      kCardW, kCardH);
    GuhuoFrame::paintSlot(painter, slot);
    if (m_card && m_flip >= 0.5)
        return;

    const qreal sx = m_card ? (1.0 - 2.0 * m_flip) : 1.0;
    if (sx <= 0.001)
        return;

    painter->save();
    painter->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform
                            | QPainter::TextAntialiasing);
    const QPointF center = slot.center();
    painter->translate(center);
    painter->scale(sx, 1.0);
    painter->translate(-center);

    const QPixmap back = G_ROOM_SKIN.getPixmap(QStringLiteral("handCardBack"), QString(), true);
    if (!back.isNull())
        painter->drawPixmap(slot, back, QRectF(back.rect()));
    else
        painter->fillRect(slot, QColor(20, 22, 14));

    if (!m_card && !m_declaredText.isEmpty()) {
        const QRectF band(slot.left(), slot.center().y() - 32, slot.width(), 64);
        painter->fillRect(band, QColor(0, 0, 0, 175));
        QFont font;
        font.setFamily(QStringLiteral("Microsoft JhengHei"));
        font.setPixelSize(14);
        font.setBold(true);
        painter->setFont(font);
        painter->setPen(QColor(232, 210, 150));
        painter->drawText(band, Qt::AlignCenter | Qt::TextWordWrap, m_declaredText);
    }
    painter->restore();
}

void GuhuoBox::setFlip(qreal value)
{
    m_flip = value;
    if (m_card) {
        const bool faceUp = value >= 0.5;
        m_card->setVisible(faceUp);
        if (faceUp) {
            const qreal sx = qMax(0.0001, 2.0 * value - 1.0);
            const qreal width = G_COMMON_LAYOUT.m_cardNormalWidth;
            const qreal height = G_COMMON_LAYOUT.m_cardNormalHeight;
            m_card->setTransform(QTransform::fromTranslate(-width / 2.0, -height / 2.0)
                                 * QTransform::fromScale(sx, 1.0));
        }
    }
    update();
}

void GuhuoBox::doGuhuoBox(const QString &phase, const QString &yuji,
                           const QString &declared, int realId)
{
    if (phase == QStringLiteral("clear")) {
        hide();
        if (m_card) {
            m_card->deleteLater();
            m_card = nullptr;
        }
        m_flip = 0;
        return;
    }

    const ClientPlayer *player = ClientInstance ? ClientInstance->getPlayer(yuji) : nullptr;
    const QString who = player ? Sanguosha->translate(player->getGeneralName()) : QString();
    m_title = tr("%1 invokes Guhuo").arg(who);
    m_declaredText = tr("Declared: %1").arg(translatedDeclared(declared));

    if (phase == QStringLiteral("declare")) {
        if (m_card) {
            m_card->deleteLater();
            m_card = nullptr;
        }
        m_flip = 0;
        show();
        update();
        return;
    }

    if (phase != QStringLiteral("reveal"))
        return;
    const Card *card = Sanguosha->getCard(realId);
    if (!card)
        return;
    if (m_card) {
        m_card->deleteLater();
        m_card = nullptr;
    }
    m_card = new CardItem(card);
    m_card->setParentItem(this);
    m_card->setFlag(QGraphicsItem::ItemIsMovable, false);
    m_card->setAcceptedMouseButtons(Qt::NoButton);
    m_card->setAutoBack(false);
    m_card->setPos(GuhuoFrame::kBorder + GuhuoFrame::kPad + kCardW / 2.0,
                   GuhuoFrame::kBorder + GuhuoFrame::kPad + GuhuoFrame::kTitleH + kCardH / 2.0);
    m_card->setVisible(false);
    show();

    QPropertyAnimation *animation = new QPropertyAnimation(this, "flip");
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    animation->setDuration(360);
    animation->setEasingCurve(QEasingCurve::InOutQuad);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}
