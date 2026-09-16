#include "desktop-game-presentation.h"

#include "carditem.h"
#include "cardcontainer.h"
#include "choosetriggerorderbox.h"
#include "client.h"
#include "client-core.h"
#include "client-live-session.h"
#include "clientplayer.h"
#include "dashboard.h"
#include "engine.h"
#include "game-control-panel.h"
#include "photo.h"
#include "qsanbutton.h"
#include "roomscene.h"
#include "skill.h"

#include <QAbstractButton>
#include <QApplication>
#include <QJsonDocument>
#include <QMainWindow>
#include <QTextDocumentFragment>
#include <QTextDocument>
#include <QTimer>
#include <algorithm>

void RoomScene::showGameStateSnapshot()
{
#if !defined(QSAN_XP_LEGACY)
    if (!m_gamePresentation) m_gamePresentation = new DesktopGamePresentation(this);
    m_gamePresentation->showSnapshot();
#endif
}

void RoomScene::showGameControlPanel()
{
#if !defined(QSAN_XP_LEGACY)
    if (!m_gamePresentation) m_gamePresentation = new DesktopGamePresentation(this);
    m_gamePresentation->showControls();
#endif
}

namespace {
QString plain(const QString &text)
{
    return QTextDocumentFragment::fromHtml(text).toPlainText().simplified();
}

bool cardInteraction(InteractionType type)
{
    switch (type) {
    case InteractionType::PlayCard:
    case InteractionType::ResponseCard:
    case InteractionType::AskPeach:
    case InteractionType::Nullification:
    case InteractionType::ShowCard:
    case InteractionType::Pindian:
    case InteractionType::DiscardCard:
    case InteractionType::ExchangeCard:
    case InteractionType::ChoosePlayer:
    case InteractionType::SkillYiji:
        return true;
    default: return false;
    }
}
}

DesktopGamePresentation::DesktopGamePresentation(RoomScene *scene)
    : QObject(scene), m_scene(scene), m_client(ClientInstance)
{
    qRegisterMetaType<GameViewState>();
    qRegisterMetaType<GameActionModel>();
    // Queue publication until the existing signal chain has finished updating
    // card eligibility, targets and buttons (including an atomic STATE_SYNC).
    connect(scene, &QGraphicsScene::changed, this, [this]() { scheduleRefresh(); });
    connect(scene->m_guanxingBox, &GuanxingBox::draftChanged, this, [this]() { scheduleRefresh(); });
    connect(scene->card_container, &CardContainer::gongxinDraftChanged, this, [this]() { scheduleRefresh(); });
    connect(scene->m_chooseTriggerOrderBox, &ChooseTriggerOrderBox::draftChanged, this, [this]() { scheduleRefresh(); });
    connect(scene->dashboard, &Dashboard::dialogOptionSelectionChanged, this, [this]() { scheduleRefresh(); });
#ifdef Q_OS_ANDROID
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this]() {
        // Fence queued taps even if suspend/resume completes before publication.
        ++m_revision;
        scheduleRefresh();
    });
#endif
    if (!m_client) return;
    connect(m_client, &Client::status_changed, this, [this]() { m_stateDirty = true; scheduleRefresh(); });
    ClientCore *core = m_client->interactionCore();
    connect(core, &ClientCore::requestStarted, this, [this]() { scheduleRefresh(); });
    connect(core, &ClientCore::requestCancelled, this, [this]() { scheduleRefresh(); });
    connect(core, &ClientCore::responseAccepted, this, [this]() { scheduleRefresh(); });
    connect(core, &ClientCore::responseRejected, this, [this]() { scheduleRefresh(); });
    if (auto *session = m_client->liveSession()) {
        connect(session, &ClientLiveSession::protocolMessageReceived, this,
                [this]() { m_stateDirty = true; scheduleRefresh(); });
        connect(session, &ClientLiveSession::connectionChanged, this,
                [this]() { scheduleRefresh(); });
        connect(session, &ClientLiveSession::disconnected, this,
                [this]() { scheduleRefresh(); });
    }
}

DesktopGamePresentation::~DesktopGamePresentation()
{
    // These are parented to the main window, whose lifetime exceeds RoomScene.
    delete m_panel;
    delete m_snapshot;
}

void DesktopGamePresentation::setLiveConsumer(QObject *consumer, bool live)
{
    if (!consumer) return;
    if (live) {
        if (m_liveConsumers.contains(consumer)) return;
        const auto connection = connect(consumer, &QObject::destroyed, this, [this, consumer]() {
            m_liveConsumers.remove(consumer);
            if (m_liveConsumers.isEmpty()) m_forcePresentation = false;
        });
        m_liveConsumers.insert(consumer, connection);
        m_forcePresentation = true;
        requestRefresh();
        return;
    }
    const auto it = m_liveConsumers.find(consumer);
    if (it == m_liveConsumers.end()) return;
    disconnect(it.value());
    m_liveConsumers.erase(it);
    if (m_liveConsumers.isEmpty()) m_forcePresentation = false;
}

void DesktopGamePresentation::requestRefresh()
{
    if (!m_liveConsumers.isEmpty()) m_forcePresentation = true;
    scheduleRefresh();
}

void DesktopGamePresentation::submitIntent(const QString &kind, const QString &id, bool selected,
                                          quint64 generation, quint64 revision, quint64 requestId)
{
    QTimer::singleShot(0, this, [this, kind, id, selected, generation, revision, requestId]() {
        // A checkable widget may have toggled locally before an old press is
        // rejected. Publish the authoritative draft even if its revision is unchanged.
        if (!m_liveConsumers.isEmpty()) m_forcePresentation = true;
        applyIntent(kind, id, selected, generation, revision, requestId);
    });
}

void DesktopGamePresentation::scheduleRefresh()
{
    if ((m_liveConsumers.isEmpty() && (!m_panel || !m_panel->isVisible())) || m_refreshPending) return;
    m_refreshPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_refreshPending = false;
        refresh();
    });
}

QString DesktopGamePresentation::playerLabel(const QString &name) const
{
    if (!m_client) return name;
    const auto data = m_client->interactionCore()->state()->player(name);
    const QString general = data.value(QStringLiteral("general")).toString();
    const QString screen = data.value(QStringLiteral("screen_name"), name).toString();
    return plain(general.isEmpty() ? screen : Sanguosha->translate(general) + QStringLiteral("（") + screen + QStringLiteral("）"));
}

QString DesktopGamePresentation::cardLabel(int id) const
{
    const Card *card = Sanguosha ? Sanguosha->getCard(id) : nullptr;
    if (!card) return tr("Unknown card");
    return plain(tr("%1, %2 %3, ID %4").arg(Sanguosha->translate(card->objectName()),
        Sanguosha->translate(card->getSuitString()), card->getNumberString()).arg(id));
}

QAbstractButton *DesktopGamePresentation::optionButton(const QString &id) const
{
    if (!m_scene->m_choiceDialog) return nullptr;
    const auto buttons = m_scene->m_choiceDialog->findChildren<QAbstractButton *>();
    for (QAbstractButton *button : buttons)
        if (button->objectName() == id) return button;
    return nullptr;
}

GameActionModel DesktopGamePresentation::actionModel() const
{
    GameActionModel model;
    if (!m_client || !m_scene->dashboard) {
        model.unsupportedReason = tr("No game connection is active.");
        return model;
    }
    ClientCore *core = m_client->interactionCore();
    auto *session = m_client->liveSession();
    model.sessionGeneration = session ? session->generation() : 0;
    model.requestId = core->activeRequestId();
#ifdef Q_OS_ANDROID
    if (QGuiApplication::applicationState() != Qt::ApplicationActive) {
        model.unsupportedReason = tr("Return to the game before taking an action.");
        return model;
    }
#endif
    if (m_client->getReplayer()) {
        model.unsupportedReason = tr("Replays only support viewing the game state.");
        return model;
    }
    if (!session || !session->isActive() || session->isStateSyncActive()) {
        model.unsupportedReason = tr("Connection or state synchronization is not complete.");
        return model;
    }
    if (!core->hasActiveRequest()) {
        model.unsupportedReason = tr("No action currently needs a response.");
        return model;
    }
    const auto &request = core->activeRequest();
    model.request = request;
    model.prompt = plain(m_client->getPromptDoc()->toPlainText());
    if (model.prompt.isEmpty()) model.prompt = plain(request.prompt);
    model.minSelection = request.minSelection();
    model.maxSelection = request.maxSelection();
    if (request.deadlineMs > 0 && core->now() >= request.deadlineMs) {
        model.unsupportedReason = tr("This action has timed out.");
        return model;
    }
    if (cardInteraction(request.type) && m_scene->m_presentedDialog != nullptr) {
        Dashboard *dashboard = m_scene->dashboard;
        model.actionContext = QStringLiteral("skill-dialog");
        model.supported = true;
        model.prompt += tr("\nSkill options: %1").arg(plain(Sanguosha->translate(m_scene->m_presentedDialog->objectName())));
        // These are the same options and selection that the graphics presenter
        // prepared; confirming still calls applyPresentedDialogOption().
        for (QGraphicsObject *item : dashboard->m_dialogOptionItems) {
            const QString id = dashboard->m_dialogOptionItemMap.key(item);
            const bool enabled = item->isEnabled() && m_scene->isPresentedDialogOptionEnabled(id);
            model.actions.append({id, plain(Sanguosha->translate(id)), enabled,
                dashboard->selectedDialogOption() == id, enabled ? QString() : tr("This skill option is currently unavailable")});
        }
        model.canConfirm = m_scene->ok_button->isEnabled()
            && m_scene->isPresentedDialogOptionEnabled(dashboard->selectedDialogOption());
        model.canCancel = m_scene->cancel_button->isEnabled(); // Cancel this local skill choice, not the request.
        if (std::none_of(model.actions.cbegin(), model.actions.cend(), [](const GameActionEntry &entry) { return entry.enabled; }))
            model.unsupportedReason = tr("This skill has no available options. Cancel the skill selection.");
        return model;
    }
    const int status = m_client->getStatus() & Client::ClientStatusBasicMask;
    if (const auto *gongxin = request.payloadAs<GongxinInteractionPayload>()) {
        CardContainer *box = m_scene->card_container;
        QList<int> actual;
        for (CardItem *item : box->getItems()) actual << item->getId();
        QList<int> expected = gongxin->visibleCards;
        std::sort(actual.begin(), actual.end());
        std::sort(expected.begin(), expected.end());
        if (status != Client::AskForGongxin || !box->isVisible() || !box->gongxinActive() || actual != expected) {
            model.unsupportedReason = tr("Gongxin cards are still synchronizing. Please wait.");
            return model;
        }
        model.supported = true;
        model.actionContext = QStringLiteral("gongxin");
        model.minSelection = 0;
        model.maxSelection = gongxin->allowHeartOperation ? 1 : 0;
        QStringList visibleLabels;
        for (CardItem *item : box->getItems()) {
            const int id = item->getId();
            // The request's disclosed set is the authority, never target->getHandcards().
            const QString label = id >= 0 ? cardLabel(id) : tr("Unknown card");
            visibleLabels << label;
            const bool enabled = id >= 0 && gongxin->allowHeartOperation
                && gongxin->selectableCards.contains(id) && item->isEnabled();
            const bool selected = enabled && box->selectedGongxinCard() == id;
            model.cards.append({id >= 0 ? QString::number(id) : QStringLiteral("unknown:%1").arg(model.cards.size()),
                label, enabled, selected, enabled ? QString() : tr("View only")});
            model.canConfirm |= selected;
        }
        model.canConfirm |= request.cancelable && box->selectedGongxinCard() < 0;
        model.canCancel = request.cancelable;
        model.prompt = tr("Gongxin: %1\nRevealed cards: %2\n%3")
            .arg(playerLabel(gongxin->targetPlayer), visibleLabels.join(QStringLiteral("、")),
                gongxin->allowHeartOperation ? tr("Select an available card and confirm. Confirm without a selection to take no action.")
                                            : tr("This request is view only. Confirm to finish viewing."));
        return model;
    }
    if (const auto *trigger = request.payloadAs<TriggerOrderInteractionPayload>()) {
        ChooseTriggerOrderBox *box = m_scene->m_chooseTriggerOrderBox;
        if (status != Client::AskForTriggerOrder || !box->isVisible()) {
            model.unsupportedReason = tr("Skill trigger order is still synchronizing. Please wait.");
            return model;
        }
        QStringList expected;
        for (const auto &option : trigger->options) expected << option.responseValue;
        const auto options = box->keyboardOptions();
        QStringList actual;
        for (const auto &option : options) {
            actual << option.id;
        }
        expected.removeDuplicates();
        actual.removeDuplicates();
        expected.sort();
        actual.sort();
        if (actual != expected) {
            model.unsupportedReason = tr("Skill trigger options do not match the current request. Please wait.");
            return model;
        }
        model.supported = true;
        model.actionContext = QStringLiteral("trigger-order");
        model.minSelection = model.maxSelection = 1;
        for (const auto &option : options) {
            model.actions.append({option.id, plain(option.label), option.enabled, option.selected, {}});
            model.canConfirm |= option.enabled && option.selected;
        }
        model.canCancel = request.cancelable && box->canCancelChoice();
        model.prompt = tr("Select the next skill to invoke, then confirm.")
            + (model.canCancel ? tr(" You may cancel this invocation.") : tr(" A selection is required; cancellation is not allowed."));
        if (model.actions.isEmpty() && !model.canCancel)
            model.unsupportedReason = tr("No trigger options are available. Waiting for a request update.");
        return model;
    }
    if (const auto *order = request.payloadAs<RearrangeCardsInteractionPayload>()) {
        GuanxingBox *box = m_scene->m_guanxingBox;
        if (request.type != InteractionType::SkillGuanxing || status != Client::AskForGuanxing
            || order->mirrored || !box->editable()) {
            model.unsupportedReason = tr("The current Guanxing piles cannot be edited.");
            return model;
        }
        const QList<int> top = box->topCards(), bottom = box->bottomCards();
        QList<int> actual = top + bottom, expected = order->cardIds;
        std::sort(actual.begin(), actual.end());
        std::sort(expected.begin(), expected.end());
        if (actual != expected) {
            model.unsupportedReason = tr("Guanxing piles are still synchronizing. Please wait.");
            return model;
        }
        model.supported = true;
        model.arrangingCards = true;
        model.actionContext = QStringLiteral("rearrangement");
        model.canMoveToTop = order->mode != RearrangementMode::DownOnly;
        model.canMoveToBottom = order->mode != RearrangementMode::UpOnly;
        for (int i = 0; i < top.size(); ++i)
            model.topCards.append({QString::number(top.at(i)), tr("%1. %2").arg(i + 1).arg(cardLabel(top.at(i))), true, false, {}});
        for (int i = 0; i < bottom.size(); ++i)
            model.bottomCards.append({QString::number(bottom.at(i)), tr("%1. %2").arg(i + 1).arg(cardLabel(bottom.at(i))), true, false, {}});
        model.canConfirm = m_scene->ok_button->isEnabled()
            && top.size() >= order->minTop && top.size() <= order->maxTop
            && bottom.size() >= order->minBottom && bottom.size() <= order->maxBottom
            && (model.canMoveToTop || top.isEmpty()) && (model.canMoveToBottom || bottom.isEmpty());
        model.prompt += tr("\nTop: %1 cards (%2–%3); bottom: %4 cards (%5–%6). Select a card and use the move buttons to arrange it.")
            .arg(top.size()).arg(order->minTop).arg(order->maxTop)
            .arg(bottom.size()).arg(order->minBottom).arg(order->maxBottom);
        if (!model.canConfirm) model.unsupportedReason = tr("Adjust the pile sizes to the allowed range before confirming.");
        return model;
    }
    if (const auto *options = request.payloadAs<OptionInteractionPayload>()) {
        const bool booleanPrompt = status == Client::AskForSkillInvoke;
        const bool choiceVisible = status == Client::ExecDialog && m_scene->m_choiceDialog
            && m_scene->m_choiceDialog->isVisible();
        bool usable = booleanPrompt;
        for (const auto &option : options->options) {
            auto *button = optionButton(option.value);
            usable |= choiceVisible && button != nullptr;
            const bool enabled = option.enabled && (booleanPrompt || (choiceVisible && button && button->isEnabled()));
            const QString label = button ? plain(button->text()) : plain(
                option.value == QLatin1String("yes") ? tr("Yes") : option.value == QLatin1String("no")
                    ? tr("No") : Sanguosha->translate(option.label.isEmpty() ? option.value : option.label));
            model.actions.append({option.value, label, enabled, option.value == m_option,
                enabled ? QString() : tr("This option is currently unavailable")});
            model.canConfirm |= enabled && option.value == m_option;
        }
        model.supported = usable;
        model.canCancel = request.cancelable && (booleanPrompt
            ? m_scene->cancel_button->isEnabled() : choiceVisible);
        if (!usable) model.unsupportedReason = tr("This option requires a dedicated interface. Use the original window.");
        return model;
    }
    if (!cardInteraction(request.type)) {
        model.unsupportedReason = tr("This action (%1) requires a dedicated interface. Use the original window.")
            .arg(interactionTypeName(request.type));
        return model;
    }
    model.supported = true;
    Dashboard *dashboard = m_scene->dashboard;
    model.canConfirm = m_scene->ok_button->isEnabled();
    model.canCancel = m_scene->cancel_button->isEnabled()
        && (request.cancelable || status == Client::Playing);
    model.canFinish = request.type == InteractionType::PlayCard && m_scene->discard_button->isEnabled();
    if (const auto *distribution = request.payloadAs<YijiInteractionPayload>()) {
        if (status != Client::AskForYiji) {
            model.supported = false;
            model.unsupportedReason = tr("Distribution options are still synchronizing. Please wait.");
            return model;
        }
        const Card *pending = dashboard->pendingCard();
        const int count = pending ? pending->getSubcards().size() : 0;
        model.canConfirm = model.canConfirm && count >= distribution->minCards
            && count <= distribution->maxCards && m_scene->selected_targets.size() == 1;
        model.prompt += tr("\nDistribution: %1 selected, %2–%3 allowed, %4 remaining. Select one recipient and confirm.")
            .arg(count).arg(distribution->minCards).arg(distribution->maxCards).arg(distribution->remainingCount);
    }
    // Eligibility and the selection draft remain exactly the ones used by the
    // graphical table, including view-as, equip and expanded-pile candidates.
    if (request.type != InteractionType::ChoosePlayer) {
        for (CardItem *item : dashboard->getHandCards()) {
            if (item->getId() < 0) continue;
            const bool enabled = item->isEnabled() || item->isSelected();
            model.cards.append({QString::number(item->getId()), cardLabel(item->getId()),
                enabled, item->isSelected(), enabled ? QString() : tr("This card cannot currently be selected")});
        }
        for (int slot = 0; slot < S_EQUIP_AREA_LENGTH; ++slot) {
            CardItem *item = dashboard->_m_equipCards[slot];
            if (!item) continue;
            const bool enabled = item->isMarkable() || item->isMarked();
            model.cards.append({QStringLiteral("equip:") + QString::number(item->getId()),
                tr("Equipment: %1").arg(cardLabel(item->getId())), enabled, item->isMarked(),
                enabled ? QString() : tr("This equipment cannot currently be selected")});
        }
    }
    for (PlayerCardContainer *item : m_scene->item2player.keys()) {
        const auto *player = m_scene->item2player.value(item);
        if (!player) continue;
        const bool enabled = item->isSelected()
            || (item->isEnabled() && item->flags().testFlag(QGraphicsItem::ItemIsSelectable));
        GameActionEntry entry{player->objectName(), playerLabel(player->objectName()),
            enabled, item->isSelected(), enabled ? QString() : tr("This player cannot currently be selected")};
        // Dead Hulao players reuse getVotes() for their reform countdown.
        entry.selectedVotes = item->isSelected() ? qMax(item->getVotes(), 1) : 0;
        entry.maxVotes = item->maxVotes();
        model.players.append(entry);
    }
    // Stable seat order, independent of the QMap's graphics-object addresses.
    const QStringList seats = core->state()->playerNames();
    std::stable_sort(model.players.begin(), model.players.end(), [&seats](const GameActionEntry &a, const GameActionEntry &b) {
        return seats.indexOf(a.id) < seats.indexOf(b.id);
    });
    for (QSanSkillButton *button : m_scene->m_skillButtons) {
        if (!button->getViewAsSkill() || !button->isVisibleTo(m_scene->dashboard)) continue;
        const bool dialogNeeded = button->property("gamePresentationNeedsDialog").toBool()
            || button->getSkill()->getDialogInfo().isValid();
        const bool enabled = button->isEnabled();
        QString label = plain(Sanguosha->translate(button->getSkill()->objectName()));
        if (dialogNeeded) label += tr(" (options)");
        model.skills.append({button->objectName(), label, enabled, button->isDown(),
            enabled ? QString() : tr("This skill cannot currently be invoked")});
    }
    return model;
}

void DesktopGamePresentation::refresh()
{
    if (!m_client) return;
    const auto *session = m_client->liveSession();
    const quint64 generation = session ? session->generation() : 0;
    const quint64 request = m_client->interactionCore()->activeRequestId();
    if (request != m_draftRequest || generation != m_draftGeneration) {
        m_option.clear();
        m_draftRequest = request;
        m_draftGeneration = generation;
    }
    GameActionModel next = actionModel();
    GameActionModel previous = m_model;
    previous.presentationRevision = 0;
    bool stateChanged = false;
    if (m_stateDirty && (!session || !session->isStateSyncActive())) {
        const QJsonObject state = m_client->interactionCore()->state()->toJson();
        stateChanged = state != m_lastState;
        m_lastState = state;
        m_stateDirty = false;
    }
    if (next.toJson() != previous.toJson() || stateChanged || m_revision == 0) ++m_revision;
    next.presentationRevision = m_revision;
    m_model = next;
    if (m_panel) m_panel->setModel(m_model);
    if (!m_liveConsumers.isEmpty()) {
        const GameViewState state = viewState();
        const QJsonObject projected = state.toJson();
        if (m_forcePresentation || projected != m_lastPublishedView) {
            m_lastPublishedView = projected;
            m_forcePresentation = false;
            emit presentationChanged(state, m_model);
        }
    }
}

GameViewState DesktopGamePresentation::viewState() const
{
    if (!m_client) { GameViewState empty; empty.ready = false; return empty; }
    const ClientCore *core = m_client->interactionCore();
    GameViewFormatOptions options;
    const auto *operating = m_scene->getDashboardPlayer();
    options.operatingPlayer = operating ? operating->objectName() : core->state()->selfName();
    options.authorizedHandPlayers << core->state()->selfName();
    const ClientPlayer *recipient = m_client->getPlayer(core->state()->selfName());
    if (operating && recipient && recipient->canSeeHandcard(operating))
        options.authorizedHandPlayers << operating->objectName();
    options.translate = [](const QString &key) { return Sanguosha->translate(key); };
    options.cardLabel = [this](int id) { return cardLabel(id); };
    options.playerLabel = [this](const QString &id) { return playerLabel(id); };
    options.phaseLabel = [](const QString &phase) {
        // State uses enum names (NotActive/Play); the shared locale uses lowercase keys.
        const QString normalized = phase.toLower();
        if (normalized.isEmpty() || normalized == QLatin1String("phasenone")) return tr("None");
        const QString key = normalized == QLatin1String("notactive") ? QStringLiteral("tui_name_not_active")
            : normalized == QLatin1String("roundstart") ? QStringLiteral("tui_name_round_start")
            : QStringLiteral("tui_name_phase_") + normalized;
        const QString label = Sanguosha->translate(key);
        return plain(label == key ? phase : label);
    };
    options.distanceLabel = [this](const QString &from, const QString &to) {
        const auto *source = m_client->getPlayer(from);
        const auto *target = m_client->getPlayer(to);
        return source && target && source->isAlive() && target->isAlive()
            ? QString::number(source->distanceTo(target)) : QString();
    };
    const auto *session = m_client->liveSession();
    options.stateReady = (!session || (!session->isStateSyncActive() && session->isActive()));
    auto result = GameViewState::fromState(*core->state(), core->hasActiveRequest() ? &core->activeRequest() : nullptr,
        session ? session->generation() : 0, m_revision, options);
    // The desktop already expands translated prompts with skill-specific context.
    result.prompt = m_model.prompt.isEmpty() ? plain(m_client->getPromptDoc()->toPlainText()) : m_model.prompt;
    return result;
}

void DesktopGamePresentation::showSnapshot()
{
    m_stateDirty = true;
    refresh();
    if (!m_snapshot) {
        m_snapshot = new GameTextSnapshotDialog(m_scene->mainWindow());
        connect(m_snapshot, &GameTextSnapshotDialog::refreshRequested, this, &DesktopGamePresentation::showSnapshot);
    }
    const GameViewState view = viewState();
    QString text = view.ready ? view.toPlainText() : tr("Connection or state synchronization is not complete. Refresh later.");
    if (view.ready) {
        QStringList actions;
        for (const auto &action : m_model.actions) if (action.enabled) actions << action.label;
        for (const auto &skill : m_model.skills) if (skill.enabled) actions << skill.label;
        if (m_model.canConfirm) actions << tr("Confirm");
        if (m_model.canCancel) actions << tr("Cancel selection or response");
        if (m_model.canFinish) actions << tr("Finish play phase");
        text += tr("\nAvailable actions: %1").arg(actions.isEmpty() ? tr("None") : actions.join(QStringLiteral("、")));
        if (!m_model.unsupportedReason.isEmpty()) text += QLatin1Char('\n') + m_model.unsupportedReason;
        m_events.synchronize(*m_client->interactionCore()->state(), view.sessionGeneration);
        const auto recent = m_events.events();
        if (!recent.isEmpty()) {
            text += tr("\nRecent events:");
            for (int i = qMax(0, int(recent.size()) - 10); i < recent.size(); ++i)
                text += QLatin1Char('\n') + plain(recent.at(i).text);
        }
    }
    m_snapshot->showSnapshot(text);
}

void DesktopGamePresentation::showControls()
{
    if (!m_panel) {
        m_panel = new GameControlPanel(m_scene->mainWindow());
        connect(m_panel, &GameControlPanel::intentRequested, this, &DesktopGamePresentation::applyIntent,
                Qt::QueuedConnection);
    }
    refresh();
    m_panel->openPanel();
}

void DesktopGamePresentation::applyIntent(const QString &kind, const QString &id, bool selected,
                                         quint64 generation, quint64 revision, quint64 requestId)
{
    // Re-project immediately before dispatch. An input from an old UI frame may
    // never reach the legacy click handlers, which do not carry request identity.
    refresh();
    if (!m_model.supported || !m_model.isCurrentFor(generation, revision, requestId) || !m_client) return;
    const auto enabledEntry = [&id](const QList<GameActionEntry> &entries) {
        for (const auto &entry : entries) if (entry.id == id) return entry.enabled;
        return false;
    };
    Dashboard *dashboard = m_scene->dashboard;
    if (kind.startsWith(QLatin1String("order-")) && m_model.arrangingCards
        && (enabledEntry(m_model.topCards) || enabledEntry(m_model.bottomCards))) {
        GuanxingBox *box = m_scene->m_guanxingBox;
        const int cardId = id.toInt();
        const auto top = box->topCards(), bottom = box->bottomCards();
        const bool inBottom = bottom.contains(cardId);
        const int index = (inBottom ? bottom : top).indexOf(cardId);
        if (kind == QLatin1String("order-earlier")) box->moveCard(cardId, inBottom, index - 1);
        else if (kind == QLatin1String("order-later")) box->moveCard(cardId, inBottom, index + 1);
        else if (kind == QLatin1String("order-top") && inBottom && m_model.canMoveToTop)
            box->moveCard(cardId, false, top.size());
        else if (kind == QLatin1String("order-bottom") && !inBottom && m_model.canMoveToBottom)
            box->moveCard(cardId, true, bottom.size());
    } else if (kind == QLatin1String("option") && enabledEntry(m_model.actions)) {
        if (m_model.actionContext == QLatin1String("skill-dialog")) {
            if ((dashboard->selectedDialogOption() == id) != selected) dashboard->_onDialogOptionClicked(id);
        } else if (m_model.actionContext == QLatin1String("trigger-order")) {
            m_scene->m_chooseTriggerOrderBox->selectChoice(id, selected);
        } else m_option = selected ? id : QString();
    } else if (kind == QLatin1String("card") && enabledEntry(m_model.cards)) {
        if (m_model.actionContext == QLatin1String("gongxin")) {
            m_scene->card_container->selectGongxinCard(id.toInt(), selected);
        } else if (id.startsWith(QLatin1String("equip:"))) {
            const int cardId = id.mid(6).toInt();
            for (int slot = 0; slot < S_EQUIP_AREA_LENGTH; ++slot) {
                CardItem *item = dashboard->_m_equipCards[slot];
                if (item && item->getId() == cardId && item->isMarked() != selected) item->mark(selected);
            }
        } else {
            for (CardItem *item : dashboard->getHandCards())
                if (item->getId() == id.toInt() && item->isSelected() != selected) {
                    item->clickItem();
                    item->goBack(true);
                    break;
                }
        }
    } else if (kind == QLatin1String("player") && enabledEntry(m_model.players)) {
        for (PlayerCardContainer *item : m_scene->item2player.keys())
            if (m_scene->item2player.value(item)->objectName() == id) item->setSelected(selected);
    } else if (kind == QLatin1String("player-add-vote")
               || kind == QLatin1String("player-remove-vote")) {
        const bool addVote = kind == QLatin1String("player-add-vote");
        for (PlayerCardContainer *item : m_scene->item2player.keys()) {
            const ClientPlayer *player = m_scene->item2player.value(item);
            if (!player || player->objectName() != id) continue;
            const GameActionEntry *entry = nullptr;
            for (const GameActionEntry &candidate : m_model.players)
                if (candidate.id == id) { entry = &candidate; break; }
            if (!entry) break;
            if ((addVote && entry->enabled && entry->selectedVotes < entry->maxVotes)
                || (!addVote && entry->selected && entry->selectedVotes > 0))
                item->changeVotes(addVote ? 1 : -1);
            break;
        }
    } else if (kind == QLatin1String("skill") && enabledEntry(m_model.skills)) {
        for (QSanSkillButton *button : m_scene->m_skillButtons)
            if (button->objectName() == id && button->isDown() != selected) { button->click(); break; }
    } else if (kind == QLatin1String("confirm") && m_model.canConfirm) {
        if (m_model.actionContext == QLatin1String("skill-dialog")) m_scene->doOkButton();
        else if (m_model.actionContext == QLatin1String("trigger-order"))
            m_scene->m_chooseTriggerOrderBox->submitChoice(m_scene->m_chooseTriggerOrderBox->selectedChoice());
        else if (m_model.actionContext == QLatin1String("gongxin"))
            m_scene->card_container->submitGongxin(m_scene->card_container->selectedGongxinCard());
        else if (!m_model.actions.isEmpty()) {
            if ((m_client->getStatus() & Client::ClientStatusBasicMask) == Client::AskForSkillInvoke) {
                if (m_option == QLatin1String("yes")) m_scene->doOkButton();
                else if (m_option == QLatin1String("no")) m_scene->doCancelButton();
            } else if (QAbstractButton *button = optionButton(m_option)) button->click();
        } else m_scene->doOkButton();
    } else if (kind == QLatin1String("cancel") && m_model.canCancel) {
        if (m_model.actionContext == QLatin1String("trigger-order"))
            m_scene->m_chooseTriggerOrderBox->submitChoice(QStringLiteral("cancel"));
        else if (m_model.actionContext == QLatin1String("gongxin")) m_scene->card_container->submitGongxin();
        else m_scene->doCancelButton();
    }
    else if (kind == QLatin1String("finish") && m_model.canFinish) m_scene->doDiscardButton();
    refresh();
}
