#include "room-overlay-host.h"

#include "desktop-game-presentation.h"
#include "settings.h"

#include <QApplication>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QRegion>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSignalBlocker>
#include <QScroller>
#include <QScrollBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace {
QRect widgetRect(const QRectF &rect)
{
    return rect.isValid() && !rect.isEmpty() ? rect.toAlignedRect() : QRect();
}

QWidget *makePanel(QWidget *parent)
{
    auto *panel = new QWidget(parent);
    panel->setAttribute(Qt::WA_StyledBackground, true);
    panel->setStyleSheet(QStringLiteral("QWidget { background: rgba(25, 31, 42, 238); color: white; "
                                        "border: 1px solid #8794a8; border-radius: 8px; }"
                                        "QToolButton, QPushButton, QLineEdit { background: #344257; "
                                        "border: 1px solid #71819a; border-radius: 5px; padding: 5px; }"));
    return panel;
}

void setTouchSize(QWidget *widget)
{
    widget->setMinimumSize(48, 48);
    widget->setFocusPolicy(Qt::StrongFocus);
}
}

RoomOverlayHost::RoomOverlayHost(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("roomOverlayHost"));
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    m_handedness = static_cast<RoomLayoutEngine::Handedness>(Config.oneHandedness());
    connect(&Config, &Settings::uiLayoutChanged, this, [this] {
        m_handedness = static_cast<RoomLayoutEngine::Handedness>(Config.oneHandedness());
        emit layoutPreferencesChanged();
    });
    createPersistentUi();
    updateGeometry();
}

void RoomOverlayHost::createPersistentUi()
{
    // Native Photo items receive the clicks; this control only pages their positions.
    m_nativeSeatScroll = new QScrollBar(Qt::Horizontal, this);
    m_nativeSeatScroll->setObjectName(QStringLiteral("roomNativeSeatScroll"));
    m_nativeSeatScroll->setAccessibleName(tr("Player seats"));
    // Include native seat paging in Tab navigation; QScrollBar defaults to NoFocus.
    m_nativeSeatScroll->setFocusPolicy(Qt::StrongFocus);
    m_nativeSeatScroll->setSingleStep(1);
    connect(m_nativeSeatScroll, &QScrollBar::valueChanged, this, &RoomOverlayHost::layoutPreferencesChanged);
    m_inspector = makePanel(this);
    m_inspector->setObjectName(QStringLiteral("roomPlayerInspector"));
    auto *inspectorBox = new QVBoxLayout(m_inspector);
    inspectorBox->setContentsMargins(10, 10, 10, 10);
    auto *inspectorHeader = new QHBoxLayout;
    inspectorHeader->addWidget(new QLabel(tr("Player details"), m_inspector), 1);
    auto *closeInspector = new QToolButton(m_inspector);
    closeInspector->setObjectName(QStringLiteral("roomInspectorClose"));
    closeInspector->setText(QStringLiteral("×"));
    closeInspector->setAccessibleName(tr("Close player details"));
    setTouchSize(closeInspector);
    inspectorHeader->addWidget(closeInspector);
    inspectorBox->addLayout(inspectorHeader);
    connect(closeInspector, &QToolButton::clicked, this, [this] { setInspectorOpen(false); });
    auto *playerPicker = new QComboBox(m_inspector);
    playerPicker->setObjectName(QStringLiteral("roomInspectorPlayerPicker"));
    setTouchSize(playerPicker);
    inspectorBox->addWidget(playerPicker);
    connect(playerPicker, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this, playerPicker](int index) {
        if (index >= 0) { m_inspectedPlayer = playerPicker->itemData(index).toString(); updateInspector(); }
    });
    auto *details = new QLabel(m_inspector);
    details->setObjectName(QStringLiteral("roomInspectorDetails"));
    details->setWordWrap(true);
    details->setTextFormat(Qt::PlainText);
    details->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    auto *detailsScroll = new QScrollArea(m_inspector);
    detailsScroll->setWidgetResizable(true);
    detailsScroll->setFrameShape(QFrame::NoFrame);
    detailsScroll->setWidget(details);
    inspectorBox->addWidget(detailsScroll, 1);
    m_inspectorLayout = inspectorBox;
    m_inspector->setProperty("detailsLabel", QVariant::fromValue<QObject *>(details));
    m_inspector->setProperty("playerPicker", QVariant::fromValue<QObject *>(playerPicker));
    m_inspector->hide();
}

void RoomOverlayHost::setPresentation(DesktopGamePresentation *presentation)
{
    if (m_presentation == presentation) return;
    if (m_presentation) {
        if (m_registeredLive) m_presentation->setLiveConsumer(this, false);
        disconnect(m_presentation, nullptr, this, nullptr);
    }
    m_registeredLive = false;
    m_presentation = presentation;
    if (!m_presentation) return;
    connect(m_presentation, &DesktopGamePresentation::presentationChanged,
            this, &RoomOverlayHost::updateFromPresentation, Qt::UniqueConnection);
    updateGeometry();
}

void RoomOverlayHost::setLayoutResult(const RoomLayoutEngine::ResponsiveResult &layout)
{
    m_layout = layout;
    updateGeometry();
}

void RoomOverlayHost::setResponsiveEnabled(bool enabled)
{
    if (m_responsiveEnabled == enabled) return;
    m_responsiveEnabled = enabled;
    emit responsiveEnabledChanged(enabled);
    updateGeometry();
}

bool RoomOverlayHost::responsiveEnabled() const { return m_responsiveEnabled; }

bool RoomOverlayHost::logShown() const { return m_logVisible || m_layout.logAlwaysVisible; }

bool RoomOverlayHost::logToggleEnabled() const
{
    return !m_layout.logAlwaysVisible
        && (m_responsiveEnabled || m_layout.profile == RoomLayoutEngine::Profile::LargeRoom)
        && m_layout.profile != RoomLayoutEngine::Profile::LegacyLandscape;
}

void RoomOverlayHost::toggleLog()
{
    m_logVisible = !m_logVisible;
    updateGeometry();
    emit layoutPreferencesChanged();
}
bool RoomOverlayHost::logVisible() const { return m_logVisible; }
bool RoomOverlayHost::inspectorRequested() const { return m_inspectorRequested; }
RoomLayoutEngine::Handedness RoomOverlayHost::handedness() const { return m_handedness; }

void RoomOverlayHost::inspectPlayer(const QString &player)
{
    m_inspectedPlayer = player;
    setInspectorOpen(true);
    updateInspector();
}

int RoomOverlayHost::firstVisibleSeat() const
{
    return m_nativeSeatScroll->value();
}

void RoomOverlayHost::updateFromPresentation(const GameViewState &view, const GameActionModel &)
{
    // The native Dashboard owns input; projection is needed only by the inspector.
    m_view = view;
    updateInspector();
    updateGeometry();
}
void RoomOverlayHost::setInspectorOpen(bool open)
{
    if (m_inspectorRequested == open) return;
    m_inspectorRequested = open;
    updateInspector();
    updateGeometry();
    emit layoutPreferencesChanged();
}

void RoomOverlayHost::updateInspector()
{
    auto *picker = qobject_cast<QComboBox *>(m_inspector->property("playerPicker").value<QObject *>());
    auto *details = qobject_cast<QLabel *>(m_inspector->property("detailsLabel").value<QObject *>());
    if (!picker || !details) return;
    const QSignalBlocker blocker(picker);
    picker->clear();
    if (!m_view.ready) {
        details->setText(tr("Synchronizing game state…"));
        m_inspector->setVisible(m_inspectorRequested || !widgetRect(m_layout.inspectorRect).isEmpty());
        return;
    }
    const GameViewPlayer *selected = nullptr;
    for (const GameViewPlayer &player : m_view.players) {
        picker->addItem(player.label, player.name);
        if (player.name == m_inspectedPlayer) selected = &player;
    }
    if (!selected && !m_view.players.isEmpty()) {
        selected = &m_view.players.front();
        m_inspectedPlayer = selected->name;
    }
    if (!selected) {
        details->clear();
        m_inspector->setVisible(m_inspectorRequested || !widgetRect(m_layout.inspectorRect).isEmpty());
        return;
    }
    picker->setCurrentIndex(qMax(0, picker->findData(selected->name)));
    QStringList lines;
    lines << selected->label
          << tr("HP: %1/%2").arg(selected->hp).arg(selected->maxHp)
          << tr("Hand: %1").arg(selected->handCount)
          << tr("Alive: %1").arg(selected->alive ? tr("Yes") : tr("No"));
    if (!selected->role.isEmpty()) lines << tr("Role: %1").arg(selected->role);
    if (selected->faceUp.isValid()) lines << tr("Face up: %1").arg(selected->faceUp.toBool() ? tr("Yes") : tr("No"));
    if (selected->chained.isValid()) lines << tr("Chained: %1").arg(selected->chained.toBool() ? tr("Yes") : tr("No"));
    if (selected->removed.isValid()) lines << tr("Removed: %1").arg(selected->removed.toBool() ? tr("Yes") : tr("No"));
    if (selected->handMax >= 0) lines << tr("Hand limit: %1").arg(selected->handMax);
    if (!selected->distanceFromOperatingPlayer.isEmpty())
        lines << tr("Distance: %1").arg(selected->distanceFromOperatingPlayer);
    if (selected->offensiveDistance >= 0)
        lines << tr("Offensive distance: %1").arg(selected->offensiveDistance);
    if (selected->defensiveDistance >= 0)
        lines << tr("Defensive distance: %1").arg(selected->defensiveDistance);
    if (!selected->marks.isEmpty()) {
        lines << tr("Marks:");
        for (auto it = selected->marks.cbegin(); it != selected->marks.cend(); ++it)
            lines << QStringLiteral("• %1: %2").arg(it.key(), it.value().toString());
    }
    if (!selected->skills.isEmpty()) lines << tr("Skills: %1").arg(selected->skills.join(QStringLiteral(", ")));
    if (!selected->piles.isEmpty()) {
        lines << tr("Piles:");
        for (auto it = selected->piles.cbegin(); it != selected->piles.cend(); ++it) {
            const QVariantMap pile = it.value().toMap();
            const int count = pile.value(QStringLiteral("count")).toInt();
            lines << QStringLiteral("• %1: %2").arg(it.key()).arg(count);
            const QVariantList cards = pile.value(QStringLiteral("cards")).toList();
            QStringList visibleLabels;
            for (const QVariant &card : cards)
                visibleLabels << card.toMap().value(QStringLiteral("label")).toString();
            if (!visibleLabels.isEmpty()) lines << tr("  Visible: %1").arg(visibleLabels.join(QStringLiteral(", ")));
        }
    }
    if (selected->handVisible && !selected->hand.isEmpty()) {
        QStringList hand;
        for (const GameViewCard &card : selected->hand) hand << card.label;
        lines << tr("Visible hand: %1").arg(hand.join(QStringLiteral(", ")));
    }
    if (!selected->equipment.isEmpty()) {
        QStringList equipment;
        for (const GameViewCard &card : selected->equipment) equipment << card.label;
        lines << tr("Equipment: %1").arg(equipment.join(QStringLiteral(", ")));
    }
    if (!selected->judging.isEmpty()) {
        QStringList judging;
        for (const GameViewCard &card : selected->judging) judging << card.label;
        lines << tr("Judging area: %1").arg(judging.join(QStringLiteral(", ")));
    }
    details->setText(lines.join(QLatin1Char('\n')));
    m_inspector->setVisible(m_inspectorRequested || !widgetRect(m_layout.inspectorRect).isEmpty());
}

void RoomOverlayHost::updateGeometry()
{
    if (!m_nativeSeatScroll) return;
    QRect pane = widgetRect(m_layout.mainRect);
    if (pane.isEmpty()) pane = QRect(0, 0, width(), height());
    const QRect header = widgetRect(m_layout.headerRect);
    const bool active = (m_responsiveEnabled || m_layout.profile == RoomLayoutEngine::Profile::LargeRoom) && m_layout.valid
        && m_layout.profile != RoomLayoutEngine::Profile::LegacyLandscape;
    const QRect inspectorRect = widgetRect(m_layout.inspectorRect);
    const bool permanentSplit = !inspectorRect.isEmpty()
        && (m_layout.profile == RoomLayoutEngine::Profile::ExpandedSplit
            || m_layout.profile == RoomLayoutEngine::Profile::Book)
        && !widgetRect(m_layout.mainRect).intersects(inspectorRect);
    const bool needsLiveProjection = m_inspectorRequested || permanentSplit;
    if (m_presentation && needsLiveProjection != m_registeredLive) {
        m_registeredLive = needsLiveProjection;
        m_presentation->setLiveConsumer(this, m_registeredLive);
        if (m_registeredLive) m_presentation->requestRefresh();
    }
    QRect fallback;
    if (inspectorRect.isEmpty() && m_inspectorRequested) {
        const int drawerWidth = qMax(0, qMin(360, pane.width() - 16));
        const int drawerHeight = qMax(0, pane.height() - 80);
        fallback = QRect(m_handedness == RoomLayoutEngine::Handedness::Left
                             ? pane.left() + 8 : pane.right() - drawerWidth - 8,
                         pane.top() + qMin(64, pane.height()), drawerWidth,
                         qMin(drawerHeight, qMax(0, pane.bottom() - pane.top() - 72)));
    }
    m_inspector->setGeometry(inspectorRect.isEmpty() ? fallback : inspectorRect);
    m_inspector->setVisible(m_inspectorRequested || !inspectorRect.isEmpty());
    if (auto *closeInspector = m_inspector->findChild<QToolButton *>(QStringLiteral("roomInspectorClose")))
        closeInspector->setEnabled(!permanentSplit);
    const bool ribbon = active && m_layout.seatPresentation == RoomLayoutEngine::SeatPresentation::Ribbon
        && m_layout.profile != RoomLayoutEngine::Profile::LargeRoom
        && !widgetRect(m_layout.seatsRect).isEmpty();
    const int hiddenSeats = qMax(0, m_layout.photos.size() - m_layout.visibleSeatCount);
    m_nativeSeatScroll->setVisible(ribbon && hiddenSeats > 0);
    if (ribbon) {
        const QSignalBlocker blocker(m_nativeSeatScroll);
        m_nativeSeatScroll->setRange(0, hiddenSeats);
        m_nativeSeatScroll->setPageStep(qMax(1, m_layout.visibleSeatCount));
        m_nativeSeatScroll->setValue(m_layout.firstVisibleSeat);
        // Keep the paging control in the reserved header, clear of all native targets.
        const int seatLeft = pane.left() + 8;
        m_nativeSeatScroll->setGeometry(seatLeft,
            header.isEmpty() ? pane.top() + 8 : header.top() + (header.height() - 48) / 2,
            qMax(0, (header.isEmpty() ? pane.right() : header.right()) - seatLeft), 48);
    }
    updateMask();
}

void RoomOverlayHost::updateMask()
{
    QRegion region;
    if (m_nativeSeatScroll->isVisible()) region += m_nativeSeatScroll->geometry();
    if (m_inspector->isVisible()) region += m_inspector->geometry();
    // An empty mask means no mask, so let clicks fall through instead.
    setAttribute(Qt::WA_TransparentForMouseEvents, region.isEmpty());
    setMask(region);
    emit nativeRegionChanged(region.translated(geometry().topLeft()));
}

void RoomOverlayHost::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateGeometry();
}

void RoomOverlayHost::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && m_inspectorRequested) {
        setInspectorOpen(false);
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}
