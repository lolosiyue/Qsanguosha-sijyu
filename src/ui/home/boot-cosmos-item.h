#ifndef _BOOT_COSMOS_ITEM_H
#define _BOOT_COSMOS_ITEM_H

#include <QQuickItem>

// The boot splash's drawn scene: a Big Bang that settles into a black hole
// (qml/home/shaders/boot-cosmos.frag). Its clock runs on the render thread, so the
// scene keeps moving while the engine blocks the GUI thread; UniformAnimator would not,
// since it advances from the GUI thread.
class BootCosmosItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(qreal cornerRadius READ cornerRadius WRITE setCornerRadius NOTIFY cornerRadiusChanged)

public:
    explicit BootCosmosItem(QQuickItem *parent = nullptr);

    qreal cornerRadius() const;
    void setCornerRadius(qreal radius);

signals:
    void cornerRadiusChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;

private:
    qreal m_cornerRadius = 0;
};

#endif
