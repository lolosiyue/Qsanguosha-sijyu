#ifndef QSAN_TRIGGER_CASCADE_BREAK_H
#define QSAN_TRIGGER_CASCADE_BREAK_H

#include <cstdint>

// A cancellation receipt, distinct from turn changes and room termination.
// Only the owning logical operation may consume it after mandatory cleanup.
struct TriggerCascadeBreak {
    std::uint64_t cascadeId;
};

#endif
