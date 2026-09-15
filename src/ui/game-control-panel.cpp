#include "game-control-panel.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QEvent>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#ifdef Q_OS_ANDROID
#include <QScrollArea>
#include <QScroller>
#endif

GameControlPanel::GameControlPanel(QWidget *parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("gameControlPanel"));
    setWindowTitle(tr("Game Controls"));
    setModal(false);
    resize(520, 720);
    auto *layout = new QVBoxLayout(this);
#ifdef Q_OS_ANDROID
    // Keep every action reachable on a small screen, including when a skill
    // adds another list. Focused widgets scroll into view for keyboard users.
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto *content = new QWidget(scroll);
    m_contentLayout = new QVBoxLayout(content);
    scroll->setWidget(content);
    layout->addWidget(scroll);
    QScroller::grabGesture(scroll->viewport(), QScroller::TouchGesture);
    connect(qApp, &QApplication::focusChanged, this, [scroll, content](QWidget *, QWidget *focused) {
        if (focused && content->isAncestorOf(focused)) scroll->ensureWidgetVisible(focused);
    });
#else
    m_contentLayout = layout;
#endif
    m_prompt = new QLabel(this);
    m_prompt->setObjectName(QStringLiteral("interactionPrompt"));
    m_prompt->setTextFormat(Qt::PlainText);
    m_prompt->setWordWrap(true);
    m_prompt->setAccessibleName(tr("Current prompt"));
    m_prompt->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
    m_prompt->setFocusPolicy(Qt::StrongFocus);
    m_contentLayout->addWidget(m_prompt);
    m_reason = new QLabel(this);
    m_reason->setTextFormat(Qt::PlainText);
    m_reason->setWordWrap(true);
    m_contentLayout->addWidget(m_reason);
    m_actions = makeList(tr("Actions and options"), QStringLiteral("option"));
    m_skills = makeList(tr("Skills"), QStringLiteral("skill"));
    m_cards = makeList(tr("Cards"), QStringLiteral("card"));
    m_players = makeList(tr("Players and targets"), QStringLiteral("player"));
    m_orderGroup = new QGroupBox(tr("Arrange cards (draw the first card in each list first)"), this);
    auto *orderLayout = new QVBoxLayout(m_orderGroup);
    m_topCards = makeList(tr("Top of deck"), QStringLiteral("top"), orderLayout, false);
    m_bottomCards = makeList(tr("Bottom of deck"), QStringLiteral("bottom"), orderLayout, false);
    auto *orderButtons = new QDialogButtonBox(m_orderGroup);
    m_earlier = orderButtons->addButton(tr("Move earlier (&U)"), QDialogButtonBox::ActionRole);
    m_later = orderButtons->addButton(tr("Move later (&D)"), QDialogButtonBox::ActionRole);
    m_toTop = orderButtons->addButton(tr("Move to top (&T)"), QDialogButtonBox::ActionRole);
    m_toBottom = orderButtons->addButton(tr("Move to bottom (&B)"), QDialogButtonBox::ActionRole);
    m_earlier->setObjectName(QStringLiteral("orderEarlier"));
    m_later->setObjectName(QStringLiteral("orderLater"));
    m_toTop->setObjectName(QStringLiteral("orderToTop"));
    m_toBottom->setObjectName(QStringLiteral("orderToBottom"));
    for (QAbstractButton *button : orderButtons->buttons())
        if (auto *push = qobject_cast<QPushButton *>(button)) push->setAutoDefault(false);
    connect(m_earlier, &QPushButton::clicked, this, [this]() { moveOrderCard(QStringLiteral("order-earlier")); });
    connect(m_later, &QPushButton::clicked, this, [this]() { moveOrderCard(QStringLiteral("order-later")); });
    connect(m_toTop, &QPushButton::clicked, this, [this]() { moveOrderCard(QStringLiteral("order-top")); });
    connect(m_toBottom, &QPushButton::clicked, this, [this]() { moveOrderCard(QStringLiteral("order-bottom")); });
    orderLayout->addWidget(orderButtons);
    m_contentLayout->addWidget(m_orderGroup);
    auto *buttons = new QDialogButtonBox(this);
    m_confirm = buttons->addButton(tr("Confirm (&C)"), QDialogButtonBox::ActionRole);
    m_cancel = buttons->addButton(tr("Cancel selection or response"), QDialogButtonBox::ActionRole);
    m_finish = buttons->addButton(tr("Finish play phase"), QDialogButtonBox::ActionRole);
    m_confirm->setObjectName(QStringLiteral("confirmAction"));
    m_cancel->setObjectName(QStringLiteral("cancelAction"));
    auto *close = buttons->addButton(tr("Close panel"), QDialogButtonBox::RejectRole);
    for (QAbstractButton *button : buttons->buttons())
        if (auto *push = qobject_cast<QPushButton *>(button)) push->setAutoDefault(false);
    connect(m_confirm, &QPushButton::clicked, this, [this]() { sendIntent(QStringLiteral("confirm")); });
    connect(m_cancel, &QPushButton::clicked, this, [this]() { sendIntent(QStringLiteral("cancel")); });
    connect(m_finish, &QPushButton::clicked, this, [this]() { sendIntent(QStringLiteral("finish")); });
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(this, &QDialog::finished, this, [this]() {
        // Native dialog deactivation otherwise overwrites focus restored from
        // finished(). Restore the actual originating window after that event.
        const QPointer<QWidget> returnFocus = m_returnFocus;
        if (returnFocus) QTimer::singleShot(0, returnFocus.data(), [returnFocus]() {
            if (!returnFocus || !returnFocus->isVisible() || !returnFocus->isEnabled()) return;
            returnFocus->window()->raise();
            returnFocus->window()->activateWindow();
            returnFocus->setFocus();
        });
    });
#ifdef Q_OS_ANDROID
    for (QDialogButtonBox *box : {orderButtons, buttons}) {
        box->setOrientation(Qt::Vertical);
        for (QAbstractButton *button : box->buttons()) button->setMinimumHeight(48);
    }
#endif
    m_contentLayout->addWidget(buttons);
    setModel(GameActionModel());
}

QListWidget *GameControlPanel::makeList(const QString &title, const QString &kind,
                                      QVBoxLayout *section, bool checkable)
{
    auto *label = new QLabel(title, this);
    auto *list = new QListWidget(this);
    list->setObjectName(kind + QStringLiteral("List"));
    list->setAccessibleName(title);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    label->setBuddy(list);
    m_listLabels.insert(list, label);
    QLayout *target = section ? section : m_contentLayout;
#ifdef Q_OS_ANDROID
    list->setMinimumHeight(144);
    list->setMaximumHeight(192);
    QScroller::grabGesture(list->viewport(), QScroller::TouchGesture);
#endif
    target->addWidget(label);
    target->addWidget(list);
    if (checkable) {
        connect(list, &QListWidget::itemChanged, this, [this, kind](QListWidgetItem *item) {
            sendIntent(kind, item->data(Qt::UserRole).toString(), item->checkState() == Qt::Checked);
        });
    } else {
        list->installEventFilter(this);
        connect(list, &QListWidget::currentRowChanged, this, [this, list]() {
            m_orderList = list;
            updateOrderButtons();
        });
    }
    return list;
}

void GameControlPanel::sendIntent(const QString &kind, const QString &id, bool selected)
{
    emit intentRequested(kind, id, selected, m_model.sessionGeneration,
                         m_model.presentationRevision, m_model.requestId);
}

void GameControlPanel::updateList(QListWidget *list, const QList<GameActionEntry> &entries, bool checkable)
{
    const QSignalBlocker blocker(list);
    const QString focusedId = list->currentItem()
        ? list->currentItem()->data(Qt::UserRole).toString() : QString();
    // Retain actual items where possible so screen readers keep their focus and
    // receive normal item-data changes instead of a replacement accessibility tree.
    for (int row = 0; row < entries.size(); ++row) {
        const GameActionEntry &entry = entries.at(row);
        int found = row;
        while (found < list->count()
               && list->item(found)->data(Qt::UserRole).toString() != entry.id) ++found;
        if (found == list->count()) list->insertItem(row, new QListWidgetItem);
        else if (found != row) list->insertItem(row, list->takeItem(found));
        QListWidgetItem *item = list->item(row);
        const QString text = entry.reason.isEmpty() ? entry.label
            : tr("%1; %2").arg(entry.label, entry.reason);
        item->setData(Qt::UserRole, entry.id);
        item->setText(text);
        item->setData(Qt::AccessibleTextRole, text);
#ifdef Q_OS_ANDROID
        item->setSizeHint(QSize(0, 48));
#endif
        item->setFlags(Qt::ItemIsSelectable | (checkable ? Qt::ItemIsUserCheckable : Qt::NoItemFlags)
                       | (entry.enabled ? Qt::ItemIsEnabled : Qt::NoItemFlags));
        if (checkable) item->setCheckState(entry.selected ? Qt::Checked : Qt::Unchecked);
    }
    while (list->count() > entries.size()) delete list->takeItem(list->count() - 1);
    for (int row = 0; row < list->count(); ++row)
        if (list->item(row)->data(Qt::UserRole).toString() == focusedId) {
            list->setCurrentRow(row);
            return;
        }
    if (list->currentRow() < 0 && list->count()) list->setCurrentRow(0);
}

void GameControlPanel::setModel(const GameActionModel &model)
{
    QPointer<QWidget> previousFocus = QApplication::focusWidget();
    const bool hadPanelFocus = previousFocus && isAncestorOf(previousFocus);
    const bool changedRevision = m_model.presentationRevision != model.presentationRevision;
    if (m_model.sessionGeneration != model.sessionGeneration || m_model.requestId != model.requestId) {
        m_pendingOrderCard.clear();
        m_orderList = nullptr;
    }
    m_model = model;
    m_prompt->setText(model.prompt);
    // A QLabel's explicit accessible name replaces its text; include the
    // actual prompt so focusing it exposes disclosed read-only cards as well.
    m_prompt->setAccessibleName(tr("Current prompt: %1").arg(model.prompt));
    m_reason->setText(model.unsupportedReason);
    updateList(m_actions, model.actions);
    updateList(m_skills, model.skills);
    updateList(m_cards, model.cards);
    updateList(m_players, model.players);
    updateList(m_topCards, model.topCards, false);
    updateList(m_bottomCards, model.bottomCards, false);
    // An empty pile cannot supply an active card. Do not let Tab enter it and
    // disable all move buttons for the card just selected in the other pile.
    for (QListWidget *list : {m_topCards, m_bottomCards})
        list->setFocusPolicy(list->count() ? Qt::StrongFocus : Qt::NoFocus);
    for (QListWidget *list : {m_actions, m_skills, m_cards, m_players}) {
        list->setVisible(list->count() > 0);
        m_listLabels.value(list)->setVisible(list->count() > 0);
    }
    m_orderGroup->setVisible(model.arrangingCards);
    // Follow a moved card across piles while leaving keyboard focus on the
    // activated button, allowing repeated moves without returning to the list.
    if (changedRevision && !m_pendingOrderCard.isEmpty()) {
        for (QListWidget *list : {m_topCards, m_bottomCards}) {
            for (int row = 0; row < list->count(); ++row)
                if (list->item(row)->data(Qt::UserRole).toString() == m_pendingOrderCard) {
                    const QSignalBlocker blocker(list);
                    list->setCurrentRow(row);
                    m_orderList = list;
                }
        }
        m_pendingOrderCard.clear();
    }
    if (!m_orderList || !m_orderList->currentItem())
        m_orderList = m_topCards->count() ? m_topCards : m_bottomCards;
    updateOrderButtons();
    m_confirm->setEnabled(model.supported && model.canConfirm);
    m_cancel->setEnabled(model.supported && model.canCancel);
    m_finish->setEnabled(model.supported && model.canFinish);
    if (hadPanelFocus && (!previousFocus || !previousFocus->isVisible() || !previousFocus->isEnabled()))
        focusPrimaryControl();
}

bool GameControlPanel::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::FocusIn && (object == m_topCards || object == m_bottomCards)) {
        m_orderList = static_cast<QListWidget *>(object);
        updateOrderButtons();
    }
    return QDialog::eventFilter(object, event);
}

void GameControlPanel::updateOrderButtons()
{
    const bool active = m_model.supported && m_model.arrangingCards && m_orderList
        && m_orderList->currentItem() && m_orderList->currentItem()->flags().testFlag(Qt::ItemIsEnabled);
    m_earlier->setEnabled(active && m_orderList->currentRow() > 0);
    m_later->setEnabled(active && m_orderList->currentRow() + 1 < m_orderList->count());
    m_toTop->setEnabled(active && m_orderList == m_bottomCards && m_model.canMoveToTop);
    m_toBottom->setEnabled(active && m_orderList == m_topCards && m_model.canMoveToBottom);
}

void GameControlPanel::moveOrderCard(const QString &kind)
{
    if (!m_orderList || !m_orderList->currentItem()) return;
    m_pendingOrderCard = m_orderList->currentItem()->data(Qt::UserRole).toString();
    sendIntent(kind, m_pendingOrderCard);
}

void GameControlPanel::openPanel()
{
    if (!isVisible()) m_returnFocus = QApplication::focusWidget();
#ifdef Q_OS_ANDROID
    showMaximized();
#else
    show();
#endif
    raise();
    activateWindow();
    focusPrimaryControl();
}

void GameControlPanel::focusPrimaryControl()
{
    if (m_model.arrangingCards) m_orderList->setFocus();
    else if (m_cards->count()) m_cards->setFocus();
    else if (m_actions->count()) m_actions->setFocus();
    else if (m_players->count()) m_players->setFocus();
    else if (m_skills->count()) m_skills->setFocus();
    else m_prompt->setFocus();
}

GameTextSnapshotDialog::GameTextSnapshotDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("gameTextSnapshot"));
    setWindowTitle(tr("Game State"));
    setModal(false);
    resize(580, 580);
    auto *layout = new QVBoxLayout(this);
    m_text = new QPlainTextEdit(this);
    m_text->setReadOnly(true);
    m_text->setAccessibleName(tr("Game state snapshot"));
    layout->addWidget(m_text);
    auto *buttons = new QDialogButtonBox(this);
    auto *refresh = buttons->addButton(tr("Refresh"), QDialogButtonBox::ActionRole);
    auto *copy = buttons->addButton(tr("Copy"), QDialogButtonBox::ActionRole);
    auto *close = buttons->addButton(tr("Close"), QDialogButtonBox::RejectRole);
    refresh->setObjectName(QStringLiteral("refreshSnapshot"));
    copy->setObjectName(QStringLiteral("copySnapshot"));
#ifdef Q_OS_ANDROID
    for (QAbstractButton *button : buttons->buttons()) button->setMinimumHeight(48);
#endif
    connect(refresh, &QPushButton::clicked, this, &GameTextSnapshotDialog::refreshRequested);
    connect(copy, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_text->toPlainText());
    });
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(this, &QDialog::finished, this, [this]() {
        if (m_returnFocus && m_returnFocus->isVisible()) m_returnFocus->setFocus();
    });
    layout->addWidget(buttons);
}

void GameTextSnapshotDialog::showSnapshot(const QString &text)
{
    if (!isVisible()) m_returnFocus = QApplication::focusWidget();
    m_text->setPlainText(text);
#ifdef Q_OS_ANDROID
    showMaximized();
#else
    show();
#endif
    raise();
    activateWindow();
    m_text->setFocus();
}
