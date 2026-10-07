#ifndef QSAN_GAME_TIMELINE_H
#define QSAN_GAME_TIMELINE_H

#include <QMap>
#include <QSet>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include <memory>

namespace GameState { class WorldStore; }

// Lives with the logical game, not with a replaceable simulation/runtime.
// All methods are game-thread only. Transport/socket ownership stays outside it.
class GameTimeline
{
public:
    enum class AnchorKind { PlayerTurn, FullRound, ExtraTurn, Continuation, ManagedSetup };
    struct Anchor {
        QString id;
        AnchorKind kind = AnchorKind::PlayerTurn;
        QString scopeId;
        QString playerId;
        quint64 generation = 0;
        quint64 sequence = 0;
        // A paired turn alias may remain attached to a retained round snapshot
        // after it falls outside the independently bounded turn window.
        bool rewindable = true;
    };
    struct RequestToken {
        QString rootGameId;
        QString worldId;
        quint64 generation = 0;
        quint64 serial = 0;
        QString instanceId; // Separates live worlds even when logical IDs coincide.
    };
    struct Record {
        quint64 sequence = 0;
        quint64 generation = 0;
        QString branchId;
        QString kind;
        QVariantMap data;
    };

    explicit GameTimeline(const QString &rootGameId = {}, const QString &worldId = {});
    GameTimeline &operator=(const GameTimeline &) = delete;
    const QString &rootGameId() const { return m_rootGameId; }
    const QString &worldId() const { return m_worldId; }
    const QString &branchId() const { return m_branchId; }
    quint64 generation() const { return m_generation; }
    quint64 revision() const { return m_revision; }
    const QVector<Record> &records() const { return m_records; }
    const QVector<Anchor> &anchors() const { return m_anchors; }

    // Call FullRound only at GameRule's actual beginRound scope. No seat arithmetic.
    // Extra turns and nested resumptions have their own kinds and never count as rounds.
    Anchor beginAnchor(AnchorKind kind, const QString &scopeId,
                       const QString &playerId = {}, QString *error = nullptr);
    Anchor previousPlayerTurn() const;
    Anchor previousFullRound() const;
    Anchor anchor(const QString &id) const;
    RequestToken issueRequest(const QString &playerId, const QString &command);
    bool isPending(const RequestToken &token) const;
    bool acceptDecision(const RequestToken &token, const QVariantMap &decision,
                        const QString &source, QString *error = nullptr);
    bool acceptTimeout(const RequestToken &token, const QVariantMap &fallback,
                       QString *error = nullptr);
    // Capture the value at its accepted point (clock, external AI, external seed, etc.).
    // This journal does not by itself constitute an executable command replayer.
    bool recordNondeterminism(const QString &source, const QVariantMap &value,
                             QString *error = nullptr);
    std::unique_ptr<GameTimeline> prepareRestore(const QString &anchorId,
                                                QString *error = nullptr) const;

private:
    GameTimeline(const GameTimeline &) = default;
    void swap(GameTimeline &other) noexcept;
    friend class GameState::WorldStore;
    Anchor previous(AnchorKind kind) const;
    void retainAnchors(const QSet<QString> &retained, const QSet<QString> &rewindable);
    void append(const QString &kind, const QVariantMap &data);
    QString m_rootGameId;
    QString m_worldId;
    QString m_branchId;
    QString m_instanceId;
    quint64 m_generation = 0;
    quint64 m_revision = 0;
    quint64 m_nextSequence = 1;
    quint64 m_nextRequest = 1;
    QVector<Record> m_records;
    QVector<Anchor> m_anchors;
    QMap<quint64, QVariantMap> m_pending;
};

#endif
