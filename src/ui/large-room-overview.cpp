#include "large-room-overview.h"

#include "desktop-game-presentation.h"
#include "graphicsbox.h"
#include "photo.h"
#include <QScrollBar>
#include <QSignalBlocker>
#include <QFontMetricsF>
#include <QCoreApplication>
#include <QtMath>
#include "qsanbutton.h"
#include "room-layout-engine.h"
#include "skin-bank.h"
#include "package-catalog.h"
#include "runtime-paths.h"
#include "settings.h"
#include "engine.h"

#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneWheelEvent>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsProxyWidget>
#include <QGraphicsTextItem>
#include <QKeyEvent>
#include <QPainter>
#include <QTextDocument>
#include <QSet>
#include <algorithm>
#include <climits>
#include <functional>

namespace {
constexpr qreal MiniWidth = 110;
constexpr qreal MiniHeight = 126;
constexpr qreal MiniStep = 116;

QFont overviewFont() {
    QFont font = UiConfig.SmallFont;
    font.setPixelSize(14);
    return font;
}

// Reuse the handcard skin's border and texture, excluding its baked-in caption.
class OverviewButton final : public QSanButton
{
public:
    explicit OverviewButton(QGraphicsItem *parent) : QSanButton("handcard", "sort", parent) {}
    void setActionText(const QString &text) {
        caption = text;
        setSize(QSize(qCeil(QFontMetricsF(overviewFont()).horizontalAdvance(text)) + 24, 28));
        update();
    }
protected:
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override {
        const QPixmap &skin = _m_bgPixmap[int(_m_state)];
        const QRectF dst(0, 0, _m_size.width(), _m_size.height());
        const qreal edge = qMin(4, skin.width() / 3);
        p->drawPixmap(QRectF(0, 0, edge, dst.height()), skin, QRectF(0, 0, edge, skin.height()));
        p->drawPixmap(QRectF(edge, 0, dst.width() - 2 * edge, edge), skin,
            QRectF(skin.width() / 2, 0, 1, edge));
        p->drawPixmap(QRectF(edge, edge, dst.width() - 2 * edge, dst.height() - 2 * edge), skin,
            QRectF(skin.width() / 2, skin.height() - 5, 1, 1));
        p->drawPixmap(QRectF(edge, dst.height() - edge, dst.width() - 2 * edge, edge), skin,
            QRectF(skin.width() / 2, skin.height() - edge, 1, edge));
        p->drawPixmap(QRectF(dst.width() - edge, 0, edge, dst.height()), skin, QRectF(skin.width() - edge, 0, edge, skin.height()));
        p->setFont(overviewFont()); p->setPen(_m_state == S_STATE_DISABLED ? Qt::gray : QColor(255, 232, 186));
        p->drawText(dst.translated(0, _m_state == S_STATE_DOWN ? 1 : 0), Qt::AlignCenter, caption);
    }
private:
    QString caption;
};

// Selection adornments are separate children: candidate changes never repaint
// the portrait/HP/hand/death projection below them.
class TargetBadge final : public QGraphicsItem
{
public:
    explicit TargetBadge(QGraphicsItem *parent) : QGraphicsItem(parent) { setAcceptedMouseButtons(Qt::NoButton); }
    QRectF boundingRect() const override { return QRectF(0, 0, MiniWidth, MiniHeight); }
    void setState(bool active, bool legal, int votes) {
        if (m_active == active && m_legal == legal && m_votes == votes) return;
        m_active = active; m_legal = legal; m_votes = votes; update();
    }
    void setCursor(bool value) { if (m_cursor != value) { m_cursor = value; update(); } }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override {
        if (m_cursor) {
            p->setPen(QPen(Qt::white, 2, Qt::DashLine)); p->setBrush(Qt::NoBrush);
            p->drawRect(boundingRect().adjusted(4, 4, -4, -4));
            p->drawText(QRectF(3, 22, 20, 20), Qt::AlignCenter, QStringLiteral("▶"));
        }
        if (!m_active) return;
        if (!m_legal && !m_votes) p->fillRect(boundingRect(), QColor(0, 0, 0, 145));
        p->setPen(QPen(m_legal ? QColor(245, 211, 117) : QColor(155, 155, 155), m_legal ? 3 : 1));
        p->setBrush(Qt::NoBrush); p->drawRect(boundingRect().adjusted(2, 2, -2, -2));
        if (m_legal) p->drawText(QRectF(3, 2, 22, 20), Qt::AlignCenter, QStringLiteral("＋"));
        if (m_votes) {
            p->fillRect(QRectF(26, 1, 37, 20), QColor(30, 25, 12, 225));
            p->drawText(QRectF(26, 1, 37, 20), Qt::AlignCenter, QStringLiteral("%1 ×").arg(m_votes));
        }
    }
private:
    bool m_active = false, m_legal = false, m_cursor = false;
    int m_votes = 0;
};

class PlayerMiniItem final : public QGraphicsObject
{
public:
    explicit PlayerMiniItem(QGraphicsItem *parent) : QGraphicsObject(parent), badge(new TargetBadge(this)) {
        setAcceptHoverEvents(true); setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
        photo = new Photo; photo->setParentItem(this);
        // Scale the full native Photo layout, including its fullskin crop.
        photo->setPos(16, 0);
        const QRectF bounds = photo->boundingRect();
        photo->setTransform(QTransform::fromScale(94 / bounds.width(), 108 / bounds.height()));
        photo->setAcceptedMouseButtons(Qt::NoButton);
        badge->setZValue(200);
    }
    QRectF boundingRect() const override { return QRectF(0, 0, MiniWidth, MiniHeight); }
    void project(const GameViewPlayer &player, const QString &focus, bool responding) {
        // Photo owns the portrait cache; only this item's caption/death mark is cached here.
        const bool hidden = !player.self && player.skills.contains(Sanguosha->translate(QStringLiteral("inovation_fengbi")));
        name = player.name;
        const bool photoChanged = photo->projectOverview(player.general, player.kingdom, player.hp, player.maxHp,
            player.handCount, player.handMax, hidden, player.alive);
        // Reapply after the value projection, which hides non-mini native controls.
        if (photoChanged || responseFrame != responding)
            photo->setFrame(responding ? Photo::S_FRAME_RESPONDING : Photo::S_FRAME_NO_FRAME);
        responseFrame = responding;
        if (alive == player.alive && self == player.self && mark == focus) return;
        alive = player.alive; self = player.self; mark = focus;
        update();
    }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override {
        p->setFont(overviewFont()); p->setPen(Qt::white);
        const QString caption = (self ? QCoreApplication::translate("LargeRoomOverview", "Self") : QString())
            + (self && !mark.isEmpty() ? QCoreApplication::translate("LargeRoomOverview", " - ") : QString()) + mark;
        p->drawText(QRectF(0, 108, MiniWidth, 18), Qt::AlignCenter, caption);
        if (!alive) { p->setPen(QPen(Qt::lightGray, 2)); p->drawLine(8, 8, 24, 24); p->drawLine(24, 8, 8, 24); }
    }
    std::function<void(const QString &, bool, bool)> clicked;
    std::function<void(const QString &)> hovered;
    std::function<void()> unhovered;
    std::function<void(qreal)> dragged;
    std::function<void(int)> wheeled;
    QString name;
    TargetBadge *badge;
protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent *) override { if (hovered) hovered(name); }
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *) override { if (unhovered) unhovered(); }
    void mousePressEvent(QGraphicsSceneMouseEvent *e) override {
        parentItem()->setFocus(Qt::MouseFocusReason);
        start = last = e->scenePos(); moved = false; e->accept();
    }
    void mouseMoveEvent(QGraphicsSceneMouseEvent *e) override {
        if ((e->scenePos() - start).manhattanLength() > 5) moved = true;
        if (moved && dragged) dragged(parentItem()->mapFromScene(last).x() - parentItem()->mapFromScene(e->scenePos()).x());
        last = e->scenePos(); e->accept();
    }
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *e) override {
        if (!moved && clicked) clicked(name, e->button() == Qt::RightButton, e->pos().y() < 22 && e->pos().x() > 26);
        e->accept();
    }
    void wheelEvent(QGraphicsSceneWheelEvent *e) override { if (wheeled) wheeled(e->delta() > 0 ? -1 : 1); e->accept(); }
private:
    QString mark;
    Photo *photo;
    bool alive = true, self = false, moved = false, responseFrame = false;
    QPointF start, last;
};

class MiniRow final : public QGraphicsObject
{
public:
    explicit MiniRow(QGraphicsItem *parent, bool alwaysShowScroll = false)
        : QGraphicsObject(parent), alwaysShowScroll(alwaysShowScroll) {
        setFlag(ItemClipsChildrenToShape); setFlag(ItemIsFocusable);
        bar = new QScrollBar(Qt::Horizontal); bar->setFocusPolicy(Qt::NoFocus);
        proxy = new QGraphicsProxyWidget(this); proxy->setWidget(bar); proxy->setZValue(300);
        proxy->setVisible(alwaysShowScroll);
        connect(bar, &QScrollBar::valueChanged, this, [this](int value) { offset = value; arrange(); });
    }
    QRectF boundingRect() const override { return rect; }
    void paint(QPainter *, const QStyleOptionGraphicsItem *, QWidget *) override {}
    void setRect(const QRectF &value) {
        if (rect.size() != value.size()) { prepareGeometryChange(); rect = QRectF(QPointF(), value.size()); }
        setPos(value.topLeft());
        proxy->setGeometry(QRectF(0, MiniHeight, rect.width(), 18));
        arrange();
    }
    void scroll(qreal delta) { offset += delta; arrange(); }
    void reveal(const QString &id) {
        const int i = order.indexOf(id); if (i < 0) return;
        if (i * MiniStep < offset) offset = i * MiniStep;
        else if (i * MiniStep + MiniWidth > offset + rect.width()) offset = i * MiniStep + MiniWidth - rect.width();
        arrange();
    }
    void arrange() {
        const int maximum = qMax(0, qCeil(order.size() * MiniStep - 6 - rect.width()));
        // Empty or fully visible target rows need no scrollbar; the overview keeps its navigation affordance.
        proxy->setVisible(alwaysShowScroll || maximum > 0);
        offset = qBound<qreal>(0, offset, maximum);
        const QSignalBlocker blocker(bar);
        bar->setRange(0, maximum); bar->setPageStep(qMax(1, int(rect.width())));
        bar->setSingleStep(MiniStep); bar->setValue(qRound(offset));
        QHash<QString, int> positions;
        positions.reserve(order.size());
        for (int i = 0; i < order.size(); ++i) positions.insert(order.at(i), i);
        for (auto it = items.cbegin(); it != items.cend(); ++it) {
            const int i = positions.value(it.key(), -1); it.value()->setVisible(i >= 0);
            if (i >= 0) it.value()->setPos(i * MiniStep - offset, 0);
        }
    }
    PlayerMiniItem *item(const QString &id) {
        if (!items.contains(id)) items.insert(id, new PlayerMiniItem(this));
        return items.value(id);
    }
    QStringList order;
    QHash<QString, PlayerMiniItem *> items;
    std::function<void(int)> cycle;
protected:
    void wheelEvent(QGraphicsSceneWheelEvent *e) override {
        const int delta = e->delta() > 0 ? -1 : 1;
        if (cycle) cycle(delta); else scroll(delta * MiniStep); e->accept();
    }
private:
    QRectF rect;
    qreal offset = 0;
    bool alwaysShowScroll;
    QScrollBar *bar;
    QGraphicsProxyWidget *proxy;
};

// The resolution panel contains a native fullskin Photo, never card art.
class DetailCard final : public GraphicsBox
{
public:
    explicit DetailCard(QGraphicsItem *parent) : GraphicsBox(QString()) {
        setParentItem(parent); setFlags({});
        photo = new Photo; photo->setParentItem(this); photo->setAcceptedMouseButtons(Qt::NoButton); photo->hide();
    }
    QRectF boundingRect() const override { return rect; }
    void setRect(const QRectF &value) {
        if (rect.size() != value.size()) { prepareGeometryChange(); rect = QRectF(QPointF(), value.size()); }
        setPos(value.topLeft()); positionPhoto(); update();
    }
    void project(const GameViewPlayer *player, const QString &caption) {
        // Let Photo compare its own visible fields instead of serializing the full player.
        if (player) {
            photo->projectOverview(player->general, player->kingdom, player->hp, player->maxHp,
                player->handCount, player->handMax, !player->self && player->skills.contains(Sanguosha->translate(QStringLiteral("inovation_fengbi"))), player->alive);
        }
        const QString nextLabel = player ? player->label : QString();
        if (hasPlayer == (player != nullptr) && label == nextLabel && title == caption) return;
        title = caption; label = nextLabel; hasPlayer = player != nullptr;
        photo->setVisible(hasPlayer);
        setToolTip(caption + (player ? QLatin1Char('\n') + player->label : QString()));
        positionPhoto(); update();
    }
    std::function<void()> clicked;
protected:
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override { if (clicked) clicked(); event->accept(); }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override {
        p->save(); p->setClipRect(rect);
        paintGraphicsBoxStyle(p, QString(), rect);
        p->setFont(overviewFont()); p->setPen(Qt::white);
        QFontMetricsF metrics(overviewFont());
        // Elision respects translated text width; the full caption remains available as a tooltip.
        p->drawText(QRectF(6, 2, rect.width() - 12, 24), Qt::AlignCenter,
            metrics.elidedText(title, Qt::ElideRight, rect.width() - 12));
        if (hasPlayer) p->drawText(QRectF(6, 30, rect.width() - 12, 22), Qt::AlignCenter,
            metrics.elidedText(label, Qt::ElideRight, rect.width() - 12));
        p->restore();
    }
private:
    void positionPhoto() {
        const QRectF native = photo->boundingRect();
        const qreal scale = qMax<qreal>(0, qMin((rect.width() - 12) / native.width(), (rect.height() - 60) / native.height()));
        photo->setTransform(QTransform::fromScale(scale, scale));
        photo->setPos((rect.width() - native.width() * scale) / 2, 56);
    }
    QRectF rect;
    QString label;
    Photo *photo;
    bool hasPlayer = false;
};

class ScrollText final : public QGraphicsTextItem
{
public:
    explicit ScrollText(QGraphicsItem *parent) : QGraphicsTextItem(parent) {
        setDefaultTextColor(Qt::white); setFont(overviewFont());
    }
    qreal viewportHeight = 0;
protected:
    void wheelEvent(QGraphicsSceneWheelEvent *event) override {
        const qreal bottom = qMax<qreal>(0, boundingRect().height() - viewportHeight);
        setY(qBound(-bottom, y() + (event->delta() > 0 ? 40 : -40), 0.0));
        event->accept();
    }
};

class TextClip final : public QGraphicsItem
{
public:
    explicit TextClip(QGraphicsItem *parent) : QGraphicsItem(parent) { setFlag(ItemClipsChildrenToShape); }
    QRectF boundingRect() const override { return rect; }
    void paint(QPainter *, const QStyleOptionGraphicsItem *, QWidget *) override {}
    void setRect(const QRectF &value) { prepareGeometryChange(); rect = QRectF(QPointF(), value.size()); setPos(value.topLeft()); }
private:
    QRectF rect;
};

QGraphicsTextItem *textItem(QGraphicsItem *parent) {
    auto *item = new QGraphicsTextItem(parent); item->setDefaultTextColor(Qt::white);
    item->setFont(overviewFont()); item->document()->setDocumentMargin(0);
    item->setAcceptedMouseButtons(Qt::NoButton); return item;
}
void setText(QGraphicsTextItem *item, const QString &text) {
    item->setData(0, text); item->setToolTip(text);
    QStringList lines = text.split(QLatin1Char('\n'));
    const QFontMetricsF metrics(item->font());
    for (QString &line : lines) if (item->textWidth() > 0)
        line = metrics.elidedText(line, Qt::ElideRight, item->textWidth());
    const int maxLines = item->data(1).isValid() ? item->data(1).toInt() : 2;
    const QString fitted = lines.mid(0, maxLines).join(QLatin1Char('\n'));
    if (item->toPlainText() != fitted) item->setPlainText(fitted);
}
}

struct LargeRoomOverview::Data
{
    LargeRoomOverview *owner;
    DesktopGamePresentation *presentation;
    RoomLayoutEngine::ResponsiveResult layout;
    GameViewState view;
    GameActionModel actions;
    QHash<QString, GameViewPlayer> players;
    QHash<QString, GameActionEntry> targets;
    MiniRow *overview, *candidates, *draft;
    DetailCard *primaryCard, *secondaryCard, *detail;
    QGraphicsTextItem *relation, *count;
    ScrollText *detailText;
    TextClip *detailClip;
    OverviewButton *jumpFocus, *jumpSelf, *lock, *filter, *sort, *close, *toggleFocus;
    QString primary, secondary, preview, cursor, lockedPlayer, inspected;
    QStringList related, legal;
    QHash<QString, int> seatIndex;
    QHash<QString, QJsonObject> playerSnapshots;
    quint64 visualRevision = 0, catalogRevision = 0;
    QString assetRoot;
    bool onlyLegal = true;
    bool focusExpanded = false;
    int sortMode = 0;
    quint64 generation = 0, request = 0;

    static bool actionsChanged(const GameActionModel &a, const GameActionModel &b) {
        if (a.sessionGeneration != b.sessionGeneration || a.requestId != b.requestId
            || a.supported != b.supported
            || a.actionContext != b.actionContext || a.prompt != b.prompt
            || a.unsupportedReason != b.unsupportedReason
            || a.players.size() != b.players.size()) return true;
        for (int i = 0; i < a.players.size(); ++i) {
            const auto &x = a.players.at(i); const auto &y = b.players.at(i);
            if (x.id != y.id || x.enabled != y.enabled || x.selected != y.selected
                || x.reason != y.reason || x.selectedVotes != y.selectedVotes
                || x.maxVotes != y.maxVotes) return true;
        }
        return false;
    }

    OverviewButton *button(const QString &label, std::function<void()> fn) {
        auto *b = new OverviewButton(owner);
        // The native skin is loaded by the constructor before geometry is set.
        b->setActionText(label);
        QObject::connect(b, &QSanButton::clicked, owner, std::move(fn)); return b;
    }
    const GameViewPlayer *player(const QString &id) const {
        const auto it = players.constFind(id); return it == players.cend() ? nullptr : &it.value();
    }
    QString label(const QString &id) const { const auto *p = player(id); return p ? p->label : id; }
    void vote(const QString &id, bool remove) {
        const auto entry = targets.value(id);
        if (!actions.supported || (remove ? !entry.selected : !entry.enabled)) return;
        presentation->submitIntent(remove ? QStringLiteral("player-remove-vote") : QStringLiteral("player-add-vote"),
            id, !remove, actions.sessionGeneration, actions.presentationRevision, actions.requestId);
    }
    void inspect(const QString &id) { inspected = id; detail->show(); close->show(); detailText->show(); detailText->setY(0); refreshDetails(); }
    void hover(const QString &id) {
        cursor = id;
        for (MiniRow *row : {overview, candidates, draft})
            for (auto it = row->items.cbegin(); it != row->items.cend(); ++it) it.value()->badge->setCursor(it.key() == cursor);
        if (preview == id) return;
        preview = id; refreshFocus();
    }
    void cycle(int direction) {
        if (legal.isEmpty()) return;
        int index = legal.indexOf(cursor); index = index < 0 ? (direction > 0 ? 0 : legal.size() - 1)
            : (index + direction + legal.size()) % legal.size();
        candidates->reveal(legal[index]); hover(legal[index]); candidates->setFocus();
    }
    void refreshFocus() {
        const QString shown = !lockedPlayer.isEmpty() ? lockedPlayer : (!preview.isEmpty() ? preview : primary);
        primaryCard->project(player(shown), !lockedPlayer.isEmpty() ? QCoreApplication::translate("LargeRoomOverview", "Observation locked")
            : !preview.isEmpty() ? QCoreApplication::translate("LargeRoomOverview", "Target preview") : QCoreApplication::translate("LargeRoomOverview", "Primary focus"));
        secondaryCard->project(player(secondary), QCoreApplication::translate("LargeRoomOverview", "Source / target"));
        lock->setActionText(lockedPlayer.isEmpty() ? QCoreApplication::translate("LargeRoomOverview", "Following - Lock") : QCoreApplication::translate("LargeRoomOverview", "Locked - Follow"));
    }
    void refreshDetails() {
        const auto *p = player(inspected);
        detail->project(nullptr, QCoreApplication::translate("LargeRoomOverview", "Player details (scroll to browse)"));
        if (!p) { setText(detailText, QString()); return; }
        QStringList lines;
        lines << p->label << QCoreApplication::translate("LargeRoomOverview", "HP %1/%2 - Hand %3 - Limit %4").arg(p->hp).arg(p->maxHp).arg(p->handCount).arg(p->handMax)
            << QCoreApplication::translate("LargeRoomOverview", "Distance %1 - Attack modifier %2 - Defense modifier %3").arg(p->distanceFromOperatingPlayer).arg(p->offensiveDistance).arg(p->defensiveDistance)
            << QCoreApplication::translate("LargeRoomOverview", "%1 - %2 - %3 - %4").arg(p->alive ? QCoreApplication::translate("LargeRoomOverview", "Alive") : QCoreApplication::translate("LargeRoomOverview", "Dead"),
                p->faceUp.isValid() && !p->faceUp.toBool() ? QCoreApplication::translate("LargeRoomOverview", "Face down") : QCoreApplication::translate("LargeRoomOverview", "Face up"),
                p->chained.toBool() ? QCoreApplication::translate("LargeRoomOverview", "Chained") : QCoreApplication::translate("LargeRoomOverview", "Unchained"), p->removed.toBool() ? QCoreApplication::translate("LargeRoomOverview", "Removed") : QString())
            << QCoreApplication::translate("LargeRoomOverview", "Role: %1 - Kingdom: %2").arg(p->role, p->kingdom);
        for (const auto &card : p->equipment) lines << QCoreApplication::translate("LargeRoomOverview", "Equipment: ") + card.label;
        for (const auto &card : p->judging) lines << QCoreApplication::translate("LargeRoomOverview", "Judging: ") + card.label;
        lines << QCoreApplication::translate("LargeRoomOverview", "Skills: ") + p->skills.join(QCoreApplication::translate("LargeRoomOverview", ", "));
        for (auto it = p->marks.cbegin(); it != p->marks.cend(); ++it) lines << QCoreApplication::translate("LargeRoomOverview", "Mark %1: %2").arg(it.key(), it.value().toString());
        for (auto it = p->piles.cbegin(); it != p->piles.cend(); ++it)
            lines << QCoreApplication::translate("LargeRoomOverview", "Pile %1: %2").arg(it.key()).arg(it.value().toMap().value(QStringLiteral("count")).toInt());
        detailText->setPlainText(lines.join(QLatin1Char('\n')));
    }
    void bind(MiniRow *row, const QString &id, bool candidate, bool selected) {
        auto *mini = row->item(id);
        // Each item stays in its row; callbacks read the current targets/actions at invocation.
        if (!mini->clicked) {
            mini->dragged = [row](qreal delta) { row->scroll(delta); };
            mini->wheeled = [this, row, candidate](int delta) { if (candidate) cycle(delta); else row->scroll(delta * MiniStep); };
            mini->hovered = [this, candidate](const QString &name) { if (candidate) hover(name); };
            mini->unhovered = [this, candidate] { if (candidate && lockedPlayer.isEmpty()) { preview.clear(); refreshFocus(); } };
            mini->clicked = [this, candidate, selected](const QString &name, bool right, bool removeCorner) {
                const bool chosen = targets.value(name).selected;
                if (selected || (chosen && removeCorner)) vote(name, true);
                else if (candidate || right) vote(name, chosen);
                else inspect(name);
            };
        }
        const auto *p = player(id); if (!p) return;
        QString focus;
        if (id == view.currentPlayer) focus = QCoreApplication::translate("LargeRoomOverview", "Turn");
        if (related.contains(id)) focus = QCoreApplication::translate("LargeRoomOverview", "Resolution");
        const bool responding = view.responseFocus.contains(id);
        if (responding) focus.clear();
        mini->project(*p, focus, responding);
        const auto entry = targets.value(id);
        mini->badge->setState(actions.supported && !actions.players.isEmpty(), entry.enabled && targets.contains(id),
            entry.selected ? qMax(1, entry.selectedVotes) : 0);
        mini->badge->setCursor(id == cursor);
        const QString tip = p->label + (entry.reason.isEmpty() ? QString() : QLatin1Char('\n') + entry.reason);
        if (mini->toolTip() != tip) mini->setToolTip(tip);
    }
    void refreshRows(bool forceAll = true, const QSet<QString> &changed = {}) {
        const bool bindAll = forceAll || (sortMode != 0 && !changed.isEmpty());
        QStringList selected, candidateIds;
        legal.clear(); targets.clear();
        for (const auto &entry : actions.players) targets.insert(entry.id, entry);
        for (const QString &id : overview->order) {
            const auto entry = targets.value(id);
            const bool enabled = actions.supported && targets.contains(id) && entry.enabled;
            if (entry.selected) selected << id;
            if (!onlyLegal || enabled) candidateIds << id;
            if (bindAll || changed.contains(id)) bind(overview, id, false, false);
        }
        auto less = [this](const QString &a, const QString &b) {
            if (sortMode == 1) {
                bool okA = false, okB = false;
                const double da = players.value(a).distanceFromOperatingPlayer.toDouble(&okA);
                const double db = players.value(b).distanceFromOperatingPlayer.toDouble(&okB);
                if (okA != okB) return okA;
                if (okA && da != db) return da < db;
            } else if (sortMode == 2) {
                const int n = overview->order.size();
                const int ia = seatIndex.value(a, n), ib = seatIndex.value(b, n);
                if (qMin(ia, n - ia) != qMin(ib, n - ib)) return qMin(ia, n - ia) < qMin(ib, n - ib);
            } else if (sortMode == 3 && targets.value(a).enabled != targets.value(b).enabled)
                return targets.value(a).enabled;
            return seatIndex.value(a, INT_MAX) < seatIndex.value(b, INT_MAX);
        };
        // Seat order is already stable; keyboard navigation follows the displayed candidates.
        if (sortMode != 0) std::stable_sort(candidateIds.begin(), candidateIds.end(), less);
        for (const QString &id : candidateIds)
            if (actions.supported && targets.contains(id) && targets.value(id).enabled) legal << id;
        candidates->order = candidateIds; draft->order = selected;
        for (const QString &id : candidateIds)
            if (bindAll || changed.contains(id)) bind(candidates, id, true, false);
        for (const QString &id : selected)
            if (bindAll || changed.contains(id)) bind(draft, id, false, true);
        QString reason;
        if (legal.isEmpty()) {
            if (!actions.supported) reason = actions.unsupportedReason;
            else if (actions.players.isEmpty()) reason = QCoreApplication::translate("LargeRoomOverview", "No player selection is required");
            else {
                for (const auto &entry : actions.players) if (!entry.reason.isEmpty()) { reason = entry.reason; break; }
                if (reason.isEmpty()) reason = QCoreApplication::translate("LargeRoomOverview", "No legal targets for this card or skill");
            }
        }
        setText(count, QCoreApplication::translate("LargeRoomOverview", "Legal targets %1 / %2 - Selected %3%4").arg(legal.size()).arg(view.players.size()).arg(selected.size())
            .arg(reason.isEmpty() ? QString() : QCoreApplication::translate("LargeRoomOverview", " - ") + reason));
        if (!legal.contains(preview)) preview.clear();
        refreshFocus();
    }
    void project(const GameViewState &next, const GameActionModel &model) {
        const bool visualsChanged = visualRevision != G_ROOM_SKIN.visualRevision()
            || catalogRevision != QSanPackages::catalogRevision()
            || assetRoot != QSanRuntimePaths::assetRoot();
        visualRevision = G_ROOM_SKIN.visualRevision();
        catalogRevision = QSanPackages::catalogRevision();
        assetRoot = QSanRuntimePaths::assetRoot();
        const GameActionModel previousActions = actions;
        const GameViewState previousView = view;
        const QStringList previousOverviewOrder = overview->order;
        const bool newSession = generation != next.sessionGeneration;
        const bool requestChanged = request != model.requestId;
        const bool playersShared = !newSession && next.players.constData() == view.players.constData();
        if (newSession || requestChanged) { preview.clear(); cursor.clear(); }
        if (newSession) { lockedPlayer.clear(); inspected.clear(); detail->hide(); close->hide(); detailText->hide(); }
        generation = next.sessionGeneration; request = model.requestId; view = next; actions = model;
        bool playerValuesChanged = newSession || players.size() != view.players.size();
        QSet<QString> changedPlayers;
        bool seatChanged = newSession;
        if (!playersShared) {
            for (const auto &p : view.players) {
                const auto old = playerSnapshots.constFind(p.name);
                const QJsonObject snapshot = p.toJson();
                const bool changed = old == playerSnapshots.cend() || old.value() != snapshot;
                playerValuesChanged = playerValuesChanged || changed;
                if (changed) changedPlayers.insert(p.name);
                const auto oldPlayer = players.constFind(p.name);
                seatChanged = seatChanged || oldPlayer == players.cend() || oldPlayer.value().seat != p.seat;
                players.insert(p.name, p);
                playerSnapshots.insert(p.name, snapshot);
            }
        }
        const bool rosterChanged = newSession || overview->order.size() != view.players.size()
            || previousView.selfName != view.selfName || seatChanged;
        if (rosterChanged) {
            players.clear();
            playerSnapshots.clear();
            for (const auto &p : view.players) players.insert(p.name, p);
            for (const auto &p : view.players) playerSnapshots.insert(p.name, p.toJson());
            QList<GameViewPlayer> ordered = view.players;
            std::stable_sort(ordered.begin(), ordered.end(), [](const GameViewPlayer &a, const GameViewPlayer &b) { return a.seat < b.seat; });
            QStringList ring;
            for (const auto &player : ordered) ring << player.name;
            const int self = ring.indexOf(view.selfName);
            if (self > 0) std::rotate(ring.begin(), ring.begin() + self, ring.end());
            seatIndex.clear();
            for (int i = 0; i < ring.size(); ++i) seatIndex.insert(ring.at(i), i);
            if (ring != overview->order) overview->order = ring;
        }
        const bool resolutionChanged = newSession || previousView.activeResolutions != view.activeResolutions
            || previousView.responseFocus != view.responseFocus || previousView.resolutionAvailable != view.resolutionAvailable;
        if (resolutionChanged) { primary.clear(); secondary.clear(); related.clear(); }
        QString description = view.resolutionAvailable ? QCoreApplication::translate("LargeRoomOverview", "No active resolution") : QCoreApplication::translate("LargeRoomOverview", "Resolution information is not synchronized");
        if (!view.activeResolutions.isEmpty()) {
            const auto frame = view.activeResolutions.last().toMap();
            const QString affected = frame.value(QStringLiteral("affected")).toString();
            const QString actor = frame.value(QStringLiteral("actor")).toString();
            const QString source = frame.value(QStringLiteral("source")).toString();
            const QStringList targetNames = frame.value(QStringLiteral("targets")).toStringList();
            primary = !affected.isEmpty() ? affected : actor;
            secondary = source != primary ? source : QString();
            if (secondary.isEmpty() && actor != primary) secondary = actor;
            if (secondary.isEmpty()) for (const auto &id : targetNames) if (id != primary) { secondary = id; break; }
            for (const QString &id : {affected, actor, source}) if (!id.isEmpty() && !related.contains(id)) related << id;
            for (const auto &id : targetNames) if (!related.contains(id)) related << id;
            const QString kind = frame.value(QStringLiteral("kind")).toString();
            const QString card = frame.value(QStringLiteral("card_name")).toString();
            static const QHash<QString, QString> kinds = {
                {QStringLiteral("card"), QCoreApplication::translate("LargeRoomOverview", "Using a card")}, {QStringLiteral("effect"), QCoreApplication::translate("LargeRoomOverview", "Card effect")},
                {QStringLiteral("damage"), QCoreApplication::translate("LargeRoomOverview", "Damage")}, {QStringLiteral("recover"), QCoreApplication::translate("LargeRoomOverview", "Recovery")},
                {QStringLiteral("judge"), QCoreApplication::translate("LargeRoomOverview", "Judgement")}, {QStringLiteral("dying"), QCoreApplication::translate("LargeRoomOverview", "Dying rescue")},
                {QStringLiteral("skill"), QCoreApplication::translate("LargeRoomOverview", "Skill effect")}};
            const QString action = kinds.value(kind, QCoreApplication::translate("LargeRoomOverview", "Resolution"))
                + (card.isEmpty() ? QString() : QCoreApplication::translate("LargeRoomOverview", " - ") + Sanguosha->translate(card));
            const QString from = !source.isEmpty() ? source : actor;
            QStringList targetLabels; for (const auto &id : targetNames) targetLabels << label(id);
            const QString to = !affected.isEmpty() && affected != from ? label(affected) : targetLabels.join(QCoreApplication::translate("LargeRoomOverview", ", "));
            description = to.isEmpty() ? QCoreApplication::translate("LargeRoomOverview", "%1 is resolving %2").arg(label(primary), action)
                : QCoreApplication::translate("LargeRoomOverview", "%1 -> %2: %3").arg(label(from), to, action);
        }
        // A waiting response is independent of the turn owner and stale log history.
        if (!view.responseFocus.isEmpty()) {
            for (const auto &id : view.responseFocus) if (!related.contains(id)) related << id;
            QStringList waiting; for (const auto &id : view.responseFocus) waiting << label(id);
            if (primary.isEmpty() && view.responseFocus.size() == 1) primary = view.responseFocus.first();
            description += QCoreApplication::translate("LargeRoomOverview", "\nWaiting for %1 to respond").arg(waiting.join(QCoreApplication::translate("LargeRoomOverview", ", ")));
        }
        related.erase(std::remove_if(related.begin(), related.end(), [this](const QString &id) { return !players.contains(id); }), related.end());
        if (!players.contains(lockedPlayer)) lockedPlayer.clear();
        const bool actionChanged = actionsChanged(previousActions, actions);
        const bool currentPlayerChanged = previousView.currentPlayer != view.currentPlayer;
        const bool fullRefresh = visualsChanged || requestChanged || resolutionChanged || rosterChanged || actionChanged || currentPlayerChanged;
        const QStringList oldCandidateOrder = candidates->order;
        const QStringList oldDraftOrder = draft->order;
        if (fullRefresh || playerValuesChanged) {
            setText(relation, QCoreApplication::translate("LargeRoomOverview", "Current resolution: ") + description);
            refreshRows(fullRefresh, changedPlayers);
            if (overview->order != previousOverviewOrder) overview->arrange();
            if (candidates->order != oldCandidateOrder) candidates->arrange();
            if (draft->order != oldDraftOrder) draft->arrange();
        }
        if ((newSession || changedPlayers.contains(inspected) || !players.contains(inspected))
            && !inspected.isEmpty() && detail->isVisible()) refreshDetails();
    }
};

LargeRoomOverview::LargeRoomOverview(DesktopGamePresentation *presentation) : d(new Data)
{
    d->owner = this; d->presentation = presentation;
    setZValue(6); setAcceptedMouseButtons(Qt::NoButton);
    d->overview = new MiniRow(this, true); d->candidates = new MiniRow(this); d->draft = new MiniRow(this);
    d->primaryCard = new DetailCard(this); d->secondaryCard = new DetailCard(this); d->detail = new DetailCard(this);
    d->primaryCard->clicked = [this] {
        const QString id = !d->lockedPlayer.isEmpty() ? d->lockedPlayer : (!d->preview.isEmpty() ? d->preview : d->primary);
        if (!id.isEmpty()) d->inspect(id);
    };
    d->secondaryCard->clicked = [this] { if (!d->secondary.isEmpty()) d->inspect(d->secondary); };
    d->relation = textItem(this); d->count = textItem(this);
    d->primaryCard->hide(); d->secondaryCard->hide();
    d->toggleFocus = d->button(QCoreApplication::translate("LargeRoomOverview", "Expand focus"), [this] {
        // Presentation updates retain the user's choice; only this button changes it.
        d->focusExpanded = !d->focusExpanded;
        setLayout(d->layout);
    });
    d->detailClip = new TextClip(d->detail); d->detailText = new ScrollText(d->detailClip);
    d->jumpFocus = d->button(QCoreApplication::translate("LargeRoomOverview", "Jump to focus"), [this] {
        d->lockedPlayer.clear(); d->preview.clear(); d->overview->reveal(d->primary); d->refreshFocus(); setLayout(d->layout); });
    d->jumpSelf = d->button(QCoreApplication::translate("LargeRoomOverview", "Jump to self"), [this] { d->overview->reveal(d->view.selfName); });
    d->lock = d->button(QCoreApplication::translate("LargeRoomOverview", "Lock observation"), [this] {
        if (d->lockedPlayer.isEmpty()) d->lockedPlayer = d->preview.isEmpty() ? d->primary : d->preview;
        else { d->lockedPlayer.clear(); d->preview.clear(); } d->refreshFocus(); setLayout(d->layout); });
    d->filter = d->button(QCoreApplication::translate("LargeRoomOverview", "Legal targets only"), [this] { d->onlyLegal = !d->onlyLegal;
        d->filter->setActionText(d->onlyLegal ? QCoreApplication::translate("LargeRoomOverview", "Legal targets only") : QCoreApplication::translate("LargeRoomOverview", "All players")); d->refreshRows(); setLayout(d->layout); });
    d->sort = d->button(QCoreApplication::translate("LargeRoomOverview", "Sort: seat"), [this] { d->sortMode = (d->sortMode + 1) % 4;
        d->sort->setActionText(QStringList{QCoreApplication::translate("LargeRoomOverview", "Sort: seat"), QCoreApplication::translate("LargeRoomOverview", "Sort: distance"),
            QCoreApplication::translate("LargeRoomOverview", "Sort: proximity"), QCoreApplication::translate("LargeRoomOverview", "Sort: legality")}[d->sortMode]); d->refreshRows(); setLayout(d->layout); });
    d->close = d->button(QCoreApplication::translate("LargeRoomOverview", "Close details"), [this] { d->detail->hide(); d->close->hide(); d->detailText->hide(); });
    d->close->setZValue(22); d->detail->setZValue(20); d->detailText->setZValue(21);
    d->detail->hide(); d->close->hide(); d->detailText->hide();
    d->candidates->cycle = [this](int delta) { d->cycle(delta); };
    connect(presentation, &DesktopGamePresentation::presentationChanged, this,
        [this](const GameViewState &view, const GameActionModel &actions) { if (isVisible()) d->project(view, actions); });
}

LargeRoomOverview::~LargeRoomOverview() = default;
QRectF LargeRoomOverview::boundingRect() const { return d->layout.mainRect; }

void LargeRoomOverview::setLayout(const RoomLayoutEngine::ResponsiveResult &layout)
{
    if (boundingRect() != layout.mainRect) prepareGeometryChange();
    d->layout = layout;
    const QRectF header = layout.headerRect;
    qreal buttonsWidth = 12;
    for (auto *button : {d->jumpFocus, d->jumpSelf, d->lock}) buttonsWidth += button->boundingRect().width();
    const qreal buttonScale = qMin<qreal>(1, qMax<qreal>(0.25, (header.width() - 64) / buttonsWidth));
    qreal x = header.left() + 60;
    for (auto *button : {d->jumpFocus, d->jumpSelf, d->lock}) {
        button->setScale(buttonScale); button->setPos(x, header.top());
        x += (button->boundingRect().width() + 6) * buttonScale;
    }
    const qreal miniScale = qMax<qreal>(0.25, layout.seatsRect.height() / (MiniHeight + 18));
    const auto placeRow = [miniScale](MiniRow *row, const QRectF &rect) {
        row->setScale(miniScale);
        row->setRect(QRectF(rect.topLeft(), QSizeF(rect.width() / miniScale, MiniHeight + 18)));
    };
    placeRow(d->overview, layout.seatsRect);
    const QRectF focus = layout.resolutionRect;
    d->toggleFocus->setActionText(d->focusExpanded
        ? QCoreApplication::translate("LargeRoomOverview", "Collapse focus")
        : QCoreApplication::translate("LargeRoomOverview", "Expand focus"));
    d->toggleFocus->setPos(focus.topLeft());
    d->primaryCard->setVisible(d->focusExpanded);
    d->secondaryCard->setVisible(d->focusExpanded);
    const qreal half = (focus.width() - 8) / 2;
    const qreal cardTop = focus.top() + 34;
    const qreal cardHeight = qMax<qreal>(32, focus.height() - 74);
    d->primaryCard->setRect(QRectF(focus.left(), cardTop, half, cardHeight));
    d->secondaryCard->setRect(QRectF(focus.left() + half + 8, cardTop, half, cardHeight));
    // Collapsed mode keeps one short live summary and the full text in its tooltip.
    const qreal summaryLeft = d->focusExpanded ? focus.left()
        : focus.left() + d->toggleFocus->boundingRect().width() + 8;
    d->relation->setPos(summaryLeft, d->focusExpanded ? focus.bottom() - 38 : focus.top() + 5);
    d->relation->setTextWidth(qMax<qreal>(1, focus.right() - summaryLeft));
    d->relation->setData(1, d->focusExpanded ? 2 : 1);
    const QRectF actions = layout.actionsRect;
    d->filter->setPos(actions.topLeft());
    d->sort->setPos(actions.left() + d->filter->boundingRect().width() + 6, actions.top());
    const qreal countX = d->sort->pos().x() + d->sort->boundingRect().width() + 10;
    d->count->setPos(countX, actions.top() + 4);
    d->count->setTextWidth(qMax<qreal>(1, actions.right() - countX));
    for (auto *text : {d->relation, d->count}) setText(text, text->data(0).toString());
    placeRow(d->candidates, QRectF(actions.left(), actions.top() + 32, actions.width() * 0.65 - 8, MiniHeight));
    placeRow(d->draft, QRectF(actions.left() + actions.width() * 0.65, actions.top() + 32, actions.width() * 0.35, MiniHeight));
    const qreal w = qMin<qreal>(680, layout.mainRect.width() - 24);
    const qreal h = qMin<qreal>(540, layout.mainRect.height() - 24);
    const QRectF popup(layout.mainRect.center() - QPointF(w / 2, h / 2), QSizeF(w, h));
    d->detail->setRect(popup);
    d->close->setPos(popup.right() - d->close->boundingRect().width() - 8, popup.bottom() - 34);
    d->detailClip->setRect(QRectF(8, 32, w - 16, h - 76));
    d->detailText->setX(0); d->detailText->setTextWidth(w - 16); d->detailText->viewportHeight = h - 76;
}

QVariant LargeRoomOverview::itemChange(GraphicsItemChange change, const QVariant &value)
{
    // Follow scene ownership instead of reinstalling the filter on every layout refresh.
    if (change == ItemSceneChange && scene()) scene()->removeEventFilter(this);
    const QVariant result = QGraphicsObject::itemChange(change, value);
    if (change == ItemSceneHasChanged && scene()) scene()->installEventFilter(this);
    return result;
}

bool LargeRoomOverview::eventFilter(QObject *watched, QEvent *event)
{
    if (!isVisible() || watched != scene() || event->type() != QEvent::KeyPress) return false;
    // Chat and embedded editors retain their ordinary keyboard behavior.
    for (QGraphicsItem *item = scene()->focusItem(); item; item = item->parentItem())
        if (dynamic_cast<QGraphicsProxyWidget *>(item)) return false;
    auto *key = static_cast<QKeyEvent *>(event);
    if (key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) return false;
    if ((key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) && !d->related.isEmpty()) {
        const int direction = key->key() == Qt::Key_Backtab || key->modifiers().testFlag(Qt::ShiftModifier) ? -1 : 1;
        int index = d->related.indexOf(d->cursor);
        index = index < 0 ? (direction > 0 ? 0 : d->related.size() - 1) : (index + direction + d->related.size()) % d->related.size();
        d->overview->reveal(d->related[index]); d->hover(d->related[index]); key->accept(); return true;
    }
    const bool inside = scene()->focusItem() && isAncestorOf(scene()->focusItem());
    if (!inside) return false;
    if (key->key() == Qt::Key_Left || key->key() == Qt::Key_Right) {
        const int direction = key->key() == Qt::Key_Left ? -1 : 1;
        if (scene()->focusItem() == d->overview) d->overview->scroll(direction * MiniStep); else d->cycle(direction);
    } else if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) d->vote(d->cursor, false);
    else if (key->key() == Qt::Key_Delete) d->vote(d->cursor, true);
    else if (key->key() == Qt::Key_Escape) { d->preview.clear(); d->refreshFocus(); d->detail->hide(); d->close->hide(); d->detailText->hide(); }
    else return false;
    key->accept(); return true;
}
