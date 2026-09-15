#ifndef GAME_EVENT_STREAM_H
#define GAME_EVENT_STREAM_H

#include "client-game-state.h"

#include <QList>
#include <QVariantMap>

struct GamePresentationEvent
{
    quint64 generation = 0;
    quint64 sequence = 0;
    int command = 0;
    QString text;
    QVariant payload;

    QVariantMap toVariantMap() const;
};

// Bounded cursor over ClientGameState's existing presentation event source.
class GameEventStream
{
public:
    static constexpr int MaximumEvents = 200;

    quint64 generation() const { return m_generation; }
    quint64 nextSequence() const { return m_nextSequence; }
    void reset(quint64 generation);
    quint64 append(int command, const QString &text, const QVariant &payload = QVariant());
    // Imports the current source window by its sequence numbers; identical event text is safe.
    void synchronize(const ClientGameState &state, quint64 generation);
    QList<GamePresentationEvent> since(quint64 sequenceExclusive) const;
    QList<GamePresentationEvent> events() const { return m_events; }

private:
    quint64 m_generation = 0;
    quint64 m_nextSequence = 1;
    QList<GamePresentationEvent> m_events;
};

#endif
