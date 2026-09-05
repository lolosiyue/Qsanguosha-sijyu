#include "tui-application-controller.h"
#include "tui-text.h"

#include "card.h"
#include "engine.h"
#include "general.h"
#include "protocol-interaction-request-builder.h"
#include "protocol.h"
#include "tui-card-text.h"
#include "tui-log-text.h"
#include "protocol/session/session-payloads.h"
#include "tui-play-skills.h"
#include "tui-skill-dialog.h"
#include "tui-synthesized-log.h"
#include "tui-script-runner.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>
#include <QTimer>
#include <variant>

using namespace QSanProtocol;

namespace {

// The words that walk back a half-composed play, matching the ones
// TuiInteractionView accepts for giving up on a prompt.
bool isAbandonToken(const QString &text)
{
    return text.compare(QStringLiteral("pass"), Qt::CaseInsensitive) == 0
        || text.compare(QStringLiteral("cancel"), Qt::CaseInsensitive) == 0
        || text == QStringLiteral("过") || text == QStringLiteral("過");
}

// The words that close a target list the engine would still let grow.
bool isAcceptToken(const QString &text)
{
    return text.compare(QStringLiteral("ok"), Qt::CaseInsensitive) == 0
        || text == QStringLiteral("确定") || text == QStringLiteral("確定");
}

bool isSkillActivation(const InteractionResponse &response)
{
    const auto *cards = std::get_if<InteractionResponse::CardSelectionData>(&response.payload);
    return cards != nullptr && !cards->activationSkillName.isEmpty()
        && cards->targets.isEmpty();
}

} // namespace

TuiApplicationController::TuiApplicationController(const TuiApplicationOptions &options,
    QObject *parent)
    : QObject(parent), m_options(options), m_roomContext(m_core.state()),
      m_players(m_core.state()), m_session(&m_core, this),
      m_renderer(options.ansiEnabled,
          TuiRenderer::Resolvers{
              [this](int cardId) { return resolveCardDisplayText(cardId); },
              [this](const QString &name) { return resolveNameText(name); },
              [this](const QString &objectName) { return resolvePlayerName(objectName); },
              [](const QString &general) { return resolveGeneralKingdom(general); },
              [this](int cardId) { return resolveCardHint(cardId); },
              [this](const QString &objectName) { return resolvePlayerHint(objectName); },
              [this](int cardId) { return resolveCardTargets(cardId); },
              [this](int cardId) { return resolveHandCardHint(cardId); },
              [this](const QString &skillName, int instanceId) {
                  return resolveSkillHint(skillName, instanceId);
              }}),
      m_view(&m_renderer, [this](const QString &text) { writeOutput(text); },
             [this](int cardId) { return resolveCardWireText(cardId); },
             [this](const QString &skillName, int instanceId, const QList<int> &subcards,
                    QString *error) {
                 return resolveSkillCardWireText(skillName, instanceId, subcards, error);
             },
             [this](const QString &skillName, const QString &declaration, QString *error) {
                 return applySkillDeclaration(skillName, declaration, error);
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
    m_roomContext.setOwnerResolver(
        [this](int cardId) { return m_players.cardOwner(cardId); });
    connect(&m_session, &ClientLiveSession::connectionChanged, this,
        [this](const QString &state) {
            // The session reports the wire token; the player reads the label.
            writeOutput(tuiText("tui_event_connection").arg(m_renderer.nameText(state)));
        });
    connect(&m_session, &ClientLiveSession::sessionActive, this, [this](bool reconnected) {
        writeOutput(reconnected ? tuiText("tui_event_reconnected")
                                : tuiText("tui_event_session_ready"));
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
                writeError(tuiText("tui_event_trust_restore_failed").arg(error));
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
    connect(&m_session, &ClientLiveSession::frontendMessageReceived, this,
        [this](const ProtocolMessage &message) {
            // The room has to be registered before anything renders a card, so
            // it goes first: card text resolves through it.
            m_roomContext.applyMessage(message);
            // Cards resolve through the room, so the room goes first and the
            // players -- which reach for their equipment through it -- second.
            m_players.sync();
            if (!m_core.hasActiveRequest()) {
                m_roomContext.setCardUseContext(
                    CardUseStruct::CARD_USE_REASON_UNKNOWN, QString());
                m_hintType = InteractionType::None;
                m_hintReason = CardUseStruct::CARD_USE_REASON_UNKNOWN;
                m_skillReason = CardUseStruct::CARD_USE_REASON_UNKNOWN;
                m_hintPattern.clear();
            }
            appendSynthesizedLogs(message);
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
            // The prompt's own pattern decides which skills are on offer, so
            // it has to be read before the list is built.
            m_skillReason = CardUseStruct::CARD_USE_REASON_UNKNOWN;
            if (auto *cards = std::get_if<CardInteractionPayload>(&request.payload)) {
                m_skillReason = tuiSkillPromptReason(request.type,
                    cards->selection.handlingMethod, cards->selection.pattern);
                if (m_skillReason != CardUseStruct::CARD_USE_REASON_UNKNOWN)
                    fillSkillCandidates(request.type, cards);
            }
            if (request.timeoutMs <= 0) {
                const int seconds = m_core.state()->setup().value(
                    QStringLiteral("operation_timeout")).toInt();
                if (seconds > 0)
                    request.timeoutMs = static_cast<qint64>(seconds) * 1000 + 5000;
            }
            // Legality questions the renderer asks while the prompt is up read
            // the reason and pattern off the room, so set them before the
            // request goes live.
            CardUseStruct::CardUseReason reason = CardUseStruct::CARD_USE_REASON_UNKNOWN;
            QString usePattern;
            if (const auto *cards = std::get_if<CardInteractionPayload>(&request.payload)) {
                usePattern = cards->selection.pattern;
                if (request.type == InteractionType::PlayCard)
                    usePattern.clear();
                // The same table the skill list is built from. Ask-for-peach
                // and nullification name no handling method on the wire, and
                // both are presented as a use, which a second rule derived from
                // the wire alone got wrong.
                reason = tuiSkillPromptReason(request.type, cards->selection.handlingMethod,
                                              cards->selection.pattern);
                if (reason == CardUseStruct::CARD_USE_REASON_UNKNOWN) {
                    // Prompts that offer no skills still need a reason for the
                    // card hints to read.
                    reason = cards->selection.handlingMethod == Card::MethodUse
                        ? CardUseStruct::CARD_USE_REASON_RESPONSE_USE
                        : CardUseStruct::CARD_USE_REASON_RESPONSE;
                }
            }
            m_roomContext.setCardUseContext(reason, usePattern);
            m_hintType = request.type;
            m_hintReason = reason;
            m_hintPattern = usePattern;
            m_hintTargets.clear();
            if (const auto *cards = std::get_if<CardInteractionPayload>(&request.payload))
                m_hintTargets = cards->optionalTargets;
            // A prompt the server replaced abandons whatever the player was
            // half way through composing.
            clearPendingActivation();
            if (m_core.beginRequest(std::move(request)) == 0) {
                writeError(tuiText("tui_error_begin_request"));
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
                *error = tuiText("tui_error_log_open").arg(m_log.errorString());
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

    writeOutput(tuiText("tui_banner"));
    m_session.connectToServer(m_options.session);
    return true;
}

const Card *TuiApplicationController::answerCard(const InteractionResponse &response) const
{
    if (Sanguosha == nullptr)
        return nullptr;
    const auto *cards = std::get_if<InteractionResponse::CardSelectionData>(&response.payload);
    if (cards == nullptr)
        return nullptr;
    // A card the player picked outright is the room's own copy.
    if (cards->cardIds.size() == 1)
        return Sanguosha->getCard(cards->cardIds.first());
    // Anything else is a card the view just composed. Card::Parse() would
    // rebuild it, but it ends in Card::deleteLater() -> drain(), which reaps
    // pending engine cards and leaves Lua skills indexing a nil card, so use
    // the instance the resolver kept instead. Nothing to say without one.
    return cards->activationSkillName.isEmpty() ? nullptr : m_builtSkillCard;
}

TuiPlayerLookup TuiApplicationController::playerLookup() const
{
    return [this](const QString &name) -> const Player * { return m_players.player(name); };
}

bool TuiApplicationController::targetGateOpen() const
{
    return m_hintType == InteractionType::PlayCard
        || (m_hintType == InteractionType::ResponseCard
            && m_hintReason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE);
}

TuiTargetStep TuiApplicationController::targetStep(const Card *card,
                                                   const QStringList &chosen) const
{
    return tuiTargetStep(card, chosen, m_hintTargets, playerLookup(), m_players.self());
}

bool TuiApplicationController::checkPlayAnswer(const InteractionResponse &response,
                                               QString *error, bool *incomplete) const
{
    if (incomplete != nullptr)
        *incomplete = false;
    const Card *card = answerCard(response);
    const auto *cards = std::get_if<InteractionResponse::CardSelectionData>(&response.payload);
    if (card == nullptr || cards == nullptr)
        return true;

    const QString reason = tuiValidateTargets(card, cards->targets, playerLookup(),
        m_players.self(),
        [this](const QString &name) { return m_renderer.nameText(name); },
        [this](const QString &name) { return resolvePlayerName(name); },
        incomplete);
    if (reason.isEmpty())
        return true;
    if (error != nullptr)
        *error = reason;
    return false;
}

bool TuiApplicationController::beginTargetStage(const QString &firstLine,
                                                const InteractionResponse &response)
{
    // Whatever the player already aimed at stays aimed at: the line is cut at
    // the arrow so stage two can put the whole list back after it.
    m_pending.firstLine = firstLine.section(QStringLiteral("->"), 0, 0).trimmed();
    m_pending.chosen.clear();
    if (resumeTargetStage(response))
        return true;
    m_pending.firstLine.clear();
    return false;
}

void TuiApplicationController::printTargetStage(const Card *card,
                                                const TuiTargetStep &step)
{
    const QString shown = m_renderer.nameText(card->objectName());
    QStringList lines;
    if (m_pending.chosen.isEmpty()) {
        lines << tuiText("tui_target_stage_header").arg(shown);
    } else {
        QStringList picked;
        for (const QString &name : m_pending.chosen)
            picked << resolvePlayerName(name);
        lines << tuiText("tui_target_stage_more").arg(shown, picked.join(QStringLiteral("、")));
    }
    for (const QString &name : step.candidates) {
        const qsizetype index = m_hintTargets.indexOf(name);
        QString note = resolvePlayerHint(name);
        const int votes = step.maxVotes.value(name, 1);
        if (votes > 1)
            note += tuiText("tui_target_stage_votes").arg(votes);
        lines << tuiText("tui_target_stage_line").arg(index >= 0 ? index + 1 : 0)
            .arg(resolvePlayerName(name), note);
    }
    // A list that is already a legal play can be sent as it stands; one that is
    // not has nothing to offer but more targets.
    lines << tuiText(step.feasible ? "tui_target_stage_hint_done" : "tui_target_stage_hint");
    writeOutput(lines.join(QLatin1Char('\n')));
}

bool TuiApplicationController::resumeTargetStage(const InteractionResponse &response)
{
    const Card *card = answerCard(response);
    const auto *cards = std::get_if<InteractionResponse::CardSelectionData>(&response.payload);
    if (card == nullptr || cards == nullptr)
        return false;
    const TuiTargetStep step = targetStep(card, cards->targets);
    if (!step.known || step.fixed || step.candidates.isEmpty())
        return false;
    m_pending.active = true;
    m_pending.chosen = cards->targets;
    printTargetStage(card, step);
    return true;
}

void TuiApplicationController::clearPendingActivation()
{
    m_pending = PendingActivation{};
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
        writeError(tuiText("tui_no_request_hint"));
        return;
    }

    QString answer = input;
    // A leading "!" waives the local check. The engine's verdict is advice
    // built from a client-side copy of the table; if it ever disagrees with the
    // server there has to be a way through, or a wrong hint becomes a wall.
    const bool forced = answer.startsWith(QLatin1Char('!'));
    if (forced)
        answer = answer.mid(1).trimmed();

    const bool wasPending = m_pending.active;
    bool accepted = false;
    if (wasPending) {
        if (isAbandonToken(answer)) {
            clearPendingActivation();
            writeOutput(tuiText("tui_activation_abandoned"));
            return;
        }
        // Stage two replays the player's own first line with every target
        // picked so far, so there is exactly one answer parser and one place
        // that decides what is legal.
        accepted = isAcceptToken(answer);
        static const QRegularExpression whitespace(QStringLiteral("\\s+"));
        QStringList targets = m_pending.chosen;
        if (!accepted)
            targets += answer.split(whitespace, Qt::SkipEmptyParts);
        if (targets.isEmpty()) {
            writeError(tuiText("tui_target_stage_empty"));
            return;
        }
        answer = tuiText("tui_replay_line").arg(m_pending.firstLine,
                                                targets.join(QLatin1Char(' ')));
        m_pending.active = false;
    }

    InteractionResponse response;
    QString error;
    if (!m_view.parseAnswer(m_core.activeRequest(), answer, &response, &error)) {
        writeError(error);
        m_pending.active = wasPending;
        return;
    }

    if (!forced && targetGateOpen()) {
        QString reason;
        bool incomplete = false;
        if (!checkPlayAnswer(response, &reason, &incomplete)) {
            // A skill card is composed before its targets are known, so the
            // first thing a bare activation needs is not a complaint but the
            // list of places it can go.
            // A card that has some targets but not enough is in the same
            // position: what it needs is the rest of the menu, not a refusal.
            if (!wasPending && (isSkillActivation(response) || incomplete)
                && beginTargetStage(input, response)) {
                return;
            }
            // Half way through a target list, "not finished" is not a mistake:
            // keep what was accepted and ask for the rest.
            if (wasPending && incomplete && resumeTargetStage(response))
                return;
            m_pending.active = wasPending;
            writeError(tuiText("tui_play_rejected").arg(reason));
            if (wasPending) {
                // The refused token is dropped; whatever was already accepted
                // stays, with its menu printed again.
                const Card *card = answerCard(response);
                if (card != nullptr)
                    printTargetStage(card, targetStep(card, m_pending.chosen));
            }
            return;
        }
        // Legal so far, but a card that could still take another target waits
        // for the player to say it is done rather than firing on the first
        // one that happens to be feasible.
        if (wasPending && !accepted && resumeTargetStage(response))
            return;
    }

    clearPendingActivation();
    if (!m_session.submitInteractionResponse(std::move(response), &error))
        writeError(error.isEmpty() ? tuiText("tui_answer_rejected") : error);
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

    writeError(error.isEmpty() ? tuiText("tui_role_skip_failed") : error);
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
        writeError(tuiText("tui_command_readonly_only"));
        return;
    }
    if (intent.type == TuiCommandType::Help) {
        writeOutput(tuiText("tui_help"));
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
            writeError(tuiText("tui_no_request"));
            return;
        }
        InteractionResponse response = InteractionResponse::makeCancel(m_core.activeRequestId());
        response.command = m_core.activeRequest().command;
        QString error;
        if (!m_session.submitInteractionResponse(std::move(response), &error))
            writeError(error.isEmpty() ? tuiText("tui_request_not_cancellable") : error);
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

void TuiApplicationController::fillSkillCandidates(InteractionType type,
    CardInteractionPayload *payload) const
{
    // The play phase has no pattern of its own; anywhere else the server's
    // pattern may name the one skill it will take an answer from.
    tuiFillSkillCandidates(*m_core.state(),
        type == InteractionType::PlayCard ? QString() : payload->selection.pattern, payload);
}

QStringList TuiApplicationController::bannedPackages() const
{
    return m_core.state()->setup().value(QStringLiteral("ban_packages")).toStringList();
}

QString TuiApplicationController::resolveSkillHint(const QString &skillName, int instanceId) const
{
    if (m_skillReason == CardUseStruct::CARD_USE_REASON_UNKNOWN)
        return QString();
    const QString hint = tuiSkillActivationHint(skillName, instanceId, m_skillReason,
                                                m_hintPattern);
    if (!hint.isEmpty())
        return hint;
    // A skill whose dialog is open needs one more word from the player than
    // the menu number, so the listing has to say so before they type it.
    return tuiSkillNeedsDeclaration(skillName, bannedPackages()) ? tuiText("tui_hint_declaration_required") : QString();
}

bool TuiApplicationController::applySkillDeclaration(const QString &skillName,
    const QString &declaration, QString *error) const
{
    return tuiApplySkillDeclaration(skillName, declaration, bannedPackages(), error);
}

QString TuiApplicationController::resolveSkillCardWireText(const QString &skillName,
    int instanceId, const QList<int> &subcardIds, QString *error) const
{
    // Keep the card the wire text was made from: the legality checks below run
    // in this same event handler and would otherwise have to parse the string
    // back, which drains the engine's card lifetime manager.
    m_builtSkillCard = nullptr;
    return tuiResolveSkillCardWireText(m_core.state()->selfName(), skillName, instanceId,
                                       subcardIds, error, &m_builtSkillCard);
}

QString TuiApplicationController::resolveCardDisplayText(int cardId) const
{
    return tuiCardDisplayText(cardId);
}

QString TuiApplicationController::resolveGeneralKingdom(const QString &generalName)
{
    if (Sanguosha == nullptr || generalName.isEmpty())
        return QString();
    const General *general = Sanguosha->getGeneral(generalName);
    return general == nullptr ? QString() : general->getKingdom();
}

QString TuiApplicationController::resolveCardHint(int cardId) const
{
    if (Sanguosha == nullptr)
        return QString();
    const ClientPlayer *self = m_players.self();
    // getCard() only answers inside a game; before one there is nothing to say.
    const Card *card = Sanguosha->getCard(cardId);
    if (self == nullptr || card == nullptr)
        return QString();

    // Same questions RoomScene::enableTargets() asks, in the same order.
    if (m_hintType == InteractionType::PlayCard) {
        if (self->isCardLimited(card, Card::MethodUse))
            return tuiText("tui_hint_restricted");
        return card->isAvailable(self) ? QString() : tuiText("tui_hint_unavailable");
    }
    if (m_hintType == InteractionType::DiscardCard) {
        return self->isCardLimited(card, Card::MethodDiscard) ? tuiText("tui_hint_restricted") : QString();
    }

    Card::HandlingMethod method = card->getHandlingMethod();
    if (m_hintReason == CardUseStruct::CARD_USE_REASON_RESPONSE
        && method == Card::MethodUse) {
        method = Card::MethodResponse;
    }
    if (self->isCardLimited(card, method))
        return tuiText("tui_hint_restricted");
    QString pattern = m_hintPattern;
    if (pattern.endsWith(QLatin1Char('!')))
        pattern.chop(1);
    if (pattern.isEmpty() || pattern == QLatin1String("."))
        return QString();
    return Sanguosha->matchPattern(pattern, self, card) ? QString() : tuiText("tui_hint_mismatch");
}

TuiRenderer::CardTargets TuiApplicationController::cardTargetAdvice(const Card *card) const
{
    // Nothing selected yet is exactly the question RoomScene asks to decide
    // which photos light up before the first click. The candidate set is the
    // one the server offered, so a target the prompt withheld never appears
    // just because the engine would allow it.
    const TuiTargetStep step = targetStep(card, {});
    TuiRenderer::CardTargets advice;
    advice.known = step.known;
    advice.targetFixed = step.fixed;
    advice.targets = step.candidates;
    advice.maxVotes = step.maxVotes;
    return advice;
}

TuiRenderer::CardTargets TuiApplicationController::resolveCardTargets(int cardId) const
{
    if (Sanguosha == nullptr)
        return {};
    return cardTargetAdvice(Sanguosha->getCard(cardId));
}

QString TuiApplicationController::resolveHandCardHint(int cardId) const
{
    if (Sanguosha == nullptr)
        return QString();
    const ClientPlayer *self = m_players.self();
    const Card *card = Sanguosha->getCard(cardId);
    if (self == nullptr || card == nullptr)
        return QString();
    if (self->isCardLimited(card, Card::MethodUse))
        return tuiText("tui_hand_hint_restricted");
    return card->isAvailable(self) ? tuiText("tui_hand_hint_available") : QString();
}

QString TuiApplicationController::resolvePlayerHint(const QString &objectName) const
{
    const ClientPlayer *self = m_players.self();
    const ClientPlayer *target = m_players.player(objectName);
    if (self == nullptr || target == nullptr || self == target)
        return QString();
    const int distance = self->distanceTo(target);
    return distance > 0 ? tuiText("tui_hand_hint_distance").arg(distance) : QString();
}

QString TuiApplicationController::resolveNameText(const QString &name) const
{
    if (name.isEmpty())
        return name;
    static const QRegularExpression sgsName(
        QStringLiteral("^sgs\\d+$"),
        QRegularExpression::UseUnicodePropertiesOption);
    if (sgsName.match(name).hasMatch())
        return resolveLogPlayerName(name);
    if (Sanguosha == nullptr)
        return name;
    const QString translated = Sanguosha->translate(name);
    return translated.isEmpty() ? name : translated;
}

QString TuiApplicationController::renderPiles() const
{
    const QVariantMap player = m_core.state()->player(m_core.state()->selfName());
    const QVariantMap piles = player.value(QStringLiteral("piles")).toMap();
    QStringList lines{tuiText("tui_piles_header")};
    for (auto it = piles.constBegin(); it != piles.constEnd(); ++it)
        lines << tuiText("tui_pile_line").arg(TuiRenderer::sanitize(it.key(), 128))
            .arg(it.value().toList().size());
    lines << tuiText("tui_pile_summary")
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
    return tuiText("tui_skills_section").arg(TuiRenderer::sanitize(
        translated.join(QLatin1Char(' ')), 2048));
}

QString TuiApplicationController::renderEquipment() const
{
    QStringList lines{tuiText("tui_equipment_header")};
    const QList<int> equipment = m_core.state()->cardsForPlayer(
        m_core.state()->selfName(), 1);
    for (int cardId : equipment)
        lines << QStringLiteral("ID=%1 %2").arg(cardId)
            .arg(TuiRenderer::sanitize(resolveCardDisplayText(cardId), 512));
    if (equipment.isEmpty())
        lines << tuiText("tui_empty");
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

QString TuiApplicationController::resolveLogPlayerName(const QString &objectName) const
{
    return tuiResolveLogPlayerName(*m_core.state(), objectName);
}

void TuiApplicationController::appendSynthesizedLogs(const ProtocolMessage &message)
{
    tuiAppendSynthesizedLogs(m_core.state(), &m_renPile, message,
        [this](const QString &line) { writeOutput(line); });
}

QString TuiApplicationController::presentationText(int command, const QString &fallbackText,
                                                   const QVariant &payload) const
{
    const TuiPlayerNameResolver names = [this, command](const QString &objectName) {
        return command == S_COMMAND_SPEAK ? resolvePlayerName(objectName)
                                          : resolveLogPlayerName(objectName);
    };
    return tuiPresentationEventText(command, fallbackText, payload, names);
}

QString TuiApplicationController::renderLog() const
{
    QStringList lines{tuiText("tui_log_header")};
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
        add(QStringLiteral("过"));
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
