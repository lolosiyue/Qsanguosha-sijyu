#include "extra-turn-scheduler.h"

#include "engine.h"
#include "gamerule.h"
#include "room.h"
#include "roomthread.h"
#include "serverplayer.h"

#include <algorithm>
#include <functional>

namespace {

bool failRestore(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

class ScopedCallback
{
public:
    explicit ScopedCallback(const std::function<void()> &callback)
        : m_callback(callback)
    {
    }

    ~ScopedCallback()
    {
        if (m_callback)
            m_callback();
    }

private:
    std::function<void()> m_callback;
};

}

ExtraTurnScheduler::ExtraTurnScheduler(Room &room)
    : m_room(room), m_processing(false)
{
}

int ExtraTurnScheduler::schedule(ServerPlayer *player, const QString &reason,
                                 QList<Player::Phase> phases, int times)
{
    if (!player || player->getRoom() != &m_room || !player->isAlive()
        || player->isRemoved() || times <= 0)
        return 0;

    Request request;
    request.player = player;
    request.phases = phases;
    request.reason = reason;
    for (int i = 0; i < times; ++i)
        m_requests << request;
    return times;
}

int ExtraTurnScheduler::schedule(ServerPlayer *player, const SkillInstanceRef &sourceRef,
                                 QList<Player::Phase> phases, int times)
{
    if (!player || player->getRoom() != &m_room || !player->isAlive()
        || player->isRemoved() || times <= 0)
        return 0;

    Request request;
    request.player = player;
    request.phases = phases;
    request.reason = sourceRef.key.skillName;
    request.sourceRef = sourceRef;
    for (int i = 0; i < times; ++i)
        m_requests << request;
    return times;
}

bool ExtraTurnScheduler::isCurrentExtraTurn() const
{
    return !m_contexts.isEmpty();
}

QString ExtraTurnScheduler::currentReason() const
{
    return m_contexts.isEmpty() ? QString() : m_contexts.last().reason;
}

SkillInstanceRef ExtraTurnScheduler::currentSourceRef() const
{
    return m_contexts.isEmpty() ? SkillInstanceRef() : m_contexts.last().sourceRef;
}

QList<ExtraTurnScheduler::SnapshotRequest> ExtraTurnScheduler::pendingRequestsSnapshot() const
{
    QList<SnapshotRequest> snapshot;
    snapshot.reserve(m_requests.size());
    for (const Request &request : m_requests) {
        SnapshotRequest item;
        if (request.player)
            item.playerObjectName = request.player->objectName();
        item.phases.reserve(request.phases.size());
        for (Player::Phase phase : request.phases)
            item.phases << static_cast<int>(phase);
        item.reason = request.reason;
        item.sourceRef = request.sourceRef;
        snapshot << item;
    }
    return snapshot;
}

bool ExtraTurnScheduler::restorePendingRequests(const QList<SnapshotRequest> &requests,
                                                const PlayerResolver &resolver,
                                                QString *error)
{
    if (m_processing)
        return failRestore(error, QStringLiteral("cannot restore extra turns while processing"));
    if (!resolver)
        return failRestore(error, QStringLiteral("extra-turn player resolver is missing"));

    QList<Request> restored;
    restored.reserve(requests.size());
    for (const SnapshotRequest &item : requests) {
        if (item.playerObjectName.isEmpty())
            return failRestore(error, QStringLiteral("extra-turn request has no player"));

        ServerPlayer *player = resolver(item.playerObjectName);
        if (!player || player->getRoom() != &m_room)
            return failRestore(error, QStringLiteral("extra-turn request has an invalid player: %1")
                               .arg(item.playerObjectName));

        QList<Player::Phase> phases;
        phases.reserve(item.phases.size());
        for (int phaseValue : item.phases) {
            if (phaseValue < static_cast<int>(Player::RoundStart)
                || phaseValue > static_cast<int>(Player::Finish))
                return failRestore(error, QStringLiteral("extra-turn request has an invalid phase"));
            phases << static_cast<Player::Phase>(phaseValue);
        }

        const SkillInstanceRef &sourceRef = item.sourceRef;
        const bool hasSourceRef = !sourceRef.ownerObjectName.isEmpty()
            || !sourceRef.key.skillName.isEmpty() || sourceRef.key.instanceID != 0;
        if (hasSourceRef && !sourceRef.isValid())
            return failRestore(error, QStringLiteral("extra-turn request has an invalid skill reference"));
        if (sourceRef.isValid()) {
            ServerPlayer *owner = resolver(sourceRef.ownerObjectName);
            if (!owner || owner->getRoom() != &m_room
                || !owner->findSkillInstance(sourceRef.key.skillName, sourceRef.key.instanceID))
                return failRestore(error, QStringLiteral("extra-turn request skill owner is invalid"));
        }

        Request request;
        request.player = player;
        request.phases = phases;
        request.reason = item.reason;
        request.sourceRef = sourceRef;
        restored << request;
    }

    m_requests = restored;
    return true;
}

void ExtraTurnScheduler::restoreRequests(const QList<Request> &requests)
{
    if (requests.isEmpty()) return;
    QList<Request> restored = requests;
    restored.append(m_requests);
    m_requests = restored;
}

void ExtraTurnScheduler::process()
{
    if (m_processing || m_room.isFinished() || m_requests.isEmpty())
        return;

    m_processing = true;
    ScopedCallback resetProcessing([this]() { m_processing = false; });

    while (!m_requests.isEmpty() && !m_room.isFinished()) {
        QList<Request> batch = m_requests;
        m_requests.clear();

        const QList<ServerPlayer *> actionOrder = m_room.getAllPlayers(true);
        std::stable_sort(batch.begin(), batch.end(),
            [&actionOrder](const Request &left, const Request &right) {
                if (left.player == right.player) return false;
                int leftIndex = actionOrder.indexOf(left.player);
                int rightIndex = actionOrder.indexOf(right.player);
                if (leftIndex < 0) leftIndex = actionOrder.length();
                if (rightIndex < 0) rightIndex = actionOrder.length();
                return leftIndex < rightIndex;
            });

        for (int i = 0; i < batch.length(); ++i) {
            if (m_room.isFinished()) break;
            const Request &request = batch.at(i);
            if (!request.player || request.player->getRoom() != &m_room
                || !request.player->isAlive() || request.player->isRemoved())
                continue;

            try {
                execute(request.player, request.phases, request.reason, request.sourceRef);
            } catch (TriggerEvent controlEvent) {
                if (controlEvent == TurnBroken)
                    continue;

                restoreRequests(batch.mid(i + 1));
                throw controlEvent;
            }
        }
    }
}

void ExtraTurnScheduler::execute(ServerPlayer *player, QList<Player::Phase> phases,
                                 const QString &reason, const SkillInstanceRef &sourceRef)
{
    if (!player || player->getRoom() != &m_room) return;

    if (phases.isEmpty())
        phases << Player::RoundStart << Player::Start << Player::Judge << Player::Draw
               << Player::Play << Player::Discard << Player::Finish;

    QVariantList phaseData;
    foreach (Player::Phase phase, phases)
        phaseData << static_cast<int>(phase);

    const QString globalTag = "Global_ExtraTurn" + player->objectName();
    const QVariant previousGlobalTag = m_room.getTag(globalTag);
    const QVariant previousPhases = player->getTag("extraTurnPhases");
    const int previousExtraTurnMark = player->getMark("@extra_turn");
    ServerPlayer *previousCurrent = m_room.getCurrent();

    Context context;
    context.player = player;
    context.reason = reason;
    context.sourceRef = sourceRef;
    m_contexts << context;

    m_room.setCurrent(player);
    player->setTag("extraTurnPhases", phaseData);
    m_room.setTag(globalTag, true);

    bool restored = false;
    auto restoreState = [&]() {
        if (restored) return;
        restored = true;

        if (previousGlobalTag.isValid()) m_room.setTag(globalTag, previousGlobalTag);
        else m_room.removeTag(globalTag);

        if (previousPhases.isValid()) player->setTag("extraTurnPhases", previousPhases);
        else player->removeTag("extraTurnPhases");

        if (player->getMark("@extra_turn") != previousExtraTurnMark)
            m_room.setPlayerMark(player, "@extra_turn", previousExtraTurnMark);
        if (!m_contexts.isEmpty())
            m_contexts.removeLast();
        m_room.setCurrent(previousCurrent);
    };

    try {
        m_room.getThread()->trigger(TurnStart, &m_room, player);
    } catch (TriggerEvent controlEvent) {
        if (controlEvent == TurnBroken && player->getPhase() != Player::NotActive) {
            try {
                QString gameRule = m_room.getMode() == "04_1v3" ? "hulaopass_mode" : "game_rule";
                const GameRule *rule = qobject_cast<const GameRule *>(Sanguosha->getSkill(gameRule));
                if (rule) rule->trigger(EventPhaseEnd, &m_room, player);
                player->changePhase(player->getPhase(), Player::NotActive);
            } catch (TriggerEvent) {
                // The original control event remains authoritative.
            }
        }
        restoreState();
        throw controlEvent;
    } catch (...) {
        restoreState();
        throw;
    }

    restoreState();
}
