#include "replay-takeover.h"
#include "recorder.h"
#include "replay-game-state.h"
#include "game-snapshot.h"
#include "client.h"
#include "clientplayer.h"
#include "engine.h"
#include "json.h"

#include <QFile>
#include <QDir>
#include <QDateTime>

using namespace QSanProtocol;

ReplayTakeoverManager::ReplayTakeoverManager(Replayer *replayer, QObject *parent)
    : QObject(parent), m_replayer(replayer), m_gameState(nullptr), m_takeoverEnabled(false), m_startPairIndex(-1)
{
    m_newRecorder = new Recorder(this);
}

ReplayTakeoverManager::~ReplayTakeoverManager()
{
    if (m_gameState)
        delete m_gameState;
    if (m_newRecorder)
        delete m_newRecorder;
}

void ReplayTakeoverManager::setTakeoverTarget(const QString &playerName)
{
    m_takeoverTarget = playerName;
}

void ReplayTakeoverManager::enableTakeover()
{
    if (m_takeoverEnabled || m_takeoverTarget.isEmpty())
        return;

    m_takeoverEnabled = true;
    m_startPairIndex = m_replayer->getCurrentPairIndex();

    delete m_newRecorder;
    m_newRecorder = new Recorder(this);
    m_newCommands.clear();

    initializeFromReplay();
    syncHandcards(m_takeoverTarget);

    connect(m_replayer, &Replayer::command_parsed, this, &ReplayTakeoverManager::onCommandParsed);
    connect(m_replayer, &Replayer::seek_finished, this, &ReplayTakeoverManager::onSeekFinished);

    emit takeoverEnabled(m_takeoverTarget);
    emit perspectiveChanged(m_takeoverTarget);
}

void ReplayTakeoverManager::disableTakeover()
{
    if (!m_takeoverEnabled)
        return;

    m_takeoverEnabled = false;
    disconnect(m_replayer, &Replayer::command_parsed, this, &ReplayTakeoverManager::onCommandParsed);
    disconnect(m_replayer, &Replayer::seek_finished, this, &ReplayTakeoverManager::onSeekFinished);

    emit takeoverDisabled();
}

bool ReplayTakeoverManager::isTakeoverEnabled() const
{
    return m_takeoverEnabled;
}

QString ReplayTakeoverManager::getTakeoverTarget() const
{
    return m_takeoverTarget;
}

void ReplayTakeoverManager::initializeFromReplay()
{
    if (!m_replayer)
        return;

    delete m_gameState;
    m_gameState = new ReplayGameState(this);
    m_gameState->rebuildFromEvents(
        m_replayer->events(), m_replayer->getCurrentPairIndex());
}

void ReplayTakeoverManager::syncHandcards(const QString &playerName)
{
    if (!ClientInstance || playerName.isEmpty())
        return;

    ClientPlayer *target = ClientInstance->getPlayer(playerName);
    if (!target)
        return;

    PlayerSnapshot *snapshot = m_gameState->getPlayerState(playerName);
    if (!snapshot)
        return;

    JsonArray knownCardsArg;
    knownCardsArg << playerName << JsonUtils::toJsonArray(snapshot->handcards);
    ProtocolMessage message;
    message.type = ProtocolMessageType::Notification;
    message.source = ProtocolEndpoint::Room;
    message.destination = ProtocolEndpoint::Client;
    message.command = S_COMMAND_SET_KNOWN_CARDS;
    message.hasPayload = true;
    message.payload = knownCardsArg;
    ClientInstance->processReplayMessage(message);
}

void ReplayTakeoverManager::processRequest(const ProtocolMessage &message)
{
    const CommandType command = static_cast<CommandType>(message.command);
    const QVariant body = message.payload;

    QString targetPlayer;
    if (command == S_COMMAND_INVOKE_SKILL) {
        JsonArray args = body.value<JsonArray>();
        if (args.size() > 0)
            targetPlayer = args[0].toString();
    } else if (command == S_COMMAND_RESPONSE_CARD) {
        JsonArray args = body.value<JsonArray>();
        if (args.size() > 1)
            targetPlayer = args[0].toString();
    } else if (command == S_COMMAND_DISCARD_CARD) {
        JsonArray args = body.value<JsonArray>();
        if (args.size() > 0)
            targetPlayer = args[0].toString();
    }

    if (targetPlayer == m_takeoverTarget) {
        emit requestProcessed(targetPlayer, QVariant());
    } else {
        QVariant aiResponse = generateAIResponse(message, targetPlayer);
        emit requestProcessed(targetPlayer, aiResponse);
    }

    recordMessage(message);
}

QVariant ReplayTakeoverManager::generateAIResponse(
    const ProtocolMessage &message, const QString &playerName)
{
    Q_UNUSED(playerName);
    const CommandType command = static_cast<CommandType>(message.command);

    switch (command) {
    case S_COMMAND_INVOKE_SKILL:
        return false;

    case S_COMMAND_RESPONSE_CARD:
        return QString();

    case S_COMMAND_DISCARD_CARD:
        return JsonArray();

    case S_COMMAND_CHOOSE_CARD:
        return -1;

    case S_COMMAND_CHOOSE_PLAYER:
        return QString();

    case S_COMMAND_MULTIPLE_CHOICE:
        return 0;

    default:
        return QVariant();
    }
}

void ReplayTakeoverManager::onCommandParsed(const ProtocolMessage &message)
{
    if (!m_takeoverEnabled)
        return;

    if (message.type == ProtocolMessageType::Request) {
        processRequest(message);
    } else {
        m_gameState->applyMessage(message);
        recordMessage(message);
    }
}

void ReplayTakeoverManager::onSeekFinished()
{
    if (m_takeoverEnabled) {
        initializeFromReplay();
    }
}

void ReplayTakeoverManager::recordMessage(const ProtocolMessage &message)
{
    if (!m_newRecorder)
        return;

    QString error;
    if (!m_newRecorder->recordMessage(message, &error)) {
        qWarning().noquote() << "Replay takeover recording failed:" << error;
        return;
    }
    m_newCommands.append(message);
}

void ReplayTakeoverManager::saveNewReplay(const QString &filepath)
{
    if (!m_newRecorder)
        return;

    m_newRecorder->save(filepath);
}

QString ReplayTakeoverManager::generateNewReplayFilename() const
{
    QString originalPath = m_replayer->getPath();
    QFileInfo info(originalPath);
    QString baseName = info.completeBaseName();
    QString extension = info.suffix();
    QString dir = info.absolutePath();

    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString newFilename = QString("%1/%2_branch_%3.%4").arg(dir).arg(baseName).arg(timestamp).arg(extension);

    return newFilename;
}
