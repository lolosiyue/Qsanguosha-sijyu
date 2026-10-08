#include "controller-router.h"

#include "controller-service.h"
#include "input/input-mode-tracker.h"
#include "settings.h"
#include "controller-text-entry.h"
#include "desktop-game-presentation.h"
#include "roomscene.h"
#include "client.h"
#include "client-core.h"
#include "client-live-session.h"
#include "game-view.h"
#include "general-info-card.h"
#include <QApplication>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QDialog>
#include <QFileDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QLabel>
#include <QMenu>
#include <QScrollArea>
#include <QScrollBar>
#include <QListWidget>
#include <QLocalServer>
#include <QLocalSocket>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDateTime>
#include <QTimer>
#include <QDebug>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QPushButton>
#include <QTextDocumentFragment>
#include <cmath>
#include <limits>

namespace {
QString argument(const QString &name)
{
    const QStringList args = qApp->arguments();
    for (int i = 0; i < args.size(); ++i) {
        if (args.at(i).startsWith(name + '=')) return args.at(i).mid(name.size() + 1);
        if (args.at(i) == name && i + 1 < args.size()) return args.at(i + 1);
    }
    return {};
}
bool navigates(ControllerAction a)
{
    return a == ControllerAction::Up || a == ControllerAction::Down
        || a == ControllerAction::Left || a == ControllerAction::Right
        || a == ControllerAction::PreviousGroup || a == ControllerAction::NextGroup
        || a == ControllerAction::PreviousPage || a == ControllerAction::NextPage;
}
bool eligible(QWidget *widget, QWidget *scope)
{
    return widget && widget->isVisibleTo(scope) && widget->isEnabled()
        && widget->focusPolicy() != Qt::NoFocus && (widget == scope || scope->isAncestorOf(widget));
}
}

ControllerRouter::ControllerRouter(ControllerService *service, QObject *parent)
    : QObject(parent), m_service(service)
{
    qRegisterMetaType<ControllerAction>();
    if (qsanBigPictureModeActive())
        InputModeTracker::instance()->setHideCursorInGamepadMode(true);
    // The SDL timer must return before a widget opens a nested dialog loop.
    connect(service, &ControllerService::action, this, [this](ControllerAction action, quint64 epoch, bool repeat) {
        const QPointer<QWidget> scope = widgetScope();
        const QPointer<ClientCore> core = ClientInstance ? ClientInstance->interactionCore() : nullptr;
        const quint64 requestId = core ? core->activeRequestId() : 0;
        QTimer::singleShot(0, this, [this, action, epoch, repeat, scope, core, requestId]() {
            // Simultaneous edges must not answer a successor request or a dialog
            // opened by an earlier edge from the same SDL poll.
            const auto *currentCore = ClientInstance ? ClientInstance->interactionCore() : nullptr;
            if (scope != widgetScope() || core != currentCore
                || (core && requestId != core->activeRequestId())) {
                trace("stale-input-dropped", {{"action", controllerActionName(action)}});
                return;
            }
            dispatch(action, epoch, repeat);
        });
    });
    connect(service, &ControllerService::deviceChanged, this, [this](const QString &name, quint64 epoch) {
        trace(QStringLiteral("device"), {{"name", name}, {"epoch", QString::number(epoch)}});
        if (RoomSceneInstance && RoomSceneInstance->mainWindow())
            RoomSceneInstance->mainWindow()->setProperty("controllerConnected", !name.isEmpty());
    });
    connect(service, &ControllerService::deviceInput, this, [this](const QString &control, int value, quint64 epoch) {
        trace(QStringLiteral("hardware"), {{"control", control}, {"value", value}, {"epoch", QString::number(epoch)}});
    });
    qApp->installEventFilter(this);
    auto *observer = new QTimer(this);
    observer->setInterval(100);
    connect(observer, &QTimer::timeout, this, &ControllerRouter::observe);
    observer->start();
    startDiagnostics();
}

bool ControllerRouter::isControllerOnlyRun()
{
    return !argument(QStringLiteral("--controller-virtual-input")).isEmpty();
}

bool ControllerRouter::hasConnectedController()
{
    const auto *service = qApp->findChild<ControllerService *>();
    return service && service->hasConnectedDevice();
}

void ControllerRouter::sendKey(QWidget *target, int key, Qt::KeyboardModifiers modifiers)
{
    if (!target) return;
    const QPointer<QWidget> guarded = target;
    const QPointer<QWidget> window = target->window();
    const QPointer<QWidget> parentWindow = window && window->parentWidget()
        ? window->parentWidget()->window() : nullptr;
    trace(QStringLiteral("native-key"), {{"key", key}, {"target", target->objectName()}});
    InputModeTracker::SyntheticInputScope synthetic;
    QKeyEvent press(QEvent::KeyPress, key, modifiers);
    QApplication::sendEvent(target, &press);
    // Modal closure may destroy the recipient; never deliver release to its successor.
    if (guarded) {
        QKeyEvent release(QEvent::KeyRelease, key, modifiers);
        QApplication::sendEvent(guarded, &release);
    }
    if (parentWindow) {
        QTimer::singleShot(0, this, [this, window, parentWindow]() {
            // Restore the owner only after this activation closed its child.
            // Do not continuously steal focus when the user leaves the app.
            if ((!window || !window->isVisible()) && parentWindow && parentWindow->isVisible()
                && !QApplication::activeWindow() && !QApplication::activeModalWidget()) {
                parentWindow->activateWindow();
                recoverFocus(parentWindow);
            }
        });
    }
}

QWidget *ControllerRouter::widgetScope() const
{
    if (auto *popup = QApplication::activePopupWidget()) return popup;
    if (auto *modal = QApplication::activeModalWidget()) return modal;
    QWidget *window = QApplication::activeWindow();
    // Some native gameplay dialogs are shown without activating their window.
    // Prefer a visible QDialog over the table; local panels are nonmodal too.
    if (RoomSceneInstance && window == RoomSceneInstance->mainWindow()) {
        for (QWidget *candidate : QApplication::topLevelWidgets()) {
            if (qobject_cast<QDialog *>(candidate) && candidate->isVisible()
                && (candidate->parentWidget() == window || RoomSceneInstance->controllerOwnsDialog(candidate)))
                return candidate;
        }
    }
    return window;
}

void ControllerRouter::recoverFocus(QWidget *scope)
{
    if (!scope) return;
    QWidget *focused = QApplication::focusWidget();
    if (focused != scope && eligible(focused, scope)) return;
    for (QWidget *candidate : scope->findChildren<QWidget *>()) {
        if (!eligible(candidate, scope)) continue;
        if (qobject_cast<QLabel *>(candidate)) continue;
        candidate->setFocus(Qt::OtherFocusReason);
        return;
    }
    scope->setFocus(Qt::OtherFocusReason);
}

void ControllerRouter::moveFocus(QWidget *scope, ControllerAction action)
{
    recoverFocus(scope);
    QWidget *focused = QApplication::focusWidget();
    if (!focused) return;
    if (action == ControllerAction::PreviousGroup || action == ControllerAction::NextGroup) {
        // Item views can consume Tab forever while moving between cells.
        // Traverse Qt's actual widget focus chain to leave the current control.
        QWidget *candidate = focused;
        for (int i = 0; i < 4096; ++i) {
            candidate = action == ControllerAction::PreviousGroup
                ? candidate->previousInFocusChain() : candidate->nextInFocusChain();
            if (!candidate || candidate == focused) break;
            if (!eligible(candidate, scope) || !(candidate->focusPolicy() & Qt::TabFocus)
                || qobject_cast<QLabel *>(candidate) || candidate->isAncestorOf(focused)) continue;
            candidate->setFocus(Qt::OtherFocusReason);
            // Composite controls can redirect focus to the same owner (for
            // example a QSpinBox's private QLineEdit). Keep traversing instead
            // of trapping every shoulder press inside that control.
            if (QApplication::focusWidget() == focused) continue;
            for (auto *scroll : scope->findChildren<QScrollArea *>())
                if (scroll->isAncestorOf(QApplication::focusWidget())) scroll->ensureWidgetVisible(QApplication::focusWidget());
            return;
        }
        return;
    }
    const QPoint origin = focused->mapTo(scope, focused->rect().center());
    QWidget *best = nullptr;
    double bestScore = std::numeric_limits<double>::max();
    for (QWidget *candidate : scope->findChildren<QWidget *>()) {
        if (!eligible(candidate, scope) || candidate == focused || candidate->isAncestorOf(focused)
            || focused->isAncestorOf(candidate) || qobject_cast<QLabel *>(candidate)) continue;
        const QPoint delta = candidate->mapTo(scope, candidate->rect().center()) - origin;
        const bool horizontal = action == ControllerAction::Left || action == ControllerAction::Right;
        const int forward = horizontal ? delta.x() : delta.y();
        const int side = horizontal ? delta.y() : delta.x();
        if ((action == ControllerAction::Left || action == ControllerAction::Up) ? forward >= 0 : forward <= 0) continue;
        const double score = std::abs(forward) + 3.0 * std::abs(side);
        if (score < bestScore) { bestScore = score; best = candidate; }
    }
    if (best) best->setFocus(Qt::OtherFocusReason);
    else sendKey(focused, action == ControllerAction::Left || action == ControllerAction::Up
        ? Qt::Key_Backtab : Qt::Key_Tab);
    if (auto *scroll = qobject_cast<QScrollArea *>(scope)) scroll->ensureWidgetVisible(QApplication::focusWidget());
    for (auto *scroll : scope->findChildren<QScrollArea *>())
        if (scroll->isAncestorOf(QApplication::focusWidget())) scroll->ensureWidgetVisible(QApplication::focusWidget());
}

bool ControllerRouter::routeWidget(QWidget *scope, ControllerAction action)
{
    if (!scope) return false;
    // Qt native file dialogs hide their control tree. Switching to the widget
    // implementation is an explicit controller compatibility requirement.
    if (auto *file = qobject_cast<QFileDialog *>(scope)) file->setOption(QFileDialog::DontUseNativeDialog);
    recoverFocus(scope);
    QWidget *focused = QApplication::focusWidget();
    if (!eligible(focused, scope)) return false;
    if (action == ControllerAction::Recover) return true;
    if (action == ControllerAction::Back) {
        if (RoomSceneInstance && scope == RoomSceneInstance->mainWindow()) {
            for (auto *view : scope->findChildren<QGraphicsView *>())
                if (view->scene() == RoomSceneInstance) { view->setFocus(Qt::OtherFocusReason); return true; }
        }
        const bool local = scope->property("controllerLocalDialog").toBool()
            || scope->objectName() == QLatin1String("gameControlPanel")
            || scope->objectName() == QLatin1String("gameTextSnapshot")
            || scope->objectName() == QLatin1String("controllerTextEntryDialog");
        const auto *core = ClientInstance ? ClientInstance->interactionCore() : nullptr;
        if (!local && core && core->hasActiveRequest() && !core->activeRequest().cancelable) {
            trace("blocked-cancel", {{"scope", scope->objectName()}});
            return true;
        }
        sendKey(focused, Qt::Key_Escape);
        return true;
    }
    if (action == ControllerAction::Menu) {
        // Menu never replaces a current modal choice. NextGroup can still reach
        // every command and standard control in this scope.
        sendKey(focused, Qt::Key_Tab);
        return true;
    }
    if (action == ControllerAction::Activate || action == ControllerAction::Submit) {
        if (action == ControllerAction::Submit) {
            const QString confirmId = scope->objectName() == QLatin1String("gameControlPanel") ? QStringLiteral("confirmAction")
                : scope->objectName() == QLatin1String("controllerTextEntryDialog") ? QStringLiteral("doneTextEntryButton")
                : scope->objectName() == QLatin1String("controllerCustomInteraction") ? QStringLiteral("controllerCustomConfirm") : QString();
            if (!confirmId.isEmpty()) {
                if (auto *button = scope->findChild<QAbstractButton *>(confirmId); button && button->isEnabled()) sendKey(button, Qt::Key_Space);
                return true;
            }
        }
        if (scope == QApplication::activePopupWidget()) sendKey(focused, Qt::Key_Return);
        // OptionButton has a native keyboard path that emits double_clicked for
        // general choices and clicked for direction/order. Preserve that path.
        else if (qobject_cast<QAbstractButton *>(focused)) sendKey(focused, Qt::Key_Space);
        else if (auto *line = qobject_cast<QLineEdit *>(focused); line && !line->isReadOnly()
                 && !qobject_cast<QAbstractSpinBox *>(line->parentWidget())) {
            if (action == ControllerAction::Submit) sendKey(line, Qt::Key_Return);
            else ControllerTextEntry::edit(line, scope);
        } else if (auto *text = qobject_cast<QTextEdit *>(focused); text && !text->isReadOnly())
            ControllerTextEntry::edit(text, scope);
        else if (auto *text = qobject_cast<QPlainTextEdit *>(focused); text && !text->isReadOnly())
            ControllerTextEntry::edit(text, scope);
        else if (qobject_cast<QComboBox *>(focused)) sendKey(focused, Qt::Key_Space);
        else sendKey(focused, action == ControllerAction::Activate ? Qt::Key_Space : Qt::Key_Return);
        return true;
    }
    if (action == ControllerAction::Inspect) {
        GeneralInfoCard::ensureToolTip(focused);
        QString detail = focused->accessibleDescription();
        if (detail.isEmpty()) detail = focused->toolTip();
        if (detail.isEmpty()) detail = focused->accessibleName();
        detail = QTextDocumentFragment::fromHtml(detail).toPlainText();
        auto *dialog = new QDialog(scope);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setObjectName("controllerWidgetDetails");
        dialog->setProperty("controllerLocalDialog", true);
        auto *layout = new QVBoxLayout(dialog);
        auto *text = new QPlainTextEdit(detail, dialog);
        text->setReadOnly(true); text->setTabChangesFocus(true);
        layout->addWidget(text);
        auto *close = new QPushButton(tr("Back"), dialog);
        connect(close, &QPushButton::clicked, dialog, &QDialog::close);
        layout->addWidget(close); dialog->open();
        return true;
    }
    if (action == ControllerAction::PreviousPage || action == ControllerAction::NextPage) {
        sendKey(focused, action == ControllerAction::PreviousPage ? Qt::Key_PageUp : Qt::Key_PageDown);
        return true;
    }
    if (action == ControllerAction::MoveEarlier || action == ControllerAction::MoveLater) {
        sendKey(focused, action == ControllerAction::MoveEarlier ? Qt::Key_Left : Qt::Key_Right, Qt::AltModifier);
        return true;
    }
    if (!navigates(action)) return true;
    if (action == ControllerAction::PreviousGroup || action == ControllerAction::NextGroup) {
        moveFocus(scope, action); return true;
    }
    const int key = action == ControllerAction::Up ? Qt::Key_Up : action == ControllerAction::Down ? Qt::Key_Down
        : action == ControllerAction::Left ? Qt::Key_Left : Qt::Key_Right;
    if (qobject_cast<QAbstractItemView *>(focused) || qobject_cast<QAbstractItemView *>(focused->parentWidget())) {
        // Left/right leave vertical lists so command buttons remain reachable
        // even with only D-pad/South/East. Up/down traverse rows and scroll.
        if (action == ControllerAction::Left || action == ControllerAction::Right) moveFocus(scope, action);
        else sendKey(focused, key);
    } else if (qobject_cast<QComboBox *>(focused) || qobject_cast<QAbstractSpinBox *>(focused)
               || qobject_cast<QAbstractSpinBox *>(focused->parentWidget())
               || qobject_cast<QMenu *>(scope)) sendKey(focused, key);
    else if (auto *text = qobject_cast<QPlainTextEdit *>(focused); text && text->isReadOnly()) {
        if (action == ControllerAction::Up || action == ControllerAction::Down) sendKey(text, key);
        else moveFocus(scope, action);
    } else moveFocus(scope, action);
    return true;
}

void ControllerRouter::dispatch(ControllerAction action, quint64 epoch, bool repeat)
{
    // SDL emits edges, so a held activation cannot cross into a nested dialog.
    // Queued delivery keeps subsequent presses available inside exec() loops.
    if (epoch != m_service->deviceEpoch() || QGuiApplication::applicationState() != Qt::ApplicationActive) return;
    InputModeTracker::instance()->noteGamepadInput();
    InputModeTracker::SyntheticInputScope synthetic;
    const QPointer<QWidget> scope = widgetScope();
    QPointer<RoomScene> room = RoomSceneInstance;
    bool handled = false;
    QString focus;
    const bool table = room && scope == room->mainWindow()
        && !qobject_cast<QLineEdit *>(QApplication::focusWidget());
    if (table && action == ControllerAction::Menu) { room->showControllerMenu(); handled = true; }
    else if (table) {
        auto *presentation = room->gamePresentation();
        const auto model = presentation->currentActions();
        if (model.supported) handled = presentation->handleControllerAction(action);
        if (!handled && ClientInstance && ClientInstance->interactionCore()->hasActiveRequest()) {
            int key = 0; Qt::KeyboardModifiers modifiers = Qt::NoModifier;
            switch (action) {
            // Bespoke one-dimensional boxes keep left/right for candidates;
            // Up reaches explicit commands, including mandatory arrangement
            // confirmation, with only the D-pad and activation/back buttons.
            case ControllerAction::Up: room->showControllerMenu(); handled = true; break;
            case ControllerAction::Down: key = Qt::Key_Down; break;
            case ControllerAction::Left: key = Qt::Key_Left; break;
            case ControllerAction::Right: key = Qt::Key_Right; break;
            case ControllerAction::Activate:
                key = ClientInstance->interactionCore()->activeRequest().type == InteractionType::AmazingGrace
                    || ClientInstance->interactionCore()->activeRequest().type == InteractionType::TriggerOrder
                    || ClientInstance->interactionCore()->activeRequest().type == InteractionType::ChooseCard
                    ? Qt::Key_Return : Qt::Key_Space;
                break;
            case ControllerAction::Submit: key = Qt::Key_Return; break;
            case ControllerAction::Back: key = Qt::Key_Escape; break;
            case ControllerAction::PreviousGroup: room->showControllerMenu(); handled = true; break;
            case ControllerAction::NextGroup: case ControllerAction::Recover: key = Qt::Key_Tab; break;
            case ControllerAction::MoveEarlier: key = Qt::Key_Left; modifiers = Qt::AltModifier; break;
            case ControllerAction::MoveLater: key = Qt::Key_Right; modifiers = Qt::AltModifier; break;
            case ControllerAction::Inspect: room->showGameStateSnapshot(); handled = true; break;
            default: break;
            }
            if (key) {
                QKeyEvent press(QEvent::KeyPress, key, modifiers);
                handled = room->handleNativeKey(&press);
                QKeyEvent release(QEvent::KeyRelease, key, modifiers);
                if (room) room->handleNativeKey(&release);
            }
        }
        if (room) focus = presentation->controllerFocus();
    } else handled = routeWidget(scope, action);
    QWidget *focused = QApplication::focusWidget();
    if (focus.isEmpty() && focused) focus = focused->objectName();
    trace("action", {{"action", controllerActionName(action)}, {"epoch", QString::number(epoch)},
        {"repeat", repeat}, {"handled", handled}, {"scope", scope ? scope->objectName() : QString()}, {"focus", focus}});
}

bool ControllerRouter::eventFilter(QObject *, QEvent *event)
{
    if (isControllerOnlyRun() && event->spontaneous()
        && (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease
            || event->type() == QEvent::Wheel || event->type() == QEvent::TouchBegin
            || event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)) {
        trace("forbidden-input", {{"event_type", int(event->type())}});
        qApp->exit(3); return true;
    }
    return false;
}

void ControllerRouter::trace(const QString &kind, QJsonObject data)
{
    if (!m_trace.isOpen()) return;
    data.insert("kind", kind);
    data.insert("sequence", QString::number(++m_sequence));
    data.insert("time_ms", QString::number(QDateTime::currentMSecsSinceEpoch()));
    if (ClientInstance && ClientInstance->interactionCore()) {
        const auto *core = ClientInstance->interactionCore();
        data.insert("request_id", QString::number(core->activeRequestId()));
        if (ClientInstance->liveSession()) data.insert("generation", QString::number(ClientInstance->liveSession()->generation()));
    }
    m_trace.write(QJsonDocument(data).toJson(QJsonDocument::Compact) + '\n');
    m_trace.flush();
}

void ControllerRouter::observe()
{
    if (RoomSceneInstance && RoomSceneInstance->mainWindow())
        RoomSceneInstance->mainWindow()->setProperty("controllerConnected", m_service->hasConnectedDevice());
    if (!m_trace.isOpen()) return;
    if (ClientInstance && m_observedCore != ClientInstance->interactionCore()) {
        if (m_observedCore) disconnect(m_observedCore, nullptr, this, nullptr);
        m_observedCore = ClientInstance->interactionCore();
        connect(m_observedCore, &ClientCore::requestStarted, this, [this](quint64 id) {
            trace("request-start", {{"id", QString::number(id)}});
        });
        connect(m_observedCore, &ClientCore::responseAccepted, this, [this](quint64 id) {
            trace("core-accepted", {{"id", QString::number(id)}});
        });
        connect(m_observedCore, &ClientCore::responseRejected, this, [this](quint64 id, int reason) {
            trace("core-rejected", {{"id", QString::number(id)}, {"reason", reason}});
        });
        connect(m_observedCore, &ClientCore::requestCancelled, this, [this](quint64 id, int reason) {
            trace("request-cancelled", {{"id", QString::number(id)}, {"reason", reason}});
        });
        connect(ClientInstance, &Client::game_started, this, [this]() { trace("game-start", {}); });
        connect(ClientInstance, &Client::game_over, this, [this]() { trace("game-over", {}); });
        connect(ClientInstance, &Client::standoff, this, [this]() { trace("game-standoff", {}); });
        if (auto *session = ClientInstance->liveSession()) {
            connect(session, &ClientLiveSession::protocolMessageSent, this, [this](const QSanProtocol::ProtocolMessage &message) {
                if (message.type != QSanProtocol::ProtocolMessageType::Reply) return;
                trace("wire-reply", {{"message_id", QString::number(message.messageId)},
                    {"reply_to", QString::number(message.replyTo)}, {"command", message.command},
                    {"payload", QJsonValue::fromVariant(message.payload)}});
            });
            connect(session, &ClientLiveSession::protocolMessageReceived, this, [this](const QSanProtocol::ProtocolMessage &message) {
                trace("wire-received", {{"message_id", QString::number(message.messageId)},
                    {"command", message.command}, {"message_type", int(message.type)},
                    {"payload", QJsonValue::fromVariant(message.payload)}});
            });
        }
    }
    QJsonObject observation;
    observation.insert("application_state", int(QGuiApplication::applicationState()));
    QWidget *scope = widgetScope();
    observation.insert("scope", scope ? scope->objectName() : QString());
    observation.insert("title", scope ? scope->windowTitle() : QString());
    QWidget *focus = QApplication::focusWidget();
    observation.insert("focus", focus ? focus->objectName() : QString());
    QJsonArray controls;
    if (scope) for (QWidget *widget : scope->findChildren<QWidget *>()) {
        if (!eligible(widget, scope)) continue;
        QString label = widget->accessibleName();
        if (auto *button = qobject_cast<QAbstractButton *>(widget)) label = button->text();
        QJsonObject entry{{"id", widget->objectName()}, {"class", widget->metaObject()->className()},
            {"label", label}, {"focused", widget == focus}};
        if (auto *list = qobject_cast<QListWidget *>(widget)) {
            QJsonArray rows;
            for (int i = 0; i < list->count(); ++i) rows.append(QJsonObject{{"label", list->item(i)->text()},
                {"id", list->item(i)->data(Qt::UserRole).toString()}, {"checked", list->item(i)->checkState() == Qt::Checked}});
            entry.insert("rows", rows); entry.insert("current_row", list->currentRow());
        }
        controls.append(entry);
    }
    observation.insert("controls", controls);
    if (ClientInstance && ClientInstance->interactionCore()) {
        const auto *state = ClientInstance->interactionCore()->state();
        observation.insert("self", state->selfName());
        observation.insert("self_state", state->playerValue(state->selfName(), "state").toString());
    }
    if (RoomSceneInstance) {
        auto *presentation = RoomSceneInstance->gamePresentation();
        observation.insert("actions", presentation->currentActions().toJson());
        observation.insert("table_focus", presentation->controllerFocus());
        observation.insert("native_keyboard_ready", RoomSceneInstance->nativeKeyboardAvailable());
        QGraphicsItem *sceneFocus = RoomSceneInstance->focusItem();
        auto *focusObject = dynamic_cast<QGraphicsObject *>(sceneFocus);
        observation.insert("scene_focus", focusObject ? focusObject->objectName() : QString());
        observation.insert("scene_focus_type", sceneFocus ? sceneFocus->type() : -1);
        observation.insert("unsupported_controller_interaction", RoomSceneInstance->mainWindow()->property("controllerUnsupported").toString());
    }
    trace("observation", observation);
}

void ControllerRouter::startDiagnostics()
{
    const QString log = argument("--controller-trace");
    if (!log.isEmpty()) {
        m_trace.setFileName(log);
        if (!m_trace.open(QIODevice::WriteOnly)) qWarning() << m_trace.errorString();
        else m_trace.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
    const QString path = argument("--controller-virtual-input");
    if (path.isEmpty()) return;
    if (!m_service->attachVirtualDevice()) { trace("diagnostic-error", {{"error", "cannot attach virtual gamepad"}}); QTimer::singleShot(0, qApp, [] { qApp->exit(4); }); return; }
    m_input = new QLocalServer(this);
    m_input->setSocketOptions(QLocalServer::UserAccessOption);
    // Never unlink an existing socket belonging to another process.
    if (!m_input->listen(path)) { trace("diagnostic-error", {{"error", m_input->errorString()}}); QTimer::singleShot(0, qApp, [] { qApp->exit(4); }); return; }
    connect(m_input, &QLocalServer::newConnection, this, [this]() {
        while (m_input->hasPendingConnections()) {
            auto *socket = m_input->nextPendingConnection();
            socket->setParent(this);
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
                if (socket->bytesAvailable() > 65536) { socket->abort(); return; }
                while (socket->canReadLine()) {
                    const auto doc = QJsonDocument::fromJson(socket->readLine(4096));
                    const auto command = doc.object();
                    bool ok = false;
                    if (command.contains("button") && command.value("down").isBool())
                        ok = m_service->setVirtualButton(command.value("button").toString(), command.value("down").toBool());
                    else if (command.contains("axis") && command.value("value").isDouble())
                        ok = m_service->setVirtualAxis(command.value("axis").toString(), command.value("value").toInt());
                    else if (command.value("device") == QLatin1String("detach")) { m_service->detachVirtualDevice(); ok = true; }
                    else if (command.value("device") == QLatin1String("attach")) ok = m_service->attachVirtualDevice();
                    socket->write(QJsonDocument(QJsonObject{{"ok", ok}}).toJson(QJsonDocument::Compact) + '\n');
                    trace("virtual-injection", {{"command", command}, {"ok", ok}});
                }
            });
        }
    });
    trace("diagnostic-ready", {{"socket", path}, {"device", "SDL virtual gamepad"}});
}
