#ifndef MANAGED_REWIND_SYNC_POLICY_H
#define MANAGED_REWIND_SYNC_POLICY_H

#include "core/protocol/session/managed-rewind-payloads.h"

namespace ManagedRewindSyncPolicy {

inline bool needsResyncAfterSnapshot(bool managedTimelineRestore,
                                    const QSanProtocol::RewindStatusPayload &status,
                                    const QString &rootGameId, const QString &worldId,
                                    const QString &generation)
{
    // ManagedRewindLab::networkReport sends the committed snapshot followed
    // by a fresh status for each peer. At sync end the accepted status can
    // still name the previous generation; using its identity for resync would
    // produce a newer stale-request ACK that hides the successful completion.
    if (managedTimelineRestore)
        return false;
    return status.rootGameId != rootGameId
        || status.worldId != worldId
        || status.generation != generation;
}

} // namespace ManagedRewindSyncPolicy

#endif
