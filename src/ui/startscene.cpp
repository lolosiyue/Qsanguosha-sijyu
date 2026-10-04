#include "startscene.h"
#include "engine.h"
#include "audio.h"
#include "settings.h"
#include "server.h"
#ifdef QSAN_XP_LEGACY
#include "local-server-controller.h"
#include <QJsonArray>
#endif

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QGraphicsPixmapItem>
#include <QGraphicsSceneMouseEvent>
#include <QImageReader>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QScopedValueRollback>
#include <QScreen>
#include <QScrollBar>
#include <QTextEdit>
#include <QTextStream>

namespace {

// Tokens from qml/home/HomeTheme.qml, painted without QML, OpenGL or blur.
struct HomePalette
{
    QColor windowBg;
    qreal backdropOpacity;
    QColor plateTop, plateBottom, plateBorder, shadow, accent;
    QColor primaryTop, primaryBottom, primaryDown, primaryBorder, primaryText, primaryIconBg;
    QColor secondaryTop, secondaryBottom, secondaryDown, secondaryBorder, secondaryText, secondaryIconBg;
    QColor navText, navHoverBg, focus, logBase;
};

const HomePalette &homePalette()
{
    static const HomePalette light = {
        QColor(0xD8E2F0), 0.32,
        QColor::fromRgba(0xF0F3F8FD), QColor::fromRgba(0xE6DCEAF6), QColor(0x8AB3CC), QColor::fromRgba(0x260A2A50), QColor(0xFFD84D),
        QColor(0x5EC4EE), QColor(0x2F8EC4), QColor(0x2478A8), QColor(0xFFFFFF), QColor(0xFFFFFF), QColor::fromRgba(0x55FFFFFF),
        QColor::fromRgba(0xF2FFFFFF), QColor::fromRgba(0xE6DCEAF6), QColor(0xD4E4F2), QColor(0x8AB3CC), QColor(0x073B5B), QColor(0xE8F0FB),
        QColor(0x185879), QColor::fromRgba(0x334EB8EA), QColor(0x4EB8EA), QColor(0x142844)
    };
    static const HomePalette dark = {
        QColor(0x0B1A2E), 0.45,
        QColor::fromRgba(0xCC243A58), QColor::fromRgba(0xE0142844), QColor(0x5A8AAB), QColor::fromRgba(0x40061428), QColor(0xFFD84D),
        QColor(0x4AA8D0), QColor(0x1E6A94), QColor(0x185878), QColor(0x8FD4EE), QColor(0xFFFFFF), QColor::fromRgba(0x55FFFFFF),
        QColor::fromRgba(0xF01E334C), QColor::fromRgba(0xE6142844), QColor(0x1A2E46), QColor(0x5A8AAB), QColor(0xD6E8F4), QColor::fromRgba(0x332A4A66),
        QColor(0xD2E7F4), QColor::fromRgba(0x333AA8D4), QColor(0xFFFFFF), QColor(0x0B1A2E)
    };
    return Config.ColorScheme == 2 ? dark : light;
}

QFont homeFont(int pixelSize, bool bold)
{
    QFont font(UiConfig.AppFont);
    font.setPixelSize(pixelSize);
    font.setBold(bold);
    font.setUnderline(false);
    return font;
}

// BASlantedPanel's Shear transform, about the plate centre.
QTransform shearAround(const QRectF &rect, qreal slant)
{
    QTransform transform;
    transform.translate(rect.center().x(), rect.center().y());
    transform.shear(slant, 0);
    transform.translate(-rect.center().x(), -rect.center().y());
    return transform;
}

void paintPlate(QPainter *painter, const QRectF &rect, qreal slant, qreal radius,
    const QColor &top, const QColor &bottom, const QPen &border, const QColor &shadow)
{
    painter->save();
    painter->setTransform(shearAround(rect, slant), true);
    painter->setPen(Qt::NoPen);
    // Two offset layers stand in for the QML blur at a fraction of its cost.
    QColor soft = shadow;
    soft.setAlphaF(shadow.alphaF() * 0.6);
    painter->setBrush(soft);
    painter->drawRoundedRect(rect.translated(0, 6), radius, radius);
    painter->setBrush(shadow);
    painter->drawRoundedRect(rect.translated(0, 3), radius, radius);
    QLinearGradient fill(rect.topLeft(), rect.bottomLeft());
    fill.setColorAt(0, top);
    fill.setColorAt(1, bottom);
    painter->setBrush(fill);
    painter->setPen(border);
    painter->drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    painter->restore();
}

// Monogram badge in place of the SVG icons, which XP cannot load.
void paintBadge(QPainter *painter, const QPointF &center, qreal diameter, const QColor &fill,
    const QColor &border, const QColor &textColor, const QString &text)
{
    const QRectF circle(center.x() - diameter / 2, center.y() - diameter / 2, diameter, diameter);
    painter->setBrush(fill);
    painter->setPen(border.isValid() ? QPen(border, 2) : QPen(Qt::NoPen));
    painter->drawEllipse(circle.adjusted(1, 1, -1, -1));
    painter->setFont(homeFont(qMax(10, int(diameter * 0.46)), true));
    painter->setPen(textColor);
    painter->drawText(circle, Qt::AlignCenter, text.left(1));
}

} // namespace

class HomePlate final : public QGraphicsItem
{
public:
    HomePlate(qreal slant, bool accent)
        : m_slant(slant), m_accent(accent)
    {
        // Large gradient: blit it from cache when a button above it repaints.
        setCacheMode(QGraphicsItem::DeviceCoordinateCache);
    }

    void setSize(const QSizeF &size)
    {
        if (m_size == size)
            return;
        prepareGeometryChange();
        m_size = size;
    }

    QRectF boundingRect() const override
    {
        const qreal overhang = qAbs(m_slant) * m_size.height() / 2 + 2;
        return QRectF(QPointF(), m_size).adjusted(-overhang, -2, overhang, 8);
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        const HomePalette &p = homePalette();
        const QRectF r(QPointF(), m_size);
        paintPlate(painter, r, m_slant, 12, p.plateTop, p.plateBottom, QPen(p.plateBorder, 1), p.shadow);
        if (m_accent) {
            painter->save();
            painter->setTransform(shearAround(r, m_slant), true);
            painter->setPen(QPen(p.accent, 2));
            painter->drawLine(QPointF(r.left() + 14, r.top() + 1.5), QPointF(r.right() - 14, r.top() + 1.5));
            painter->restore();
        }
    }

private:
    qreal m_slant;
    bool m_accent;
    QSizeF m_size;
};

class HomeButton final : public QGraphicsObject
{
public:
    enum Style { Primary, Secondary, Nav };

    HomeButton(QAction *action, Style style)
        : m_action(action), m_style(style)
    {
        setAcceptHoverEvents(true);
        setAcceptedMouseButtons(Qt::LeftButton);
        setFlag(QGraphicsItem::ItemIsFocusable);
        connect(action, &QAction::changed, this, [this]() { update(); });
    }

    Style style() const { return m_style; }

    void setSize(const QSizeF &size, int pixelSize)
    {
        prepareGeometryChange();
        m_size = size;
        m_pixelSize = pixelSize;
    }

    void setKeyboardSelected(bool selected)
    {
        if (m_selected == selected)
            return;
        m_selected = selected;
        update();
    }

    void activate()
    {
        if (!m_action || !m_action->isEnabled())
            return;
        Sanguosha->playSystemAudioEffect("button-down", false);
        m_action->trigger();
    }

    QRectF boundingRect() const override
    {
        const qreal overhang = qAbs(slant()) * m_size.height() / 2 + 4;
        return QRectF(QPointF(), m_size).adjusted(-overhang, -4, overhang, 9);
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        if (!m_action)
            return;
        const HomePalette &p = homePalette();
        const bool enabled = m_action->isEnabled();
        const QRectF r(QPointF(), m_size);
        const QString label = m_action->iconText();
        painter->setOpacity(enabled ? 1.0 : 0.45);

        if (m_style == Nav) {
            if (enabled && (m_hovered || m_pressed || m_selected)) {
                painter->save();
                painter->setTransform(shearAround(r, slant()), true);
                painter->setPen(m_selected ? QPen(p.focus, 2) : QPen(Qt::NoPen));
                QColor fill = p.navHoverBg;
                if (m_pressed)
                    fill.setAlphaF(qMin(1.0, fill.alphaF() * 2));
                painter->setBrush(fill);
                painter->drawRoundedRect(r.adjusted(2, 2, -2, -2), 8, 8);
                painter->restore();
            }
            const QFont font = homeFont(m_pixelSize, true);
            const QFontMetrics metrics(font);
            // Narrow docks keep the whole label and drop the badge first.
            qreal badge = qMin<qreal>(26, r.height() - 16);
            if (badge + 6 + metrics.horizontalAdvance(label) > r.width() - 8)
                badge = 0;
            const qreal spacing = badge > 0 ? 6 : 0;
            const QString text = metrics.elidedText(label, Qt::ElideRight, int(r.width() - 8 - badge - spacing));
            const qreal textWidth = metrics.horizontalAdvance(text);
            const qreal left = r.center().x() - (badge + spacing + textWidth) / 2;
            if (badge > 0)
                paintBadge(painter, QPointF(left + badge / 2, r.center().y()), badge,
                    p.secondaryIconBg, p.secondaryBorder, p.navText, label);
            painter->setFont(font);
            painter->setPen(p.navText);
            painter->drawText(QRectF(left + badge + spacing, r.top(), textWidth + 2, r.height()),
                Qt::AlignLeft | Qt::AlignVCenter, text);
            return;
        }

        const bool primary = m_style == Primary;
        QColor top = primary ? p.primaryTop : p.secondaryTop;
        QColor bottom = primary ? p.primaryBottom : p.secondaryBottom;
        if (enabled && m_pressed) {
            top = bottom = primary ? p.primaryDown : p.secondaryDown;
        } else if (enabled && m_hovered) {
            top = top.lighter(108);
            bottom = bottom.lighter(108);
        }
        const QColor border = primary ? p.primaryBorder : p.secondaryBorder;
        paintPlate(painter, r, slant(), 12, top, bottom, QPen(border, primary ? 2 : 1), p.shadow);
        if (m_selected) {
            painter->save();
            painter->setTransform(shearAround(r, slant()), true);
            painter->setBrush(Qt::NoBrush);
            painter->setPen(QPen(p.focus, 2));
            painter->drawRoundedRect(r.adjusted(-3, -3, 3, 3), 14, 14);
            painter->restore();
        }

        const qreal badge = qMax<qreal>(24, r.height() - 28);
        const qreal badgeLeft = r.left() + 16;
        const QColor textColor = primary ? p.primaryText : p.secondaryText;
        paintBadge(painter, QPointF(badgeLeft + badge / 2, r.center().y()), badge,
            primary ? p.primaryIconBg : p.secondaryIconBg, border, textColor, label);
        const QRectF textRect(badgeLeft + badge + 10, r.top(), r.right() - badgeLeft - badge - 26, r.height());
        const QFont font = homeFont(m_pixelSize, true);
        painter->setFont(font);
        painter->setPen(textColor);
        painter->drawText(textRect, Qt::AlignCenter,
            QFontMetrics(font).elidedText(label, Qt::ElideRight, int(textRect.width())));
    }

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent *) override
    {
        m_hovered = true;
        update();
        if (m_action && m_action->isEnabled())
            Sanguosha->playSystemAudioEffect("button-hover", false);
    }

    void hoverLeaveEvent(QGraphicsSceneHoverEvent *) override
    {
        m_hovered = false;
        update();
    }

    void mousePressEvent(QGraphicsSceneMouseEvent *event) override
    {
        m_pressed = true;
        update();
        event->accept();
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override
    {
        const bool inside = QRectF(QPointF(), m_size).contains(event->pos());
        m_pressed = false;
        update();
        if (inside)
            activate();
    }

private:
    qreal slant() const { return m_style == Nav ? -0.17 : -0.12; }

    QPointer<QAction> m_action;
    Style m_style;
    QSizeF m_size;
    int m_pixelSize = 16;
    bool m_hovered = false;
    bool m_pressed = false;
    bool m_selected = false;
};

namespace {

// Decode once at no more than the screen size: low-memory machines must not
// hold full-resolution art they will only ever show downscaled.
QPixmap loadScreenSized(const QString &path, bool cover)
{
    QImageReader reader(path);
    const QSize source = reader.size();
    QScreen *screen = QGuiApplication::primaryScreen();
    const QSize bound = screen ? (QSizeF(screen->size()) * screen->devicePixelRatio()).toSize() : QSize(1920, 1080);
    if (source.isValid()) {
        const QSize fitted = source.scaled(bound, cover ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio);
        if (fitted.width() < source.width())
            reader.setScaledSize(fitted);
    }
    return QPixmap::fromImage(reader.read());
}

// Builds without the logo artwork show the game title, drawn large so downscaling stays sharp.
QPixmap titleLogo()
{
    QPainterPath path;
    path.addText(0, 0, homeFont(96, true), QCoreApplication::translate("MainWindow", "Sanguosha"));
    const QRectF bounds = path.boundingRect().adjusted(-6, -6, 6, 6);
    QPixmap pixmap(bounds.size().toSize());
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(-bounds.topLeft());
    painter.strokePath(path, QPen(homePalette().primaryBottom, 8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.fillPath(path, Qt::white);
    return pixmap;
}

} // namespace

StartScene::StartScene()
    : server_log(nullptr), m_server(nullptr), m_currentIndex(-1), m_relayouting(false)
{
    setSceneRect(-UiConfig.Rect.width() / 2.0, -UiConfig.Rect.height() / 2.0,
        UiConfig.Rect.width(), UiConfig.Rect.height());

    portrait_source = loadScreenSized(QStringLiteral("image/home/character.png"), false);
    backdrop_source = loadScreenSized(Config.BackgroundImage, true);
    if (!logo_source.load(QStringLiteral("image/logo/logo.png")))
        logo_source = titleLogo();

    portrait = addPixmap(QPixmap());
    portrait->setTransformationMode(Qt::SmoothTransformation);
    portrait->setZValue(0);
    dock = new HomePlate(-0.17, true);
    dock->setZValue(1);
    addItem(dock);
    log_plate = new HomePlate(0, false);
    log_plate->setZValue(1);
    log_plate->hide();
    addItem(log_plate);
    logo = addPixmap(QPixmap());
    logo->setTransformationMode(Qt::SmoothTransformation);
    logo->setZValue(2);

    //the website URL, as a quiet credit line
    website_text = addSimpleText("http://mogara.org", homeFont(12, false));
    website_text->setZValue(3);

    connect(this, &QGraphicsScene::sceneRectChanged, this, &StartScene::relayout);
    relayout();
}

StartScene::~StartScene()
{
}

void StartScene::addButton(QAction *action)
{
    // As on the new home page: one primary action, one secondary, then tools in the dock.
    const int index = buttons.length();
    HomeButton *button = new HomeButton(action, index == 0 ? HomeButton::Primary
        : index == 1 ? HomeButton::Secondary : HomeButton::Nav);
    button->setZValue(2);
    addItem(button);
    buttons << button;
    relayout();
}

void StartScene::rebuildBackdrop()
{
    const qreal dpr = qApp->devicePixelRatio();
    const QSize device = (sceneRect().size() * dpr).toSize();
    if (device.isEmpty() || (backdrop.size() == device && !backdrop.isNull()))
        return;
    // HomeBackground.qml: window colour under a translucent backdrop. Composed
    // once per size so every partial repaint is a plain blit.
    const HomePalette &p = homePalette();
    QPixmap canvas(device);
    canvas.fill(p.windowBg);
    if (!backdrop_source.isNull()) {
        QPainter painter(&canvas);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.setOpacity(p.backdropOpacity);
        const QSize cover = backdrop_source.size().scaled(device, Qt::KeepAspectRatioByExpanding);
        painter.drawPixmap(QRect(QPoint((device.width() - cover.width()) / 2,
            (device.height() - cover.height()) / 2), cover), backdrop_source);
    }
    canvas.setDevicePixelRatio(dpr);
    backdrop = canvas;
    update();
}

void StartScene::drawBackground(QPainter *painter, const QRectF &rect)
{
    if (backdrop.isNull()) {
        QGraphicsScene::drawBackground(painter, rect);
        return;
    }
    const qreal dpr = backdrop.devicePixelRatio();
    const QRectF source((rect.topLeft() - sceneRect().topLeft()) * dpr, rect.size() * dpr);
    painter->drawPixmap(rect, backdrop, source);
}

void StartScene::relayout()
{
    if (m_relayouting)
        return;
    QScopedValueRollback<bool> guard(m_relayouting, true);

    const QRectF r = sceneRect();
    if (r.isEmpty())
        return;
    rebuildBackdrop();
    const HomePalette &p = homePalette();
    const qreal dpr = qApp->devicePixelRatio();
    const bool serverMode = server_log != nullptr;
    const bool compact = r.width() < 900 || r.height() < 600;
    const qreal margin = compact ? 12 : qBound<qreal>(16, r.width() * 0.03, 40);
    const qreal gap = compact ? 10 : 14;

    QList<HomeButton *> mains, navs;
    foreach (HomeButton *button, buttons)
        (button->style() == HomeButton::Nav ? navs : mains) << button;

    // Bottom dock (HomeBottomBar): slanted plate, yellow accent, tool entries.
    qreal bottomLimit = r.bottom() - margin;
    const bool dockVisible = !serverMode && !navs.isEmpty();
    dock->setVisible(dockVisible);
    if (dockVisible) {
        const int n = navs.length();
        const qreal dockHeight = compact ? 56 : 72;
        const qreal dockWidth = qMin(r.width() - 2 * margin, n * (compact ? 116.0 : 150.0) + 40);
        const qreal itemWidth = (dockWidth - 40) / n;
        dock->setSize(QSizeF(dockWidth, dockHeight));
        dock->setPos(r.center().x() - dockWidth / 2, r.bottom() - margin - dockHeight);
        for (int i = 0; i < n; ++i) {
            navs.at(i)->setSize(QSizeF(itemWidth, dockHeight - 16), compact ? 13 : 15);
            navs.at(i)->setPos(dock->x() + 20 + i * itemWidth, dock->y() + 8);
        }
        bottomLimit = dock->y() - gap * 1.5;
    }

    // Main actions (MainActionPanel): right-aligned, left edges cascading.
    const qreal mainHeight = compact ? 58 : 76;
    const qreal mainWidth = compact ? qMin<qreal>(330, r.width() * 0.46) : qMin<qreal>(440, r.width() * 0.34);
    const qreal stagger = compact ? 12 : 18;
    const qreal columnRight = serverMode ? r.center().x() : r.right() - margin - (compact ? 0 : r.width() * 0.03);
    const qreal columnLeft = columnRight - mainWidth;

    QSizeF logoSize;
    if (!logo_source.isNull()) {
        logoSize = QSizeF(logo_source.size()).scaled(QSizeF(serverMode ? 260 : mainWidth * 0.72,
            r.height() * (compact ? 0.14 : 0.18)), Qt::KeepAspectRatio);
        const QSize device = (logoSize * dpr).toSize();
        // KeepAspectRatio may round one pixel short; compare the requested size.
        if (logo->data(0).toSize() != device) {
            logo->setData(0, device);
            QPixmap scaled = logo_source.scaled(device, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            scaled.setDevicePixelRatio(dpr);
            logo->setPixmap(scaled);
        }
    }

    const qreal top = r.top() + margin;
    if (serverMode) {
        logo->setPos(r.center().x() - logoSize.width() / 2, top);
        const qreal plateTop = top + logoSize.height() + gap;
        const qreal plateWidth = qMin<qreal>(r.width() - 2 * margin, 900);
        log_plate->setSize(QSizeF(plateWidth, qMax<qreal>(100, bottomLimit - plateTop)));
        log_plate->setPos(r.center().x() - plateWidth / 2, plateTop);
        log_plate->show();
        // The proxy follows the widget geometry, as the classic log did with move().
        server_log->setGeometry(QRectF(log_plate->x() + 14, plateTop + 14, plateWidth - 28,
            qMax<qreal>(72, bottomLimit - plateTop - 28)).toRect());
    } else {
        const qreal columnHeight = logoSize.height() + gap * 1.5
            + mains.length() * mainHeight + qMax(0, mains.length() - 1) * gap;
        qreal y = qMax(top, top + (bottomLimit - top - columnHeight) / 2);
        logo->setPos(columnRight - logoSize.width(), y);
        y += logoSize.height() + gap * 1.5;
        for (int i = 0; i < mains.length(); ++i) {
            mains.at(i)->setSize(QSizeF(mainWidth - i * stagger, i == 0 ? mainHeight : mainHeight - 4),
                compact ? 18 : 22);
            mains.at(i)->setPos(columnLeft + i * stagger, y);
            y += mainHeight + gap;
        }
    }

    // The art fills the free area left of the actions and steps aside when cramped.
    const qreal freeWidth = columnLeft - r.left() - margin;
    const bool portraitVisible = !serverMode && !portrait_source.isNull() && freeWidth >= 180;
    portrait->setVisible(portraitVisible);
    if (portraitVisible) {
        QSizeF size = QSizeF(portrait_source.size()).scaled(QSizeF(freeWidth, r.height() * 0.94), Qt::KeepAspectRatio);
        const QSize device = (size * dpr).toSize();
        if (device.width() > 0 && portrait->data(0).toSize() != device) {
            portrait->setData(0, device);
            QPixmap scaled = portrait_source.scaled(device, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            scaled.setDevicePixelRatio(dpr);
            portrait->setPixmap(scaled);
        }
        size = QSizeF(portrait->pixmap().size()) / dpr;
        portrait->setPos(r.left() + margin + (freeWidth - size.width()) / 2, r.bottom() - size.height());
    }

    QColor websiteColor = p.secondaryText;
    websiteColor.setAlphaF(0.55);
    website_text->setBrush(websiteColor);
    website_text->setPos(r.right() - website_text->boundingRect().width() - 10, r.top() + 6);
}

void StartScene::setServerLogBackground()
{
    if (server_log) {
        // Dark log plate in both themes keeps the configurable (white) log text
        // readable. A widget rule, because sanguosha.qss frames every QTextEdit.
        server_log->setStyleSheet(QString("QTextEdit { color: %1; background-color: %2; border: none; border-image: none; }")
            .arg(UiConfig.TextEditColor.name(), homePalette().logBase.name()));
    }
}

void StartScene::switchToServer(Server *server)
{
    m_server = server;
#ifdef AUDIO_SUPPORT
    Audio::quit();
#endif
    foreach (HomeButton *button, buttons)
        delete button;
    buttons.clear();
    m_currentIndex = -1;

    server_log = new QTextEdit();
    server_log->setReadOnly(true);
    server_log->setFrameShape(QFrame::NoFrame);
#ifdef Q_OS_LINUX
    QFont logFont("DroidSansFallback", 12);
#else
    QFont logFont("Verdana", 12);
#endif
    // Unset decorations would be inherited from the application font.
    logFont.setUnderline(false);
    logFont.setStrikeOut(false);
    server_log->setFont(logFont);
    setServerLogBackground();
    addWidget(server_log)->setZValue(2);

    QFile file("qss/scroll.qss");
    if (file.open(QIODevice::ReadOnly)) {
        QTextStream stream(&file);
        server_log->verticalScrollBar()->setStyleSheet(stream.readAll());
    }

    printServerInfo();
    if (server)
        connect(server, SIGNAL(logMessage(QString)), server_log, SLOT(append(QString)));
    relayout();
}

void StartScene::printServerInfo()
{
    if (!m_server)
        return;
    foreach (const QString &message, m_server->startupMessages())
        server_log->append(message);
}

#ifdef QSAN_XP_LEGACY
void StartScene::switchToServer(LocalServerController *controller)
{
    // Reuse classic presentation without an in-process Server pointer.
    switchToServer(static_cast<Server *>(nullptr));
    server_log->document()->setMaximumBlockCount(1000);
    for (const QString &message : controller->startupMessages()) server_log->append(message.toHtmlEscaped());
    connect(controller, &LocalServerController::logMessage, server_log,
        [this](const QString &message) { server_log->append(message.toHtmlEscaped()); });
    connect(controller, &LocalServerController::statusChanged, server_log,
        [this](const QJsonObject &status) {
            server_log->setToolTip(tr("Rooms: %1; players: %2")
                .arg(status.value("rooms").toArray().size()).arg(status.value("players").toArray().size()));
        });
}
#endif

void StartScene::keyPressEvent(QKeyEvent *event)
{
    if (buttons.isEmpty()) {
        QGraphicsScene::keyPressEvent(event);
        return;
    }

    const int count = buttons.length();
    switch (event->key()) {
    case Qt::Key_Up:
    case Qt::Key_Left:
        selectButton(m_currentIndex <= 0 ? count - 1 : m_currentIndex - 1);
        break;
    case Qt::Key_Down:
    case Qt::Key_Right:
        selectButton(m_currentIndex < 0 || m_currentIndex >= count - 1 ? 0 : m_currentIndex + 1);
        break;
    case Qt::Key_Enter:
    case Qt::Key_Return:
    case Qt::Key_Space:
        if (m_currentIndex >= 0 && m_currentIndex < count)
            buttons.at(m_currentIndex)->activate();
        break;
    default:
        QGraphicsScene::keyPressEvent(event);
        break;
    }
}

void StartScene::selectButton(int index)
{
    if (index < 0 || index >= buttons.length()) return;

    if (m_currentIndex >= 0 && m_currentIndex < buttons.length()) {
        HomeButton *oldBtn = buttons.at(m_currentIndex);
        oldBtn->setKeyboardSelected(false);
        oldBtn->clearFocus();
    }

    m_currentIndex = index;
    HomeButton *btn = buttons.at(index);
    btn->setKeyboardSelected(true);
    btn->setFocus();
}
