#ifndef QSAN_STARTUP_TIMING_H
#define QSAN_STARTUP_TIMING_H

#include <QElapsedTimer>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QtGlobal>
#include <cstdio>

// Opt-in synchronous startup diagnostics. Nested phases are inclusive: compare
// siblings, never add an Engine child to its enclosing main.engine duration.
class QSanStartupTiming
{
public:
    explicit QSanStartupTiming(const char *phase, const QString &detail = QString(),
                              bool aggregate = false)
        : m_enabled(qEnvironmentVariableIntValue("QSAN_STARTUP_PROFILE") == 1),
          m_phase(phase), m_detail(detail), m_aggregate(aggregate)
    {
        if (m_enabled) {
            m_timer.start();
            if (!m_aggregate) write("begin", 0);
        }
    }

    ~QSanStartupTiming() { finish(); }
    Q_DISABLE_COPY(QSanStartupTiming)

    void next(const char *phase)
    {
        finish();
        m_phase = phase;
        if (m_enabled) {
            m_timer.start();
            if (!m_aggregate) write("begin", 0);
        }
    }

    void finish()
    {
        if (m_enabled && m_timer.isValid()) {
            const double elapsed = m_timer.nsecsElapsed() / 1000000.0;
            if (m_aggregate) {
                auto &total = totals()[m_phase];
                total.elapsed += elapsed;
                ++total.calls;
            } else {
                write("end", elapsed);
            }
            m_timer.invalidate();
        }
    }

    static void flushAggregates()
    {
        // Hot leaf functions must not flush stderr thousands of times. Keep
        // per-thread totals so profiling never adds a shared runtime lock.
        const auto pending = totals();
        totals().clear();
        for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
            const QJsonObject record{
                {QStringLiteral("phase"), QString::fromLatin1(it.key())},
                {QStringLiteral("event"), QStringLiteral("aggregate")},
                {QStringLiteral("elapsed_ms"), it.value().elapsed},
                {QStringLiteral("calls"), it.value().calls}
            };
            const QByteArray json = QJsonDocument(record).toJson(QJsonDocument::Compact);
            std::fprintf(stderr, "STARTUP_TIMING %s\n", json.constData());
        }
        if (!pending.isEmpty()) std::fflush(stderr);
    }

private:
    struct Total { double elapsed = 0; int calls = 0; };
    static QHash<QByteArray, Total> &totals()
    {
        static thread_local QHash<QByteArray, Total> values;
        return values;
    }

    void write(const char *event, double elapsed) const
    {
        const QJsonObject record{
            {QStringLiteral("phase"), QString::fromLatin1(m_phase)},
            {QStringLiteral("detail"), m_detail},
            {QStringLiteral("event"), QString::fromLatin1(event)},
            {QStringLiteral("elapsed_ms"), elapsed}
        };
        const QByteArray json = QJsonDocument(record).toJson(QJsonDocument::Compact);
        // Flush begin markers too, so an external timeout retains the active phase.
        std::fprintf(stderr, "STARTUP_TIMING %s\n", json.constData());
        std::fflush(stderr);
    }

    const bool m_enabled;
    const char *m_phase;
    QString m_detail;
    const bool m_aggregate;
    QElapsedTimer m_timer;
};

#endif
