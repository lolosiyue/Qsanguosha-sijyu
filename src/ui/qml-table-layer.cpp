#include "qml-table-layer.h"

#include "engine.h"
#include "pointer-hover-delivery.h"
#include "qml-element-path.h"
#include "runtime-paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGraphicsView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlNetworkAccessManagerFactory>
#include <QQuickItem>
#include <QSurfaceFormat>
#include <QTimer>
#include <QTouchEvent>
#include <QDebug>

namespace {

// Extension QML is local presentation: only file: and qrc: load, everything else fails.
class LocalOnlyNetworkAccessManager final : public QNetworkAccessManager
{
public:
    using QNetworkAccessManager::QNetworkAccessManager;

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *data) override
    {
        const QString scheme = request.url().scheme();
        if (scheme == QLatin1String("file") || scheme == QLatin1String("qrc"))
            return QNetworkAccessManager::createRequest(op, request, data);
        QNetworkRequest blocked(request);
        blocked.setUrl(QUrl(QStringLiteral("qsan-blocked:")));
        return QNetworkAccessManager::createRequest(op, blocked, data);
    }
};

class LocalOnlyNetworkFactory final : public QQmlNetworkAccessManagerFactory
{
public:
    QNetworkAccessManager *create(QObject *parent) override
    {
        return new LocalOnlyNetworkAccessManager(parent);
    }
};

QJsonObject rectJson(const QRectF &rect)
{
    return QJsonObject{{QStringLiteral("x"), rect.x()}, {QStringLiteral("y"), rect.y()},
                       {QStringLiteral("w"), rect.width()}, {QStringLiteral("h"), rect.height()}};
}

QString profileName(RoomLayoutEngine::Profile profile)
{
    switch (profile) {
    case RoomLayoutEngine::Profile::CompactPortrait: return QStringLiteral("portrait");
    case RoomLayoutEngine::Profile::CompactLandscape: return QStringLiteral("compact-landscape");
    case RoomLayoutEngine::Profile::Medium: return QStringLiteral("medium");
    case RoomLayoutEngine::Profile::ExpandedSplit: return QStringLiteral("split");
    case RoomLayoutEngine::Profile::Book: return QStringLiteral("book");
    case RoomLayoutEngine::Profile::Tabletop: return QStringLiteral("tabletop");
    case RoomLayoutEngine::Profile::LargeRoom: return QStringLiteral("large-room");
    case RoomLayoutEngine::Profile::LegacyLandscape: break;
    }
    return QStringLiteral("landscape");
}

constexpr qreal kGap = 4.0;
constexpr qreal kEdge = 8.0;

}

QmlTableLayer::QmlTableLayer(QGraphicsView *view, QWidget *parent)
    : QQuickWidget(parent)
    , m_view(view)
{
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_AlwaysStackOnTop, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_AcceptTouchEvents, true);
    // Alpha-capable render format, as EmbeddedQmlLoader uses for its transparent overlays.
    QSurfaceFormat surfaceFormat = format();
    surfaceFormat.setAlphaBufferSize(8);
    setFormat(surfaceFormat);
    setClearColor(Qt::transparent);
    setFocusPolicy(Qt::NoFocus);
    setResizeMode(QQuickWidget::SizeRootObjectToView);
    setMouseTracking(true);
    static LocalOnlyNetworkFactory networkFactory; // QQmlEngine does not own the factory.
    engine()->setNetworkAccessManagerFactory(&networkFactory);
    engine()->addImportPath(QSanRuntimePaths::assetPath(QStringLiteral(".")));
    setSource(QUrl(QStringLiteral("qrc:/QSanguosha/Table/TableLayer.qml")));
    if (QQuickItem *root = rootObject())
        m_content = root->findChild<QQuickItem *>(QStringLiteral("content"));
    if (!m_content)
        qWarning().noquote() << "QmlTableLayer: TableLayer.qml failed to load:" << errors();

    m_relayoutTimer = new QTimer(this);
    m_relayoutTimer->setSingleShot(true);
    m_relayoutTimer->setInterval(16); // At most one relayout per frame.
    connect(m_relayoutTimer, &QTimer::timeout, this, &QmlTableLayer::relayout);

    setAttribute(Qt::WA_NoMousePropagation, true); // Ignored input goes to the table by hand.
    setPassThrough(true);
    if (m_view) {
        m_view->viewport()->installEventFilter(this);
        setGeometry(m_view->viewport()->rect());
    }
    hide(); // Shown when the first element is mounted; an empty layer costs nothing.
}

QmlTableLayer::~QmlTableLayer()
{
    // Items and components must go before QQuickWidget destroys the engine they belong to.
    for (Element &element : m_elements)
        delete element.frame.data();
    m_elements.clear();
    qDeleteAll(m_components);
    m_components.clear();
}

void QmlTableLayer::setGeometryProvider(GeometryProvider provider)
{
    m_provider = std::move(provider);
    scheduleRelayout();
}

void QmlTableLayer::setVisualMode(qreal saturation, qreal contrast)
{
    if (QQuickItem *root = rootObject()) {
        root->setProperty("saturation", saturation);
        root->setProperty("contrast", contrast);
    }
}

void QmlTableLayer::scheduleRelayout()
{
    if (!m_relayoutTimer->isActive())
        m_relayoutTimer->start();
}

void QmlTableLayer::handleElement(const QVariantMap &payload)
{
    const QString op = payload.value(QStringLiteral("op")).toString();
    const QString scope = payload.value(QStringLiteral("scope")).toString() == QLatin1String("player")
        ? QStringLiteral("player") : QStringLiteral("all");
    const QString key = scope + QLatin1Char('/') + payload.value(QStringLiteral("id")).toString();
    const QVariantMap data = payload.value(QStringLiteral("data")).toMap();
    if (op == QLatin1String("add")) {
        Element element;
        element.source = payload.value(QStringLiteral("qml")).toString();
        element.anchor = payload.value(QStringLiteral("anchor")).toString();
        if (element.anchor.startsWith(QLatin1String("seat:")))
            element.player = element.anchor.mid(5);
        element.data = data;
        addElement(key, element);
    } else if (op == QLatin1String("update")) {
        auto it = m_elements.find(key);
        if (it == m_elements.end())
            return;
        for (auto field = data.cbegin(); field != data.cend(); ++field)
            it->data.insert(field.key(), field.value());
    } else if (op == QLatin1String("remove")) {
        removeElement(key);
    } else if (op == QLatin1String("clear")) {
        const QString prefix = scope + QLatin1Char('/');
        for (const QString &existing : m_elements.keys())
            if (existing.startsWith(prefix))
                removeElement(existing);
    } else {
        qWarning().noquote() << "QmlTableLayer: unknown op" << op;
        return;
    }
    m_reportPending = true;
    scheduleRelayout();
    syncActive();
}

void QmlTableLayer::setMark(const QString &player, const QString &mark, int value)
{
    const QString key = QStringLiteral("mark/%1/%2").arg(player, mark);
    if (value <= 0) {
        removeElement(key);
    } else if (m_elements.contains(key)) {
        m_elements[key].data.insert(QStringLiteral("value"), value);
    } else {
        const QmlMarkBinding binding = Sanguosha->qmlMarkFor(mark);
        if (!binding.isValid())
            return;
        Element element;
        element.source = binding.qmlPath;
        element.anchor = binding.anchor;
        element.player = player;
        element.data = QVariantMap{{QStringLiteral("mark"), mark}, {QStringLiteral("value"), value}};
        addElement(key, element);
    }
    m_reportPending = true;
    scheduleRelayout();
    syncActive();
}

void QmlTableLayer::syncActive()
{
    const bool active = !m_elements.isEmpty();
    if (active == m_active)
        return;
    m_active = active;
    if (active) {
        show();
    } else {
        // Input routing restarts from a clean state the next time an element appears.
        m_forwarding = false;
        m_interactiveRects.clear();
        setPassThrough(true);
        hide();
    }
    emit activeChanged(active);
}

void QmlTableLayer::addElement(const QString &key, Element element)
{
    removeElement(key);
    if (m_content) {
        if (QQuickItem *item = createItem(element)) {
            auto *frame = new QQuickItem(m_content);
            frame->setParent(m_content);
            frame->setVisible(false);
            item->setParent(frame);
            item->setParentItem(frame);
            element.frame = frame;
            element.item = item;
            connect(item, &QQuickItem::implicitWidthChanged, this, &QmlTableLayer::scheduleRelayout);
            connect(item, &QQuickItem::implicitHeightChanged, this, &QmlTableLayer::scheduleRelayout);
        }
    } else {
        element.error = QStringLiteral("table layer failed to load");
    }
    if (!element.error.isEmpty() && !m_reportedErrors.contains(element.source)) {
        m_reportedErrors.insert(element.source);
        qWarning().noquote() << "QmlTableLayer:" << key << element.error;
    }
    m_elements.insert(key, element);
}

void QmlTableLayer::removeElement(const QString &key)
{
    auto it = m_elements.find(key);
    if (it == m_elements.end())
        return;
    if (it->frame) {
        it->frame->setVisible(false);
        it->frame->deleteLater();
    }
    m_elements.erase(it);
}

QQuickItem *QmlTableLayer::createItem(Element &element)
{
    QString error;
    if (!QmlElementPath::isAllowed(element.source, &error)) {
        element.error = error;
        return nullptr;
    }
    const QString file = QSanRuntimePaths::assetPath(QDir::cleanPath(QDir::fromNativeSeparators(element.source)));
    if (!QFileInfo(file).isFile()) {
        element.error = QStringLiteral("\"%1\" not found").arg(element.source);
        return nullptr;
    }
    QQmlComponent *&component = m_components[file];
    if (!component)
        component = new QQmlComponent(engine(), QUrl::fromLocalFile(file), QQmlComponent::PreferSynchronous);
    if (!component->isReady()) {
        element.error = component->isError() ? component->errorString() : QStringLiteral("component is not ready");
        return nullptr;
    }
    QObject *object = component->beginCreate(engine()->rootContext());
    if (!object) {
        element.error = component->errorString().isEmpty()
            ? QStringLiteral("\"%1\" could not be created").arg(element.source)
            : component->errorString();
        return nullptr;
    }
    // Always complete, so the cached component never stays "completion pending".
    component->completeCreate();
    auto *item = qobject_cast<QQuickItem *>(object);
    if (!item) {
        delete object;
        element.error = QStringLiteral("\"%1\" root is not an Item").arg(element.source);
        return nullptr;
    }
    return item;
}

QJsonObject QmlTableLayer::snapshot() const
{
    QJsonArray elements;
    for (auto it = m_elements.cbegin(); it != m_elements.cend(); ++it) {
        QJsonObject entry{
            {QStringLiteral("key"), it.key()},
            {QStringLiteral("source"), it->source},
            {QStringLiteral("anchor"), it->anchor},
            {QStringLiteral("player"), it->player},
            {QStringLiteral("visible"), it->visible},
            {QStringLiteral("error"), it->error},
            {QStringLiteral("rect"), rectJson(it->viewRect)},
            {QStringLiteral("data"), QJsonObject::fromVariantMap(it->data)},
            {QStringLiteral("interactive"), it->item && it->item->property("qsInteractive").toBool()},
            {QStringLiteral("instance"), it->item
                ? QString::number(reinterpret_cast<quintptr>(it->item.data()), 16) : QString()},
        };
        if (!it->player.isEmpty())
            entry.insert(QStringLiteral("seat_rect"), rectJson(it->seatRect));
        elements.append(entry);
    }
    const auto map = [this](const QRectF &rect) {
        return m_view && rect.isValid() ? QRectF(m_view->mapFromScene(rect).boundingRect()) : QRectF();
    };
    QJsonObject result{{QStringLiteral("elements"), elements},
                       {QStringLiteral("profile"), profileName(m_table.profile)},
                       {QStringLiteral("compact"), m_table.compactSeats}};
    if (m_view) {
        const QPointF center = m_view->mapFromScene(m_table.tableCenter);
        result.insert(QStringLiteral("table_center"),
                      QJsonObject{{QStringLiteral("x"), center.x()}, {QStringLiteral("y"), center.y()}});
        if (m_table.headerRect.isValid())
            result.insert(QStringLiteral("header_rect"), rectJson(map(m_table.headerRect)));
        if (m_table.interactionRect.isValid())
            result.insert(QStringLiteral("interaction_rect"), rectJson(map(m_table.interactionRect)));
    }
    return result;
}

void QmlTableLayer::setOccludedRegion(const QRegion &region)
{
    if (region == m_occluded)
        return;
    m_occluded = region;
    scheduleRelayout();
}

void QmlTableLayer::relayout()
{
    if (!m_view || !m_content)
        return;
    QList<QmlSeatGeometry> seats;
    QmlTableGeometry table;
    const bool mounted = !m_elements.isEmpty();
    if (m_provider)
        m_provider(mounted ? &seats : nullptr, &table);
    m_table = table;
    if (!mounted) {
        m_interactiveRects.clear();
        if (m_reportPending) {
            m_reportPending = false;
            emit elementsChanged();
        }
        return;
    }
    const auto map = [this](const QRectF &rect) {
        return rect.isValid() ? QRectF(m_view->mapFromScene(rect).boundingRect()) : QRectF();
    };
    const qreal viewScale = m_view->transform().m11();
    const QRectF main = table.mainRect.isValid() ? map(table.mainRect) : QRectF(rect());
    const QRectF header = map(table.headerRect);
    const QRectF interaction = map(table.interactionRect);
    QHash<QString, const QmlSeatGeometry *> seatByPlayer;
    for (const QmlSeatGeometry &seat : seats)
        seatByPlayer.insert(seat.player, &seat);

    // Group elements that share an anchor; each group is laid out as one row.
    struct Placement { Element *element; QSizeF size; qreal scale; };
    QMap<QString, QList<Placement>> groups;
    for (auto it = m_elements.begin(); it != m_elements.end(); ++it) {
        Element &element = *it;
        element.visible = false;
        if (!element.frame || !element.item) {
            element.viewRect = QRectF();
            continue;
        }
        const QmlSeatGeometry *seat = element.player.isEmpty() ? nullptr : seatByPlayer.value(element.player);
        if (!element.player.isEmpty() && (!seat || !seat->visible)) {
            element.frame->setVisible(false);
            element.viewRect = QRectF();
            continue;
        }
        const qreal scale = seat ? viewScale * seat->itemScale : viewScale;
        QVariantMap qs{{QStringLiteral("data"), element.data},
                       {QStringLiteral("player"), seat ? QVariant(seat->snapshot) : QVariant()},
                       {QStringLiteral("scale"), scale},
                       {QStringLiteral("profile"), profileName(table.profile)},
                       {QStringLiteral("compact"), table.compactSeats}};
        if (qs != element.lastQs) {
            element.item->setProperty("qs", qs);
            element.lastQs = qs;
        }
        QSizeF size(element.item->implicitWidth(), element.item->implicitHeight());
        if (size.isEmpty())
            size = element.item->size();
        element.seatRect = seat ? map(seat->sceneRect) : QRectF();
        QString group = element.anchor;
        if (seat) {
            const bool centered = element.anchor == QLatin1String("avatar") || element.anchor.startsWith(QLatin1String("seat:"));
            // Ribbon seats are too small for rows outside them: every non-centred seat anchor
            // becomes one row along the inside bottom of the seat.
            group = element.player + QLatin1Char('|')
                + (table.compactSeats && !seat->self && !centered ? QStringLiteral("compact") : (centered ? QStringLiteral("center") : element.anchor));
        }
        groups[group].append({&element, size * scale, scale});
    }

    m_interactiveRects.clear();
    for (auto group = groups.begin(); group != groups.end(); ++group) {
        const QList<Placement> &row = group.value();
        qreal width = -kGap, height = 0;
        for (const Placement &p : row) {
            width += p.size.width() + kGap;
            height = qMax(height, p.size.height());
        }
        const QString kind = group.key().section(QLatin1Char('|'), -1);
        const Element *first = row.first().element;
        const QRectF seat = first->seatRect;
        QPointF origin;
        bool clipToSeat = false;
        if (!first->player.isEmpty()) {
            if (kind == QLatin1String("center"))
                origin = QPointF(seat.center().x() - width / 2, seat.center().y() - height / 2);
            else if (kind == QLatin1String("compact")) {
                origin = QPointF(seat.left() + 2, seat.bottom() - height - 2);
                clipToSeat = true;
            } else if (kind == QLatin1String("top"))
                origin = QPointF(seat.left(), seat.top() - height - 2);
            else if (kind == QLatin1String("bottom"))
                origin = QPointF(seat.left(), seat.bottom() + 2);
            else // mark-area
                origin = QPointF(seat.left() + 2, seat.top() + 2);
        } else if (kind == QLatin1String("table-center")) {
            const QPointF center = m_view->mapFromScene(table.tableCenter);
            origin = QPointF(center.x() - width / 2, center.y() - height / 2);
        } else if (kind == QLatin1String("screen-top")) {
            const qreal top = header.isValid() ? header.bottom() + kEdge : main.top() + kEdge;
            origin = QPointF(main.center().x() - width / 2, top);
        } else if (kind == QLatin1String("screen-bottom")) {
            const qreal bottom = interaction.isValid() ? interaction.top() - kEdge : main.bottom() - kEdge;
            origin = QPointF(main.center().x() - width / 2, bottom - height);
        } else if (kind == QLatin1String("screen-top-left")) {
            origin = QPointF(main.left() + kEdge, (header.isValid() ? header.bottom() : main.top()) + kEdge);
        } else if (kind == QLatin1String("screen-top-right")) {
            origin = QPointF(main.right() - kEdge - width, (header.isValid() ? header.bottom() : main.top()) + kEdge);
        } else if (kind == QLatin1String("screen-bottom-left")) {
            origin = QPointF(main.left() + kEdge, (interaction.isValid() ? interaction.top() : main.bottom()) - kEdge - height);
        } else { // screen-bottom-right
            origin = QPointF(main.right() - kEdge - width, (interaction.isValid() ? interaction.top() : main.bottom()) - kEdge - height);
        }

        qreal x = origin.x();
        for (const Placement &p : row) {
            Element &element = *p.element;
            const QRectF fullBox(QPointF(x, origin.y()), p.size);
            QRectF box = fullBox;
            x += p.size.width() + kGap;
            if (clipToSeat)
                box = box.intersected(seat);
            if (!box.isEmpty() && !m_occluded.isEmpty() && m_occluded.intersects(box.toAlignedRect()))
                box = QRectF();
            element.frame->setPosition(box.topLeft());
            element.frame->setSize(box.size());
            element.frame->setClip(clipToSeat);
            element.item->setTransformOrigin(QQuickItem::TopLeft);
            element.item->setScale(p.scale);
            element.item->setPosition(QPointF(fullBox.left() - box.left(), fullBox.top() - box.top()));
            element.frame->setVisible(!box.isEmpty());
            element.visible = !box.isEmpty();
            element.viewRect = box;
            if (element.visible && element.item->property("qsInteractive").toBool())
                m_interactiveRects.append(box);
        }
    }

    if (m_reportPending) {
        m_reportPending = false;
        emit elementsChanged();
    }
}

bool QmlTableLayer::interactiveAt(const QPointF &pos) const
{
    for (const QRectF &rect : m_interactiveRects)
        if (rect.contains(pos))
            return true;
    return false;
}

void QmlTableLayer::setPassThrough(bool passThrough)
{
    if (testAttribute(Qt::WA_TransparentForMouseEvents) != passThrough)
        setAttribute(Qt::WA_TransparentForMouseEvents, passThrough);
}

namespace {

bool isPointerEventType(QEvent::Type type)
{
    switch (type) {
    case QEvent::MouseMove:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TouchCancel:
    case QEvent::Wheel: // WA_NoMousePropagation also stops wheel events.
        return true;
    default:
        return false;
    }
}

} // namespace

// While the pointer is over an interactive element the layer takes input itself;
// leaving it hands the next events back to the table.
bool QmlTableLayer::event(QEvent *event)
{
    switch (event->type()) {
    case QEvent::MouseMove:
        if (!m_forwarding && !interactiveAt(static_cast<QMouseEvent *>(event)->position()))
            setPassThrough(true);
        break;
    case QEvent::HoverMove:
        if (!m_forwarding && !interactiveAt(static_cast<QHoverEvent *>(event)->position()))
            setPassThrough(true);
        break;
    case QEvent::Leave:
        if (!m_forwarding)
            setPassThrough(true);
        break;
    default:
        break;
    }
    if (!isPointerEventType(event->type()))
        return QQuickWidget::event(event);

    // Reached the layer directly (hover-first path): if QML ignores it, hand it to the table via the
    // viewport. The layer has WA_NoMousePropagation, so Qt does not do that itself; the guard makes
    // the viewport filter let the re-sent event through instead of forwarding it back.
    // Either delivery may tear the room down and delete the layer, so no member is touched
    // once `self` is null (m_dispatching is reset by hand for the same reason).
    if (m_dispatching)
        return QQuickWidget::event(event);
    QPointer<QmlTableLayer> self(this);
    m_dispatching = true;
    const bool handled = QQuickWidget::event(event);
    if (!self)
        return handled;
    if (!event->isAccepted() && m_view) {
        QCoreApplication::sendEvent(m_view->viewport(), event);
        if (!self)
            return handled;
        if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick
            || event->type() == QEvent::TouchBegin)
            setPassThrough(true);
    }
    m_dispatching = false;
    return handled;
}

bool QmlTableLayer::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_view || watched != m_view->viewport())
        return QQuickWidget::eventFilter(watched, event);

    // An empty layer is hidden and takes no part in input; only keep its geometry in sync.
    if (isHidden() && event->type() != QEvent::Resize)
        return false;

    // A delivery to the layer may tear the room down and delete it: after each one, no member is
    // touched once `self` is null.
    QPointer<QmlTableLayer> self(this);
    // Delivers `event` to the layer; m_dispatching keeps an ignored event, which Qt propagates to
    // the viewport, from being forwarded again. Returns whether QML took it.
    const auto deliver = [this, &self](QEvent *e) {
        m_dispatching = true;
        e->accept();
        QCoreApplication::sendEvent(this, e);
        if (self)
            m_dispatching = false;
        return e->isAccepted();
    };
    const auto releaseForwarding = [this] {
        m_forwarding = false;
        setPassThrough(true);
    };

    if (isPointerEventType(event->type()) || qsanIsPointerHoverEvent(event->type())) {
        if (m_dispatching)
            return false; // Event the layer already saw (or is re-sending): the table gets it.
    }

    switch (event->type()) {
    case QEvent::Resize:
        setGeometry(m_view->viewport()->rect());
        scheduleRelayout();
        break;
    case QEvent::WindowDeactivate:
        if (m_forwarding)
            releaseForwarding();
        break;
    case QEvent::MouseMove:
    case QEvent::HoverEnter:
    case QEvent::HoverMove: {
        const QPointF pos = event->type() == QEvent::MouseMove
            ? static_cast<QMouseEvent *>(event)->position() : static_cast<QHoverEvent *>(event)->position();
        if (m_forwarding) {
            deliver(event);
            return true;
        }
        if (interactiveAt(pos)) {
            setPassThrough(false);
            // Wayland may send hover without a mouse move; QQuickWidget only maps mouse moves.
            m_dispatching = true;
            qsanForwardPointerHoverAsMouseMove(this, event);
            if (!self)
                return false;
            m_dispatching = false;
        }
        break;
    }
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
        // A click that reached the viewport first (no hover before it) is handed over whole.
        if (interactiveAt(static_cast<QMouseEvent *>(event)->position())) {
            m_forwarding = true;
            setPassThrough(false);
            if (deliver(event))
                return true;
            if (self)
                releaseForwarding(); // QML has no handler there: the table takes the click.
            return false;
        }
        if (m_forwarding)
            releaseForwarding();
        break;
    case QEvent::MouseButtonRelease:
        if (m_forwarding) {
            m_forwarding = false;
            const bool accepted = deliver(event);
            return accepted;
        }
        break;
    case QEvent::TouchBegin: {
        const auto *touch = static_cast<QTouchEvent *>(event);
        if (!touch->points().isEmpty() && interactiveAt(touch->points().first().position())) {
            m_forwarding = true;
            setPassThrough(false);
            if (deliver(event))
                return true;
            if (self)
                releaseForwarding();
            return false;
        }
        if (m_forwarding)
            releaseForwarding();
        break;
    }
    case QEvent::TouchUpdate:
        if (m_forwarding) {
            deliver(event);
            return true;
        }
        break;
    case QEvent::TouchEnd:
    case QEvent::TouchCancel:
        if (m_forwarding) {
            m_forwarding = false;
            return deliver(event);
        }
        break;
    default:
        break;
    }
    return QQuickWidget::eventFilter(watched, event);
}
