#include "game-timeline.h"

#include <QMetaType>
#include <QUuid>
#include <cmath>
#include <limits>
#include <utility>

namespace {
bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}
bool journalValue(const QVariant &value, int depth = 0)
{
    if (depth > 64) return false;
    switch (value.userType()) {
    case QMetaType::UnknownType:
    case QMetaType::Bool:
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::QString: return true;
    case QMetaType::Double: return std::isfinite(value.toDouble());
    case QMetaType::QVariantList:
        for (const auto &entry : value.toList())
            if (!journalValue(entry, depth + 1)) return false;
        return true;
    case QMetaType::QVariantMap:
        for (const auto &entry : value.toMap())
            if (!journalValue(entry, depth + 1)) return false;
        return true;
    default: return false;
    }
}
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
}

GameTimeline::GameTimeline(const QString &rootGameId, const QString &worldId)
    : m_rootGameId(rootGameId.isEmpty() ? newId() : rootGameId),
      m_worldId(worldId.isEmpty() ? newId() : worldId), m_branchId(QStringLiteral("0")),
      m_instanceId(newId())
{
}

void GameTimeline::append(const QString &kind, const QVariantMap &data)
{
    m_records.append({m_nextSequence++, m_generation, m_branchId, kind, data});
    ++m_revision;
}

GameTimeline::Anchor GameTimeline::beginAnchor(AnchorKind kind, const QString &scopeId,
                                               const QString &playerId, QString *error)
{
    switch (kind) {
    case AnchorKind::PlayerTurn: case AnchorKind::FullRound:
    case AnchorKind::ExtraTurn: case AnchorKind::Continuation: case AnchorKind::ManagedSetup: break;
    default:
        fail(error, QStringLiteral("unknown anchor kind"));
        return {};
    }
    const bool roomScope = kind == AnchorKind::FullRound || kind == AnchorKind::ManagedSetup;
    if (scopeId.isEmpty() || (!roomScope && playerId.isEmpty())
        || (roomScope && !playerId.isEmpty())) {
        fail(error, QStringLiteral("anchor requires explicit scope and player"));
        return {};
    }
    for (const auto &a : m_anchors) {
        if (a.kind == kind && a.scopeId == scopeId) {
            fail(error, QStringLiteral("duplicate active scope anchor"));
            return {};
        }
    }
    Anchor result{QString::number(m_nextSequence), kind, scopeId, playerId,
                  m_generation, m_nextSequence};
    append(QStringLiteral("anchor"), {{"id", result.id}, {"scope", scopeId},
                                      {"player", playerId}, {"kind", int(kind)}});
    m_anchors.append(result);
    return result;
}

GameTimeline::Anchor GameTimeline::previous(AnchorKind kind) const
{
    bool currentFound = false;
    for (auto it = m_anchors.crbegin(); it != m_anchors.crend(); ++it) {
        if (it->kind != kind || !it->rewindable) continue;
        if (currentFound) return *it;
        currentFound = true;
    }
    return {};
}

void GameTimeline::retainAnchors(const QSet<QString> &retained, const QSet<QString> &rewindable)
{
    QVector<Anchor> next;
    next.reserve(m_anchors.size());
    bool changed = false;
    for (const auto &anchor : m_anchors) {
        if (!retained.contains(anchor.id)) {
            changed = true;
            continue;
        }
        Anchor kept = anchor;
        const bool canRewind = rewindable.contains(anchor.id);
        if (kept.rewindable != canRewind) {
            kept.rewindable = canRewind;
            changed = true;
        }
        next.append(std::move(kept));
    }
    if (!changed) return;
    m_anchors.swap(next);
    ++m_revision;
}

GameTimeline::Anchor GameTimeline::previousPlayerTurn() const { return previous(AnchorKind::PlayerTurn); }
GameTimeline::Anchor GameTimeline::previousFullRound() const { return previous(AnchorKind::FullRound); }
GameTimeline::Anchor GameTimeline::anchor(const QString &id) const
{
    for (const auto &a : m_anchors) if (a.id == id) return a;
    return {};
}

GameTimeline::RequestToken GameTimeline::issueRequest(const QString &playerId, const QString &command)
{
    const quint64 serial = m_nextRequest++;
    m_pending.insert(serial, {{"player", playerId}, {"command", command}});
    ++m_revision;
    return {m_rootGameId, m_worldId, m_generation, serial, m_instanceId};
}

bool GameTimeline::isPending(const RequestToken &token) const
{
    return token.rootGameId == m_rootGameId && token.worldId == m_worldId
        && token.instanceId == m_instanceId && token.generation == m_generation
        && m_pending.contains(token.serial);
}

bool GameTimeline::acceptDecision(const RequestToken &token, const QVariantMap &decision,
                                  const QString &source, QString *error)
{
    if (!isPending(token)) return fail(error, QStringLiteral("stale or unknown request token"));
    if (source.isEmpty() || !journalValue(decision))
        return fail(error, QStringLiteral("invalid accepted decision payload/source"));
    QVariantMap data = m_pending.value(token.serial);
    data.insert("request", QString::number(token.serial));
    data.insert("source", source);
    data.insert("decision", decision);
    append(QStringLiteral("accepted_decision"), data);
    m_pending.remove(token.serial);
    return true;
}

bool GameTimeline::acceptTimeout(const RequestToken &token, const QVariantMap &fallback, QString *error)
{
    return acceptDecision(token, fallback, QStringLiteral("timeout"), error);
}

bool GameTimeline::recordNondeterminism(const QString &source, const QVariantMap &value, QString *error)
{
    if (source.isEmpty() || !journalValue(value))
        return fail(error, QStringLiteral("invalid nondeterminism payload/source"));
    append(QStringLiteral("nondeterminism"), {{"source", source}, {"value", value}});
    return true;
}

std::unique_ptr<GameTimeline> GameTimeline::prepareRestore(const QString &anchorId, QString *error) const
{
    const Anchor target = anchor(anchorId);
    if (target.id.isEmpty() || !target.rewindable) {
        fail(error, QStringLiteral("restore anchor is not retained on the active ancestry"));
        return {};
    }
    if (m_generation == std::numeric_limits<quint64>::max()) {
        fail(error, QStringLiteral("timeline generation exhausted"));
        return {};
    }
    auto next = std::unique_ptr<GameTimeline>(new GameTimeline(*this));
    ++next->m_generation;
    next->m_branchId = QString::number(next->m_generation);
    next->m_pending.clear();
    while (!next->m_anchors.isEmpty() && next->m_anchors.last().sequence > target.sequence)
        next->m_anchors.removeLast();
    next->append(QStringLiteral("restore"), {{"anchor", anchorId}, {"kind", int(target.kind)},
                                            {"origin_generation", QString::number(target.generation)}});
    return next;
}

void GameTimeline::swap(GameTimeline &other) noexcept
{
    m_rootGameId.swap(other.m_rootGameId);
    m_worldId.swap(other.m_worldId);
    m_branchId.swap(other.m_branchId);
    m_instanceId.swap(other.m_instanceId);
    std::swap(m_generation, other.m_generation);
    std::swap(m_revision, other.m_revision);
    std::swap(m_nextSequence, other.m_nextSequence);
    std::swap(m_nextRequest, other.m_nextRequest);
    m_records.swap(other.m_records);
    m_anchors.swap(other.m_anchors);
    m_pending.swap(other.m_pending);
}
