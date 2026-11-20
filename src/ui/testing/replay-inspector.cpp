#include "replay-inspector.h"

#include "client.h"
#include "clientplayer.h"
#include "card.h"
#include "core/client-core.h"
#include "mainwindow.h"
#include "protocol.h"
#include "recorder.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDialog>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QSaveFile>
#include <QSet>
#include <QTableWidget>
#include <QTimer>

#include <algorithm>
#include <cstdio>
#include <memory>

namespace {

constexpr int DefaultTimeoutMs = 60000;
constexpr int MaximumTimeoutMs = 45 * 60 * 1000;

QString optionValue(const QStringList &arguments, const QString &option)
{
    const int index = arguments.indexOf(option);
    return index >= 0 && index + 1 < arguments.size()
        ? arguments.at(index + 1) : QString();
}

QString digest(const QJsonObject &object)
{
    return QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(object).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
}

QJsonArray cardIds(QList<int> ids)
{
    std::sort(ids.begin(), ids.end());
    QJsonArray result;
    for (int id : ids) result.append(id);
    return result;
}

struct Seat {
    QString path;
    QString player;
    qint64 availableMs = 0;
    qint64 middleMs = 0;
    qint64 endMs = 0;
    int eventCount = 0;
    bool containsGameOver = false;
};

struct Stage {
    enum Action { Open, BackwardSeek, FreshComparison, PerspectiveSwitch } action = Open;
    QString name;
    int seat = 0;
    qint64 elapsedMs = 0;
    bool terminal = false;
};

class Inspection final : public QObject
{
public:
    int run(const QStringList &arguments)
    {
        m_elapsed.start();
        const QString input = optionValue(arguments, QStringLiteral("--inspect-replay"));
        m_reportPath = optionValue(arguments, QStringLiteral("--replay-report"));
        m_report = QJsonObject{
            {QStringLiteral("format"), QStringLiteral("qsanguosha-replay-inspection")},
            {QStringLiteral("schema_version"), 1},
            {QStringLiteral("status"), QStringLiteral("running")},
            {QStringLiteral("timeout_ms"), m_timeoutMs},
            {QStringLiteral("input"), QFileInfo(input).absoluteFilePath()}};
        if (input.isEmpty() || m_reportPath.isEmpty()) {
            error(QStringLiteral("Both --inspect-replay and --replay-report require a path"));
            return finishWithoutWindow();
        }
        m_reportPath = QFileInfo(m_reportPath).absoluteFilePath();
        // Reports are new JSON artifacts, never an input recording or its index.
        if (!m_reportPath.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)
            || QFileInfo(m_reportPath).fileName().compare(QStringLiteral("index.json"), Qt::CaseInsensitive) == 0
            || QFileInfo(m_reportPath).absoluteFilePath().compare(
                QFileInfo(input).absoluteFilePath(), Qt::CaseInsensitive) == 0) {
            error(QStringLiteral("Report must be a separate .json file, not index.json or the input"));
            m_reportPath.clear();
            return finishWithoutWindow();
        }
        if (arguments.contains(QStringLiteral("--replay-timeout-ms"))) {
            bool valid = false;
            const int requested = optionValue(arguments, QStringLiteral("--replay-timeout-ms")).toInt(&valid);
            if (!valid || requested < 1 || requested > MaximumTimeoutMs) {
                error(QStringLiteral("--replay-timeout-ms must be between 1 and %1").arg(MaximumTimeoutMs));
                return finishWithoutWindow();
            }
            m_timeoutMs = requested;
            m_report.insert(QStringLiteral("timeout_ms"), m_timeoutMs);
        }
        m_requestedSeats = optionValue(arguments, QStringLiteral("--replay-perspectives"))
            .split(',', Qt::SkipEmptyParts);
        if (!loadSeats(QFileInfo(input).absoluteFilePath())) return finishWithoutWindow();
        for (const Seat &seat : m_seats) {
            if (QFileInfo(m_reportPath).absoluteFilePath() == QFileInfo(seat.path).absoluteFilePath()) {
                error(QStringLiteral("Report path must not replace a recording"));
                m_reportPath.clear();
                return finishWithoutWindow();
            }
        }
        if (QFileInfo(m_reportPath).absoluteFilePath()
            == QFileInfo(input).absoluteDir().absoluteFilePath(QStringLiteral("index.json"))) {
            error(QStringLiteral("Report path must not replace the recording catalogue"));
            m_reportPath.clear();
            return finishWithoutWindow();
        }
        if (!writeReport()) return 1;
        if (m_elapsed.elapsed() >= m_timeoutMs) {
            error(QStringLiteral("Replay catalogue inspection exceeded the %1-ms deadline").arg(m_timeoutMs));
            return finishWithoutWindow();
        }
        buildStages();
        const bool quitOnLastWindow = QApplication::quitOnLastWindowClosed();
        QApplication::setQuitOnLastWindowClosed(false);
        m_window.reset(new MainWindow);
        qApp->installEventFilter(this);
        connect(m_window.get(), &MainWindow::replayOpenFailed, this,
            [this](const QString &detail) { error(detail); finish(); });
        connect(m_window.get(), &MainWindow::replayPositionReady, this, [this](Client *client) {
            if (m_finished) return;
            const QPointer<Client> ready(client);
            // GAME_OVER can enter a nested result dialog event loop. The exact
            // result-dialog guard is queued first; capture on the outer loop.
            QTimer::singleShot(0, this, [this, ready] {
                if (!m_finished && ready && ClientInstance == ready) capture(ready.data());
            });
        });
        m_watchdog.setSingleShot(true);
        connect(&m_watchdog, &QTimer::timeout, this, [this] {
            error(QStringLiteral("Replay inspection exceeded the %1-ms deadline").arg(m_timeoutMs));
            finish();
        });
        m_watchdog.start(qMax(1, m_timeoutMs - int(m_elapsed.elapsed())));
        QTimer::singleShot(0, this, [this] { next(); });
        m_loop.exec();
        if (!m_finished) error(QStringLiteral("Inspection event loop exited before completion"));
        m_watchdog.stop();
        qApp->removeEventFilter(this);

        // Retire the worker, then the scene, then Client, while Engine and the
        // skin factory still exist. No asynchronous objects survive the run.
        if (ClientInstance && ClientInstance->getReplayer()
            && !ClientInstance->getReplayer()->stopAndWait(5000))
            error(QStringLiteral("Replay worker did not stop during inspection shutdown"));
        QMetaObject::invokeMethod(m_window.get(), "shutdownUi", Qt::DirectConnection);
        delete ClientInstance;
        m_window.reset();
        QApplication::setQuitOnLastWindowClosed(quitOnLastWindow);
        return finishWithoutWindow();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        QDialog *dialog = qobject_cast<QDialog *>(watched);
        if (event->type() == QEvent::Show && m_finished && qobject_cast<QMessageBox *>(dialog)
            && dialog->parentWidget() == m_window.get()) {
            // A failed native open emits replayOpenFailed before its error box.
            // Close only that already-reported failure, so shutdown cannot wait.
            const QPointer<QDialog> failure(dialog);
            QTimer::singleShot(0, this, [failure] { if (failure) failure->reject(); });
        }
        if (event->type() == QEvent::Show && dialog
            && dialog->findChild<QLabel *>(QStringLiteral("result_banner"))
            && dialog->findChild<QTableWidget *>(QStringLiteral("winner_table"))
            && dialog->findChild<QTableWidget *>(QStringLiteral("loser_table"))) {
            const QPointer<QDialog> result(dialog);
            QTimer::singleShot(0, this, [this, result] {
                if (!result) return;
                m_dialogClosures.append(QJsonObject{
                    {QStringLiteral("stage"), m_stageIndex < m_stages.size()
                        ? m_stages.at(m_stageIndex).name : QString()},
                    {QStringLiteral("kind"), QStringLiteral("native-result-dialog")},
                    {QStringLiteral("elapsed_ms"), QString::number(m_elapsed.elapsed())}});
                result->accept();
            });
        }
        return QObject::eventFilter(watched, event);
    }

private:
    bool loadSeats(const QString &path)
    {
        Replayer primary(nullptr, path);
        if (!primary.isValid()) { error(primary.errorString()); return false; }
        bool completed = false;
        QFile manifest(QFileInfo(path).absoluteDir().filePath(QStringLiteral("index.json")));
        if (manifest.open(QIODevice::ReadOnly)) {
            const QJsonObject object = QJsonDocument::fromJson(manifest.readAll()).object();
            if (object.value(QStringLiteral("format")).toString() == QLatin1String("qsanguosha-headless-game")) {
                if (primary.perspectives().isEmpty()) {
                    error(QStringLiteral("Headless recording catalogue failed native validation"));
                    return false;
                }
                const QString status = object.value(QStringLiteral("status")).toString();
                m_report.insert(QStringLiteral("recording_status"), status);
                completed = status == QLatin1String("completed");
            }
        }
        QList<Replayer::Perspective> perspectives = primary.perspectives();
        if (perspectives.isEmpty()) {
            Replayer::Perspective only;
            only.path = path;
            perspectives << only;
        } else {
            // Start with the explicitly requested stream, irrespective of index order.
            for (int i = 0; i < perspectives.size(); ++i)
                if (perspectives.at(i).playerName == primary.sourcePlayerName()) {
                    perspectives.swapItemsAt(0, i);
                    break;
                }
        }
        QJsonArray catalogue;
        for (const Replayer::Perspective &perspective : perspectives) {
            if (!m_requestedSeats.isEmpty() && !m_requestedSeats.contains(perspective.playerName)) continue;
            Replayer replay(nullptr, perspective.path);
            if (!replay.isValid() || replay.events().isEmpty()) {
                error(QStringLiteral("Invalid or empty seat stream: %1: %2")
                    .arg(perspective.path, replay.errorString()));
                return false;
            }
            QString recordedSelf;
            bool sawGameOver = false;
            int getCardEvents = 0;
            int loseCardEvents = 0;
            for (const QSanReplay::ReplayEvent &event : replay.events()) {
                const QVariantMap payload = event.message.payload.toMap();
                if (recordedSelf.isEmpty() && event.message.command == QSanProtocol::S_COMMAND_SET_PROPERTY
                    && payload.value(QStringLiteral("player_name")).toString() == QLatin1String(QSanProtocol::S_PLAYER_SELF_REFERENCE_ID)
                    && payload.value(QStringLiteral("property_name")).toString() == QLatin1String("objectName"))
                    recordedSelf = payload.value(QStringLiteral("string_value")).toString();
                if (event.message.command == QSanProtocol::S_COMMAND_GAME_OVER) sawGameOver = true;
                if (event.message.command == QSanProtocol::S_COMMAND_GET_CARD) ++getCardEvents;
                if (event.message.command == QSanProtocol::S_COMMAND_LOSE_CARD) ++loseCardEvents;
            }
            if (recordedSelf.isEmpty() || (!perspective.playerName.isEmpty() && recordedSelf != perspective.playerName)) {
                error(QStringLiteral("Recorded .self identity does not match source seat: %1").arg(perspective.path));
                return false;
            }
            // A structurally valid stream without moves cannot verify hands.
            // Reject missing coverage before empty hands can become a baseline.
            if (getCardEvents == 0 || loseCardEvents == 0) {
                error(QStringLiteral("No card-movement coverage for %1 (GET_CARD=%2, LOSE_CARD=%3)")
                    .arg(perspective.path).arg(getCardEvents).arg(loseCardEvents));
                return false;
            }
            Seat seat;
            seat.path = perspective.path;
            seat.player = recordedSelf;
            seat.availableMs = perspective.availableFromMs;
            seat.endMs = replay.events().last().elapsedMs;
            seat.middleMs = seat.availableMs + (seat.endMs - seat.availableMs) / 2;
            seat.eventCount = int(replay.events().size());
            seat.containsGameOver = sawGameOver;
            // Native death/skill unwinding can publish cleanup after GAME_OVER.
            // Keep those events and verify game_over at the final applied state.
            if (completed && !seat.containsGameOver) {
                error(QStringLiteral("Completed stream has no GAME_OVER: %1").arg(seat.path));
                return false;
            }
            m_seats << seat;
            catalogue.append(QJsonObject{{QStringLiteral("player_name"), seat.player},
                {QStringLiteral("file"), seat.path},
                {QStringLiteral("available_from_ms"), QString::number(seat.availableMs)},
                {QStringLiteral("middle_ms"), QString::number(seat.middleMs)},
                {QStringLiteral("end_ms"), QString::number(seat.endMs)},
                {QStringLiteral("event_count"), seat.eventCount},
                {QStringLiteral("get_card_events"), getCardEvents},
                {QStringLiteral("lose_card_events"), loseCardEvents},
                {QStringLiteral("game_over_seen"), seat.containsGameOver},
                {QStringLiteral("terminal_game_over"),
                    replay.events().last().message.command == QSanProtocol::S_COMMAND_GAME_OVER}});
        }
        m_report.insert(QStringLiteral("perspectives"), catalogue);
        for (const QString &requested : m_requestedSeats) {
            const bool found = std::any_of(m_seats.cbegin(), m_seats.cend(),
                [&requested](const Seat &seat) { return seat.player == requested; });
            if (!found) {
                error(QStringLiteral("Requested replay perspective is unavailable: %1").arg(requested));
                return false;
            }
        }
        return !m_seats.isEmpty();
    }

    void buildStages()
    {
        for (int i = 0; i < m_seats.size(); ++i) {
            const Seat &seat = m_seats.at(i);
            m_stages << Stage{Stage::Open, QStringLiteral("middle:%1").arg(seat.player), i, seat.middleMs, false};
            m_stages << Stage{Stage::Open, QStringLiteral("ending:%1").arg(seat.player), i, seat.endMs, true};
            if (i == 0) {
                m_stages << Stage{Stage::BackwardSeek, QStringLiteral("backward-seek"), 0, seat.middleMs, false};
                m_stages << Stage{Stage::FreshComparison, QStringLiteral("fresh-prefix-comparison"), 0, seat.middleMs, false};
            }
        }
        if (m_seats.size() > 1) {
            m_stages << Stage{Stage::Open, QStringLiteral("perspective-switch-anchor"), 0, m_seats.first().endMs, true};
            for (int i = 1; i < m_seats.size(); ++i)
                m_stages << Stage{Stage::PerspectiveSwitch,
                    QStringLiteral("perspective-switch:%1").arg(m_seats.at(i).player), i, 0, false};
        }
    }

    void next()
    {
        if (m_finished) return;
        if (++m_stageIndex >= m_stages.size()) {
            if (!m_backwardSeekObserved) error(QStringLiteral("No backward seek was observed"));
            if (m_seats.size() > 1 && m_switchesObserved == 0)
                error(QStringLiteral("No available perspective switch was observed"));
            finish();
            return;
        }
        const Stage &stage = m_stages.at(m_stageIndex);
        const Seat &seat = m_seats.at(stage.seat);
        m_report.insert(QStringLiteral("active_stage"), stage.name);
        if (!writeReport()) { finish(); return; }
        if (stage.action == Stage::BackwardSeek) {
            if (!ClientInstance || m_baselineIndex < 0
                || ClientInstance->lastReplayPairIndex() <= m_baselineIndex) {
                error(QStringLiteral("Recording has no earlier middle boundary for a backward seek"));
                finish();
                return;
            }
            m_window->seekReplayToPosition(m_baselineIndex);
        } else if (stage.action == Stage::PerspectiveSwitch) {
            if (!ClientInstance || !ClientInstance->getPlayer(seat.player)
                || ClientInstance->lastReplayElapsedMs() < seat.availableMs) {
                m_captures.append(QJsonObject{{QStringLiteral("stage"), stage.name},
                    {QStringLiteral("status"), QStringLiteral("skipped")},
                    {QStringLiteral("reason"), QStringLiteral("Seat is not in the roster at this coordinate")}});
                QTimer::singleShot(0, this, [this] { next(); });
                return;
            }
            m_window->switchReplayPerspective(seat.player);
        } else if (!m_window->openReplayAt(seat.path,
            stage.action == Stage::FreshComparison ? m_baselineElapsed : stage.elapsedMs, true, 2.0)) {
            error(QStringLiteral("Native replay open failed for stage %1").arg(stage.name));
            finish();
        }
    }

    void capture(Client *client)
    {
        if (m_stageIndex < 0 || m_stageIndex >= m_stages.size()) return;
        const Stage &stage = m_stages.at(m_stageIndex);
        const Seat &seat = m_seats.at(stage.seat);
        Replayer *replayer = client->getReplayer();
        ClientCore *core = client->interactionCore();
        if (!replayer || !core) { error(QStringLiteral("Replay Client/Core disappeared")); finish(); return; }
        const ClientGameState *state = core->state();
        QStringList failures;
        QSet<QString> names, childNames;
        QJsonArray players;
        for (const ClientPlayer *player : client->getPlayers()) {
            if (!player) { failures << QStringLiteral("Null player in active roster"); continue; }
            const QString name = player->objectName();
            if (name.isEmpty() || names.contains(name)) failures << QStringLiteral("Duplicate or empty active player name: %1").arg(name);
            names.insert(name);
            QList<int> known;
            for (const Card *card : player->getKnownCards()) if (card) known << card->getEffectiveId();
            const QList<int> hand = player->handCards();
            QSet<int> uniqueCards;
            for (int id : hand) {
                if (id >= 0 && uniqueCards.contains(id)) failures << QStringLiteral("Duplicate hand card %1 for %2").arg(id).arg(name);
                if (id >= 0) uniqueCards.insert(id);
            }
            players.append(QJsonObject{{QStringLiteral("player_name"), name},
                {QStringLiteral("seat"), player->getSeat()},
                {QStringLiteral("alive"), player->isAlive()},
                {QStringLiteral("general"), player->getGeneralName()},
                {QStringLiteral("hp"), player->getHp()},
                {QStringLiteral("maxhp"), player->getMaxHp()},
                {QStringLiteral("hand_count"), player->getHandcardNum()},
                {QStringLiteral("hand_ids"), cardIds(hand)},
                {QStringLiteral("known_hand_ids"), cardIds(known)}});
        }
        for (const ClientPlayer *child : client->findChildren<ClientPlayer *>(QString(), Qt::FindDirectChildrenOnly)) {
            const QString name = child->objectName();
            if (name.isEmpty() || childNames.contains(name)) failures << QStringLiteral("Duplicate or empty ClientPlayer child: %1").arg(name);
            childNames.insert(name);
        }
        if (names != childNames) failures << QStringLiteral("ClientPlayer children differ from active roster");
        const QStringList coreNames = state->playerNames();
        const QSet<QString> coreNameSet(coreNames.cbegin(), coreNames.cend());
        if (coreNameSet.size() != coreNames.size() || coreNameSet != names)
            failures << QStringLiteral("ClientCore roster differs from native Client players");
        if (state->selfName() != seat.player) failures << QStringLiteral("ClientCore .self differs from source seat");
        if (!replayer->sourcePlayerName().isEmpty() && replayer->sourcePlayerName() != seat.player)
            failures << QStringLiteral("Selected native stream differs from requested source seat");
        QString operating = state->gameValue(QStringLiteral("operating_player")).toString();
        if (operating.isEmpty()) operating = state->selfName();
        if (!Self || Self->objectName() != operating)
            failures << QStringLiteral("Native operating Self differs from recorded control context");
        if (replayer->isPlaying() || replayer->getSpeed() != 2.0)
            failures << QStringLiteral("Paused state or playback speed was not preserved");
        const int coordinate = client->lastReplayPairIndex();
        if (coordinate < 0 || coordinate >= replayer->events().size()
            || replayer->events().at(coordinate).elapsedMs != client->lastReplayElapsedMs())
            failures << QStringLiteral("Applied replay event coordinate is inconsistent");
        if (stage.terminal && (coordinate != seat.eventCount - 1
            || (seat.containsGameOver && !state->gameValue(QStringLiteral("game_over")).toBool())))
            failures << QStringLiteral("Ending did not reach the terminal recorded state");

        const QJsonObject coreJson = core->toJson();
        bool compared = false;
        bool matches = true;
        if (m_stageIndex == 0) {
            m_baselineCore = coreJson;
            m_baselinePlayers = players;
            m_baselineIndex = coordinate;
            m_baselineElapsed = client->lastReplayElapsedMs();
        } else if (stage.action == Stage::BackwardSeek || stage.action == Stage::FreshComparison) {
            compared = true;
            matches = coreJson == m_baselineCore && players == m_baselinePlayers && coordinate == m_baselineIndex;
            if (!matches) failures << QStringLiteral("Reconstructed middle state differs from the fresh prefix baseline");
            if (stage.action == Stage::BackwardSeek) m_backwardSeekObserved = true;
        }
        if (stage.action == Stage::PerspectiveSwitch) ++m_switchesObserved;
        QJsonArray issues;
        for (const QString &failure : failures) issues.append(failure);
        m_captures.append(QJsonObject{{QStringLiteral("stage"), stage.name},
            {QStringLiteral("status"), failures.isEmpty() ? QStringLiteral("passed") : QStringLiteral("failed")},
            {QStringLiteral("file"), replayer->getPath()},
            {QStringLiteral("source_player"), seat.player},
            {QStringLiteral("core_self"), state->selfName()},
            {QStringLiteral("operating_player"), Self ? Self->objectName() : QString()},
            {QStringLiteral("applied_event_index"), coordinate},
            {QStringLiteral("applied_elapsed_ms"), QString::number(client->lastReplayElapsedMs())},
            {QStringLiteral("paused"), !replayer->isPlaying()},
            {QStringLiteral("speed"), replayer->getSpeed()},
            {QStringLiteral("fresh_prefix_compared"), compared},
            {QStringLiteral("fresh_prefix_matches"), matches},
            {QStringLiteral("client_core_sha256"), digest(coreJson)},
            {QStringLiteral("client_core"), coreJson},
            {QStringLiteral("players"), players},
            {QStringLiteral("errors"), issues}});
        for (const QString &failure : failures) error(stage.name + QStringLiteral(": ") + failure);
        if (!writeReport() || !failures.isEmpty()) { finish(); return; }
        QTimer::singleShot(0, this, [this] { next(); });
    }

    void error(const QString &message)
    {
        m_errors.append(message);
        qWarning().noquote() << "REPLAY_INSPECTOR" << message;
    }

    bool writeReport()
    {
        m_report.insert(QStringLiteral("elapsed_ms"), QString::number(m_elapsed.elapsed()));
        m_report.insert(QStringLiteral("stages"), m_captures);
        m_report.insert(QStringLiteral("errors"), m_errors);
        m_report.insert(QStringLiteral("result_dialog_closures"), m_dialogClosures);
        m_report.insert(QStringLiteral("backward_seek_observed"), m_backwardSeekObserved);
        m_report.insert(QStringLiteral("perspective_switches_observed"), m_switchesObserved);
        if (m_reportPath.isEmpty()) return false;
        const QDir directory = QFileInfo(m_reportPath).absoluteDir();
        if (!QDir().mkpath(directory.absolutePath())) {
            error(QStringLiteral("Cannot create report directory"));
            return false;
        }
        QSaveFile output(m_reportPath);
        const QByteArray bytes = QJsonDocument(m_report).toJson(QJsonDocument::Indented);
        if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
            error(QStringLiteral("Cannot write replay inspection report: %1").arg(output.errorString()));
            return false;
        }
        return true;
    }

    void finish()
    {
        if (m_finished) return;
        m_finished = true;
        m_watchdog.stop();
        m_loop.quit();
    }

    int finishWithoutWindow()
    {
        m_report.insert(QStringLiteral("status"), m_errors.isEmpty() ? QStringLiteral("passed") : QStringLiteral("failed"));
        const bool saved = writeReport();
        const int code = saved && m_errors.isEmpty() ? 0 : 1;
        const QByteArray marker = QStringLiteral("REPLAY_INSPECTION %1 %2\n")
            .arg(code == 0 ? QStringLiteral("PASS") : QStringLiteral("FAIL"), m_reportPath).toUtf8();
        std::fwrite(marker.constData(), 1, size_t(marker.size()), stdout);
        std::fflush(stdout);
        return code;
    }

    std::unique_ptr<MainWindow> m_window;
    QEventLoop m_loop;
    QTimer m_watchdog;
    QElapsedTimer m_elapsed;
    QString m_reportPath;
    QStringList m_requestedSeats;
    QJsonObject m_report;
    QJsonArray m_errors, m_captures, m_dialogClosures;
    QList<Seat> m_seats;
    QList<Stage> m_stages;
    int m_stageIndex = -1;
    int m_timeoutMs = DefaultTimeoutMs;
    int m_baselineIndex = -1;
    qint64 m_baselineElapsed = 0;
    QJsonObject m_baselineCore;
    QJsonArray m_baselinePlayers;
    bool m_backwardSeekObserved = false;
    int m_switchesObserved = 0;
    bool m_finished = false;
};

} // namespace

int ReplayInspector::run(QStringList arguments)
{
    Inspection inspection;
    return inspection.run(arguments);
}
