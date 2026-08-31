#include "tui-application-controller.h"

#include "card.h"
#include "engine.h"
#include "protocol-interaction-request-builder.h"
#include "protocol/session/session-payloads.h"
#include "tui-script-runner.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QSet>
#include <QTextStream>
#include <QTimer>

using namespace QSanProtocol;

namespace {

QString tr(const char *source)
{
    return QCoreApplication::translate("QSanguoshaTui", source);
}

} // namespace

TuiApplicationController::TuiApplicationController(const TuiApplicationOptions &options,
    QObject *parent)
    : QObject(parent), m_options(options), m_session(&m_core, this),
      m_renderer(options.ansiEnabled,
          [this](int cardId) { return resolveCardDisplayText(cardId); },
          [this](const QString &name) { return resolveNameText(name); }),
      m_view(&m_renderer, [this](const QString &text) { writeOutput(text); },
             [this](int cardId) { return resolveCardWireText(cardId); }),
      m_input(this)
{
    m_core.setView(&m_view);
    connect(&m_input, &TuiInput::lineReady, this, &TuiApplicationController::handleInputLine);
    connect(&m_input, &TuiInput::endOfInput, this, [this]() { requestExit(0); });
    connect(&m_input, &TuiInput::interruptRequested, this, [this]() { requestExit(0); });
    connect(&m_input, &TuiInput::inputError, this, [this](const QString &error) {
        writeError(error);
        requestExit(6);
    });
    connect(&m_session, &ClientLiveSession::connectionChanged, this,
        [this](const QString &state) { writeOutput(tr("連線：%1").arg(state)); });
    connect(&m_session, &ClientLiveSession::sessionActive, this, [this](bool reconnected) {
        writeOutput(reconnected ? tr("已重連；等待原子狀態快照")
                                : tr("Protocol V2 工作階段已啟用"));
        if (reconnected && m_trusted) {
            TrustPayload payload;
            payload.trusted = true;
            QString error;
            if (!m_session.sendControl(S_COMMAND_TRUST, payload.toVariant(), &error))
                writeError(tr("重連後無法恢復託管：%1").arg(error));
        }
        if (m_script != nullptr)
            m_script->notifyStateChanged();
    });
    connect(&m_session, &ClientLiveSession::stateChanged, this, [this]() {
        const QString syncId = m_core.state()->connectionValue(
            QStringLiteral("sync_id")).toString();
        if (m_core.state()->connectionValue(QStringLiteral("sync_phase"))
                == QLatin1String("end")
            && !syncId.isEmpty() && syncId != m_lastMarkedSyncId) {
            m_lastMarkedSyncId = syncId;
            writeAutomationMarker(QStringLiteral("STATE_SYNC_COMMITTED"));
        }
        if (m_core.state()->gameValue(QStringLiteral("game_over")).toBool()
            && !m_gameOverMarked) {
            m_gameOverMarked = true;
            writeAutomationMarker(QStringLiteral("GAME_OVER"));
        }
        if (m_script != nullptr)
            m_script->notifyStateChanged();
        if (m_core.state()->gameValue(QStringLiteral("game_over")).toBool())
            writeOutput(m_renderer.renderState(*m_core.state()));
    });
    connect(&m_session, &ClientLiveSession::presentationEvent, this,
        [this](int, const QString &text) { writeOutput(text); });
    connect(&m_session, &ClientLiveSession::commandResult, this,
        [this](int command, bool success, const QString &message) {
            writeOutput(tr("命令 %1：%2%3").arg(command)
                .arg(success ? tr("成功") : tr("失敗"),
                     message.isEmpty() ? QString() : tr(" - %1").arg(message)));
        });
    connect(&m_session, &ClientLiveSession::interactionRequested, this,
        [this](const ProtocolMessage &message) {
            InteractionRequest request;
            QString error;
            if (!ProtocolInteractionRequestBuilder::build(message, *m_core.state(),
                    &request, &error)) {
                writeError(error);
                requestExit(4);
                return;
            }
            if (request.timeoutMs <= 0) {
                const int seconds = m_core.state()->setup().value(
                    QStringLiteral("operation_timeout")).toInt();
                if (seconds > 0)
                    request.timeoutMs = static_cast<qint64>(seconds) * 1000 + 5000;
            }
            if (m_core.beginRequest(std::move(request)) == 0) {
                writeError(tr("無法啟用伺服器互動"));
                requestExit(4);
                return;
            }
            if (m_script != nullptr)
                m_script->notifyStateChanged();
        });
    connect(&m_session, &ClientLiveSession::fatalError, this,
        [this](int exitCode, const QString &code, const QString &message) {
            writeError(QStringLiteral("%1: %2").arg(code, message));
            requestExit(exitCode);
        });
}

bool TuiApplicationController::start(QString *error)
{
    if (!m_options.logFile.isEmpty()) {
        m_log.setFileName(m_options.logFile);
        if (!m_log.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            if (error != nullptr)
                *error = tr("無法開啟記錄檔：%1").arg(m_log.errorString());
            return false;
        }
    }
    m_core.state()->setConnectionValue(QStringLiteral("host"), m_options.session.host);
    m_core.state()->setConnectionValue(QStringLiteral("port"), m_options.session.port);

    if (m_options.scriptFile.isEmpty()) {
        if (!m_input.start(error))
            return false;
    } else {
        m_script = new TuiScriptRunner(&m_core,
            [this](const QString &line) { handleInputLine(line); }, this);
        if (!m_script->load(m_options.scriptFile, error))
            return false;
        connect(m_script, &TuiScriptRunner::scriptError, this, [this](const QString &message) {
            writeError(message);
            requestExit(7);
        });
        connect(m_script, &TuiScriptRunner::finished, this, [this]() { requestExit(0); });
        m_script->start();
    }

    writeOutput(tr("QSanguosha 終端客戶端－輸入 /help 查看命令"));
    m_session.connectToServer(m_options.session);
    return true;
}

void TuiApplicationController::handleInputLine(const QString &line)
{
    if (m_exiting)
        return;
    const QString input = line.trimmed();
    if (input.isEmpty())
        return;
    if (input.startsWith(QLatin1Char('/'))) {
        TuiCommandIntent intent;
        QString error;
        if (!TuiCommandParser::parse(input, &intent, &error)) {
            writeError(error);
            return;
        }
        handleCommand(intent);
        return;
    }
    if (!m_core.hasActiveRequest()) {
        writeError(tr("目前沒有互動；輸入 /help 查看命令"));
        return;
    }
    InteractionResponse response;
    QString error;
    if (!m_view.parseAnswer(m_core.activeRequest(), input, &response, &error)) {
        writeError(error);
        return;
    }
    if (!m_session.submitInteractionResponse(std::move(response), &error))
        writeError(error.isEmpty() ? tr("作答已被拒絕") : error);
}

void TuiApplicationController::handleCommand(const TuiCommandIntent &intent)
{
    static const QSet<TuiCommandType> promptSafeCommands{TuiCommandType::Cancel,
        TuiCommandType::Help, TuiCommandType::Status, TuiCommandType::Players,
        TuiCommandType::Hand, TuiCommandType::Equipment, TuiCommandType::Piles,
        TuiCommandType::Skills, TuiCommandType::Log, TuiCommandType::Quit};
    if (m_core.hasActiveRequest() && !promptSafeCommands.contains(intent.type)) {
        writeError(tr("提示期間只可使用唯讀命令、/cancel 與 /quit"));
        return;
    }
    if (intent.type == TuiCommandType::Help) {
        writeOutput(tr(
            "/help /status /players /hand /equip /piles /skills /log\n"
            "/chat <文字> /trust [on|off] /addrobot [all|數量] /surrender /reconnect /quit\n"
            "提示作答：索引、標籤、範圍（1-3）、card <牌字串> -> 目標、"
            "頂部 | 底部、cards <索引> -> 玩家、/cancel"));
    } else if (intent.type == TuiCommandType::Status) {
        writeOutput(m_renderer.renderState(*m_core.state()));
    } else if (intent.type == TuiCommandType::Players) {
        writeOutput(m_renderer.renderPlayers(*m_core.state()));
    } else if (intent.type == TuiCommandType::Hand) {
        writeOutput(m_renderer.renderHand(*m_core.state()));
    } else if (intent.type == TuiCommandType::Equipment) {
        writeOutput(renderEquipment());
    } else if (intent.type == TuiCommandType::Piles) {
        writeOutput(renderPiles());
    } else if (intent.type == TuiCommandType::Skills) {
        writeOutput(renderSkills());
    } else if (intent.type == TuiCommandType::Log) {
        writeOutput(renderLog());
    } else if (intent.type == TuiCommandType::Cancel) {
        if (!m_core.hasActiveRequest()) {
            writeError(tr("目前沒有互動"));
            return;
        }
        InteractionResponse response = InteractionResponse::makeCancel(m_core.activeRequestId());
        response.command = m_core.activeRequest().command;
        QString error;
        if (!m_session.submitInteractionResponse(std::move(response), &error))
            writeError(error.isEmpty() ? tr("此請求不可取消") : error);
    } else if (intent.type == TuiCommandType::Chat) {
        ChatPayload payload;
        payload.text = TuiRenderer::sanitize(intent.text, 1000);
        QString error;
        if (!m_session.sendControl(S_COMMAND_SPEAK, payload.toVariant(), &error))
            writeError(error);
    } else if (intent.type == TuiCommandType::Trust) {
        const bool requested = intent.trustMode == TuiTrustMode::Enable
            || (intent.trustMode == TuiTrustMode::Toggle && !m_trusted);
        TrustPayload payload;
        payload.trusted = requested;
        QString error;
        if (!m_session.sendControl(S_COMMAND_TRUST, payload.toVariant(), &error))
            writeError(error);
        else
            m_trusted = requested;
    } else if (intent.type == TuiCommandType::AddRobot) {
        AddRobotPayload payload;
        payload.fillRemaining = intent.fillRemaining;
        payload.count = intent.count;
        QString error;
        if (!m_session.sendControl(S_COMMAND_ADD_ROBOT, payload.toVariant(), &error))
            writeError(error);
    } else if (intent.type == TuiCommandType::Surrender) {
        SurrenderRequestPayload payload;
        QString error;
        if (!m_session.sendRequest(S_COMMAND_SURRENDER, payload.toVariant(), &error))
            writeError(error);
    } else if (intent.type == TuiCommandType::Reconnect) {
        m_session.reconnect();
    } else if (intent.type == TuiCommandType::Quit) {
        requestExit(0);
    }
}

QString TuiApplicationController::resolveCardWireText(int cardId) const
{
    const Card *card = Sanguosha != nullptr ? Sanguosha->getCard(cardId) : nullptr;
    return card != nullptr ? card->toString() : QString::number(cardId);
}

QString TuiApplicationController::resolveCardDisplayText(int cardId) const
{
    const Card *card = Sanguosha != nullptr ? Sanguosha->getCard(cardId) : nullptr;
    return card != nullptr ? resolveNameText(card->objectName()) : tr("牌 %1").arg(cardId);
}

QString TuiApplicationController::resolveNameText(const QString &name) const
{
    if (Sanguosha == nullptr || name.isEmpty())
        return name;
    const QString translated = Sanguosha->translate(name);
    return translated.isEmpty() ? name : translated;
}

QString TuiApplicationController::renderPiles() const
{
    const QVariantMap player = m_core.state()->player(m_core.state()->selfName());
    const QVariantMap piles = player.value(QStringLiteral("piles")).toMap();
    QStringList lines{tr("== 牌堆 ==")};
    for (auto it = piles.constBegin(); it != piles.constEnd(); ++it)
        lines << tr("%1：%2 張").arg(TuiRenderer::sanitize(it.key(), 128))
            .arg(it.value().toList().size());
    lines << tr("牌堆：%1  棄牌：%2")
        .arg(m_core.state()->gameValue(QStringLiteral("draw_pile_count")).toInt())
        .arg(m_core.state()->gameValue(QStringLiteral("discard_pile")).toList().size());
    return lines.join(QLatin1Char('\n'));
}

QString TuiApplicationController::renderSkills() const
{
    const QVariantMap player = m_core.state()->player(m_core.state()->selfName());
    QStringList translated;
    for (const QString &skill : player.value(QStringLiteral("skills")).toStringList())
        translated << resolveNameText(skill);
    return tr("== 技能 ==\n%1").arg(TuiRenderer::sanitize(
        translated.join(QLatin1Char(' ')), 2048));
}

QString TuiApplicationController::renderEquipment() const
{
    QStringList lines{tr("== 裝備 ==")};
    const QList<int> equipment = m_core.state()->cardsForPlayer(
        m_core.state()->selfName(), 1);
    for (int cardId : equipment)
        lines << QStringLiteral("ID=%1 %2").arg(cardId)
            .arg(TuiRenderer::sanitize(resolveCardDisplayText(cardId), 512));
    if (equipment.isEmpty())
        lines << tr("（空）");
    return lines.join(QLatin1Char('\n'));
}

QString TuiApplicationController::renderLog() const
{
    QStringList lines{tr("== 記錄 ==")};
    const QVariantList events = m_core.state()->presentationEvents();
    const int first = qMax(0, events.size() - 30);
    for (int i = first; i < events.size(); ++i)
        lines << TuiRenderer::sanitize(events.at(i).toMap().value(QStringLiteral("text")).toString(), 1024);
    return lines.join(QLatin1Char('\n'));
}

void TuiApplicationController::writeOutput(const QString &text)
{
    const QString safe = TuiRenderer::sanitize(text, 16384);
    QTextStream(stdout) << safe << '\n' << Qt::flush;
    appendLogLine(safe);
}

void TuiApplicationController::writeError(const QString &text)
{
    const QString safe = TuiRenderer::sanitize(text, 4096);
    QTextStream(stderr) << "TUI_ERROR " << safe << '\n' << Qt::flush;
    appendLogLine(QStringLiteral("TUI_ERROR %1").arg(safe));
}

void TuiApplicationController::writeAutomationMarker(const QString &marker)
{
    if (!m_log.isOpen())
        return;
    appendLogLine(QStringLiteral("[TUI_EVENT] %1")
        .arg(TuiRenderer::sanitize(marker, 128)));
}

bool TuiApplicationController::appendLogLine(const QString &line)
{
    if (!m_log.isOpen() || m_logFailed)
        return !m_logFailed;
    QByteArray bytes = line.toUtf8();
    bytes.append('\n');
    if (m_log.write(bytes) == bytes.size() && m_log.flush())
        return true;

    m_logFailed = true;
    const QString error = m_log.errorString();
    m_log.close();
    QTextStream(stderr) << "TUI_ERROR log_file: "
                        << TuiRenderer::sanitize(error, 1024)
                        << '\n' << Qt::flush;
    QTimer::singleShot(0, this, [this]() { requestExit(6); });
    return false;
}

void TuiApplicationController::requestExit(int code)
{
    if (m_exiting)
        return;
    m_exiting = true;
    m_input.stop();
    m_session.disconnectGracefully();
    QTimer::singleShot(0, QCoreApplication::instance(), [code]() {
        QCoreApplication::exit(code);
    });
}
