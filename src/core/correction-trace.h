#ifndef CORRECTION_TRACE_H
#define CORRECTION_TRACE_H

#include <QCoreApplication>
#include <QList>
#include <QMutex>
#include <QString>
#include <atomic>
#include <memory>

class Player;
class Card;
class Skill;
struct CorrectSkillResult;

// Value-only diagnostics for the local runtime. Never serialized into gameplay packets.
struct CorrectionTraceRow {
    QString skill, owner, version, operation, detail;
    int instance = -1;
    int value = 0;
    bool significant = true;
};
struct CorrectionTraceRecord {
    quint64 id = 0, parent = 0, revision = 0;
    QString category, source, target, card, context, result;
    QList<CorrectionTraceRow> rows;
    int omitted = 0;
    void addRow(CorrectionTraceRow row);
};

class CorrectionTraceStore {
public:
    enum { MaxRecords = 128, MaxRows = 256, MaxStoredRows = 4096, MaxText = 256 };
    ~CorrectionTraceStore();
    static bool anyEnabled();
    void setEnabled(bool enabled);
    bool enabled() const { return m_enabled.load(std::memory_order_relaxed); }
    void close();
    bool closed() const;
    void clear();
    QList<CorrectionTraceRecord> snapshot() const;
    quint64 begin(quint64 *generation);
    void append(CorrectionTraceRecord record, quint64 generation);
    static QString bounded(const QString &text) { return text.left(MaxText); }
private:
    mutable QMutex m_mutex;
    std::atomic<bool> m_enabled{false};
    bool m_closed = false;
    quint64 m_nextId = 0, m_generation = 0;
    QList<CorrectionTraceRecord> m_records;
    int m_storedRows = 0;
    static std::atomic<int> s_enabled;
};

// A scope observes values already calculated by its caller. It never queries
// ownership/validity, invokes callbacks, or changes a calculation's control flow.
class CorrectionTrace {
    Q_DECLARE_TR_FUNCTIONS(CorrectionTrace)
public:
    CorrectionTrace(const char *category, const Player *source,
                    const Player *target = nullptr, const Card *card = nullptr);
    ~CorrectionTrace();
    CorrectionTrace(const CorrectionTrace &) = delete;
    CorrectionTrace &operator=(const CorrectionTrace &) = delete;
    explicit operator bool() const { return m_record.id != 0; }
    void context(const QString &text);
    void note(const QString &operation, int value, const QString &detail = QString());
    void legacy(const Skill *skill, int value, const QString &operation);
    void v2(const Skill *skill, const Player *holder, int instance, int amount,
            const CorrectSkillResult &result, bool fixed);
    void v2Prohibit(const Skill *skill, const Player *holder, int instance, bool prohibited);
    void skipped(const Skill *skill, const QString &reason, const Player *holder = nullptr,
                 int instance = -1);
    int finish(int result, const QString &detail = QString());
    void outcome(const QString &result);
private:
    void add(CorrectionTraceRow row);
    std::shared_ptr<CorrectionTraceStore> m_store;
    CorrectionTraceRecord m_record;
    quint64 m_generation = 0;
    CorrectionTrace *m_previous = nullptr;
    static thread_local CorrectionTrace *s_current;
};
#endif
