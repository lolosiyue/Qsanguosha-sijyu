#include "game-event-stream.h"

#include <QVariant>

QVariantMap GamePresentationEvent::toVariantMap() const
{
    QVariantMap result{{QStringLiteral("generation"), QString::number(generation)},
                       {QStringLiteral("sequence"), QString::number(sequence)},
                       {QStringLiteral("command"), command}, {QStringLiteral("text"), text}};
    if (payload.isValid()) result.insert(QStringLiteral("payload"), payload);
    return result;
}

void GameEventStream::reset(quint64 generation)
{
    m_generation = generation;
    m_nextSequence = 1;
    m_events.clear();
}

quint64 GameEventStream::append(int command, const QString &text, const QVariant &payload)
{
    GamePresentationEvent event{m_generation, m_nextSequence++, command, text, payload};
    m_events.append(event);
    while (m_events.size() > MaximumEvents) m_events.removeFirst();
    return event.sequence;
}

void GameEventStream::synchronize(const ClientGameState &state, quint64 generation)
{
    if (m_generation != generation) reset(generation);
    quint64 sequence = state.firstPresentationEventSequence();
    for (const QVariant &raw : state.presentationEvents()) {
        const QVariantMap value = raw.toMap();
        if (sequence < m_nextSequence) {
            ++sequence;
            continue;
        }
        // A source state may have evicted older events; preserve the source's sequence gap.
        if (sequence > m_nextSequence) m_nextSequence = sequence;
        GamePresentationEvent event{m_generation, sequence,
            value.value(QStringLiteral("command")).toInt(),
            value.value(QStringLiteral("text")).toString(),
            value.value(QStringLiteral("payload"))};
        m_events.append(event);
        m_nextSequence = sequence + 1;
        while (m_events.size() > MaximumEvents) m_events.removeFirst();
        ++sequence;
    }
}

QList<GamePresentationEvent> GameEventStream::since(quint64 sequenceExclusive) const
{
    QList<GamePresentationEvent> result;
    for (const GamePresentationEvent &event : m_events) {
        if (event.sequence > sequenceExclusive) result.append(event);
    }
    return result;
}
