#ifndef TRIGGER_DISPATCH_BUDGET_H
#define TRIGGER_DISPATCH_BUDGET_H

#include <cstdint>

// One room, one synchronous root cascade. In particular, popping the event
// stack for a deferred flush must not start a fresh budget. No wall clock is
// used: input, pauses and network waits do not consume dispatch work.
class TriggerDispatchBudget
{
public:
    struct Limits {
        bool enabled = true;
        unsigned depth = 64;
        std::uint64_t events = 1000000;
        std::uint64_t steps = 10000000;
        std::uint64_t contexts = 65536;
    };
    enum class Failure { None, Depth, Events, Steps, Contexts };

    explicit TriggerDispatchBudget(Limits limits) : m_limits(limits) {}
    Failure enter(bool event = true) {
        if (aborted()) return m_failure;
        if (m_depth == 0) m_events = m_steps = 0;
        if (m_limits.enabled && m_depth >= m_limits.depth)
            return fail(Failure::Depth);
        if (event && m_limits.enabled && m_events >= m_limits.events)
            return fail(Failure::Events);
        ++m_depth;
        if (event) ++m_events;
        return Failure::None;
    }
    void leave() { if (m_depth) --m_depth; }
    Failure step() {
        if (aborted()) return m_failure;
        if (m_limits.enabled && m_steps >= m_limits.steps)
            return fail(Failure::Steps);
        ++m_steps;
        return Failure::None;
    }
    Failure checkContexts(std::uint64_t existing) {
        if (aborted()) return m_failure;
        return m_limits.enabled && existing >= m_limits.contexts
            ? fail(Failure::Contexts) : Failure::None;
    }
    bool aborted() const { return m_failure != Failure::None; }
    unsigned depth() const { return m_depth; }
    std::uint64_t events() const { return m_events; }
    std::uint64_t steps() const { return m_steps; }
    const Limits &limits() const { return m_limits; }
    Failure failure() const { return m_failure; }
    void cancel(Failure failure) { if (!aborted()) fail(failure); }
    void resetCascade() { m_failure = Failure::None; m_events = m_steps = 0; }

private:
    Failure fail(Failure failure) { m_failure = failure; return failure; }
    Limits m_limits;
    Failure m_failure = Failure::None;
    unsigned m_depth = 0;
    std::uint64_t m_events = 0;
    std::uint64_t m_steps = 0;
};

#endif
