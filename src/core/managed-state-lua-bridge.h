#ifndef QSAN_MANAGED_STATE_LUA_BRIDGE_H
#define QSAN_MANAGED_STATE_LUA_BRIDGE_H

#include <QString>

class LuaRuntime;
namespace GameState { class WorldStore; }

// Installs the data-only managed-state API into one already initialized Lua
// runtime. The Room owns the WorldStore and must keep it alive until after all
// runtimes using this bridge have shut down. Closures pin the runtime identity
// and generation while following LuaRuntime's explicit owner-thread Binding
// handoff. A shared WorldStore still requires the Room to serialize all bound
// game/AI runtimes on the same owner thread.
class ManagedStateLuaBridge
{
public:
    // Adds sgs.ManagedState.get/set/remove/array. Each runtime binds a distinct
    // provider ID but reads and writes the same Room-owned WorldStore root.
    // Install on the runtime's owner thread after the script has created `sgs`.
    static bool install(LuaRuntime &runtime, GameState::WorldStore &store,
                        const QString &providerId, QString *error = nullptr);
};

#endif
