#ifndef GRAPHICSBOX_H
#define GRAPHICSBOX_H

#include <QGraphicsObject>

class GraphicsBox : public QGraphicsObject
{
    Q_OBJECT

public:
    explicit GraphicsBox(const QString &title = QString());
    ~GraphicsBox() override;

    static void paintGraphicsBoxStyle(QPainter *painter, const QString &title, const QRectF &rect);
    static void stylize(QGraphicsObject *target);
    static void moveToCenter(QGraphicsObject *target);

protected:
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    QRectF boundingRect() const override = 0;

    virtual void paintLayout(QPainter *painter)
    {
        Q_UNUSED(painter)
    }

    void moveToCenter();
    void disappear();

    QString title;
};

#endif