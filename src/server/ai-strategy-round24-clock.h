#ifndef AI_STRATEGY_ROUND24_CLOCK_H
#define AI_STRATEGY_ROUND24_CLOCK_H
#include <QJsonObject>
#include <QString>
#include <QVector>
class Room;
class GameRng;
struct AIRequest;
// Invocation-local native state only. No Lua setter or persistent replay API.
class AIRound24SourceSession final {
public:
    struct ClockRead { QString key; qint64 seconds; };
    AIRound24SourceSession(Room &, const AIRequest &, int nativeTrigger = -1,
                          const QString &nativeEventKind = QString());
    QJsonObject clock(const QString &key);
    QJsonObject gaodaRandom(GameRng &aiRng, int upper);
    QJsonObject numericCosts(const QString &package, const QString &text) const;
    bool replayClock(const QVector<ClockRead> &reads);
    const QVector<ClockRead> &clockReads() const { return m_reads; }
private:
    bool current() const;
    bool gaoda() const;
    Room &m_room;
    const AIRequest &m_request;
    QString m_viewer, m_eventKind;
    quint64 m_decision, m_revision;
    int m_trigger;
    QVector<ClockRead> m_reads, m_replay;
    bool m_replaying = false, m_shuffleReady = false;
    int m_nextUpper = 0;
};
#endif
