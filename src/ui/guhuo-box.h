#ifndef _GUHUO_BOX_H
#define _GUHUO_BOX_H

#include "qsan-selectable-item.h"

class CardItem;

// 蠱惑聲明牌中央提示：聲明時顯示牌背，結算時翻開實際牌。
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
