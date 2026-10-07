#include "qml-table-layer.h"

#include "engine.h"
#include "qml-element-path.h"
#include "runtime-paths.h"

#include <QDir>
#include <QFileInfo>
#include <QGraphicsView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlNetworkAccessManagerFactory>
#include <QQuickItem>
#include <QTimer>
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

}

QmlTableLayer::QmlTableLayer(QGraphicsView *view, QWidget *parent)
    : QQuickWidget(parent)
    , m_view(view)
{
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_AlwaysStackOnTop, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_AcceptTouchEvents, true);
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

    setPassThrough(true);
    if (m_view) {
        m_view->viewport()->installEventFilter(this);
        setGeometry(m_view->viewport()->rect());
    }
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
                       {QStringLiteral("profile"), QString()},
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

void QmlTableLayer::relayout()
{
    if (m_reportPending) {
        m_reportPending = false;
        emit elementsChanged();
    }
}

bool QmlTableLayer::interactiveAt(const QPointF &) const
{
    return false;
}

void QmlTableLayer::setPassThrough(bool passThrough)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, passThrough);
}

bool QmlTableLayer::event(QEvent *event)
{
    return QQuickWidget::event(event);
}

bool QmlTableLayer::eventFilter(QObject *watched, QEvent *event)
{
    return QQuickWidget::eventFilter(watched, event);
}
