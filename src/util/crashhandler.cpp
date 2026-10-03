#include "crashhandler.h"

#if defined(QSG_CRASH_HANDLER) && defined(Q_OS_WIN)

// QSG_BUILD_ID is generated from the current Git HEAD by build.ps1 before each build.
// A header change triggers recompilation, keeping the embedded ID aligned with the symbol-package filename.
// See Write-BuildIdHeader in build.ps1.
// Direct qmake builds may lack this file; __has_include falls back to unknown.
#if defined(__has_include)
#  if __has_include("build_id.h")
#    include "build_id.h"
#  endif
#endif
#ifndef QSG_BUILD_ID
#  define QSG_BUILD_ID "unknown"
#endif

#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>
#include <csignal>
#include <cstdlib>
#include <cstdarg>
#include <exception>
#include <new>

// Read the Lua call stack (.lua filename and line) through the Lua C API.
// Lua is always built (CONFIG+=lua is enabled by default in QSanguosha.pro),
// and src/lua is on INCLUDEPATH.
#include "lua.hpp"

#ifndef RRF_RT_REG_SZ
#define RRF_RT_REG_SZ 0x00000002
#endif

namespace {

// Prevent recursion: abandon crash handling if it crashes again.
volatile LONG g_handling = 0;

// Whether normal shutdown has begun (qApp->exec() has returned).
// Lua shutdown runs __gc finalizers, which can call C++ destructors through SWIG.
// An uncaught exception there reaches terminate/SEH during user-requested cleanup,
// not a crash during gameplay, so do not show a crash report.
volatile LONG g_shuttingDown = 0;

// Filled at startup for direct crash-time output; 8 KB is sufficient.
char g_envInfo[8192] = {0};

// Main-thread ID and startup time identify the crashing thread and process uptime.
DWORD g_mainThreadId = 0;
ULONGLONG g_startTick = 0;
wchar_t g_dumpDirectory[MAX_PATH] = L"dmp";
wchar_t g_configPath[MAX_PATH] = L"config.ini";

// Some SDK headers declare GetTickCount64 only with _WIN32_WINNT>=0x0600; resolve it at runtime.
ULONGLONG tickCount64()
{
    typedef ULONGLONG (WINAPI *Fn)(void);
    static Fn fn = (Fn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                      "GetTickCount64");
    return fn ? fn() : GetTickCount();
}

// Absolute path to the current replay file; maintained by setLiveRecordPath, empty outside a game.
wchar_t g_liveRecord[MAX_PATH] = {0};

// Game version, maintained by setVersion. Engine is unavailable during install(), so use a placeholder
// in filenames and summaries if a crash occurs before Engine construction completes.
char g_version[64] = "unknown";

// Window state and game phase at the crash, maintained by setWindowState / setGamePhase.
// Written on the UI thread and read by the crash thread without locking; this best-effort diagnostic
// may be torn, but that does not affect the minidump.
char g_windowState[256] = {0};
int  g_gamePhase = 0; // See CrashHandler::GamePhase.

// UTF-8 game-configuration summary staged by Qt; 16 KB holds translated names for 100+ packages.
// Written on the UI thread and read by the crash thread without locking; a torn read may return an older value.
char g_gameConfig[16384] = {0};

// Current-game data: start tick, player count, and completed rounds.
// Registered at game start by setGamePhase / setGameStats and cleared on return to the lobby.
// Writers (game logic or UI thread) and the crash reader do not lock; a torn read may return an older value.
ULONGLONG g_gameStartTick = 0;
int g_playerCount = 0;
int g_gameRound   = 0;

thread_local void *g_luaState = nullptr;
thread_local DWORD g_luaThreadId = 0;

void appendEnv(const char *fmt, ...)
{
    size_t used = lstrlenA(g_envInfo);
    if (used >= sizeof(g_envInfo) - 1) return;
    va_list args;
    va_start(args, fmt);
    wvsprintfA(g_envInfo + used, fmt, args); // wvsprintfA does not support %f; this is sufficient.
    va_end(args);
}

// Function-pointer type for RegGetValueW, resolved from advapi32 at runtime to avoid an explicit link.
typedef LONG (WINAPI *RegGetValueWFn)(HKEY, LPCWSTR, LPCWSTR, DWORD,
                                      LPDWORD, PVOID, LPDWORD);

// Read a string value from HKLM, convert it to UTF-8, and write it to out; leave out unchanged on failure.
void readRegStr(RegGetValueWFn fn, const wchar_t *subkey,
                const wchar_t *value, char *out, int outBytes)
{
    if (!fn) return;
    wchar_t wbuf[512];
    DWORD cb = sizeof(wbuf);
    if (fn(HKEY_LOCAL_MACHINE, subkey, value, RRF_RT_REG_SZ,
           nullptr, wbuf, &cb) == ERROR_SUCCESS)
        WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, out, outBytes,
                            nullptr, nullptr);
}

// EnumDisplayMonitors callback: report each display's resolution, position, and device name.
// The device name (\\.\DISPLAYn) matches the screen recorded by setWindowState.
BOOL CALLBACK monitorEnumProc(HMONITOR hMon, HDC, LPRECT, LPARAM lp)
{
    int *idx = (int *)lp;
    MONITORINFOEXW mi;
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hMon, (LPMONITORINFO)&mi)) {
        char name8[64] = {0};
        WideCharToMultiByte(CP_UTF8, 0, mi.szDevice, -1, name8, sizeof(name8),
                            nullptr, nullptr);
        appendEnv("显示器 %d: %d x %d @ (%d,%d) %s%s\r\n",
                  ++(*idx),
                  (int)(mi.rcMonitor.right - mi.rcMonitor.left),
                  (int)(mi.rcMonitor.bottom - mi.rcMonitor.top),
                  (int)mi.rcMonitor.left, (int)mi.rcMonitor.top, name8,
                  (mi.dwFlags & MONITORINFOF_PRIMARY) ? " [主]" : "");
    }
    return TRUE;
}

void collectEnvInfo()
{
    appendEnv("==== 环境信息 ====\r\n");
    appendEnv("Build ID: %s\r\n", QSG_BUILD_ID);

    // RtlGetVersion reports the OS version accurately, unlike GetVersionEx on Windows 8.1 and later.
    typedef LONG (WINAPI *RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW osv = {};
    osv.dwOSVersionInfoSize = sizeof(osv);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        RtlGetVersionPtr fn = (RtlGetVersionPtr)(void(*)())GetProcAddress(ntdll, "RtlGetVersion");
        if (fn && fn(&osv) == 0)
            appendEnv("Windows: %d.%d build %d\r\n",
                      (int)osv.dwMajorVersion, (int)osv.dwMinorVersion,
                      (int)osv.dwBuildNumber);
    }

    appendEnv("进程: %d 位\r\n", (int)(sizeof(void *) * 8));

    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    const char *arch;
    switch (si.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64: arch = "x64";  break;
    case PROCESSOR_ARCHITECTURE_INTEL: arch = "x86";  break;
    default:                           arch = "其它"; break;
    }
    appendEnv("CPU 架构: %s,逻辑核心: %d\r\n",
              arch, (int)si.dwNumberOfProcessors);

    MEMORYSTATUSEX mem = {};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem))
        appendEnv("物理内存: %d MB(可用 %d MB)\r\n",
                  (int)(mem.ullTotalPhys / (1024 * 1024)),
                  (int)(mem.ullAvailPhys / (1024 * 1024)));

    int monIdx = 0;
    EnumDisplayMonitors(nullptr, nullptr, monitorEnumProc, (LPARAM)&monIdx);
    HDC hdc = GetDC(nullptr);
    if (hdc) {
        appendEnv("屏幕 DPI: %d\r\n", GetDeviceCaps(hdc, LOGPIXELSX));
        ReleaseDC(nullptr, hdc);
    }

    wchar_t locale[64] = {0};
#ifdef QSAN_XP_LEGACY
    // GetUserDefaultLocaleName is Vista-only.  Build the same language-region
    // shape from APIs exported by Windows XP so the executable has no hard import.
    wchar_t language[16] = {0};
    wchar_t country[16] = {0};
    const LCID userLocale = GetUserDefaultLCID();
    const bool hasLanguage = GetLocaleInfoW(userLocale, LOCALE_SISO639LANGNAME,
        language, sizeof(language) / sizeof(language[0])) > 0;
    const bool hasCountry = GetLocaleInfoW(userLocale, LOCALE_SISO3166CTRYNAME,
        country, sizeof(country) / sizeof(country[0])) > 0;
    if (hasLanguage && hasCountry)
        _snwprintf(locale, sizeof(locale) / sizeof(locale[0]) - 1,
            L"%s-%s", language, country);
#else
    GetUserDefaultLocaleName(locale, 64);
#endif
    if (locale[0] != L'\0') {
        char loc8[128] = {0};
        WideCharToMultiByte(CP_UTF8, 0, locale, -1, loc8, sizeof(loc8), nullptr, nullptr);
        appendEnv("系统区域: %s\r\n", loc8);
    }

    // ---- Hardware model (CPU/GPU/motherboard) and disk capacity ----
    // Resolve Reg* / EnumDisplayDevices at runtime to avoid explicit advapi32/user32 links.
    HMODULE advapi = LoadLibraryW(L"advapi32.dll");
    RegGetValueWFn regGet = advapi
        ? (RegGetValueWFn)(void(*)())GetProcAddress(advapi, "RegGetValueW") : nullptr;
    if (regGet) {
        char cpu[256] = {0};
        readRegStr(regGet, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                   L"ProcessorNameString", cpu, sizeof(cpu));
        if (cpu[0]) appendEnv("CPU: %s\r\n", cpu);

        char mbVendor[128] = {0}, mbProduct[128] = {0};
        readRegStr(regGet, L"HARDWARE\\DESCRIPTION\\System\\BIOS",
                   L"BaseBoardManufacturer", mbVendor, sizeof(mbVendor));
        readRegStr(regGet, L"HARDWARE\\DESCRIPTION\\System\\BIOS",
                   L"BaseBoardProduct", mbProduct, sizeof(mbProduct));
        if (mbVendor[0] || mbProduct[0])
            appendEnv("主板: %s %s\r\n", mbVendor, mbProduct);
    }

    HMODULE user32 = LoadLibraryW(L"user32.dll");
    typedef BOOL (WINAPI *EnumDisplayDevicesWFn)(LPCWSTR, DWORD,
                                                 PDISPLAY_DEVICEW, DWORD);
    EnumDisplayDevicesWFn enumDD = user32
        ? (EnumDisplayDevicesWFn)(void(*)())GetProcAddress(user32, "EnumDisplayDevicesW")
        : nullptr;
    if (enumDD) {
        DISPLAY_DEVICEW dd;
        char lastGpu[256] = {0};
        for (DWORD i = 0; ; ++i) {
            ZeroMemory(&dd, sizeof(dd));
            dd.cb = sizeof(dd);
            if (!enumDD(nullptr, i, &dd, 0)) break;
            if (!(dd.StateFlags & DISPLAY_DEVICE_ACTIVE)) continue; // Skip disabled and mirrored drivers.
            char gpu[256] = {0};
            WideCharToMultiByte(CP_UTF8, 0, dd.DeviceString, -1,
                                gpu, sizeof(gpu), nullptr, nullptr);
            // Multiple displays on one GPU can repeat an adapter; remove adjacent duplicates.
            if (gpu[0] && lstrcmpA(gpu, lastGpu) != 0) {
                appendEnv("显卡: %s\r\n", gpu);
                lstrcpynA(lastGpu, gpu, sizeof(lastGpu));
            }
        }
    }

    // Disk capacity for the game drive (nullptr selects the current working-directory volume).
    ULARGE_INTEGER diskAvail, diskTotal, diskTotalFree;
    if (GetDiskFreeSpaceExW(nullptr, &diskAvail, &diskTotal, &diskTotalFree))
        appendEnv("硬盘(游戏所在盘): 总 %d GB / 可用 %d GB\r\n",
                  (int)(diskTotal.QuadPart / (1024ULL * 1024 * 1024)),
                  (int)(diskTotalFree.QuadPart / (1024ULL * 1024 * 1024)));

    // Executable path, working directory, and command line.
    wchar_t pathw[MAX_PATH] = {0};
    char path8[MAX_PATH * 3] = {0};
    if (GetModuleFileNameW(nullptr, pathw, MAX_PATH)) {
        WideCharToMultiByte(CP_UTF8, 0, pathw, -1, path8, sizeof(path8),
                            nullptr, nullptr);
        appendEnv("程序路径: %s\r\n", path8);
    }
    if (GetCurrentDirectoryW(MAX_PATH, pathw)) {
        WideCharToMultiByte(CP_UTF8, 0, pathw, -1, path8, sizeof(path8),
                            nullptr, nullptr);
        appendEnv("工作目录: %s\r\n", path8);
    }
    char cmd8[1024] = {0};
    if (WideCharToMultiByte(CP_UTF8, 0, GetCommandLineW(), -1,
                            cmd8, sizeof(cmd8), nullptr, nullptr))
        appendEnv("命令行: %s\r\n", cmd8);
}

// Write the crash-file prefix, without an extension, to out as a wide string.
// Example: dmp\crash-20260515-203045-20260420-a1b2c3d
void buildPrefix(wchar_t *out, size_t cch)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
#ifdef QSAN_XP_LEGACY
    _snwprintf_s(out, cch > 32 ? cch - 32 : cch, _TRUNCATE,
              L"%s\\crash-%04d%02d%02d-%02d%02d%02d-%S-%S",
#else
    wsprintfW(out, L"%s\\crash-%04d%02d%02d-%02d%02d%02d-%S-%S",
#endif
              g_dumpDirectory,
              st.wYear, st.wMonth, st.wDay,
              st.wHour, st.wMinute, st.wSecond,
              g_version,         // Game version, registered by setVersion after Engine construction.
              QSG_BUILD_ID);
    (void)cch;
}

// Write a minidump to <prefix>.dmp; synthesize a context when pointers is null.
bool writeMiniDump(const wchar_t *prefix, EXCEPTION_POINTERS *pointers)
{
    wchar_t path[MAX_PATH];
    wsprintfW(path, L"%s.dmp", prefix);

    HANDLE hFile = CreateFileW(path, GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    // abort/terminate paths have no EXCEPTION_POINTERS, so synthesize a context.
    EXCEPTION_RECORD record;
    CONTEXT context;
    EXCEPTION_POINTERS synthesized;
    if (!pointers) {
        ZeroMemory(&record, sizeof(record));
        ZeroMemory(&context, sizeof(context));
        RtlCaptureContext(&context);
        record.ExceptionCode = 0xE0000001; // Custom code for a non-SEH crash.
        synthesized.ExceptionRecord = &record;
        synthesized.ContextRecord = &context;
        pointers = &synthesized;
    }

    MINIDUMP_EXCEPTION_INFORMATION mei;
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = pointers;
    mei.ClientPointers = FALSE;

    BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                                hFile, MiniDumpNormal, &mei, nullptr, nullptr);
    CloseHandle(hFile);
    return ok != FALSE;
}

// Read the preferred link-time ImageBase from a PE file on disk (EXE/DLL).
// Do not read it from the mapped in-memory PE header: with ASLR, the loader replaces
// OptionalHeader.ImageBase with the randomized runtime base. The unchanged disk file
// provides the link base required by addr2line; return 0 on failure.
ULONGLONG diskImageBase(const wchar_t *path)
{
    HANDLE f = CreateFileW(path, GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return 0;

    ULONGLONG base = 0;
    IMAGE_DOS_HEADER dos;
    DWORD got = 0;
    if (ReadFile(f, &dos, sizeof(dos), &got, nullptr) && got == sizeof(dos)
        && dos.e_magic == IMAGE_DOS_SIGNATURE
        && SetFilePointer(f, dos.e_lfanew, nullptr, FILE_BEGIN)
               != INVALID_SET_FILE_POINTER) {
        IMAGE_NT_HEADERS nt;
        if (ReadFile(f, &nt, sizeof(nt), &got, nullptr) && got == sizeof(nt)
            && nt.Signature == IMAGE_NT_SIGNATURE)
            base = nt.OptionalHeader.ImageBase;
    }
    CloseHandle(f);
    return base;
}

// Write the crash summary to <prefix>.txt.
void writeSummary(const wchar_t *prefix, const char *reason,
                  EXCEPTION_POINTERS *pointers)
{
    wchar_t path[MAX_PATH];
    wsprintfW(path, L"%s.txt", prefix);
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return;

    char buf[2048];
    DWORD written = 0;

    DWORD tid = GetCurrentThreadId();
    const char *threadKind = (tid == g_mainThreadId) ? "主线程" : "后台线程";
    unsigned upSec = (unsigned)((tickCount64() - g_startTick) / 1000);
    wsprintfA(buf, "==== 崩溃摘要 ====\r\n崩溃类型: %s\r\n游戏版本: %s\r\n"
                   "崩溃线程: %u(%s)\r\n进程已运行: %u 秒\r\n",
              reason, g_version, (unsigned)tid, threadKind, upSec);
    WriteFile(h, buf, lstrlenA(buf), &written, nullptr);

    // Process memory usage at crash time; resolve K32GetProcessMemoryInfo at runtime to avoid linking psapi.
    typedef BOOL (WINAPI *GetProcMemFn)(HANDLE, PROCESS_MEMORY_COUNTERS *, DWORD);
    GetProcMemFn memFn = (GetProcMemFn)(void(*)())GetProcAddress(
        GetModuleHandleW(L"kernel32.dll"), "K32GetProcessMemoryInfo");
    if (memFn) {
        PROCESS_MEMORY_COUNTERS_EX pmc;
        ZeroMemory(&pmc, sizeof(pmc));
        pmc.cb = sizeof(pmc);
        if (memFn(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&pmc,
                  sizeof(pmc))) {
            wsprintfA(buf, "进程内存: 工作集 %d MB / 私有 %d MB\r\n",
                      (int)(pmc.WorkingSetSize / (1024 * 1024)),
                      (int)(pmc.PrivateUsage / (1024 * 1024)));
            WriteFile(h, buf, lstrlenA(buf), &written, nullptr);
        }
    }

    // Game phase and window state at the time of the crash, recorded by the UI thread.
    const char *phaseStr;
    switch (g_gamePhase) {
    case 1:  phaseStr = "对局中(玩家存活)";              break;
    case 2:  phaseStr = "对局中(玩家已阵亡,AI 接管快进)"; break;
    case 3:  phaseStr = "录像回放";                        break;
    default: phaseStr = "大厅 / 未开局";                   break;
    }
    wsprintfA(buf, "游戏阶段: %s\r\n", phaseStr);
    WriteFile(h, buf, lstrlenA(buf), &written, nullptr);
    if (g_windowState[0]) {
        wsprintfA(buf, "游戏窗口: %s\r\n", g_windowState);
        WriteFile(h, buf, lstrlenA(buf), &written, nullptr);
    }

    // Current-game data (start time, player count, rounds; see setGamePhase / setGameStats).
    // A nonzero g_gameStartTick means the crash occurred during a game or replay.
    if (g_gameStartTick != 0) {
        unsigned gameSec = (unsigned)((tickCount64() - g_gameStartTick) / 1000);
        if (g_playerCount > 0)
            wsprintfA(buf, "本局时长: %u 秒\r\n本局人数: %d 人\r\n"
                           "已进行轮数: 第 %d 轮\r\n",
                      gameSec, g_playerCount, g_gameRound);
        else // Replays and similar contexts may have duration but no player or round count.
            wsprintfA(buf, "本局时长: %u 秒\r\n", gameSec);
        WriteFile(h, buf, lstrlenA(buf), &written, nullptr);
    }

    if (pointers && pointers->ExceptionRecord) {
        void *addr = pointers->ExceptionRecord->ExceptionAddress;
        wsprintfA(buf, "异常码: 0x%08X\r\n崩溃地址: 0x%p\r\n",
                  (unsigned)pointers->ExceptionRecord->ExceptionCode, addr);
        WriteFile(h, buf, lstrlenA(buf), &written, nullptr);

        // Resolve the module, relative offset, and symbolization address for the crash address.
        HMODULE mod = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCWSTR)addr, &mod) && mod) {
            wchar_t modPath[MAX_PATH] = {0};
            GetModuleFileNameW(mod, modPath, MAX_PATH);
            const wchar_t *modName = wcsrchr(modPath, L'\\');
            modName = modName ? modName + 1 : modPath;
            uintptr_t offset = (uintptr_t)addr - (uintptr_t)mod;

            // addr2line expects the PE link base plus RVA, not the runtime address
            // (the module may have been relocated by ASLR) or the bare RVA. Read the preferred base
            // from the module file on disk: the loader changes the in-memory PE header's ImageBase
            // to the randomized runtime base. If reading from disk fails, use the memory base,
            // matching the previous behavior; this remains correct without ASLR (as in Qt 5's mingw730 build).
            ULONGLONG linkBase = diskImageBase(modPath);
            if (linkBase == 0)
                linkBase = (ULONGLONG)mod;
            ULONGLONG va = linkBase + offset;

            wsprintfA(buf, "崩溃模块: %S\r\n模块内偏移: 0x%IX\r\n"
                           "符号化地址: 0x%I64X\r\n"
                           "(符号化: addr2line -e <模块>.debug -f -C -i 0x%I64X)\r\n",
                      modName, offset, va, va);
            WriteFile(h, buf, lstrlenA(buf), &written, nullptr);
        }
    } else {
        // No exception context: abort/terminate/SIGABRT or a user-submitted hang report.
        // Include the reason directly instead of adding another branch.
        char nabuf[256];
        wsprintfA(nabuf, "异常: 无(%s)\r\n", reason);
        WriteFile(h, nabuf, lstrlenA(nabuf), &written, nullptr);
    }

    WriteFile(h, "\r\n", 2, &written, nullptr);
    WriteFile(h, g_envInfo, lstrlenA(g_envInfo), &written, nullptr);

    // Game-configuration summary staged by Qt; see stashGameConfigForCrash in settings.cpp.
    // Omit the title when empty, such as during an early crash before initialization.
    if (g_gameConfig[0]) {
        const char *hdr = "\r\n==== 游戏配置 ====\r\n";
        WriteFile(h, hdr, lstrlenA(hdr), &written, nullptr);
        WriteFile(h, g_gameConfig, lstrlenA(g_gameConfig), &written, nullptr);
        WriteFile(h, "\r\n", 2, &written, nullptr);
    }
    CloseHandle(h);
}

// Copy the working directory's config.ini to dmp\<prefix>-config.ini for diagnosis.
// Ignore failures: config.ini may not exist on a first launch, and the copy may fail because of
// sharing or permissions. CrashHandler runs from the deployment directory, where Settings reads config.ini.
// config.ini is the same file Settings reads and writes.
void copyConfigIni(const wchar_t *prefix)
{
    wchar_t dst[MAX_PATH];
    wsprintfW(dst, L"%s-config.ini", prefix);
    CopyFileW(g_configPath, dst, FALSE);
}

// Append the crashing thread's Lua call stack (.lua filename and line) to the open summary file h.
// Native dumps and stack traces show only the C call stack; Lua functions all execute inside
// luaV_execute, so the current Lua source line exists in Lua's CallInfo chain, not the C stack.
// Read it through the Lua debug API.
// lua_getstack / lua_getinfo traverse existing state without allocating or executing Lua.
void writeLuaStack(HANDLE h)
{
    lua_State *L = (lua_State *)g_luaState;
    char buf[600];
    DWORD written = 0;

    const char *hdr =
        "\r\n==== Lua 调用栈(崩溃线程)====\r\n"
        "(原生栈只到 Lua 解释器为止;以下行号系崩溃时回查 Lua 调试信息得到)\r\n";
    WriteFile(h, hdr, lstrlenA(hdr), &written, nullptr);

    lua_Debug ar;
    int shown = 0;
    for (int level = 0; level < 100 && lua_getstack(L, level, &ar); ++level) {
        if (!lua_getinfo(L, "Sln", &ar))
            break;
        // Lua provides a function name only when it can infer one from the call site; calls through
        // pcall / the C API may have none, so report the function-definition line instead.
        if (ar.currentline >= 0) { // Lua frame with source file and line.
            if (ar.name && ar.name[0])
                wsprintfA(buf, "  #%d  %s:%d  函数 %s\r\n",
                          level, ar.short_src, ar.currentline, ar.name);
            else
                wsprintfA(buf, "  #%d  %s:%d  (函数定义于第 %d 行)\r\n",
                          level, ar.short_src, ar.currentline, ar.linedefined);
        } else {                   // C frame (SWIG wrapper / Lua library function), already present in the native stack.
            const char *name = (ar.name && ar.name[0]) ? ar.name : "?";
            wsprintfA(buf, "  #%d  [C]  %s\r\n", level, name);
        }
        WriteFile(h, buf, lstrlenA(buf), &written, nullptr);
        ++shown;
    }
    if (shown == 0) {
        const char *none = "  (空 —— 崩溃时不在 Lua 执行中)\r\n";
        WriteFile(h, none, lstrlenA(none), &written, nullptr);
    }
}

// Append the Lua call stack to the summary only when this is the thread that registered
// g_luaState; see its comment.
void appendLuaStack(const wchar_t *prefix)
{
    if (!g_luaState || GetCurrentThreadId() != g_luaThreadId)
        return;

    wchar_t path[MAX_PATH];
    wsprintfW(path, L"%s.txt", prefix);
    HANDLE h = CreateFileW(path, FILE_APPEND_DATA, 0, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return;
    writeLuaStack(h);
    CloseHandle(h);
}

// All capture paths converge here; pointers may be null for abort/terminate paths.
void handleCrash(const char *reason, EXCEPTION_POINTERS *pointers)
{
    // Do not report crashes during normal shutdown cleanup; see g_shuttingDown.
    if (g_shuttingDown)
        return;

    if (InterlockedExchange(&g_handling, 1) != 0)
        return;

    CreateDirectoryW(g_dumpDirectory, nullptr);

    wchar_t prefix[MAX_PATH];
    buildPrefix(prefix, MAX_PATH);

    writeMiniDump(prefix, pointers);
    writeSummary(prefix, reason, pointers);
    copyConfigIni(prefix);

    // Append the Lua call stack last: reading Lua debug state has a very small risk of another crash
    // if the original crash damaged Lua memory. The minidump and summary are already saved,
    // so those results remain available if this step fails.
    appendLuaStack(prefix);
}

LONG WINAPI sehFilter(EXCEPTION_POINTERS *pointers)
{
    handleCrash("SEH", pointers);
    return EXCEPTION_EXECUTE_HANDLER;
}

void terminateHandler()
{
    handleCrash("terminate", nullptr);
    _exit(3);
}

void sigabrtHandler(int)
{
    handleCrash("SIGABRT", nullptr);
    _exit(3);
}

} // namespace

namespace CrashHandler {

void install()
{
#ifdef QSAN_XP_LEGACY
    // Cache native paths before a crash: the handler must not allocate through
    // Qt, and an installation/CD directory is not a writable diagnostics root.
    wchar_t root[160] = {0};
    const DWORD length = GetEnvironmentVariableW(L"QSAN_USER_DATA_ROOT", root, 160);
    if (!length || length >= 160) GetTempPathW(160, root);
    CreateDirectoryW(root, nullptr);
    wsprintfW(g_dumpDirectory, L"%s\\dmp", root);
    const DWORD configLength = GetEnvironmentVariableW(L"QSAN_XP_SETTINGS", g_configPath, MAX_PATH);
    if (!configLength || configLength >= MAX_PATH)
        wsprintfW(g_configPath, L"%s\\config.ini", root);
#endif
    g_mainThreadId = GetCurrentThreadId();
    g_startTick = tickCount64();
    SetUnhandledExceptionFilter(sehFilter);
    std::set_terminate(terminateHandler);
    signal(SIGABRT, sigabrtHandler);
    // Do not call _set_abort_behavior: MinGW's msvcrt import library lacks this symbol,
    // and sigabrtHandler calls _exit before the system abort dialog could appear.
    collectEnvInfo();
}

void setVersion(const char *version)
{
    if (version && version[0])
        lstrcpynA(g_version, version, sizeof(g_version));
}

void beginShutdown()
{
    InterlockedExchange(&g_shuttingDown, 1);
}

void setLiveRecordPath(const wchar_t *path)
{
    if (path && path[0])
        lstrcpynW(g_liveRecord, path, MAX_PATH);
    else
        g_liveRecord[0] = 0;
}

void setGamePhase(GamePhase phase)
{
    g_gamePhase = (int)phase;
    if (phase == PhaseLobby) {
        // On return to the lobby, clear current-game data so a later lobby crash cannot report stale values.
        g_gameStartTick = 0;
        g_playerCount = 0;
        g_gameRound = 0;
    } else if (g_gameStartTick == 0) {
        // Record the game or replay start time for crash-time duration reporting.
        g_gameStartTick = tickCount64();
    }
}

void setGameStats(int playerCount, int round)
{
    if (playerCount > 0)
        g_playerCount = playerCount;
    if (round >= 0)
        g_gameRound = round;
}

void setLuaState(void *L)
{
    g_luaState = L;
    g_luaThreadId = L ? GetCurrentThreadId() : 0;
}

void setWindowState(int x, int y, int w, int h, const wchar_t *screenName)
{
    char name8[64] = {0};
    if (screenName && screenName[0])
        WideCharToMultiByte(CP_UTF8, 0, screenName, -1, name8, sizeof(name8),
                            nullptr, nullptr);
    wsprintfA(g_windowState, "%d x %d @ (%d,%d)%s%s",
              w, h, x, y, name8[0] ? "  屏幕: " : "", name8);
}

void setGameConfig(const char *utf8)
{
    if (utf8 && utf8[0])
        lstrcpynA(g_gameConfig, utf8, sizeof(g_gameConfig));
    else
        g_gameConfig[0] = 0;
}

void reportHang()
{
    // User-triggered hang report, similar to handleCrash, but:
    //   - Do not lock g_handling, so a later real crash can still be reported. A local once
    //     flag prevents re-entry into this function and is cleared before it returns.
    //   - Do not exit the process; the user closes the window after handling the hang.
    //   - There is no exception context; writeMiniDump synthesizes one for nullptr.
    // Design: docs/specs/2026-05-20-hang-report-and-crash-config-design.md
    static volatile LONG once = 0;
    if (InterlockedExchange(&once, 1) != 0)
        return;

    if (!g_shuttingDown) {
        CreateDirectoryW(g_dumpDirectory, nullptr);

        wchar_t prefix[MAX_PATH];
        buildPrefix(prefix, MAX_PATH);

        writeMiniDump(prefix, nullptr);
        writeSummary(prefix, "卡死(玩家手动上报)", nullptr);
        copyConfigIni(prefix);
    }

    InterlockedExchange(&once, 0);
}

const char *buildId()
{
    return QSG_BUILD_ID;
}

void selfTest(const char *type)
{
    if (lstrcmpA(type, "av") == 0) {
        volatile int *p = nullptr;
        *p = 1;
    } else if (lstrcmpA(type, "abort") == 0) {
        abort();
    } else if (lstrcmpA(type, "throw") == 0) {
        throw std::runtime_error("crashtest: uncaught exception");
    }
}

} // namespace CrashHandler

#else  // Empty implementation on non-Windows builds or when crash handling is disabled.

namespace CrashHandler {
void install() {}
void setVersion(const char *) {}
void beginShutdown() {}
void setLiveRecordPath(const wchar_t *) {}
void setGamePhase(GamePhase) {}
void setGameStats(int, int) {}
void setLuaState(void *) {}
void setWindowState(int, int, int, int, const wchar_t *) {}
void setGameConfig(const char *) {}
void reportHang() {}
const char *buildId() { return "unknown"; }
void selfTest(const char *) {}
}

#endif
