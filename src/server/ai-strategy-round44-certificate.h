#ifndef AI_STRATEGY_ROUND44_CERTIFICATE_H
#define AI_STRATEGY_ROUND44_CERTIFICATE_H
#include <QString>
class Room; class ServerPlayer; struct AIRequest; struct AIWorldView;
// Only the two audited native copy sites may instantiate this lexical alias:
// runAnswer's legacy-hook hint and resolveConversionProposal's readonly Play.
// Lua cannot register aliases or provide a certificate.
class AIRound44RangeRequestAlias final {
public:
    AIRound44RangeRequestAlias(const AIRequest &original,const AIRequest &copy);
    ~AIRound44RangeRequestAlias();
    AIRound44RangeRequestAlias(const AIRound44RangeRequestAlias&)=delete;
    AIRound44RangeRequestAlias&operator=(const AIRound44RangeRequestAlias&)=delete;
    static const AIRequest *canonical(const AIRequest &);
    static const AIWorldView *canonical(const AIWorldView &);
private:
    const AIRequest *m_original,*m_copy;
    AIRound44RangeRequestAlias *m_previous;
    static thread_local AIRound44RangeRequestAlias *s_current;
};
bool aiRound44RangeWorldCurrent(Room &,const AIWorldView &);
// No JSON/Lua certificate, request restamp, new action ticket or payment right.
void beginAIRound44RangeDecision(Room &, const AIRequest &, const void *luaRequest = nullptr);
bool aiRound44RangeLuaRequestCurrent(Room &, const AIRequest &, const void *);
void finishAIRound44RangeDecision(Room &, const AIRequest &);
void invalidateAIRound44RangeDecision(Room &);
bool aiRound44RangeRequestCurrent(Room &, const AIRequest &, const ServerPlayer * = nullptr);
// Active native policy session only. Unlike RequestCurrent, this denies
// finished certificates retained solely for final native result admission.
bool aiRound44RangeDecisionActive(Room &, const AIRequest &, const ServerPlayer * = nullptr);
bool aiRound44RangeContinuation(Room &, const AIRequest &);
bool aiRound44RangeSource(const QString &flag, const QString &source);
// Runtime-only call at the original reached setFlags sites, never a generic host setter.
bool writeAIRound44RangeFlag(Room &, const AIRequest &, ServerPlayer *,
    const QString &flag, const QString &source, bool value, QString *error = nullptr);
#endif
