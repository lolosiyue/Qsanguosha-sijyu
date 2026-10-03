#ifndef QSG_CRASHHANDLER_H
#define QSG_CRASHHANDLER_H

// Public crash-reporter interface. The implementation is active only on Windows with QSG_CRASH_HANDLER defined;
// every function is otherwise a no-op (see crashhandler.cpp), so callers need no conditional compilation.
namespace CrashHandler {

// Call once near the start of main() to install three crash handlers and cache environment details.
void install();

// Called by main() after Engine construction to register the game version; Engine is unavailable
// during install(). The string is copied internally.
void setVersion(const char *version);

// Called by main() after qApp->exec() returns to mark normal process shutdown.
// Crashes during cleanup (Lua shutdown, __gc finalizers calling C++ destructors through SWIG)
// are not reported because the player already exited and no gameplay was lost.
void beginShutdown();

// Called by Recorder to register the current replay's absolute path (wide string).
// Pass nullptr to clear it when no game is active; the string is copied internally.
void setLiveRecordPath(const wchar_t *path);

// Coarse game phase used to describe the player's activity in a crash summary.
enum GamePhase {
    PhaseLobby = 0,            // Lobby or no game started.
    PhasePlaying = 1,          // Game in progress; player is alive.
    PhaseDeadFastForward = 2,  // Game in progress; player is dead (AI takeover or fast-forward).
    PhaseReplay = 3            // Replay.
};

// Called by the UI when the game phase changes.
// Entering a game or replay records its start time for duration reporting;
// returning to the lobby clears duration, player count, and rounds.
void setGamePhase(GamePhase phase);

// Called by GameRule at game start and at the beginning of each round to record player count and completed rounds.
// Crash summaries use these values; playerCount<=0 leaves the count unchanged,
// round<0 leaves the round unchanged, and setGamePhase clears both in the lobby.
void setGameStats(int playerCount, int round);

// Called at the start of RoomThread::run() to register the Lua state for the game-logic thread
// (void* avoids exposing lua_State in this widely included header).
// If a crash occurs on that thread, append its Lua call stack (.lua filename and line)
// to the summary; native stack traces stop at the Lua interpreter (luaV_execute),
// while the current Lua source location lives in the interpreter's own state.
void setLuaState(void *L);

// Called by the main window on resize/move to record its position, size, and screen at crash time.
// screenName is the display device name (for example, \.\DISPLAY1) and may be nullptr; only the formatted result is stored.
void setWindowState(int x, int y, int w, int h, const wchar_t *screenName);

// Called by Qt while its state is healthy (at the end of Settings::init() or ServerDialog acceptance)
// to copy a preformatted UTF-8 configuration summary into a fixed 16 KB buffer.
// Crash/hang reporting writes it directly; the handler uses only Win32 APIs and avoids
// Engine/Config to prevent a second crash. A null or empty utf8 clears the staged summary.
void setGameConfig(const char *utf8);

// User-triggered hang report from the Help menu. Creates a full-thread minidump,
// summary, and config.ini, then launches crashreporter.exe. Unlike handleCrash, it does not
// exit the process or lock g_handling, so a later crash can still be reported; a local
// once flag prevents re-entry. The user may trigger it repeatedly;
// Each click creates a separate report.
void reportHang();

// Return this build's ID (QSG_BUILD_ID or "unknown" when unavailable).
// Used by server logs and other diagnostics; the returned C string has static lifetime.
const char *buildId();

// Trigger a selected crash type for -crashtest validation only.
// type: "av"=access violation, "abort"=abort(), "throw"=uncaught exception.
void selfTest(const char *type);

} // namespace CrashHandler

#endif // QSG_CRASHHANDLER_H
