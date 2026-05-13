# QSanguosha-v2 Project Map

## Tech Stack
- **Framework**: Qt 5.14 (Widgets, Network, QML/Quick, Multimedia, SQL)
- **Scripting**: Embedded Lua (v5.3+ custom build), integrated via SWIG bindings.
- **Audio**: FMOD Ex (AUDIO_SUPPORT for positional/skill audio).
- **Graphics**: Spine-cpp (Skeleton animations), Qt GraphicsView (Core UI).
- **Core Language**: C++ (MSVC/MinGW on Windows, GCC on Linux/Android).
- **Architecture**: Distributed Client-Server model (Local server used for single-player).

## Architecture Map

### Data Flow
`Frontend (Qt UI) <--> Client (src/client) <--> Protocol (JSON/Socket) <--> Server (src/server) <--> Room/RoomThread <--> Lua (AI/Logic)`

### Core Modules Index
- **Core Entities** ([src/core](file:///l:/finaldebug/QSanguosha-v2/src/core)):
    - `Engine`: Content registry, translation center, and Lua state manager.
    - `Player` / `ServerPlayer` / `ClientPlayer`: State synchronization model.
    - `Card` / `WrappedCard`: Logic and property container for game cards.
    - `Skill`: Base classes for all game techniques (TriggerSkill, ViewAsSkill, etc.).
- **Game Server** ([src/server](file:///l:/finaldebug/QSanguosha-v2/src/server)):
    - `Room`: Game session controller, manages card movements and requests.
    - `RoomThread`: Sequential logic executor for game phases and events.
    - `AI`: Lua-driven decision making.
- **Client/UI** ([src/client](file:///l:/finaldebug/QSanguosha-v2/src/client), [src/ui](file:///l:/finaldebug/QSanguosha-v2/src/ui)):
    - `Client`: Handles server communication and local state.
    - `RoomScene`: The main GraphicsView scene for the game table.
- **Lua Intelligence** ([lua/ai](file:///l:/finaldebug/QSanguosha-v2/lua/ai)):
    - `smart-ai.lua`: Base AI logic and global `sgs` interface.
    - Specialized AI scripts for cards and generals.

### Type/Schema Center
- **Logic Structs**: [src/core/structs.h](file:///l:/finaldebug/QSanguosha-v2/src/core/structs.h) (Damage, CardUse, Pindian, etc.).
- **Protocol Codes**: [src/core/protocol.h](file:///l:/finaldebug/QSanguosha-v2/src/core/protocol.h) (Network packet types).
- **Events**: `TriggerEvent` enum in `structs.h` defines all hookable game moments.

## Implicit Logic & Rules
- **Concurrency Control**: Must use `SafeLuaMutex` or RAII `LuaLocker`/`LuaUnlocker` when C++ threads access the shared `lua_State`.
- **Naming Conventions**: 
    - C++: Classes use `PascalCase`, methods use `camelCase`. Private members use `m_` or `_m_` prefix.
    - Lua: Global table `sgs` is used for all C++ exported interfaces. AI class names in `lua/ai` follow `middleclass` OOP.
- **Game Events**: The project uses an event-priority system. Skills are triggered by `TriggerEvent` hooks.
- **Network Sync**: Property updates (HP, marks, flags) are automated via `Protocol` and JSON synchronization.

## MIMOV2 Constraints
- **Minimize file reads**: Check `STRUCTURE.md` first to locate targets.
- **Short Output**: Do not explain code unless explicitly asked.
