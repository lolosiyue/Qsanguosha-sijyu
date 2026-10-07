#ifndef QML_TABLE_LAYER_H
#define QML_TABLE_LAYER_H

#include "room-layout-engine.h"

#include <QHash>
#include <QJsonObject>
#include <QMap>
#include <QPointer>
#include <QQuickWidget>
#include <QSet>
#include <QVariantMap>
#include <functional>

class QGraphicsView;
class QQmlComponent;
class QQuickItem;
class QTimer;

// One seat as the layer sees it: scene geometry plus a value snapshot of its
// player. Never a ClientPlayer*: the layer and its QML may outlive the player.
struct QmlSeatGeometry
{
    QString player;
    QRectF sceneRect;      // Photo, or the dashboard avatar area for the dashboard seat.
    qreal itemScale = 1.0; // The seat item's own scale in the scene.
    bool visible = true;
    bool self = false;
    QVariantMap snapshot;  // {objectName, general, seat, kingdom, alive, self}
};

// Table zones in scene coordinates, filled for both the legacy and the responsive layout.
struct QmlTableGeometry
{
    QRectF mainRect;
    QRectF headerRect;      // Invalid when the layout has no header.
    QRectF interactionRect; // Dashboard / hand area; table and screen elements stay off it.
    QPointF tableCenter;
    RoomLayoutEngine::Profile profile = RoomLayoutEngine::Profile::LegacyLandscape;
    bool compactSeats = false;
};

// Transparent QML layer over the room view for extension elements
// (Room::addQmlElement, Engine::addQmlMark). See docs/qml-table-elements.md.
class QmlTableLayer final : public QQuickWidget
{
    Q_OBJECT
public:
    using GeometryProvider = std::function<void(QList<QmlSeatGeometry> *, QmlTableGeometry *)>;

    QmlTableLayer(QGraphicsView *view, QWidget *parent);
    ~QmlTableLayer() override;

    void setGeometryProvider(GeometryProvider provider);
    void setVisualMode(qreal saturation, qreal contrast);
    // Smoke-test evidence: every live element with its view geometry and data.
    QJsonObject snapshot() const;

public slots:
    void handleElement(const QVariantMap &payload);
    void setMark(const QString &player, const QString &mark, int value);
    void scheduleRelayout();

signals:
    // Emitted after a relayout that follows an element being added, updated or removed.
    void elementsChanged();

protected:
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Element
    {
        QString source;   // Relative .qml path as the room sent it.
        QString anchor;
        QString player;   // Seat the element follows; empty for table and screen anchors.
        QVariantMap data;
        QPointer<QQuickItem> frame; // Positioning wrapper owned by the layer.
        QPointer<QQuickItem> item;  // The extension's root item.
        QString error;
        QRectF viewRect;
        QRectF seatRect;
        bool visible = false;
        QVariantMap lastQs;
    };

    void addElement(const QString &key, Element element);
    void removeElement(const QString &key);
    QQuickItem *createItem(Element &element);
    void relayout();
    bool interactiveAt(const QPointF &pos) const;
    void setPassThrough(bool passThrough);

    QPointer<QGraphicsView> m_view;
    GeometryProvider m_provider;
    QPointer<QQuickItem> m_content;
    QMap<QString, Element> m_elements; // Sorted keys give a stable stacking order.
    QHash<QString, QQmlComponent *> m_components;
    QSet<QString> m_reportedErrors;
    QList<QRectF> m_interactiveRects;
    QTimer *m_relayoutTimer = nullptr;
    QmlTableGeometry m_table;
    bool m_reportPending = false;
    bool m_forwarding = false;
};

#endif
