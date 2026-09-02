#ifndef POINTER_EFFECT_OVERLAY_H
#define POINTER_EFFECT_OVERLAY_H

#include <QCursor>
#include <QElapsedTimer>
#include <QImage>
#include <QPixmap>
#include <QPointF>
#include <QQuickPaintedItem>
#include <QRect>
#include <QTimer>
#include <QVector>
#include <QWidget>

class QPainter;

class PointerFxEngine
{
public:
    PointerFxEngine();

    void reset();
    qreal elapsed() const;
    QRect tick(const QPointF &localPos, bool inside, Qt::MouseButtons buttons);
    void paint(QPainter &painter);
    bool hasContent() const;
    bool hasBaCursor() const;
    QCursor baCursor() const;

private:
    struct TrailPoint {
        QPointF pos;
        qreal time = 0;
    };
    struct MeshParticle {
        qreal startSize = 0;
        qreal initialRotation = 0;
        qreal rotationBlend = 0;
    };
    struct TriangleParticle {
        QPointF origin;
        QPointF shapeOffset;
        QPointF direction;
        qreal startedAt = 0;
        qreal lifetime = 0;
        qreal speed = 0;
        qreal size = 0;
        bool alternateFrame = false;
    };
    struct ClickEffect {
        QPointF position;
        qreal startedAt = 0;
        QVector<MeshParticle> meshParticles;
        QVector<TriangleParticle> triangles;
    };

    void spawnClick(const QPointF &pos, qreal now);
    TriangleParticle createTriangle(const QPointF &pos, qreal now, bool movement);
    void updateTrail(const QPointF &pos, qreal now);
    void pruneExpired(qreal now);
    bool hasVisibleContent(qreal now) const;
    QRect visibleBounds(qreal now) const;
    QPointF trianglePosition(const TriangleParticle &particle, qreal now) const;
    void drawTrail(QPainter &painter, qreal now);
    void drawClickEffects(QPainter &painter, qreal now);
    void drawTriangles(QPainter &painter, qreal now, const QVector<TriangleParticle> &particles);
    void drawSprite(QPainter &painter, const QPixmap &pixmap, const QRectF &src,
                    const QPointF &center, const QSizeF &size, qreal rotationRad,
                    qreal opacity);
    static QImage loadAsset(const QString &fileName, bool luminanceAsAlpha = false);
    static QString assetPath(const QString &fileName);
    static QColor lerpColor(const QColor &from, const QColor &to, qreal t);
    static qreal lerp(qreal from, qreal to, qreal t);
    static qreal smoothStep(qreal value);
    static qreal cubicHermite(qreal from, qreal to, qreal outgoing, qreal incoming,
                              qreal normalizedTime, qreal duration);
    static qreal ringSizeCurve(qreal progress);
    static qreal meshTriSizeCurve(qreal progress);
    static qreal meshTriRotationDelta(qreal progress, qreal blend);
    static qreal meshTriDissolveThreshold(qreal progress);
    static QColor meshTriColor(qreal progress);
    static qreal triangleSizeCurve(qreal progress);
    static qreal triangleOpacity(qreal progress);
    static QColor triangleColor(qreal progress);
    static QColor trailColor(qreal progress);

    QElapsedTimer m_clock;
    QPixmap m_circlePm;
    QPixmap m_circleBluePm;
    QPixmap m_ringPm;
    QPixmap m_ringBluePm;
    QPixmap m_trianglePm;
    QCursor m_baCursor;
    QVector<TrailPoint> m_trailPoints;
    QVector<ClickEffect> m_clickEffects;
    QVector<TriangleParticle> m_moveParticles;
    QPointF m_lastTrailPos;
    qreal m_emissionCarry = 0;
    Qt::MouseButtons m_prevButtons = Qt::NoButton;
    bool m_hasBaCursor = false;
    bool m_hasLastTrailPos = false;
};

class PointerEffectOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit PointerEffectOverlay(QWidget *host);
    ~PointerEffectOverlay() override;

    void syncToHost();
    void setPageEnabled(bool enabled);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void onFrame();
    void setActive(bool active);
    void applyCursor(bool inside);

    QWidget *m_host = nullptr;
    QTimer m_timer;
    PointerFxEngine m_fx;
    QRect m_syncedGeo;
    QRect m_lastPainted;
    bool m_active = false;
    bool m_pageEnabled = false;
    bool m_cursorOverridden = false;
    bool m_lastHadContent = true;
};

class HomePointerFxItem : public QQuickPaintedItem
{
    Q_OBJECT

public:
    explicit HomePointerFxItem(QQuickItem *parent = nullptr);
    ~HomePointerFxItem() override;

    void paint(QPainter *painter) override;

protected:
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private:
    void onFrame();
    void applyCursor(bool inside);

    PointerFxEngine m_fx;
    QTimer m_timer;
    QRect m_lastPainted;
    bool m_cursorOverridden = false;
    bool m_hadContent = false;
};

#endif
