#include "game-state-contract.h"

#include <QMetaType>
#include <cmath>
#include <exception>

namespace GameState {
namespace {
bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}
bool tree(const QVariant &value, const WorldState &world, QString *error, int depth)
{
    if (depth > 64) return fail(error, QStringLiteral("managed value nesting exceeds 64"));
    switch (value.userType()) {
    case QMetaType::UnknownType:
    case QMetaType::Bool:
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::QString: return true;
    case QMetaType::Double:
        return std::isfinite(value.toDouble()) || fail(error, QStringLiteral("non-finite state number"));
    case QMetaType::QVariantList:
        for (const auto &entry : value.toList())
            if (!tree(entry, world, error, depth + 1)) return false;
        return true;
    case QMetaType::QVariantMap: {
        const QVariantMap map = value.toMap();
        if (map.contains(QStringLiteral("$ref"))) {
            if (map.size() != 1 || map.value("$ref").userType() != QMetaType::QVariantMap)
                return fail(error, QStringLiteral("malformed stable reference"));
            const QVariantMap ref = map.value("$ref").toMap();
            if (ref.size() != 2 || ref.value("kind").userType() != QMetaType::QString
                || ref.value("id").userType() != QMetaType::QString)
                return fail(error, QStringLiteral("malformed stable reference fields"));
            const QString kind = ref.value("kind").toString();
            const QString id = ref.value("id").toString();
            const bool found = (kind == "player" && world.players.contains(id))
                || (kind == "card" && world.cards.contains(id))
                || (kind == "skill" && world.skills.contains(id));
            return found || fail(error, QStringLiteral("unresolved stable reference %1:%2").arg(kind, id));
        }
        for (const auto &entry : map)
            if (!tree(entry, world, error, depth + 1)) return false;
        return true;
    }
    default: return fail(error, QStringLiteral("unmanaged QVariant type: %1").arg(value.userType()));
    }
}
bool nonemptyUnique(const QStringList &items)
{
    QSet<QString> seen;
    for (const auto &item : items) {
        if (item.isEmpty() || seen.contains(item)) return false;
        seen.insert(item);
    }
    return true;
}
}

QStringList requiredDomains()
{
    return {"players", "cards", "skills", "tags", "turn", "rng", "history",
            "providers", "packages", "requests", "timers", "continuations"};
}

QVariantMap reference(const QString &kind, const QString &id)
{
    return {{"$ref", QVariantMap{{"kind", kind}, {"id", id}}}};
}

bool validateValue(const QVariant &value, const WorldState &world, QString *error)
{
    return tree(value, world, error, 0);
}

bool ProviderRegistry::registerProvider(const ProviderContract &contract, QString *error)
{
    if (contract.id.isEmpty() || contract.version <= 0 || contract.audit.isEmpty()
        || m_providers.contains(contract.id) || !nonemptyUnique(contract.skillDefinitions))
        return fail(error, QStringLiteral("provider requires unique ID, version and explicit audit"));
    for (const auto &existing : m_providers) {
        for (const auto &definition : contract.skillDefinitions)
            if (existing.skillDefinitions.contains(definition))
                return fail(error, QStringLiteral("skill definition has multiple provider owners"));
    }
    m_providers.insert(contract.id, contract);
    return true;
}

bool ProviderRegistry::registerNativePackage(const QString &name, const QString &version,
                                             const QString &audit, QString *error)
{
    if (name.isEmpty() || version.isEmpty() || audit.isEmpty() || m_packages.contains(name))
        return fail(error, QStringLiteral("native package requires unique name, version and explicit audit"));
    m_packages.insert(name, version);
    return true;
}

bool validateWorld(const WorldState &world, const ProviderRegistry &registry, QString *error)
{
    if (world.version != SchemaVersion)
        return fail(error, QStringLiteral("unsupported world schema version"));
    if (world.rootGameId.isEmpty() || world.worldId.isEmpty())
        return fail(error, QStringLiteral("missing logical game/world identity"));
    QStringList domains = world.completeDomains;
    QStringList required = requiredDomains();
    domains.sort(); required.sort();
    if (domains != required)
        return fail(error, QStringLiteral("incomplete or unknown state domain inventory"));
    if (!world.unsupportedCapabilities.isEmpty())
        return fail(error, QStringLiteral("unsupported state: %1").arg(world.unsupportedCapabilities.join(", ")));
    if (world.nativePackages != registry.packages())
        return fail(error, QStringLiteral("native package inventory/version is not explicitly audited"));
    if (!world.turn.continuations.isEmpty() || !world.turn.pendingExtraTurns.isEmpty()
        || !world.turn.pendingRequests.isEmpty() || !world.turn.timers.isEmpty())
        return fail(error, QStringLiteral("executor requires quiescent boundary: pending continuation/extra turn/request/timer"));
    if (!world.gameplayRng.isValid() || !world.aiRng.isValid())
        return fail(error, QStringLiteral("unsupported RNG algorithm"));
    if (world.players.isEmpty() || !nonemptyUnique(world.seatOrder)
        || world.seatOrder.size() != world.players.size())
        return fail(error, QStringLiteral("invalid player/seat inventory"));
    for (const auto &id : world.seatOrder)
        if (!world.players.contains(id)) return fail(error, QStringLiteral("seat references unknown player"));
    for (auto it = world.players.cbegin(); it != world.players.cend(); ++it) {
        const auto &p = it.value();
        if (it.key().isEmpty() || p.id != it.key() || p.maxHp < 0 || p.armor < 0 || p.hp > p.maxHp)
            return fail(error, QStringLiteral("invalid player identity/health"));
        if (!validateValue(p.tags, world, error) || !validateValue(p.properties, world, error)) return false;
    }
    const QStringList phases = {"RoundStart", "Start", "Judge", "Draw", "Play", "Discard",
                                "Finish", "NotActive", "PhaseNone"};
    if (!world.players.contains(world.turn.playerId) || !phases.contains(world.turn.phase)
        || world.turn.turnScopeId.isEmpty() || world.turn.roundScopeId.isEmpty())
        return fail(error, QStringLiteral("invalid turn/round scope"));
    for (auto it = world.cards.cbegin(); it != world.cards.cend(); ++it) {
        if (it.key().isEmpty() || it.key() != it->id || it->definition.isEmpty())
            return fail(error, QStringLiteral("invalid card identity/definition"));
        if (!validateValue(it->state, world, error)) return false;
    }
    QSet<QString> zoneIds, locatedCards;
    QMap<int, QMap<QString, QSet<QString>>> zoneRoles;
    for (const auto &zone : world.zones) {
        if (zone.id.isEmpty() || zoneIds.contains(zone.id))
            return fail(error, QStringLiteral("duplicate/empty card zone ID"));
        zoneIds.insert(zone.id);
        bool personal = false;
        switch (zone.kind) {
        case ZoneKind::Hand: case ZoneKind::Equip: case ZoneKind::Judge: case ZoneKind::Private:
            personal = true; break;
        case ZoneKind::Draw: case ZoneKind::Discard: case ZoneKind::Table: case ZoneKind::Void: break;
        default: return fail(error, QStringLiteral("unknown card zone kind"));
        }
        if ((personal && !world.players.contains(zone.ownerId)) || (!personal && !zone.ownerId.isEmpty())
            || (zone.kind == ZoneKind::Private && zone.name.isEmpty())
            || (zone.kind != ZoneKind::Private && !zone.name.isEmpty()))
            return fail(error, QStringLiteral("invalid zone owner/name"));
        auto &names = zoneRoles[int(zone.kind)][zone.ownerId];
        if (names.contains(zone.name)) return fail(error, QStringLiteral("duplicate semantic card zone"));
        names.insert(zone.name);
        for (const auto &id : zone.cards) {
            if (!world.cards.contains(id) || locatedCards.contains(id))
                return fail(error, QStringLiteral("card missing or assigned to multiple zones"));
            locatedCards.insert(id);
        }
    }
    if (locatedCards.size() != world.cards.size())
        return fail(error, QStringLiteral("card inventory has unlocated cards"));
    for (auto kind : {ZoneKind::Draw, ZoneKind::Discard, ZoneKind::Table, ZoneKind::Void})
        if (!zoneRoles.value(int(kind)).value(QString()).contains(QString()))
            return fail(error, QStringLiteral("missing global card zone, including empty zones"));
    for (const auto &player : world.players)
        for (auto kind : {ZoneKind::Hand, ZoneKind::Equip, ZoneKind::Judge})
            if (!zoneRoles.value(int(kind)).value(player.id).contains(QString()))
                return fail(error, QStringLiteral("missing player card zone, including empty zones"));
    for (auto it = world.skills.cbegin(); it != world.skills.cend(); ++it) {
        const auto &skill = it.value();
        const auto provider = registry.providers().constFind(skill.providerId);
        if (it.key().isEmpty() || it.key() != skill.id || !world.players.contains(skill.ownerId)
            || provider == registry.providers().cend() || !provider->skillDefinitions.contains(skill.definition))
            return fail(error, QStringLiteral("unknown skill instance owner/definition/provider"));
        QSet<QString> ancestry;
        QString parent = skill.id;
        while (!parent.isEmpty()) {
            if (!world.skills.contains(parent) || ancestry.contains(parent))
                return fail(error, QStringLiteral("missing or cyclic skill parent reference"));
            ancestry.insert(parent);
            parent = world.skills.value(parent).parentId;
        }
        if (!validateValue(skill.state, world, error) || !validateValue(skill.correctState, world, error)) return false;
    }
    if (!validateValue(world.roomTags, world, error)) return false;
    if (world.history.version != 1 || world.history.nextEventId == 0)
        return fail(error, QStringLiteral("unsupported history schema/cursor"));
    QSet<QString> historyIds;
    for (const auto &event : world.history.events) {
        // Parents precede children; this also rules out cycles and dangling parents.
        bool numeric = false;
        const quint64 number = event.id.toULongLong(&numeric);
        if (event.id.isEmpty() || historyIds.contains(event.id) || event.kind.isEmpty()
            || !numeric || !number || QString::number(number) != event.id || number >= world.history.nextEventId
            || (!event.parentId.isEmpty() && !historyIds.contains(event.parentId)))
            return fail(error, QStringLiteral("invalid history ID/parent/cursor"));
        historyIds.insert(event.id);
        if (!validateValue(event.data, world, error)) return false;
    }
    if (!nonemptyUnique(world.history.activeEventIds))
        return fail(error, QStringLiteral("duplicate active history scope"));
    for (const auto &id : world.history.activeEventIds)
        if (!historyIds.contains(id)) return fail(error, QStringLiteral("unresolved active history scope"));
    if (world.providers.keys() != registry.providers().keys())
        return fail(error, QStringLiteral("incomplete provider inventory"));
    for (auto it = world.providers.cbegin(); it != world.providers.cend(); ++it) {
        const auto &contract = registry.providers().value(it.key());
        if (it->version != contract.version)
            return fail(error, QStringLiteral("provider version mismatch: %1").arg(it.key()));
        if (!contract.unsupportedCapabilities.isEmpty())
            return fail(error, QStringLiteral("unsupported provider %1: %2")
                        .arg(it.key(), contract.unsupportedCapabilities.join(", ")));
        if (!validateValue(it->state, world, error)) return false;
    }
    return true;
}

WorldStore::WorldStore(const WorldState &initial, const ProviderRegistry &registry,
                       const CheckpointRetention &retention,
                       std::shared_ptr<GameTimeline> timeline)
    : m_state(std::make_shared<const WorldState>(initial)), m_registry(registry),
      m_timeline(std::move(timeline)), m_checkpointRetention(retention)
{
}

bool WorldStore::prepareValues(WorldState &candidate, QString *error) const
{
    // References and provider coverage must be valid BEFORE any restore hook.
    if (!validateWorld(candidate, m_registry, error)) return false;
    try {
        for (const auto &provider : m_registry.providers())
            if (provider.prepare && !provider.prepare(candidate, error)) return false;
        if (candidate.rootGameId != m_timeline->rootGameId() || candidate.worldId != m_timeline->worldId())
            return fail(error, QStringLiteral("provider changed logical identity"));
        if (!validateWorld(candidate, m_registry, error)) return false;
        for (const auto &provider : m_registry.providers())
            if (provider.validate && !provider.validate(candidate, error)) return false;
    } catch (const std::exception &exception) {
        return fail(error, QStringLiteral("provider threw: %1").arg(QString::fromUtf8(exception.what())));
    } catch (...) {
        return fail(error, QStringLiteral("provider threw an unknown exception"));
    }
    return true;
}

std::unique_ptr<WorldStore> WorldStore::create(const WorldState &initial,
                                              const ProviderRegistry &registry, QString *error,
                                              std::shared_ptr<GameTimeline> timeline)
{
    return create(initial, registry, CheckpointRetention{}, error, std::move(timeline));
}

std::unique_ptr<WorldStore> WorldStore::create(const WorldState &initial,
                                              const ProviderRegistry &registry,
                                              const CheckpointRetention &retention,
                                              QString *error,
                                              std::shared_ptr<GameTimeline> timeline)
{
    if (retention.playerTurns < 1 || retention.fullRounds < 1 || retention.otherAnchors < 1) {
        fail(error, QStringLiteral("checkpoint retention limits must be positive"));
        return {};
    }
    if (!timeline)
        timeline = std::make_shared<GameTimeline>(initial.rootGameId, initial.worldId);
    // A Room may attach its timeline only before this logical world is enrolled.
    // Reject stale/mismatched objects before any provider hook can run or state
    // can be prepared, so a failed attachment leaves the authoritative object
    // untouched. revision zero also proves there are no request/decision records.
    const bool cleanInitial = timeline->rootGameId() == initial.rootGameId
        && timeline->worldId() == initial.worldId
        && timeline->branchId() == QStringLiteral("0")
        && timeline->generation() == 0 && timeline->revision() == 0
        && timeline->records().isEmpty() && timeline->anchors().isEmpty()
        && timeline->m_pending.isEmpty()
        && timeline->m_nextSequence == 1 && timeline->m_nextRequest == 1;
    if (!cleanInitial) {
        fail(error, QStringLiteral("shared timeline identity mismatch or timeline is not clean initial state"));
        return {};
    }
    auto store = std::unique_ptr<WorldStore>(new WorldStore(initial, registry, retention, std::move(timeline)));
    WorldState candidate = initial;
    if (!store->prepareValues(candidate, error)) return {};
    store->m_state = std::make_shared<const WorldState>(std::move(candidate));
    return store;
}

void WorldStore::retainCheckpoints(GameTimeline &timeline,
                                   QMap<QString, std::shared_ptr<const WorldState>> &checkpoints) const
{
    QSet<QString> retained;
    QSet<QString> rewindable;
    int keptTurns = 0;
    int keptRounds = 0;
    int keptOther = 0;

    // Walk newest-first because anchor sequence is monotonic on the active branch.
    for (auto it = timeline.anchors().crbegin(); it != timeline.anchors().crend(); ++it) {
        if (it->kind != GameTimeline::AnchorKind::PlayerTurn || !checkpoints.contains(it->id)) continue;
        if (keptTurns >= m_checkpointRetention.playerTurns) continue;
        retained.insert(it->id);
        rewindable.insert(it->id);
        ++keptTurns;
    }
    for (auto it = timeline.anchors().crbegin(); it != timeline.anchors().crend(); ++it) {
        if (it->kind != GameTimeline::AnchorKind::FullRound || !checkpoints.contains(it->id)) continue;
        if (keptRounds >= m_checkpointRetention.fullRounds) continue;
        retained.insert(it->id);
        rewindable.insert(it->id);
        ++keptRounds;
    }
    for (auto it = timeline.anchors().crbegin(); it != timeline.anchors().crend(); ++it) {
        if (it->kind == GameTimeline::AnchorKind::PlayerTurn
            || it->kind == GameTimeline::AnchorKind::FullRound
            || !checkpoints.contains(it->id)) continue;
        if (keptOther >= m_checkpointRetention.otherAnchors) continue;
        retained.insert(it->id);
        rewindable.insert(it->id);
        ++keptOther;
    }

    // A retained round and its first normal PlayerTurn are one immutable world
    // state. Keep the paired turn alias alive so the round's identity remains
    // intact, but do not let it escape the independent turn-target window.
    for (const auto &round : timeline.anchors()) {
        if (round.kind != GameTimeline::AnchorKind::FullRound || !rewindable.contains(round.id)) continue;
        const auto roundState = checkpoints.value(round.id);
        if (!roundState || roundState->turn.turnScopeId.isEmpty()) continue;
        for (const auto &turn : timeline.anchors()) {
            if (turn.kind != GameTimeline::AnchorKind::PlayerTurn
                || turn.scopeId != roundState->turn.turnScopeId
                || checkpoints.value(turn.id) != roundState) continue;
            retained.insert(turn.id);
            break;
        }
    }

    QMap<QString, std::shared_ptr<const WorldState>> nextCheckpoints;
    for (const auto &anchor : timeline.anchors()) {
        if (retained.contains(anchor.id) && checkpoints.contains(anchor.id))
            nextCheckpoints.insert(anchor.id, checkpoints.value(anchor.id));
    }
    timeline.retainAnchors(retained, rewindable);
    checkpoints.swap(nextCheckpoints);
}

bool WorldStore::update(const std::function<bool(WorldState &, QString *)> &mutation, QString *error)
{
    const quint64 revision = m_revision;
    const quint64 timelineRevision = m_timeline->revision();
    WorldState value = *m_state;
    try {
        if (!mutation(value, error)) return false;
        if (value.rootGameId != m_timeline->rootGameId() || value.worldId != m_timeline->worldId())
            return fail(error, QStringLiteral("transaction changed logical identity"));
        if (!validateWorld(value, m_registry, error)) return false;
        for (const auto &provider : m_registry.providers())
            if (provider.validate && !provider.validate(value, error)) return false;
    } catch (...) {
        return fail(error, QStringLiteral("state transaction threw"));
    }
    auto next = std::make_shared<const WorldState>(std::move(value));
    if (revision != m_revision || timelineRevision != m_timeline->revision())
        return fail(error, QStringLiteral("state changed during transaction"));
    m_state.swap(next);
    ++m_revision;
    return true;
}

GameTimeline::Anchor WorldStore::checkpoint(GameTimeline::AnchorKind kind,
                                           const QString &scopeId, const QString &playerId,
                                           QString *error)
{
    return checkpointUpdate({}, kind, scopeId, playerId, error);
}

GameTimeline::Anchor WorldStore::checkpointUpdate(const std::function<bool(WorldState &, QString *)> &mutation,
                                                 GameTimeline::AnchorKind kind, const QString &scopeId,
                                                 const QString &playerId, QString *error, bool alsoRoundStart)
{
    const quint64 revision = m_revision;
    const quint64 timelineRevision = m_timeline->revision();
    WorldState value = *m_state;
    try {
        if (mutation && !mutation(value, error)) return {};
        if (value.rootGameId != m_timeline->rootGameId() || value.worldId != m_timeline->worldId()) {
            fail(error, QStringLiteral("checkpoint changed logical identity")); return {};
        }
        if (!validateWorld(value, m_registry, error)) return {};
        for (const auto &provider : m_registry.providers())
            if (provider.validate && !provider.validate(value, error)) return {};
    } catch (...) {
        fail(error, QStringLiteral("checkpoint preparation threw")); return {};
    }
    const bool setup = kind == GameTimeline::AnchorKind::ManagedSetup;
    if (setup && (value.turn.phase != QStringLiteral("PhaseNone")
                  || !scopeId.startsWith(QStringLiteral("setup:")) || !playerId.isEmpty()
                  || value.turn.extraTurn || scopeId != value.turn.turnScopeId)) {
        fail(error, QStringLiteral("managed setup anchor requires explicit setup scope and PhaseNone"));
        return {};
    }
    if (!setup && value.turn.phase != QStringLiteral("RoundStart")) {
        fail(error, QStringLiteral("start anchor requires the managed RoundStart boundary"));
        return {};
    }
    switch (kind) {
    case GameTimeline::AnchorKind::PlayerTurn:
    case GameTimeline::AnchorKind::FullRound:
    case GameTimeline::AnchorKind::ExtraTurn: break;
    case GameTimeline::AnchorKind::ManagedSetup: break;
    case GameTimeline::AnchorKind::Continuation:
    default:
        fail(error, QStringLiteral("unsupported continuation or unknown anchor kind"));
        return {};
    }
    if (setup) {
        // Deliberately not a gameplay turn/round anchor.
    } else if (kind == GameTimeline::AnchorKind::FullRound) {
        if (scopeId != value.turn.roundScopeId || value.turn.extraTurn || !playerId.isEmpty()) {
            fail(error, QStringLiteral("round anchor must match actual normal round scope"));
            return {};
        }
    } else if (scopeId != value.turn.turnScopeId || playerId != value.turn.playerId
               || (kind == GameTimeline::AnchorKind::PlayerTurn && value.turn.extraTurn)
               || (kind == GameTimeline::AnchorKind::ExtraTurn && !value.turn.extraTurn)
               || kind == GameTimeline::AnchorKind::Continuation) {
        fail(error, QStringLiteral("turn anchor mismatch or unsupported continuation boundary"));
        return {};
    }
    // Prepare both collections before changing the live timeline.
    GameTimeline next = *m_timeline;
    GameTimeline::Anchor roundAnchor;
    if (alsoRoundStart) {
        if (kind != GameTimeline::AnchorKind::PlayerTurn || value.turn.roundScopeId.isEmpty()) {
            fail(error, QStringLiteral("paired round anchor requires an ordinary turn boundary")); return {};
        }
        roundAnchor = next.beginAnchor(GameTimeline::AnchorKind::FullRound, value.turn.roundScopeId, {}, error);
        if (roundAnchor.id.isEmpty()) return {};
    }
    const auto anchor = next.beginAnchor(kind, scopeId, playerId, error);
    if (anchor.id.isEmpty()) return {};
    auto nextState = std::make_shared<const WorldState>(std::move(value));
    auto checkpoints = m_checkpoints;
    if (!roundAnchor.id.isEmpty()) checkpoints.insert(roundAnchor.id, nextState);
    checkpoints.insert(anchor.id, nextState);
    retainCheckpoints(next, checkpoints);
    if (revision != m_revision || timelineRevision != m_timeline->revision()) {
        fail(error, QStringLiteral("world changed during checkpoint preparation")); return {};
    }
    m_state.swap(nextState);
    m_checkpoints.swap(checkpoints);
    m_timeline->swap(next);
    ++m_revision;
    return anchor;
}

std::unique_ptr<WorldStore::Candidate> WorldStore::prepareRestore(const QString &anchorId, QString *error) const
{
    if (!m_checkpoints.contains(anchorId)) {
        fail(error, QStringLiteral("no state checkpoint bound to this anchor"));
        return {};
    }
    auto nextTimeline = m_timeline->prepareRestore(anchorId, error);
    if (!nextTimeline) return {};
    // A normal round and its first player turn can name the exact same
    // immutable boundary. Restoring the round must retain that paired turn,
    // while still discarding every genuinely later checkpoint.
    const auto saved = m_checkpoints.value(anchorId);
    if (m_timeline->anchor(anchorId).kind == GameTimeline::AnchorKind::FullRound) {
        for (const auto &anchor : m_timeline->anchors()) {
            if (anchor.kind == GameTimeline::AnchorKind::PlayerTurn
                && anchor.scopeId == saved->turn.turnScopeId
                && m_checkpoints.value(anchor.id) == saved
                && nextTimeline->anchor(anchor.id).id.isEmpty()) {
                nextTimeline->m_anchors.append(anchor);
                ++nextTimeline->m_revision;
            }
        }
    }
    auto result = std::unique_ptr<Candidate>(new Candidate);
    result->m_owner = this;
    result->m_stateRevision = m_revision;
    result->m_timelineRevision = m_timeline->revision();
    for (const auto &anchor : nextTimeline->anchors()) {
        const auto checkpoint = m_checkpoints.constFind(anchor.id);
        if (checkpoint == m_checkpoints.cend()) {
            fail(error, QStringLiteral("retained anchor has no bound checkpoint state"));
            return {};
        }
        result->m_checkpoints.insert(anchor.id, checkpoint.value());
    }
    WorldState value = *m_checkpoints.value(anchorId);
    if (!prepareValues(value, error)) return {};
    const auto &savedTurn = m_checkpoints.value(anchorId)->turn;
    if (value.turn.playerId != savedTurn.playerId || value.turn.phase != savedTurn.phase
        || value.turn.turnScopeId != savedTurn.turnScopeId || value.turn.roundScopeId != savedTurn.roundScopeId
        || value.turn.extraTurn != savedTurn.extraTurn) {
        fail(error, QStringLiteral("provider changed the checkpoint's bound turn/round position"));
        return {};
    }
    result->m_state = std::make_shared<const WorldState>(std::move(value));
    result->m_timeline = std::move(nextTimeline);
    return result;
}

std::shared_ptr<const WorldState> WorldStore::checkpointState(const QString &anchorId) const
{
    return m_timeline->anchor(anchorId).id.isEmpty() || !m_checkpoints.contains(anchorId)
        ? nullptr : m_checkpoints.value(anchorId);
}

bool WorldStore::publish(Candidate &&candidate, QString *error)
{
    if (!canPublish(candidate, error)) return false;
    // No user code, allocating containers, or external effects beyond this point.
    m_state.swap(candidate.m_state);
    // Keep the shared authoritative object's address stable for Room observers.
    m_timeline->swap(*candidate.m_timeline);
    m_checkpoints.swap(candidate.m_checkpoints);
    ++m_revision;
    candidate.m_owner = nullptr;
    return true;
}

bool WorldStore::canPublish(const Candidate &candidate, QString *error) const
{
    if (candidate.m_owner != this || !candidate.m_state || !candidate.m_timeline
        || candidate.m_timeline->m_instanceId != m_timeline->m_instanceId)
        return fail(error, QStringLiteral("candidate is consumed or belongs to another world"));
    if (candidate.m_stateRevision != m_revision || candidate.m_timelineRevision != m_timeline->revision())
        return fail(error, QStringLiteral("candidate is stale"));
    return true;
}

} // namespace GameState
