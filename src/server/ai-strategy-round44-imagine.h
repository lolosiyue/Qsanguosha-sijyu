#ifndef AI_STRATEGY_ROUND44_IMAGINE_H
#define AI_STRATEGY_ROUND44_IMAGINE_H
#include <QString>
class Room; class ServerPlayer; struct AIRequest;
// Diagnostic-only finite Imagine source sinks. Never reads gameplay facts.
bool emitAIRound44ImagineDiagnostic(Room &, const AIRequest &, ServerPlayer *,
                                    const QString &site, const QString &message);
#endif
