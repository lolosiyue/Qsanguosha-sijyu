#include "tui-application-controller.h"

#include "card.h"
#include "engine.h"
#include "protocol-interaction-request-builder.h"
#include "skill.h"
#include "tui-card-text.h"
#include "tui-log-text.h"
#include "protocol/session/session-payloads.h"
#include "tui-script-runner.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QSet>
#include <QTextStream>
#include <QTimer>
#include <variant>

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
             [this](int cardId) { return resolveCardWireText(cardId); },
             [this](const QString &skillName, int instanceId, const QList<int> &subcards,
                    QString *error) {
                 return resolveSkillCardWireText(skillName, instanceId, subcards, error);
             }),
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
    connect(&m_input, &TuiInput::completionChoices, this, [this](const QStringList &matches) {
        writeOutput(matches.join(QLatin1Char(' ')));
    });
    connect(&m_session, &ClientLiveSession::connectionChanged, this,
        [this](const QString &state) { writeOutput(tr("連線：%1").arg(state)); });
    connect(&m_session, &ClientLiveSession::sessionActive, this, [this](bool reconnected) {
        writeOutput(reconnected ? tr("已重連；等待原子狀態快照")
                                : tr("Protocol V2 工作階段已啟用"));
        // The server does not echo our own screen name back, so record the one
        // we signed up with; every view then names us the same way.
        const QString self = m_core.state()->selfName();
        if (!self.isEmpty() && !m_options.session.screenName.isEmpty()
            && m_core.state()->player(self).value(QStringLiteral("screen_name"))
                   .toString().isEmpty()) {
            m_core.state()->setPlayerValue(self, QStringLiteral("screen_name"),
                                           m_options.session.screenName);
        }
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
        [this](int command, const QString &text, const QVariant &payload) {
            const QString line = presentationText(command, text, payload);
            if (!line.isEmpty())
                writeOutput(line);
        });
    connect(&m_session, &ClientLiveSession::commandResult, this,
        [this](int command, bool success, const QString &message) {
            const QString line = TuiRenderer::commandResultText(command, success, message);
            if (!line.isEmpty())
                writeOutput(line);
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
            if (auto *assignment = std::get_if<RoleAssignmentInteractionPayload>(
                    &request.payload)) {
                if (assignment->roles.isEmpty() && Sanguosha != nullptr) {
                    assignment->roles = Sanguosha->getRoleList(
                        m_core.state()->setup().value(QStringLiteral("game_mode")).toString());
                }
            }
            if (auto *cards = std::get_if<CardInteractionPayload>(&request.payload)) {
                if (request.type == InteractionType::PlayCard)
                    fillPlaySkillCandidates(cards);
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
            trySkipRoleAssignment();
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
        m_input.setCompleter([this](const QString &line, QStringList *matches) {
            const TuiCompletion done = completeTuiLine(line, completionExtraTokens());
            if (matches != nullptr)
                *matches = done.matches;
            return done.line;
        });
    } else {
        m_script = new TuiScriptRunner(&m_core,
            [this](const QString &line) { handleInputLine(line); }, this);
        m_script->setEventFormatter([this](int command, const QString &fallbackText,
                                           const QVariant &payload) {
            return presentationText(command, fallbackText, payload);
        });
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

bool TuiApplicationController::trySkipRoleAssignment()
{
    if (!m_core.hasActiveRequest(InteractionType::ChooseRole))
        return false;

    InteractionResponse response = InteractionResponse::makeCancel(m_core.activeRequestId());
    response.command = m_core.activeRequest().command;
    QString error;
    if (m_session.submitInteractionResponse(std::move(response), &error))
        return true;

    writeError(error.isEmpty() ? tr("無法略過身分分配") : error);
    writeOutput(m_renderer.renderInteraction(m_core.activeRequest()));
    return false;
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

void TuiApplicationController::fillPlaySkillCandidates(CardInteractionPayload *payload) const
{
    if (payload == nullptr || Sanguosha == nullptr)
        return;
    const QString self = m_core.state()->selfName();
    if (self.isEmpty())
        return;

    QSet<QString> seenKeys;
    QSet<QString> seenNames;
    auto addSkill = [&](const QString &name, int instanceId) {
        const Skill *skill = Sanguosha->getSkill(name);
        if (skill == nullptr || skill->isHideSkill() || !skill->isVisible())
            return;
        if (skill->inherits("FilterSkill"))
            return;
        if (ViewAsSkill::parseViewAsSkill(skill) == nullptr)
            return;
        const QString key = QStringLiteral("%1#%2").arg(name).arg(instanceId);
        if (seenKeys.contains(key))
            return;
        if (instanceId <= 0 && seenNames.contains(name))
            return;
        seenKeys.insert(key);
        if (instanceId > 0)
            seenNames.insert(name);
        SkillActivationCandidate candidate;
        candidate.skillName = name;
        candidate.instanceId = instanceId;
        payload->skillCandidates.append(candidate);
    };

    const QVariantMap instances = m_core.state()->playerValue(
        self, QStringLiteral("skill_instances")).toMap();
    for (auto it = instances.constBegin(); it != instances.constEnd(); ++it) {
        const QVariantMap entry = it.value().toMap();
        if (!entry.value(QStringLiteral("visible"), true).toBool())
            continue;
        addSkill(entry.value(QStringLiteral("skill_name")).toString(),
                 entry.value(QStringLiteral("instance_id")).toInt());
    }
    for (const QString &name : m_core.state()->playerValue(
             self, QStringLiteral("skills")).toStringList()) {
        addSkill(name, 0);
    }
    for (int cardId : m_core.state()->cardsForPlayer(self, 1)) {
        const Card *equip = Sanguosha->getEngineCard(cardId);
        if (equip != nullptr)
            addSkill(equip->objectName(), 0);
    }
}

QString TuiApplicationController::resolveSkillCardWireText(const QString &skillName,
    int instanceId, const QList<int> &subcardIds, QString *error) const
{
    if (Sanguosha == nullptr) {
        if (error != nullptr)
            *error = tr("引擎尚未載入");
        return QString();
    }
    const ViewAsSkill *viewAs = Sanguosha->getViewAsSkill(skillName);
    if (viewAs == nullptr) {
        if (error != nullptr)
            *error = tr("沒有這個轉換技");
        return QString();
    }

    const Card *card = nullptr;
    if (const auto *v2 = dynamic_cast<const ViewAsSkillV2 *>(viewAs)) {
        ActiveSkillRequest request;
        request.reason = CardUseStruct::CARD_USE_REASON_PLAY;
        request.selectedCardIds = subcardIds;
        request.activationRef = SkillInstanceRef(m_core.state()->selfName(),
            SkillInstanceKey(skillName, instanceId));
        card = v2->createCard(request);
    } else if (const auto *zero = qobject_cast<const ZeroCardViewAsSkill *>(viewAs)) {
        card = zero->viewAs();
    } else {
        QList<const Card *> selected;
        for (int cardId : subcardIds) {
            const Card *subcard = Sanguosha->getEngineCard(cardId);
            if (subcard == nullptr) {
                if (error != nullptr)
                    *error = tr("沒有這張牌");
                return QString();
            }
            selected.append(subcard);
        }
        card = viewAs->viewAs(selected);
    }
    if (card == nullptr) {
        if (error != nullptr) {
            *error = subcardIds.isEmpty()
                ? tr("此技能需要選手牌")
                : tr("這些牌不能發動該技能");
        }
        return QString();
    }

    Card *mutableCard = const_cast<Card *>(card);
    mutableCard->setActivationSkill(skillName, instanceId);
    const QString text = card->toString();
    if (card->isVirtualCard() && card->parent() == nullptr)
        mutableCard->deleteLater();
    return text;
}

QString TuiApplicationController::resolveCardDisplayText(int cardId) const
{
    return tuiCardDisplayText(cardId);
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

QString TuiApplicationController::resolvePlayerName(const QString &objectName) const
{
    const QVariantMap player = m_core.state()->player(objectName);
    const QString screenName = player.value(QStringLiteral("screen_name")).toString();
    if (!screenName.isEmpty())
        return screenName;
    // The server never echoes our own screen name back, so use the one we
    // signed up with rather than showing the player their object name.
    if (objectName == m_core.state()->selfName() && !m_options.session.screenName.isEmpty())
        return m_options.session.screenName;
    return objectName;
}

QString TuiApplicationController::presentationText(int command, const QString &fallbackText,
                                                   const QVariant &payload) const
{
    return tuiPresentationEventText(command, fallbackText, payload,
        [this](const QString &objectName) { return resolvePlayerName(objectName); });
}

QString TuiApplicationController::renderLog() const
{
    QStringList lines{tr("== 記錄 ==")};
    const QVariantList events = m_core.state()->presentationEvents();
    QStringList rendered;
    for (const QVariant &entry : events) {
        const QVariantMap event = entry.toMap();
        const QString line = presentationText(event.value(QStringLiteral("command")).toInt(),
            event.value(QStringLiteral("text")).toString(),
            event.value(QStringLiteral("payload")));
        if (!line.isEmpty())
            rendered << TuiRenderer::sanitize(line, 1024);
    }
    lines << rendered.mid(qMax(0, rendered.size() - 30));
    return lines.join(QLatin1Char('\n'));
}

QStringList TuiApplicationController::completionExtraTokens() const
{
    QStringList tokens;
    QSet<QString> seen;
    const auto add = [&](const QString &token) {
        if (token.isEmpty() || seen.contains(token))
            return;
        seen.insert(token);
        tokens.append(token);
    };
    if (!m_core.hasActiveRequest())
        return tokens;
    const InteractionRequest &request = m_core.activeRequest();
    if (request.type == InteractionType::PlayCard) {
        add(QStringLiteral("pass"));
        add(QStringLiteral("過"));
        add(QStringLiteral("不出"));
    }
    if (request.cancelable || request.type == InteractionType::PlayCard)
        add(QStringLiteral("cancel"));
    if (const auto *value = request.payloadAs<OptionInteractionPayload>()) {
        for (const InteractionOption &option : value->options)
            add(option.value);
    }
    if (const auto *value = request.payloadAs<ChooseOrderInteractionPayload>()) {
        for (const InteractionOption &option : value->options)
            add(option.value);
    }
    if (const auto *value = request.payloadAs<TriggerOrderInteractionPayload>()) {
        for (const TriggerOrderOption &option : value->options)
            add(option.responseValue);
    }
    QStringList players;
    if (const auto *value = request.payloadAs<PlayerInteractionPayload>())
        players = value->selection.selectablePlayers;
    else if (const auto *value = request.payloadAs<CardInteractionPayload>())
        players = value->optionalTargets;
    else if (const auto *value = request.payloadAs<YijiInteractionPayload>())
        players = value->targetPlayers;
    for (int i = 0; i < players.size(); ++i) {
        add(QString::number(i + 1));
        add(players.at(i));
    }
    return tokens;
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
