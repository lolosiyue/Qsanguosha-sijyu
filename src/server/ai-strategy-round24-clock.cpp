#include "ai-strategy-round24-clock.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "game-rng.h"
#include "structs.h"
#include "util.h"
#include <QJsonArray>
#include <cmath>
#include <ctime>
namespace {
QJsonObject failure(const char *message) { return {{"error", message}, {"key", "source_compatibility"}}; }
}
AIRound24SourceSession::AIRound24SourceSession(Room &room, const AIRequest &request,
 int trigger, const QString &kind) : m_room(room), m_request(request),
 m_viewer(request.viewerObjectName), m_eventKind(kind), m_decision(request.decisionId),
 m_revision(request.stateRevision), m_trigger(trigger) {}
bool AIRound24SourceSession::current() const {
 return m_request.viewerObjectName == m_viewer && m_request.decisionId == m_decision
  && m_request.stateRevision == m_revision && m_room.roomRuntime()->stateRevision() == m_revision
  && m_room.findPlayerByObjectName(m_viewer, true);
}
bool AIRound24SourceSession::gaoda() const {
 return m_trigger < 0 && m_request.kind == AIRequest::UseCard
  && m_request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
  && m_request.pattern == "@@gaoda_zhanshi";
}
bool AIRound24SourceSession::replayClock(const QVector<ClockRead> &reads) {
 if (!m_reads.isEmpty() || m_replaying || reads.size() > 64) return false;
 for (const auto &read : reads) if (read.seconds < 0 || read.seconds > Q_INT64_C(9007199254740991)) return false;
 m_replay = reads; m_replaying = true; return true;
}
QJsonObject AIRound24SourceSession::clock(const QString &key) {
 if (!current()) return failure("source clock invocation expired");
 const bool shuffle = key == "gaoda:shuffle:zhanshi" && gaoda();
 const bool chat = (key == "chat:PreCardUsed:CardUse" && m_trigger == PreCardUsed && m_eventKind == "card_use")
  || (key == "chat:EventPhaseStart:luanwu" && m_trigger == EventPhaseStart && m_eventKind == "other");
 if ((!shuffle && !chat) || m_reads.size() >= 64) return failure("source clock key or bounded domain unavailable");
 if (shuffle && (m_shuffleReady || m_nextUpper != 0)) return failure("source shuffle clock order differs");
 qint64 seconds;
 if (m_replaying) {
  if (m_reads.size() >= m_replay.size() || m_replay.at(m_reads.size()).key != key)
   return failure("source clock replay order differs");
  seconds = m_replay.at(m_reads.size()).seconds;
 } else {
  const std::time_t sampled = std::time(nullptr);
  if (sampled == std::time_t(-1)) return failure("native source clock unavailable");
  seconds = qint64(sampled);
 }
 if (seconds < 0 || seconds > Q_INT64_C(9007199254740991)) return failure("source clock value outside exact Lua integer range");
 m_reads.append({key, seconds});
 if (shuffle) m_shuffleReady = true;
 return {{"kind", "source_clock"}, {"source_key", key}, {"seconds", double(seconds)}};
}
QJsonObject AIRound24SourceSession::gaodaRandom(GameRng &aiRng, int upper) {
 if (!current() || !gaoda() || !m_shuffleReady
  || (m_nextUpper == 0 ? (upper < 8 || upper > 11) : upper != m_nextUpper))
  return failure("Gaoda shuffle random order or source unavailable");
 // Fixed engine util.cpp190–219: randomseed only checks an integer; it does
 // not seed. Preserve random(i)'s fraction transform using the AI RNG only.
 const double random = double(aiRng.bounded(0x7fffffff)) / double(0x7fffffff);
 const int value = int(std::floor(random * upper)) + 1;
 m_nextUpper = upper - 1;
 if (upper == 2) { m_shuffleReady = false; m_nextUpper = -1; }
 return {{"kind", "source_gaoda_random"}, {"upper", upper}, {"value", value}};
}
QJsonObject AIRound24SourceSession::numericCosts(const QString &package, const QString &text) const {
 if (!current() || (package != "gaoda" && package != "sfoflCard")) return failure("numeric source invocation unavailable");
 if (text.toUtf8().size() > 4096 || text.contains(QChar(0))) return failure("numeric source text outside bounded domain");
 const QStringList pieces = text.split('+');
 if (pieces.size() > 128) return failure("numeric source cost count exceeds bound");
 QJsonArray ids;
 for (int id : ListS2I(pieces)) ids.append(id);
 return {{"kind", "source_numeric_costs"}, {"package", package}, {"ids", ids}};
}
