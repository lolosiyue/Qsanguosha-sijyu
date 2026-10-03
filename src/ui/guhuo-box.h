#ifndef _GUHUO_BOX_H
#define _GUHUO_BOX_H

#include "qsan-selectable-item.h"

class CardItem;

// The declaration shows the card back; resolution reveals the actual card.
class GuhuoBox : public QSanSelectableItem
{
    Q_OBJECT
    Q_PROPERTY(qreal flip READ flip WRITE setFlip)

public:
    GuhuoBox();
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

    qreal flip() const { return m_flip; }
    void setFlip(qreal value);

public slots:
    void doGuhuoBox(const QString &phase, const QString &yuji,
                    const QString &declared, int realId);

private:
    QString translatedDeclared(const QString &raw) const;

    QString m_title;
    QString m_declaredText;
    CardItem *m_card;
    qreal m_flip;

    static const int kCardW;
    static const int kCardH;
};

#endif
