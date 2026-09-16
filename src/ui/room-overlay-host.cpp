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
    m_handedness = static_cast<RoomLayoutEngine::Handedness>(
        qBound(0, Config.value(QStringLiteral("UI/RoomHandedness"), 0).toInt(), 2));
    createPersistentUi();
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *focused) {
        if (!focused || !isAncestorOf(focused)) return;
        // Reveal keyboard focus in each nested scroll zone without moving the
        // fixed action row or the table itself.
        for (QScrollArea *scroll : {m_handScroll, m_seatsScroll, m_actionsScroll,
                                   m_ribbonScroll, m_contentScroll}) {
            if (scroll->widget() && scroll->widget()->isAncestorOf(focused))
                scroll->ensureWidgetVisible(focused);
        }
    });
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
        menu->addAction(tr("Game log"), this, [this] {
            m_logVisible = !m_logVisible; updateGeometry(); emit layoutPreferencesChanged();
        });
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

    m_interaction = makePanel(this);
    m_interaction->setObjectName(QStringLiteral("roomInteractionZone"));
    m_interaction->setMinimumSize(0, 0);
    m_interactionLayout = new QVBoxLayout(m_interaction);
    m_interactionLayout->setContentsMargins(8, 8, 8, 8);
    m_interactionLayout->setSpacing(5);
    m_contentScroll = new QScrollArea(m_interaction);
    m_contentScroll->setObjectName(QStringLiteral("roomInteractionScroll"));
    m_contentScroll->setWidgetResizable(true);
    m_contentScroll->setFrameShape(QFrame::NoFrame);
    m_contentScroll->setMinimumSize(0, 0);
    m_contentScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    m_contentWidget = new QWidget(m_contentScroll);
    m_contentLayout = new QVBoxLayout(m_contentWidget);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(5);
    m_statusLabel = new QLabel(m_contentWidget);
    m_statusLabel->setObjectName(QStringLiteral("roomInteractionStatus"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setTextFormat(Qt::PlainText);
    m_contentLayout->addWidget(m_statusLabel);
    m_promptLabel = new QLabel(m_contentWidget);
    m_promptLabel->setWordWrap(true);
    m_promptLabel->setTextFormat(Qt::PlainText);
    m_contentLayout->addWidget(m_promptLabel);

    m_handPanel = new QWidget(m_contentWidget);
    m_handPanel->setStyleSheet(QStringLiteral("background: transparent; border: 0;"));
    auto *handOuter = new QVBoxLayout(m_handPanel);
    handOuter->setContentsMargins(0, 0, 0, 0);
    m_handScroll = new QScrollArea(m_handPanel);
    m_handScroll->setObjectName(QStringLiteral("roomHandScroll"));
    m_handScroll->setWidgetResizable(true);
    m_handScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_handScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    auto *handContents = new QWidget(m_handScroll);
    m_handLayout = new QHBoxLayout(handContents);
    m_handLayout->setContentsMargins(0, 0, 0, 0);
    m_handLayout->setSpacing(6);
    m_handScroll->setWidget(handContents);
    handOuter->addWidget(m_handScroll);
    m_handPanel->setMinimumHeight(84);
    m_handPanel->setMaximumHeight(130);
    m_contentLayout->addWidget(m_handPanel);
    QScroller::grabGesture(m_handScroll->viewport(), QScroller::TouchGesture);

    m_seatRibbon = new QWidget(this);
    m_seatRibbon->setObjectName(QStringLiteral("roomSeatRibbon"));
    m_seatRibbon->setStyleSheet(QStringLiteral("background: transparent; border: 0;"));
    m_ribbonScroll = new QScrollArea(m_seatRibbon);
    m_ribbonScroll->setWidgetResizable(true);
    m_ribbonScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_ribbonScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_ribbonScroll->setFrameShape(QFrame::NoFrame);
    auto *ribbonContents = new QWidget(m_ribbonScroll);
    m_ribbonLayout = new QHBoxLayout(ribbonContents);
    m_ribbonLayout->setContentsMargins(0, 0, 0, 0);
    m_ribbonLayout->setSpacing(4);
    m_ribbonScroll->setWidget(ribbonContents);
    QScroller::grabGesture(m_ribbonScroll->viewport(), QScroller::TouchGesture);
    m_seatRibbon->hide();
    auto *selectionZones = new QWidget(m_contentWidget);
    auto *selectionLayout = new QHBoxLayout(selectionZones);
    selectionLayout->setContentsMargins(0, 0, 0, 0);
    selectionLayout->setSpacing(6);
    m_seatsScroll = new QScrollArea(selectionZones);
    m_seatsScroll->setWidgetResizable(true);
    m_seatsScroll->setFrameShape(QFrame::NoFrame);
    m_seatsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    auto *seatsContents = new QWidget(m_seatsScroll);
    m_seatsLayout = new QVBoxLayout(seatsContents);
    m_seatsLayout->setContentsMargins(0, 0, 0, 0);
    m_seatsLayout->setSpacing(4);
    m_seatsScroll->setWidget(seatsContents);
    selectionLayout->addWidget(m_seatsScroll, 1);

    m_actionsPanel = new QWidget;
    m_actionsPanel->setStyleSheet(QStringLiteral("background: transparent; border: 0;"));
    m_actionsLayout = new QVBoxLayout(m_actionsPanel);
    m_actionsLayout->setContentsMargins(0, 0, 0, 0);
    m_actionsLayout->setSpacing(4);
    m_reasonLabel = new QLabel(m_actionsPanel);
    m_reasonLabel->setWordWrap(true);
    m_actionsLayout->addWidget(m_reasonLabel);
    m_arrangeButton = new QPushButton(tr("Open game controls to arrange cards"), m_actionsPanel);
    setTouchSize(m_arrangeButton);
    m_actionsLayout->addWidget(m_arrangeButton);
    connect(m_arrangeButton, &QPushButton::clicked, this, &RoomOverlayHost::controlsRequested);
    m_actionsSpacer = new QWidget(m_actionsPanel);
    m_actionsLayout->addWidget(m_actionsSpacer, 1);
    m_actionsScroll = new QScrollArea(selectionZones);
    m_actionsScroll->setWidgetResizable(true);
    m_actionsScroll->setFrameShape(QFrame::NoFrame);
    m_actionsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_actionsScroll->setWidget(m_actionsPanel);
    selectionLayout->addWidget(m_actionsScroll, 1);
    selectionZones->setMinimumHeight(96);
    m_contentLayout->addWidget(selectionZones, 1);
    m_contentScroll->setWidget(m_contentWidget);
    m_interactionLayout->addWidget(m_contentScroll, 1);

    m_interactionFooter = new QWidget(m_interaction);
    auto *footerLayout = new QHBoxLayout(m_interactionFooter);
    footerLayout->setContentsMargins(0, 0, 0, 0);
    footerLayout->setSpacing(4);
    const auto addActionButton = [this, footerLayout](const QString &text, const QString &kind) {
        auto *button = new QPushButton(text, m_interactionFooter);
        setTouchSize(button);
        button->setObjectName(QStringLiteral("roomAction_") + kind);
        m_footerButtons.append(button);
        bindIntentButton(button, kind, QString(), true);
        footerLayout->addWidget(button, 1);
        return button;
    };
    addActionButton(tr("Confirm"), QStringLiteral("confirm"));
    addActionButton(tr("Cancel"), QStringLiteral("cancel"));
    addActionButton(tr("Finish"), QStringLiteral("finish"));
    m_interactionFooter->setFixedHeight(48);
    m_interactionLayout->addWidget(m_interactionFooter);

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

    m_logPanel = makePanel(this);
    auto *logBox = new QVBoxLayout(m_logPanel);
    auto *logHeader = new QHBoxLayout;
    logHeader->addWidget(new QLabel(tr("Game log"), m_logPanel), 1);
    m_logClose = new QToolButton(m_logPanel);
    m_logClose->setText(QStringLiteral("×")); setTouchSize(m_logClose);
    logHeader->addWidget(m_logClose);
    logBox->addLayout(logHeader);
    connect(m_logClose, &QToolButton::clicked, this, [this] { m_logVisible = false; updateGeometry(); emit layoutPreferencesChanged(); });
    m_logView = new QTextBrowser(m_logPanel);
    m_logView->setObjectName(QStringLiteral("roomLiveLog"));
    logBox->addWidget(m_logView);
    m_logPanel->hide();

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

void RoomOverlayHost::setDocuments(QTextDocument *log, QTextDocument *chat, QLineEdit *draft)
{
    const QPointer<QLineEdit> oldDraft = m_legacyDraft;
    if (oldDraft) {
        disconnect(m_chatDraft, nullptr, oldDraft, nullptr);
        disconnect(oldDraft, nullptr, m_chatDraft, nullptr);
    }
    m_logDocument = log;
    m_chatDocument = chat;
    m_legacyDraft = draft;
    if (log) m_logView->setDocument(log);
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

void RoomOverlayHost::updateFromPresentation(const GameViewState &view, const GameActionModel &actions)
{
    QPointer<QWidget> focused = focusWidget();
    const int handPosition = m_handScroll->horizontalScrollBar()->value();
    const int targetPosition = m_seatsScroll->verticalScrollBar()->value();
    const int optionPosition = m_actionsScroll->verticalScrollBar()->value();
    const int contentPosition = m_contentScroll->verticalScrollBar()->value();
    m_view = view;
    m_actions = actions;
    m_generation = actions.sessionGeneration;
    m_revision = actions.presentationRevision;
    m_requestId = actions.requestId;
    if (view.ready) {
        const auto operating = std::find_if(view.players.cbegin(), view.players.cend(),
            [&view](const GameViewPlayer &player) { return player.name == view.operatingPlayer; });
        const QString hp = operating == view.players.cend() ? QString()
            : tr(" · HP %1/%2").arg(operating->hp).arg(operating->maxHp);
        m_statusLabel->setText(tr("Operating: %1%2 · Current: %3 · Phase: %4 · Draw: %5 · Discard: %6")
            .arg(view.operatingPlayerLabel, hp, view.currentPlayerLabel, view.phaseLabel)
            .arg(view.drawPileCount < 0 ? QStringLiteral("?") : QString::number(view.drawPileCount))
            .arg(view.discardPileCount));
        m_promptLabel->setText(actions.prompt.isEmpty() ? view.prompt : actions.prompt);
    } else {
        m_statusLabel->setText(tr("Synchronizing game state…"));
        m_promptLabel->clear();
    }
    QLayoutItem *item = nullptr;
    while ((item = m_handLayout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->hide();
        delete item;
    }
    QList<GameActionEntry> shownCards = view.ready ? actions.cards : QList<GameActionEntry>();
    bool passiveCards = false;
    if (shownCards.isEmpty() && view.ready) {
        passiveCards = true;
        for (const GameViewPlayer &player : view.players) {
            if (player.name != view.operatingPlayer) continue;
            for (const GameViewCard &card : player.hand)
                shownCards.append({QString::number(card.id), card.label, false, false, QString()});
            for (const GameViewCard &card : player.equipment)
                shownCards.append({QStringLiteral("equip:") + QString::number(card.id), card.label, false, false, QString()});
            break;
        }
    }
    for (const GameActionEntry &entry : shownCards) {
        QToolButton *button = entryButton(QStringLiteral("card:") + entry.id, entry.label,
            QStringLiteral("card"), entry.id, entry.selected, !passiveCards && entry.enabled,
            entry.selectedVotes, entry.maxVotes);
        button->setMinimumSize(72, 48);
        button->setMaximumWidth(120);
        button->setToolTip(entry.label);
        button->setAccessibleName(entry.label);
        m_handLayout->addWidget(button);
    }

    for (auto it = m_entryButtons.cbegin(); it != m_entryButtons.cend(); ++it) {
        if (it.key().startsWith(QStringLiteral("option:")) || it.key().startsWith(QStringLiteral("skill:")))
            it.value()->hide();
    }
    while ((item = m_actionsLayout->takeAt(0)) != nullptr) delete item;
    m_actionsLayout->addWidget(m_reasonLabel);
    putEntryButtons(m_actionsLayout, QStringLiteral("option"), view.ready ? actions.actions : QList<GameActionEntry>());
    putEntryButtons(m_actionsLayout, QStringLiteral("skill"), view.ready ? actions.skills : QList<GameActionEntry>());
    m_actionsLayout->addWidget(m_arrangeButton);
    m_reasonLabel->setText(actions.unsupportedReason);
    m_reasonLabel->setVisible(!actions.unsupportedReason.isEmpty());
    m_arrangeButton->setVisible(actions.arrangingCards);
    m_actionsLayout->addWidget(m_actionsSpacer, 1);

    while ((item = m_seatsLayout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->hide();
        delete item;
    }
    for (const GameViewPlayer &player : view.players) {
        if (!view.ready) break;
        QWidget *row = m_playerRows.value(player.name, nullptr);
        QToolButton *target = nullptr;
        QToolButton *details = nullptr;
        QToolButton *increment = nullptr;
        QToolButton *decrement = nullptr;
        QLabel *votes = nullptr;
        if (!row) {
            row = new QWidget(m_interaction);
            row->setProperty("playerName", player.name);
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            target = new QToolButton(row);
            target->setObjectName(QStringLiteral("roomSeatTarget_") + player.name);
            target->setToolButtonStyle(Qt::ToolButtonTextOnly);
            target->setCheckable(true);
            setTouchSize(target);
            rowLayout->addWidget(target, 1);
            increment = new QToolButton(row);
            increment->setObjectName(QStringLiteral("roomSeatVoteAdd"));
            increment->setText(QStringLiteral("+")); setTouchSize(increment);
            rowLayout->addWidget(increment);
            decrement = new QToolButton(row);
            decrement->setObjectName(QStringLiteral("roomSeatVoteRemove"));
            decrement->setText(QStringLiteral("−")); setTouchSize(decrement);
            rowLayout->addWidget(decrement);
            votes = new QLabel(row);
            votes->setObjectName(QStringLiteral("roomSeatVoteCount"));
            votes->setMinimumWidth(42);
            rowLayout->addWidget(votes);
            details = new QToolButton(row);
            details->setObjectName(QStringLiteral("roomSeatDetails"));
            details->setText(tr("Details"));
            setTouchSize(details);
            rowLayout->addWidget(details);
            m_playerRows.insert(player.name, row);
            bindIntentButton(target, QStringLiteral("player"), player.name);
            connect(details, &QToolButton::clicked, this, [this, playerName = player.name] { inspectPlayer(playerName); });
            bindIntentButton(decrement, QStringLiteral("player-remove-vote"), player.name, false);
            bindIntentButton(increment, QStringLiteral("player-add-vote"), player.name, true);
        } else {
            target = row->findChild<QToolButton *>(QStringLiteral("roomSeatTarget_") + player.name);
            increment = row->findChild<QToolButton *>(QStringLiteral("roomSeatVoteAdd"));
            decrement = row->findChild<QToolButton *>(QStringLiteral("roomSeatVoteRemove"));
            votes = row->findChild<QLabel *>(QStringLiteral("roomSeatVoteCount"));
            details = row->findChild<QToolButton *>(QStringLiteral("roomSeatDetails"));
        }
        if (!target || !details || !increment || !decrement || !votes) continue;
        target->setText(player.label);
        target->setToolTip(player.label);
        target->setAccessibleName(player.label);
        const auto action = std::find_if(actions.players.cbegin(), actions.players.cend(),
            [&player](const GameActionEntry &entry) { return entry.id == player.name; });
        const bool legal = action != actions.players.cend() && action->enabled;
        target->setEnabled(legal);
        target->setChecked(action != actions.players.cend() && action->selected);
        const int selectedVotes = action == actions.players.cend() ? 0 : action->selectedVotes;
        const int maxVotes = action == actions.players.cend() ? 0 : action->maxVotes;
        votes->setText(QString::number(selectedVotes) + QLatin1Char('/') + QString::number(maxVotes));
        const bool multiVote = maxVotes > 1;
        votes->setVisible(multiVote);
        increment->setVisible(multiVote);
        decrement->setVisible(multiVote);
        increment->setEnabled(legal && selectedVotes < maxVotes);
        decrement->setEnabled(legal && selectedVotes > 0);
        m_seatsLayout->addWidget(row);
        row->show();
    }
    while ((item = m_ribbonLayout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->hide();
        delete item;
    }
    int operatingIndex = -1;
    for (int index = 0; index < view.players.size(); ++index)
        if (view.players.at(index).name == view.operatingPlayer) { operatingIndex = index; break; }
    for (int offset = 1; view.ready && offset < view.players.size(); ++offset) {
        const int index = (operatingIndex + offset + view.players.size()) % view.players.size();
        const GameViewPlayer &player = view.players.at(index);
        if (player.name == view.operatingPlayer) continue;
        QWidget *button = m_ribbonButtons.value(player.name, nullptr);
        if (!button) {
            button = new QWidget(m_seatRibbon);
            auto *row = new QHBoxLayout(button);
            row->setContentsMargins(0, 0, 0, 0);
            auto *label = new QToolButton(button);
            label->setObjectName(QStringLiteral("roomRibbonPlayer_") + player.name);
            label->setToolButtonStyle(Qt::ToolButtonTextOnly);
            label->setCheckable(true);
            setTouchSize(label);
            auto *details = new QToolButton(button);
            details->setObjectName(QStringLiteral("roomRibbonDetails"));
            details->setText(tr("Details"));
            setTouchSize(details);
            row->addWidget(label);
            row->addWidget(details);
            m_ribbonButtons.insert(player.name, button);
            bindIntentButton(label, QStringLiteral("player"), player.name);
            connect(details, &QToolButton::clicked, this, [this, playerName = player.name] { inspectPlayer(playerName); });
        }
        auto *label = button->findChild<QToolButton *>(QStringLiteral("roomRibbonPlayer_") + player.name);
        const auto targetEntry = std::find_if(actions.players.cbegin(), actions.players.cend(),
            [&player](const GameActionEntry &entry) { return entry.id == player.name; });
        if (label) {
            label->setText(tr("Seat %1 · HP %2/%3 · Hand %4 · %5")
                .arg(player.seat).arg(player.hp).arg(player.maxHp).arg(player.handCount).arg(player.label));
            label->setToolTip(label->text());
            label->setAccessibleName(label->text());
            label->setEnabled(targetEntry != actions.players.cend() && targetEntry->enabled);
            label->setChecked(targetEntry != actions.players.cend() && targetEntry->selected);
        }
        m_ribbonLayout->addWidget(button);
        button->show();
    }
    const auto setEnabledByName = [this](const QString &name, bool enabled) {
        if (auto *button = m_interaction->findChild<QPushButton *>(name)) button->setEnabled(enabled);
    };
    setEnabledByName(QStringLiteral("roomAction_confirm"), view.ready && actions.canConfirm);
    setEnabledByName(QStringLiteral("roomAction_cancel"), view.ready && actions.canCancel);
    setEnabledByName(QStringLiteral("roomAction_finish"), view.ready && actions.canFinish);
    updateInspector();
    updateGeometry();
    m_handScroll->horizontalScrollBar()->setValue(handPosition);
    m_seatsScroll->verticalScrollBar()->setValue(targetPosition);
    m_actionsScroll->verticalScrollBar()->setValue(optionPosition);
    m_contentScroll->verticalScrollBar()->setValue(contentPosition);
    if (focused && focused->isVisible() && focused->isEnabled()) focused->setFocus();
}

void RoomOverlayHost::putEntryButtons(QVBoxLayout *layout, const QString &kind,
                                      const QList<GameActionEntry> &entries)
{
    for (const GameActionEntry &entry : entries) {
        const QString key = kind + QLatin1Char(':') + entry.id;
        auto *button = entryButton(key, entry.label, kind, entry.id, entry.selected,
                                   entry.enabled, entry.selectedVotes, entry.maxVotes);
        layout->addWidget(button);
    }
}

QToolButton *RoomOverlayHost::entryButton(const QString &key, const QString &label,
                                          const QString &kind, const QString &id, bool selected,
                                          bool enabled, int votes, int maxVotes)
{
    QToolButton *button = m_entryButtons.value(key, nullptr);
    if (!button) {
        button = new QToolButton(kind == QLatin1String("card") ? m_handPanel : m_actionsPanel);
        button->setObjectName(QStringLiteral("roomIntent_") + key);
        button->setCheckable(true);
        setTouchSize(button);
        m_entryButtons.insert(key, button);
        bindIntentButton(button, kind, id);
    }
    button->setText(maxVotes > 1 ? label + QStringLiteral("  %1/%2").arg(votes).arg(maxVotes) : label);
    button->setToolTip(label);
    button->setAccessibleName(label);
    button->setChecked(selected);
    button->setEnabled(enabled);
    button->setProperty("intentKind", kind);
    button->setProperty("intentId", id);
    button->show();
    return button;
}

void RoomOverlayHost::submit(const QString &kind, const QString &id, bool selected)
{
    submitCaptured(kind, id, selected, m_generation, m_revision, m_requestId);
}

void RoomOverlayHost::submitCaptured(const QString &kind, const QString &id, bool selected,
                                     quint64 generation, quint64 revision, quint64 requestId)
{
    if (m_presentation)
        m_presentation->submitIntent(kind, id, selected, generation, revision, requestId);
}

void RoomOverlayHost::bindIntentButton(QAbstractButton *button, const QString &kind,
                                       const QString &id, bool selectedOnClick)
{
    if (!button) return;
    button->setProperty("intentKind", kind);
    button->setProperty("intentId", id);
    button->setProperty("intentSelectedOnClick", selectedOnClick);
    connect(button, &QAbstractButton::pressed, this, [this, button] {
        button->setProperty("pressGeneration", QVariant::fromValue<qulonglong>(m_generation));
        button->setProperty("pressRevision", QVariant::fromValue<qulonglong>(m_revision));
        button->setProperty("pressRequestId", QVariant::fromValue<qulonglong>(m_requestId));
    });
    connect(button, &QAbstractButton::clicked, this, [this, button] {
        const bool selected = button->isCheckable()
            ? button->isChecked() : button->property("intentSelectedOnClick").toBool();
        if (button->property("intentKind").toString() == QLatin1String("card"))
            m_handScroll->ensureWidgetVisible(button);
        submitCaptured(button->property("intentKind").toString(),
                       button->property("intentId").toString(), selected,
                       button->property("pressGeneration").toULongLong(),
                       button->property("pressRevision").toULongLong(),
                       button->property("pressRequestId").toULongLong());
    });
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
    m_handedness = value;
    Config.setValue(QStringLiteral("UI/RoomHandedness"), static_cast<int>(value));
    emit layoutPreferencesChanged();
}

void RoomOverlayHost::updateGeometry()
{
    if (!m_launcher) return;
    QRect pane = widgetRect(m_layout.mainRect);
    if (pane.isEmpty()) pane = QRect(0, 0, width(), height());
    const QRect header = widgetRect(m_layout.headerRect);
    m_launcher->setGeometry(pane.left() + 8,
        header.isEmpty() ? pane.top() + 8 : header.top() + (header.height() - 48) / 2, 48, 48);
    const bool active = m_responsiveEnabled && m_layout.valid;
    m_interaction->setVisible(active);
    const QRect interaction = widgetRect(m_layout.interactionRect);
    m_interaction->setGeometry(interaction);
    const QRect safe = widgetRect(m_layout.safeInteractionRect);
    const int availableFooterWidth = qMax(0, interaction.width() - 16);
    const int desiredFooterWidth = m_handedness == RoomLayoutEngine::Handedness::None
        || safe.isEmpty() ? availableFooterWidth : safe.width();
    const int minimumFooterWidth = m_interactionFooter->layout()->minimumSize().width();
    m_interactionFooter->setFixedWidth(qBound(qMin(availableFooterWidth, minimumFooterWidth),
                                              desiredFooterWidth, availableFooterWidth));
    m_interactionFooter->setFixedHeight(48);
    m_interactionLayout->setAlignment(m_interactionFooter,
        m_handedness == RoomLayoutEngine::Handedness::Left ? Qt::AlignLeft
        : m_handedness == RoomLayoutEngine::Handedness::Right ? Qt::AlignRight : Qt::AlignHCenter);
    const QRect inspectorRect = widgetRect(m_layout.inspectorRect);
    const bool permanentSplit = !inspectorRect.isEmpty()
        && (m_layout.profile == RoomLayoutEngine::Profile::ExpandedSplit
            || m_layout.profile == RoomLayoutEngine::Profile::Book)
        && !widgetRect(m_layout.mainRect).intersects(inspectorRect);
    const bool needsLiveProjection = m_responsiveEnabled || m_inspectorRequested || permanentSplit;
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
    const QRect logRect = widgetRect(m_layout.logRect);
    const bool twoPanels = m_logVisible && m_chatVisible;
    const int fallbackWidth = qMax(0, qMin(360, (pane.width() - 24) / (twoPanels ? 2 : 1)));
    const int fallbackHeight = qMax(0, qMin(pane.height() / 2, pane.height() - 16));
    const int fallbackTop = pane.top() + qMax(8, pane.height() / 4);
    const QRect logFallback(pane.left() + 8, fallbackTop, fallbackWidth, fallbackHeight);
    m_logPanel->setGeometry(logRect.isEmpty() ? logFallback : logRect);
    const QRect chatRect = widgetRect(m_layout.chatRect);
    const QRect chatFallback(m_logVisible ? pane.right() - fallbackWidth - 8 : pane.left() + 8,
                             fallbackTop, fallbackWidth, fallbackHeight);
    m_chatPanel->setGeometry(chatRect.isEmpty() ? chatFallback : chatRect);
    m_inspector->setVisible(m_inspectorRequested || !inspectorRect.isEmpty());
    if (auto *closeInspector = m_inspector->findChild<QToolButton *>(QStringLiteral("roomInspectorClose")))
        closeInspector->setEnabled(!permanentSplit);
    const bool ribbon = active && m_layout.seatPresentation == RoomLayoutEngine::SeatPresentation::Ribbon
        && !widgetRect(m_layout.seatsRect).isEmpty();
    m_seatRibbon->setVisible(ribbon);
    if (ribbon) {
        const QRect seats = widgetRect(m_layout.seatsRect);
        m_seatRibbon->setGeometry(seats);
        m_ribbonScroll->setGeometry(0, qMax(0, seats.height() - 56), seats.width(), qMin(52, seats.height()));
    }
    m_logPanel->setVisible(m_logVisible);
    m_chatPanel->setVisible(m_chatVisible);
    updateMask();
}

void RoomOverlayHost::updateMask()
{
    QRegion region(m_launcher->geometry());
    if (m_interaction->isVisible()) region += m_interaction->geometry();
    if (m_seatRibbon->isVisible())
        region += m_ribbonScroll->geometry().translated(m_seatRibbon->geometry().topLeft());
    if (m_inspector->isVisible()) region += m_inspector->geometry();
    if (m_logPanel->isVisible()) region += m_logPanel->geometry();
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
