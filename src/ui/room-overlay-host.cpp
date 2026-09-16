#include "room-overlay-host.h"

#include "desktop-game-presentation.h"
#include "settings.h"

#include <QAction>
#include <QApplication>
#include <QActionGroup>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QRegion>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSignalBlocker>
#include <QScroller>
#include <QTextBrowser>
#include <QScrollBar>
#include <QTextDocument>
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
    m_launcher = new QToolButton(this);
    m_launcher->setObjectName(QStringLiteral("roomOverlayLauncher"));
    m_launcher->setText(QStringLiteral("☰"));
    m_launcher->setToolTip(tr("Room layout and views"));
    m_launcher->setAccessibleName(tr("Room layout and views"));
    setTouchSize(m_launcher);
    connect(m_launcher, &QToolButton::clicked, this, [this] {
        auto *menu = new QMenu(this);
        menu->setStyleSheet(QStringLiteral("QMenu::item { min-height: 48px; padding: 0 12px; }"));
        QAction *responsive = menu->addAction(tr("Responsive preview"));
        responsive->setCheckable(true);
        responsive->setChecked(m_responsiveEnabled);
        connect(responsive, &QAction::toggled, this, &RoomOverlayHost::setResponsiveEnabled);
        menu->addAction(tr("Player details"), this, [this] {
            if (m_inspectedPlayer.isEmpty() && !m_view.players.isEmpty())
                m_inspectedPlayer = m_view.operatingPlayer;
            setInspectorOpen(true);
        });
        QAction *logAction = menu->addAction(tr("Game log"), this, [this] {
            m_logVisible = !m_logVisible; updateGeometry(); emit layoutPreferencesChanged();
        });
        logAction->setCheckable(true);
        logAction->setChecked(m_logVisible);
        logAction->setEnabled(m_responsiveEnabled
            && m_layout.profile != RoomLayoutEngine::Profile::LegacyLandscape);
        menu->addAction(tr("Chat"), this, [this] {
            m_chatVisible = !m_chatVisible; updateGeometry(); emit layoutPreferencesChanged();
        });
        auto *hand = menu->addMenu(tr("One-handed layout"));
        auto *group = new QActionGroup(hand);
        group->setExclusive(true);
        const QList<QPair<QString, RoomLayoutEngine::Handedness>> values = {
            {tr("None"), RoomLayoutEngine::Handedness::None},
            {tr("Left"), RoomLayoutEngine::Handedness::Left},
            {tr("Right"), RoomLayoutEngine::Handedness::Right}};
        for (const auto &value : values) {
            QAction *action = hand->addAction(value.first);
            action->setCheckable(true);
            action->setChecked(value.second == m_handedness);
            group->addAction(action);
            connect(action, &QAction::triggered, this, [this, value] { saveHandedness(value.second); });
        }
        menu->addSeparator();
        menu->addAction(tr("Game controls"), this, &RoomOverlayHost::controlsRequested);
        menu->exec(m_launcher->mapToGlobal(QPoint(0, m_launcher->height())));
        menu->deleteLater();
    });

    // Native Photo items receive the clicks; this control only pages their positions.
    m_nativeSeatScroll = new QScrollBar(Qt::Horizontal, this);
    m_nativeSeatScroll->setObjectName(QStringLiteral("roomNativeSeatScroll"));
    m_nativeSeatScroll->setAccessibleName(tr("Player seats"));
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

    m_chatPanel = makePanel(this);
    auto *chatBox = new QVBoxLayout(m_chatPanel);
    auto *chatHeader = new QHBoxLayout;
    chatHeader->addWidget(new QLabel(tr("Chat"), m_chatPanel), 1);
    m_chatClose = new QToolButton(m_chatPanel);
    m_chatClose->setText(QStringLiteral("×")); setTouchSize(m_chatClose);
    chatHeader->addWidget(m_chatClose);
    chatBox->addLayout(chatHeader);
    connect(m_chatClose, &QToolButton::clicked, this, [this] { m_chatVisible = false; updateGeometry(); emit layoutPreferencesChanged(); });
    m_chatView = new QTextBrowser(m_chatPanel);
    m_chatView->setObjectName(QStringLiteral("roomLiveChat"));
    chatBox->addWidget(m_chatView, 1);
    auto *chatRow = new QHBoxLayout;
    m_chatDraft = new QLineEdit(m_chatPanel);
    m_chatDraft->setObjectName(QStringLiteral("roomChatDraft"));
    setTouchSize(m_chatDraft);
    auto *sendChat = new QPushButton(tr("Send"), m_chatPanel);
    setTouchSize(sendChat);
    chatRow->addWidget(m_chatDraft, 1);
    chatRow->addWidget(sendChat);
    chatBox->addLayout(chatRow);
    connect(sendChat, &QPushButton::clicked, this, &RoomOverlayHost::sendChatRequested);
    connect(m_chatDraft, &QLineEdit::returnPressed, this, &RoomOverlayHost::sendChatRequested);
    m_chatPanel->hide();
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
bool RoomOverlayHost::logVisible() const { return m_logVisible; }
bool RoomOverlayHost::chatVisible() const { return m_chatVisible; }
bool RoomOverlayHost::inspectorRequested() const { return m_inspectorRequested; }
RoomLayoutEngine::Handedness RoomOverlayHost::handedness() const { return m_handedness; }

void RoomOverlayHost::inspectPlayer(const QString &player)
{
    m_inspectedPlayer = player;
    setInspectorOpen(true);
    updateInspector();
}

void RoomOverlayHost::setChatDocument(QTextDocument *chat, QLineEdit *draft)
{
    const QPointer<QLineEdit> oldDraft = m_legacyDraft;
    if (oldDraft) {
        disconnect(m_chatDraft, nullptr, oldDraft, nullptr);
        disconnect(oldDraft, nullptr, m_chatDraft, nullptr);
    }
    m_chatDocument = chat;
    m_legacyDraft = draft;
    if (chat) m_chatView->setDocument(chat);
    if (draft) {
        m_chatDraft->setMaxLength(draft->maxLength());
        m_chatDraft->setText(draft->text());
        connect(m_chatDraft, &QLineEdit::textChanged, draft, [draft](const QString &text) {
            if (draft && draft->text() != text) draft->setText(text);
        });
        connect(draft, &QLineEdit::textChanged, m_chatDraft, [this](const QString &text) {
            if (m_chatDraft->text() != text) m_chatDraft->setText(text);
        });
    }
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

void RoomOverlayHost::saveHandedness(RoomLayoutEngine::Handedness value)
{
    if (m_handedness == value) return;
    Config.setOneHandedness(static_cast<int>(value));
}

void RoomOverlayHost::updateGeometry()
{
    if (!m_launcher) return;
    QRect pane = widgetRect(m_layout.mainRect);
    if (pane.isEmpty()) pane = QRect(0, 0, width(), height());
    const QRect header = widgetRect(m_layout.headerRect);
    m_launcher->setGeometry(pane.left() + 8,
        header.isEmpty() ? pane.top() + 8 : header.top() + (header.height() - 48) / 2, 48, 48);
    const bool active = m_responsiveEnabled && m_layout.valid
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
    const bool twoPanels = m_logVisible && m_chatVisible;
    const int fallbackWidth = qMax(0, qMin(360, (pane.width() - 24) / (twoPanels ? 2 : 1)));
    const int fallbackHeight = qMax(0, qMin(pane.height() / 2, pane.height() - 16));
    const int fallbackTop = pane.top() + qMax(8, pane.height() / 4);
    const QRect chatRect = widgetRect(m_layout.chatRect);
    const QRect chatFallback(m_logVisible ? pane.right() - fallbackWidth - 8 : pane.left() + 8,
                             fallbackTop, fallbackWidth, fallbackHeight);
    m_chatPanel->setGeometry(chatRect.isEmpty() ? chatFallback : chatRect);
    m_inspector->setVisible(m_inspectorRequested || !inspectorRect.isEmpty());
    if (auto *closeInspector = m_inspector->findChild<QToolButton *>(QStringLiteral("roomInspectorClose")))
        closeInspector->setEnabled(!permanentSplit);
    const bool ribbon = active && m_layout.seatPresentation == RoomLayoutEngine::SeatPresentation::Ribbon
        && !widgetRect(m_layout.seatsRect).isEmpty();
    const int hiddenSeats = qMax(0, m_layout.photos.size() - m_layout.visibleSeatCount);
    m_nativeSeatScroll->setVisible(ribbon && hiddenSeats > 0);
    if (ribbon) {
        const QSignalBlocker blocker(m_nativeSeatScroll);
        m_nativeSeatScroll->setRange(0, hiddenSeats);
        m_nativeSeatScroll->setPageStep(qMax(1, m_layout.visibleSeatCount));
        m_nativeSeatScroll->setValue(m_layout.firstVisibleSeat);
        // Keep the paging control in the reserved header, clear of all native targets.
        m_nativeSeatScroll->setGeometry(m_launcher->geometry().right() + 8,
            m_launcher->y(), qMax(0, pane.width() - 72), 48);
    }
    m_chatPanel->setVisible(active && m_chatVisible);
    updateMask();
}

void RoomOverlayHost::updateMask()
{
    QRegion region(m_launcher->geometry());
    if (m_nativeSeatScroll->isVisible()) region += m_nativeSeatScroll->geometry();
    if (m_inspector->isVisible()) region += m_inspector->geometry();
    if (m_chatPanel->isVisible()) region += m_chatPanel->geometry();
    setMask(region);
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
