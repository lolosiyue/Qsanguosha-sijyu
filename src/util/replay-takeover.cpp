#include "replay-takeover.h"
#include "replayer.h"
#include "replay-game-state.h"
#include "game-snapshot.h"
#include "recorder.h"
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
    m_startTime = QDateTime::currentDateTime();

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

    m_gameState = new ReplayGameState(this);

    QList<QPair<int, QString>> pairs;
    int currentIndex = m_replayer->getCurrentPairIndex();
    int duration = m_replayer->getDuration() * 1000;

    for (int i = 0; i <= currentIndex; i++) {
        int elapsed = m_replayer->getCurrentElapsed();
        QString cmd;
        pairs << qMakePair(elapsed, cmd);
    }

    m_gameState->rebuildFromCommands(pairs, currentIndex);
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
    ClientInstance->processServerPacket(QString("{\"command\":%1,\"body\":%2}")
        .arg(S_COMMAND_SET_KNOWN_CARDS)
        .arg(QString::fromUtf8(QJsonDocument::fromVariant(knownCardsArg).toJson())).toLatin1());
}

void ReplayTakeoverManager::processRequest(const QString &cmd)
{
    Packet packet;
    if (!packet.parse(cmd.toLatin1().constData()))
        return;

    CommandType command = packet.getCommandType();
    QVariant body = packet.getMessageBody();

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
        QVariant aiResponse = generateAIResponse(cmd, targetPlayer);
        emit requestProcessed(targetPlayer, aiResponse);
    }

    recordCommand(cmd);
}

QVariant ReplayTakeoverManager::generateAIResponse(const QString &cmd, const QString &playerName)
{
    Packet packet;
    if (!packet.parse(cmd.toLatin1().constData()))
        return QVariant();

    CommandType command = packet.getCommandType();
    QVariant body = packet.getMessageBody();

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

void ReplayTakeoverManager::onCommandParsed(const QString &cmd)
{
    if (!m_takeoverEnabled)
        return;

    Packet packet;
    if (!packet.parse(cmd.toLatin1().constData()))
        return;

    if (packet.getPacketType() == S_TYPE_REQUEST) {
        processRequest(cmd);
    } else {
        m_gameState->applyCommand(cmd);
        recordCommand(cmd);
    }
}

void ReplayTakeoverManager::onSeekFinished()
{
    if (m_takeoverEnabled) {
        initializeFromReplay();
    }
}

void ReplayTakeoverManager::recordCommand(const QString &cmd)
{
    if (!m_newRecorder)
        return;

    int elapsed = m_startTime.msecsTo(QDateTime::currentDateTime());
    QString line = QString("%1 %2\n").arg(elapsed).arg(cmd);
    m_newRecorder->recordLine(line);
    m_newCommands.append(cmd);
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