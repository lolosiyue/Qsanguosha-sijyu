#include "correction-trace.h"
#include "engine.h"
#include "room-runtime.h"
#include "skill.h"
#include "card.h"

std::atomic<int> CorrectionTraceStore::s_enabled{0};
thread_local CorrectionTrace *CorrectionTrace::s_current = nullptr;

CorrectionTraceStore::~CorrectionTraceStore() { close(); }
bool CorrectionTraceStore::anyEnabled() { return s_enabled.load(std::memory_order_relaxed) != 0; }
void CorrectionTraceStore::setEnabled(bool enabled)
{
    QMutexLocker lock(&m_mutex);
    enabled = enabled && !m_closed;
    if (m_enabled.exchange(enabled) == enabled) return;
    s_enabled.fetch_add(enabled ? 1 : -1, std::memory_order_relaxed);
    ++m_generation; // Drop calculations spanning pause/resume.
}
void CorrectionTraceStore::close()
{
    QMutexLocker lock(&m_mutex);
    m_closed = true;
    if (m_enabled.exchange(false)) s_enabled.fetch_sub(1, std::memory_order_relaxed);
    ++m_generation;
}
bool CorrectionTraceStore::closed() const { QMutexLocker lock(&m_mutex); return m_closed; }
void CorrectionTraceStore::clear()
{
    QMutexLocker lock(&m_mutex);
    m_records.clear();
    m_storedRows = 0;
    ++m_generation;
}
QList<CorrectionTraceRecord> CorrectionTraceStore::snapshot() const
{
    QMutexLocker lock(&m_mutex);
    return m_records;
}
quint64 CorrectionTraceStore::begin(quint64 *generation)
{
    QMutexLocker lock(&m_mutex);
    if (!m_enabled.load()) return 0;
    *generation = m_generation;
    return ++m_nextId;
}
void CorrectionTraceStore::append(CorrectionTraceRecord record, quint64 generation)
{
    QMutexLocker lock(&m_mutex);
    if (!record.id || !m_enabled.load() || generation != m_generation) return;
    while (!m_records.isEmpty() && (m_records.size() >= MaxRecords
            || m_storedRows + record.rows.size() > MaxStoredRows)) {
        m_storedRows -= m_records.first().rows.size();
        m_records.removeFirst();
    }
    m_storedRows += record.rows.size();
    m_records.append(std::move(record));
}

CorrectionTrace::CorrectionTrace(const char *category, const Player *source,
                                 const Player *target, const Card *card)
{
    // Disabled hot path: one atomic load; no engine locks or allocations.
    if (!CorrectionTraceStore::anyEnabled() || !Sanguosha) return;
    EngineRuntimeContext *context = Sanguosha->currentRoomContext();
    RoomRuntime *runtime = context ? context->roomRuntime() : nullptr;
    if (!runtime) return; // Never collect the normal client's partial/hidden state.
    m_store = runtime->correctionTraces();
    m_record.id = m_store->begin(&m_generation);
    if (!m_record.id) return;
    m_record.category = QString::fromLatin1(category);
    m_record.revision = runtime->stateRevision();
    m_record.source = source ? CorrectionTraceStore::bounded(source->objectName()) : QString();
    m_record.target = target ? CorrectionTraceStore::bounded(target->objectName()) : QString();
    // Card name and skill context only: do not stringify hidden subcard IDs.
    m_record.card = card ? CorrectionTraceStore::bounded(card->objectName()) : QString();
    if (card) m_record.context = CorrectionTraceStore::bounded(card->getSkillName());
    m_previous = s_current;
    if (m_previous && m_previous->m_store == m_store) m_record.parent = m_previous->m_record.id;
    s_current = this;
}
CorrectionTrace::~CorrectionTrace()
{
    if (!*this) return;
    s_current = m_previous;
    if (m_record.result.isEmpty()) m_record.result = tr("incomplete / unwound");
    m_store->append(std::move(m_record), m_generation);
}
void CorrectionTrace::context(const QString &text)
{
    if (*this) m_record.context = CorrectionTraceStore::bounded(m_record.context + QLatin1Char(' ') + text);
}
void CorrectionTrace::add(CorrectionTraceRow row)
{
    if (!*this) return;
    m_record.addRow(std::move(row));
}
void CorrectionTraceRecord::addRow(CorrectionTraceRow row)
{
    if (rows.size() >= CorrectionTraceStore::MaxRows) {
        ++omitted;
        if (!row.significant) return;
        // Keep later contributing returns visible even in a large skill catalog.
        // Evict an observed no-op/skip first; never invent a row for an uncalled skill.
        int discard = -1;
        for (int i = 0; i < rows.size(); ++i) if (!rows.at(i).significant) { discard = i; break; }
        if (discard < 0) return;
        rows.removeAt(discard);
    }
    row.skill = CorrectionTraceStore::bounded(row.skill);
    row.owner = CorrectionTraceStore::bounded(row.owner);
    row.operation = CorrectionTraceStore::bounded(row.operation);
    row.detail = CorrectionTraceStore::bounded(row.detail);
    rows.append(std::move(row));
}
void CorrectionTrace::note(const QString &operation, int value, const QString &detail)
{
    if (!*this) return;
    CorrectionTraceRow row;
    row.operation = operation; row.value = value; row.detail = detail;
    add(row);
}
void CorrectionTrace::legacy(const Skill *skill, int value, const QString &operation)
{
    if (!*this) return;
    CorrectionTraceRow row;
    row.skill = skill->objectName(); row.version = QStringLiteral("V1");
    row.owner = tr("unknown (callback has no holder)");
    row.operation = operation; row.value = value;
    row.significant = operation == QLatin1String("fixed maximum")
        ? value >= (m_record.category == QLatin1String("Distance correction") ? 1 : 0) : value != 0;
    add(row);
}
void CorrectionTrace::v2(const Skill *skill, const Player *holder, int instance, int amount,
                         const CorrectSkillResult &result, bool fixed)
{
    if (!*this) return;
    CorrectionTraceRow row;
    row.skill = skill->objectName(); row.version = QStringLiteral("V2");
    row.owner = holder ? holder->objectName() : tr("system"); row.instance = instance;
    row.value = result.value;
    row.significant = result.applies;
    row.operation = !result.applies ? QStringLiteral("noEffect")
        : result.unlimited ? QStringLiteral("unlimitedResidue")
        : result.explicitSigned ? QStringLiteral("signedAmount") : QStringLiteral("useAmount / applicable");
    row.detail = tr("%1; currentAmount=%2; applies=%3; unlimited=%4; explicitSigned=%5")
        .arg(fixed ? tr("fixed maximum") : tr("add"))
        .arg(amount).arg(result.applies).arg(result.unlimited).arg(result.explicitSigned);
    add(row);
}
void CorrectionTrace::v2Prohibit(const Skill *skill, const Player *holder, int instance, bool prohibited)
{
    if (!*this) return;
    CorrectionTraceRow row;
    row.skill = skill->objectName(); row.version = QStringLiteral("V2");
    row.owner = holder ? holder->objectName() : tr("system"); row.instance = instance;
    row.operation = QStringLiteral("isProhibited (boolean)"); row.value = int(prohibited);
    row.significant = prohibited;
    add(row);
}
void CorrectionTrace::skipped(const Skill *skill, const QString &reason, const Player *holder, int instance)
{
    if (!*this) return;
    CorrectionTraceRow row;
    row.skill = skill->objectName(); row.owner = holder ? holder->objectName() : QString();
    row.instance = instance; row.operation = QStringLiteral("skipped"); row.detail = reason;
    row.significant = false;
    add(row);
}
int CorrectionTrace::finish(int result, const QString &detail)
{
    if (*this) m_record.result = CorrectionTraceStore::bounded(QString::number(result) + QLatin1Char(' ') + detail);
    return result;
}
void CorrectionTrace::outcome(const QString &result)
{
    if (*this) m_record.result = CorrectionTraceStore::bounded(result);
}
