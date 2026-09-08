#ifndef QSAN_XP_GUI_ACCEPTANCE_H
#define QSAN_XP_GUI_ACCEPTANCE_H

// Explicit, opt-in XP QA driver. It invokes production UI/controller paths.
#include "mainwindow.h"
#include "client.h"
#include "clientplayer.h"
#include "recorder.h"
#include "protocol.h"
#include "roomscene.h"
#include "settings.h"
#include "runtime-paths.h"
#include "src/ui/testing/network-ui-smoke-responder.h"
#ifdef QSAN_XP_LEGACY
#include "local-server-controller.h"
#endif
#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QInputDialog>
#include <QMessageBox>
#include <QPointer>
#include <QTimer>

class XpGuiAcceptance final : public QObject
{
public:
    static void install(MainWindow *window)
    {
        for (const QString &arg : qApp->arguments()) {
            if (arg.startsWith("--xp-acceptance=")) {
                new XpGuiAcceptance(window, arg.mid(16));
                break;
            }
        }
    }

private:
    explicit XpGuiAcceptance(MainWindow *window, const QString &mode)
        : QObject(window), m_window(window), m_mode(mode)
    {
        m_elapsed.start();
        // closeEvent calls quit(); preserve a failing QA result across that
        // normal shutdown path, including an unexpected/manual early close.
        qApp->setProperty("xpAcceptanceExitCode", 1);
        m_log.setFileName(QSanRuntimePaths::userDataPath("xp-gui-acceptance.jsonl"));
        m_log.open(QIODevice::WriteOnly | QIODevice::Truncate);
        record("gui_created", {{"exe", qApp->applicationFilePath()}, {"mode", mode}});
        qApp->installEventFilter(this);
#ifdef QSAN_XP_LEGACY
        if (m_window->localServerController()) {
            LocalServerController *controller = m_window->localServerController();
            connect(controller, &LocalServerController::replayFinalized, this,
                [this](const QString &path, bool ok, const QString &detail) {
                    m_replayExported = ok;
                    record("replay_export", {{"path", path}, {"success", ok}, {"detail", detail}});
                    if (m_waitingReplay) {
                        if (ok) finish(m_started, "game_complete");
                        else fail("replay_export");
                    }
                });
            connect(controller, &LocalServerController::ready, this, [this]() {
                if (!m_branchRequested && m_mode != "management") return;
                record(m_mode == "management" ? "management_server_ready" : "takeover_controller_ready",
                    {{"generation", m_originalGeneration},
                    {"endpoint", m_originalEndpoint}});
                if (m_mode == "replay-rollback")
                    m_window->rollbackTakeover(QStringLiteral("xp_acceptance_cancel"));
                else if (m_mode == "management") {
                    QAction *action = m_window->findChild<QAction *>("actionBroadcast");
                    if (!action || !action->isEnabled()) { fail("management_action_missing"); return; }
                    record("activate_broadcast_action");
                    action->trigger();
                }
            });
            connect(controller, &LocalServerController::takeoverReady, this, [this, controller]() {
                m_takeoverReady = true;
                record("takeover_ready", {{"generation", controller->generation()},
                    {"endpoint", controller->endpoint()}, {"takeover_session", controller->takeoverSession()}});
                QTimer::singleShot(0, this, [this]() {
                    if (m_mode == "replay-takeover" && !m_takeoverGameSeen
                        && m_window->m_takeoverGameStarted && ClientInstance
                        && ClientInstance->getReplayer() == nullptr) {
                        m_takeoverGameSeen = true;
                        enableTrustee();
                        finish(true, "takeover_live_game");
                    }
                });
            });
            connect(controller, &LocalServerController::stopped, this, [this](bool graceful) {
                m_helperStopped = true;
                m_helperGraceful = graceful;
                record("helper_stopped", {{"graceful", graceful}});
                scheduleRestoreCheck();
                if (m_finishScheduled) {
                    if (!graceful) {
                        m_result = false; m_over = false;
                        qApp->setProperty("xpAcceptanceExitCode", 1);
                        record("FAIL_helper_ungraceful");
                    }
                    QTimer::singleShot(0, this, [this]() {
                        if (m_window) m_window->close();
                        QCoreApplication::exit(m_result ? 0 : 1);
                    });
                }
            });
            connect(controller, &LocalServerController::commandResult, this,
                [this](const QString &, bool accepted, const QJsonObject &body) {
                    if (m_mode != "management" || !m_managementBroadcastAccepted
                        || m_finishScheduled) return;
                    if (accepted && body.value("code").toString() == QStringLiteral("ok")) {
                        record("management_broadcast_ack", {{"ban_ip", "not_exercised"}});
                        finish(true, "management_broadcast_ack");
                    } else {
                        fail("management_broadcast_rejected");
                    }
                });
        }
#endif
        connect(window, &MainWindow::roomSceneCreated, this, [this](RoomScene *scene) {
            record("room_scene_created");
            m_scene = scene;
            if (m_mode == "replay-rollback" && m_branchRequested) {
                m_restoreSceneSeen = true;
                scheduleRestoreCheck();
            }
            new NetworkUiSmokeResponder(scene, 3000, scene);
            Client *client = ClientInstance;
            if (!client) { fail("client_missing"); return; }
            connect(client, &Client::server_reply, this, [this](int command) {
                if (command == QSanProtocol::S_COMMAND_CHOOSE_GENERAL) record("general_selected");
            });
            connect(client, &Client::game_started, this, [this]() {
                QTimer::singleShot(0, this, [this]() { handleGameStarted(); });
            });
            auto over = [this]() {
                if (m_finishScheduled) return;
                if (m_mode.startsWith("replay-") && !m_takeoverGameSeen) {
                    fail("replay_ended_before_acceptance"); return;
                }
                record("GAME_OVER", {{"started", m_started}});
                m_over = true;
#ifdef QSAN_XP_LEGACY
                LocalServerController *controller = m_window->localServerController();
                if (controller && controller->active() && !controller->hostOnly()
                    && Config.value("recorder/autosave", true).toBool() && !m_replayExported) {
                    // Saving the text and exporting snapshots are asynchronous;
                    // do not shut down the owner after a fixed 1.5-second delay.
                    m_waitingReplay = true;
                    QTimer::singleShot(16000, this, [this]() {
                        if (!m_finishScheduled) fail("replay_export_timeout");
                    });
                    return;
                }
#endif
                finish(m_started, "game_complete");
            };
            connect(client, &Client::game_over, this, over);
            connect(client, &Client::standoff, this, over);
            connect(client, &Client::error_message, this, [this](const QString &error) {
                record("client_error", {{"message", error}});
                fail("client_error");
            });
            QTimer *owner = new QTimer(scene);
            owner->setInterval(100);
            connect(owner, &QTimer::timeout, this, [this, owner]() {
                // The process-wide acceptance timeout owns the bound. Starting
                // this poll only after the room scene exists prevents slow
                // optical-media initialization from consuming the AI-fill window.
                if (!ClientInstance || m_started) {
                    owner->stop(); return;
                }
                if (Self && Self->isOwner()) {
                    owner->stop(); record("owner_fill_robots");
                    QMetaObject::invokeMethod(m_scene, "fillRobots", Qt::QueuedConnection);
                }
            });
            owner->start();
        });
        connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
            record("gui_exit", {{"game_complete", m_result && m_over}});
        });
        QTimer::singleShot(600000, this, [this]() { if (!m_finishScheduled) fail("timeout"); });
        QTimer::singleShot(0, this, [this]() { startMode(); });
    }

    QString argument(const QString &name) const
    {
        const QString prefix = name + '=';
        for (const QString &arg : qApp->arguments())
            if (arg.startsWith(prefix)) return arg.mid(prefix.size());
        return QString();
    }

    void startMode()
    {
        if (m_mode == "private" || m_mode == "reconnect") {
            record("activate_singleplayer");
            QMetaObject::invokeMethod(m_window, "startLocalConsoleGame", Qt::QueuedConnection);
        } else if (m_mode == "join" || m_mode == "host-only" || m_mode == "management") {
            QAction *action = m_window->findChild<QAction *>("actionStart_Server");
            if (action && action->isEnabled()) {
                record("activate_server_action"); action->trigger();
            } else fail("server_action_missing");
        } else if (m_mode == "replay-takeover" || m_mode == "replay-rollback") {
            m_replayPath = argument("--xp-replay");
            m_snapshotPath = argument("--xp-snapshot");
            m_seatName = argument("--xp-seat");
            bool ok = false;
            m_pairIndex = argument("--xp-pair-index").toInt(&ok);
            if (!ok) m_pairIndex = 0;
            if (m_replayPath.isEmpty() || m_snapshotPath.isEmpty() || m_seatName.isEmpty()
                || !QFileInfo(m_replayPath).isFile() || !QFileInfo(m_snapshotPath).isFile()) {
                fail("replay_arguments_missing"); return;
            }
            MainWindow::ReplayRestoreState initial;
            initial.path = QFileInfo(m_replayPath).absoluteFilePath();
            initial.pairIndex = m_pairIndex;
            initial.perspective = argument("--xp-perspective");
            initial.wasPaused = qApp->arguments().contains("--xp-paused");
            m_window->reopenReplay(initial);
        } else if (m_mode != "external") {
            fail("unknown_mode");
        }
    }

    void handleGameStarted()
    {
        if (!ClientInstance) { fail("client_missing"); return; }
        record("GAME_STARTED", {{"players", ClientInstance->getPlayers().size()}});
        if (m_mode == "replay-takeover" || m_mode == "replay-rollback") {
            if (!m_branchRequested) {
                Replayer *replayer = ClientInstance->getReplayer();
                if (!replayer || !replayer->isValid()) { fail("replay_not_ready"); return; }
                m_originalPath = replayer->getPath();
                m_originalPairIndex = replayer->getCurrentPairIndex();
                m_originalPerspective = Self ? Self->objectName() : QString();
                m_originalPaused = !replayer->isPlaying();
#ifdef QSAN_XP_LEGACY
                LocalServerController *controller = m_window->localServerController();
                if (!controller) { fail("controller_missing"); return; }
                m_originalGeneration = controller->generation();
                m_originalEndpoint = controller->endpoint();
#endif
                record("replay_ready", {{"path", m_originalPath}, {"pairIndex", m_originalPairIndex},
                    {"perspective", m_originalPerspective}, {"paused", m_originalPaused}});
                m_branchRequested = true;
                m_window->startTakeoverGame(m_snapshotPath, m_seatName);
                if (m_mode == "replay-rollback") {
                    const MainWindow::ReplayRestoreState &restore = m_window->m_replayRestoreState;
                    if (!restore.valid) { fail("takeover_restore_state_missing"); return; }
                    m_originalPath = restore.path;
                    m_originalPairIndex = restore.pairIndex;
                    m_originalPerspective = restore.perspective;
                    m_originalPaused = restore.wasPaused;
                    m_restoreCheckStarted = false;
                }
            } else if (m_mode == "replay-rollback") {
                scheduleRestoreCheck();
            } else if (m_mode == "replay-takeover" && m_takeoverReady) {
                m_takeoverGameSeen = true;
                enableTrustee();
                finish(true, "takeover_live_game");
            }
            return;
        }
        if (m_mode == "reconnect" && !m_reconnectRequested) {
#ifdef QSAN_XP_LEGACY
            LocalServerController *controller = m_window->localServerController();
            if (!controller) { fail("controller_missing"); return; }
            m_originalGeneration = controller->generation();
            m_originalEndpoint = controller->endpoint();
#endif
            m_started = true; m_reconnectRequested = true;
            record("reconnect_request", {{"generation", m_originalGeneration}, {"endpoint", m_originalEndpoint}});
            Client *previous = ClientInstance;
            previous->disconnectFromHost();
            delete previous;
            ClientInstance = nullptr;
            QTimer::singleShot(100, this, [this]() { m_window->startConnectionWithReconnect(true); });
            return;
        }
        m_started = true;
        enableTrustee();
        if (m_mode == "reconnect" && m_reconnectRequested) {
#ifdef QSAN_XP_LEGACY
            LocalServerController *controller = m_window->localServerController();
            if (!controller || controller->generation() != m_originalGeneration
                || controller->endpoint() != m_originalEndpoint) {
                fail("reconnect_replaced_helper"); return;
            }
#endif
            finish(true, "reconnect_live_game");
        }
    }

    void enableTrustee()
    {
        if (m_scene && Self && Self->getState() != QStringLiteral("trust")) {
            if (!QMetaObject::invokeMethod(m_scene, "trust", Qt::DirectConnection)) {
                fail("trust_invoke_failed");
                return;
            }
            record("trustee_enabled");
        }
    }

    void scheduleRestoreCheck()
    {
        if (m_mode != "replay-rollback" || !m_branchRequested || m_finishScheduled
            || m_restoreCheckPending) return;
        if (!m_restoreCheckStarted) {
            m_restoreCheckStarted = true;
            m_restoreDeadline = m_elapsed.elapsed() + 5000;
        }
        m_restoreCheckPending = true;
        QTimer::singleShot(100, this, [this]() {
            m_restoreCheckPending = false;
            if (m_finishScheduled) return;
            if (m_helperStopped && m_helperGraceful && m_restoreSceneSeen) {
                if (checkRestoredReplay()) return;
            }
            if (m_elapsed.elapsed() >= m_restoreDeadline) {
                fail("rollback_restore_timeout"); return;
            }
            scheduleRestoreCheck();
        });
    }

    bool checkRestoredReplay()
    {
        Replayer *replayer = ClientInstance ? ClientInstance->getReplayer() : nullptr;
        const bool restored = replayer && replayer->isValid()
            && QFileInfo(replayer->getPath()).absoluteFilePath() == QFileInfo(m_originalPath).absoluteFilePath()
            && replayer->getCurrentPairIndex() == m_originalPairIndex
            && Self && Self->objectName() == m_originalPerspective
            && ((!replayer->isPlaying()) == m_originalPaused);
        record("replay_rollback_check", {{"path", replayer ? replayer->getPath() : QString()},
            {"pairIndex", replayer ? replayer->getCurrentPairIndex() : -1},
            {"perspective", Self ? Self->objectName() : QString()},
            {"paused", replayer ? !replayer->isPlaying() : false}, {"helper_stopped", m_helperStopped}});
        if (restored && m_helperStopped && m_helperGraceful) {
            finish(true, "rollback_restored");
            return true;
        }
        return false;
    }

    void finish(bool success, const QString &stage)
    {
        if (m_finishScheduled) return;
        m_finishScheduled = true; m_result = success;
        qApp->setProperty("xpAcceptanceExitCode", success ? 0 : 1);
        record(success ? stage : "FAIL_" + stage);
        QTimer::singleShot(success ? 1500 : 0, this, [this]() {
            if (m_window) m_window->close();
#ifdef QSAN_XP_LEGACY
            if (m_window && m_window->localServerController()
                && m_window->localServerController()->active()) return;
#endif
            QCoreApplication::exit(m_result ? 0 : 1);
        });
    }
    void fail(const QString &reason) { finish(false, reason); }

    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() == QEvent::Show && object->inherits("QMessageBox")) {
            QPointer<QMessageBox> dialog(qobject_cast<QMessageBox *>(object));
            QTimer::singleShot(0, this, [this, dialog]() {
                if (!dialog) return;
                record("error_dialog", {{"message", dialog->text()}});
                dialog->accept();
                fail("error_dialog");
            });
        }
        if (event->type() == QEvent::Show && object->inherits("ServerDialog")) {
            QPointer<QObject> dialog(object);
            QTimer::singleShot(0, this, [this, dialog]() {
                if (!dialog) return;
                record("accept_server_dialog");
                QMetaObject::invokeMethod(dialog, m_mode == "host-only" || m_mode == "management"
                    ? "onServerButtonClicked" : "onConsoleButtonClicked", Qt::QueuedConnection);
            });
        }
        if (m_mode == "management" && event->type() == QEvent::Show
            && object->inherits("QInputDialog")) {
            QInputDialog *dialog = qobject_cast<QInputDialog *>(object);
            if (dialog) {
                QTimer::singleShot(0, this, [this, dialog]() {
                    if (!dialog || m_finishScheduled) return;
                    dialog->setTextValue(QStringLiteral("xp-qa-broadcast"));
                    m_managementBroadcastAccepted = true;
                    record("accept_broadcast_dialog", {{"message", "xp-qa-broadcast"}});
                    dialog->accept();
                });
            }
        }
        return QObject::eventFilter(object, event);
    }
    void record(const QString &stage, QJsonObject details = {})
    {
        details.insert("stage", stage);
        details.insert("elapsedMs", QString::number(m_elapsed.elapsed()));
        details.insert("pid", QString::number(qApp->applicationPid()));
        details.insert("utc", QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd'T'HH:mm:ss.zzz'Z'"));
        m_log.write(QJsonDocument(details).toJson(QJsonDocument::Compact) + '\n');
        m_log.flush();
    }

    QPointer<MainWindow> m_window;
    QPointer<RoomScene> m_scene;
    QString m_mode, m_replayPath, m_snapshotPath, m_seatName;
    QString m_originalPath, m_originalPerspective, m_originalGeneration, m_originalEndpoint;
    QFile m_log;
    QElapsedTimer m_elapsed;
    int m_pairIndex = 0, m_originalPairIndex = 0;
    bool m_started = false, m_over = false, m_result = false;
    bool m_finishScheduled = false, m_branchRequested = false, m_takeoverReady = false;
    bool m_takeoverGameSeen = false, m_helperStopped = false, m_reconnectRequested = false;
    bool m_helperGraceful = false, m_restoreSceneSeen = false, m_restoreCheckStarted = false;
    bool m_restoreCheckPending = false, m_managementBroadcastAccepted = false;
    bool m_originalPaused = false;
    bool m_waitingReplay = false, m_replayExported = false;
    qint64 m_restoreDeadline = 0;
};
#endif
