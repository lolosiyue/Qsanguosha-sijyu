#include "resolution-history.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QSharedPointer>
#include <QVariantList>

#include <algorithm>
#include <exception>
#include <array>
#include <limits>
#include <cmath>
#include <iterator>
#include <memory>

namespace {

// Persistent radix tree: snapshots share immutable pages; an edit copies only
// one 256-record page and the bounded path to it, never the whole journal.
template<typename T> class Records {
    static constexpr int PageSize = 256;
    struct Node {
        std::shared_ptr<const Node> left, right;
        std::shared_ptr<const QVector<T>> page;
    };
    std::shared_ptr<const Node> root;
    qsizetype count = 0;
    static std::shared_ptr<const Node> replace(const std::shared_ptr<const Node> &old,
                                              quint64 pageId, int bit,
                                              const std::shared_ptr<const QVector<T>> &page)
    {
        auto node = old ? std::make_shared<Node>(*old) : std::make_shared<Node>();
        if (bit < 0) node->page = page;
        else if ((pageId >> bit) & 1) node->right = replace(node->right, pageId, bit - 1, page);
        else node->left = replace(node->left, pageId, bit - 1, page);
        return node;
    }
    const QVector<T> *pageAt(qsizetype index) const
    {
        const Node *node = root.get();
        const quint64 pageId = quint64(index / PageSize);
        for (int bit = 31; bit >= 0 && node; --bit)
            node = ((pageId >> bit) & 1) ? node->right.get() : node->left.get();
        return node ? node->page.get() : nullptr;
    }
public:
    qsizetype size() const { return count; }
    const T &at(qsizetype index) const { return pageAt(index)->at(index % PageSize); }
    void set(qsizetype index, const T &value)
    {
        auto page = std::make_shared<QVector<T>>(*pageAt(index));
        (*page)[index % PageSize] = value;
        root = replace(root, quint64(index / PageSize), 31, page);
    }
    void append(const T &value)
    {
        const auto *old = pageAt(count);
        auto page = old ? std::make_shared<QVector<T>>(*old) : std::make_shared<QVector<T>>();
        page->append(value);
        root = replace(root, quint64(count / PageSize), 31, page);
        ++count;
    }
    struct Iterator {
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = qsizetype;
        using pointer = const T *;
        using reference = const T &;
        const Records *records;
        qsizetype index;
        const T &operator*() const { return records->at(index); }
        const T *operator->() const { return &records->at(index); }
        Iterator &operator++() { ++index; return *this; }
        bool operator==(const Iterator &other) const { return index == other.index; }
        bool operator!=(const Iterator &other) const { return !(*this == other); }
    };
    Iterator begin() const { return {this, 0}; }
    Iterator end() const { return {this, count}; }
    Iterator cbegin() const { return begin(); }
    Iterator cend() const { return end(); }
};

struct EventRecord {
    qint64 id = 0;
    qint64 parentId = 0;
    QString kind;
    qint64 roundId = 0;
    qint64 turnId = 0;
    qint64 phaseId = 0;
    QString status = QStringLiteral("active");
    QString outcome;
    QVariantMap data;
};

struct FactRecord {
    qint64 id = 0;
    qint64 sequence = 0;
    qint64 eventId = 0;
    QString kind;
    qint64 roundId = 0;
    qint64 turnId = 0;
    qint64 phaseId = 0;
    QVariantMap data;
};

struct Journal {
    Records<EventRecord> events;
    Records<FactRecord> facts;
    QVector<qint64> active;
    qint64 nextId = 1;
    qint64 nextSequence = 1;
    qint64 roundScopeId = 0;
    qint64 contextEventId = 0;
    bool complete = true;
};

struct Scope {
    qint64 roundId = 0;
    qint64 turnId = 0;
    qint64 phaseId = 0;
};

static QString idString(qint64 id)
{
    return QString::number(id);
}

static bool parseScopeId(const QVariant &value, qint64 *id)
{
    if (!value.isValid() || value.isNull()) return false;
    if (value.typeId() != QMetaType::QString && value.typeId() != QMetaType::LongLong
        && value.typeId() != QMetaType::Int && value.typeId() != QMetaType::UInt
        && value.typeId() != QMetaType::ULongLong) return false;
    const QString text = value.toString();
    // Signed 64-bit IDs have at most 19 decimal digits; never round via double.
    if (text.isEmpty() || text.size() > 19) return false;
    for (QChar digit : text) if (digit < QLatin1Char('0') || digit > QLatin1Char('9')) return false;
    bool ok = false;
    const qint64 parsed = text.toLongLong(&ok);
    if (!ok || parsed < 0) return false;
    if (id) *id = parsed;
    return true;
}

static bool parseId(const QVariant &value, qint64 *id)
{
    qint64 parsed = 0;
    if (!parseScopeId(value, &parsed) || parsed == 0) return false;
    if (id) *id = parsed;
    return true;
}

static bool eventKind(const QString &kind)
{
    static const QSet<QString> kinds = {QStringLiteral("game"), QStringLiteral("round"),
        QStringLiteral("turn"), QStringLiteral("phase"), QStringLiteral("skill"),
        QStringLiteral("use_card"), QStringLiteral("respond_card"), QStringLiteral("damage"),
        QStringLiteral("move_cards")};
    return kinds.contains(kind);
}

static bool factKind(const QString &kind)
{
    static const QSet<QString> kinds = {QStringLiteral("actual_damage"), QStringLiteral("move"),
        QStringLiteral("use_card"), QStringLiteral("respond_card"), QStringLiteral("damage_component")};
    return kinds.contains(kind);
}

static bool terminalOutcome(const QString &outcome)
{
    static const QSet<QString> outcomes = {QStringLiteral("cancelled"), QStringLiteral("skipped"),
        QStringLiteral("broken"), QStringLiteral("interrupted"), QStringLiteral("completed"),
        QStringLiteral("aborted"), QStringLiteral("pay_failed"), QStringLiteral("prevented")};
    return outcomes.contains(outcome);
}

static bool pureValue(const QVariant &value, QVariant *result, int depth = 0)
{
    if (depth > 32) return false;
    if (!value.isValid() || value.typeId() == QMetaType::Nullptr) {
        *result = QVariant();
        return true;
    }
    const int type = value.typeId();
    if (type == QMetaType::Bool || type == QMetaType::Int || type == QMetaType::UInt
        || type == QMetaType::LongLong || type == QMetaType::ULongLong
        || type == QMetaType::Double || type == QMetaType::QString) {
        if (type == QMetaType::Double && !std::isfinite(value.toDouble())) return false;
        *result = value;
        return true;
    }
    if (type == QMetaType::QVariantList) {
        QVariantList list;
        for (const QVariant &item : value.toList()) {
            QVariant clean;
            if (!pureValue(item, &clean, depth + 1))
                return false;
            list.append(clean);
        }
        *result = list;
        return true;
    }
    if (type == QMetaType::QVariantMap) {
        QVariantMap map;
        const QVariantMap source = value.toMap();
        for (auto it = source.cbegin(); it != source.cend(); ++it) {
            QVariant clean;
            if (!pureValue(it.value(), &clean, depth + 1))
                return false;
            map.insert(it.key(), clean);
        }
        *result = map;
        return true;
    }
    return false;
}

static QVariantMap cleanMap(const QVariantMap &source, bool *valid = nullptr)
{
    QVariant clean;
    const bool ok = pureValue(source, &clean);
    if (valid) *valid = ok;
    if (!ok)
        return {};
    return clean.toMap();
}

static QVariantMap eventMap(const EventRecord &event)
{
    QVariantMap result;
    result.insert(QStringLiteral("id"), idString(event.id));
    result.insert(QStringLiteral("parent_id"), idString(event.parentId));
    result.insert(QStringLiteral("kind"), event.kind);
    result.insert(QStringLiteral("round_id"), idString(event.roundId));
    result.insert(QStringLiteral("turn_id"), idString(event.turnId));
    result.insert(QStringLiteral("phase_id"), idString(event.phaseId));
    result.insert(QStringLiteral("status"), event.status);
    result.insert(QStringLiteral("outcome"), event.outcome);
    result.insert(QStringLiteral("data"), event.data);
    return result;
}

static QVariantMap factMap(const FactRecord &fact)
{
    QVariantMap result;
    result.insert(QStringLiteral("id"), idString(fact.id));
    result.insert(QStringLiteral("sequence"), idString(fact.sequence));
    result.insert(QStringLiteral("event_id"), idString(fact.eventId));
    result.insert(QStringLiteral("kind"), fact.kind);
    result.insert(QStringLiteral("round_id"), idString(fact.roundId));
    result.insert(QStringLiteral("turn_id"), idString(fact.turnId));
    result.insert(QStringLiteral("phase_id"), idString(fact.phaseId));
    result.insert(QStringLiteral("data"), fact.data);
    return result;
}

struct Index {
    QHash<qint64, qsizetype> byId;
    QHash<QString, QVector<qsizetype>> byKind;
    QHash<qint64, QVector<qsizetype>> byRound, byTurn, byPhase, byEvent;
    template<typename T> void add(const T &record, qsizetype position, qint64 eventId) {
        byId.insert(record.id, position);
        byKind[record.kind].append(position);
        byRound[record.roundId].append(position);
        byTurn[record.turnId].append(position);
        byPhase[record.phaseId].append(position);
        byEvent[eventId].append(position);
    }
};

static Scope scopeFor(const Journal &journal, const Index &index)
{
    Scope scope;
    if (journal.contextEventId) {
        const auto found = index.byId.constFind(journal.contextEventId);
        if (found != index.byId.cend()) {
            const EventRecord &event = journal.events.at(found.value());
            scope = {event.roundId, event.turnId, event.phaseId};
        }
    }
    for (qint64 id : journal.active) {
        const auto found = index.byId.constFind(id);
        if (found == index.byId.cend()) continue;
        const EventRecord &event = journal.events.at(found.value());
        if (event.kind == QLatin1String("round")) scope.roundId = id;
        if (event.kind == QLatin1String("turn")) { scope.turnId = id; scope.phaseId = 0; }
        if (event.kind == QLatin1String("phase")) scope.phaseId = id;
    }
    if (!journal.contextEventId && journal.roundScopeId) scope.roundId = journal.roundScopeId;
    return scope;
}

static bool matchText(const QVariantMap &filter, const QString &key, const QString &actual)
{
    return !filter.contains(key) || filter.value(key).toString() == actual;
}

static bool matchScope(const QVariantMap &filter, const QString &key, qint64 actual)
{
    if (!filter.contains(key)) return true;
    qint64 wanted = 0;
    return parseScopeId(filter.value(key), &wanted) && wanted == actual;
}

static bool matchDataField(const QVariantMap &filter, const QVariantMap &data, const QString &key)
{
    if (!filter.contains(key)) return true;
    const QVariant wanted = filter.value(key);
    const QVariant actual = data.value(key);
    return actual.isValid() && actual == wanted;
}

static bool matchAttribution(const QVariantMap &filter, const QVariantMap &data)
{
    return matchDataField(filter, data, QStringLiteral("from"))
        && matchDataField(filter, data, QStringLiteral("to"))
        && matchDataField(filter, data, QStringLiteral("player"))
        && matchDataField(filter, data, QStringLiteral("skill_owner"))
        && matchDataField(filter, data, QStringLiteral("skill_name"));
}

static bool validEventReferences(const QVariant &value, const QHash<qint64, const EventRecord *> &events,
                                 const QString &key = QString())
{
    if (key == QLatin1String("cause_event_id")) {
        qint64 id = 0;
        return parseScopeId(value, &id) && (id == 0 || events.contains(id));
    }
    if (key == QLatin1String("execution_id")) return parseScopeId(value, nullptr);
    if (value.typeId() == QMetaType::QVariantMap) {
        const QVariantMap map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it)
            if (!validEventReferences(it.value(), events, it.key())) return false;
    } else if (value.typeId() == QMetaType::QVariantList) {
        for (const QVariant &item : value.toList()) if (!validEventReferences(item, events)) return false;
    }
    return true;
}

static bool validateJournal(const Journal &journal, QString *error)
{
    auto fail = [error](const char *message) {
        if (error) *error = QString::fromLatin1(message);
        return false;
    };
    if (!journal.complete || journal.contextEventId) return fail("incomplete or unsafe journal");
    QHash<qint64, const EventRecord *> events;
    QHash<qint64, qint64> nearestTurns;
    qint64 previousId = 0, maxId = 0;
    for (const EventRecord &event : journal.events) {
        if (event.id <= previousId || !eventKind(event.kind)
            || (event.status != QLatin1String("active") && event.status != QLatin1String("finished"))
            || (event.status == QLatin1String("active") ? !event.outcome.isEmpty() : !terminalOutcome(event.outcome)))
            return fail("invalid event identity, order or status");
        if (event.parentId && (event.parentId >= event.id || !events.contains(event.parentId)))
            return fail("invalid event ancestry");
        events.insert(event.id, &event);
        nearestTurns.insert(event.id, event.kind == QLatin1String("turn")
            ? event.id : nearestTurns.value(event.parentId));
        previousId = event.id;
        maxId = event.id;
    }
    auto validScope = [&events](qint64 id, const char *kind) {
        const auto it = events.constFind(id);
        return id == 0 || (it != events.cend() && it.value()->kind == QLatin1String(kind));
    };
    auto scopes = [&validScope](qint64 round, qint64 turn, qint64 phase) {
        return validScope(round, "round") && validScope(turn, "turn") && validScope(phase, "phase");
    };
    QSet<qint64> active;
    for (qint64 id : journal.active) {
        const auto it = events.constFind(id);
        if (active.contains(id) || it == events.cend() || it.value()->kind != QLatin1String("round")
            || it.value()->status != QLatin1String("active")) return fail("unsafe active scope");
        active.insert(id);
    }
    if (journal.roundScopeId) {
        const auto it = events.constFind(journal.roundScopeId);
        if (it == events.cend() || it.value()->kind != QLatin1String("round")
            || it.value()->status != QLatin1String("active")) return fail("invalid round scope");
    }
    for (const EventRecord &event : journal.events) {
        if (!validEventReferences(event.data, events) || !scopes(event.roundId, event.turnId, event.phaseId)
            || event.turnId > event.id || event.phaseId > event.id
            || (event.kind != QLatin1String("turn") && event.roundId > event.id)
            || (event.kind == QLatin1String("round") && event.roundId != event.id)
            || (event.kind == QLatin1String("turn") && event.turnId != event.id)
            || (event.kind == QLatin1String("phase") && event.phaseId != event.id))
            return fail("invalid event scope references");
        const EventRecord *parent = events.value(event.parentId, nullptr);
        if (event.kind == QLatin1String("turn")) {
            // Inserted turns keep causal ancestry but start their own range.
            if (event.phaseId != 0) return fail("turn cannot inherit a phase");
        } else if (event.kind == QLatin1String("phase")) {
            if (event.turnId != nearestTurns.value(event.parentId))
                return fail("phase belongs to a different turn");
        } else if (event.kind != QLatin1String("round")) {
            if (event.turnId != (parent ? parent->turnId : 0)
                || event.phaseId != (parent ? parent->phaseId : 0))
                return fail("event does not inherit its parent scope");
        }
        if (event.status == QLatin1String("active") && !active.contains(event.id)
            && event.id != journal.roundScopeId) return fail("untracked active event");
    }
    previousId = 0;
    qint64 previousSequence = 0;
    for (const FactRecord &fact : journal.facts) {
        if (fact.id <= previousId || events.contains(fact.id) || !factKind(fact.kind)
            || !validEventReferences(fact.data, events)
            || fact.sequence != previousSequence + 1 || !events.contains(fact.eventId)
            || fact.eventId >= fact.id || !scopes(fact.roundId, fact.turnId, fact.phaseId)
            || fact.roundId >= fact.id || fact.turnId >= fact.id || fact.phaseId >= fact.id)
            return fail("invalid fact identity, order or scope references");
        const EventRecord *owner = events.value(fact.eventId);
        if (fact.turnId != owner->turnId || fact.phaseId != owner->phaseId)
            return fail("fact belongs to a different event scope");
        // Round is deliberately not compared: beginRound can update the first
        // turn's header after earlier facts were emitted, without rewriting them.
        previousId = fact.id;
        previousSequence = fact.sequence;
        maxId = qMax(maxId, fact.id);
    }
    // Every ID is allocated exactly once; rejecting holes also detects silently
    // dropped history records in a purportedly complete takeover checkpoint.
    if (maxId == std::numeric_limits<qint64>::max()
        || journal.nextId != maxId + 1
        || journal.nextId - 1 != journal.events.size() + journal.facts.size()
        || journal.nextSequence != previousSequence + 1) return fail("invalid history counters");
    return true;
}

static QVariant persistentPayload(const QVariant &value, const QString &key = QString())
{
    // Only these metadata IDs cross JSON as decimal strings. Card IDs and skill
    // instance IDs retain their existing value types and interpretation.
    if (key == QLatin1String("cause_event_id") || key == QLatin1String("execution_id")) {
        qint64 id = 0;
        // Preserve malformed input so strict restore rejects it, rather than
        // laundering a floating-point ID into a seemingly precise string.
        return parseScopeId(value, &id) ? QVariant(idString(id)) : value;
    }
    if (value.typeId() == QMetaType::QVariantMap) {
        QVariantMap map = value.toMap();
        for (auto it = map.begin(); it != map.end(); ++it)
            it.value() = persistentPayload(it.value(), it.key());
        return map;
    }
    if (value.typeId() == QMetaType::QVariantList) {
        QVariantList list = value.toList();
        for (QVariant &item : list) item = persistentPayload(item);
        return list;
    }
    return value;
}

static QVariantList recordsToVariant(const Records<EventRecord> &events)
{
    QVariantList result;
    for (const EventRecord &event : events) {
        QVariantMap map = eventMap(event);
        map.insert(QStringLiteral("data"), persistentPayload(event.data));
        result.append(map);
    }
    return result;
}

static void remapPlayers(QVariant &value, const QMap<QString, QString> &mapping, const QString &key = QString())
{
    if (value.typeId() == QMetaType::QVariantMap) {
        QVariantMap map = value.toMap();
        for (auto it = map.begin(); it != map.end(); ++it) remapPlayers(it.value(), mapping, it.key());
        value = map;
    } else if (value.typeId() == QMetaType::QVariantList) {
        QVariantList list = value.toList(); for (QVariant &item : list) remapPlayers(item, mapping, key); value = list;
    } else if (value.typeId() == QMetaType::QString && mapping.contains(value.toString())
               && (key == QLatin1String("from") || key == QLatin1String("to") || key == QLatin1String("player")
                   || key == QLatin1String("target") || key == QLatin1String("actor") || key == QLatin1String("owner")
                   || key == QLatin1String("skill_owner") || key == QLatin1String("player_id")
                   || key == QLatin1String("targets") || key == QLatin1String("players")
                   || key == QLatin1String("source") || key == QLatin1String("invoker")
                   || key == QLatin1String("activation_owner") || key == QLatin1String("reason_player")
                   || key == QLatin1String("first_player") || key == QLatin1String("source_owner"))) {
        value = mapping.value(value.toString());
    }
}

} // namespace

struct ResolutionHistorySnapshot::Data : public QSharedData {
    QSharedPointer<Journal> journal;
};

struct ResolutionHistoryService::Data : public QSharedData {
    QSharedPointer<Journal> journal = QSharedPointer<Journal>::create();
    Index events, facts;
    Data() = default;
    Data(const Data &other) : QSharedData(other), journal(QSharedPointer<Journal>::create(*other.journal)), events(other.events), facts(other.facts) {}
    void rebuild() {
        events = Index(); facts = Index();
        for (qsizetype i = 0; i < journal->events.size(); ++i) events.add(journal->events.at(i), i, journal->events.at(i).id);
        for (qsizetype i = 0; i < journal->facts.size(); ++i) facts.add(journal->facts.at(i), i, journal->facts.at(i).eventId);
    }
};

ResolutionHistorySnapshot::ResolutionHistorySnapshot() : d(new Data)
{
    auto journal = QSharedPointer<Journal>::create(); journal->complete = false; d->journal = journal;
}

ResolutionHistorySnapshot::ResolutionHistorySnapshot(const QSharedDataPointer<Data> &data) : d(data) {}
ResolutionHistorySnapshot::ResolutionHistorySnapshot(const ResolutionHistorySnapshot &other) = default;
ResolutionHistorySnapshot &ResolutionHistorySnapshot::operator=(const ResolutionHistorySnapshot &other) = default;
ResolutionHistorySnapshot::~ResolutionHistorySnapshot() = default;

QVariantMap ResolutionHistorySnapshot::serialize() const
{
    QVariantMap result;
    if (!d || !d->journal) { result.insert(QStringLiteral("complete"), false); return result; }
    result.insert(QStringLiteral("version"), 1);
    result.insert(QStringLiteral("complete"), isComplete());
    result.insert(QStringLiteral("next_id"), idString(d->journal->nextId));
    result.insert(QStringLiteral("next_sequence"), idString(d->journal->nextSequence));
    result.insert(QStringLiteral("round_scope_id"), idString(d->journal->roundScopeId));
    result.insert(QStringLiteral("events"), recordsToVariant(d->journal->events));
    QVariantList facts;
    for (const FactRecord &fact : d->journal->facts) {
        QVariantMap map = factMap(fact);
        map.insert(QStringLiteral("data"), persistentPayload(fact.data));
        facts.append(map);
    }
    result.insert(QStringLiteral("facts"), facts);
    QVariantList active; for (qint64 id : d->journal->active) active.append(idString(id));
    result.insert(QStringLiteral("active"), active);
    return result;
}

bool ResolutionHistorySnapshot::deserialize(const QVariantMap &serialized, ResolutionHistorySnapshot *snapshot, QString *error)
{
    auto fail = [error](const char *message) {
        if (error) *error = QString::fromLatin1(message);
        return false;
    };
    qint64 version = 0;
    const QVariant versionValue = serialized.value(QStringLiteral("version"));
    const bool supportedVersion = versionValue.typeId() == QMetaType::Double
        ? versionValue.toDouble() == 1.0 : parseId(versionValue, &version) && version == 1;
    if (!snapshot || !supportedVersion
        || serialized.value(QStringLiteral("complete")).typeId() != QMetaType::Bool
        || !serialized.value(QStringLiteral("complete")).toBool()) return fail("unsupported or incomplete snapshot");
    for (const QString &key : {QStringLiteral("events"), QStringLiteral("facts"), QStringLiteral("active")})
        if (serialized.value(key).typeId() != QMetaType::QVariantList) return fail("invalid snapshot collection");
    auto journal = QSharedPointer<Journal>::create();
    if (!parseId(serialized.value(QStringLiteral("next_id")), &journal->nextId)
        || !parseId(serialized.value(QStringLiteral("next_sequence")), &journal->nextSequence)
        || !parseScopeId(serialized.value(QStringLiteral("round_scope_id")), &journal->roundScopeId))
        return fail("invalid snapshot counters or round scope");
    auto readScopes = [](const QVariantMap &map, qint64 *round, qint64 *turn, qint64 *phase) {
        return parseScopeId(map.value(QStringLiteral("round_id")), round)
            && parseScopeId(map.value(QStringLiteral("turn_id")), turn)
            && parseScopeId(map.value(QStringLiteral("phase_id")), phase);
    };
    for (const QVariant &value : serialized.value(QStringLiteral("events")).toList()) {
        if (value.typeId() != QMetaType::QVariantMap) return fail("invalid event object");
        const QVariantMap map = value.toMap();
        EventRecord record;
        if (!parseId(map.value(QStringLiteral("id")), &record.id)
            || !parseScopeId(map.value(QStringLiteral("parent_id")), &record.parentId)
            || !readScopes(map, &record.roundId, &record.turnId, &record.phaseId)
            || map.value(QStringLiteral("kind")).typeId() != QMetaType::QString
            || map.value(QStringLiteral("status")).typeId() != QMetaType::QString
            || map.value(QStringLiteral("outcome")).typeId() != QMetaType::QString
            || map.value(QStringLiteral("data")).typeId() != QMetaType::QVariantMap)
            return fail("invalid event fields");
        record.kind = map.value(QStringLiteral("kind")).toString();
        record.status = map.value(QStringLiteral("status")).toString();
        record.outcome = map.value(QStringLiteral("outcome")).toString();
        bool valid = false;
        record.data = cleanMap(map.value(QStringLiteral("data")).toMap(), &valid);
        if (!valid) return fail("invalid event payload");
        journal->events.append(record);
    }
    for (const QVariant &value : serialized.value(QStringLiteral("facts")).toList()) {
        if (value.typeId() != QMetaType::QVariantMap) return fail("invalid fact object");
        const QVariantMap map = value.toMap();
        FactRecord record;
        if (!parseId(map.value(QStringLiteral("id")), &record.id)
            || !parseId(map.value(QStringLiteral("sequence")), &record.sequence)
            || !parseId(map.value(QStringLiteral("event_id")), &record.eventId)
            || !readScopes(map, &record.roundId, &record.turnId, &record.phaseId)
            || map.value(QStringLiteral("kind")).typeId() != QMetaType::QString
            || map.value(QStringLiteral("data")).typeId() != QMetaType::QVariantMap)
            return fail("invalid fact fields");
        record.kind = map.value(QStringLiteral("kind")).toString();
        bool valid = false;
        record.data = cleanMap(map.value(QStringLiteral("data")).toMap(), &valid);
        if (!valid) return fail("invalid fact payload");
        journal->facts.append(record);
    }
    for (const QVariant &value : serialized.value(QStringLiteral("active")).toList()) {
        qint64 id = 0;
        if (!parseId(value, &id)) return fail("invalid active scope");
        journal->active.append(id);
    }
    if (!validateJournal(*journal, error)) return false;
    auto data = QSharedDataPointer<Data>(new Data);
    data->journal = journal;
    *snapshot = ResolutionHistorySnapshot(data);
    return true;
}

bool ResolutionHistorySnapshot::isComplete() const
{
    if (!d || !d->journal || !d->journal->complete || d->journal->contextEventId) return false;
    for (qint64 activeId : d->journal->active) {
        auto it = std::find_if(d->journal->events.cbegin(), d->journal->events.cend(), [activeId](const EventRecord &event) { return event.id == activeId; });
        if (it == d->journal->events.cend() || it->kind != QLatin1String("round")) return false;
    }
    return true;
}

bool ResolutionHistorySnapshot::remapPlayerIds(const QMap<QString, QString> &mapping, QString *error)
{
    if (!isComplete()) { if (error) *error = QStringLiteral("snapshot is incomplete"); return false; }
    auto journal = QSharedPointer<Journal>::create(*d->journal);
    for (qsizetype i = 0; i < journal->events.size(); ++i) { EventRecord event = journal->events.at(i); QVariant value = event.data; remapPlayers(value, mapping); event.data = value.toMap(); journal->events.set(i, event); }
    for (qsizetype i = 0; i < journal->facts.size(); ++i) { FactRecord fact = journal->facts.at(i); QVariant value = fact.data; remapPlayers(value, mapping); fact.data = value.toMap(); journal->facts.set(i, fact); }
    auto data = QSharedDataPointer<Data>(new Data); data->journal = journal; d = data; return true;
}

ResolutionHistoryService::ResolutionHistoryService() : d(new Data) {}
ResolutionHistoryService::~ResolutionHistoryService() = default;

qint64 ResolutionHistoryService::beginEvent(const QString &kind, const QVariantMap &data)
{
    d.detach();
    Journal &j = *d->journal;
    const Scope scope = scopeFor(j, d->events);
    bool valid = false;
    const QVariantMap clean = cleanMap(data, &valid);
    if (!valid || !eventKind(kind) || j.nextId == std::numeric_limits<qint64>::max()) {
        j.complete = false; return 0;
    }
    EventRecord event;
    event.id = j.nextId++;
    event.parentId = j.active.isEmpty() ? 0 : j.active.constLast();
    event.kind = kind;
    event.roundId = scope.roundId;
    event.turnId = scope.turnId;
    event.phaseId = scope.phaseId;
    event.data = clean;
    if (kind == QLatin1String("round")) event.roundId = event.id;
    if (kind == QLatin1String("turn")) { event.turnId = event.id; event.phaseId = 0; }
    if (kind == QLatin1String("phase")) event.phaseId = event.id;
    d->events.add(event, j.events.size(), event.id);
    j.events.append(event);
    j.active.append(event.id);
    return event.id;
}

void ResolutionHistoryService::finishEvent(qint64 id, const QString &outcome)
{
    if (!terminalOutcome(outcome)) return;
    d.detach(); Journal &j = *d->journal;
    const auto found = d->events.byId.constFind(id);
    if (found == d->events.byId.cend()) return;
    EventRecord record = j.events.at(found.value());
    if (record.status != QLatin1String("active")) return;
    record.status = QStringLiteral("finished"); record.outcome = outcome;
    j.events.set(found.value(), record);
    j.active.removeAll(id);
    if (j.roundScopeId == id) j.roundScopeId = 0;
}

void ResolutionHistoryService::updateEvent(qint64 id, const QVariantMap &data)
{
    d.detach();
    bool valid = false; const QVariantMap clean = cleanMap(data, &valid);
    if (!valid) { d->journal->complete = false; return; }
    const auto found = d->events.byId.constFind(id);
    if (found == d->events.byId.cend()) return;
    EventRecord record = d->journal->events.at(found.value());
    for (auto it = clean.cbegin(); it != clean.cend(); ++it) record.data.insert(it.key(), it.value());
    d->journal->events.set(found.value(), record);
}

qint64 ResolutionHistoryService::appendFact(qint64 eventId, const QString &kind, const QVariantMap &data)
{
    d.detach();
    Journal &j = *d->journal;
    if (!d->events.byId.contains(eventId)) return 0;
    bool valid = false;
    const QVariantMap clean = cleanMap(data, &valid);
    if (!valid || !factKind(kind) || j.nextId == std::numeric_limits<qint64>::max()
        || j.nextSequence == std::numeric_limits<qint64>::max()) {
        j.complete = false; return 0;
    }
    const Scope scope = scopeFor(j, d->events);
    FactRecord fact;
    fact.id = j.nextId++;
    fact.sequence = j.nextSequence++;
    fact.eventId = eventId;
    fact.kind = kind;
    fact.roundId = scope.roundId;
    fact.turnId = scope.turnId;
    fact.phaseId = scope.phaseId;
    fact.data = clean;
    d->facts.add(fact, j.facts.size(), fact.eventId);
    j.facts.append(fact);
    return fact.id;
}

qint64 ResolutionHistoryService::beginRound(const QVariantMap &data)
{
    d.detach();
    Journal &j = *d->journal;
    bool valid = false;
    const QVariantMap clean = cleanMap(data, &valid);
    if (!valid || j.nextId == std::numeric_limits<qint64>::max()) {
        j.complete = false; return 0;
    }
    // A broken last turn can bypass RoundEnd. Close only the history lifecycle;
    // do not replay game callbacks or leave an orphan active round in snapshots.
    if (j.roundScopeId) finishEvent(j.roundScopeId, QStringLiteral("interrupted"));
    EventRecord round;
    round.id = j.nextId++;
    round.parentId = j.active.isEmpty() ? 0 : j.active.constLast();
    round.kind = QStringLiteral("round");
    round.roundId = round.id;
    round.data = clean;
    d->events.add(round, j.events.size(), round.id);
    j.events.append(round);
    j.roundScopeId = round.id;
    // Round boundaries occur inside the first turn. Only its header changes;
    // previously emitted facts retain the scope in which they occurred.
    const Scope scope = scopeFor(j, d->events);
    if (scope.turnId) {
        const qsizetype position = d->events.byId.value(scope.turnId);
        EventRecord turn = j.events.at(position);
        if (turn.status == QLatin1String("active")) {
            d->events.byRound[turn.roundId].removeAll(position);
            turn.roundId = round.id;
            auto &positions = d->events.byRound[round.id];
            positions.insert(std::lower_bound(positions.begin(), positions.end(), position), position);
            j.events.set(position, turn);
        }
    }
    return round.id;
}

void ResolutionHistoryService::endRound(const QString &outcome)
{
    const qint64 id = d->journal->roundScopeId;
    if (!id || !terminalOutcome(outcome)) return;
    finishEvent(id, outcome);
    d.detach(); d->journal->roundScopeId = 0;
}
qint64 ResolutionHistoryService::currentEventId() const { return d->journal->active.isEmpty() ? 0 : d->journal->active.constLast(); }

QVariantMap ResolutionHistoryService::event(qint64 id) const
{
    const auto found = d->events.byId.constFind(id);
    return found == d->events.byId.cend() ? QVariantMap() : eventMap(d->journal->events.at(found.value()));
}

QVariantMap ResolutionHistoryService::findParent(qint64 id, const QString &kind, bool includeSelf) const
{
    qint64 current = id;
    bool first = true;
    while (current) {
        const auto found = d->events.byId.constFind(current);
        if (found == d->events.byId.cend()) return {};
        const EventRecord &record = d->journal->events.at(found.value());
        if ((!first || includeSelf) && record.kind == kind) return eventMap(record);
        first = false;
        current = record.parentId;
    }
    return {};
}

namespace {
struct Query {
    qint64 after = 0, watermark = 0;
    int limit = 100;
};

static bool parseQuery(const QVariantMap &filter, qint64 maximum, bool facts, Query *query)
{
    static const QSet<QString> fields = {QStringLiteral("kind"), QStringLiteral("round_id"),
        QStringLiteral("turn_id"), QStringLiteral("phase_id"), QStringLiteral("from"),
        QStringLiteral("to"), QStringLiteral("player"), QStringLiteral("skill_owner"),
        QStringLiteral("skill_name"), QStringLiteral("after"), QStringLiteral("limit"),
        QStringLiteral("watermark")};
    for (auto it = filter.cbegin(); it != filter.cend(); ++it) {
        if (!fields.contains(it.key()) && !(facts && it.key() == QLatin1String("event_id"))) return false;
        if (it.key().endsWith(QLatin1String("_id")) || it.key() == QLatin1String("after")
            || it.key() == QLatin1String("watermark") || it.key() == QLatin1String("limit")) {
            qint64 value = 0;
            if (!it.value().isValid() || it.value().isNull() || !parseScopeId(it.value(), &value)) return false;
            if (it.key() == QLatin1String("after")) query->after = value;
            if (it.key() == QLatin1String("limit")) {
                if (value <= 0 || value > std::numeric_limits<int>::max()) return false;
                query->limit = int(value);
            }
        } else {
            if (it.value().typeId() != QMetaType::QString) return false;
            if (it.key() == QLatin1String("kind")
                && !(facts ? factKind(it.value().toString()) : eventKind(it.value().toString()))) return false;
        }
    }
    query->watermark = maximum;
    if (filter.contains(QStringLiteral("watermark"))) {
        if (!parseScopeId(filter.value(QStringLiteral("watermark")), &query->watermark)
            || query->watermark > maximum) return false;
    }
    return true;
}

static const QVector<qsizetype> *queryCandidates(const Index &index, const QVariantMap &filter)
{
    static const QVector<qsizetype> empty;
    const QVector<qsizetype> *best = nullptr;
    auto consider = [&best](const QVector<qsizetype> *candidate) {
        if (!best || candidate->size() < best->size()) best = candidate;
    };
    if (filter.contains(QStringLiteral("kind"))) {
        auto it = index.byKind.constFind(filter.value(QStringLiteral("kind")).toString());
        consider(it == index.byKind.cend() ? &empty : &it.value());
    }
    const std::array<std::pair<QString, const QHash<qint64, QVector<qsizetype>> *>, 4> scopes = {{
        {QStringLiteral("round_id"), &index.byRound}, {QStringLiteral("turn_id"), &index.byTurn},
        {QStringLiteral("phase_id"), &index.byPhase}, {QStringLiteral("event_id"), &index.byEvent}}};
    for (const auto &scope : scopes) {
        if (!filter.contains(scope.first)) continue;
        qint64 id = 0; parseScopeId(filter.value(scope.first), &id);
        auto it = scope.second->constFind(id);
        consider(it == scope.second->cend() ? &empty : &it.value());
    }
    return best;
}

static QVariantMap queryResult(const QString &alias, const QVariantList &items,
                               const Query &query, bool more, bool complete,
                               bool attributionComplete, bool valid, const QString &cursorKey)
{
    QVariantMap result{{alias, items}, {QStringLiteral("items"), items},
        {QStringLiteral("has_more"), more}, {QStringLiteral("complete"), complete && valid},
        {QStringLiteral("attribution_complete"), attributionComplete && valid},
        {QStringLiteral("watermark"), idString(query.watermark)},
        {QStringLiteral("next_after"), items.isEmpty() ? QVariant(idString(query.after))
            : items.constLast().toMap().value(cursorKey)}};
    if (!valid) result.insert(QStringLiteral("error"), QStringLiteral("invalid_query"));
    return result;
}

template<typename T, typename Cursor, typename Mapper>
static QVariantMap runQuery(const Records<T> &records, const Index &index,
                            const QVariantMap &filter, qint64 maximum, bool complete,
                            bool facts, Cursor cursor, Mapper mapper)
{
    Query query;
    const QString alias = facts ? QStringLiteral("facts") : QStringLiteral("events");
    const QString cursorKey = facts ? QStringLiteral("sequence") : QStringLiteral("id");
    if (!parseQuery(filter, maximum, facts, &query))
        return queryResult(alias, {}, query, false, false, false, false, cursorKey);
    const auto *candidates = queryCandidates(index, filter);
    const qsizetype size = candidates ? candidates->size() : records.size();
    // Index lists are append ordered. Seek directly past the cursor so paging
    // never repeatedly walks all previous records in the selected scope.
    qsizetype low = 0, high = size;
    while (low < high) {
        const qsizetype middle = low + (high - low) / 2;
        const auto &record = records.at(candidates ? candidates->at(middle) : middle);
        if (cursor(record) <= query.after) low = middle + 1;
        else high = middle;
    }
    QVariantList result;
    bool more = false, attributionComplete = complete;
    for (qsizetype i = low; i < size; ++i) {
        const auto &record = records.at(candidates ? candidates->at(i) : i);
        if (cursor(record) > query.watermark) break;
        if (!matchText(filter, QStringLiteral("kind"), record.kind)
            || !matchScope(filter, QStringLiteral("round_id"), record.roundId)
            || !matchScope(filter, QStringLiteral("turn_id"), record.turnId)
            || !matchScope(filter, QStringLiteral("phase_id"), record.phaseId)) continue;
        const QVariantMap mapped = mapper(record);
        if (!matchScope(filter, QStringLiteral("event_id"), mapped.value(QStringLiteral("event_id")).toLongLong())) continue;
        // Unknown source attribution cannot become a proof of absence merely
        // because the caller requested a specific owner or skill.
        if (record.data.contains(QStringLiteral("attribution_complete"))
            && !record.data.value(QStringLiteral("attribution_complete")).toBool()) attributionComplete = false;
        if (!matchAttribution(filter, record.data)) continue;
        if (result.size() >= query.limit) { more = true; break; }
        result.append(mapped);
    }
    return queryResult(alias, result, query, more, complete, attributionComplete, true, cursorKey);
}
} // namespace

QVariantMap ResolutionHistoryService::queryEvents(const QVariantMap &filter) const
{
    return runQuery(d->journal->events, d->events, filter, d->journal->nextId - 1,
        d->journal->complete, false, [](const EventRecord &record) { return record.id; }, eventMap);
}

QVariantMap ResolutionHistoryService::queryFacts(const QVariantMap &filter) const
{
    return runQuery(d->journal->facts, d->facts, filter, d->journal->nextSequence - 1,
        d->journal->complete, true, [](const FactRecord &record) { return record.sequence; }, factMap);
}

QVariantMap ResolutionHistoryService::currentScopes() const
{
    const Scope scope = scopeFor(*d->journal, d->events);
    return {{QStringLiteral("round_id"), idString(scope.roundId)},
        {QStringLiteral("turn_id"), idString(scope.turnId)},
        {QStringLiteral("phase_id"), idString(scope.phaseId)}};
}

ResolutionHistorySnapshot ResolutionHistoryService::snapshot() const
{
    auto data = QSharedDataPointer<ResolutionHistorySnapshot::Data>(new ResolutionHistorySnapshot::Data);
    // Journal copies only persistent roots, counters and the small active stack.
    // The growing query indexes remain owned exclusively by the live service.
    data->journal = QSharedPointer<Journal>::create(*d->journal);
    return ResolutionHistorySnapshot(data);
}

bool ResolutionHistoryService::restore(const ResolutionHistorySnapshot &snapshot, QString *error)
{
    if (d->journal->contextEventId || !snapshot.isComplete()) {
        if (error) *error = QStringLiteral("snapshot or live cleanup context is not at a safe boundary");
        return false;
    }
    if (!snapshot.d || !snapshot.d->journal || !validateJournal(*snapshot.d->journal, error)) return false;
    d.detach();
    d->journal = QSharedPointer<Journal>::create(*snapshot.d->journal);
    d->rebuild();
    return true;
}

struct ResolutionHistoryContextGuard::Data {
    QSharedPointer<Journal> journal;
    QVector<qint64> previousActive;
    qint64 previousContext = 0;
};

ResolutionHistoryContextGuard::ResolutionHistoryContextGuard(ResolutionHistoryService &service,
                                                             qint64 eventId, bool enabled)
{
    if (!enabled) return;
    service.d.detach();
    const auto found = service.d->events.byId.constFind(eventId);
    if (found == service.d->events.byId.cend()) return;
    d = std::make_unique<Data>();
    d->journal = service.d->journal;
    d->previousActive = d->journal->active;
    d->previousContext = d->journal->contextEventId;
    // Keep causal ancestry available to findParent without reopening the event.
    d->journal->active = {eventId};
    d->journal->contextEventId = eventId;
}

ResolutionHistoryContextGuard::~ResolutionHistoryContextGuard() noexcept
{
    if (!d) return;
    // Unbalanced cleanup cannot silently leave an apparently safe checkpoint.
    if (d->journal->active.size() != 1
        || d->journal->active.constFirst() != d->journal->contextEventId)
        d->journal->complete = false;
    d->journal->active.swap(d->previousActive);
    d->journal->contextEventId = d->previousContext;
}

ResolutionHistoryEventGuard::ResolutionHistoryEventGuard(ResolutionHistoryService &history,
                                                         const QString &kind,
                                                         const QVariantMap &data,
                                                         bool enabled)
    : service(enabled ? &history : nullptr),
      eventId(enabled ? history.beginEvent(kind, data) : 0),
      uncaughtAtConstruction(std::uncaught_exceptions())
{
}

ResolutionHistoryEventGuard::~ResolutionHistoryEventGuard()
{
    if (!service || finished)
        return;
    service->finishEvent(eventId, std::uncaught_exceptions() > uncaughtAtConstruction
                                      ? QStringLiteral("aborted")
                                      : QStringLiteral("completed"));
}

qint64 ResolutionHistoryEventGuard::id() const { return eventId; }

void ResolutionHistoryEventGuard::update(const QVariantMap &data)
{
    if (service && !finished)
        service->updateEvent(eventId, data);
}

void ResolutionHistoryEventGuard::finish(const QString &outcome)
{
    if (service && !finished) {
        service->finishEvent(eventId, outcome);
        finished = true;
    }
}
