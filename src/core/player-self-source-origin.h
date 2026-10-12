#ifndef PLAYER_SELF_SOURCE_ORIGIN_H
#define PLAYER_SELF_SOURCE_ORIGIN_H
// Native source receipt only; never reads Self or a Player. True means that
// this thread's actual static `Self = nullptr` producer has never been followed
// by any compiled engine Self assignment, including equal/null assignments.
// Monotonic for the entire thread lifetime. This is solely an early bootstrap
// premise: it is not authority for a later request or a private player value.
// Complete fixed native/source reachability and all assignment sites must still
// be covered by the caller before preserving this premise through execution.
bool hasAIEngineSelfNullSourceOrigin() noexcept;
#endif
