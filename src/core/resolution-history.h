#ifndef RESOLUTION_HISTORY_H
#define RESOLUTION_HISTORY_H

#include <QSharedDataPointer>
#include <QMap>
#include <QString>
#include <QVariantMap>
#include <memory>

class SnapshotJsonWriter;

// Immutable, pure-value checkpoint of a ResolutionHistoryService journal.
class ResolutionHistorySnapshot
{
public:
    ResolutionHistorySnapshot();
    ResolutionHistorySnapshot(const ResolutionHistorySnapshot &other);
    ResolutionHistorySnapshot &operator=(const ResolutionHistorySnapshot &other);
    ~ResolutionHistorySnapshot();

    QVariantMap serialize() const;
    bool writeJson(SnapshotJsonWriter &writer) const;
    static bool deserialize(const QVariantMap &serialized,
                            ResolutionHistorySnapshot *snapshot,
                            QString *error = nullptr);
    bool isComplete() const;
    bool remapPlayerIds(const QMap<QString, QString> &mapping, QString *error = nullptr);

private:
    struct Data;
    explicit ResolutionHistorySnapshot(const QSharedDataPointer<Data> &data);
    QSharedDataPointer<Data> d;
    friend class ResolutionHistoryService;
};

// Room-owned, dependency-free journal for authoritative resolution history.
// Values accepted by this service are copied as primitive QVariant trees only.
class ResolutionHistoryService
{
public:
    ResolutionHistoryService();
    ~ResolutionHistoryService();

    qint64 beginEvent(const QString &kind, const QVariantMap &data = {});
    void finishEvent(qint64 id, const QString &outcome = QStringLiteral("completed"));
    void updateEvent(qint64 id, const QVariantMap &data);
    qint64 appendFact(qint64 eventId, const QString &kind, const QVariantMap &data);

    qint64 beginRound(const QVariantMap &data = {});
    void endRound(const QString &outcome = QStringLiteral("completed"));

    qint64 currentEventId() const;
    QVariantMap event(qint64 id) const;
    QVariantMap findParent(qint64 id, const QString &kind,
                           bool includeSelf = false) const;
    QVariantMap queryEvents(const QVariantMap &filter) const;
    QVariantMap queryFacts(const QVariantMap &filter) const;
    QVariantMap currentScopes() const;

    ResolutionHistorySnapshot snapshot() const;
    bool restore(const ResolutionHistorySnapshot &snapshot, QString *error = nullptr);

private:
    struct Data;
    QSharedDataPointer<Data> d;
    friend class ResolutionHistoryContextGuard;
};

// Re-enter only the attribution context of an already recorded event while
// exception cleanup runs; the event's terminal outcome remains unchanged.
class ResolutionHistoryContextGuard
{
public:
    ResolutionHistoryContextGuard(ResolutionHistoryService &service, qint64 eventId,
                                  bool enabled = true);
    ~ResolutionHistoryContextGuard() noexcept;
    ResolutionHistoryContextGuard(const ResolutionHistoryContextGuard &) = delete;
    ResolutionHistoryContextGuard &operator=(const ResolutionHistoryContextGuard &) = delete;
private:
    struct Data;
    std::unique_ptr<Data> d;
};

// RAII lifecycle helper for integrations that may leave resolution by exception.
class ResolutionHistoryEventGuard
{
public:
    ResolutionHistoryEventGuard(ResolutionHistoryService &service, const QString &kind,
                                const QVariantMap &data = {}, bool enabled = true);
    ~ResolutionHistoryEventGuard();
    ResolutionHistoryEventGuard(const ResolutionHistoryEventGuard &) = delete;
    ResolutionHistoryEventGuard &operator=(const ResolutionHistoryEventGuard &) = delete;

    qint64 id() const;
    void update(const QVariantMap &data);
    void finish(const QString &outcome = QStringLiteral("completed"));

private:
    ResolutionHistoryService *service = nullptr;
    qint64 eventId = 0;
    bool finished = false;
    int uncaughtAtConstruction = 0;
};

#endif // RESOLUTION_HISTORY_H
