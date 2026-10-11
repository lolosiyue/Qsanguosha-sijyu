#ifndef AI_STRATEGY_ROUND56_HOST_H
#define AI_STRATEGY_ROUND56_HOST_H
#include <QtGlobal>
class QObject;class Room;
// Source-owned registration; neither function runs or replaces a callback.
// Default sink attestation belongs at the reviewed native main entry before
// any message handler installation; it is NOT an arbitrary runtime probe.
void initializeAIRound56DefaultMessageSink() noexcept;
void recordAIRound56ServerMessageSink(bool installed) noexcept;
quint64 aiRound56ServerMessageSinkEpoch() noexcept;
bool proveAIRound56ServerLogEffects(const Room &) noexcept;
class AIRound56ReviewedLogConnect final {
public:
 explicit AIRound56ReviewedLogConnect(const QObject *) noexcept;
 ~AIRound56ReviewedLogConnect();
 static bool matches(const QObject *) noexcept;
private:
 const QObject *m_sender;
 AIRound56ReviewedLogConnect *m_previous;
};
#endif
