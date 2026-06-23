#ifndef CHOOSESUITBOX_H
#define CHOOSESUITBOX_H

#include "graphicsbox.h"

class Button;
class QGraphicsProxyWidget;
class QSanCommandProgressBar;

class SuitOptionButton : public QGraphicsObject
{
    Q_OBJECT

    friend class ChooseSuitBox;

signals:
    void clicked();
    void hovered(bool entering);

protected:
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override;
    QRectF boundingRect() const override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override;

private:
    SuitOptionButton(QGraphicsObject *parent, const QString &suit, int width);

    QString m_suit;
    int width;
};

class ChooseSuitBox : public GraphicsBox
{
    Q_OBJECT

public:
    ChooseSuitBox();

    QRectF boundingRect() const override;

    void chooseSuit(const QStringList &suits);
    void clear();

private:
    int suitNumber;
    QStringList m_suits;
    QList<SuitOptionButton *> buttons;

    static const int outerBlankWidth;
    static const int buttonWidth;
    static const int buttonHeight;
    static const int interval;
    static const int topBlankWidth;
    static const int bottomBlankWidth;

    QGraphicsProxyWidget *progressBarItem;
    QSanCommandProgressBar *progressBar;

    void reply();
};

#endif
